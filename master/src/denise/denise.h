/*
 * frank-amiga — master: Denise rendering and Paula output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_DENISE_H
#define FRANK_DENISE_H

#include <stdbool.h>
#include <stdint.h>

/* The message ring core 1 renders from (denise_c1.c). Single producer
 * (stream.c on core 0), single consumer (core 1). */
uint32_t denise_ring_space(void);
void denise_ring_put(const uint32_t *words, uint32_t count);

/* Core 1 entry, after video_start(): never returns. */
void hostCore1Loop(void);
void denise_c1_init(void);
extern volatile uint32_t hostCaptureHold;

/* Called by core 1 when it meets a message it cannot parse. */
void denise_stream_corrupt(uint32_t header);
extern volatile uint32_t denise_corrupt_count;
/* frank-amiga: the oldest frame the scanout buffers hold (0: unknown), for
 * the stream requests; set denise_buffers_stale after a stream loss. */
extern volatile uint32_t denise_oldest_frame, denise_buffers_stale;
extern volatile uint32_t denise_resync_request;
extern volatile uint32_t denise_audio_dropped, denise_audio_adjusted;

/* Paula output (denise_audio.c). */
void denise_audio_init(void);
void denise_audio_service(void);

#endif
