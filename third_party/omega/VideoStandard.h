#pragma once

// Shared compile-time video standard.  Keep these values numeric so both the
// shell build and CMake can pass OMEGA_VIDEO_STANDARD directly to the compiler.
#define OMEGA_VIDEO_NTSC 0
#define OMEGA_VIDEO_PAL  1

#ifndef OMEGA_VIDEO_STANDARD
#define OMEGA_VIDEO_STANDARD OMEGA_VIDEO_NTSC
#endif

#if OMEGA_VIDEO_RUNTIME
// frank-amiga: the standard chosen at boot (the board's ini, video=pal|ntsc)
// instead of at compile time: the same constants as below, from one variable
// that is set before the chipset starts and never changes while it runs.
// OMEGA_VIDEO_STANDARD only gives the default.
extern int omegaVideoNtsc;
#define OMEGA_VIDEO_IS_NTSC           (omegaVideoNtsc != 0)
#define OMEGA_VIDEO_NAME              (omegaVideoNtsc ? "NTSC" : "PAL")
#define OMEGA_VIDEO_FRAME_LINES       (omegaVideoNtsc ? 263 : 313)
#define OMEGA_VIDEO_VPOSR_ID          (omegaVideoNtsc ? 0x1000 : 0x0000)
#define OMEGA_VIDEO_RATE_NUMERATOR    (omegaVideoNtsc ? 60000 : 50)
#define OMEGA_VIDEO_RATE_DENOMINATOR  (omegaVideoNtsc ? 1001 : 1)
#define OMEGA_VIDEO_NATIVE_HEIGHT     512
#define OMEGA_VIDEO_VIEWPORT_Y_OFFSET (omegaVideoNtsc ? 40 : 96)
#define OMEGA_SPRITE_FIRST_LINE       (omegaVideoNtsc ? 20 : 25)
#define OMEGA_VIDEO_LONG_LINES        (omegaVideoNtsc ? 1 : 0)
#elif OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_PAL
#define OMEGA_VIDEO_IS_NTSC           0
#define OMEGA_VIDEO_NAME              "PAL"
#define OMEGA_VIDEO_FRAME_LINES       313
#define OMEGA_VIDEO_VPOSR_ID          0x0000
#define OMEGA_VIDEO_RATE_NUMERATOR    50
#define OMEGA_VIDEO_RATE_DENOMINATOR  1
#define OMEGA_VIDEO_NATIVE_HEIGHT     512
#define OMEGA_VIDEO_VIEWPORT_Y_OFFSET 96
// First line after vertical blanking: sprite DMA fetches SPRxPOS/CTL here.
#define OMEGA_SPRITE_FIRST_LINE       25
// Colour clocks per line: 227 (hPos 0..0xE2).
#define OMEGA_VIDEO_LONG_LINES        0
#elif OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_NTSC
#define OMEGA_VIDEO_IS_NTSC           1
#define OMEGA_VIDEO_NAME              "NTSC"
#define OMEGA_VIDEO_FRAME_LINES       263
#define OMEGA_VIDEO_VPOSR_ID          0x1000
#define OMEGA_VIDEO_RATE_NUMERATOR    60000
#define OMEGA_VIDEO_RATE_DENOMINATOR  1001
#define OMEGA_VIDEO_NATIVE_HEIGHT     400
#define OMEGA_VIDEO_VIEWPORT_Y_OFFSET 40
#define OMEGA_SPRITE_FIRST_LINE       20
// Colour clocks per line: 228 and 227 alternately (hPos 0..0xE3 on long
// lines), 227.5 on average.
#define OMEGA_VIDEO_LONG_LINES        1
#else
#error "OMEGA_VIDEO_STANDARD must be OMEGA_VIDEO_NTSC (0) or OMEGA_VIDEO_PAL (1)"
#endif

// Twice the average colour clocks per line (PAL 454, NTSC 455).
#define OMEGA_VIDEO_LINE_CCK2 (454 + OMEGA_VIDEO_LONG_LINES)

// Lores beam position of the first pixel of the first fetched bitplane word,
// relative to 2 * DDFSTRT (OCS: +17 LORES, +9 HIRES). Sprites are placed
// against it.
#ifndef OMEGA_SPRITE_LORES_OFFSET
#define OMEGA_SPRITE_LORES_OFFSET 17
#endif
#ifndef OMEGA_SPRITE_HIRES_OFFSET
#define OMEGA_SPRITE_HIRES_OFFSET 9
#endif
