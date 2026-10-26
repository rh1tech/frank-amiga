/*
 * frank-amiga — slave: Kickstart, extended ROM and disks from the master.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The master reads them from its microSD and uploads them over the link
 * (link_proto.h LINK_OP_UPLOAD_*). Core 1 (the link pump) receives into
 * PSRAM; core 0, if the machine is running, is held at a frame boundary
 * meanwhile so it never reads a half-written ROM or disk.
 *
 * A record of what is in PSRAM (sizes and CRCs) survives a watchdog reset,
 * so BOOT is simply: record the upload, reboot, and start from it — a cold
 * start with nothing left over from the previous machine.
 */
#include "upload.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "board_config.h"
#include "host_link.h"
#include "link_proto.h"
#include "../omega/Floppy.h"

#define REC_MAGIC 0x55504c44u   /* "UPLD" */

typedef struct {
    uint32_t magic;
    uint32_t size[5];           /* by LINK_UP_* target */
    uint32_t crc[5];
    uint32_t boot_flags;        /* LINK_BOOT_*, from the last BOOT */
    uint32_t check;             /* XOR of the above with the magic */
} upload_rec_t;

static volatile upload_rec_t *rec(void) {
    return (volatile upload_rec_t *)(0x15000000u + BOARD_MAP_UPLOAD_REC_OFFSET);
}

static uint32_t rec_check(const volatile upload_rec_t *r) {
    uint32_t x = r->magic;
    for (int i = 0; i < 5; i++) x ^= r->size[i] ^ (r->crc[i] * 2654435761u);
    return x ^ r->boot_flags * 0x9E3779B1u;
}

static uint8_t *target_base(uint32_t target) {
    switch (target) {
    case LINK_UP_ROM: return (uint8_t *)(0x11000000u + BOARD_MAP_ROM_OFFSET);
    case LINK_UP_EXT: return (uint8_t *)(0x11000000u + BOARD_MAP_EXT_ROM_OFFSET);
    case LINK_UP_DF0: return (uint8_t *)(0x11000000u + BOARD_MAP_DF0_ADF_OFFSET);
    case LINK_UP_DF1: return (uint8_t *)(0x11000000u + BOARD_MAP_DF1_ADF_OFFSET);
    default: return NULL;
    }
}

static uint32_t target_max(uint32_t target) {
    return target == LINK_UP_DF0 || target == LINK_UP_DF1 ? FLOPPY_ADF_SIZE : 0x80000u;
}

/* ── Core 0 hold ───────────────────────────────────────────────────────── */
static volatile bool hold_req, held, running;
static volatile int insert_req = -1;      /* drive to (re)insert, or -1 */
static volatile bool eject_only;          /* ... or just to empty */

void upload_core0_frame(void) {
    if (hold_req) {
        held = true;
        while (hold_req) tight_loop_contents();
        held = false;
    }
    static int insert_wait;
    const int d = insert_req;
    if (d == 0 || d == 1) {
        const uint32_t target = d ? LINK_UP_DF1 : LINK_UP_DF0;
        /* Eject first and give the OS time to see the change. */
        if (df[d].hasDisk) {
            floppyInsert(d);          /* toggles: eject */
            insert_wait = 50;          /* 1 s with the drive empty */
        } else if (insert_wait > 0) {
            insert_wait--;
        } else {
            const volatile upload_rec_t *r = rec();
            floppyConnect(d);
            if (!eject_only && r->size[target] == FLOPPY_ADF_SIZE &&
                floppyMountADF(d, target_base(target), FLOPPY_ADF_SIZE)) {
                floppyInsert(d);
                if (!df[d].hasDisk) {   /* never identified (connected late) */
                    df[d].hasDisk = 1;
                    df[d].pra &= 0xFB;
                }
                printf("upload: DF%d inserted\n", d);
            }
            insert_req = -1;
        }
    } else if (d >= 0) {
        insert_req = -1;
    }
}

void upload_set_running(bool on) { running = on; }

static void hold_core0(bool on) {
    if (!running) return;
    hold_req = on;
    /* Core 0 parks at its next frame end. The master is not reading the
     * stream meanwhile, so drop what the ring holds or core 0 would block
     * on a full ring before it got there; the master resynchronises at a
     * frame start afterwards. */
    if (on) {
        while (!held) {
            uint32_t slot;
            const uint32_t n = host_ring_pending(&slot);
            if (n) host_ring_consume(n);
        }
        host_frame_marks_clear();   /* their positions may be a lap old */
    }
}

/* ── Core 1: the link side ─────────────────────────────────────────────── */
static uint32_t up_target, up_size;
static uint8_t bounce[LINK_UPLOAD_CHUNK] __attribute__((aligned(4)));

