/*
 * frank-amiga — master: SuperCard Pro (.scp) flux images, track by track.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_SCP_H
#define FRANK_SCP_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#define SCP_TRACKS 168

typedef struct {
    bool open;
    FIL f;
    uint32_t offset[SCP_TRACKS];   /* track headers in the file (0: none) */
    uint32_t ticks_ns;             /* flux time unit */
    DWORD clmt[64];                /* FatFs fast-seek cluster map */
    int bg_track;                  /* background decode (-1: none) */
    uint32_t bg_at, bg_len, bg_got;
} scp_t;

/* Bit-cell timing of a track, per 256-bit block, in ns: only for tracks
 * whose cells vary by more than SCP_TIMED_PERMILLE (copy protections that
 * time a sector written slower, e.g. Rob Northen Copylock); n = 0 for the
 * others. */
#define SCP_CELL_BLOCKS 512
#define SCP_TIMED_PERMILLE 40u
typedef struct {
    uint16_t n;
    uint16_t spread;                    /* largest deviation from the mean, 1/1000 */
    uint16_t ns[SCP_CELL_BLOCKS];
} scp_cells_t;

bool scp_open(scp_t *s, const char *path);
void scp_close(scp_t *s);
/* Track `track` (cylinder * 2 + side) as MFM bytes in `out` (at most `cap`),
 * its first sync word-aligned; returns the bytes (a multiple of 4), 0 if
 * there is no such track. */
uint32_t scp_track(scp_t *s, int track, uint8_t *out, uint32_t cap, scp_cells_t *cells);
/* The same in the background, a slice of the file at a time: start a
 * track, then step until a step returns its bytes (bg_track is -1 again
 * when it finishes or fails). One job at a time across all images. */
bool scp_bg_start(scp_t *s, int track);
uint32_t scp_bg_step(scp_t *s, uint32_t slice, uint8_t *out, uint32_t cap, scp_cells_t *cells);

#endif
