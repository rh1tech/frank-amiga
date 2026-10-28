// Core 0 -> core 1 message format and the inline fast path of the bitplane
// block producers (RP2350 HDMI builds). Host.c owns the ring; DMA.c uses
// hostDirectHiresFast()/hostDirectLoresFast() so a block that continues the
// current run is appended without a call into Host.c.
#ifndef HOST_RING_H
#define HOST_RING_H

#include <stdint.h>
#include "../omega/Chipset.h"

enum {
    MSG_BEGIN = 1,   // + 3 words: frame layout, frame number; frank-amiga:
                     //   extra = slave time per frame, 0.1 ms (meter)
    MSG_HIRES,       // + 2 words per block: planes 1..4
    MSG_LORES,       // + 3 words per block: planes 1..6; extra bit 0 = HAM
    MSG_PALETTE,     // + 1 + extra words: mask of the colours that changed,
                     //   then those colours (0x0RGB) in order, two per word
    MSG_END,         // + 1 word: border colour (0x0RGB)
    MSG_SPRITE,      // + 1-2 words: sprite planes; extra = base | att | behind
    MSG_ROW_END,     // no words: image row `row` ends (its border colour)
    MSG_COLOUR,      // + extra words, one per change in order:
                     //   MSG_COLOUR_WORD(x = raster column, register, 0x0RGB)
    MSG_ROW_SAME,    // no words: image row `row` is unchanged (copy it)
    // frank-amiga: Paula's line record (omega/Audio.h); extra = word count.
    MSG_AUDIO,
    // frank-amiga: dual playfield state of image row `row`, sent before its
    // first DPF block and when it changes: extra bits 0-4 = PF2H - PF1H
    // (signed, lores pixels), bit 5 = PF2PRI.
    MSG_DPF,
};
// frank-amiga: HIRES/LORES run header extra bit 5 (header bit 28): the
// run's blocks are dual playfield (planes 1/3/5 and 2/4/6 apart).
#define MSG_RUN_DPF (1u << 28)
#define MSG_HEADER(type, row, x, extra) \
    ((uint32_t)(type) | (uint32_t)(row) << 4 | (uint32_t)(x) << 13 | \
     (uint32_t)(extra) << 23)
// HIRES and LORES messages carry a run of 1..MSG_RUN_MAX consecutive blocks
// of one row (block i at x + i * block width): count - 1 sits in header
// bits 24..27, above the HAM bit. One message per run instead of per block
// saves a ring push on core 0 and a dispatch on core 1 for each block.
#define MSG_RUN_MAX 16
#define MSG_RUN_SHIFT 24
#define MSG_MAX_WORDS (1 + 3 * MSG_RUN_MAX) // a run of lores blocks
#define MSG_RUN_COUNT_MASK ((uint32_t)(MSG_RUN_MAX - 1) << MSG_RUN_SHIFT)
// Header bits that a block must match to join a run: type, row, HAM.
#define MSG_RUN_KEY_MASK (~(MSG_RUN_COUNT_MASK | MSG_HEADER(0, 0, 0x3ff, 0)))
// frank-amiga: one Copper colour change of a MSG_COLOUR message. State of
// the Art's chunky part changes ~36 colours a line: a message each (two
// words) was a third of the stream, and full palettes another quarter.
#define MSG_COLOUR_WORD(x, reg, rgb) \
    ((uint32_t)(rgb) | (uint32_t)(reg) << 12 | (uint32_t)(x) << 17)
#define MSG_COLOUR_MAX (MSG_MAX_WORDS - 1)   // fits the longest message

// The run being collected (see Host.c): msg[0] is its header, words its
// length so far (0: none), next_x the x its next block must have;
// palette_sent the palette generation core 1 has.
typedef struct {
    uint32_t *msg;
    uint32_t words;
    int next_x;
    uint32_t palette_sent;
} HostRun;
extern HostRun host_run;

// Hash of the open line's blocks and sprites (Host.c hostDirectRowEnd():
// an unchanged image row is not sent again). Mixed in where each block is
// made, from values already in registers: re-reading the line's messages
// from the ring cost xSysinfo ~4 % of core 0.
// A run's header (type, row, start x, HAM) is mixed in when the run starts
// (Host.c runStart()); each block adds its plane words.
extern uint32_t host_row_hash;
static inline uint32_t hostMix(uint32_t h, uint32_t w) {
    return (h ^ w) * 0x01000193u;
}
static inline void hostRowMix2(uint32_t a, uint32_t b) {
    host_row_hash = hostMix(hostMix(host_row_hash, a), b);
}
static inline void hostRowMix3(uint32_t a, uint32_t b, uint32_t c) {
    host_row_hash = hostMix(hostMix(hostMix(host_row_hash, a), b), c);
}

void hostDirectHires(int row, int x, uint16_t p1, uint16_t p2,
                     uint16_t p3, uint16_t p4);
void hostDirectLores(int row, int x, uint16_t p1, uint16_t p2, uint16_t p3,
                     uint16_t p4, uint16_t p5, uint16_t p6, int ham);

// The DMA's block producers. frank-amiga: on the board a block is a record
// for core 1 (host_link.c), which builds the runs; nothing to do inline.
static inline void hostDirectHiresFast(int row, int x, uint16_t p1,
                                       uint16_t p2, uint16_t p3, uint16_t p4) {
    hostDirectHires(row, x, p1, p2, p3, p4);
}

static inline void hostDirectLoresFast(int row, int x, uint16_t p1,
                                       uint16_t p2, uint16_t p3, uint16_t p4,
                                       uint16_t p5, uint16_t p6, int ham) {
    hostDirectLores(row, x, p1, p2, p3, p4, p5, p6, ham);
}

#endif
