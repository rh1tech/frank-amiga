/*
 * frank-amiga — master (RP2350B, U3).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Core 1 renders the Amiga's display (Denise) from the slave's message
 * stream and drives HDMI; core 0 runs the link receiver, Paula's output,
 * input and the console on UART0 (J2): type `help`.
 */
#include <stdio.h>
#include <stdlib.h>
#include "link_stream.h"
#include <string.h>

#include "pico/stdlib.h"

#include "clocks.h"
#include "console.h"
#include "link_bus.h"
#include "link_pins.h"
#include "link_session.h"
#include "membench.h"
#include "psram_init.h"
#include "bringup.h"
#include "denise/denise.h"
#include "denise/denise_frames.h"
#include "input.h"
#include "stream.h"
#include "media.h"
#include "osd.h"
#include "usb_amiga.h"
#include "usb/usbhid.h"
#include "audio/audio.h"
#include "video/hstx_video.h"
#include "pico/multicore.h"

#define M_PSRAM_CS_PIN 47
#define SOAK_MAX_BYTES (8u * 1024u)
/* Frame records (4 x dvi_indexed_frame_t, ~136 KB) at the start of PSRAM. */
#define PSRAM_RECORDS ((void *)0x11000000u)

static link_t link;
static link_session_t sess;
static uint8_t ctrl_tx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint8_t ctrl_rx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint32_t soak_buf[SOAK_MAX_BYTES / 4];

static bool slave_hello(void) {
    if (!link_m_send_ctrl(&sess, LINK_OP_HELLO, 0, 0, NULL, 0) ||
        !link_m_recv_ctrl(&sess) ||
        link_rx_hdr(&sess)->op != LINK_OP_HELLO_ACK) {
        printf("hello: no answer\n");
        return false;
    }
    const link_node_info_t *info =
        (const link_node_info_t *)(sess.ctrl_rx + sizeof(link_hdr_t));
    printf("hello: slave '%.32s' clk %lu Hz psram %lu up %lu ms\n", info->build,
           (unsigned long)info->sys_clk_hz, (unsigned long)info->psram_bytes,
           (unsigned long)info->uptime_ms);
    if (info->sys_clk_hz != clock_get_hz(clk_sys)) {
        printf("hello: CLOCK MISMATCH (master %lu Hz) - link will drop bytes\n",
               (unsigned long)clock_get_hz(clk_sys));
        return false;
    }
    return true;
}

static void cmd_ping(int argc, char **argv) {
    const int n = argc > 1 ? atoi(argv[1]) : 100;
    int ok = 0;
    uint64_t t0 = time_us_64();
    for (int i = 0; i < n; i++) {
        if (link_m_send_ctrl(&sess, LINK_OP_PING, (uint32_t)i, 0, NULL, 0) &&
            link_m_recv_ctrl(&sess) && link_rx_hdr(&sess)->op == LINK_OP_PONG &&
            link_rx_hdr(&sess)->arg0 == (uint32_t)i)
            ok++;
    }
    uint64_t dt = time_us_64() - t0;
    printf("ping: %d/%d ok, %.1f us round trip\n", ok, n, (double)dt / n);
}

static bool set_clkdiv(float div) {
    const uint32_t x100 = (uint32_t)(div * 100.0f + 0.5f);
    if (!link_m_send_ctrl(&sess, LINK_OP_SET_CLKDIV, x100, 0, NULL, 0) ||
        !link_m_recv_ctrl(&sess) ||
        link_rx_hdr(&sess)->op != LINK_OP_SET_CLKDIV_ACK)
        return false;
    link_set_bulk_clkdiv(&link, div);
    return true;
}

/* soak <clkdiv> <seconds> [bytes]: alternate M->S and S->M bulk blocks
 * with a fresh pattern each, counting failed handshakes and bad words. */
