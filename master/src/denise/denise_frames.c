// frank-amiga — indexed Amiga frames and their scanout expansion (master).
//
// SPDX-License-Identifier: MPL-2.0
// From github.com/fboldog/rp2350-amiga src/dvi_display.c: the frame
// handshake, the frame records and the row expansion (scan_*), unchanged
// except that colours go through the video mode's RGB range and that rows
// are delivered to master/src/video/hstx_video.c's row callback instead of
// that file's own HSTX ring. Interlace weaving is not ported yet.

#include "denise_frames.h"
#include "video/hstx_video.h"
#include "../omega/DisplayLayout.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "link_pins.h"
#include <string.h>

#define AMIGA_SOURCE_WIDTH 640u
#define AMIGA_SOURCE_HEIGHT ((uint32_t)OMEGA_DIRECT_ROWS)
#define DVI_FRAME_W AMIGA_SOURCE_WIDTH
#define DVI_FRAME_H AMIGA_SOURCE_HEIGHT
#define DVI_FRAME_BYTES (DVI_FRAME_W * DVI_FRAME_H)

// The image frames live in SRAM; only the 640-wide image is stored, the
// borders come from each row's COLOR00.
static uint8_t sram_frames[2][DVI_FRAME_BYTES] __attribute__((aligned(4)));
static uint8_t *source_frames[2] = { sram_frames[0], sram_frames[1] };
static bool frame_indexed[2];

// Records in PSRAM (cached alias): core 1 writes them and the scanout
// interrupt (same core) reads them.
static dvi_indexed_frame_t *frame_records;
// Each frame buffer b alternates between records b and b + 2: core 1
// writes one while the other keeps the buffer's last finished frame, which
// the scanout replays and core 1 copies unchanged rows from (MSG_ROW_SAME).
volatile uint8_t frame_rec[2] = { 0, 1 };
static volatile uint8_t buf_rec[2] = { 0, 1 };
static int rec_target;

static inline dvi_indexed_frame_t *__not_in_flash_func(dvi_next_record)(
    int b, dvi_indexed_frame_t **prev) {
    *prev = &frame_records[buf_rec[b]];
    rec_target = buf_rec[b] == b ? b + 2 : b;
    return &frame_records[rec_target];
}

// Frame handshake, one atomic word: bit 0 is the displayed buffer, bit 1 set
// means the other buffer holds a finished frame waiting for the next display
// frame boundary (the scanout swaps at line 0).
#define FRAME_PENDING 2u
static volatile uint32_t frame_state;
volatile uint32_t dvi_frames_shown;
volatile uint32_t dvi_refreshes;

bool __not_in_flash_func(dvi_display_frame_pending)(void) {
    return (__atomic_load_n(&frame_state, __ATOMIC_ACQUIRE) & FRAME_PENDING) != 0;
}

static int __not_in_flash_func(dvi_claim_back_buffer)(void) {
    const uint32_t old = __atomic_fetch_and(&frame_state, ~FRAME_PENDING,
                                            __ATOMIC_ACQ_REL);
    return (int)(old & 1u) ^ 1;
}

// Border colour (COLOR00, RGB888) of each frame, for the lines above and
// below the image.
static uint32_t frame_border[2];

// 4-bit component -> 8 bits in the video mode's range (hstx_video.c).
static uint8_t nib8[16];
static inline uint32_t ocs_rgb888(uint32_t c) {
    return (uint32_t)nib8[(c >> 8) & 15u] << 16 | (uint32_t)nib8[(c >> 4) & 15u] << 8 |
           nib8[c & 15u];
}

static inline uint8_t *dvi_image_row(uint8_t *frame, uint y) {
    return frame + y * DVI_FRAME_W;
}

static int direct_target = -1;

int __not_in_flash_func(dvi_display_direct_height)(void) {
    return (int)AMIGA_SOURCE_HEIGHT;
}

uint8_t *__not_in_flash_func(dvi_display_direct_acquire)(
    dvi_indexed_frame_t **record, dvi_indexed_frame_t **prev, int *buffer) {
    direct_target = dvi_claim_back_buffer();
    *record = dvi_next_record(direct_target, prev);
    *buffer = direct_target;
    return source_frames[direct_target];
}

void __not_in_flash_func(dvi_display_direct_publish)(uint16_t border) {
    if (direct_target < 0)
        return;
    frame_indexed[direct_target] = true;
    frame_border[direct_target] = ocs_rgb888(border);
    buf_rec[direct_target] = (uint8_t)rec_target;
    __atomic_fetch_or(&frame_state, FRAME_PENDING, __ATOMIC_RELEASE);
    direct_target = -1;
}

