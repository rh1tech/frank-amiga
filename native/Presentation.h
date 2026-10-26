#pragma once

#include <stdint.h>

void hostPresentFrame(uint32_t *fb, uint32_t *render_fb);
void hostClearRaster(uint32_t *render_fb, uint32_t border);
