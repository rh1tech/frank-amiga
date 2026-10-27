/*
 * frank-amiga — slave host layer: ring access for the consumer on core 1.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_HOST_LINK_H
#define FRANK_HOST_LINK_H

#include <stdbool.h>
#include <stdint.h>

/* The ring of 32-bit message words (HostRing.h formats). */
uint32_t host_ring_words(void);
const uint32_t *host_ring_base(void);
/* Words published and not yet consumed; *slot is where they start. They
 * may wrap at host_ring_words(). */
uint32_t host_ring_pending(uint32_t *slot);
void host_ring_consume(uint32_t words);
uint32_t host_ring_tail(void);
uint32_t host_ring_span(void);

/* Ring positions (0 .. host_ring_span()-1) where a frame's MSG_BEGIN sits,
 * oldest first. */
bool host_frame_mark_peek(uint32_t *pos);
void host_frame_mark_pop(void);
void host_frame_marks_clear(void);

/* Frames handed to the ring so far (once per VBL). */
extern volatile uint32_t host_frames_emulated;
extern volatile uint32_t hostAudioDrops;

/* Called once per emulated frame, after it is queued: pacing hook. */
void host_frame_paced(void);

void hostAmigaKey(uint8_t code, int released);
/* A savestate was loaded: resend everything (savestate.c). */
void host_state_restored(void);
extern int hostResetRequested;

#endif
