// frank-amiga — slave host layer: the emulator's display/audio producer.
//
// SPDX-License-Identifier: MPL-2.0
// The producer half of github.com/fboldog/rp2350-amiga src/Host.c (core 0 ->
// core 1 message ring, HostRing.h), unchanged except where marked. On this
// board the consumer is not core 1 of the same chip: slave core 1 is the
// link pump (link_pump.c) that streams the ring to the master, where
// Denise is rendered. Paula's line records join the same ring
// (MSG_AUDIO), so video and audio arrive at the master in emulated-time
// order.
//
// frank-amiga: the messages are built on core 1. Core 0 (the emulator)
// only appends compact records of what it produced (bitplane blocks,
// colour writes, sprites, line and frame ends, Paula's line records) to an
// event queue; core 1 turns them into messages between link requests (the
// emulator's share of State of the Art's chunky part was ~12 %). Core 1
// keeps its own copy of the colour registers from the colour records, so
// nothing here reads the chipset while core 0 runs on.

#include "Host.h"
#include "HostRing.h"
#include "host_link.h"
#include "parallel.h"
#include "link_pump.h"
#include "../omega/DisplayLayout.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/Audio.h"
#include "../omega/CPU.h"
#include "../omega/DMA.h"
#include "../omega/VideoStandard.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include <stdio.h>
#include <string.h>

Host_t host;

// The ARGB raster fallback is never used here (every frame is direct), but
// DMA.c still references it; a scratch line keeps a stray call harmless.
// (one block at most: 32 pixels, see Planar.c)
static uint32_t raster_scratch[64];
uint32_t *hostRasterPixels(int row, int x) { (void)row; (void)x; return raster_scratch; }
void hostRasterWritten(int row, int x, int width) { (void)row; (void)x; (void)width; }

// frank-amiga: image rows of the direct frame, as dvi_display.c defined them.
#define DVI_MAX_ROWS 256
static inline int dvi_display_direct_height(void) { return OMEGA_DIRECT_ROWS; }

// Direct indexed rendering: for full-width and plain narrow fetch layouts,
// bitplane pixels go straight into the free SRAM scanout frame as colour
// register numbers (HAM as raw codes), so the ARGB raster in PSRAM is
// neither written nor read. Scanout expands them to exact 12-bit colours
// (dvi_indexed_frame_t in dvi_display.h).
//
// The conversion runs on core 1. Core 0 (the emulator) only enqueues each
// fetched bitplane block, palette updates and frame begin/end into a
// single-producer/single-consumer ring of 32-bit words; core 1 drains it in
// order between its HSTX line interrupts and logs every palette change
// against the pixels drawn after it, so Copper palette changes still hit the
// right pixels. Core 1 owns the scanout frame, its record and its palette
// copy.
#define DIRECT_MAX_ROWS DVI_MAX_ROWS
static bool direct_frame;         // this frame renders straight to the frame
int hostDirectActive;             // mirrors direct_frame for DMA.c

#define SPRITE_X_BIAS 64   // raster x + bias fits the 10-bit field

// Event queue records (core 0 -> core 1, see host_events_drain()).
enum {
    EV_PAD,         // the rest of the buffer is unused
    EV_LORES,       // + 3 words: planes 1/2, 3/4, 5/6; row, x, flags
    EV_HIRES,       // + 2 words: planes 1/2, 3/4; row, x, flags
    EV_COLOUR,      // + 1 word: 0x0RGB; register, row, column, flags
    EV_SPRITE,      // + 3 words: the sprite message
    EV_ROW_END,     // row + 1 (bits 4..15), drawn (bit 16)
    EV_AUDIO,       // + count words (bits 4..): Paula's line record
    EV_FRAME,       // + 2 words: DDFSTRT | DDFSTOP << 16, BPLCON0 | LOF << 16
    EV_BEGIN,       // the same, first frame (no frame end before)
    EV_RESTORED,    // a savestate was loaded
    EV_LAYOUT,      // first raster column reference (bits 4..15), HAM (bit 16)
    EV_CLIP,        // + 1 word: x0 | x1 << 16 (raster columns); row (bits 4..12)
};
#define EV_HAM (1u << 23)
#define EV_DPF (1u << 24)
#define EV_PF2PRI (1u << 25)
#define EV_DPF_DELTA_SHIFT 26     // PF2H - PF1H + 16, 5 bits
#define EV_SPF (1u << 31)         // with EV_DPF: one playfield, its odd and
                                  // even planes scrolled apart
static void evFrame(uint32_t type);
// Core 1: the line layout colour writes are placed with (EV_LAYOUT).
static int c1_layout_first = 2 * 0x38 + OMEGA_SPRITE_LORES_OFFSET, c1_layout_ham;
// Message format: HostRing.h.