static void cmd_soak(int argc, char **argv) {
    const float div = argc > 1 ? (float)atof(argv[1]) : 2.0f;
    const uint32_t secs = argc > 2 ? (uint32_t)atoi(argv[2]) : 10u;
    uint32_t bytes = argc > 3 ? (uint32_t)atoi(argv[3]) : 4096u;
    if (bytes > SOAK_MAX_BYTES) bytes = SOAK_MAX_BYTES;
    bytes &= ~3u;
    if (!set_clkdiv(div)) {
        printf("soak: slave did not accept clkdiv\n");
        return;
    }
    uint32_t blocks = 0, hs_fail = 0, bad_blocks = 0, bad_words = 0;
    uint64_t moved = 0, busy_us = 0;
    const uint64_t t_end = time_us_64() + (uint64_t)secs * 1000000u;
    uint32_t seed = time_us_32();
    while (time_us_64() < t_end) {
        seed = seed * 1664525u + 1013904223u;
        /* M -> S */
        for (uint32_t i = 0; i < bytes / 4; i++)
            soak_buf[i] = link_soak_word(seed, i);
        uint64_t t0 = time_us_64();
        bool ok = link_m_send_ctrl(&sess, LINK_OP_SOAK_M2S, bytes, seed, NULL, 0) &&
                  link_m_bulk_send(&sess, soak_buf, bytes) &&
                  link_m_recv_ctrl(&sess) &&
                  link_rx_hdr(&sess)->op == LINK_OP_SOAK_M2S_ACK;
        busy_us += time_us_64() - t0;
        if (!ok) {
            hs_fail++;
        } else if (!link_rx_hdr(&sess)->arg1) {
            bad_blocks++;
            bad_words += link_rx_hdr(&sess)->arg0;
        }
        moved += bytes;
        /* S -> M */
        seed = seed * 1664525u + 1013904223u;
        memset(soak_buf, 0, bytes);
        t0 = time_us_64();
        ok = link_m_send_ctrl(&sess, LINK_OP_SOAK_S2M, bytes, seed, NULL, 0) &&
             link_m_recv_ctrl(&sess) &&
             link_rx_hdr(&sess)->op == LINK_OP_SOAK_S2M_ACK &&
             link_m_bulk_recv(&sess, soak_buf, bytes);
        busy_us += time_us_64() - t0;
        if (!ok) {
            hs_fail++;
        } else {
            uint32_t bad = 0;
            for (uint32_t i = 0; i < bytes / 4; i++)
                if (soak_buf[i] != link_soak_word(seed, i)) bad++;
            if (bad) { bad_blocks++; bad_words += bad; }
        }
        moved += bytes;
        blocks += 2;
    }
    printf("soak: clkdiv %.2f, %lu blocks of %lu B, %lu handshake failures, "
           "%lu bad blocks (%lu bad words), %.2f MB/s incl. handshakes\n",
           (double)div, (unsigned long)blocks, (unsigned long)bytes,
           (unsigned long)hs_fail, (unsigned long)bad_blocks,
           (unsigned long)bad_words, (double)moved / (double)busy_us);
}

static void cmd_hello(int argc, char **argv) { slave_hello(); }

/* ── The Amiga display ── */
static bool amiga_running;

static void core1_amiga(void) {
    video_start();
    hostCore1Loop();
}

static void amiga_start(void) {
    if (amiga_running) return;
    if (!video_init(VIDEO_640x480p50, denise_row, denise_frame_start)) {
        printf("amiga: video init failed\n");
        return;
    }
    denise_frames_init(PSRAM_RECORDS);
    osd_init();
    usb_amiga_init();
    denise_c1_init();
    denise_audio_init();
    multicore_launch_core1(core1_amiga);
    stream_start();
    amiga_running = true;
    printf("amiga: display %s, streaming from the slave\n", video_mode_name());
}

static void main_poll(void);

