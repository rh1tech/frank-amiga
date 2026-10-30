/*
 * frank-amiga — slave core 1: the link pump.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Answers the master's control frames. Steady state is LINK_OP_STREAM_REQ:
 * apply the input events it carries (queued for core 0), then reply with
 * one chunk of the message ring (link_stream.h). Core 0 never touches the
 * link: it only fills the ring and drains the input queue once per frame.
 */
#include "link_pump.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"

#include "link_bus.h"
#include "link_pins.h"
#include "link_session.h"
#include "link_stream.h"
#include "host_link.h"
#include "HostRing.h"
#include "upload.h"
#include "../omega/Floppy.h"
#include "../omega/m68k.h"
#include "../omega/DMA.h"
#include "parallel.h"

#ifdef FRANK_PARALLEL
static bool par_in_cpu;                  /* inside m68k_execute() */
static bool par_cpu_step(void);
#endif

static link_t link;
static link_session_t sess;
static uint8_t ctrl_tx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint8_t ctrl_rx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint32_t chunk[LINK_STREAM_CHUNK_BYTES / 4];

volatile uint32_t pump_chunks, pump_words, pump_requests, pump_resyncs;
static uint32_t chunk_seq;

/* ── Input events for core 0 (single producer: this core) ── */
#define INPUT_Q 64u
static link_input_ev_t input_q[INPUT_Q];
static volatile uint32_t input_head, input_tail;

bool pump_input_pop(link_input_ev_t *ev) {
    const uint32_t t = input_tail;
    if (t == __atomic_load_n(&input_head, __ATOMIC_ACQUIRE))
        return false;
    *ev = input_q[t % INPUT_Q];
    __atomic_store_n(&input_tail, t + 1u, __ATOMIC_RELEASE);
    return true;
}

static void input_push(const link_input_ev_t *ev) {
    const uint32_t h = input_head;
    if (h - __atomic_load_n(&input_tail, __ATOMIC_ACQUIRE) >= INPUT_Q)
        return;                                 /* full: drop */
    input_q[h % INPUT_Q] = *ev;
    __atomic_store_n(&input_head, h + 1u, __ATOMIC_RELEASE);
}

static void reply_hello(void) {
    link_node_info_t info = { 0 };
    info.sys_clk_hz = clock_get_hz(clk_sys);
    info.proto_ver = LINK_PROTO_VER;
    info.psram_bytes = 8u << 20;
    info.uptime_ms = to_ms_since_boot(get_absolute_time());
    snprintf(info.build, sizeof info.build, "amiga slave %s", __TIME__);
    link_s_send_ctrl(&sess, LINK_OP_HELLO_ACK, 0, 0, &info, sizeof info);
}

/* Position (0..span) of ring position p relative to the tail, in words. */
static inline uint32_t ring_distance(uint32_t from, uint32_t to) {
    const uint32_t span = host_ring_span();
    return to >= from ? to - from : to + span - from;
}

/* Words of the message whose header is `h` (HostRing.h formats), or 0 for
 * a word that is not a valid header. */
static uint32_t msg_words(uint32_t h) {
    switch (h & 15u) {
    case MSG_BEGIN:    return 4;
    case MSG_HIRES:    return 1 + 2 * (((h >> MSG_RUN_SHIFT) & (MSG_RUN_MAX - 1)) + 1);
    case MSG_LORES:    return 1 + 3 * (((h >> MSG_RUN_SHIFT) & (MSG_RUN_MAX - 1)) + 1);
    case MSG_PALETTE:  return 2 + (h >> 23);
    case MSG_END:      return 2;
    case MSG_SPRITE:   return (h >> 28) & 1u ? 3 : 2;   /* attached */
    case MSG_ROW_END:  return 1;
    case MSG_COLOUR:   return 1 + (h >> 23);
    case MSG_ROW_SAME: return 1;
    case MSG_AUDIO:    return 1 + (h >> 23);
    case MSG_DPF:      return 1;
    case MSG_CLIP:     return 2;
    default:           return 0;
    }
}

volatile uint32_t pump_bad_headers;

