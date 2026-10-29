/*
 * frank-amiga — master: SuperCard Pro (.scp) flux images, track by track.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An .scp holds each track as flux transition times (16-bit big-endian, in
 * 25 ns ticks times the header's resolution) for one or more revolutions.
 * Amiga DD tracks are MFM at a 2 us bit cell: an interval of n cells is n-1
 * zero bits and a one. When the slave's drive reaches a track, its first
 * revolution is turned into bits with a simple clock that follows the
 * disk's speed and sent as raw MFM (link_proto.h TRACKDATA), so any format
 * works, AmigaDOS or a game's own. The track starts at the index, as on
 * the disk; the slave's disk DMA finds sync words at any bit position.
 */
#include "scp.h"

#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool scp_has(const scp_t *s, int track) {
    if (s->ipf) return ipf_has(s->ipf, track);
    return track >= 0 && track < SCP_TRACKS && s->offset[track];
}

bool scp_open(scp_t *s, const char *path, ipf_t *ipf_index) {
    scp_close(s);
    if (f_open(&s->f, path, FA_READ) != FR_OK) return false;
    uint8_t hdr[16];
    UINT got = 0;
    s->ipf = NULL;
    if (f_read(&s->f, hdr, 4, &got) == FR_OK && got == 4 && !memcmp(hdr, "CAPS", 4)) {
        /* An .ipf: its records indexed once, tracks built from them. */
        s->clmt[0] = sizeof s->clmt / sizeof s->clmt[0];
        s->f.cltbl = s->clmt;
        if (f_lseek(&s->f, CREATE_LINKMAP) != FR_OK) s->f.cltbl = NULL;
        if (!ipf_index || !ipf_index_read(ipf_index, &s->f)) {
            f_close(&s->f);
            return false;
        }
        for (int t = 0; t < SCP_TRACKS; t++) s->offset[t] = ipf_has(ipf_index, t) ? 1u : 0u;
        s->ipf = ipf_index;
        s->bg_track = -1;
        s->open = true;
        return true;
    }
    if (f_lseek(&s->f, 0) != FR_OK ||
        f_read(&s->f, hdr, sizeof hdr, &got) != FR_OK || got != sizeof hdr ||
        memcmp(hdr, "SCP", 3) != 0 ||
        f_read(&s->f, s->offset, sizeof s->offset, &got) != FR_OK || got != sizeof s->offset) {
        f_close(&s->f);
        return false;
    }
    for (int t = 0; t < SCP_TRACKS; t++) s->offset[t] = le32((const uint8_t *)&s->offset[t]);
    /* Seeks to a track then cost no walk along the FAT chain (a 38 MB image
     * spans hundreds of clusters); a file too fragmented for the map seeks
     * the slow way. */
    s->clmt[0] = sizeof s->clmt / sizeof s->clmt[0];
    s->f.cltbl = s->clmt;
    if (f_lseek(&s->f, CREATE_LINKMAP) != FR_OK) s->f.cltbl = NULL;
    s->ticks_ns = 25u * ((uint32_t)hdr[11] + 1u);
    s->bg_track = -1;
    s->open = true;
    return true;
}

void scp_close(scp_t *s) {
    if (s->open) f_close(&s->f);
    s->open = false;
    s->bg_track = -1;
}

/* Flux is read whole into master PSRAM, after the track cache: the card is
 * several times faster at long reads than at small ones. One buffer for a
 * track asked for now, one for the background job. */
#define SCP_FLUX_CAP 0x40000u
static uint8_t *const flux_now = (uint8_t *)(0x11000000u + 0x740000u);
static uint8_t *const flux_bg = (uint8_t *)(0x11000000u + 0x780000u);

/* A track's header: where its first revolution's flux is, and how long. */
static bool track_flux(scp_t *s, int track, uint32_t *at, uint32_t *bytes) {
    if (!s->open || track < 0 || track >= SCP_TRACKS || !s->offset[track]) return false;
    uint8_t th[16];
    UINT got = 0;
    const uint32_t off = s->offset[track];
    if (f_lseek(&s->f, off) != FR_OK || f_read(&s->f, th, sizeof th, &got) != FR_OK ||
        got != sizeof th || memcmp(th, "TRK", 3) != 0)
        return false;
    const uint32_t n = le32(&th[8]) * 2u;
    *at = off + le32(&th[12]);
    *bytes = n > SCP_FLUX_CAP ? SCP_FLUX_CAP : n;
    return true;
}

