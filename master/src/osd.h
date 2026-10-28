/*
 * frank-amiga — master: the on-screen menu (F12).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_OSD_H
#define FRANK_OSD_H

#include <stdbool.h>
#include <stdint.h>

void osd_init(void);              /* after video_init() */
void osd_toggle(void);
void osd_fm_toggle(void);        /* the file manager (F11) */
bool osd_active(void);
/* A key while the menu is open (HID usage code). */
void osd_key(uint8_t hid, bool down);
/* The performance meter (bottom right): emulator time per frame and
 * emulated frames per second, as the slave reports them. */
void osd_meter_set(bool on);
bool osd_meter_on(void);
/* Denise, at every emulated frame's start: the slave's time per frame
 * (0.1 ms, MSG_BEGIN extra). */
void osd_meter_frame(uint32_t emu_tenths_ms);
/* Scanout: draw the menu over source row `row`. */
void osd_overlay_row(uint32_t row, uint32_t *rgb888, uint32_t width);

#endif
