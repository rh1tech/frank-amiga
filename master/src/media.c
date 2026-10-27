/*
 * frank-amiga — master: Kickstart and disks from the microSD to the slave.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Configuration: 0:/amiga/frank-amiga.ini, lines of key=path:
 *   kick=/amiga/kick/kick13.rom     Kickstart (256 or 512 KB)
 *   ext=/amiga/kick/aros-ext.bin    optional extended ROM (AROS)
 *   df0=/amiga/adf/workbench.adf    optional disk in DF0
 *   df1=/amiga/adf/disk2.adf        optional disk in DF1 (connects DF1)
 *   drives=2                        DF1 connected even with no disk
 *   chip=512                        chip RAM in KB: 512, 1024 or 2048
 *   slow=512                        slow RAM at 0xC00000 in KB: 0, 512, 1024
 *   joykeys=arrows                  keyboard joystick in port 2: off, arrows
 *                                   (Right Ctrl/Alt fire), numpad (0 . fire)
 * Uploads go over the link (link_proto.h LINK_OP_UPLOAD_*) with the stream
 * paused; BOOT then makes the slave restart from them.
 */
#include "media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "ff.h"

#include "link_proto.h"
#include "stream.h"
#include "usb_amiga.h"
#include "scp.h"

#define INI_PATH "0:/amiga/frank-amiga.ini"

static link_session_t *sess;
static FATFS fs;
static bool mounted;
static uint8_t buf[LINK_UPLOAD_CHUNK] __attribute__((aligned(4)));

media_config_t media_config;

bool media_mount(void) {
    if (mounted) return true;
    return media_remount();
}

/* A fresh mount (the card may have been swapped: FatFs keeps the old one's
 * FAT and directories). Callers retry once with it when an open fails. */
bool media_remount(void) {
    mounted = false;
    f_unmount("0:");
    FRESULT r = f_mount(&fs, "0:", 1);
    if (r != FR_OK) { printf("media: no microSD (%d)\n", r); return false; }
    mounted = true;
    return true;
}

/* Bitwise CRC-32 (IEEE), resumable: same result as link_crc32(). */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static bool ctrl(uint16_t op, uint32_t a0, uint32_t a1) {
    return link_m_send_ctrl(sess, op, a0, a1, NULL, 0) && link_m_recv_ctrl(sess) &&
           link_rx_hdr(sess)->op == (uint16_t)(op | 1u);
}

/* Flux images in DF0/DF1: their tracks go to the slave as it asks. */
static scp_t scp[2];
static uint16_t track_len[2][SCP_TRACKS];   /* decoded and cached (0: not yet) */
static int bg_next[2];                      /* where the background decoding goes on */
static uint8_t bg_failed[2][SCP_TRACKS / 8];  /* tracks it could not read */

static void cache_reset(int d) {
    memset(track_len[d], 0, sizeof track_len[d]);
    memset(bg_failed[d], 0, sizeof bg_failed[d]);
    bg_next[d] = 0;
}

static bool is_scp(const char *path) {
    const size_t n = strlen(path);
    return n > 4 && !strcasecmp(path + n - 4, ".scp");
}

bool media_upload(uint32_t target, const char *path) {
    if (!media_mount()) return false;

    /* A running slave first parks its emulation at a frame boundary (up to
     * a frame) before it answers; the stream's 20 ms is too tight. */
    const uint32_t saved_timeout = sess->handshake_timeout_us;
    sess->handshake_timeout_us = 200000;
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK &&
        (!media_remount() || f_open(&f, path, FA_READ) != FR_OK)) {
        printf("media: cannot open %s\n", path);
        return false;
    }
    const uint32_t size = (uint32_t)f_size(&f);
    if (!ctrl(LINK_OP_UPLOAD_BEGIN, target, size) || !link_rx_hdr(sess)->arg0) {
        printf("media: slave refused %s (%lu bytes)\n", path, (unsigned long)size);
        f_close(&f);
        sess->handshake_timeout_us = saved_timeout;
        return false;
    }
    uint32_t crc = 0, off = 0;
    const uint64_t t0 = time_us_64();
    bool ok = true;
    while (ok && off < size) {
        UINT got = 0;
        const uint32_t want = size - off < sizeof buf ? size - off : sizeof buf;
        ok = f_read(&f, buf, want, &got) == FR_OK && got == want;
        if (!ok) break;
        crc = crc32_update(crc, buf, got);
        const uint32_t n = (got + 3u) & ~3u;          /* whole words */
        for (uint32_t i = got; i < n; i++) buf[i] = 0;
        ok = link_m_send_ctrl(sess, LINK_OP_UPLOAD_CHUNK, off, n, NULL, 0) &&
             link_m_bulk_send(sess, buf, n) && link_m_recv_ctrl(sess) &&
             link_rx_hdr(sess)->op == (LINK_OP_UPLOAD_CHUNK | 1u) &&
             link_rx_hdr(sess)->arg0;
        off += got;
    }
    f_close(&f);
    ok = ok && ctrl(LINK_OP_UPLOAD_END, crc, 0) && link_rx_hdr(sess)->arg1;
    printf("media: %s -> slave (%lu bytes, crc %08lx) %s in %lu ms\n", path,
           (unsigned long)size, (unsigned long)crc, ok ? "ok" : "FAILED",
           (unsigned long)((time_us_64() - t0) / 1000u));
    sess->handshake_timeout_us = saved_timeout;
    return ok;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) s[--n] = 0;
}

