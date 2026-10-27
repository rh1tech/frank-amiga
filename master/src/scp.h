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
} scp_t;

bool scp_open(scp_t *s, const char *path);
void scp_close(scp_t *s);
/* Track `track` (cylinder * 2 + side) as MFM bytes in `out` (at most `cap`),
 * its first sync word-aligned; returns the bytes (a multiple of 4), 0 if
 * there is no such track. */
uint32_t scp_track(scp_t *s, int track, uint8_t *out, uint32_t cap);

#endif