/* Fill one chunk from the ring. Waits briefly for a fuller chunk: the
 * master asked, so it is waiting, but a nearly empty chunk wastes a round
 * trip. */
static void fill_chunk(bool resync) {
    link_stream_hdr_t *h = (link_stream_hdr_t *)chunk;
    uint32_t *payload = chunk + LINK_STREAM_HDR_WORDS;
    uint32_t slot;
    uint32_t avail = host_ring_pending(&slot);
    const uint64_t deadline = time_us_64() + 2000u;
    while (avail < LINK_STREAM_PAYLOAD_WORDS && time_us_64() < deadline) {
#ifdef FRANK_PARALLEL
        /* The CPU runs on this core: no waiting for a fuller chunk. */
        if (par_running)
            break;
#endif
        if (!host_events_drain())
            tight_loop_contents();
        avail = host_ring_pending(&slot);
    }
    /* After a loss the master wants to restart at a frame: drop everything
     * before the next frame start. */
    uint32_t mark;
    if (resync) {
        pump_resyncs++;
        while (host_frame_mark_peek(&mark)) {
            const uint32_t d = ring_distance(host_ring_tail(), mark);
            if (d <= avail) {
                host_ring_consume(d);
                avail = host_ring_pending(&slot);
                break;
            }
            break;
        }
    }
    /* Whole messages only: the master's renderer reads a message as soon as
     * it is published, so a chunk must never end inside one. */
    const uint32_t *ring = host_ring_base();
    const uint32_t ring_words = host_ring_words();
    const uint32_t limit = avail < LINK_STREAM_PAYLOAD_WORDS ? avail : LINK_STREAM_PAYLOAD_WORDS;
    uint32_t n = 0;
    if (limit && !msg_words(ring[slot])) {
        /* Not at a message start: the stream is out of step (it should
         * never be). Drop everything queued and every mark, and skip a
         * sequence number so the master resynchronises at the next fresh
         * frame start. */
        pump_bad_headers++;
        host_ring_consume(avail);
        host_frame_marks_clear();
        chunk_seq++;
        avail = 0;
    }
    const uint32_t limit2 = avail < LINK_STREAM_PAYLOAD_WORDS ? avail : LINK_STREAM_PAYLOAD_WORDS;
    {
        uint32_t s = slot;
        while (n < limit2) {
            const uint32_t len = msg_words(ring[s]);
            if (!len) { pump_bad_headers++; break; }
            if (n + len > limit2) break;
            n += len;
            s += len;
            if (s >= ring_words) s -= ring_words;
        }
    }
    /* First frame start inside this chunk, for the master's resync. Marks
     * the ring has already passed are dropped. */
    uint16_t frame_off = LINK_STREAM_NO_FRAME;
    const uint32_t tail = host_ring_tail();
    while (host_frame_mark_peek(&mark)) {
        const uint32_t d = ring_distance(tail, mark);
        if (d >= host_ring_span() / 2u) { host_frame_mark_pop(); continue; }  /* stale */
        if (d < n) { frame_off = (uint16_t)d; host_frame_mark_pop(); }
        break;
    }
    uint32_t check = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t w = ring[slot];
        payload[i] = w;
        check ^= w;
        if (++slot == ring_words) slot = 0;
    }
    host_ring_consume(n);
    h->magic = LINK_STREAM_MAGIC;
    h->seq = ++chunk_seq;
    int td, tt;
    h->words = (uint16_t)(n | (floppyDirty[0] | floppyDirty[1] ? LINK_STREAM_WORDS_DIRTY : 0u) |
                          (floppyTrackWanted(&td, &tt) ? LINK_STREAM_WORDS_TRACK : 0u));
    h->frame_off = frame_off;
    h->check = check ^ chunk_seq;
    pump_words += n;
    pump_chunks++;
}

/* One step: answers the master's control frame if it has rung, else turns
 * queued records into messages. */
