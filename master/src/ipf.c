/*
 * frank-amiga — master: SPS/CAPS (.ipf) disk images, track by track.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Our own reader, written from the published IPF record layout (not SPS's
 * capsimg). An IPF is a chain of records (12-byte header: type, length,
 * CRC): CAPS, INFO, one IMGE per track (cylinder, side, density, where the
 * track starts after the index, its length in bit cells, its blocks), and a
 * DATA record per track: block descriptors (32 bytes each), then each
 * block's data stream. A stream element is a head byte (type in bits 0-4,
 * the size's width in bytes in bits 5-7), the size (bytes, or bits with the
 * block's DataInBit flag) and the bytes: sync and raw elements are bit
 * cells as they are, data and gap elements are bytes to MFM-encode. A
 * block's gap cells not in its stream are its gap value, MFM-encoded (old
 * CAPS-encoder images; SPS-encoder gap streams, forward and backward, are
 * expanded as well).
 *
 * The track goes to the slave as the drive delivers it: from the index, the
 * first block at its start position, whole 32-bit words (the few cells
 * beyond come off the end of the last gap, where the write splice is).
 * Noise (unformatted) tracks are random cells. Copylock densities slow
 * down / speed up the cells of the sectors with syncs $8914 / $8912 by 5 %,
 * sent as the slave's timed-track cell table.
 */
#include "ipf.h"

#include <string.h>

/* Scratch for gaps and cell times: master PSRAM after the drives' indexes
 * (media.c IPF_INDEX), SRAM being full. */
#ifndef IPF_SCRATCH
#define IPF_SCRATCH ((uint8_t *)(0x11000000u + 0x130000u))
#endif

static uint32_t be32(const uint8_t *b) {
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

bool ipf_index_read(ipf_t *p, FIL *f) {
    memset(p, 0, sizeof *p);
    uint8_t h[28 + 68];
    UINT got;
    FSIZE_t at = 0;
    const FSIZE_t size = f_size(f);
    bool caps = false;
    while (at + 12u <= size) {
        if (f_lseek(f, at) != FR_OK || f_read(f, h, 12, &got) != FR_OK || got != 12) return false;
        const uint32_t len = be32(h + 4);
        if (!memcmp(h, "CAPS", 4)) {
            caps = true;
        } else if (!caps) {
            return false;
        } else if (!memcmp(h, "INFO", 4)) {
            if (f_read(f, h, 8, &got) != FR_OK || got != 8) return false;
            p->encoder = be32(h + 4);
        } else if (!memcmp(h, "IMGE", 4)) {
            if (f_read(f, h + 12, 68, &got) != FR_OK || got != 68) return false;
            const uint32_t cyl = be32(h + 12), side = be32(h + 16);
            const uint32_t t = cyl * 2u + side;
            if (t < IPF_TRACKS) {
                ipf_track_t *tr = &p->t[t];
                tr->density = (uint8_t)be32(h + 20);
                tr->start_bit = be32(h + 36);
                tr->track_bits = be32(h + 48);
                tr->blocks = (uint16_t)be32(h + 52);
                /* data key: matched with the DATA record below */
                tr->data_at = be32(h + 64);
                tr->data_len = 0xFFFFu;          /* key not resolved yet */
            }
        } else if (!memcmp(h, "DATA", 4)) {
            if (f_read(f, h + 12, 16, &got) != FR_OK || got != 16) return false;
            const uint32_t dl = be32(h + 12), key = be32(h + 24);
            for (int t = 0; t < IPF_TRACKS; t++) {
                ipf_track_t *tr = &p->t[t];
                if (tr->data_len == 0xFFFFu && tr->data_at == key) {
                    tr->data_at = (uint32_t)at + 28u;
                    tr->data_len = (uint16_t)(dl > 0xFFFEu ? 0 : dl);
                    break;
                }
            }
            at += 28u + dl;
            continue;
        }
        if (len < 12u) return false;
        at += len;
    }
    for (int t = 0; t < IPF_TRACKS; t++)
        if (p->t[t].data_len == 0xFFFFu) p->t[t].data_len = 0;
    return caps;
}

bool ipf_has(const ipf_t *p, int track) {
    return track >= 0 && track < IPF_TRACKS && p->t[track].track_bits;
}

uint32_t ipf_data(const ipf_t *p, int track, uint32_t *at) {
    if (!ipf_has(p, track) || !p->t[track].blocks) return 0;
    *at = p->t[track].data_at;
    return p->t[track].data_len;
}

/* ── Building the track ── */
typedef struct {
    uint8_t *out;
    uint32_t pos, bits;     /* next cell (from the index), the track's cells */
    uint32_t written;
    uint8_t prev;           /* last data bit, for MFM clocks */
    uint16_t ns;            /* this block's cell time */
    uint32_t *block_ns;     /* sum of cell times per 256 cells */
} trk_t;

static inline void put(trk_t *k, unsigned b) {
    const uint32_t i = k->pos;
    if (b) k->out[i >> 3] |= (uint8_t)(0x80u >> (i & 7));
    if (k->block_ns) k->block_ns[i >> 8] += k->ns;
    k->pos = i + 1u == k->bits ? 0 : i + 1u;
    k->written++;
}

static void put_cells(trk_t *k, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n && k->written < k->bits; i++) {
        const unsigned b = (src[i >> 3] >> (7 - (i & 7))) & 1u;
        put(k, b);
        k->prev = (uint8_t)b;
    }
}

