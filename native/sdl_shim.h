// Minimal shim so the desktop (#ifndef PICO_BUILD) code paths in omega/DMA.c
// link without SDL2.  The core runs single-threaded here, so the CPU/DMA
// hand-off atomic is a no-op that always reports "slot free".
//
// display_sdl.c is compiled WITHOUT this shim (it includes SDL2/SDL.h directly)
// so there is no conflict between these stubs and SDL2's extern declarations.
#ifndef NATIVE_SDL_SHIM_H
#define NATIVE_SDL_SHIM_H

typedef struct { int value; } SDL_atomic_t;
static SDL_atomic_t cpuWait;

static inline int  SDL_AtomicGet(SDL_atomic_t *a)        { (void)a; return 0; }
static inline void SDL_AtomicSet(SDL_atomic_t *a, int v) { (void)a; (void)v; }

#endif