static void __not_in_flash_func(pump_once)(void) {
    link_db_set(sess.link, false);      /* idle: DB_SM low (see link_m_resync) */
    /* Until the master rings, build messages from core 0's records. */
    if (!link_db_get(sess.link)) {
        host_events_drain();
        return;
    }
    const uint16_t op = link_s_wait_ctrl(&sess, 1000);
    if (!op) return;
    const link_hdr_t *hdr = link_rx_hdr(&sess);
    switch (op) {
    case LINK_OP_HELLO:
        reply_hello();
        break;
    case LINK_OP_PING:
        link_s_send_ctrl(&sess, LINK_OP_PONG, hdr->arg0, hdr->arg1, NULL, 0);
        break;
    case LINK_OP_STREAM_REQ: {
        pump_requests++;
        const bool resync = hdr->arg1 & LINK_STREAM_FLAG_RESYNC;
        const link_input_batch_t *in =
            (const link_input_batch_t *)(sess.ctrl_rx + sizeof(link_hdr_t));
        const uint32_t count = in->count < 24u ? in->count : 24u;
        for (uint32_t i = 0; i < count; i++)
            input_push(&in->ev[i]);
        fill_chunk(resync);
        link_s_bulk_send(&sess, chunk, LINK_STREAM_CHUNK_BYTES);
        break;
    }
    default:
        upload_handle(&sess, op);   /* Kickstart / disk uploads, boot */
        break;
    }
}

void __not_in_flash_func(pump_service)(void) { pump_once(); }

#ifdef FRANK_PARALLEL
/* ── The 68000 on this core (parallel.h) ── */
volatile uint32_t par_cpu_slot, par_dma_slot;
volatile bool par_running;
volatile int par_stop_req, par_cpu_parked;
/* The CPU's time: whole slots run, and an odd cycle carried over (this
 * core; m68kcpu.c reads them). Kept in slots, not cycles: a 32-bit cycle
 * count halved wraps at 2^31 slots (~10 minutes), half way round the
 * chipset's slot count, and the CPU then waited for ever. */
uint32_t par_cpu_base, par_cpu_odd;

static inline bool par_on_cpu_core(void) { return get_core_num() == 1u; }

/* While the CPU waits, the link keeps going. */
static void __not_in_flash_func(par_wait_step)(void) {
    pump_once();
}

void __not_in_flash_func(parSync)(void) {
    if (!par_running || !par_on_cpu_core())
        return;
    const uint32_t t = par_cpu_base + ((par_cpu_odd + (uint32_t)m68k_cycles_run()) >> PAR_CPU_SHIFT);
    __dmb();
    if ((int32_t)(t - par_cpu_slot) > 0)
        par_cpu_slot = t;
    if (par_dma_slot == t && !par_stop_req) {
        __dmb();
        return;
    }
#ifdef FRANK_PROF
    const uint32_t c0 = PROF_CYC;
    prof_c1_syncs++;
#endif
    par_cpu_parked = 2;
    while (par_dma_slot != t || par_stop_req)
        par_wait_step();
    par_cpu_parked = 0;
#ifdef FRANK_PROF
    prof_c1_sync += PROF_CYC - c0;
#endif
    __dmb();
}

/* Register accesses that need not wait for the chipset. Core 0 publishes,
 * after each run of slots and before the CPU can see it got there, what the
 * blitter is doing (par_publish_blit()); a synced CPU write clears it
 * (parWrote()), as the write may start a blit or stop its DMA:
 *  - DMACONR read while a computed blit waits out its time (state 3) and
 *    the CPU is still before its end: BBUSY stays set until then, and the
 *    other bits change only with DMACON writes (the CPU's own, synced; or
 *    the Copper's, which may be seen up to PAR_LEAD_SLOTS late, as chip
 *    RAM is). Blitter-wait loops poll it (State of the Art's chunky part:
 *    ~850 a frame), each poll a handoff between the cores;
 *  - writes to the blitter's registers but BLTSIZE (BLTCON0..BLTDPTL,
 *    BLTxMOD) while it is idle or only waiting: they are plain stores that
 *    no blit reads before the next BLTSIZE, which syncs (~1900 a frame). */
volatile uint32_t par_blit_until;        /* CPU slot BBUSY stays set before */
volatile bool par_blit_wait, par_blit_free;

