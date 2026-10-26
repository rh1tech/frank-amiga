#pragma once
#include <stdint.h>

void sdl_display_open(int w, int h);
void sdl_display_push(const uint32_t *argb32);
// A frame of another size (the RTG card's): the window follows it.
void sdl_display_push_size(const uint32_t *argb32, int w, int h);
int  sdl_display_poll(void);   /* returns 1 if user closed the window */
void sdl_display_close(void);
