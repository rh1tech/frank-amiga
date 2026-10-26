// RP2350 host layer for the Omega Amiga emulator.
// Replaces the SDL2-based Host.h/Host.c with bare-metal equivalents.
//
// The framebuffer is written to PSRAM; keyboard/mouse entry points remain
// stubs until USB HID support is added.
// PicoDVI is implemented when OMEGA_ENABLE_HDMI is enabled; VGA-PIO and
// SPI-TFT remain possible alternative backends.

#pragma once
#include <stdint.h>
#include "../omega/VideoStandard.h"

// Screen dimensions that the Omega DMA engine renders into
#define SCREEN_W 640
#ifdef PICO_BUILD
// The RP2350's 8 MB PSRAM map reserves a fixed 1 MB ARGB framebuffer.
#define SCREEN_H 400
// Full-width LORES modes use alternate beam rows.  Keep the complete NTSC
// field here: a 200-row scratch buffer preserves only the upper 100 logical
// rows of the Kickstart 1.3 insert-disk artwork.
#define HOST_RASTER_H SCREEN_H
#else
#define SCREEN_H OMEGA_VIDEO_NATIVE_HEIGHT
// Native LORES DMA can occupy every PAL/NTSC beam row before presentation
// deinterlaces it.  Keep the complete field so artwork near the bottom is
// not truncated; HIRES presentation still consumes only SCREEN_H / 2 rows.
#define HOST_RASTER_H SCREEN_H
#endif

// Omega's DMA renderer produces an intermediate beam raster.  Kickstart's
// DMA fetch window contains pipeline/overscan words around the visible data.
// The host presentation step clips that raster into a 640x400 image and
// doubles scanlines vertically.
#define HOST_RASTER_W       640
#define HOST_RASTER_PIXELS  (HOST_RASTER_W * HOST_RASTER_H)
#define HOST_VISIBLE_X0     40
#define HOST_VISIBLE_X1     596
#define HOST_FETCH_LEAD     24
#define HOST_CONTENT_Y      44

// Host state – RP2350 flavour (no SDL types here)
typedef struct {
    int rasterRow;  // destination row in the intermediate DMA raster
    int rasterX;    // destination pixel within rasterRow
    int displayIsLores; // mode used by the most recently rendered bitplane row

    // Pointer to the 32-bit ARGB framebuffer in PSRAM
    void *pixels;
} Host_t;

extern Host_t host;

// Raster writes go through the host: hostRasterPixels() returns where the
// planar converter writes `width` ARGB pixels for raster (row, x), and
// hostRasterWritten() reports them afterwards. Writes along a row are
// contiguous. On the RP2350 HDMI build the host either records the written
// [min, max) columns per raster row, so the raster never needs clearing, or
// converts the pixels straight to RGB332 into the SRAM scanout frame.
#ifdef PICO_BUILD
uint32_t *hostRasterPixels(int row, int x);
void hostRasterWritten(int row, int x, int width);
#else
static inline uint32_t *hostRasterPixels(int row, int x) {
    return &((uint32_t *)host.pixels)[row * HOST_RASTER_W + x];
}
static inline void hostRasterWritten(int row, int x, int width) {
    (void)row; (void)x; (void)width;
}
#endif

// Direct indexed rendering (RP2350 HDMI): while hostDirectActive, the DMA
// hands every bitplane block (HAM included) to these functions instead of
// hostRasterPixels()/hostRasterWritten().
#if defined(PICO_BUILD) && OMEGA_ENABLE_HDMI
extern int hostDirectActive;
// One sprite (or attached pair) on raster row `row` from raster column x
// (lores pixels, each 2 columns wide): colour base register, attached,
// behind the playfield, planes a/b (and c/d for an attached pair), bit 15
// leftmost.
void hostDirectSprite(int row, int x, int colour_base, int attached,
                      int behind, uint16_t a, uint16_t b, uint16_t c,
                      uint16_t d);
// Core 1 work loop (never returns): converts the enqueued blocks into the
// scanout frame. Started by dvi_display.c after HSTX scanout is running.
void hostCore1Loop(void);
void hostDirectHires(int row, int x, uint16_t p1, uint16_t p2,
                     uint16_t p3, uint16_t p4);
void hostDirectLores(int row, int x, uint16_t p1, uint16_t p2, uint16_t p3,
                     uint16_t p4, uint16_t p5, uint16_t p6, int ham);
// Colour register `reg` was set from `old` to `value` (0x0RGB) at the
// current beam position: pixels from that position on show it, even
// within a block already drawn or in the border.
void hostDirectColour(int reg, uint16_t old, uint16_t value);
// End of a line (`row`: its image row, or -1 outside the image; `drawn`:
// it had bitplane blocks): its messages go to core 1, or MSG_ROW_SAME if
// the row is unchanged, and a row without blocks shows COLOR00 as it is
// now (Copper colour changes outside the display window).
void hostDirectRowEnd(int row, int drawn);
#else
static inline void hostDirectColour(int reg, uint16_t old, uint16_t value) {
    (void)reg; (void)old; (void)value;
}
static inline void hostDirectRowEnd(int row, int drawn) {
    (void)row; (void)drawn;
}
#define hostDirectActive 0
static inline void hostDirectSprite(int row, int x, int colour_base,
                                    int attached, int behind, uint16_t a,
                                    uint16_t b, uint16_t c, uint16_t d) {
    (void)row; (void)x; (void)colour_base; (void)attached; (void)behind;
    (void)a; (void)b; (void)c; (void)d;
}
static inline void hostDirectHires(int row, int x, uint16_t p1, uint16_t p2,
                                   uint16_t p3, uint16_t p4) {
    (void)row; (void)x; (void)p1; (void)p2; (void)p3; (void)p4;
}
static inline void hostDirectLores(int row, int x, uint16_t p1, uint16_t p2,
                                   uint16_t p3, uint16_t p4, uint16_t p5,
                                   uint16_t p6, int ham) {
    (void)row; (void)x; (void)p1; (void)p2; (void)p3; (void)p4;
    (void)p5; (void)p6; (void)ham;
}
#endif

// ── Pixel-conversion helpers (same signature as the SDL version) ──────────
void hiresPlanar2Chunky(uint32_t *pixBuff,
                        uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4);
void loresPlanar2Chunky(uint32_t *pixBuff,
                        uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4,
                        uint16_t p5, uint16_t p6);
void loresHAM2Chunky(uint32_t *pixBuff,
                     uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4,
                     uint16_t p5, uint16_t p6);
// ── Keyboard / mouse ──────────────────────────────────────────────────────
// Keys by SDL key code (original Omega table; the native SDL build).
void pressKey(uint16_t keyCode);
void releaseKey(uint16_t keyCode);
// One Amiga raw keycode (0x00-0x67) pressed or released (USB keyboards).
void hostAmigaKey(uint8_t code, int released);
// Set by Ctrl + both Amiga keys; the main loop calls cpu_keyboard_reset().
extern int hostResetRequested;

// ── Lifecycle ─────────────────────────────────────────────────────────────
void hostInit(void);
// Called once per frame: push framebuffer to display hardware, poll input
void hostDisplay(void);

void toggleLEDs(void);