/* The beam where the chipset's next slot runs, published with the blitter
 * state: VPOSR/VHPOSR at the CPU's own slot follow from it, so a beam-
 * polling loop need not hand off to the chipset at every read (Mind Riot's
 * "m" part: ~4800 a frame, 42 fps). An odd sequence count marks a snapshot
 * being written; info 0, none usable. */
static volatile uint32_t par_beam_seq, par_beam_slot, par_beam_pos,
                         par_beam_info, par_beam_hi;

void par_publish_blit(uint32_t slot) {
    const int rem = dmaBlitWaitRemaining();
    par_blit_until = slot + (uint32_t)rem;
    par_blit_wait = rem > 0;
    par_blit_free = dmaBlitterFree() != 0;
    uint32_t pos, info, hi;
    const int ok = dmaBeamNext(&pos, &info, &hi);
    par_beam_seq++;
    __dmb();
    par_beam_slot = slot;
    par_beam_pos = pos;
    par_beam_info = ok ? info : 0u;
    par_beam_hi = hi;
    __dmb();
    par_beam_seq++;
}

/* A synced register read sees the chipset after its slot t - 1 ran: the beam
 * of that slot (dma_run() leaves VHPOSR there, VPOSR at its line). Worked out
 * here from the snapshot, unless that slot lies in another frame or before
 * the snapshot's line. */
int __not_in_flash_func(parBeamRead)(uint32_t *vposr, uint32_t *vhposr) {
    if (!par_running || !par_on_cpu_core())
        return 0;
    const uint32_t s = par_beam_seq;
    __dmb();
    if (s & 1u)
        return 0;
    const uint32_t slot = par_beam_slot, pos = par_beam_pos;
    const uint32_t info = par_beam_info, hi = par_beam_hi;
    __dmb();
    if (par_beam_seq != s || !info)
        return 0;
    const uint32_t t = par_cpu_base + ((par_cpu_odd + (uint32_t)m68k_cycles_run()) >> PAR_CPU_SHIFT);
    const int32_t d = (int32_t)(t - 1u - slot);
    int v = (int)(pos >> 16), h = (int)(pos & 0xFFFFu);
    const int last = (int)(info >> 16), vend = (int)(info & 0xFFFFu);
    if (d < -1 || d > 1024)
        return 0;
    if (d < 0) {
        if (h == 0)
            return 0;
        h--;
    } else {
        h += d;
        while (h > last) {
            h -= last + 1;
            if (++v >= vend)
                return 0;
        }
    }
    __dmb();
    if ((int32_t)(t - par_cpu_slot) > 0)
        par_cpu_slot = t;
    *vposr = hi | (uint32_t)(v >> 8);
    *vhposr = (uint32_t)((v << 8 | h) & 0xFFFF);
    return 1;
}

void __not_in_flash_func(parWrote)(void) {
    if (!par_running || !par_on_cpu_core())
        return;
    par_blit_wait = false;
    par_blit_free = false;
    par_beam_info = 0;      /* a write may move the beam (VPOSW's LOF) */
    __dmb();
}

static inline bool par_blit_reg(unsigned r) {
    return (r >= 0x20u && r <= 0x2Bu) || (r >= 0x30u && r <= 0x33u);
}

int __not_in_flash_func(parSkipSync)(int write, unsigned reg) {
    if (!par_running || !par_on_cpu_core())
        return 0;
    if (write) {
        const unsigned hi = reg >> 8;
        if (!par_blit_free || !par_blit_reg(reg & 0xFFu) || (hi && !par_blit_reg(hi)))
            return 0;
    } else if (reg != 1u || !par_blit_wait) {
        return 0;
    }
    const uint32_t t = par_cpu_base + ((par_cpu_odd + (uint32_t)m68k_cycles_run()) >> PAR_CPU_SHIFT);
    if (!write && (int32_t)(par_blit_until - t) <= 1)
        return 0;
    /* The CPU is at t: the chipset may run up to it meanwhile. */
    __dmb();
    if ((int32_t)(t - par_cpu_slot) > 0)
        par_cpu_slot = t;
    return 1;
}

