/*
 * frank-amiga — master: Paula's mixed output to the I2S DAC.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * omega/AudioMix.c turns the slave's line records into 48 kHz stereo in
 * emulated time and hands it to hostAudioOut(); the I2S driver
 * (audio/audio.c) plays it from its ring.
 */
#include "denise.h"
#include "audio/audio.h"
#include "../omega/Audio.h"

#include "pico/stdlib.h"

volatile uint32_t denise_audio_dropped;
volatile uint32_t denise_corrupt_count;

/* Rate control. Paula's output is 48 kHz of *emulated* time; emulation is
 * paced to the display (50.000 Hz, a real PAL Amiga is 49.92) and arrives
 * in per-line bursts, so the DAC's ring drifts. Keep it near half full by
 * dropping or repeating one stereo frame now and then: at most one per
 * RATE_PERIOD frames (0.2 %), which is inaudible. */
#define RING_FRAMES  (32768u / 4u)
#define RATE_PERIOD  500u
volatile uint32_t denise_audio_adjusted;
volatile uint32_t denise_audio_produced;

void __not_in_flash_func(hostAudioOut)(const int16_t *samples, int frames) {
    static const int16_t silence[2 * 64];
    static uint32_t since_adjust;
    int16_t buf[2 * 64 + 2];
    denise_audio_produced += (uint32_t)frames;
    while (frames > 0) {
        const int n = frames < 64 ? frames : 64;
        const int16_t *src = samples ? samples : silence;
        uint32_t out = (uint32_t)n;
        since_adjust += (uint32_t)n;
        const uint32_t fill = audio_queued() / 4u;
        if (since_adjust >= RATE_PERIOD && fill > RING_FRAMES * 5u / 8u) {
            out = (uint32_t)n - 1u;                 /* drop the last frame */
            since_adjust = 0;
            denise_audio_adjusted++;
        } else if (since_adjust >= RATE_PERIOD && fill < RING_FRAMES * 3u / 8u) {
            for (int i = 0; i < 2 * n; i++) buf[i] = src[i];
            buf[2 * n] = src[2 * n - 2];            /* repeat the last frame */
            buf[2 * n + 1] = src[2 * n - 1];
            src = buf;
            out = (uint32_t)n + 1u;
            since_adjust = 0;
            denise_audio_adjusted++;
        }
        const uint32_t took = audio_write(src, out * 4u);
        if (took < out * 4u)
            denise_audio_dropped += out - took / 4u;
        if (samples) samples += 2 * n;
        frames -= n;
    }
}

void denise_audio_service(void) {}

volatile uint32_t denise_resync_request;

void denise_stream_corrupt(uint32_t header) {
    (void)header;
    denise_corrupt_count++;
    denise_resync_request = 1;   /* stream.c restarts at a frame start */
}

void denise_audio_init(void) {
    audioMixInit();
}