static void put_mfm(trk_t *k, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n && k->written < k->bits; i++) {
        const unsigned b = (src[i >> 3] >> (7 - (i & 7))) & 1u;
        put(k, !k->prev && !b);
        put(k, b);
        k->prev = (uint8_t)b;
    }
}

static uint32_t rng_state = 0x2545F491u;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* An SPS-encoder gap stream (forward or backward list) at q: its parts as
 * (length or 0, sample bits, sample). */
typedef struct { uint32_t len, nbits; const uint8_t *smp; } gap_part_t;
static int gap_list(const uint8_t *d, uint32_t dl, uint32_t *q, gap_part_t *parts, int max) {
    int n = 0;
    uint32_t len = 0;
    while (*q < dl) {
        const uint8_t h = d[(*q)++];
        const unsigned type = h & 31u, w = h >> 5;
        if (!type) break;
        uint32_t v = 0;
        for (unsigned i = 0; i < w && *q < dl; i++) v = v << 8 | d[(*q)++];
        if (type == 1) {
            len = v;
        } else if (type == 2) {
            if (n < max) parts[n++] = (gap_part_t){ len, v, d + *q };
            *q += (v + 7u) / 8u;
            len = 0;
        }
    }
    return n;
}

static void put_gap_sps(trk_t *k, const uint8_t *d, uint32_t dl, uint32_t goff,
                        uint32_t flags, uint32_t gap) {
    gap_part_t fw[4], bw[4];
    uint32_t q = goff;
    const int nf = (flags & 1u) ? gap_list(d, dl, &q, fw, 4) : 0;
    const int nb = (flags & 2u) ? gap_list(d, dl, &q, bw, 4) : 0;
    /* The gap as cells in a scratch bit array (gaps are a few kilobits). */
    uint8_t *const g = IPF_SCRATCH, *const set = IPF_SCRATCH + 2048;
    if (gap > 2048u * 8u) gap = 2048u * 8u;
    memset(g, 0, (gap + 7u) / 8u);
    memset(set, 0, (gap + 7u) / 8u);
#define GSET(i, b) do { if (b) g[(i) >> 3] |= (uint8_t)(0x80u >> ((i) & 7)); \
                        set[(i) >> 3] |= (uint8_t)(0x80u >> ((i) & 7)); } while (0)
#define SBIT(p, i) (((p)->smp[(i) >> 3] >> (7 - ((i) & 7))) & 1u)
    uint32_t pos = 0, end = gap;
    for (int i = 0; i < nf; i++) {
        const gap_part_t *pt = &fw[i];
        if (!pt->nbits) continue;
        const uint32_t cnt = pt->len ? pt->len : gap - pos;
        for (uint32_t j = 0; j < cnt && pos < gap; j++, pos++) GSET(pos, SBIT(pt, j % pt->nbits));
    }
    for (int i = 0; i < nb; i++) {
        const gap_part_t *pt = &bw[i];
        if (!pt->nbits) continue;
        const uint32_t cnt = pt->len ? pt->len : end - pos;
        for (uint32_t j = 0; j < cnt && end > 0; j++) {
            end--;
            GSET(end, SBIT(pt, pt->nbits - 1u - j % pt->nbits));
        }
    }
    const gap_part_t *last = nf ? &fw[nf - 1] : nb ? &bw[0] : NULL;
    for (uint32_t i = 0; i < gap; i++) {
        const bool done = (set[i >> 3] >> (7 - (i & 7))) & 1u;
        const unsigned b = done ? (g[i >> 3] >> (7 - (i & 7))) & 1u
                         : last && last->nbits ? SBIT(last, i % last->nbits) : (i & 1u);
        if (k->written < k->bits) put(k, b);
        k->prev = (uint8_t)b;
    }
#undef GSET
#undef SBIT
}