static void prefix_path(char *dst, size_t cap, const char *v) {
    snprintf(dst, cap, "%s%s", v[0] == '0' && v[1] == ':' ? "" : "0:", v);
}

bool media_load_config(void) {
    memset(&media_config, 0, sizeof media_config);
    media_config.chip_kb = 512;
    media_config.slow_kb = 512;
    if (!media_mount()) return false;
    FIL f;
    if (f_open(&f, INI_PATH, FA_READ) != FR_OK &&
        (!media_remount() || f_open(&f, INI_PATH, FA_READ) != FR_OK)) {
        printf("media: no %s\n", INI_PATH);
        return false;
    }
    char line[300];
    while (f_gets(line, sizeof line, &f)) {
        trim(line);
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#' || line[0] == ';') continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "kick")) prefix_path(media_config.kick, sizeof media_config.kick, v);
        else if (!strcmp(k, "ext")) prefix_path(media_config.ext, sizeof media_config.ext, v);
        else if (!strcmp(k, "df0")) prefix_path(media_config.df0, sizeof media_config.df0, v);
        else if (!strcmp(k, "df1")) prefix_path(media_config.df1, sizeof media_config.df1, v);
        else if (!strcmp(k, "drives")) media_config.drive1 = atoi(v) >= 2;
        else if (!strcmp(k, "chip")) media_config.chip_kb = (uint16_t)atoi(v);
        else if (!strcmp(k, "slow")) media_config.slow_kb = (uint16_t)atoi(v);
        else if (!strcmp(k, "joykeys")) media_config.joykeys = usb_amiga_joykeys_parse(v);
    }
    f_close(&f);
    if (media_config.df1[0]) media_config.drive1 = true;
    printf("media: kick=%s ext=%s df0=%s df1=%s%s, %u KB chip + %u KB slow\n", media_config.kick,
           media_config.ext, media_config.df0, media_config.df1,
           media_config.drive1 ? "" : " (no DF1)", media_config.chip_kb, media_config.slow_kb);
    return media_config.kick[0] != 0;
}

/* Writes media_config back to the ini (paths without the "0:" drive), so
 * a Kickstart or disk chosen in the file manager is there after power-off. */