/* Runs the CPU for a little while, at most PAR_LEAD_SLOTS ahead of the
 * chipset; false if it had to wait. */
static bool __not_in_flash_func(par_cpu_step)(void) {
    if (par_stop_req) {
        par_cpu_parked = 1;
        while (par_stop_req)
            par_wait_step();
        par_cpu_parked = 0;
        __dmb();
    }
    const int32_t room =
        (int32_t)(par_dma_slot + PAR_LEAD_SLOTS - par_cpu_base);
    if (room <= 0)
        return false;
    int budget = room << PAR_CPU_SHIFT;
    if (budget > PAR_CPU_CHUNK)
        budget = PAR_CPU_CHUNK;
    par_in_cpu = true;
#ifdef FRANK_PROF
    const uint32_t c0 = PROF_CYC;
#endif
    {
        const uint32_t c = par_cpu_odd + (uint32_t)m68k_execute(budget);
        par_cpu_base += c >> PAR_CPU_SHIFT;
        par_cpu_odd = c & ((1u << PAR_CPU_SHIFT) - 1u);
    }
#ifdef FRANK_PROF
    prof_c1_cpu += PROF_CYC - c0;
#endif
    par_in_cpu = false;
    __dmb();
    par_cpu_slot = par_cpu_base;
    return true;
}

bool par_stop(bool between) {
    par_stop_req = 1;
    __dmb();
    for (;;) {
        const int parked = par_cpu_parked;
        if (parked == 1 || (parked == 2 && !between))
            break;
        if (parked == 2) {          /* inside an instruction: not now */
            par_stop_req = 0;
            __dmb();
            return false;
        }
        tight_loop_contents();
    }
    __dmb();
    return true;
}

void par_resume(void) {
    /* The machine may have changed under the stopped CPU (a savestate, a
     * reset): no shortcuts until the chipset publishes again. */
    par_blit_wait = false;
    par_blit_free = false;
    __dmb();
    par_stop_req = 0;
}
#endif

static void __not_in_flash_func(pump_loop)(void) {
    for (;;) {
#ifdef FRANK_PARALLEL
        if (par_running) {
            /* The CPU first: the chipset waits for it. The master's
             * requests are answered at once; messages are built only while
             * the CPU has to wait (its lead used up), a few at a time. If
             * they pile up, the chipset stops at the full queue, the CPU
             * soon after, and this core catches up. */
            const bool ran = par_cpu_step();
#ifdef FRANK_PROF
            const uint32_t c0 = PROF_CYC;
#endif
            if (link_db_get(sess.link))
                pump_once();
            else if (!ran)
                host_events_drain_batch(32);
#ifdef FRANK_PROF
            if (ran) prof_c1_pump += PROF_CYC - c0;
            else prof_c1_lead += PROF_CYC - c0;
#endif
            continue;
        }
#endif
        pump_once();
    }
}

/* Display refreshes counted from the master's FS toggles (both edges). */
volatile uint32_t pump_refreshes;
volatile uint64_t pump_refresh_us;

static void fs_irq(uint gpio, uint32_t events) {
    if (gpio == S_LINK_FS) {
        pump_refreshes++;
        pump_refresh_us = time_us_64();
    }
}

#ifdef FRANK_PROF
volatile uint32_t prof_c1_cpu, prof_c1_sync, prof_c1_syncs, prof_c1_lead, prof_c1_pump;
#endif

void pump_core1_entry(void) {
#ifdef FRANK_PROF
    prof_dwt_on();
#endif
    link_init(&link, LINK_PIO_SLAVE, S_LINK_B_DATA_BASE, S_LINK_A_DATA_BASE,
              S_LINK_DB_OUT, S_LINK_DB_IN, S_LINK_FS, false);
    link_set_bulk_clkdiv(&link, 1.0f);
    sess.link = &link;
    sess.ctrl_tx = ctrl_tx;
    sess.ctrl_rx = ctrl_rx;
    sess.handshake_timeout_us = 20000;
    /* FS edges are counted here, on core 1, so core 0 never takes an IRQ. */
    gpio_set_irq_enabled_with_callback(S_LINK_FS, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, fs_irq);
    pump_loop();
}
