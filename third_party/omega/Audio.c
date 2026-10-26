//
//  Audio.c
//  Paula audio: four DMA channels (the emulation side; AudioMix.c mixes).
//
//  Each channel plays signed 8-bit samples, high byte of each word first,
//  one sample every AUDxPER colour clocks (DMA slots in this emulator), at
//  AUDxVOL (0-64). With DMA on, starting the channel copies AUDxLC/AUDxLEN
//  into its counters and raises the channel's interrupt, so software can
//  queue the next block; words are fetched as needed, and when the length
//  runs out the registers are reloaded and the interrupt raised again. With
//  DMA off, a CPU write to AUDxDAT plays that word, and the interrupt asks
//  for the next one. Attach modes (ADKCON) are not emulated.
//
//  The channels run in emulated time and are advanced once per raster line.
//  Each line's sample changes (with their colour clock), volumes and LED
//  filter state go to the host as a line record (Audio.h); AudioMix.c makes
//  the 48 kHz output from it, so the pitch is exact relative to the
//  emulated machine whatever the emulation speed. On the RP2350 the mixing
//  runs on core 1, off the emulator's core.
//

#include "Audio.h"
#include "Chipset.h"
#include "VideoStandard.h"
#include "CIA.h"

// Line records only when something plays the output (RP2350: HDMI audio
// builds; the native runner always). Without it Paula still runs (DMA,
// interrupts), unmixed.
#ifndef OMEGA_AUDIO_OUTPUT
#define OMEGA_AUDIO_OUTPUT 1
#endif

#define MIN_DMA_PERIOD 124   // shortest period audio DMA can feed

typedef struct {
    int mode;           // 0 off, 1 DMA, 2 CPU-written AUDxDAT
    uint32_t ptr;       // next word to fetch (word index into chip RAM)
    uint32_t len;       // words left in the current block
    uint16_t word;      // word being played
    int lowNext;        // its low byte is the next sample
    int32_t counter;    // colour clocks left of the current sample
    int8_t sample;      // current output sample
    int datPending;     // CPU wrote AUDxDAT since the last word was taken
} AudioChannel;

static AudioChannel ch[4];
static int active;      // channels with mode != 0 (bit per channel)
// The sample each channel last had in a line record (SENT_UNKNOWN:
// unknown, restated in the next record).
static int sent[4];
#define SENT_UNKNOWN 0x100  // outside the sample range
// The line record being built (Audio.h).
static uint32_t record[AUDIO_LINE_MAX_WORDS];
static int recordWords;

static inline uint32_t regLc(int c) {
    return c == 0 ? chipset.aud0lc : c == 1 ? chipset.aud1lc :
           c == 2 ? chipset.aud2lc : chipset.aud3lc;
}
static inline uint16_t regLen(int c) {
    return c == 0 ? chipset.aud0len : c == 1 ? chipset.aud1len :
           c == 2 ? chipset.aud2len : chipset.aud3len;
}
static inline uint16_t regPer(int c) {
    return c == 0 ? chipset.aud0per : c == 1 ? chipset.aud1per :
           c == 2 ? chipset.aud2per : chipset.aud3per;
}
static inline uint16_t regVol(int c) {
    return c == 0 ? chipset.aud0vol : c == 1 ? chipset.aud1vol :
           c == 2 ? chipset.aud2vol : chipset.aud3vol;
}
static inline uint16_t regDat(int c) {
    return c == 0 ? chipset.aud0dat : c == 1 ? chipset.aud1dat :
           c == 2 ? chipset.aud2dat : chipset.aud3dat;
}

static void raiseInterrupt(int c) {
    putChipReg16[INTREQ]((uint16_t)(0x8000u | (0x80u << c)));
}

static void reload(int c) {
    ch[c].ptr = regLc(c);
    ch[c].len = regLen(c) ? regLen(c) : 0x10000u;
    raiseInterrupt(c);
}

static int dmaOn(int c) {
    return (chipset.dmaconr & (0x200u | (1u << c))) == (0x200u | (1u << c));
}

static void setMode(int c, int mode) {
    ch[c].mode = mode;
    if (mode)
        active |= 1 << c;
    else
        active &= ~(1 << c);
}

void audioReset(void) {
    static int mixReady;
    if (!mixReady) {
        mixReady = 1;
        audioMixInit();
    }
    for (int c = 0; c < 4; ++c) {
        ch[c] = (AudioChannel){0};
        sent[c] = SENT_UNKNOWN;
    }
    active = 0;
}

