#include <time.h>
#include <stdlib.h>
// SDL-free desktop host layer for running the Omega core natively (no RP2350,
// no PSRAM, no display hardware).  Renders planar->chunky into a plain malloc'd
// framebuffer so the emulator core can be exercised head-less on a PC.
//
// The planar2chunky routines are byte-for-byte the same as src/Host.c (they are
// platform independent); only hostInit / hostDisplay differ.

#include "Host.h"
#include "Presentation.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/CPU.h"
#include "../omega/VideoStandard.h"
#include "../omega/Picasso2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SDL2
#include "display_sdl.h"
#endif

Host_t host;

static uint32_t *fb;                    // presented SCREEN_W * SCREEN_H image
static uint32_t *render_fb;             // intermediate DMA beam raster
// The Picasso II+'s picture, while its monitor switch shows it.
static uint32_t *rtg_fb;
static int rtg_w, rtg_h, rtg_shown;
unsigned long native_frame_counter = 0; // bumped by hostDisplay()

// ── Amiga key-code table (unused head-less, kept for link compatibility) ──
static const uint8_t keyMapping[] = { 0 };

void pressKey(uint16_t keyCode)   { (void)keyCode; }
void releaseKey(uint16_t keyCode) { (void)keyCode; }
void toggleLEDs(void)             { }

// ── Audio: OMEGA_PCM=<file> writes 48 kHz signed 16-bit stereo PCM ───────
// (play or convert with e.g. ffmpeg -f s16le -ar 48000 -ac 2 -i <file> ...)
#include "Audio.h"
// Paula's line records are mixed straight away (the RP2350 does it on core 1).
int hostAudioLine(const uint32_t *words, int count) {
    audioMixLine(words, count);
    return 1;
}

void hostAudioOut(const int16_t *samples, int frames) {
    static FILE *pcm;
    static int opened;
    if (!opened) {
        opened = 1;
        const char *path = getenv("OMEGA_PCM");
        if (path)
            pcm = fopen(path, "wb");
    }
    if (!pcm)
        return;
    static const int16_t silence[2 * 8];
    fwrite(samples ? samples : silence, 2 * sizeof(int16_t), (size_t)frames, pcm);
}

// ── Lifecycle ────────────────────────────────────────────────────────────
void hostInit(void) {
    fb = calloc(SCREEN_W * SCREEN_H, sizeof(uint32_t));
    render_fb = calloc(HOST_RASTER_PIXELS, sizeof(uint32_t));
    if (!fb || !render_fb) {
        fprintf(stderr, "Host init: framebuffer allocation failed\n");
        exit(1);
    }
    host.pixels    = render_fb;
    host.rasterRow = 0;
    host.rasterX   = 0;
    host.displayIsLores = 0;
    printf("Host init (native): framebuffer %p (%dx%d ARGB)\n",
           (void *)fb, SCREEN_W, SCREEN_H);
}

void hostDisplay(void) {
    native_frame_counter++;
    {   // frank-amiga: OMEGA_FRAME_TIMES=1 prints the host time of every 50 frames
        static int on = -1;
        static struct timespec t0;
        if (on < 0) { on = getenv("OMEGA_FRAME_TIMES") != NULL; clock_gettime(CLOCK_MONOTONIC, &t0); }
        if (on && native_frame_counter % 50 == 0) {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            const double ms = (t.tv_sec - t0.tv_sec) * 1e3 + (t.tv_nsec - t0.tv_nsec) / 1e6;
            printf("FT %lu %.2f ms/frame\n", native_frame_counter, ms / 50.0);
            t0 = t;
        }
    }
    rtg_shown = p2Showing();
    if (rtg_shown) {
        int w, h;
        p2Geometry(&w, &h);
        if (w != rtg_w || h != rtg_h) {
            free(rtg_fb);
            rtg_fb = calloc((size_t)w * h, sizeof(uint32_t));
            rtg_w = w;
            rtg_h = h;
        }
        for (int y = 0; y < h; y++)
            p2RenderLine(y, rtg_fb + (size_t)y * w);
#ifdef HAVE_SDL2
        sdl_display_push_size(rtg_fb, w, h);
#endif
        return;
    }
    hostPresentFrame(fb, render_fb);
#ifdef HAVE_SDL2
    sdl_display_push_size(fb, SCREEN_W, SCREEN_H);
#endif
}

// Dump the current framebuffer to a binary PPM (P6).  Called from main.
void native_dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    // the RTG card's picture while it is on the monitor
    const uint32_t *src = rtg_shown ? rtg_fb : fb;
    const int w = rtg_shown ? rtg_w : SCREEN_W, h = rtg_shown ? rtg_h : SCREEN_H;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        uint32_t p = src[i];                // ARGB (0x00RRGGBB from palette)
        unsigned char rgb[3] = {
            (unsigned char)((p >> 16) & 0xFF),
            (unsigned char)((p >>  8) & 0xFF),
            (unsigned char)( p        & 0xFF),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// Count non-black pixels – a cheap "is anything being drawn?" probe.
unsigned long native_nonblack_pixels(void) {
    unsigned long n = 0;
    for (int i = 0; i < SCREEN_W * SCREEN_H; ++i)
        if ((fb[i] & 0x00FFFFFF) != 0) ++n;
    return n;
}
