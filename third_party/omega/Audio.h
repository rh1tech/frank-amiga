//
//  Audio.h
//  Paula audio: four DMA channels, mixed to 48 kHz stereo.
//
//  Audio.c emulates the channels (DMA fetches, periods, interrupts) and
//  describes each raster line's output as a line record; AudioMix.c turns
//  the records into 48 kHz samples. The host passes the records from one to
//  the other (RP2350: from core 0 to core 1, which does the mixing).
//

#ifndef Audio_h
#define Audio_h

#include <stdint.h>

#define AUDIO_RATE 48000

// DMACON was written (old value of DMACONR); starts/stops channels.
void audioDmaconChanged(uint16_t old);
// The CPU wrote AUDxDAT (non-DMA playback while the channel's DMA is off).
void audioDatWritten(int channel);
// Advances the four channels by one raster line and passes its line record
// to hostAudioLine() (called at the end of every line).
void audioLine(int slots);
// Silences all channels (reset).
void audioReset(void);

// Line record: two header words, then one word per sample change.
// Header word 0: AUDIO_LINE_* flags. The ACTIVE flags tell whether any
// channel was playing (not off) as the line began and as it ended.
#define AUDIO_LINE_HEADER     0x80000000u  // marks word 0 (events have bit 31 clear)
#define AUDIO_LINE_LONG       0x40000000u  // 228 colour clocks (else 227)
#define AUDIO_LINE_LED        0x20000000u  // the LED filter is on
#define AUDIO_LINE_ACTIVE     0x10000000u  // a channel played as the line began
#define AUDIO_LINE_ACTIVE_END 0x08000000u  // ... and as it ended
// Header word 1: AUDxVOL of channel c (0-64) in bits 8c..8c+6.
// Event: channel c's sample becomes s (signed 8-bit) at colour clock p of
// the line (0..line length; the length itself: from the next line on).
#define AUDIO_EVENT(c, p, s) \
    ((uint32_t)(c) | (uint32_t)(p) << 2 | (uint32_t)(uint8_t)(s) << 10)
#define AUDIO_EVENT_CHANNEL(e) ((int)((e) & 3u))
#define AUDIO_EVENT_CLOCK(e)   ((int)(((e) >> 2) & 0xffu))
#define AUDIO_EVENT_SAMPLE(e)  ((int8_t)((e) >> 10))
// Sample changes per line kept in a record; beyond it (CPU playback at
// tiny periods) the record carries the channel's latest sample next line.
#define AUDIO_LINE_MAX_EVENTS 30
#define AUDIO_LINE_MAX_WORDS (2 + AUDIO_LINE_MAX_EVENTS)

// Host hook, once per raster line: its line record (`count` words). Returns
// 0 when the record was dropped (no room); the next one then restates every
// channel's sample.
int hostAudioLine(const uint32_t *words, int count);

// AudioMix.c: mixes one line record (left = channels 0 + 3, right = 1 + 2,
// filtered as the Amiga's output stage) and passes its samples on.
void audioMixInit(void);
void audioMixLine(const uint32_t *words, int count);

// Host hook, once per mixed line: its `frames` stereo output samples
// (left/right interleaved), or NULL for `frames` samples of silence.
void hostAudioOut(const int16_t *samples, int frames);

#endif