void audioDmaconChanged(uint16_t old) {
    for (int c = 0; c < 4; ++c) {
        const int was = (old & (0x200u | (1u << c))) == (0x200u | (1u << c));
        const int now = dmaOn(c);
        if (now && !was) {
            setMode(c, 1);
            ch[c].lowNext = 0;
            ch[c].counter = 0;
            reload(c);
        } else if (!now && was && ch[c].mode == 1) {
            setMode(c, 0);
            ch[c].sample = 0;
        }
    }
}

void audioDatWritten(int c) {
    if (dmaOn(c))
        return;  // DMA owns the channel
    ch[c].datPending = 1;
    if (ch[c].mode == 0) {
        setMode(c, 2);
        ch[c].lowNext = 0;
        ch[c].counter = 0;
    }
}

// Moves channel c to its next sample.
static void nextSample(int c) {
    AudioChannel *a = &ch[c];
    if (a->lowNext) {
        a->sample = (int8_t)(a->word & 0xFF);
        a->lowNext = 0;
        return;
    }
    if (a->mode == 1) {
        // chip RAM words are stored host-endian: swap to the Amiga's order
        const uint16_t w = internal.chipramW[a->ptr & 0xFFFFFu];
        a->word = (uint16_t)(w << 8 | w >> 8);
        a->ptr += 1;
        if (--a->len == 0)
            reload(c);
    } else {
        // CPU playback: a written word plays once; the interrupt asks for
        // the next, and without one the channel goes idle.
        if (!a->datPending) {
            setMode(c, 0);
            a->sample = 0;
            return;
        }
        a->datPending = 0;
        a->word = regDat(c);
        raiseInterrupt(c);
    }
    a->sample = (int8_t)(a->word >> 8);
    a->lowNext = 1;
}

// Records channel c's sample when it differs from the last one recorded.
static void recordSample(int c, int clock) {
    const int sample = ch[c].sample;
    if (sample == sent[c])
        return;
    if (recordWords == AUDIO_LINE_MAX_WORDS)
        return;  // restated next line (sent[c] still differs)
    record[recordWords++] = AUDIO_EVENT(c, clock, sample);
    sent[c] = sample;
}

// Advances channel c over a line of `slots` colour clocks.
static void advance(int c, int32_t slots) {
    AudioChannel *a = &ch[c];
    int32_t clocks = slots;
    while (clocks > 0 && a->mode != 0) {
        const int32_t seg = a->counter < clocks ? a->counter : clocks;
        a->counter -= seg;
        clocks -= seg;
        if (a->counter <= 0) {
            nextSample(c);
#if OMEGA_AUDIO_OUTPUT
            recordSample(c, slots - clocks);
#endif
            uint32_t per = regPer(c);
            if (per == 0)
                per = 0x10000;  // the period counter wraps: 0 is the longest
            if (a->mode == 1 && per < MIN_DMA_PERIOD)
                per = MIN_DMA_PERIOD;
            a->counter += (int32_t)per;
        }
    }
}

// Called at the end of each line of `slots` colour clocks (227 or 228).
void audioLine(int slots) {
#if !OMEGA_AUDIO_OUTPUT
    // No output: advance the channels (DMA fetches, interrupts) only.
    if (active)
        for (int c = 0; c < 4; ++c)
            advance(c, slots);
    return;
#else
    uint32_t flags = AUDIO_LINE_HEADER;
    if (slots > 227)
        flags |= AUDIO_LINE_LONG;
    if (!(CIAA.pra & 0x02))
        flags |= AUDIO_LINE_LED;
    if (active)
        flags |= AUDIO_LINE_ACTIVE;
    uint32_t volumes = 0;
    for (int c = 0; c < 4; ++c) {
        uint32_t vol = regVol(c) & 0x7F;
        volumes |= (vol > 64 ? 64 : vol) << (8 * c);
    }
    record[1] = volumes;
    recordWords = 2;
    // Changes since the last line (DMA switched off, a record dropped)
    // take effect from its start.
    for (int c = 0; c < 4; ++c)
        recordSample(c, 0);
    if (active)
        for (int c = 0; c < 4; ++c)
            advance(c, slots);
    if (active)
        flags |= AUDIO_LINE_ACTIVE_END;
    record[0] = flags;
    if (!hostAudioLine(record, recordWords))
        for (int c = 0; c < 4; ++c)
            sent[c] = SENT_UNKNOWN;
#endif
}
