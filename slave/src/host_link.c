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

#include "Host.h"
#include "HostRing.h"
#include "host_link.h"
#include "../omega/DisplayLayout.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/Audio.h"
#include "../omega/CPU.h"
#include "../omega/DMA.h"
#include "../omega/VideoStandard.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

Host_t host;

// The ARGB raster fallback is never used here (every frame is direct), but
// DMA.c still references it; a scratch line keeps a stray call harmless.
static uint32_t raster_scratch[HOST_RASTER_W];
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
#define RING_WORDS 16384u // 64 KB
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
    do {
        tail_seen = __atomic_load_n(&ring_tail, __ATOMIC_ACQUIRE);
    } while (ringFill(head, tail_seen) + count > RING_WORDS);
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
// Sends the whole palette; register `except` (if >= 0) as `colour`.
static void __attribute__((noinline)) hostDirectSendPaletteWith(
    int except, uint16_t colour) {
    host_run.palette_sent = internal.paletteGeneration;
    row_colours_changed = true;
    runFlush(); // the blocks before the change use the old palette
    uint32_t msg[17];
    msg[0] = MSG_HEADER(MSG_PALETTE, 0, 0, 0);
    for (int i = 0; i < 16; ++i)
        msg[1 + i] = argbToOcs(internal.palette[2 * i]) |
                     argbToOcs(internal.palette[2 * i + 1]) << 16;
    if (except >= 0) {
        uint32_t *pair = &msg[1 + except / 2];
        const int shift = (except & 1) ? 16 : 0;
        *pair = (*pair & ~(0xffffu << shift)) | (uint32_t)colour << shift;
    }
    ringPush(msg, count_of(msg));
}

static void hostDirectSendPalette(void) {
    hostDirectSendPaletteWith(-1, 0);
}

