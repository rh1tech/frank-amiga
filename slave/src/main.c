/*
 * frank-amiga — slave (RP2350A, U6), M0 bring-up firmware.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Brings up clocks and PSRAM, benchmarks memory, and answers the master's
 * link control frames (HELLO, PING, soak tests). Console on UART1 (J4).
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "clocks.h"
#include "link_bus.h"
#include "link_pins.h"
#include "link_session.h"
#include "membench.h"
#include "psram_init.h"

#define SOAK_MAX_BYTES (64u * 1024u)

static link_t link;
static link_session_t sess;
static uint8_t ctrl_tx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint8_t ctrl_rx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint32_t soak_buf[SOAK_MAX_BYTES / 4];

static uint32_t psram_bytes;

static void reply_hello(void) {
    link_node_info_t info = { 0 };
    info.sys_clk_hz = clock_get_hz(clk_sys);
    info.proto_ver = LINK_PROTO_VER;
    info.psram_bytes = psram_bytes;
    info.uptime_ms = to_ms_since_boot(get_absolute_time());
    snprintf(info.build, sizeof info.build, "slave %s %s", __DATE__, __TIME__);
    link_s_send_ctrl(&sess, LINK_OP_HELLO_ACK, 0, 0, &info, sizeof info);
}

static void soak_m2s(uint32_t bytes, uint32_t seed) {
    if (bytes > SOAK_MAX_BYTES) bytes = SOAK_MAX_BYTES;
    bytes &= ~3u;
    memset(soak_buf, 0, bytes);
    bool ok = link_s_bulk_recv(&sess, soak_buf, bytes);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < bytes / 4; i++)
        if (soak_buf[i] != link_soak_word(seed, i)) bad++;
    link_s_send_ctrl(&sess, LINK_OP_SOAK_M2S_ACK, bad, ok && !bad, NULL, 0);
}

static void soak_s2m(uint32_t bytes, uint32_t seed) {
    if (bytes > SOAK_MAX_BYTES) bytes = SOAK_MAX_BYTES;
    bytes &= ~3u;
    for (uint32_t i = 0; i < bytes / 4; i++)
        soak_buf[i] = link_soak_word(seed, i);
    link_s_send_ctrl(&sess, LINK_OP_SOAK_S2M_ACK, bytes, seed, NULL, 0);
    link_s_bulk_send(&sess, soak_buf, bytes);
}

int main(void) {
    const bool clk_ok = frank_clocks_init();
    stdio_init_all();
    gpio_init(S_LED_PIN);
    gpio_set_dir(S_LED_PIN, GPIO_OUT);

    printf("\n\nfrank-amiga slave M0 (%s %s)\n", __DATE__, __TIME__);
    if (!clk_ok) printf("WARNING: clock setup failed\n");
    frank_clocks_report();

    psram_init_with_freq(S_PSRAM_CS_PIN, FRANK_PSRAM_MAX_MHZ);
    psram_bytes = membench_psram_test(8u << 20);
    printf("PSRAM: %lu bytes %s\n", (unsigned long)psram_bytes,
           psram_bytes ? "OK" : "FAILED");
    frank_clocks_report();
    if (psram_bytes)
        membench_run(soak_buf, sizeof soak_buf);

    link_init(&link, LINK_PIO_SLAVE, S_LINK_B_DATA_BASE, S_LINK_A_DATA_BASE,
              S_LINK_DB_OUT, S_LINK_DB_IN, S_LINK_FS, false);
    sess.link = &link;
    sess.ctrl_tx = ctrl_tx;
    sess.ctrl_rx = ctrl_rx;
    printf("link: waiting for master\n");

    uint32_t frames = 0, last_print = 0;
    for (;;) {
        const uint16_t op = link_s_wait_ctrl(&sess, 100000);
        const link_hdr_t *h = link_rx_hdr(&sess);
        switch (op) {
        case 0:
            break;
        case LINK_OP_HELLO:
            reply_hello();
            printf("link: HELLO\n");
            break;
        case LINK_OP_PING:
            link_s_send_ctrl(&sess, LINK_OP_PONG, h->arg0, h->arg1, NULL, 0);
            break;
        case LINK_OP_SET_CLKDIV:
            link_set_bulk_clkdiv(&link, (float)h->arg0 / 100.0f);
            link_s_send_ctrl(&sess, LINK_OP_SET_CLKDIV_ACK, h->arg0, 0, NULL, 0);
            printf("link: bulk clkdiv %.2f\n", (double)h->arg0 / 100.0);
            break;
        case LINK_OP_SOAK_M2S:
            soak_m2s(h->arg0, h->arg1);
            break;
        case LINK_OP_SOAK_S2M:
            soak_s2m(h->arg0, h->arg1);
            break;
        default:
            printf("link: unknown op 0x%04x\n", op);
            break;
        }
        if (op) frames++;
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_print >= 5000u) {
            last_print = now;
            gpio_xor_mask(1u << S_LED_PIN);
            printf("alive %lu s, %lu ctrl frames\n", (unsigned long)(now / 1000u),
                   (unsigned long)frames);
        }
    }
}