// ── Row expansion (scan_*) ────────────────────────────────────────────────
// Scanout state of each indexed frame: its palette after `applied` log
// entries, as RGB888 (32..63 extra half-brite), and the HAM colour-load
// table.
typedef struct {
    uint32_t palette[64];
    uint32_t ham_set[64];
    uint32_t applied;
} scan_state_t;
static scan_state_t scan_states[2];
// Branchless HAM:
//   next = (previous & scan_ham_keep[code >> 4]) | ham_set[code].
static uint32_t scan_ham_keep[4] = { 0u, 0xffff00u, 0x00ffffu, 0xff00ffu };

static void dvi_init_ham_tables(void) {
    static const uint8_t shift[4] = { 0, 0, 16, 8 };
    for (uint f = 0; f < 2u; ++f)
        for (uint code = 16u; code < 64u; ++code)
            scan_states[f].ham_set[code] = (uint32_t)nib8[code & 0xfu] << shift[code >> 4];
}

#define SCAN_FUNC(name) \
    __attribute__((optimize("no-tree-loop-distribute-patterns"))) \
    __not_in_flash_func(name)

static inline void scan_set_register(scan_state_t *st, uint32_t reg,
                                     uint32_t colour) {
    const uint32_t rgb = ocs_rgb888(colour);
    st->palette[reg] = rgb;
    st->palette[reg + 32u] = ocs_rgb888((colour >> 1) & 0x777u);
    if (reg < 16u)
        st->ham_set[reg] = rgb;
}

static void SCAN_FUNC(scan_begin_frame)(int frame) {
    if (!frame_indexed[frame])
        return;
    scan_state_t *st = &scan_states[frame];
    const dvi_indexed_frame_t *rec = &frame_records[frame_rec[frame]];
    for (uint32_t i = 0; i < 32u; ++i)
        scan_set_register(st, i, rec->initial_palette[i]);
    st->applied = 0;
}

static inline void scan_apply_log(scan_state_t *st,
                                  const dvi_indexed_frame_t *rec,
                                  uint32_t pos) {
    while (st->applied < pos) {
        const uint32_t entry = rec->palette_log[st->applied++];
        scan_set_register(st, entry >> 12, entry & 0xfffu);
    }
}

static inline void scan_fill(uint32_t *out, uint32_t x0, uint32_t x1,
                             uint32_t colour) {
    for (uint32_t x = x0; x < x1; ++x)
        out[x] = colour;
}

static void SCAN_FUNC(scan_indexed)(const uint32_t *palette, uint32_t *out,
                                    const uint8_t *row, uint32_t x0,
                                    uint32_t x1) {
    uint32_t x = x0;
    for (; x < x1 && (x & 3u); ++x)
        out[x] = palette[row[x] & 63u];
    for (; x + 4u <= x1; x += 4u) {
        const uint32_t four = *(const uint32_t *)&row[x];
        out[x] = palette[four & 63u];
        out[x + 1u] = palette[(four >> 8) & 63u];
        out[x + 2u] = palette[(four >> 16) & 63u];
        out[x + 3u] = palette[(four >> 24) & 63u];
    }
    for (; x < x1; ++x)
        out[x] = palette[row[x] & 63u];
}

// HAM is LORES only, so every code covers an aligned pair of columns. A
// sprite pixel (0x40 | register) shows that colour register and leaves the
// held colour alone, as on real hardware.
static inline uint32_t scan_ham_pixel(const scan_state_t *st, uint32_t code,
                                      uint32_t *hold) {
    if (code & 0x40u)
        return st->palette[code & 0x1fu];
    code &= 63u;
    *hold = (*hold & scan_ham_keep[code >> 4]) | st->ham_set[code];
    return *hold;
}

static uint32_t SCAN_FUNC(scan_ham)(const scan_state_t *st, uint32_t *out,
                                    const uint8_t *row, uint32_t x0,
                                    uint32_t x1, uint32_t hold) {
    if ((x0 | x1) & 1u) {
        for (uint32_t x = x0; x < x1; ++x)
            out[x] = scan_ham_pixel(st, row[x], &hold);
        return hold;
    }
    for (uint32_t x = x0; x < x1; x += 2u) {
        const uint32_t pixel = scan_ham_pixel(st, row[x], &hold);
        out[x] = pixel;
        out[x + 1u] = pixel;
    }
    return hold;
}

static uint32_t scan_right_border;