static inline void hostDirectSyncPalette(void) {
    if (internal.paletteGeneration != host_run.palette_sent)
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

static void hostDirectBegin(void) {
    runFlush();
    ringPublish();
    const bool full_width = omegaDdfIsFullWidth(chipset.ddfstrt);
    // Narrow layouts that need hostPresentFrame()'s wrapped-fetch
    // reconstruction (pixels moved between lines) keep the ARGB path.
    const bool narrow = !full_width;
    direct_frame = full_width || narrow;
    hostDirectActive = direct_frame;
    if (!direct_frame)
        return;
    row_end_generation = host_run.palette_sent;   // core 1's palette
    // Rows are beam lines (DMA.c: line - OMEGA_DIRECT_FIRST_LINE), so every
    // layout maps one raster row to one image row.
    const int step = 1, first_row = 0;
    const int rotation =
        narrow ? 0 : omegaDdfRowRotation(chipset.ddfstrt, chipset.ddfstop);
    int height = dvi_display_direct_height();
    if (height > DIRECT_MAX_ROWS)
        height = DIRECT_MAX_ROWS;
    // Interlace (BPLCON0 LACE): which field this frame is (LOF: long).
    const uint32_t lace = (chipset.bplcon0 & 0x4) != 0;
    const uint32_t msg[4] = {
        MSG_HEADER(MSG_BEGIN, 0, 0, 0),
        (uint16_t)first_row | (uint32_t)step << 16 | (uint32_t)narrow << 24 |
            lace << 25 | (uint32_t)(internal.LOF & 1u) << 26,
        (uint32_t)rotation | (uint32_t)height << 16,
        ++direct_frame_no,
    };
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
void hostDirectColour(int reg, uint16_t old, uint16_t value) {
    // A change left of the image (most Copper changes, made in the
    // horizontal blank) or right of it is exact with the palette sync
    // before the next block or row end, as before; only one inside the
    // image's columns needs its position: inside drawn blocks, or in the
    // border (ATK's colour bars run through the border lines too). A
    // message per change cost RemGame ~3 %.
    const int row = internal.vPos - OMEGA_DIRECT_FIRST_LINE;
    int x = dmaBeamColumn();
    if (row < 0 || row >= OMEGA_DIRECT_ROWS || x <= 0 || x >= HOST_RASTER_W)
        return;
    // Earlier changes still pending go first, with this register as it
    // was before this change.
    if (host_run.palette_sent != internal.paletteGeneration - 2u)
        hostDirectSendPaletteWith(reg, old);
    host_run.palette_sent = internal.paletteGeneration;
    row_colours_changed = true;
    runFlush();
    x = x < 0 ? 0 : x > 0x3ff ? 0x3ff : x;
    const uint32_t msg[2] = { MSG_HEADER(MSG_COLOUR, row, x, reg), value };
    ringPush(msg, count_of(msg));
}

// Only rows without blocks need their own colour position (rows with
// blocks take their border from their first run), and only when the
// palette changed since the last message: RemGame sends none, ATK four.
uint32_t host_row_hash = 0x811c9dc5u;

static void hostDirectRowEndVideo(int row, int drawn) {
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
    if (row < 0 || drawn || internal.paletteGeneration == row_end_generation)
        return;
    row_end_generation = internal.paletteGeneration;
    hostDirectSyncPalette();
    runFlush();
    runStart(MSG_HEADER(MSG_ROW_END, row, 0, 0));   // a one-word message
    runFlush();
}

void hostDirectHires(int row, int x, uint16_t p1, uint16_t p2,
                     uint16_t p3, uint16_t p4) {
    hostDirectSyncPalette();
    uint32_t *planes = runAppend(MSG_HEADER(MSG_HIRES, row, 0, 0), x, 16, 2);
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    hostRowMix2(planes[0], planes[1]);
}

void hostDirectLores(int row, int x, uint16_t p1, uint16_t p2, uint16_t p3,
                     uint16_t p4, uint16_t p5, uint16_t p6, int ham) {
    hostDirectSyncPalette();
    uint32_t *planes =
        runAppend(MSG_HEADER(MSG_LORES, row, 0, ham ? 1 : 0), x, 32, 3);
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    planes[2] = p5 | (uint32_t)p6 << 16;
    hostRowMix3(planes[0], planes[1], planes[2]);
}

void hostDirectSprite(int row, int x, int colour_base, int attached,
                      int behind, uint16_t a, uint16_t b, uint16_t c,
                      uint16_t d) {
    const int field = x + SPRITE_X_BIAS;
    if (field < 0 || field > 0x3ff || row < 0 || row > 0x1ff)
        return;
    hostDirectSyncPalette();
    runFlush(); // sprites go over the blocks already sent
    const uint32_t msg[3] = {
        MSG_HEADER(MSG_SPRITE, row, field,
                   (uint32_t)colour_base | (uint32_t)(attached != 0) << 5 |
                   (uint32_t)(behind != 0) << 6),
        a | (uint32_t)b << 16,
        c | (uint32_t)d << 16,
    };
    hostRowMix3(msg[0], msg[1], attached ? msg[2] : 0u);
    ringPush(msg, attached ? 3u : 2u);
}

static void hostDirectFinish(void) {
    hostDirectSyncPalette();
    runFlush();
    const uint32_t msg[2] = {
        MSG_HEADER(MSG_END, 0, 0, 0),
        argbToOcs(internal.palette[0]),
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

int hostAudioLine(const uint32_t *words, int count) {
    if (audio_stage_words || count > AUDIO_LINE_MAX_WORDS) {
        ++hostAudioDrops;
        return 0;
    }
    audio_stage[0] = MSG_HEADER(MSG_AUDIO, 0, 0, (uint32_t)count);
    for (int i = 0; i < count; ++i)
        audio_stage[1 + i] = words[i];
    audio_stage_words = 1u + (uint32_t)count;
    return 1;
}

void hostAudioOut(const int16_t *samples, int frames) {
    (void)samples; (void)frames;   // mixing happens on the master
}

void hostDirectRowEnd(int row, int drawn) {
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

// ── Lifecycle ─────────────────────────────────────────────────────────────
volatile uint32_t host_frames_emulated;

void hostInit(void) {
    host.pixels = raster_scratch;
    host.rasterRow = 0;
    host.rasterX = 0;
    host.displayIsLores = 0;
    direct_frame = false;
    hostDirectActive = 0;
    // Every frame is direct from the very first one: there is no ARGB path.
    frame_mark_push(ring_wr);
    hostDirectBegin();
}

// Once per VBL (DMA.c).
void hostDisplay(void) {
    hostDirectFinish();
    host_frames_emulated++;
    host_frame_paced();
    frame_mark_push(ring_wr);
    hostDirectBegin();
}

// The mixer (omega/AudioMix.c) runs on the master; Audio.c still resets it.
void audioMixInit(void) {}
