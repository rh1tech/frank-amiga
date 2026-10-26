//
//  AudioMix.c
//  Paula's output: line records (Audio.h) mixed to 48 kHz stereo.
//
//  Each channel holds its sample between the changes a record lists, so
//  the channels are rebuilt here exactly as Audio.c played them. Output
//  samples fall at 48 kHz of emulated time; each is the average of every
//  channel over its interval (a box filter, not a point sample, which keeps
//  aliasing down), times the channel's volume, then filtered as the Amiga's
//  analogue output stage.
//
//  On the RP2350 this runs on core 1 (Host.c), from SRAM: the file is kept
//  out of flash (src/ld/default_text_excludes.incl).
//

#include "Audio.h"
#include "VideoStandard.h"
#include <math.h>

// Loops here must not become memset/memcpy calls (flash code on the RP2350).
#pragma GCC optimize("no-tree-loop-distribute-patterns")

static int8_t sample[4];   // each channel's sample at the current position
// Sum of sample x colour clocks (16.12) of each channel over the current
// output interval.
static int32_t accum[4];
// Colour clocks per output sample and until the next one (16.16).
static const uint32_t outStep = (uint32_t)(
    ((uint64_t)OMEGA_VIDEO_LINE_CCK2 * OMEGA_VIDEO_FRAME_LINES *
     OMEGA_VIDEO_RATE_NUMERATOR << 16) /
    ((uint64_t)2 * OMEGA_VIDEO_RATE_DENOMINATOR * AUDIO_RATE));
static int32_t outNext;
// accum (sample x clocks in 16.12) x volume -> fraction of full scale for
// one output interval; two channels per side, so each counts half.
static float outScale;

// The Amiga's analogue output stage, per side: a fixed first-order low-pass
// (~4.9 kHz on the A500) and the "LED" filter, a second-order Butterworth
// low-pass at ~3.3 kHz switched on while CIA-A's /LED output (PRA bit 1)
// is low.
typedef struct { float y; } OnePole;
typedef struct { float x1, x2, y1, y2; } Biquad;
static float fixedA;                              // one-pole coefficient
static float ledB0, ledB1, ledB2, ledA1, ledA2;   // biquad coefficients
static OnePole fixedL, fixedR;
static Biquad ledL, ledR;

// Called once, before the first record (libm: flash code on the RP2350).
void audioMixInit(void) {
    outScale = 1.0f / ((float)(outStep >> 4) * 128.0f * 64.0f) * 0.5f;
    const float pi = 3.14159265f;
    fixedA = 1.0f - expf(-2.0f * pi * 4900.0f / AUDIO_RATE);
    // RBJ cookbook low-pass, Q = 1/sqrt(2).
    const float w = 2.0f * pi * 3275.0f / AUDIO_RATE;
    const float alpha = sinf(w) / (2.0f * 0.70710678f);
    const float c = cosf(w), a0 = 1.0f + alpha;
    ledB0 = (1.0f - c) / 2.0f / a0;
    ledB1 = (1.0f - c) / a0;
    ledB2 = ledB0;
    ledA1 = -2.0f * c / a0;
    ledA2 = (1.0f - alpha) / a0;
}

static __attribute__((noinline)) float filter(float x, OnePole *f,
                                              Biquad *b, int led) {
    f->y += (x - f->y) * fixedA;
    x = f->y;
    if (!led)
        return x;
    const float y = ledB0 * x + ledB1 * b->x1 + ledB2 * b->x2 -
                    ledA1 * b->y1 - ledA2 * b->y2;
    b->x2 = b->x1; b->x1 = x;
    b->y2 = b->y1; b->y1 = y;
    return y;
}

static int16_t toS16(float v) {
    const float s = v * 32767.0f;
    return (int16_t)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : (int)s);
}

// One channel's sample changes in the line: colour clock, sample.
typedef struct {
    uint8_t n, next;
    uint8_t at[AUDIO_LINE_MAX_EVENTS];
    int8_t to[AUDIO_LINE_MAX_EVENTS];
} Changes;

// Accumulates channel c from `from` to `to` (16.16 colour clocks), taking
// the changes on the way; the pieces split where Audio.c's counters did.
static __attribute__((noinline)) void integrate(int c, Changes *ch,
                                                int32_t from, int32_t to) {
    while (ch->next < ch->n && (int32_t)ch->at[ch->next] << 16 <= to) {
        const int32_t at = (int32_t)ch->at[ch->next] << 16;
        if (at > from) {
            accum[c] += sample[c] * ((at - from) >> 4);  // 16.12: fits
            from = at;
        }
        sample[c] = ch->to[ch->next++];
    }
    accum[c] += sample[c] * ((to - from) >> 4);
}

// Channel c's average over the output interval, times its volume, as a
// fraction of full scale (+-1 for sample +-128 at volume 64).
static float channelOut(int c, uint32_t volumes) {
    const int32_t sum = accum[c];
    accum[c] = 0;
    return (float)sum * (float)((volumes >> (8 * c)) & 0x7f) * outScale;
}

void audioMixLine(const uint32_t *words, int count) {
    const uint32_t flags = words[0], volumes = words[1];
    const int32_t lineClocks =
        (flags & AUDIO_LINE_LONG ? 228 : 227) << 16;
    Changes ch[4];
    for (int c = 0; c < 4; ++c)
        ch[c].n = ch[c].next = 0;
    for (int i = 2; i < count; ++i) {
        const uint32_t e = words[i];
        Changes *x = &ch[AUDIO_EVENT_CHANNEL(e)];
        x->at[x->n] = (uint8_t)AUDIO_EVENT_CLOCK(e);
        x->to[x->n++] = AUDIO_EVENT_SAMPLE(e);
    }
    if (!(flags & AUDIO_LINE_ACTIVE) && fixedL.y == 0.0f && fixedR.y == 0.0f) {
        // All channels off and the filters settled (most of the time): only
        // take the changes and count the samples.
        for (int c = 0; c < 4; ++c)
            if (ch[c].n)
                sample[c] = ch[c].to[ch[c].n - 1];
        int frames = 0;
        while (outNext < lineClocks) {
            ++frames;
            outNext += (int32_t)outStep;
        }
        outNext -= lineClocks;
        hostAudioOut(0, frames);
        return;
    }
    int16_t out[2 * 4];  // a line holds at most 4 output samples
    const int led = (flags & AUDIO_LINE_LED) != 0;
    const int activeEnd = (flags & AUDIO_LINE_ACTIVE_END) != 0;
    int frames = 0;
    int32_t pos = 0;
    while (outNext < lineClocks) {
        for (int c = 0; c < 4; ++c)
            integrate(c, &ch[c], pos, outNext);
        pos = outNext;
        const float l = channelOut(0, volumes) + channelOut(3, volumes);
        const float r = channelOut(1, volumes) + channelOut(2, volumes);
        float fl = filter(l, &fixedL, &ledL, led);
        float fr = filter(r, &fixedR, &ledR, led);
        // Let a decayed filter reach exact silence (enables the fast path).
        if (!activeEnd && fabsf(fixedL.y) < 1e-5f && fabsf(fixedR.y) < 1e-5f) {
            fixedL.y = fixedR.y = 0.0f;
            fl = fr = 0.0f;
        }
        out[2 * frames] = toS16(fl);
        out[2 * frames + 1] = toS16(fr);
        ++frames;
        outNext += (int32_t)outStep;
    }
    for (int c = 0; c < 4; ++c)
        integrate(c, &ch[c], pos, lineClocks);
    outNext -= lineClocks;
    hostAudioOut(out, frames);
}