// Core 1 waits for a pending frame to be shown while core 0 keeps queueing;
// when the ring is full core 0 stalls until the swap. A bigger ring lets
// emulation run further ahead of the display. 64 KB holds a whole frame's
// messages across the wait, so every emulated frame is shown (idle
// Workbench emulated/shown: PAL 28.2/21.7 with 24 KB, 28.1/21.9 with 32 KB,
// 28.0/28.0 with 64 KB; NTSC 33.6/32.5 with 44 KB, 33.6/33.6 with 64 KB;
// larger rings change nothing). Any size works: positions run over
// [0, 2 * RING_WORDS).
#ifndef RING_WORDS
#define RING_WORDS 8192u // 32 KB (frank-amiga: filled by core 1 itself)
#endif
#define RING_SPAN (2u * RING_WORDS)
// Ring fill at which core 1 stops waiting for the pending frame's swap and
// skips drawing the new frame. The default skips once core 0 can no longer
// queue the largest message (MSG_MAX_WORDS), i.e. just before it would stall;
// RING_WORDS never skips, which locks emulation to the display cadence (PAL
// idle Workbench: 25.0/25.0 emulated/shown instead of ~29.5/20.5, as a frame
// is several times the ring and cannot absorb the wait for the swap).
#ifndef OMEGA_RING_SKIP_WORDS
#define OMEGA_RING_SKIP_WORDS (RING_WORDS - MSG_MAX_WORDS)
#endif
static uint32_t ring[RING_WORDS];
static volatile uint32_t ring_head; // published position (core 0)
static volatile uint32_t ring_tail; // consume position (core 1)
// Core 0's write position. A line's messages are published (ring_head
// moves up to it) at the end of the line, so an unchanged image row can
// still be taken back and replaced by MSG_ROW_SAME (hostDirectRowEnd()).
static uint32_t ring_wr;

static inline void ringPublish(void) {
    if (ring_head != ring_wr) {
        __atomic_store_n(&ring_head, ring_wr, __ATOMIC_RELEASE);
        __sev(); // wake core 1 from __wfe()
    }
}

static inline uint32_t ringAdvance(uint32_t pos, uint32_t words) {
    pos += words;
    return pos >= RING_SPAN ? pos - RING_SPAN : pos;
}

static inline uint32_t ringFill(uint32_t head, uint32_t tail) {
    return head >= tail ? head - tail : head + RING_SPAN - tail;
}

// Ring slot of position pos (< RING_SPAN).
static inline uint32_t ringSlot(uint32_t pos) {
    return pos >= RING_WORDS ? pos - RING_WORDS : pos;
}

// The run being collected on core 0 (host_run, see HostRing.h). It is
// written straight into the ring past ring_wr and published with its line
// (ringPublish()), so core 1 never sees it early; only a run that
// could reach the end of the ring is collected in run_stage and copied in
// (core 1 copies such a message out).
static uint32_t run_stage[MSG_MAX_WORDS];
// The open line (hostDirectRowEnd()): where its messages start, whether it
// sent blocks, and whether colours changed in it (such a row is never
// replaced: its runs carry the colour positions).
static uint32_t row_start;
static bool row_has_blocks, row_colours_changed;
HostRun host_run = { run_stage, 0, 0, ~0u };

// ── Core 0: producer ──────────────────────────────────────────────────────
// frank-amiga: the consumer's position as last read. It only moves forward,
// so a stale copy under-states the room and is safe; the shared variable is
// read again only when the copy says the ring might be full.
static uint32_t tail_seen;
static inline void ringWaitRoom(uint32_t head, uint32_t count) {
    if (ringFill(head, tail_seen) + count <= RING_WORDS)
        return;
#ifdef DMA_SLOT_STATS
    extern uint32_t dma_cyc_wait;
    const uint32_t c0 = *(volatile uint32_t *)0xE0001004u;
#endif
    do {
        tail_seen = __atomic_load_n(&ring_tail, __ATOMIC_ACQUIRE);
    } while (ringFill(head, tail_seen) + count > RING_WORDS);
#ifdef DMA_SLOT_STATS
    dma_cyc_wait += *(volatile uint32_t *)0xE0001004u - c0;
#endif
}

static inline void ringPush(const uint32_t *words, uint32_t count) {
    uint32_t head = ring_wr;
    // Wait for room: the link drains faster than the emulator fills, so
    // this only waits after a burst.
    ringWaitRoom(head, count);
    uint32_t slot = ringSlot(head);
    if (slot + count <= RING_WORDS) {
        for (uint32_t i = 0; i < count; ++i)
            ring[slot + i] = words[i];
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            ring[slot] = words[i];
            if (++slot == RING_WORDS)
                slot = 0;
        }
    }
    ring_wr = ringAdvance(head, count);
}

static void __attribute__((noinline)) runPushStaged(void) {
    ringPush(run_stage, host_run.words);
}

static inline void runFlush(void) {
    if (!host_run.words)
        return;
    if (host_run.msg == run_stage) {
        runPushStaged();
    } else {
        ring_wr = ringAdvance(ring_wr, host_run.words);
    }
    host_run.words = 0;
}

// frank-amiga: colour changes inside the image are queued and sent at the
// row's end (or before any other kind of message): sent at once, each one
// ended the run of blocks being built, splitting a Copper-heavy row (State
// of the Art's chunky part: 36 changes a line) into dozens of runs. Core 1
// places a change by its column whatever the order. HAM rows keep sending
// at once: their pixels take the palette as the blocks arrive.
// They go as one message (the row of the first; core 1 takes each one's
// row from its own message order: all are on the open row).
#define COLOUR_QUEUE_MAX MSG_COLOUR_MAX
static uint32_t colour_queue[1 + COLOUR_QUEUE_MAX];
static uint32_t colour_queued;
static int colour_queue_row;

static void __attribute__((noinline)) colourQueueSend(void) {
    runFlush();
    colour_queue[0] = MSG_HEADER(MSG_COLOUR, colour_queue_row, 0, colour_queued);
    ringPush(colour_queue, 1u + colour_queued);
    colour_queued = 0;
}

static inline void colourQueueFlush(void) {
    if (colour_queued)
        colourQueueSend();
}