bool media_save_ini(void) {
    if (!media_mount()) return false;
    FIL f;
    if (f_open(&f, INI_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
#define NO_DRIVE(p) ((p)[0] == '0' && (p)[1] == ':' ? (p) + 2 : (p))
    f_printf(&f, "# frank-amiga: Kickstart and disks at power-on (see master/src/media.c)\n");
    if (media_config.kick[0]) f_printf(&f, "kick=%s\n", NO_DRIVE(media_config.kick));
    if (media_config.ext[0]) f_printf(&f, "ext=%s\n", NO_DRIVE(media_config.ext));
    if (media_config.df0[0]) f_printf(&f, "df0=%s\n", NO_DRIVE(media_config.df0));
    if (media_config.df1[0]) f_printf(&f, "df1=%s\n", NO_DRIVE(media_config.df1));
    else if (media_config.drive1) f_printf(&f, "drives=2\n");
#undef NO_DRIVE
    f_printf(&f, "chip=%u\nslow=%u\n", media_config.chip_kb, media_config.slow_kb);
    f_printf(&f, "joykeys=%s\n", usb_amiga_joykeys_name(media_config.joykeys));
    const bool ok = f_close(&f) == FR_OK;
    printf("media: %s %s\n", INI_PATH, ok ? "saved" : "NOT saved");
    return ok;
}

bool media_boot(void) {
    if (!media_config.kick[0]) return false;
    stream_stop();
    bool ok = media_upload(LINK_UP_ROM, media_config.kick);
    if (ok && media_config.ext[0]) ok = media_upload(LINK_UP_EXT, media_config.ext);
    uint32_t flags = media_config.drive1 ? LINK_BOOT_DF1 : 0;
    const uint32_t chip = media_config.chip_kb >= 2048 ? 2u : media_config.chip_kb >= 1024 ? 1u : 0u;
    const uint32_t slow = media_config.slow_kb == 0 ? 1u : media_config.slow_kb >= 1024 ? 2u : 0u;
    flags |= chip << LINK_BOOT_CHIP_SHIFT | slow << LINK_BOOT_SLOW_SHIFT;
    for (int d = 0; d < 2 && ok; d++) {
        const char *path = d ? media_config.df1 : media_config.df0;
        scp_close(&scp[d]);
        cache_reset(d);
        if (!path[0]) continue;
        if (is_scp(path)) {
            if (scp_open(&scp[d], path) || (media_remount() && scp_open(&scp[d], path)))
                flags |= d ? LINK_BOOT_DF1_EXT : LINK_BOOT_DF0_EXT;
        } else if (media_upload(d ? LINK_UP_DF1 : LINK_UP_DF0, path)) {
            flags |= d ? LINK_BOOT_DF1_DISK : LINK_BOOT_DF0_DISK;
        }
    }
    if (ok) ok = ctrl(LINK_OP_BOOT, flags, 0);
    printf("media: boot %s\n", ok ? "sent" : "FAILED");
    return ok;
}

bool media_insert(int drive, const char *path) {
    char *dst = drive ? media_config.df1 : media_config.df0;
    if (strcmp(path, "none") && is_scp(path)) {
        char p[256];
        prefix_path(p, sizeof p, path);
        cache_reset(drive);
        if (!scp_open(&scp[drive], p) && !(media_remount() && scp_open(&scp[drive], p))) {
            printf("media: %s is not an SCP image\n", p);
            return false;
        }
        stream_stop();
        const bool ok = ctrl(LINK_OP_INSERT, (uint32_t)drive, 2);
        stream_start();
        printf("media: DF%d flux image %s %s\n", drive, p, ok ? "inserted" : "FAILED");
        if (ok) {
            snprintf(dst, sizeof media_config.df0, "%s", p);
            if (drive) media_config.drive1 = true;
        }
        return ok;
    }
    scp_close(&scp[drive]);
    if (!strcmp(path, "none")) {             /* eject */
        stream_stop();
        const bool ok = ctrl(LINK_OP_INSERT, (uint32_t)drive, 0);
        stream_start();
        if (ok) dst[0] = 0;
        return ok;
    }
    char p[256];
    prefix_path(p, sizeof p, path);
    stream_stop();
    const bool ok = media_upload(drive ? LINK_UP_DF1 : LINK_UP_DF0, p) &&
                    ctrl(LINK_OP_INSERT, (uint32_t)drive, 1);
    stream_start();
    if (ok) {
        snprintf(dst, sizeof media_config.df0, "%s", p);
        if (drive) media_config.drive1 = true;
    }
    return ok;
}

/* Disk write-back: the tracks the slave wrote go into the ADF files. */
static bool save_drive(int drive) {
    const char *path = drive ? media_config.df1 : media_config.df0;
    if (!ctrl(LINK_OP_DIRTY, (uint32_t)drive, 0)) return false;
    /* A disk decoded from a flux image keeps its writes in memory only. */
    if (is_scp(path)) path = "";
    const uint32_t tracks = link_rx_hdr(sess)->arg0;
    if (!tracks) return true;
    uint8_t map[LINK_DISK_TRACKS / 8];
    memcpy(map, (const uint8_t *)link_rx_hdr(sess) + sizeof(link_hdr_t), sizeof map);
    FIL f;
    const bool open = path[0] && media_mount() &&
        (f_open(&f, path, FA_WRITE) == FR_OK ||
         (media_remount() && f_open(&f, path, FA_WRITE) == FR_OK));
    uint32_t saved = 0;
    for (uint32_t t = 0; t < LINK_DISK_TRACKS; t++) {
        if (!(map[t >> 3] & (1u << (t & 7)))) continue;
        if (!ctrl(LINK_OP_DOWNLOAD, drive ? LINK_UP_DF1 : LINK_UP_DF0, t) ||
            !link_rx_hdr(sess)->arg0 || !link_m_bulk_recv(sess, buf, LINK_TRACK_BYTES))
            break;
        UINT n = 0;
        if (open && f_lseek(&f, t * LINK_TRACK_BYTES) == FR_OK &&
            f_write(&f, buf, LINK_TRACK_BYTES, &n) == FR_OK && n == LINK_TRACK_BYTES)
            saved++;
    }
    if (open) f_close(&f);
    printf("media: DF%d %lu of %lu written tracks saved to %s\n", drive, (unsigned long)saved,
           (unsigned long)tracks, path[0] ? path : "(no file)");
    return saved == tracks;
}

bool media_state(bool load) {
    return ctrl(LINK_OP_STATE, load ? 1u : 0u, 0);
}

/* Decoded flux tracks, cached in master PSRAM (16 KB slots, 2.6 MB a
 * drive): revisits are instant. The rest of the disk is decoded in the
 * background, from the last track asked for on, so most requests find
 * their track ready. */
#define TRACK_CACHE(d, t) ((uint8_t *)(0x11000000u + 0x200000u + (uint32_t)(d) * 0x2A0000u + \
                                       (uint32_t)(t) * 0x4000u))

