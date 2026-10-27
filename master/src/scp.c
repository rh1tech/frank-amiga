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
 * works, AmigaDOS or a game's own. Omega's disk DMA finds sync words on word
 * boundaries only, so the track is rotated to put its first 4489 on one
 * (everything one DMA write laid down keeps that alignment).
 */
#include "scp.h"

#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool scp_open(scp_t *s, const char *path) {
    scp_close(s);
    if (f_open(&s->f, path, FA_READ) != FR_OK) return false;
    uint8_t hdr[16];
    UINT got = 0;
    if (f_read(&s->f, hdr, sizeof hdr, &got) != FR_OK || got != sizeof hdr ||
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

#define SCP_TAIL_BYTES 2048u   /* a first sync further in is left in place */

/* Flux is read whole into master PSRAM, after the track cache: the card is
 * several times faster at long reads than at small ones. One buffer for a
 * track asked for now, one for the background job. */
#define SCP_FLUX_CAP 0x40000u
static uint8_t *const flux_now = (uint8_t *)(0x11000000u + 0x740000u);
static uint8_t *const flux_bg = (uint8_t *)(0x11000000u + 0x780000u);

/* Bits of `out`, most significant first. */
static inline int bit_at(const uint8_t *b, uint32_t i) { return (b[i >> 3] >> (7 - (i & 7))) & 1; }

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
                            uint8_t *out, uint32_t cap) {
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
        int32_t cells = (ticks16 + cell / 2) / cell;
        if (cells < 1) cells = 1;
        if (cells > 4) cells = 4;
        cell += (ticks16 / cells - cell) / 16;
        if (cell < nominal * 9 / 10) cell = nominal * 9 / 10;
        if (cell > nominal * 11 / 10) cell = nominal * 11 / 10;
        nbits += (uint32_t)cells;
        if (nbits > max_bits) { nbits = max_bits; break; }
        out[(nbits - 1) >> 3] |= (uint8_t)(0x80u >> ((nbits - 1) & 7));
    }

    /* Start the track at its first sync word: the revolution is a circle,
     * so the bits before it follow at the end, in order (a sector across the
     * index stays whole). The only cut is just before that sync, in its
     * gap: the bits beyond the last whole 4 bytes are dropped there. */
    uint32_t sr = 0, start = 0;
    for (uint32_t i = 0; i < nbits; i++) {
        sr = (sr << 1 | (uint32_t)bit_at(out, i)) & 0xFFFFu;
        if (i >= 15 && sr == 0x4489u) { start = i - 15; break; }
    }
    const uint32_t bytes = (nbits / 8u) & ~3u;
    if (start) {
        /* Rotate left by `start` bits: whole bytes through `tail` (the bytes
         * that wrap), then the bit remainder in place. */
        static uint8_t tail[SCP_TAIL_BYTES];
        const uint32_t whole = start >> 3, part = start & 7u;
        const uint32_t nb = (nbits + 7u) / 8u;
        if (whole < sizeof tail) {
            memcpy(tail, out, whole + 1u);
            memmove(out, out + whole, nb - whole);
            /* The rotated bit string: bits[start..nbits) then bits[0..start). */
            uint32_t dst = nbits - start;           /* where the wrapped bits go */
            for (uint32_t i = 0; i < start; i++, dst++) {
                const int bit = (tail[i >> 3] >> (7 - (i & 7))) & 1;
                const uint32_t at = dst + part;     /* still offset by `part` */
                if (bit) out[at >> 3] |= (uint8_t)(0x80u >> (at & 7));
                else out[at >> 3] &= (uint8_t)~(0x80u >> (at & 7));
            }
            if (part)
                for (uint32_t j = 0; j < bytes; j++)
                    out[j] = (uint8_t)(out[j] << part | out[j + 1] >> (8 - part));
        }
    }
    return bytes;
}

uint32_t scp_track(scp_t *s, int track, uint8_t *out, uint32_t cap) {
    uint32_t at, len;
    UINT got = 0;
    if (!track_flux(s, track, &at, &len) || f_lseek(&s->f, at) != FR_OK ||
        f_read(&s->f, flux_now, len, &got) != FR_OK)
        return 0;
    return flux_to_mfm(s, flux_now, got, out, cap);
}

bool scp_bg_start(scp_t *s, int track) {
    uint32_t at, len;
    if (!track_flux(s, track, &at, &len)) return false;
    s->bg_track = track;
    s->bg_at = at;
    s->bg_len = len;
    s->bg_got = 0;
    return true;
}

uint32_t scp_bg_step(scp_t *s, uint32_t slice, uint8_t *out, uint32_t cap) {
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
    s->bg_track = -1;
    return flux_to_mfm(s, flux_bg, s->bg_len, out, cap);
}
