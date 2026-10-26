/*
 * frank-amiga — inter-processor link protocol (control plane)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fixed-size control frames sequenced by the doorbells (link_session), used
 * for setup: HELLO, configuration, Kickstart / ADF uploads, link tests. The
 * steady-state video/audio stream (slave -> master) and input packets
 * (master -> slave) run on their own framing, defined in link_stream.h.
 */
#ifndef LINK_PROTO_H
#define LINK_PROTO_H

#include <stdint.h>

#define LINK_MAGIC        0x41474D41u   /* "AMGA" */
#define LINK_PROTO_VER    1u

/* Fixed control-frame size. Must be a multiple of 4 (PIO autopush is
 * 32-bit) and large enough for the biggest payload struct below. */
#define LINK_CTRL_BYTES   128u

/* ---- Opcodes ---- */
enum {
    LINK_OP_HELLO        = 0x0001,  /* M->S: are you there?               */
    LINK_OP_HELLO_ACK    = 0x0002,  /* S->M: payload link_node_info_t     */

    LINK_OP_PING         = 0x0040,  /* M->S: liveness / latency probe     */
    LINK_OP_PONG         = 0x0041,

    /* Link soak test. arg0 = bytes, arg1 = pattern seed.
     *   SOAK_M2S: the master sends the bulk; the ack carries arg0 = byte
     *             errors the slave counted, arg1 = 1 if every word matched.
     *   SOAK_S2M: the ack precedes a bulk from the slave, which the
     *             master checks against the same pattern. */
    LINK_OP_SOAK_M2S     = 0x0050,
    LINK_OP_SOAK_M2S_ACK = 0x0051,
    LINK_OP_SOAK_S2M     = 0x0052,
    LINK_OP_SOAK_S2M_ACK = 0x0053,
    /* arg0 = bulk clkdiv x 100 for both directions. */
    LINK_OP_SET_CLKDIV   = 0x0054,
    LINK_OP_SET_CLKDIV_ACK = 0x0055,
};

/* ---- Frame header (24 bytes), followed by payload, zero-padded to
 *      LINK_CTRL_BYTES. ---- */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t op;
    uint16_t proto_ver;
    uint32_t seq;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t crc;      /* CRC-32 of the whole frame with crc treated as 0 */
} link_hdr_t;

#define LINK_PAYLOAD_BYTES (LINK_CTRL_BYTES - sizeof(link_hdr_t))

/* HELLO_ACK payload: lets the master refuse to run against a slave built
 * for a different clock (the link would drop bytes one way) or protocol. */
typedef struct __attribute__((packed)) {
    uint32_t sys_clk_hz;
    uint32_t proto_ver;
    uint32_t psram_bytes;     /* 0 when the PSRAM self-test failed */
    uint32_t uptime_ms;
    char     build[32];
} link_node_info_t;

/* The PIO FIFOs are 32 bits wide and autopull/autopush are word-sized,
 * so every bulk transfer must be a multiple of 4 bytes (and the buffer
 * aligned). */
#define LINK_ALIGN4(n) (((n) + 3u) & ~3u)

uint32_t link_crc32(const void *data, uint32_t len);

/* Build a complete control frame into `frame` (LINK_CTRL_BYTES, 4-byte
 * aligned). Copies `payload_len` bytes of payload and fills in the CRC. */
void link_frame_build(void *frame, uint16_t op, uint32_t seq,
                      uint32_t arg0, uint32_t arg1,
                      const void *payload, uint32_t payload_len);

/* 1 if magic, version and CRC are all good. */
int link_frame_check(const void *frame);

/* Deterministic soak pattern: word i of a buffer seeded with `seed`. */
static inline uint32_t link_soak_word(uint32_t seed, uint32_t i) {
    uint32_t x = seed ^ (i * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

#endif /* LINK_PROTO_H */
