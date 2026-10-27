/*
 * frank-amiga — slave: savestates (the whole emulated machine, in PSRAM).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_SAVESTATE_H
#define FRANK_SAVESTATE_H

#include <stdbool.h>
#include <stdint.h>

/* Core 1 (link op): ask core 0 to save (0) or load (1) slot `slot` at its
 * next frame end. */
void savestate_request(int load, int slot);
/* Core 0, once per emulated frame (after it is queued). */
void savestate_core0_frame(void);

#endif
