/*
 * frank-amiga — the steady-state stream between the halves.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The master drives it as request/response on top of link_session:
 *
 *   master: link_m_send_ctrl(LINK_OP_STREAM_REQ, ack, flags, input events)
 *   slave:  replies with one LINK_STREAM_CHUNK_BYTES bulk (link_s_bulk_send)
 *   master: link_m_bulk_recv() into a bounce buffer, appends the payload
 *           to its message ring
 *
 * The payload is the slave's message ring (slave/src/platform/HostRing.h)
 * verbatim, cut wherever the chunk fills: messages may straddle chunks.
 * frame_off marks the first frame start in the chunk, so a master that lost
 * a chunk can resume at a frame boundary.
 */
#ifndef LINK_STREAM_H
#define LINK_STREAM_H

#include <stdint.h>

#define LINK_STREAM_MAGIC        0x4D525453u   /* "STRM" */
#define LINK_STREAM_CHUNK_BYTES  8192u
#define LINK_STREAM_HDR_WORDS    4u
#define LINK_STREAM_PAYLOAD_WORDS \
    (LINK_STREAM_CHUNK_BYTES / 4u - LINK_STREAM_HDR_WORDS)
#define LINK_STREAM_NO_FRAME     0xFFFFu
/* In `words`: a disk has written tracks to save (link_proto.h DIRTY). */
#define LINK_STREAM_WORDS_DIRTY  0x8000u
/* In `words`: a flux-image drive wants a track (link_proto.h TRACKREQ). */
#define LINK_STREAM_WORDS_TRACK  0x4000u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;            /* chunk number, from 1, no gaps */
    uint16_t words;          /* payload words that are valid
                              * | LINK_STREAM_WORDS_DIRTY */
    uint16_t frame_off;      /* first MSG_BEGIN in the payload, or NO_FRAME */
    uint32_t check;          /* XOR of the valid payload words ^ seq */
} link_stream_hdr_t;

/* Control opcodes (link_proto.h numbering space). */
enum {
    LINK_OP_STREAM_REQ  = 0x0060,  /* M->S: arg0 = last chunk seq received,
                                    *       arg1 = flags, payload = events */
};

#define LINK_STREAM_FLAG_RESYNC  1u   /* master lost data: restart at a frame */

/* Input events carried in a STREAM_REQ payload: a count byte then 4-byte
 * events, at most (LINK_PAYLOAD_BYTES - 4) / 4 of them. */
enum {
    LINK_EV_KEY   = 1,   /* a = Amiga raw keycode, b = 1 released          */
    LINK_EV_MOUSE = 2,   /* a = dx (int8), b = dy (int8), c = buttons       */
    LINK_EV_JOY   = 3,   /* a = port, b = LINK_JOY_* bits, c = 0            */
    LINK_EV_RESET = 4,   /* keyboard reset (Ctrl-A-A)                       */
    LINK_EV_FLOPPY = 5,  /* a = 1: disks at real speed, 0: as fast as DMA   */
};
/* LINK_EV_JOY directions and buttons. */
#define LINK_JOY_UP    0x01u
#define LINK_JOY_DOWN  0x02u
#define LINK_JOY_LEFT  0x04u
#define LINK_JOY_RIGHT 0x08u
#define LINK_JOY_FIRE  0x10u
#define LINK_JOY_FIRE2 0x20u
typedef struct __attribute__((packed)) {
    uint8_t type, a, b, c;
} link_input_ev_t;

typedef struct __attribute__((packed)) {
    uint32_t count;
    link_input_ev_t ev[24];
} link_input_batch_t;

#endif