// Starts a run with this header where it will be published.
static void __attribute__((noinline)) runStart(uint32_t header) {
    // Room for the longest run, as ringPush() would wait for it.
    row_has_blocks = true;
    host_row_hash = hostMix(host_row_hash, header);
    const uint32_t head = ring_wr;
    ringWaitRoom(head, MSG_MAX_WORDS);
    const uint32_t slot = ringSlot(head);
    host_run.msg = slot + MSG_MAX_WORDS <= RING_WORDS ? &ring[slot] : run_stage;
    host_run.msg[0] = header;
    host_run.words = 1;
}

// Appends a block at x of `words` plane words to the run, first sending the
// run if the block does not continue it, and returns where to write the
// plane words. header has x = 0 and count 1.
static inline uint32_t *runAppend(uint32_t header, int x, int width,
                                  uint32_t words) {
    if (host_run.words && (x != host_run.next_x ||
                      (host_run.msg[0] & MSG_RUN_KEY_MASK) != header ||
                      (host_run.msg[0] & MSG_RUN_COUNT_MASK) == MSG_RUN_COUNT_MASK))
        runFlush();
    if (host_run.words)
        host_run.msg[0] += 1u << MSG_RUN_SHIFT;
    else
        runStart(header | MSG_HEADER(0, 0, x, 0));
    uint32_t *planes = &host_run.msg[host_run.words];
    host_run.words += words;
    host_run.next_x = x + width;
    return planes;
}

// internal.palette holds OCS2ARGB() colours: the high nibbles are the
// original 0x0RGB register value.
static inline uint32_t argbToOcs(uint32_t argb) {
    return (argb >> 12 & 0xf00u) | (argb >> 8 & 0xf0u) | (argb >> 4 & 0xfu);
}

// Out of line, like the other rare paths, so the per-block producers stay
// small (no large stack frame or register saves on every block).
// Sends the colours core 1 does not have yet; register `except` (if >= 0)
// as `colour`. core1_palette is core 1's palette as of the messages sent;
// invalid at each frame, so a frame core 1 resynchronised at starts with
// the whole palette.
static uint16_t core1_palette[32];
static bool core1_palette_valid;
// The colour registers as of the records processed (core 1), their
// generation (+2 per write, as internal.paletteGeneration) and the
// registers written since the last palette comparison.
static uint16_t c1_colour[32];
static uint32_t c1_generation;
static uint32_t c1_changed;

static void __attribute__((noinline)) hostDirectSendPaletteWith(
    int except, uint16_t colour) {
    colourQueueFlush();
    host_run.palette_sent = c1_generation;
    row_colours_changed = true;
    runFlush(); // the blocks before the change use the old palette
    uint32_t msg[2 + 16];
    uint32_t mask = 0, n = 0;
    // Only registers written since the last comparison can differ.
    uint32_t check = core1_palette_valid ? c1_changed : ~0u;
    if (except >= 0)
        check |= 1u << except;
    c1_changed = 0;
    while (check) {
        const int i = __builtin_ctz(check);
        check &= check - 1u;
        const uint16_t c = i == except ? colour : c1_colour[i];
        if (core1_palette_valid && c == core1_palette[i])
            continue;
        core1_palette[i] = c;
        mask |= 1u << i;
        if (n & 1)
            msg[2 + n / 2] |= (uint32_t)c << 16;
        else
            msg[2 + n / 2] = c;
        n++;
    }
    core1_palette_valid = true;
    if (!mask)
        return;
    const uint32_t words = (n + 1) / 2;
    msg[0] = MSG_HEADER(MSG_PALETTE, 0, 0, words);
    msg[1] = mask;
    ringPush(msg, 2 + words);
}

static void hostDirectSendPalette(void) {
    hostDirectSendPaletteWith(-1, 0);
}

static inline void hostDirectSyncPalette(void) {
    if (c1_generation != host_run.palette_sent)
        hostDirectSendPalette();
}

// Palette generation as of the last row-end message (core 1 fills the rows
// in between with that message's position: nothing changed there).
static uint32_t row_end_generation;

// Unchanged rows (static screens: idle Workbench, xSysinfo) are not sent
// again: core 1 copies them from the buffer's last frame. Per image row,
// the hash of its messages and the frame it last changed in; in
// SCRATCH_Y (core 0's stack bank: its stack peaks at ~1 KB of 4 KB).
// frank-amiga: in main RAM, not SCRATCH_Y: the slave gives core 0 the whole
// 4 KB bank for its stack (upstream shared it, with a 2 KB stack).
static uint32_t row_hash[DIRECT_MAX_ROWS];
static uint32_t row_since[DIRECT_MAX_ROWS];
static uint32_t row_layout[2];
static uint32_t direct_frame_no;
// The previous row's messages went to core 1 (it was drawn afresh).
static bool prev_row_sent;
// Oldest frame either scanout buffer holds (core 1, 0: unknown).
static volatile uint32_t c1_oldest_frame;

// The frame's layout registers, as core 0 recorded them at its start.
typedef struct {
    uint16_t ddfstrt, ddfstop, bplcon0, lof;
} frame_regs_t;

