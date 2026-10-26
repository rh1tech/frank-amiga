/*
 * frank-amiga — M0 bench bring-up commands on the master.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Video: a test pattern built from 12-bit Amiga colours, so a captured
 * frame can be checked pixel for pixel: eight colour bars (top half), a
 * 16-step grey ramp (lower quarter), a 1-pixel white frame on the image
 * edge, and a square that moves one row per frame.
 */
#include "bringup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "video/hstx_video.h"
#include "audio/audio.h"
#include "ff.h"
#include "usb/usbhid.h"

#include <math.h>

static uint16_t ramp565[16], bg565, white565;

static const uint16_t bars12[8] = {
    0xFFF, 0xFF0, 0x0FF, 0x0F0, 0xF0F, 0xF00, 0x00F, 0x000,
};
static uint16_t bars565[8];
static volatile uint32_t square_row;

static void __not_in_flash_func(pattern_row)(uint32_t row, uint16_t *px, uint32_t w) {
    const uint32_t rows = video_rows();
    if (row == 0 || row == rows - 1u) {
        for (uint32_t x = 0; x < w; x++) px[x] = white565;
        return;
    }
    if (row < rows / 2u) {
        for (uint32_t x = 0; x < w; x++) px[x] = bars565[(x * 8u) / w];
    } else if (row >= rows * 3u / 4u) {
        for (uint32_t x = 0; x < w; x++) {
            px[x] = ramp565[(x * 16u) / w];
        }
    } else {
        for (uint32_t x = 0; x < w; x++) px[x] = bg565;
        const uint32_t sq = square_row % (rows / 4u);
        if (row - rows / 2u >= sq && row - rows / 2u < sq + 16u)
            for (uint32_t x = w / 2u - 16u; x < w / 2u + 16u; x++) px[x] = white565;
    }
    px[0] = px[w - 1u] = white565;
}

static void __not_in_flash_func(pattern_frame)(uint32_t frame) {
    square_row = frame;
}

static void core1_video(void) {
    video_start();
    for (;;) __wfi();
}

static video_mode_t parse_mode(const char *s) {
    if (!strcmp(s, "480")) return VIDEO_720x480p60;
    if (!strcmp(s, "640")) return VIDEO_640x480p60;
    return VIDEO_720x576p50;
}

static bool video_running;

static void start_video(video_mode_t mode) {
    if (video_running) {
        /* Mode changes restart the board: tearing down HSTX + four DMA
         * channels on a live core 1 is not worth the code at M0. */
        printf("video: already running %s; use `reboot` to change\n",
               video_mode_name());
        return;
    }
    if (!video_init(mode, pattern_row, pattern_frame)) {
        printf("video: init failed\n");
        return;
    }
    for (int i = 0; i < 8; i++) bars565[i] = video_rgb12_to_565(bars12[i]);
    for (int i = 0; i < 16; i++)
        ramp565[i] = video_rgb12_to_565((uint16_t)(i << 8 | i << 4 | i));
    bg565 = video_rgb12_to_565(0x125);
    white565 = video_rgb12_to_565(0xFFF);
    multicore_launch_core1(core1_video);
    video_running = true;
    sleep_ms(100);
    printf("video: %s (%s range), %lu x %lu rows\n", video_mode_name(),
           video_full_range() ? "full" : "limited",
           (unsigned long)video_width(), (unsigned long)video_rows());
}

void bringup_cmd_video(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "stat")) {
        const uint32_t f0 = video_frames;
        sleep_ms(1000);
        printf("video: %s, %lu frames/s, %lu late rows total\n", video_mode_name(),
               (unsigned long)(video_frames - f0), (unsigned long)video_late_rows);
        return;
    }
    start_video(parse_mode(argc > 1 ? argv[1] : "576"));
}

void bringup_cmd_reboot(int argc, char **argv) {
    printf("rebooting\n");
    sleep_ms(50);
    watchdog_reboot(0, 0, 0);
}

/* ---- Audio: a sine on both channels, generated into the I2S ring from
 * the console's idle loop. ---- */
static int16_t sine_tab[1024];
static uint32_t tone_phase, tone_step;       /* 32-bit phase accumulator */
static uint64_t tone_until_us;