static uint32_t flux_to_mfm(const scp_t *s, const uint8_t *flux, uint32_t len,
                            uint8_t *out, uint32_t cap, scp_cells_t *cells) {
    static uint32_t block_ticks[SCP_CELL_BLOCKS];   /* ticks * 16 per 256 bits */
    memset(block_ticks, 0, sizeof block_ticks);
    /* Flux to bits. The bit cell in ticks * 16 follows the disk's speed
     * slowly, within +-10 % of 2 us. */
    const uint32_t max_bits = cap * 8u;
    memset(out, 0, cap);
    uint32_t nbits = 0, carry = 0;
    int32_t cell = (int32_t)(2000u * 16u / s->ticks_ns);
    const int32_t nominal = cell;
    for (uint32_t k = 0; k + 1 < len; k += 2) {
        const uint32_t t = (uint32_t)flux[k] << 8 | flux[k + 1];
        if (t == 0) { carry += 65536u; continue; }
        const int32_t ticks16 = (int32_t)((t + carry) * 16u);
        carry = 0;
        int32_t ncell = (ticks16 + cell / 2) / cell;
        if (ncell < 1) ncell = 1;
        if (ncell > 4) ncell = 4;
        cell += (ticks16 / ncell - cell) / 16;
        if (cell < nominal * 9 / 10) cell = nominal * 9 / 10;
        if (cell > nominal * 11 / 10) cell = nominal * 11 / 10;
        nbits += (uint32_t)ncell;
        if (nbits > max_bits) { nbits = max_bits; break; }
        out[(nbits - 1) >> 3] |= (uint8_t)(0x80u >> ((nbits - 1) & 7));
        if (((nbits - 1) >> 8) < SCP_CELL_BLOCKS) block_ticks[(nbits - 1) >> 8] += (uint32_t)ticks16;
    }

    /* The track as the drive delivers it: from the index on, so a program
     * timing from a sync to the index pulse (Rob Northen Copylock does)
     * sees the real distance. Disk DMA finds syncs at any bit. A track is a
     * whole number of 32-bit words: the bits beyond go at its very end, just
     * before the index, where the track's write splice is. */
    const uint32_t o = (nbits - nbits % 32u) / 8u;

    /* The cells' timing, per 256-bit block of the track as sent (whole
     * blocks of the revolution only; the last, partial one is left out). */
    const uint32_t blocks = nbits >> 8;
    uint64_t sum = 0;
    for (uint32_t b = 0; b < blocks; b++) sum += block_ticks[b];
    const uint32_t mean = blocks ? (uint32_t)(sum / blocks) : 0;
    uint32_t spread = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        const uint32_t d = block_ticks[b] > mean ? block_ticks[b] - mean : mean - block_ticks[b];
        if (mean && d * 1000u / mean > spread) spread = d * 1000u / mean;
    }
    cells->spread = (uint16_t)spread;
    cells->n = 0;
    if (spread > SCP_TIMED_PERMILLE && blocks) {
        const uint32_t out_blocks = (o * 8u + 255u) >> 8;
        for (uint32_t j = 0; j < out_blocks && j < SCP_CELL_BLOCKS; j++) {
            const uint32_t t = block_ticks[j < blocks ? j : blocks - 1u];
            cells->ns[j] = (uint16_t)((uint64_t)t * s->ticks_ns / (16u * 256u));
        }
        cells->n = (uint16_t)(out_blocks < SCP_CELL_BLOCKS ? out_blocks : SCP_CELL_BLOCKS);
    }
    return o;
}

/* An .ipf track from its DATA bytes. */
static uint32_t ipf_to_mfm(scp_t *s, int track, const uint8_t *data, uint8_t *out,
                           uint32_t cap, scp_cells_t *cells) {
    return ipf_track(s->ipf, track, data, out, cap, cells->ns, &cells->n, &cells->spread);
}

uint32_t scp_track(scp_t *s, int track, uint8_t *out, uint32_t cap, scp_cells_t *cells) {
    uint32_t at, len;
    UINT got = 0;
    if (s->ipf) {
        cells->n = 0;
        if (!scp_has(s, track)) return 0;
        len = ipf_data(s->ipf, track, &at);
        if (len > SCP_FLUX_CAP) return 0;
        if (len && (f_lseek(&s->f, at) != FR_OK || f_read(&s->f, flux_now, len, &got) != FR_OK ||
                    got != len))
            return 0;
        return ipf_to_mfm(s, track, flux_now, out, cap, cells);
    }
    if (!track_flux(s, track, &at, &len) || f_lseek(&s->f, at) != FR_OK ||
        f_read(&s->f, flux_now, len, &got) != FR_OK)
        return 0;
    return flux_to_mfm(s, flux_now, got, out, cap, cells);
}

bool scp_bg_start(scp_t *s, int track) {
    uint32_t at, len;
    if (s->ipf) {
        if (!scp_has(s, track)) return false;
        len = ipf_data(s->ipf, track, &at);
        if (len > SCP_FLUX_CAP) return false;
    } else if (!track_flux(s, track, &at, &len)) {
        return false;
    }
    s->bg_track = track;
    s->bg_at = at;
    s->bg_len = len;
    s->bg_got = 0;
    return true;
}

uint32_t scp_bg_step(scp_t *s, uint32_t slice, uint8_t *out, uint32_t cap, scp_cells_t *cells) {
    if (s->bg_track < 0) return 0;
    if (s->bg_got < s->bg_len) {
        uint32_t want = s->bg_len - s->bg_got;
        if (want > slice) want = slice;
        UINT got = 0;
        if (f_lseek(&s->f, s->bg_at + s->bg_got) != FR_OK ||
            f_read(&s->f, flux_bg + s->bg_got, want, &got) != FR_OK || got != want) {
            s->bg_track = -1;
            return 0;
        }
        s->bg_got += got;
        return 0;
    }
    const int track = s->bg_track;
    s->bg_track = -1;
    if (s->ipf) return ipf_to_mfm(s, track, flux_bg, out, cap, cells);
    return flux_to_mfm(s, flux_bg, s->bg_len, out, cap, cells);
}

/* Bench: a track's raw block (TRK header, first revolution's entry, its
 * flux) into `dst`; returns the bytes (0: none, or more than `cap`). */
uint32_t scp_raw_track(scp_t *s, int track, uint8_t *dst, uint32_t cap) {
    uint32_t at, len;
    if (s->ipf) return 0;
    UINT got = 0;
    if (!track_flux(s, track, &at, &len) || 16u + len > cap) return 0;
    if (f_lseek(&s->f, s->offset[track]) != FR_OK || f_read(&s->f, dst, 16, &got) != FR_OK || got != 16)
        return 0;
    dst[12] = 16; dst[13] = dst[14] = dst[15] = 0;     /* flux right after this entry */
    if (f_lseek(&s->f, at) != FR_OK || f_read(&s->f, dst + 16, len, &got) != FR_OK || got != len)
        return 0;
    return 16u + len;
}
