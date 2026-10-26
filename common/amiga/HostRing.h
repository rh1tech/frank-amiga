// Core 0 -> core 1 message format and the inline fast path of the bitplane
// block producers (RP2350 HDMI builds). Host.c owns the ring; DMA.c uses
// hostDirectHiresFast()/hostDirectLoresFast() so a block that continues the
// current run is appended without a call into Host.c.
#ifndef HOST_RING_H
#define HOST_RING_H

#include <stdint.h>
#include "../omega/Chipset.h"

enum {
    MSG_BEGIN = 1,   // + 3 words: frame layout, frame number
    MSG_HIRES,       // + 2 words per block: planes 1..4
    MSG_LORES,       // + 3 words per block: planes 1..6; extra bit 0 = HAM
    MSG_PALETTE,     // + 16 words: 32 colours, 0x0RGB, two per word
    MSG_END,         // + 1 word: border colour (0x0RGB)
    MSG_SPRITE,      // + 1-2 words: sprite planes; extra = base | att | behind
    MSG_ROW_END,     // no words: image row `row` ends (its border colour)
    MSG_COLOUR,      // + 1 word: 0x0RGB; extra = register, x = raster column
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

// Appends a block to the current run when it continues it with the palette
// unchanged (what hostDirectHires/Lores() would do then); returns where to
// write its plane words, or NULL to take the full path.
static inline uint32_t *hostRunContinue(uint32_t key, int x, int width,
                                        uint32_t words) {
    HostRun *r = &host_run;
    if (!r->words || x != r->next_x ||
        internal.paletteGeneration != r->palette_sent)
        return 0;
    const uint32_t header = r->msg[0];
    if ((header & MSG_RUN_KEY_MASK) != key ||
        (header & MSG_RUN_COUNT_MASK) == MSG_RUN_COUNT_MASK)
        return 0;
    r->msg[0] = header + (1u << MSG_RUN_SHIFT);
    uint32_t *planes = &r->msg[r->words];
    r->words += words;
    r->next_x = x + width;
    return planes;
}

static inline void hostDirectHiresFast(int row, int x, uint16_t p1,
                                       uint16_t p2, uint16_t p3, uint16_t p4) {
    uint32_t *planes =
        hostRunContinue(MSG_HEADER(MSG_HIRES, row, 0, 0), x, 16, 2);
    if (!planes) {
        hostDirectHires(row, x, p1, p2, p3, p4);
        return;
    }
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    hostRowMix2(planes[0], planes[1]);
}

static inline void hostDirectLoresFast(int row, int x, uint16_t p1,
                                       uint16_t p2, uint16_t p3, uint16_t p4,
                                       uint16_t p5, uint16_t p6, int ham) {
    uint32_t *planes = hostRunContinue(
        MSG_HEADER(MSG_LORES, row, 0, ham ? 1 : 0), x, 32, 3);
    if (!planes) {
        hostDirectLores(row, x, p1, p2, p3, p4, p5, p6, ham);
        return;
    }
    planes[0] = p1 | (uint32_t)p2 << 16;
    planes[1] = p3 | (uint32_t)p4 << 16;
    planes[2] = p5 | (uint32_t)p6 << 16;
    hostRowMix3(planes[0], planes[1], planes[2]);
}

#endif