static void hostDirectBegin(const frame_regs_t *fr) {
    runFlush();
    ringPublish();
    const bool full_width = omegaDdfIsFullWidth(fr->ddfstrt);
    // Narrow layouts that need hostPresentFrame()'s wrapped-fetch
    // reconstruction (pixels moved between lines) keep the ARGB path.
    const bool narrow = !full_width;
    direct_frame = full_width || narrow;
    if (!direct_frame)
        return;
    row_end_generation = host_run.palette_sent;   // core 1's palette
    // Rows are beam lines (DMA.c: line - OMEGA_DIRECT_FIRST_LINE), so every
    // layout maps one raster row to one image row.
    const int step = 1, first_row = 0;
    const int rotation =
        narrow ? 0 : omegaDdfRowRotation(fr->ddfstrt, fr->ddfstop);
    int height = dvi_display_direct_height();
    if (height > DIRECT_MAX_ROWS)
        height = DIRECT_MAX_ROWS;
    // Interlace (BPLCON0 LACE): which field this frame is (LOF: long).
    const uint32_t lace = (fr->bplcon0 & 0x4) != 0;
    const uint32_t msg[4] = {
        // frank-amiga: extra = the slave's time per frame (meter, 0.1 ms).
        MSG_HEADER(MSG_BEGIN, 0, 0,
                   host_emu_tenths_ms < 511u ? host_emu_tenths_ms : 511u),
        (uint16_t)first_row | (uint32_t)step << 16 | (uint32_t)narrow << 24 |
            lace << 25 | (uint32_t)(fr->lof & 1u) << 26,
        (uint32_t)rotation | (uint32_t)height << 16,
        ++direct_frame_no,
    };
    core1_palette_valid = false;
    // A new layout maps rows differently: nothing is unchanged.
    if (msg[1] != row_layout[0] || msg[2] != row_layout[1]) {
        row_layout[0] = msg[1];
        row_layout[1] = msg[2];
        for (int r = 0; r < DIRECT_MAX_ROWS; ++r)
            row_since[r] = direct_frame_no;
    }
    ringPush(msg, count_of(msg));
    ringPublish();
    row_start = ring_wr;
    prev_row_sent = false;
}

// A colour change goes to core 1 with its beam row and raster column, so it
// takes effect there instead of at the next block (ATK's colour bars: the
// Copper changes COLOR00 every 22 colour clocks, which block boundaries
// rounded to 64 or 96 columns). Earlier changes not sent yet (between
// frames) go as the full palette first.
// Core 1: colour register `reg` set to `value`; `in_image`: the write is
// inside the image's columns, at raster column x of image row `row`; ham:
// the line is HAM.
static void c1ColourWrite(int reg, uint16_t value, int in_image, int row,
                          int x, int ham) {
    const uint16_t old = c1_colour[reg];
    c1_colour[reg] = value;
    c1_generation += 2;
    c1_changed |= 1u << reg;
    if (!direct_frame || !in_image)
        return;
    // Earlier changes still pending go first, with this register as it
    // was before this change.
    if (host_run.palette_sent != c1_generation - 2u)
        hostDirectSendPaletteWith(reg, old);
    host_run.palette_sent = c1_generation;
    row_colours_changed = true;
    core1_palette[reg] = value;
    if (colour_queued && (row != colour_queue_row || colour_queued == COLOUR_QUEUE_MAX))
        colourQueueSend();
    if (!colour_queued)
        colour_queue_row = row;
    colour_queue[1 + colour_queued++] = MSG_COLOUR_WORD(x, reg, value);
    // HAM rows send at once: their pixels take the palette as the blocks
    // arrive.
    if (ham)
        colourQueueSend();
}

// Only rows without blocks need their own colour position (rows with
// blocks take their border from their first run), and only when the
// palette changed since the last message: RemGame sends none, ATK four.
uint32_t host_row_hash = 0x811c9dc5u;

static void hostDirectRowEndVideo(int row, int drawn) {
    colourQueueFlush();
    runFlush();
    bool sent = false;
    if (row >= 0 && row < DIRECT_MAX_ROWS && row_has_blocks) {
        const uint32_t h = row_colours_changed ? 0u : host_row_hash | 1u;
        sent = true;
        if (h && h == row_hash[row]) {
            // Unchanged since row_since[row]: if every buffer core 1 may
            // draw into holds that frame or a later one, it already has the
            // row; take its messages back and let core 1 copy the row.
            // A rotated line's first words are the right edge of the row
            // above: they go again when that row was drawn afresh.
            const uint32_t oldest =
                __atomic_load_n(&c1_oldest_frame, __ATOMIC_ACQUIRE);
            const bool rotated = (row_layout[1] & 0xffffu) != 0;
            if (oldest && (int32_t)(oldest - row_since[row]) >= 0 &&
                !(rotated && prev_row_sent)) {
                ring_wr = row_start;
                runStart(MSG_HEADER(MSG_ROW_SAME, row, 0, 0)); // one word
                runFlush();
                sent = false;
            }
        } else {
            row_hash[row] = h;
            row_since[row] = direct_frame_no;
        }
    }
    prev_row_sent = sent;
    row_has_blocks = false;
    row_colours_changed = false;
    host_row_hash = 0x811c9dc5u;
    ringPublish();
    row_start = ring_wr;
    if (row < 0 || drawn || c1_generation == row_end_generation)
        return;
    row_end_generation = c1_generation;
    hostDirectSyncPalette();
    runFlush();
    runStart(MSG_HEADER(MSG_ROW_END, row, 0, 0));   // a one-word message
    runFlush();
}

// frank-amiga: dual playfield. The renderer needs the playfields' relative
// scroll and priority per row; sent before the row's first DPF block and
// whenever they change within it (Copper-driven parallax).
volatile uint32_t host_dpf_rows;
static int dpf_row = -1;
static uint32_t dpf_state = ~0u;
// The playfields' scroll difference (PF2H - PF1H, lores pixels) goes to
// the master with the row's state; it places PF2 at its own offset
// (dpfPlace()). Shifting the delayed playfield's plane words here instead
// broke Desert Dream's intro globe and Psychoball's balls (PF1 delayed) and
// Psychoball's scroller (PF2 delayed) into 16-pixel strips.