static void cmd_stats(int argc, char **argv) {
    const uint32_t f0 = dvi_frames_shown, r0 = dvi_refreshes;
    const stream_stats_t s0 = stream_stats;
    /* Measure for a second while the stream keeps running. */
    const uint64_t until = time_us_64() + 1000000u;
    while (time_us_64() < until) main_poll();
    const stream_stats_t s1 = stream_stats;
    printf("stats: shown %lu/s of %lu refreshes, link %lu chunks/s %lu kwords/s "
           "busy %lu ms/s, errors %lu bad %lu gaps %lu resyncs %lu, corrupt %lu, "
           "late rows %lu, audio dropped %lu\n",
           (unsigned long)(dvi_frames_shown - f0), (unsigned long)(dvi_refreshes - r0),
           (unsigned long)(s1.chunks - s0.chunks),
           (unsigned long)((s1.words - s0.words) / 1000u),
           (unsigned long)((s1.busy_us - s0.busy_us) / 1000u),
           (unsigned long)s1.link_errors, (unsigned long)s1.bad_chunks,
           (unsigned long)s1.seq_gaps, (unsigned long)s1.resyncs,
           (unsigned long)denise_corrupt_count, (unsigned long)video_late_rows,
           (unsigned long)denise_audio_dropped);
}

/* type <text...>: types into the Amiga (\n for Return). */
static void cmd_type(int argc, char **argv) {
    char buf[160];
    size_t n = 0;
    for (int i = 1; i < argc; i++) {
        for (const char *p = argv[i]; *p && n < sizeof buf - 2; p++) {
            if (p[0] == '\\' && p[1] == 'n') { buf[n++] = '\n'; p++; }
            else buf[n++] = *p;
        }
        if (i + 1 < argc && n < sizeof buf - 2) buf[n++] = ' ';
    }
    buf[n] = 0;
    input_type(buf);
}

/* key <hex code> [up|down|tap] */
static void cmd_key(int argc, char **argv) {
    if (argc < 2) return;
    const uint8_t code = (uint8_t)strtoul(argv[1], NULL, 16);
    const char *what = argc > 2 ? argv[2] : "tap";
    if (strcmp(what, "up")) input_key(code, false);
    if (strcmp(what, "down")) input_key(code, true);
}

/* state save|load: the slave's savestate. */
static void cmd_state(int argc, char **argv) {
    const bool load = argc > 1 && !strcmp(argv[1], "load");
    if (argc < 2 || (!load && strcmp(argv[1], "save"))) return;
    media_state(load);
}

/* usb: what the USB host sees. */
static void cmd_usb(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("usb: keyboard %s, mouse %s\n", usbhid_keyboard_connected() ? "yes" : "no",
           usbhid_mouse_connected() ? "yes" : "no");
    for (int i = 0; i < 2; i++) {
        usbhid_gamepad_state_t g;
        usbhid_get_gamepad_state_idx(i, &g);
        if (g.connected)
            printf("usb: gamepad %d (Amiga port %d): x %d y %d dpad %x buttons %04x\n", i + 1,
                   i == 0 ? 2 : 1, g.axis_x, g.axis_y, g.dpad, g.buttons);
        else
            printf("usb: gamepad %d: none\n", i + 1);
    }
}

static void wait_slave_and_stream(void);

/* mem <chip KB> <slow KB>: the machine's RAM, then cold-start. */
static void cmd_mem(int argc, char **argv) {
    if (argc < 3) {
        printf("chip=%u slow=%u\n", media_config.chip_kb, media_config.slow_kb);
        return;
    }
    media_config.chip_kb = (uint16_t)atoi(argv[1]);
    media_config.slow_kb = (uint16_t)atoi(argv[2]);
    if (media_boot()) wait_slave_and_stream(); else stream_start();
}

/* joy <port 0|1> <bits: 1 up 2 down 4 left 8 right 16 fire 32 fire2> */
static void cmd_joy(int argc, char **argv) {
    if (argc < 3) return;
    usb_amiga_force_joy(atoi(argv[1]), (uint8_t)strtol(argv[2], NULL, 0));
}

/* mouse <dx> <dy> [buttons] */
static void cmd_mouse(int argc, char **argv) {
    if (argc < 3) return;
    input_mouse(atoi(argv[1]), atoi(argv[2]), argc > 3 ? (uint8_t)atoi(argv[3]) : 0);
}

static void cmd_reset(int argc, char **argv) { stream_input(LINK_EV_RESET, 0, 0, 0); }

