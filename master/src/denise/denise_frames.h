// frank-amiga — indexed Amiga frames and their scanout expansion (master).
//
// SPDX-License-Identifier: MPL-2.0
// From github.com/fboldog/rp2350-amiga src/dvi_display.h. The HSTX part of
// that file is master/src/video/hstx_video.c here; this keeps the frame
// buffers, their records and the per-row expansion to RGB888.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Direct indexed rendering (exact Amiga colours). Core 1 writes colour
// register numbers straight into the free SRAM frame: 0..31, 32..63
// for extra half-brite, or raw 6-bit HAM codes on HAM segments. It records
// each run of pixels it draws with the number of palette writes logged
// before it; scanout replays the log and expands every row to RGB888, so
// Copper palette changes, even mid-line, keep their exact 12-bit colours.
#define DVI_DIRECT_IMAGE_WIDTH 640
#define DVI_MAX_ROWS 256
#define DVI_MAX_SEGMENTS 4096
#define DVI_MAX_PALETTE_LOG 4095
#define DVI_SEGMENT(x0, x1, pos, ham) \
    ((uint32_t)(x0) >> 1 | ((uint32_t)(x1) >> 1) << 9 | \
     (uint32_t)(pos) << 18 | (uint32_t)(ham) << 31)
#define DVI_SEGMENT_X0(s)  (((s) & 0x1ffu) << 1)
#define DVI_SEGMENT_X1(s)  ((((s) >> 9) & 0x1ffu) << 1)
#define DVI_SEGMENT_POS(s) (((s) >> 18) & 0x1fffu)
#define DVI_SEGMENT_HAM(s) ((s) >> 31)
typedef struct {
    uint16_t initial_palette[32];         // 0x0RGB colours at frame begin
    uint16_t row_segment[DVI_MAX_ROWS + 1];
    uint16_t row_log[DVI_MAX_ROWS];
    uint16_t segments_used;
    uint16_t log_used;
    bool segment_overflow;
    uint32_t segment[DVI_MAX_SEGMENTS];
    uint32_t palette_log[DVI_MAX_PALETTE_LOG]; // register << 12 | 0x0RGB
} dvi_indexed_frame_t;

// Set up the frames (records in PSRAM at `records`, which must hold four
// dvi_indexed_frame_t) for the active video mode.
void denise_frames_init(void *records);

// Image rows of the direct frame (256 PAL).
int dvi_display_direct_height(void);
// True while a finished frame waits for the next display frame boundary.
bool dvi_display_frame_pending(void);
// Claims the back buffer; *prev is the buffer's last finished record.
uint8_t *dvi_display_direct_acquire(dvi_indexed_frame_t **record,
                                    dvi_indexed_frame_t **prev, int *buffer);
// Queues the acquired frame; `border` is COLOR00 (0x0RGB) at frame end.
void dvi_display_direct_publish(uint16_t border);
// Interlaced frames: field 0 (short) / 1 (long) draws into its own buffer
// and scanout weaves the two once both exist.
uint8_t *dvi_display_field_acquire(int field, dvi_indexed_frame_t **record,
                                   dvi_indexed_frame_t **prev);
void dvi_display_field_publish(uint16_t border);
void dvi_display_weave_stop(void);

// hstx_video callbacks: one source row of RGB888, and the frame start
// (swaps in a pending frame).
void denise_row(uint32_t row, uint32_t field, uint32_t *rgb888, uint32_t width);
void denise_frame_start(uint32_t frame);

extern volatile uint32_t dvi_refreshes, dvi_frames_shown;
