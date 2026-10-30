/*
 * frank-amiga — master: the link stream receiver (core 0).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Whenever core 1's message ring has room for a chunk, ask the slave for
 * one (LINK_OP_STREAM_REQ, carrying any queued input events) and append its
 * payload to the ring. A chunk that fails its checks, or a gap in the
 * sequence, drops what is queued and asks the slave to resume at the next
 * frame start (link_stream.h).

 */
#include "stream.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "denise/denise.h"
#include "link_session.h"
#include "link_stream.h"

static link_session_t *sess;
static uint32_t chunk[LINK_STREAM_CHUNK_BYTES / 4];
static link_input_batch_t batch;
static uint32_t expect_seq = 1;
static bool resync, running;

stream_stats_t stream_stats;

void stream_init(link_session_t *s) {
    sess = s;
}

void stream_start(void) {
    running = true;
    resync = true;   /* begin at a frame start */
}

void stream_stop(void) { running = false; }

volatile bool stream_disk_dirty, stream_track_wanted;

bool stream_input(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
    if (batch.count >= 24u) return false;
    batch.ev[batch.count++] = (link_input_ev_t){ type, a, b, c };
    return true;
}

static bool chunk_valid(const link_stream_hdr_t *h) {
    if (h->magic != LINK_STREAM_MAGIC ||
        (h->words & ~(LINK_STREAM_WORDS_DIRTY | LINK_STREAM_WORDS_TRACK)) > LINK_STREAM_PAYLOAD_WORDS)
        return false;
    const uint32_t *p = chunk + LINK_STREAM_HDR_WORDS;
    uint32_t x = 0;
    const uint32_t n = h->words & ~(LINK_STREAM_WORDS_DIRTY | LINK_STREAM_WORDS_TRACK);
    for (uint32_t i = 0; i < n; i++) x ^= p[i];
    return (x ^ h->seq) == h->check;
}

bool stream_last_full;   /* the last chunk was full: the slave has more */

bool stream_poll(void) {
    stream_last_full = false;
    if (!running) return false;
    stream_stats.polls++;
    if (denise_ring_space() < LINK_STREAM_PAYLOAD_WORDS) {
        stream_stats.ring_full++;
        return false;
    }
    if (denise_resync_request) {
        denise_resync_request = 0;
        resync = true;
    }
    const uint32_t flags = (resync ? LINK_STREAM_FLAG_RESYNC : 0u) |
                           (denise_oldest_frame & 0xFFFFFFu) << LINK_STREAM_OLDEST_SHIFT;
    const uint64_t t0 = time_us_64();
    bool ok = link_m_send_ctrl(sess, LINK_OP_STREAM_REQ, expect_seq - 1u, flags,
                               &batch, 4u + batch.count * 4u);
    if (ok) batch.count = 0;
    ok = ok && link_m_bulk_recv(sess, chunk, LINK_STREAM_CHUNK_BYTES);
    stream_stats.busy_us += time_us_64() - t0;
    if (!ok) {
        stream_stats.link_errors++;
        link_m_resync(sess);
        resync = true;      /* the chunk is lost: restart at a frame */
        return false;
    }
    const link_stream_hdr_t *h = (const link_stream_hdr_t *)chunk;
    if (!chunk_valid(h)) {
        stream_stats.bad_chunks++;
        resync = true;
        return false;
    }
    const uint32_t *payload = chunk + LINK_STREAM_HDR_WORDS;
    const uint32_t words = h->words & ~(LINK_STREAM_WORDS_DIRTY | LINK_STREAM_WORDS_TRACK);
    if (h->words & LINK_STREAM_WORDS_DIRTY) stream_disk_dirty = true;
    if (h->words & LINK_STREAM_WORDS_TRACK) stream_track_wanted = true;
    uint32_t start = 0;
    if (h->seq != expect_seq && !resync) {
        stream_stats.seq_gaps++;
        resync = true;
    }
    expect_seq = h->seq + 1u;
    if (resync) {
        /* Resume only at a frame start; everything before it is dropped. */
        if (h->frame_off == LINK_STREAM_NO_FRAME) return true;
        start = h->frame_off;
        resync = false;
        stream_stats.resyncs++;
        denise_buffers_stale = 1;   /* rows of the lost data were not drawn */
    }
    if (words > start)
        denise_ring_put(payload + start, words - start);
    stream_stats.chunks++;
    stream_stats.words += words - start;
    /* The slave packs whole messages, so a chunk with more queued behind it
     * is nearly, not exactly, full. */
    stream_last_full = words >= LINK_STREAM_PAYLOAD_WORDS * 7u / 8u;
    return true;
}