/* Wait for the slave to come back after a BOOT, then stream again. */
static void wait_slave_and_stream(void) {
    sleep_ms(300);
    for (int i = 0; i < 80 && !slave_hello(); i++) sleep_ms(100);
    stream_start();
}

/* boot: reread 0:/amiga/frank-amiga.ini, upload, cold-start the Amiga. */
static void cmd_boot(int argc, char **argv) {
    if (media_load_config() && media_boot()) wait_slave_and_stream();
    else stream_start();
}

/* kick <path> / ext <path>: change the ROM and cold-start. */
static void cmd_kick(int argc, char **argv) {
    if (argc < 2) { printf("kick=%s ext=%s\n", media_config.kick, media_config.ext); return; }
    char *dst = !strcmp(argv[0], "ext") ? media_config.ext : media_config.kick;
    snprintf(dst, sizeof media_config.kick, "%s%s",
             argv[1][0] == '0' && argv[1][1] == ':' ? "" : "0:", argv[1]);
    if (!strcmp(argv[1], "none")) dst[0] = 0;
    if (media_boot()) wait_slave_and_stream(); else stream_start();
}

/* df0 <path>: change the disk in DF0 while running. */
static void cmd_df0(int argc, char **argv) {
    if (argc < 2) { printf("df0=%s\n", media_config.df0); return; }
    media_insert(0, argv[1]);
}

static void cmd_df1(int argc, char **argv) {
    if (argc < 2) { printf("df1=%s\n", media_config.df1); return; }
    media_insert(1, argv[1]);
}

/* sdput <psram offset hex> <bytes> <path>: save master PSRAM to the SD. */
static void cmd_sdput(int argc, char **argv) {
    if (argc < 4) return;
    media_sdput((uint32_t)strtoul(argv[1], NULL, 16), (uint32_t)strtoul(argv[2], NULL, 0), argv[3]);
}

/* osd: toggle the menu; osdkey <up|down|left|right|enter|esc|pgup|pgdn>. */
static void cmd_osd(int argc, char **argv) { osd_toggle(); }
static void cmd_osdkey(int argc, char **argv) {
    static const struct { const char *n; uint8_t hid; } k[] = {
        {"up", 0x52}, {"down", 0x51}, {"left", 0x50}, {"right", 0x4F},
        {"enter", 0x28}, {"esc", 0x29}, {"pgup", 0x4B}, {"pgdn", 0x4E},
    };
    for (int a = 1; a < argc; a++)
        for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
            if (!strcmp(argv[a], k[i].n)) osd_key(k[i].hid, true);
}

/* vol <0..256>: output volume (256 = unity; default 128). */
static void cmd_vol(int argc, char **argv) {
    if (argc > 1) audio_set_volume((uint32_t)atoi(argv[1]));
    extern volatile uint32_t audio_frames_played, denise_audio_produced;
    const uint32_t p0 = denise_audio_produced, c0 = audio_frames_played;
    const uint32_t r0 = dvi_refreshes;
    extern volatile uint32_t denise_audio_lines, denise_audio_long;
    const uint32_t l0 = denise_audio_lines, g0 = denise_audio_long;
    const uint64_t until = time_us_64() + 2000000u;
    while (time_us_64() < until) main_poll();
    printf("audio: %lu line records (%lu long) per refresh\n",
           (unsigned long)((denise_audio_lines - l0) / (dvi_refreshes - r0)),
           (unsigned long)((denise_audio_long - g0) / (dvi_refreshes - r0)));
    printf("audio: produced %lu/s, played %lu/s over %lu refreshes\n",
           (unsigned long)(denise_audio_produced - p0) / 2u,
           (unsigned long)(audio_frames_played - c0) / 2u,
           (unsigned long)(dvi_refreshes - r0));
    printf("vol %lu/256, ring %lu bytes queued, %lu rate adjustments\n",
           (unsigned long)audio_get_volume(), (unsigned long)audio_queued(),
           (unsigned long)denise_audio_adjusted);
}

static void main_poll(void) {
    if (amiga_running) {
        usb_amiga_poll();
        input_poll();
        stream_poll();
        media_poll();
    } else {
        bringup_poll();
    }
}
static void cmd_clocks(int argc, char **argv) { frank_clocks_report(); }