// Expands image row y of `frame` into out[0..640); returns the row's left
// border colour (COLOR00 as it began) and sets scan_right_border.
static uint32_t SCAN_FUNC(scan_expand_row)(int frame, uint y, uint32_t *out) {
    const uint8_t *row = dvi_image_row(source_frames[frame], y);
    if (!frame_indexed[frame]) {
        scan_fill(out, 0, AMIGA_SOURCE_WIDTH, 0);
        scan_right_border = 0;
        return 0;
    }
    scan_state_t *st = &scan_states[frame];
    const dvi_indexed_frame_t *rec = &frame_records[frame_rec[frame]];
    const uint32_t first = rec->row_segment[y];
    const uint32_t last = rec->row_segment[y + 1u];
    if (first == last) {
        if (rec->segment_overflow && first == rec->segments_used) {
            scan_indexed(st->palette, out, row, 0, AMIGA_SOURCE_WIDTH);
        } else {
            uint32_t pos = rec->row_log[y];
            if (pos > rec->log_used)
                pos = rec->log_used;
            scan_apply_log(st, rec, pos);
            scan_fill(out, 0, AMIGA_SOURCE_WIDTH, st->palette[0]);
        }
        scan_right_border = st->palette[0];
        return st->palette[0];
    }
    scan_apply_log(st, rec, DVI_SEGMENT_POS(rec->segment[first]));
    const uint32_t border = st->palette[0];
    uint32_t min = AMIGA_SOURCE_WIDTH, max = 0;
    for (uint32_t i = first; i < last; ++i) {
        const uint32_t seg = rec->segment[i];
        if (DVI_SEGMENT_X0(seg) < min) min = DVI_SEGMENT_X0(seg);
        if (DVI_SEGMENT_X1(seg) > max) max = DVI_SEGMENT_X1(seg);
    }
    scan_fill(out, 0, min, border);
    uint32_t hold = border;
    for (uint32_t i = first; i < last; ++i) {
        const uint32_t seg = rec->segment[i];
        scan_apply_log(st, rec, DVI_SEGMENT_POS(seg));
        if (DVI_SEGMENT_HAM(seg))
            hold = scan_ham(st, out, row, DVI_SEGMENT_X0(seg),
                            DVI_SEGMENT_X1(seg), hold);
        else
            scan_indexed(st->palette, out, row, DVI_SEGMENT_X0(seg),
                         DVI_SEGMENT_X1(seg));
    }
    scan_right_border = st->palette[0];
    scan_fill(out, max, AMIGA_SOURCE_WIDTH, scan_right_border);
    return border;
}

// ── hstx_video callbacks ──────────────────────────────────────────────────
static int active_frame;
static uint32_t image_y0, image_x0;
/* Image rows hidden above the display when it has fewer rows than the
 * image (PAL's 256 in 640x480's 240): the overscan is cropped evenly. */
static uint32_t image_skip;

void SCAN_FUNC(denise_frame_start)(uint32_t frame) {
    (void)frame;
    dvi_refreshes++;
    /* frank-amiga: every display refresh toggles FS; the slave counts the
     * edges and never runs more than a frame ahead (main.c
     * host_frame_paced()), which locks emulation to the display. */
    gpio_xor_mask64(1ull << M_LINK_FS);
    uint32_t state = __atomic_load_n(&frame_state, __ATOMIC_ACQUIRE);
    if ((state & FRAME_PENDING) &&
        __atomic_compare_exchange_n(&frame_state, &state, (state & 1u) ^ 1u,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        active_frame = (int)((state & 1u) ^ 1u);
        dvi_frames_shown++;
    }
    frame_rec[active_frame] = buf_rec[active_frame];
    scan_begin_frame(active_frame);
}

void SCAN_FUNC(denise_row)(uint32_t row, uint32_t *out, uint32_t width) {
    const uint32_t y = row - image_y0 + image_skip;
    if (row < image_y0 || y >= AMIGA_SOURCE_HEIGHT) {
        scan_fill(out, 0, width, frame_border[active_frame]);
        return;
    }
    const uint32_t border = scan_expand_row(active_frame, y, out + image_x0);
    scan_fill(out, 0, image_x0, border);
    scan_fill(out, image_x0 + AMIGA_SOURCE_WIDTH, width, scan_right_border);
}

void denise_frames_init(void *records) {
    frame_records = (dvi_indexed_frame_t *)records;
    for (uint i = 0; i < 16u; ++i)
        nib8[i] = video_nibble_to_8(i);
    dvi_init_ham_tables();
    memset(sram_frames, 0, sizeof sram_frames);
    frame_state = 0;
    frame_indexed[0] = frame_indexed[1] = false;
    frame_border[0] = frame_border[1] = 0;
    image_x0 = (video_width() - AMIGA_SOURCE_WIDTH) / 2u;
    image_y0 = video_rows() > AMIGA_SOURCE_HEIGHT
                   ? (video_rows() - AMIGA_SOURCE_HEIGHT) / 2u : 0u;
    image_skip = video_rows() < AMIGA_SOURCE_HEIGHT
                   ? (AMIGA_SOURCE_HEIGHT - video_rows()) / 2u : 0u;
}