uint32_t ipf_track(const ipf_t *p, int track, const uint8_t *d, uint8_t *out,
                   uint32_t cap, uint16_t *cell_ns, uint16_t *ncells, uint16_t *spread) {
    *ncells = 0;
    *spread = 0;
    if (!ipf_has(p, track)) return 0;
    const ipf_track_t *tr = &p->t[track];
    uint32_t bits = tr->track_bits;
    if (bits > cap * 8u) bits = cap * 8u;
    const uint32_t o = (bits - bits % 32u) / 8u;
    /* Whole 32-bit words: the cells beyond come off the end of the last
     * block's gap, the track's write splice (cutting them at the index
     * instead broke the sector that straddles it). */
    bits = o * 8u;
    memset(out, 0, (bits + 7u) / 8u);
    if (!tr->blocks || !tr->data_len) {         /* noise: random cells */
        for (uint32_t i = 0; i < o; i += 4) {
            const uint32_t r = rnd();
            memcpy(out + i, &r, 4);
        }
        return o;
    }
    const bool copylock = tr->density == 3 || tr->density == 4;
    uint32_t *const block_ns = (uint32_t *)(IPF_SCRATCH + 4096);
    trk_t k = { out, tr->start_bit % bits, bits, 0, 0, 2000, NULL };
    if (copylock) {
        memset(block_ns, 0, 512u * sizeof *block_ns);
        k.block_ns = block_ns;
    }
    const uint32_t dl = tr->data_len;
    for (uint32_t bi = 0; bi < tr->blocks && (bi + 1u) * 32u <= dl; bi++) {
        const uint8_t *bd = d + bi * 32u;
        const uint32_t gap_bits = be32(bd + 4);
        const uint32_t goff = be32(bd + 8);
        const uint32_t flags = be32(bd + 20), gap_value = be32(bd + 24);
        uint32_t q = be32(bd + 28);
        k.ns = 2000;
        if (copylock) {
            /* Copylock's key sectors by their sync word (the block's first
             * sync element, after a short lead-in); the whole block's cells
             * take its speed. */
            uint32_t r = q;
            while (r < dl) {
                const uint8_t h = d[r++];
                const unsigned type = h & 31u, w = h >> 5;
                if (!type) break;
                uint32_t n = 0;
                for (unsigned i = 0; i < w && r < dl; i++) n = n << 8 | d[r++];
                const uint32_t nb = (flags & 4u) ? n : n * 8u;
                if (type == 1 && nb >= 16 && r + 1 < dl) {
                    const uint32_t sync = (uint32_t)d[r] << 8 | d[r + 1];
                    k.ns = sync == 0x8912u ? 1900 : sync == 0x8914u ? 2100 : 2000;
                    break;
                }
                if (type != 5) r += (nb + 7u) / 8u;
            }
        }
        while (q < dl) {
            const uint8_t h = d[q++];
            const unsigned type = h & 31u, w = h >> 5;
            if (!type) break;
            uint32_t n = 0;
            for (unsigned i = 0; i < w && q < dl; i++) n = n << 8 | d[q++];
            const uint32_t nb = (flags & 4u) ? n : n * 8u;
            const uint32_t bytes = (nb + 7u) / 8u;
            if (q + bytes > dl && type != 5) break;
            switch (type) {
            case 1: case 4: put_cells(&k, d + q, nb); q += bytes; break;
            case 2: case 3: put_mfm(&k, d + q, nb); q += bytes; break;
            case 5:                                 /* fuzzy: weak cells */
                for (uint32_t i = 0; i < nb && k.written < k.bits; i++) {
                    const unsigned b = rnd() & 1u;
                    put(&k, b);
                    k.prev = (uint8_t)b;
                }
                break;
            default: q = dl; break;
            }
        }
        if (gap_bits) {
            if (p->encoder == 2 && (flags & 3u)) {
                put_gap_sps(&k, d, dl, goff, flags, gap_bits);
            } else {
                const uint8_t v = (uint8_t)gap_value;
                uint32_t left = gap_bits;
                while (left && k.written < k.bits) {
                    for (int i = 7; i >= 0 && left; i--) {
                        const unsigned b = (v >> i) & 1u, c = !k.prev && !b;
                        put(&k, c);
                        if (!--left) { k.prev = (uint8_t)c; break; }   /* cut after a clock */
                        put(&k, b);
                        left--;
                        k.prev = (uint8_t)b;
                    }
                }
            }
        }
    }
    if (copylock) {
        const uint32_t blocks = bits >> 8;
        uint64_t sum = 0;
        for (uint32_t b = 0; b < blocks; b++) sum += block_ns[b];
        const uint32_t mean = blocks ? (uint32_t)(sum / blocks) : 0;
        uint32_t sp = 0;
        for (uint32_t b = 0; b < blocks; b++) {
            const uint32_t dv = block_ns[b] > mean ? block_ns[b] - mean : mean - block_ns[b];
            if (mean && dv * 1000u / mean > sp) sp = dv * 1000u / mean;
        }
        *spread = (uint16_t)sp;
        const uint32_t out_blocks = (o * 8u + 255u) >> 8;
        for (uint32_t j = 0; j < out_blocks && j < 512u; j++) {
            const uint32_t t = block_ns[j < blocks ? j : blocks - 1u];
            cell_ns[j] = (uint16_t)(t / 256u);
        }
        *ncells = (uint16_t)(out_blocks < 512u ? out_blocks : 512u);
    }
    return o;
}