void bringup_cmd_tone(int argc, char **argv) {
    const float hz = argc > 1 ? (float)atof(argv[1]) : 1000.0f;
    const float secs = argc > 2 ? (float)atof(argv[2]) : 5.0f;
    if (argc > 3) audio_set_volume((uint32_t)atoi(argv[3]));
    tone_step = (uint32_t)((double)hz * 4294967296.0 / (double)audio_get_rate());
    tone_until_us = time_us_64() + (uint64_t)(secs * 1e6f);
    printf("tone: %.1f Hz for %.1f s at %lu Hz, volume %lu/256\n", (double)hz,
           (double)secs, (unsigned long)audio_get_rate(),
           (unsigned long)audio_get_volume());
}

static void tone_poll(void) {
    if (!tone_until_us) return;
    if (time_us_64() >= tone_until_us) { tone_until_us = 0; return; }
    int16_t frames[2 * 128];
    while (audio_space() >= sizeof frames) {
        for (int i = 0; i < 128; i++) {
            const int16_t v = sine_tab[tone_phase >> 22];
            frames[2 * i] = frames[2 * i + 1] = v;
            tone_phase += tone_step;
        }
        audio_write(frames, sizeof frames);
    }
}

/* ---- microSD ---- */
static FATFS sd_fs;
static bool sd_mounted;

static bool sd_mount(void) {
    if (sd_mounted) return true;
    const FRESULT r = f_mount(&sd_fs, "0:", 1);
    if (r != FR_OK) { printf("sd: mount failed (%d)\n", r); return false; }
    sd_mounted = true;
    return true;
}

void bringup_cmd_sd(int argc, char **argv) {
    if (!sd_mount()) return;
    const char *path = argc > 1 ? argv[1] : "0:/";
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, path) != FR_OK) { printf("sd: cannot open %s\n", path); return; }
    int n = 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        printf("  %s%-40s %10lu\n", (fi.fattrib & AM_DIR) ? "/" : " ", fi.fname,
               (unsigned long)fi.fsize);
        n++;
    }
    f_closedir(&d);
    printf("sd: %d entries in %s\n", n, path);
}

/* sdread <file>: read the whole file, report throughput. */
void bringup_cmd_sdread(int argc, char **argv) {
    if (argc < 2 || !sd_mount()) return;
    FIL f;
    if (f_open(&f, argv[1], FA_READ) != FR_OK) { printf("sd: cannot open %s\n", argv[1]); return; }
    static uint8_t buf[16384] __attribute__((aligned(4)));
    UINT got;
    uint32_t total = 0, sum = 0;
    const uint64_t t0 = time_us_64();
    while (f_read(&f, buf, sizeof buf, &got) == FR_OK && got) {
        total += got;
        for (UINT i = 0; i < got; i += 64) sum += buf[i];
    }
    const uint64_t dt = time_us_64() - t0;
    f_close(&f);
    printf("sd: %lu bytes in %lu ms, %.2f MB/s (sum %lu)\n", (unsigned long)total,
           (unsigned long)(dt / 1000u), (double)total / (double)dt, (unsigned long)sum);
}

void bringup_init(void) {
    for (int i = 0; i < 1024; i++)
        sine_tab[i] = (int16_t)lrintf(32767.0f * sinf(6.2831853f * (float)i / 1024.0f));
    audio_init();
    usbhid_init();
}

/* Print every key transition and mouse change: the M0 check that the USB
 * host enumerates keyboards and mice behind a hub. */
static void usb_poll(void) {
    usbhid_task();
    uint8_t code;
    int down;
    while (usbhid_get_key_action(&code, &down))
        printf("usb: key 0x%02x %s\n", code, down ? "down" : "up");
    usbhid_mouse_state_t ms;
    usbhid_get_mouse_state(&ms);
    if (ms.has_motion || ms.wheel)
        printf("usb: mouse dx %d dy %d wheel %d buttons %u\n", ms.dx, ms.dy,
               ms.wheel, ms.buttons);
}

void bringup_poll(void) {
    tone_poll();
    usb_poll();
}