static const console_cmd_t cmds[] = {
    { "hello",  cmd_hello,  "probe the slave" },
    { "ping",   cmd_ping,   "ping [n]: control round trips" },
    { "soak",   cmd_soak,   "soak <clkdiv> <seconds> [bytes]: link soak" },
    { "clocks", cmd_clocks, "show clocks" },
    { "stats",  cmd_stats,  "display/link statistics over one second" },
    { "type",   cmd_type,   "type <text>: type into the Amiga (\\n = Return)" },
    { "key",    cmd_key,    "key <hex> [up|down|tap]: Amiga raw keycode" },
    { "mouse",  cmd_mouse,  "mouse <dx> <dy> [buttons]" },
    { "state",  cmd_state,  "state save|load: savestate (slave PSRAM)" },
    { "usb",    cmd_usb,    "usb: connected keyboard, mouse, gamepads" },
    { "mem",    cmd_mem,    "mem <chip KB> <slow KB>: RAM (512/1024/2048, 0/512/1024), cold-start" },
    { "joy",    cmd_joy,    "joy <port> <bits> (1U 2D 4L 8R 16F 32F2)" },
    { "reset",  cmd_reset,  "Ctrl-Amiga-Amiga" },
    { "vol",    cmd_vol,    "vol [0..256]: audio output volume" },
    { "osd",    cmd_osd,    "toggle the on-screen menu (F12)" },
    { "osdkey", cmd_osdkey, "osdkey <up|down|left|right|enter|esc|pgup|pgdn>..." },
    { "boot",   cmd_boot,   "reload frank-amiga.ini, upload, cold-start" },
    { "kick",   cmd_kick,   "kick <path>: change Kickstart and cold-start" },
    { "ext",    cmd_kick,   "ext <path|none>: extended ROM and cold-start" },
    { "df0",    cmd_df0,    "df0 <path>: change the disk in DF0" },
    { "df1",    cmd_df1,    "df1 <path>: change the disk in DF1" },
    { "sdput",  cmd_sdput,  "sdput <psram off hex> <bytes> <path>: save to SD" },
    BRINGUP_COMMANDS
    { NULL, NULL, NULL },
};

int main(void) {
    const bool clk_ok = frank_clocks_init();
    stdio_init_all();
    printf("\n\nfrank-amiga master M0 (%s %s)\n", __DATE__, __TIME__);
    if (!clk_ok) printf("WARNING: clock setup failed\n");

    psram_init_with_freq(M_PSRAM_CS_PIN, FRANK_PSRAM_MAX_MHZ);
    const uint32_t psram_bytes = membench_psram_test(8u << 20);
    printf("PSRAM: %lu bytes %s\n", (unsigned long)psram_bytes,
           psram_bytes ? "OK" : "FAILED");
    frank_clocks_report();
    if (psram_bytes)
        membench_run(soak_buf, sizeof soak_buf);

    link_init(&link, LINK_PIO_MASTER, M_LINK_A_DATA_BASE, M_LINK_B_DATA_BASE,
              M_LINK_DB_OUT, M_LINK_DB_IN, M_LINK_FS, true);
    sess.link = &link;
    sess.ctrl_tx = ctrl_tx;
    sess.ctrl_rx = ctrl_rx;
    sess.handshake_timeout_us = 200000;
    stream_init(&sess);
    bringup_init();
    /* Wait for the slave (it may still be booting), then run the Amiga. */
    for (int i = 0; i < 50 && !slave_hello(); i++) sleep_ms(100);
    link_set_bulk_clkdiv(&link, 1.0f);
    sess.handshake_timeout_us = 20000;
    media_init(&sess);
    /* Kickstart and disks from the microSD: upload, then the slave
     * restarts from them. Without a configuration it runs whatever it
     * has in flash. */
    if (media_load_config() && media_boot()) {
        sleep_ms(300);
        for (int i = 0; i < 80 && !slave_hello(); i++) sleep_ms(100);
    }
    amiga_start();
    console_run(cmds, main_poll);
}