// ev: a block record's header (EV_DPF..., see the event queue below).
static inline uint32_t dpfBlockFlag(int row, uint32_t ev) {
    if (!(ev & EV_DPF))
        return 0;
    const int delta = (int)((ev >> EV_DPF_DELTA_SHIFT) & 31u) - 16;
    const uint32_t st = ((ev & EV_SPF) ? 1u << 6 : 0u) |
                        ((ev & EV_PF2PRI) ? 1u << 5 : 0u) | ((uint32_t)delta & 31u);
    if (row != dpf_row || st != dpf_state) {
        dpf_row = row;
        dpf_state = st;
        colourQueueFlush();
        runFlush();
        host_dpf_rows++;
        const uint32_t msg = MSG_HEADER(MSG_DPF, row, 0, st);
        hostRowMix2(msg, 0);
        ringPush(&msg, 1);
    }
    return MSG_RUN_DPF;
}

static void c1HiresBlock(int row, int x, uint16_t p1, uint16_t p2,
                         uint16_t p3, uint16_t p4, uint32_t ev) {
    hostDirectSyncPalette();
    const uint32_t dpf = dpfBlockFlag(row, ev);
    uint32_t *planes = runAppend(MSG_HEADER(MSG_HIRES, row, 0, 0) | dpf, x, 16, 2);
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    hostRowMix2(planes[0], planes[1]);
}

static void c1LoresBlock(int row, int x, uint16_t p1, uint16_t p2, uint16_t p3,
                         uint16_t p4, uint16_t p5, uint16_t p6, int ham,
                         uint32_t ev) {
    hostDirectSyncPalette();
    const uint32_t dpf = ham ? 0 : dpfBlockFlag(row, ev);
    uint32_t *planes =
        runAppend(MSG_HEADER(MSG_LORES, row, 0, ham ? 1 : 0) | dpf, x, 32, 3);
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    planes[2] = p5 | (uint32_t)p6 << 16;
    hostRowMix3(planes[0], planes[1], planes[2]);
}

// Core 1: the row's horizontal display window (hostDirectClip()), inside
// the row's messages so a row taken back for MSG_ROW_SAME takes it too.
static void c1Clip(int row, uint32_t x01) {
    colourQueueFlush();
    runFlush();
    const uint32_t msg[2] = { MSG_HEADER(MSG_CLIP, row, 0, 0), x01 };
    hostRowMix2(msg[0], msg[1]);
    ringPush(msg, 2);
}

// Core 1: a sprite message (built by core 0, 2 or 3 words).
static void c1Sprite(const uint32_t *msg) {
    const bool attached = (msg[0] >> 28) & 1u;
    hostDirectSyncPalette();
    colourQueueFlush();
    runFlush(); // sprites go over the blocks already sent
    hostRowMix3(msg[0], msg[1], attached ? msg[2] : 0u);
    ringPush(msg, attached ? 3u : 2u);
}

static void hostDirectFinish(void) {
    colourQueueFlush();
    hostDirectSyncPalette();
    runFlush();
    const uint32_t msg[2] = {
        MSG_HEADER(MSG_END, 0, 0, 0),
        c1_colour[0],
    };
    ringPush(msg, count_of(msg));
    ringPublish();
    static bool first_frame_queued;
    if (!first_frame_queued) {
        first_frame_queued = true;
        printf("host: first frame queued\n");
    }
}

// ── Frame boundaries for the link pump ─────────────────────────────────────
// frank-amiga: the ring position of every MSG_BEGIN, so the pump can mark
// where a frame starts in its chunks (the master resynchronises there after
// a lost chunk) without parsing the message stream.
#define FRAME_MARKS 8u
static uint32_t frame_mark[FRAME_MARKS];
static volatile uint32_t frame_mark_head, frame_mark_tail;   // free running

// ── Audio records (frank-amiga) ───────────────────────────────────────────
// Paula's per-line record (omega/Audio.c) is staged here and committed to
// the ring after the line's video messages, which may still be rolled back
// (MSG_ROW_SAME) until hostDirectRowEnd() has run.
static uint32_t audio_stage[1 + AUDIO_LINE_MAX_WORDS];
static uint32_t audio_stage_words;
volatile uint32_t hostAudioDrops;

static void c1AudioLine(const uint32_t *words, int count) {
    if (audio_stage_words || count > AUDIO_LINE_MAX_WORDS) {
        ++hostAudioDrops;
        return;
    }
    audio_stage[0] = MSG_HEADER(MSG_AUDIO, 0, 0, (uint32_t)count);
    for (int i = 0; i < count; ++i)
        audio_stage[1 + i] = words[i];
    audio_stage_words = 1u + (uint32_t)count;
}

void hostAudioOut(const int16_t *samples, int frames) {
    (void)samples; (void)frames;   // mixing happens on the master
}

static void c1RowEnd(int row, int drawn) {
    hostDirectRowEndVideo(row, drawn);
    runFlush();
    if (audio_stage_words) {
        ringPush(audio_stage, audio_stage_words);
        audio_stage_words = 0;
    }
    ringPublish();
    row_start = ring_wr;
}

// ── Consumer side, for the link pump on core 1 ─────────────────────────────
uint32_t host_ring_words(void) { return RING_WORDS; }
const uint32_t *host_ring_base(void) { return ring; }

