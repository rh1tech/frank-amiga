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
    s->ticks_ns = 25u * ((uint32_t)hdr[11] + 1u);
    s->open = true;
    return true;
}

void scp_close(scp_t *s) {
    if (s->open) f_close(&s->f);
    s->open = false;
}

/* Bits of `out`, most significant first. */
static inline int bit_at(const uint8_t *b, uint32_t i) { return (b[i >> 3] >> (7 - (i & 7))) & 1; }

uint32_t scp_track(scp_t *s, int track, uint8_t *out, uint32_t cap) {
    if (!s->open || track < 0 || track >= SCP_TRACKS || !s->offset[track]) return 0;
    uint8_t th[16];
    UINT got = 0;
    const uint32_t off = s->offset[track];
    if (f_lseek(&s->f, off) != FR_OK || f_read(&s->f, th, sizeof th, &got) != FR_OK ||
        got != sizeof th || memcmp(th, "TRK", 3) != 0)
        return 0;
    const uint32_t n = le32(&th[8]), data = le32(&th[12]);
    if (f_lseek(&s->f, off + data) != FR_OK) return 0;

    /* Flux to bits. The bit cell in ticks * 16 follows the disk's speed
     * slowly, within +-10 % of 2 us. */
    const uint32_t max_bits = cap * 8u;
    memset(out, 0, cap);
    uint32_t nbits = 0, carry = 0;
    int32_t cell = (int32_t)(2000u * 16u / s->ticks_ns);
    const int32_t nominal = cell;
    uint8_t chunk[1024];
    uint32_t pos = 0, have = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (pos + 2 > have) {
            const uint32_t left = (n - k) * 2u;
            if (f_read(&s->f, chunk, left < sizeof chunk ? left : sizeof chunk, &got) != FR_OK || got < 2)
                break;
            have = got;
            pos = 0;
        }
        const uint32_t t = (uint32_t)chunk[pos] << 8 | chunk[pos + 1];
        pos += 2;
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

    /* Put the first sync word on a word boundary. */
    uint32_t sr = 0, shift = 0;
    for (uint32_t i = 0; i < nbits; i++) {
        sr = (sr << 1 | (uint32_t)bit_at(out, i)) & 0xFFFFu;
        if (i >= 15 && sr == 0x4489u) { shift = (i - 15) & 15u; break; }
    }
    const uint32_t bytes = ((nbits - shift) / 8u) & ~3u;
    if (shift) {
        const uint32_t whole = shift >> 3, part = shift & 7u;
        for (uint32_t j = 0; j < bytes; j++) {
            const uint8_t a = out[j + whole], b = out[j + whole + 1];
            out[j] = part ? (uint8_t)(a << part | b >> (8 - part)) : a;
        }
    }
    return bytes;
}
