#pragma once

#include <stdint.h>
#include "VideoStandard.h"

enum {
    OMEGA_DDF_FULL_WIDTH_LIMIT = 0x40,
    OMEGA_DDF_EXTRA_WORD_START = 0x3c,
    OMEGA_DDF_EXTRA_WORD_STOP = 0xd0,
    OMEGA_DDF_ROTATED_START = 0x38,
    OMEGA_DDF_ROTATED_STOP = 0xd8,
    OMEGA_DDF_ROTATED_PIXELS = 80,
    OMEGA_DDF_NORMAL_FETCH_TAIL = 7,
    OMEGA_DDF_EXTRA_FETCH_TAIL = 11,
    OMEGA_DDF_NORMAL_UPPER_OVERSCAN = 14,
    // Intuition's extra-word HIRES layout opens its display window at line 5;
    // 5 + 39 anchors raster row 0 on line 44 (0x2c), the first visible line.
    OMEGA_DDF_EXTRA_UPPER_OVERSCAN = 39,
    OMEGA_DISPLAY_RASTER_ORIGIN = 43,
    OMEGA_LORES_FIRST_RENDER_LINE = 44,
};

// HDMI direct frames place every beam line at a fixed image row, as a
// monitor would: row = line - OMEGA_DIRECT_FIRST_LINE. PAL rows 0..255 are
// lines 44..299 (the standard 256-line window), NTSC rows 0..239 lines
// 23..262 (every line an NTSC frame shows). Display windows that open
// early (Kickstart and Workbench open theirs at line 5 and start the
// bitplanes with the Copper) need no per-layout anchor, and the borders
// around a smaller window show their own lines.
#if OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_PAL
#define OMEGA_DIRECT_FIRST_LINE 44
#define OMEGA_DIRECT_ROWS 256
#else
#define OMEGA_DIRECT_FIRST_LINE 23
#define OMEGA_DIRECT_ROWS 240
#endif

static inline int omegaDdfIsFullWidth(uint16_t start) {
    return start < OMEGA_DDF_FULL_WIDTH_LIMIT;
}

static inline int omegaDiwVerticalStart(uint16_t diwstrt) {
    return diwstrt >> 8;
}

// OCS has no ninth DIWSTOP bit: the hardware takes VSTOP8 as the inverse of
// VSTOP7, so stop lines run from $80 to $17f ($f4 is line 244, $2c is 300).
static inline int omegaDiwVerticalStop(uint16_t diwstop) {
    const int low = diwstop >> 8;
    return (low & 0x80) ? low : 0x100 | low;
}

static inline int omegaDiwVerticalSpan(uint16_t diwstrt, uint16_t diwstop) {
    return omegaDiwVerticalStop(diwstop) -
           omegaDiwVerticalStart(diwstrt);
}

// Lines of the window the beam actually draws: an NTSC frame ends at line
// 263, so a PAL-sized window (to line 300) is cut there, as on a real NTSC
// Amiga.
static inline int omegaDiwVisibleSpan(uint16_t diwstrt, uint16_t diwstop) {
    int stop = omegaDiwVerticalStop(diwstop);
    if (stop > OMEGA_VIDEO_FRAME_LINES)
        stop = OMEGA_VIDEO_FRAME_LINES;
    return stop - omegaDiwVerticalStart(diwstrt);
}

// LORES lines the output shows at one raster row per line: the native
// viewport's height, or on HDMI the scanout image (240 rows on NTSC, so a
// PAL-sized window shows down to the end of the NTSC frame instead of being
// cut at 200 lines).
#ifndef OMEGA_LORES_SINGLE_ROW_LINES
#if defined(PICO_BUILD) && OMEGA_ENABLE_HDMI && \
    OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_NTSC
#define OMEGA_LORES_SINGLE_ROW_LINES 240
#else
#define OMEGA_LORES_SINGLE_ROW_LINES (OMEGA_VIDEO_NATIVE_HEIGHT / 2)
#endif
#endif

// Overscan LORES displays can advance through more beam rows than fit in the
// output.  Keep their logical rows on alternating raster rows; normal
// 200/256-line displays use every raster row.
static inline int omegaLoresUsesAlternateRasterRows(uint16_t diwstrt,
                                                     uint16_t diwstop) {
    return omegaDiwVisibleSpan(diwstrt, diwstop) >
           OMEGA_LORES_SINGLE_ROW_LINES;
}

static inline int omegaDiwCrossesVerticalBank(uint16_t diwstrt,
                                               uint16_t diwstop) {
    int start = omegaDiwVerticalStart(diwstrt);
    int stopLow = diwstop >> 8;
    int stop = omegaDiwVerticalStop(diwstop);
    return stopLow < start ||
           (OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_PAL &&
            stop <= OMEGA_VIDEO_FRAME_LINES);
}

static inline int omegaDdfNeedsExtraWord(uint16_t start, uint16_t stop) {
    return start == OMEGA_DDF_EXTRA_WORD_START &&
           stop == OMEGA_DDF_EXTRA_WORD_STOP;
}

static inline int omegaDdfRowRotation(uint16_t start, uint16_t stop) {
    return start == OMEGA_DDF_ROTATED_START &&
           stop == OMEGA_DDF_ROTATED_STOP
         ? OMEGA_DDF_ROTATED_PIXELS : 0;
}

static inline int omegaDdfHiresFetchTail(uint16_t start, uint16_t stop) {
    return omegaDdfNeedsExtraWord(start, stop)
         ? OMEGA_DDF_EXTRA_FETCH_TAIL : OMEGA_DDF_NORMAL_FETCH_TAIL;
}

static inline int omegaDdfUpperOverscan(uint16_t stop) {
    return stop == OMEGA_DDF_EXTRA_WORD_STOP
         ? OMEGA_DDF_EXTRA_UPPER_OVERSCAN
         : OMEGA_DDF_NORMAL_UPPER_OVERSCAN;
}