/* A slot: the MFM, then (from 14 KB on) a timed track's cell table. */
#define TRACK_MFM_MAX 0x3800u
#define TRACK_CELLS(d, t) ((uint16_t *)(TRACK_CACHE(d, t) + TRACK_MFM_MAX))
static uint16_t track_ncells[2][SCP_TRACKS], track_spread[2][SCP_TRACKS];
static scp_cells_t cells;

static void track_store(int d, int t, uint32_t n) {
    memcpy(TRACK_CACHE(d, t), buf, n);
    memcpy(TRACK_CELLS(d, t), cells.ns, cells.n * 2u);
    track_ncells[d][t] = cells.n;
    track_spread[d][t] = cells.spread;
    track_len[d][t] = (uint16_t)n;
}

static uint32_t track_get(int d, int t) {
    if (t < 0 || t >= SCP_TRACKS) return 0;
    if (!track_len[d][t]) {
        const uint32_t n = scp_track(&scp[d], t, buf, TRACK_MFM_MAX, &cells);
        if (!n) return 0;
        track_store(d, t, n);
    }
    return track_len[d][t];
}

/* Bench: decode a track into the cache (if it is not there yet); returns
 * its length, and its cell-table entries and spread (1/1000) in `timing`
 * (entries | spread << 16). The track is at master PSRAM 0x200000 + drive *
 * 0x2A0000 + track * 16 KB (uncached view 0x15000000 + that, over SWD), its
 * cell table 14 KB on. */
uint32_t media_flux_dump(int drive, int track, uint32_t *timing) {
    if (drive < 0 || drive > 1 || !scp[drive].open) return 0;
    const uint32_t n = track_get(drive, track);
    *timing = n ? (uint32_t)track_ncells[drive][track] | (uint32_t)track_spread[drive][track] << 16 : 0;
    return n;
}

/* Bench: a track's raw SCP block into master PSRAM 0x7C0000 (256 KB). */
uint32_t media_scp_raw(int drive, int track) {
    if (drive < 0 || drive > 1 || !scp[drive].open) return 0;
    return scp_raw_track(&scp[drive], track, (uint8_t *)(0x11000000u + 0x7C0000u), 0x40000u);
}

