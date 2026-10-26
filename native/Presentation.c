#include "Presentation.h"

#include "Host.h"
#include "../omega/Chipset.h"
#include "../omega/DisplayLayout.h"
#include <string.h>

enum {
    EARLY_DISPLAY_LINE = 64,
    WRAPPED_PREFIX_PIXELS = 48,
};

static void fillPixels(uint32_t *pixels, int count, uint32_t color) {
    for (int i = 0; i < count; ++i)
        pixels[i] = color;
}

void hostClearRaster(uint32_t *render_fb, uint32_t border) {
    fillPixels(render_fb, HOST_RASTER_PIXELS, border);
}

static void duplicateScanline(uint32_t *fb, int y) {
    memcpy(&fb[(y + 1) * SCREEN_W], &fb[y * SCREEN_W],
           SCREEN_W * sizeof(uint32_t));
}

static int rightmostContentPixel(const uint32_t *row, uint32_t border) {
    int right = HOST_VISIBLE_X1 - 1;
    while (right >= SCREEN_W / 2 && row[right] == border)
        right--;
    return right;
}

static int wrappedPrefixLength(const uint32_t *row, uint32_t border) {
    int limit = HOST_VISIBLE_X0 + WRAPPED_PREFIX_PIXELS;
    int end = limit;
    while (end > HOST_VISIBLE_X0 && row[end - 1] == border)
        end--;
    return end - HOST_VISIBLE_X0;
}

void hostPresentFrame(uint32_t *fb, uint32_t *render_fb) {
    const uint32_t border = internal.palette[0];
    const int diw_start = omegaDiwVerticalStart(chipset.diwstrt);
    const int viewport_y_offset =
        (OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_PAL &&
         diw_start < EARLY_DISPLAY_LINE)
        ? HOST_CONTENT_Y : OMEGA_VIDEO_VIEWPORT_Y_OFFSET;

    // Convert the raw DMA fetch raster to the visible display.  The first
    // fetch words are pipeline/overscan data and must be clipped, not wrapped
    // around to the opposite edge.
    fillPixels(fb, SCREEN_W * SCREEN_H, border);

    // Full-width fetches already occupy a native 640-pixel raster row. Present
    // each logical scanline twice vertically. Narrow wrapped fetch windows use
    // the pipeline reconstruction path below.
    if (omegaDdfIsFullWidth(chipset.ddfstrt)) {
        int row_rotation =
            omegaDdfRowRotation(chipset.ddfstrt, chipset.ddfstop);
        // Overscan LORES layouts can advance two beam rows for each logical
        // picture row.  Normal 200/256-line LORES displays occupy every row.
        int alternate_rows =
            host.displayIsLores &&
            omegaLoresUsesAlternateRasterRows(chipset.diwstrt,
                                               chipset.diwstop);
        int source_step = alternate_rows ? 2 : 1;
        int y_offset = host.displayIsLores && !alternate_rows
                     ? EARLY_DISPLAY_LINE : 0;
        int copy_width = SCREEN_W;
        int rows = HOST_RASTER_H / source_step;
        if (rows > (SCREEN_H - y_offset) / 2)
            rows = (SCREEN_H - y_offset) / 2;
        for (int y = 0; y < rows; ++y) {
            uint32_t *dst = &fb[(y_offset + y * 2) * SCREEN_W];
            const uint32_t *src = &render_fb[(y * source_step) * SCREEN_W];
            if (row_rotation) {
                int body = copy_width - row_rotation;
                memcpy(dst, src + row_rotation,
                       body * sizeof(uint32_t));
                memcpy(dst + body, src,
                       row_rotation * sizeof(uint32_t));
            } else {
                memcpy(dst, src, copy_width * sizeof(uint32_t));
            }
            duplicateScanline(fb, y_offset + y * 2);
        }
        hostClearRaster(render_fb, border);
        return;
    }

    for (int sy = 0; sy < HOST_RASTER_H; ++sy) {
        int dy = HOST_CONTENT_Y + sy * 2 - viewport_y_offset;
        if (dy < 0 || dy + 1 >= SCREEN_H)
            continue;
        memcpy(&fb[dy * SCREEN_W + HOST_VISIBLE_X0],
               &render_fb[sy * HOST_RASTER_W + HOST_FETCH_LEAD +
                          HOST_VISIBLE_X0],
               (HOST_VISIBLE_X1 - HOST_VISIBLE_X0) * sizeof(uint32_t));

        duplicateScanline(fb, dy);
    }

    // The fetch pipeline places the next logical scanline's prefix after the
    // current line.  Rejoin such prefixes to a right-edge object, then remove
    // them from the following line's left edge.
    int reconstruct_wrapped_fetch_rows =
        omegaDiwCrossesVerticalBank(chipset.diwstrt, chipset.diwstop);
    for (int sy = 0;
         reconstruct_wrapped_fetch_rows && sy + 1 < HOST_RASTER_H; ++sy) {
        int dy = HOST_CONTENT_Y + sy * 2 - viewport_y_offset;
        if (dy < 0 || dy + 3 >= SCREEN_H)
            continue;
        uint32_t *row = &fb[dy * SCREEN_W];
        uint32_t *next = row + SCREEN_W * 2;
        int right = rightmostContentPixel(row, border);
        int count = wrappedPrefixLength(next, border);
        if (right < SCREEN_W / 2 || count == 0)
            continue;

        if (count > SCREEN_W - right - 1) count = SCREEN_W - right - 1;
        for (int x = 0; x < count; ++x) {
            row[right + 1 + x] = next[HOST_VISIBLE_X0 + x];
            next[HOST_VISIBLE_X0 + x] = border;
        }
        duplicateScanline(fb, dy);
        duplicateScanline(fb, dy + 2);
    }
    if (reconstruct_wrapped_fetch_rows) {
        for (int sy = 0; sy < HOST_RASTER_H; ++sy) {
            int dy = HOST_CONTENT_Y + sy * 2 - viewport_y_offset;
            if (dy < 0 || dy + 1 >= SCREEN_H)
                continue;
            uint32_t *row = &fb[dy * SCREEN_W];
            for (int x = HOST_VISIBLE_X0;
                 x < HOST_VISIBLE_X0 + WRAPPED_PREFIX_PIXELS; ++x)
                row[x] = border;
            // Prefix reconstruction uses raw fetch coordinates. Remove that
            // raster's left origin to keep the complete logical row visible.
            memmove(row, row + HOST_VISIBLE_X0,
                    (SCREEN_W - HOST_VISIBLE_X0) * sizeof(uint32_t));
            fillPixels(row + SCREEN_W - HOST_VISIBLE_X0,
                       HOST_VISIBLE_X0, border);
            duplicateScanline(fb, dy);
        }
    }

    hostClearRaster(render_fb, border);
}
