/*
 * frank-amiga — HSTX DVI scanout with a per-row callback.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_HSTX_VIDEO_H
#define FRANK_HSTX_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIDEO_720x576p50,   /* PAL, 288 rows, TMDS 270 MHz */
    VIDEO_720x480p60,   /* NTSC, 240 rows, TMDS 270 MHz */
    VIDEO_640x480p60,   /* VGA, 240 rows, TMDS 252 MHz */
    VIDEO_MODE_COUNT,
} video_mode_t;

/* Fill source row `row` (each shown on two output lines) with `width`
 * RGB565 pixels. Runs in an interrupt on the core that called
 * video_start(), roughly two output lines before the row is sent; must live
 * in RAM. */
typedef void (*video_row_fn)(uint32_t row, uint16_t *rgb565, uint32_t width);
/* Called from the scanout interrupt at the start of every output frame
 * (line 0). Optional. */
typedef void (*video_frame_fn)(uint32_t frame);

/* Retune PLL_USB for the mode and claim resources. Core-agnostic. */
bool video_init(video_mode_t mode, video_row_fn row, video_frame_fn frame);
/* Start scanout; its interrupts run on the calling core. */
void video_start(void);

uint32_t video_width(void);
uint32_t video_rows(void);
const char *video_mode_name(void);
/* True for IT formats (640x480), false for CEA (576p, 480p): limited range. */
bool video_full_range(void);
/* Amiga 12-bit colour to RGB565 in the active mode's range. Call after
 * video_init(); build lookup tables from it rather than calling per pixel. */
uint16_t video_rgb12_to_565(uint16_t rgb12);

extern volatile uint32_t video_frames;
extern volatile uint32_t video_late_rows;

#endif
