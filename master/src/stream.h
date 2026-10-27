/*
 * frank-amiga — master: the link stream receiver (core 0).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_STREAM_H
#define FRANK_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include "link_session.h"

typedef struct {
    uint32_t chunks, words, link_errors, bad_chunks, seq_gaps, resyncs;
    uint64_t busy_us;
} stream_stats_t;
extern stream_stats_t stream_stats;

void stream_init(link_session_t *sess);
void stream_start(void);
void stream_stop(void);
/* Pull at most one chunk; call from the main loop. True if one arrived. */
bool stream_poll(void);
/* Queue an input event for the next request (link_stream.h LINK_EV_*). */
bool stream_input(uint8_t type, uint8_t a, uint8_t b, uint8_t c);

/* Set when the slave reports written disk tracks (media_poll() saves them). */
extern volatile bool stream_disk_dirty;
/* Set when a flux-image drive wants a track (media_poll() sends it). */
extern volatile bool stream_track_wanted;

#endif