uint32_t host_ring_pending(uint32_t *slot) {
    const uint32_t head = __atomic_load_n(&ring_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = ring_tail;
    *slot = ringSlot(tail);
    return ringFill(head, tail);
}

void host_ring_consume(uint32_t words) {
    __atomic_store_n(&ring_tail, ringAdvance(ring_tail, words), __ATOMIC_RELEASE);
}

// Pending frame starts, as ring positions (oldest first).
bool host_frame_mark_peek(uint32_t *pos) {
    if (frame_mark_tail == __atomic_load_n(&frame_mark_head, __ATOMIC_ACQUIRE))
        return false;
    *pos = frame_mark[frame_mark_tail % FRAME_MARKS];
    return true;
}
void host_frame_mark_pop(void) { frame_mark_tail++; }
// Forget every pending mark: after the ring was dropped they may name
// positions of an earlier lap (positions repeat every RING_SPAN words).
void host_frame_marks_clear(void) {
    frame_mark_tail = __atomic_load_n(&frame_mark_head, __ATOMIC_ACQUIRE);
}
uint32_t host_ring_tail(void) { return ring_tail; }
uint32_t host_ring_span(void) { return RING_SPAN; }

static void frame_mark_push(uint32_t pos) {
    // Drop the oldest mark if the pump is far behind: marks only speed up
    // resynchronisation, the stream itself is complete without them.
    if (frame_mark_head - frame_mark_tail >= FRAME_MARKS) return;
    frame_mark[frame_mark_head % FRAME_MARKS] = pos;
    __atomic_store_n(&frame_mark_head, frame_mark_head + 1u, __ATOMIC_RELEASE);
}

// ── Keyboard (Amiga raw keycodes, from the master) ─────────────────────────
int hostResetRequested;

// Sends one Amiga raw keycode (0x00-0x67) to the keyboard serial port, as
// the Amiga keyboard does: the byte is rotated left with the up/down flag
// in bit 0 and inverted. Ctrl + both Amiga keys resets the 68000.
// frank-amiga: a savestate was loaded (savestate.c): the machine jumped, so
// core 1's picture of it is stale. Every row is resent (as for a new
// layout), with the palette and the DPF state.
static void c1Restored(void) {
    row_layout[0] = row_layout[1] = ~0u;
    host_run.palette_sent = c1_generation - 1u;
    core1_palette_valid = false;
    dpf_row = -1;
    dpf_state = ~0u;
}

void hostAmigaKey(uint8_t code, int released) {
    CIAWrite(&CIAA, 0xC, (uint8_t)~((code << 1) | (released ? 1 : 0)));
    keyboardInt();
    static uint8_t resetKeys;  // bit 0 Ctrl, 1 left Amiga, 2 right Amiga
    const uint8_t bit = code == 0x63 ? 1 : code == 0x66 ? 2 : code == 0x67 ? 4 : 0;
    if (released)
        resetKeys &= (uint8_t)~bit;
    else
        resetKeys |= bit;
    if (resetKeys == 7)
        hostResetRequested = 1;  // done between slices (main loop)
}

void pressKey(uint16_t keyCode) { (void)keyCode; }
void releaseKey(uint16_t keyCode) { (void)keyCode; }
void toggleLEDs(void) {}


// ── Event queue: core 0 -> core 1 (frank-amiga) ───────────────────────────
// Records of 1..4+ words, the type in the low 4 bits of the first. A record
// never wraps: the tail of the buffer is skipped with EV_PAD.
#define EVQ_WORDS 16384u   // 64 KB
static uint32_t evq[EVQ_WORDS];
static volatile uint32_t evq_head, evq_tail;   // free running word positions
static uint32_t evq_tail_seen;                 // core 0's copy of evq_tail
volatile uint32_t host_evq_waits;              // core 0 found it full
volatile uint32_t host_evq_wait_us;            // ... and waited this long

static inline uint32_t evqRoom(uint32_t head) {
    return EVQ_WORDS - (head - evq_tail_seen);
}

// Core 0: room for an n-word record; returns where to write it.
static inline uint32_t *evqReserve(uint32_t n) {
    uint32_t head = evq_head;
    uint32_t slot = head % EVQ_WORDS;
    const uint32_t need = slot + n > EVQ_WORDS ? EVQ_WORDS - slot + n : n;
    if (evqRoom(head) < need) {
        host_evq_waits++;
        const uint32_t t0 = timer_hw->timerawl;
        do {
#ifdef FRANK_PARALLEL
            // The CPU's core writing a register: it is the consumer too.
            if (par_running && get_core_num() == 1u)
                pump_service();
#endif
            evq_tail_seen = __atomic_load_n(&evq_tail, __ATOMIC_ACQUIRE);
        } while (evqRoom(head) < need);
        host_evq_wait_us += timer_hw->timerawl - t0;
    }
    if (slot + n > EVQ_WORDS) {
        evq[slot] = EV_PAD;
        head += EVQ_WORDS - slot;
        __atomic_store_n(&evq_head, head, __ATOMIC_RELEASE);
        slot = 0;
    }
    return &evq[slot];
}

// The M33 makes its stores in program order and core 1 polls, so a plain
// store after the record's words publishes it (no barrier, no event).
static inline void evqCommit(uint32_t n) {
    __atomic_signal_fence(__ATOMIC_RELEASE);
    evq_head = evq_head + n;
}

// Header bits of a block record (dpfBlockFlag()).
// A single playfield whose even planes have another delay than its odd ones
// (BPLCON1 PF2H != PF1H) goes the same way: the master places the two plane
// sets apart and puts their bits back together (Arte's "sanity has
// invented" texture, BPLCON1 $00F7).
static inline uint32_t evBlockFlags(void) {
    const int delta = (int)((chipset.bplcon1 >> 4) & 15) - (int)(chipset.bplcon1 & 15);
    if (!(chipset.bplcon0 & 0x400))
        return delta ? EV_DPF | EV_SPF | (uint32_t)(delta + 16) << EV_DPF_DELTA_SHIFT : 0;
    return EV_DPF | ((chipset.bplcon2 >> 6) & 1u ? EV_PF2PRI : 0u) |
           (uint32_t)(delta + 16) << EV_DPF_DELTA_SHIFT;
}

void hostDirectHires(int row, int x, uint16_t p1, uint16_t p2,
                     uint16_t p3, uint16_t p4) {
    uint32_t *e = evqReserve(3);
    e[0] = EV_HIRES | (uint32_t)row << 4 | (uint32_t)x << 13 | evBlockFlags();
    e[1] = p1 | (uint32_t)p2 << 16;
    e[2] = p3 | (uint32_t)p4 << 16;
    evqCommit(3);
}

void hostDirectLores(int row, int x, uint16_t p1, uint16_t p2, uint16_t p3,
                     uint16_t p4, uint16_t p5, uint16_t p6, int ham) {
    uint32_t *e = evqReserve(4);
    e[0] = EV_LORES | (uint32_t)row << 4 | (uint32_t)x << 13 |
           (ham ? EV_HAM : evBlockFlags());
    e[1] = p1 | (uint32_t)p2 << 16;
    e[2] = p3 | (uint32_t)p4 << 16;
    e[3] = p5 | (uint32_t)p6 << 16;
    evqCommit(4);
}

void hostDirectClip(int row, int x0, int x1) {
    if (row < 0 || row > 0x1ff)
        return;
    if (x0 < -0x8000) x0 = -0x8000;
    if (x1 > 0x7fff) x1 = 0x7fff;
    uint32_t *e = evqReserve(2);
    e[0] = EV_CLIP | (uint32_t)row << 4;
    e[1] = (uint16_t)x0 | (uint32_t)(uint16_t)x1 << 16;
    evqCommit(2);
}

// Every colour register write (Chipset.c setColour()), so core 1's copy
// stays complete, with its beam position: core 1 works out where it lands
// (c1ColourAt()).
void hostDirectColour(int reg, uint16_t old, uint16_t value) {
    (void)old;
    uint32_t *e = evqReserve(2);
    e[0] = EV_COLOUR | (uint32_t)reg << 4 | (uint32_t)internal.vPos << 9 |
           (uint32_t)internal.hPos << 18;
    e[1] = value & 0xfffu;
    evqCommit(2);
}

// The line layout colour writes are placed with (DMA.c), when it changes.
static int layout_first = -1, layout_ham;
void hostLineLayout(int first, int ham) {
    if (first == layout_first && ham == layout_ham)
        return;
    layout_first = first;
    layout_ham = ham;
    uint32_t *e = evqReserve(1);
    e[0] = EV_LAYOUT | (uint32_t)first << 4 | (ham ? 1u << 16 : 0u);
    evqCommit(1);
}

void hostDirectSprite(int row, int x, int colour_base, int attached,
                      int behind, uint16_t a, uint16_t b, uint16_t c,
                      uint16_t d) {
    const int field = x + SPRITE_X_BIAS;
    if (field < 0 || field > 0x3ff || row < 0 || row > 0x1ff)
        return;
    uint32_t *e = evqReserve(4);
    e[0] = EV_SPRITE;
    e[1] = MSG_HEADER(MSG_SPRITE, row, field,
                      (uint32_t)colour_base | (uint32_t)(attached != 0) << 5 |
                      (uint32_t)(behind != 0) << 6);
    e[2] = a | (uint32_t)b << 16;
    e[3] = c | (uint32_t)d << 16;
    evqCommit(4);
}

void hostDirectRowEnd(int row, int drawn) {
    uint32_t *e = evqReserve(1);
    e[0] = EV_ROW_END | (uint32_t)(row + 1) << 4 | (drawn ? 1u << 16 : 0u);
    evqCommit(1);
}

int hostAudioLine(const uint32_t *words, int count) {
    if (count > AUDIO_LINE_MAX_WORDS) {
        ++hostAudioDrops;
        return 0;
    }
    uint32_t *e = evqReserve(1u + (uint32_t)count);
    e[0] = EV_AUDIO | (uint32_t)count << 4;
    for (int i = 0; i < count; ++i)
        e[1 + i] = words[i];
    evqCommit(1u + (uint32_t)count);
    return 1;
}

// A frame starts (EV_BEGIN) or ends and the next starts (EV_FRAME), with
// the registers its layout comes from.
static void evFrame(uint32_t type) {
    uint32_t *e = evqReserve(3);
    e[0] = type;
    e[1] = chipset.ddfstrt | (uint32_t)chipset.ddfstop << 16;
    e[2] = chipset.bplcon0 | (uint32_t)(internal.LOF & 1u) << 16;
    evqCommit(3);
}

// A savestate was loaded (savestate.c): the machine jumped, so core 1's
// picture of it is stale. The colour registers go again, then every row is
// resent (as for a new layout), with the palette and the DPF state.
FRANK_COLD void host_state_restored(void) {
    for (int i = 0; i < 32; ++i) {
        uint32_t *e = evqReserve(2);
        e[0] = EV_COLOUR | (uint32_t)i << 4;
        e[1] = (&chipset.color00)[i] & 0xfffu;
        evqCommit(2);
    }
    uint32_t *e = evqReserve(1);
    e[0] = EV_RESTORED;
    evqCommit(1);
}

// Core 1: turns queued records into messages while the ring has room for
// the most any record may add. Returns whether any was taken.
#define EVQ_RING_MARGIN 512u
static bool host_events_drain_some(uint32_t budget);
bool host_events_drain(void) { return host_events_drain_some(~0u); }

// At most `budget` records (the CPU's core: between its steps).
bool host_events_drain_batch(uint32_t budget) { return host_events_drain_some(budget); }

// frank-amiga: with FRANK_PARALLEL both cores build messages when they
// would otherwise wait (the CPU's core between its steps, the chipset's
// while the CPU is behind), one at a time.
static volatile uint8_t drain_lock;

static bool host_events_drain_locked(uint32_t budget);
static bool host_events_drain_some(uint32_t budget) {
#ifdef FRANK_PARALLEL
    if (__atomic_exchange_n(&drain_lock, 1, __ATOMIC_ACQUIRE))
        return false;
    const bool r = host_events_drain_locked(budget);
    __atomic_store_n(&drain_lock, 0, __ATOMIC_RELEASE);
    return r;
#else
    return host_events_drain_locked(budget);
#endif
}

static bool host_events_drain_locked(uint32_t budget) {
    uint32_t tail = evq_tail;
    const uint32_t head = __atomic_load_n(&evq_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        // Nothing queued: wait a little before looking again, so this
        // core's polling does not compete with core 0 for SRAM (not while
        // this core runs the CPU too).
#ifdef FRANK_PARALLEL
        if (!par_running)
#endif
            busy_wait_at_least_cycles(400);
        return false;
    }
    while (tail != head && budget-- &&
           RING_WORDS - ringFill(ring_wr, __atomic_load_n(&ring_tail, __ATOMIC_ACQUIRE)) >= EVQ_RING_MARGIN) {
        const uint32_t *e = &evq[tail % EVQ_WORDS];
        const uint32_t h = e[0];
        uint32_t n;
        switch (h & 15u) {
        case EV_PAD:
            n = EVQ_WORDS - tail % EVQ_WORDS;
            break;
        case EV_LORES:
            c1LoresBlock((int)(h >> 4 & 0x1ffu), (int)(h >> 13 & 0x3ffu),
                         (uint16_t)e[1], (uint16_t)(e[1] >> 16),
                         (uint16_t)e[2], (uint16_t)(e[2] >> 16),
                         (uint16_t)e[3], (uint16_t)(e[3] >> 16),
                         (h & EV_HAM) != 0, h);
            n = 4;
            break;
        case EV_HIRES:
            c1HiresBlock((int)(h >> 4 & 0x1ffu), (int)(h >> 13 & 0x3ffu),
                         (uint16_t)e[1], (uint16_t)(e[1] >> 16),
                         (uint16_t)e[2], (uint16_t)(e[2] >> 16), h);
            n = 3;
            break;
        case EV_COLOUR: {
            // A change left of the image (most Copper changes, made in the
            // horizontal blank) or right of it takes effect with the
            // palette sync before the next block or row end; only one
            // inside the image's columns needs its position: inside drawn
            // blocks, or in the border (ATK's colour bars run through the
            // border lines too). As dmaBeamColumn(): a colour write shows
            // one colour clock (4 columns) before the bitplane pixel
            // reference.
            const int row = (int)(h >> 9 & 0x1ffu) - OMEGA_DIRECT_FIRST_LINE;
            int x = 2 * (2 * (int)(h >> 18 & 0xffu) - c1_layout_first) - 4;
            const int in_image = row >= 0 && row < OMEGA_DIRECT_ROWS &&
                                 x > 0 && x < HOST_RASTER_W;
            if (x > 0x3ff)
                x = 0x3ff;
            c1ColourWrite((int)(h >> 4 & 31u), (uint16_t)e[1], in_image,
                          row, x, c1_layout_ham);
            n = 2;
            break;
        }
        case EV_CLIP:
            c1Clip((int)(h >> 4 & 0x1ffu), e[1]);
            n = 2;
            break;
        case EV_LAYOUT:
            c1_layout_first = (int)(h >> 4 & 0xfffu);
            c1_layout_ham = (h >> 16) & 1u;
            n = 1;
            break;
        case EV_SPRITE:
            if (direct_frame)
                c1Sprite(&e[1]);
            n = 4;
            break;
        case EV_ROW_END:
            c1RowEnd((int)(h >> 4 & 0xfffu) - 1, (h >> 16) & 1u);
            n = 1;
            break;
        case EV_AUDIO:
            n = 1u + (h >> 4);
            c1AudioLine(&e[1], (int)(h >> 4));
            break;
        case EV_FRAME:
        case EV_BEGIN: {
            const frame_regs_t fr = {
                (uint16_t)e[1], (uint16_t)(e[1] >> 16),
                (uint16_t)e[2], (uint16_t)(e[2] >> 16),
            };
            if ((h & 15u) == EV_FRAME)
                hostDirectFinish();
            frame_mark_push(ring_wr);
            hostDirectBegin(&fr);
            n = 3;
            break;
        }
        case EV_RESTORED:
            c1Restored();
            n = 1;
            break;
        default:            // cannot happen: drop the queue
            n = head - tail;
            break;
        }
        tail += n;
        __atomic_store_n(&evq_tail, tail, __ATOMIC_RELEASE);
    }
    return true;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────
volatile uint32_t host_frames_emulated;

FRANK_COLD void hostInit(void) {
    host.pixels = raster_scratch;
    host.rasterRow = 0;
    host.rasterX = 0;
    host.displayIsLores = 0;
    // Every frame is direct from the very first one: there is no ARGB path.
    hostDirectActive = 1;
    evFrame(EV_BEGIN);
}

// Once per VBL (DMA.c).
void hostDisplay(void) {
    evFrame(EV_FRAME);
    host_frames_emulated++;
    host_frame_paced();
}

// The mixer (omega/AudioMix.c) runs on the master; Audio.c still resets it.
void audioMixInit(void) {}
