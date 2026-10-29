/*
 * frank-amiga — master: SPS/CAPS (.ipf) disk images, track by track.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_IPF_H
#define FRANK_IPF_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#define IPF_TRACKS 168

typedef struct {
    uint32_t data_at;       /* its DATA record's block descriptors in the file */
    uint16_t data_len;      /* their bytes, streams included (0: no data) */
    uint16_t blocks;
    uint32_t start_bit;     /* where the first block starts, from the index */
    uint32_t track_bits;
    uint8_t density;        /* IMGE density type (1 noise, 2 auto, 3/4 Copylock...) */
} ipf_track_t;

typedef struct {
    uint32_t encoder;       /* 1 CAPS, 2 SPS */
    ipf_track_t t[IPF_TRACKS];
} ipf_t;

/* Reads the image's records (`f` at its start); false if it is no IPF. */
bool ipf_index_read(ipf_t *p, FIL *f);
/* Whether `track` (cylinder * 2 + side) exists in the image. */
bool ipf_has(const ipf_t *p, int track);
/* Bytes of the track's DATA record to read (0: none, the track is noise). */
uint32_t ipf_data(const ipf_t *p, int track, uint32_t *at);
/* The track from its DATA bytes (`data`, as read): MFM from the index on,
 * `cap` bytes at most; returns the bytes (a multiple of 4). Copy-protected
 * densities (Copylock) give per-256-bit cell times in `cell_ns` (*ncells
 * entries, 0 for a uniform track). */
uint32_t ipf_track(const ipf_t *p, int track, const uint8_t *data, uint8_t *out,
                   uint32_t cap, uint16_t *cell_ns, uint16_t *ncells, uint16_t *spread);

#endif
