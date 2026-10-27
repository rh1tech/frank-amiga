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
/* Scanout: draw the menu over source row `row`. */
void osd_overlay_row(uint32_t row, uint32_t *rgb888, uint32_t width);

#endif