bool upload_handle(link_session_t *s, uint16_t op) {
    const link_hdr_t *h = link_rx_hdr(s);
    switch (op) {
    case LINK_OP_UPLOAD_BEGIN: {
        const bool ok = target_base(h->arg0) && h->arg1 && h->arg1 <= target_max(h->arg0);
        if (ok) {
            hold_core0(true);
            up_target = h->arg0;
            up_size = h->arg1;
            volatile upload_rec_t *r = rec();
            if (r->magic != REC_MAGIC || r->check != rec_check(r)) {
                memset((void *)r, 0, sizeof *r);
                r->magic = REC_MAGIC;
            }
            r->size[up_target] = 0;       /* invalid until END verifies */
            r->check = rec_check(r);
        }
        link_s_send_ctrl(s, LINK_OP_UPLOAD_BEGIN | 1, ok, 0, NULL, 0);
        return true;
    }
    case LINK_OP_UPLOAD_CHUNK: {
        const uint32_t off = h->arg0, n = h->arg1;
        bool ok = up_target && n && n <= LINK_UPLOAD_CHUNK && off + n <= up_size &&
                  !(n & 3u);
        if (ok) ok = link_s_bulk_recv(s, bounce, n);
        if (ok) memcpy(target_base(up_target) + off, bounce, n);
        link_s_send_ctrl(s, LINK_OP_UPLOAD_CHUNK | 1, ok, off, NULL, 0);
        return true;
    }
    case LINK_OP_UPLOAD_END: {
        uint32_t crc = 0;
        bool ok = false;
        if (up_target) {
            crc = link_crc32(target_base(up_target), up_size);
            ok = crc == h->arg0;
            volatile upload_rec_t *r = rec();
            r->size[up_target] = ok ? up_size : 0;
            r->crc[up_target] = crc;
            r->check = rec_check(r);
        }
        printf("upload: target %lu, %lu bytes, crc %08lx %s\n", (unsigned long)up_target,
               (unsigned long)up_size, (unsigned long)crc, ok ? "ok" : "MISMATCH");
        up_target = 0;
        link_s_send_ctrl(s, LINK_OP_UPLOAD_END | 1, crc, ok, NULL, 0);
        hold_core0(false);
        return true;
    }
    case LINK_OP_BOOT: {
        volatile upload_rec_t *r = rec();
        if (r->magic == REC_MAGIC && r->check == rec_check(r)) {
            r->boot_flags = h->arg0;
            r->check = rec_check(r);
        }
        link_s_send_ctrl(s, LINK_OP_BOOT | 1, 0, 0, NULL, 0);
        printf("upload: boot requested, restarting\n");
        sleep_ms(20);
        watchdog_reboot(0, 0, 0);
        for (;;) tight_loop_contents();
    }
    case LINK_OP_DIRTY: {
        const uint32_t d = h->arg0 & 1u;
        uint8_t map[20];
        uint32_t tracks = 0;
        floppyDirty[d] = 0;     /* before taking the bits: a later write sets it again */
        for (int i = 0; i < 20; i++) {
            map[i] = __atomic_exchange_n(&floppyDirtyTracks[d][i], 0, __ATOMIC_RELAXED);
            tracks += (uint32_t)__builtin_popcount(map[i]);
        }
        link_s_send_ctrl(s, LINK_OP_DIRTY | 1, tracks, 0, map, sizeof map);
        return true;
    }
    case LINK_OP_DOWNLOAD: {
        const uint8_t *base = target_base(h->arg0);
        const bool ok = (h->arg0 == LINK_UP_DF0 || h->arg0 == LINK_UP_DF1) &&
                        h->arg1 < LINK_DISK_TRACKS;
        link_s_send_ctrl(s, LINK_OP_DOWNLOAD | 1, ok, h->arg1, NULL, 0);
        if (ok) link_s_bulk_send(s, base + h->arg1 * LINK_TRACK_BYTES, LINK_TRACK_BYTES);
        return true;
    }
    case LINK_OP_INSERT:
        eject_only = h->arg1 == 0;
        insert_req = (int)h->arg0;
        link_s_send_ctrl(s, LINK_OP_INSERT | 1, 0, 0, NULL, 0);
        return true;
    default:
        return false;
    }
}

/* ── Boot selection ────────────────────────────────────────────────────── */
static bool rec_valid(uint32_t target) {
    const volatile upload_rec_t *r = rec();
    return r->magic == REC_MAGIC && r->check == rec_check(r) && r->size[target] &&
           link_crc32(target_base(target), r->size[target]) == r->crc[target];
}

const uint8_t *upload_rom(uint32_t *size) {
    if (!rec_valid(LINK_UP_ROM)) return NULL;
    *size = rec()->size[LINK_UP_ROM];
    return target_base(LINK_UP_ROM);
}

const uint8_t *upload_ext_rom(void) {
    return rec_valid(LINK_UP_EXT) && rec()->size[LINK_UP_EXT] == 0x80000u
         ? target_base(LINK_UP_EXT) : NULL;
}

uint32_t upload_boot_flags(void) {
    const volatile upload_rec_t *r = rec();
    return r->magic == REC_MAGIC && r->check == rec_check(r) ? r->boot_flags : 0;
}

const uint8_t *upload_df(int drive) {
    const uint32_t t = drive ? LINK_UP_DF1 : LINK_UP_DF0;
    return rec_valid(t) && rec()->size[t] == FLOPPY_ADF_SIZE ? target_base(t) : NULL;
}
