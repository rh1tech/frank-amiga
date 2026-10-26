/*
 * frank-amiga — slave core 1: the link pump.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_LINK_PUMP_H
#define FRANK_LINK_PUMP_H

#include <stdbool.h>
#include <stdint.h>
#include "link_stream.h"

/* Core 1 entry: owns the link from here on. */
void pump_core1_entry(void);

/* Core 0: next input event from the master, if any. */
bool pump_input_pop(link_input_ev_t *ev);

/* Master display refreshes (FS edges) and when the last one came. */
extern volatile uint32_t pump_refreshes;
extern volatile uint64_t pump_refresh_us;
extern volatile uint32_t pump_chunks, pump_words, pump_requests, pump_resyncs, pump_bad_headers;

#endif
