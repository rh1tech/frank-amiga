/*
 * frank-amiga — master (RP2350B, U3), M0 bring-up firmware.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clocks, PSRAM, link HELLO/PING/soak, and the bench peripherals. Driven by
 * a line-oriented console on UART0 (J2): type `help`.
 */
#include <stdio.h>
#include <stdlib.h>
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

#define M_PSRAM_CS_PIN 47
#define SOAK_MAX_BYTES (64u * 1024u)

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
static void cmd_clocks(int argc, char **argv) { frank_clocks_report(); }

static const console_cmd_t cmds[] = {
    { "hello",  cmd_hello,  "probe the slave" },
    { "ping",   cmd_ping,   "ping [n]: control round trips" },
    { "soak",   cmd_soak,   "soak <clkdiv> <seconds> [bytes]: link soak" },
    { "clocks", cmd_clocks, "show clocks" },
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
    slave_hello();

    bringup_init();
    console_run(cmds, bringup_poll);
}
