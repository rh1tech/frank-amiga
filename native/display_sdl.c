#include "display_sdl.h"
#include "../omega/VideoStandard.h"
#include <SDL.h>
#include <stdio.h>

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static int           tw, th;
static uint64_t      next_frame_tick;
static uint64_t      performance_frequency;

void sdl_display_open(int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* nearest-neighbour */
    win = SDL_CreateWindow("Omega Amiga",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w * 2, h * 2,
        SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return; }
    SDL_RenderSetLogicalSize(ren, w, h);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                            SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return; }
    tw = w; th = h;
    performance_frequency = SDL_GetPerformanceFrequency();
    next_frame_tick = SDL_GetPerformanceCounter();
}

void sdl_display_push_size(const uint32_t *argb32, int w, int h) {
    if (!tex) return;
    if (w != tw || h != th) {
        SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!t) return;
        SDL_DestroyTexture(tex);
        tex = t;
        tw = w; th = h;
        SDL_RenderSetLogicalSize(ren, w, h);
        SDL_SetWindowSize(win, w * 2 > 1600 ? w : w * 2,
                          w * 2 > 1600 ? h : h * 2);
    }
    sdl_display_push(argb32);
}

void sdl_display_push(const uint32_t *argb32) {
    if (!tex) return;

    // One call represents one complete emulated video frame.  Pace the
    // emulator here rather than merely dropping presentation calls: skipping
    // a draw still lets Kickstart advance its VBL-driven animation as fast as
    // the host CPU can run.
    const uint64_t frame_ticks =
        performance_frequency * OMEGA_VIDEO_RATE_DENOMINATOR /
        OMEGA_VIDEO_RATE_NUMERATOR;
    uint64_t now = SDL_GetPerformanceCounter();
    if (next_frame_tick > now) {
        uint64_t remaining_ms =
            (next_frame_tick - now) * 1000 / performance_frequency;
        if (remaining_ms > 1)
            SDL_Delay((uint32_t)(remaining_ms - 1));
        do {
            now = SDL_GetPerformanceCounter();
        } while (now < next_frame_tick);
    }

    SDL_UpdateTexture(tex, NULL, argb32, tw * (int)sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);

    next_frame_tick += frame_ticks;
    // Do not try to catch up after a debugger stop or a heavily delayed host.
    now = SDL_GetPerformanceCounter();
    if (next_frame_tick < now)
        next_frame_tick = now + frame_ticks;
}

int sdl_display_poll(void) {
    if (!win) return 0;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 1;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) return 1;
    }
    return 0;
}

void sdl_display_close(void) {
    if (tex) { SDL_DestroyTexture(tex);  tex = NULL; }
    if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
    if (win) { SDL_DestroyWindow(win);   win = NULL; }
    SDL_Quit();
}