/* The slave's flux-image drive wants a track: send it. */
static void serve_tracks(void) {
    for (int guard = 0; guard < 2; guard++) {
        if (!ctrl(LINK_OP_TRACKREQ, 0, 0)) break;
        const uint32_t d = link_rx_hdr(sess)->arg0, t = link_rx_hdr(sess)->arg1;
        if (d > 1 || !scp[d].open) break;
        const uint64_t t0 = time_us_64();
        const uint32_t n = track_get((int)d, (int)t);
        if (!n) break;
        /* The MFM, then a timed track's cells (padded to 4 bytes). */
        const uint32_t nc = track_ncells[d][t], tb = (nc * 2u + 3u) & ~3u;
        memcpy(buf, TRACK_CACHE(d, t), n);
        memcpy(buf + n, TRACK_CELLS(d, t), nc * 2u);
        const bool ok = link_m_send_ctrl(sess, LINK_OP_TRACKDATA, d | t << 8 | nc << 16, n + tb, NULL, 0) &&
                        link_m_bulk_send(sess, buf, n + tb) && link_m_recv_ctrl(sess) &&
                        link_rx_hdr(sess)->op == (LINK_OP_TRACKDATA | 1u) && link_rx_hdr(sess)->arg0;
        if (!ok) {
            printf("media: DF%lu track %lu not delivered\n", (unsigned long)d, (unsigned long)t);
            break;
        }
        const uint32_t ms = (uint32_t)((time_us_64() - t0) / 1000u);
        if (ms > 50u)   /* a decode the background did not hide */
            printf("media: DF%lu track %lu took %lu ms\n", (unsigned long)d, (unsigned long)t,
                   (unsigned long)ms);
        bg_next[d] = (int)t & ~1;   /* go on from this cylinder */
    }
}

/* One background step: a slice of a track's flux from the card (4 KB, a
 * couple of ms), or its decoding at the end. The stream takes one chunk a
 * main-loop pass, so the steps get a third of the time at most. */
static bool bench_hold;     /* sdhold: the bench stages a file over the cache */
void media_bench_hold(void) { bench_hold = true; }

static void prefetch_step(void) {
    static int bg_d = -1;
    if (bench_hold) return;
    static uint64_t next_us;
    const uint64_t now = time_us_64();
    if (now < next_us) return;
    if (bg_d >= 0 && scp[bg_d].bg_track >= 0) {
        const int t = scp[bg_d].bg_track;
        const uint32_t n = scp_bg_step(&scp[bg_d], 4096u, buf, TRACK_MFM_MAX, &cells);
        next_us = time_us_64() + 2u * (time_us_64() - now);
        if (n && !track_len[bg_d][t]) track_store(bg_d, t, n);
        return;
    }
    bg_d = -1;
    for (int d = 0; d < 2; d++) {
        if (!scp[d].open) continue;
        for (int k = 0; k < SCP_TRACKS; k++) {
            const int t = (bg_next[d] + k) % SCP_TRACKS;
            if (!track_len[d][t] && scp[d].offset[t] && !(bg_failed[d][t >> 3] & (1u << (t & 7)))) {
                if (scp_bg_start(&scp[d], t)) bg_d = d;
                else bg_failed[d][t >> 3] |= (uint8_t)(1u << (t & 7));
                bg_next[d] = t;
                return;
            }
        }
    }
}

void media_poll(void) {
    if (stream_track_wanted) {
        stream_track_wanted = false;
        serve_tracks();
    } else {
        prefetch_step();
    }
    /* Save a second after the slave reports writes: a game's save is a
     * burst of tracks, taken in one go between two stream requests. */
    static uint64_t dirty_since;
    if (!stream_disk_dirty) { dirty_since = 0; return; }
    const uint64_t now = time_us_64();
    if (!dirty_since) { dirty_since = now; return; }
    if (now - dirty_since < 1000000u) return;
    stream_disk_dirty = false;
    dirty_since = 0;
    save_drive(0);
    save_drive(1);
}

/* Bench helper: save `size` bytes of master PSRAM at `offset` as a file. */
bool media_sdput(uint32_t offset, uint32_t size, const char *path) {
    memset(track_len, 0, sizeof track_len);   /* the bench area overlaps the track cache */
    if (!media_mount()) return false;
    char p[256];
    prefix_path(p, sizeof p, path);
    /* Create the parent directories. */
    for (char *q = p + 3; (q = strchr(q, '/')); q++) {
        *q = 0;
        f_mkdir(p);
        *q = '/';
    }
    FIL f;
    if (f_open(&f, p, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        printf("media: cannot create %s\n", p);
        return false;
    }
    /* Uncached alias: the bench writes this area over SWD. */
    const uint8_t *src = (const uint8_t *)(0x15000000u + offset);
    UINT put = 0;
    const bool ok = f_write(&f, src, size, &put) == FR_OK && put == size;
    f_close(&f);
    printf("media: wrote %lu bytes to %s, crc %08lx %s\n", (unsigned long)size, p,
           (unsigned long)crc32_update(0, src, size), ok ? "ok" : "FAILED");
    bench_hold = false;
    return ok;
}

void media_init(link_session_t *s) { sess = s; }
