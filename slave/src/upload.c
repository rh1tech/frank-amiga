/*
 * frank-amiga — slave: Kickstart, extended ROM and disks from the master.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The master reads them from its microSD and uploads them over the link
 * (link_proto.h LINK_OP_UPLOAD_*). Core 1 (the link pump) receives into
 * PSRAM; core 0, if the machine is running, is held at a frame boundary
 * meanwhile so it never reads a half-written ROM or disk.
 *
 * A record of what is in PSRAM (sizes and CRCs) survives a watchdog reset,
 * so BOOT is simply: record the upload, reboot, and start from it — a cold
 * start with nothing left over from the previous machine.
 */
#include "upload.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "board_config.h"
#include "host_link.h"
#include "link_proto.h"
#include "../omega/Floppy.h"

#define REC_MAGIC 0x55504c44u   /* "UPLD" */

typedef struct {
    uint32_t magic;
    uint32_t size[4];           /* by LINK_UP_* target */
    uint32_t crc[4];
    uint32_t check;             /* XOR of the above with the magic */
} upload_rec_t;

static volatile upload_rec_t *rec(void) {
    return (volatile upload_rec_t *)(0x15000000u + BOARD_MAP_UPLOAD_REC_OFFSET);
}

static uint32_t rec_check(const volatile upload_rec_t *r) {
    uint32_t x = r->magic;
    for (int i = 0; i < 4; i++) x ^= r->size[i] ^ (r->crc[i] * 2654435761u);
    return x;
}

static uint8_t *target_base(uint32_t target) {
    switch (target) {
    case LINK_UP_ROM: return (uint8_t *)(0x11000000u + BOARD_MAP_ROM_OFFSET);
    case LINK_UP_EXT: return (uint8_t *)(0x11000000u + BOARD_MAP_EXT_ROM_OFFSET);
    case LINK_UP_DF0: return (uint8_t *)(0x11000000u + BOARD_MAP_DF0_ADF_OFFSET);
    default: return NULL;
    }
}

static uint32_t target_max(uint32_t target) {
    return target == LINK_UP_DF0 ? FLOPPY_ADF_SIZE : 0x80000u;
}

/* ── Core 0 hold ───────────────────────────────────────────────────────── */
static volatile bool hold_req, held, running;
static volatile int insert_req = -1;      /* drive to (re)insert, or -1 */

void upload_core0_frame(void) {
    if (hold_req) {
        held = true;
        while (hold_req) tight_loop_contents();
        held = false;
    }
    static int insert_wait;
    if (insert_req >= 0) {
        /* Eject first and give the OS time to see the change. */
        if (df[0].hasDisk) {
            floppyInsert(0);          /* toggles: eject */
            insert_wait = 25;
        } else if (insert_wait > 0) {
            insert_wait--;
        } else {
            const volatile upload_rec_t *r = rec();
            if (r->size[LINK_UP_DF0] == FLOPPY_ADF_SIZE &&
                floppyMountADF(0, target_base(LINK_UP_DF0), FLOPPY_ADF_SIZE)) {
                floppyInsert(0);
                printf("upload: DF0 inserted\n");
            }
            insert_req = -1;
        }
    }
}

void upload_set_running(bool on) { running = on; }

static void hold_core0(bool on) {
    if (!running) return;
    hold_req = on;
    /* Core 0 parks at its next frame end. The master is not reading the
     * stream meanwhile, so drop what the ring holds or core 0 would block
     * on a full ring before it got there; the master resynchronises at a
     * frame start afterwards. */
    if (on) {
        while (!held) {
            uint32_t slot;
            const uint32_t n = host_ring_pending(&slot);
            if (n) host_ring_consume(n);
        }
        host_frame_marks_clear();   /* their positions may be a lap old */
    }
}

/* ── Core 1: the link side ─────────────────────────────────────────────── */
static uint32_t up_target, up_size;
static uint8_t bounce[LINK_UPLOAD_CHUNK] __attribute__((aligned(4)));

bool upload_handle(link_session_t *s, uint16_t op) {
    const link_hdr_t *h = link_rx_hdr(s);
    switch (op) {
    case LINK_OP_UPLOAD_BEGIN: {
        const bool ok = target_base(h->arg0) && h->arg1 && h->arg1 <= target_max(h->arg0);
        if (ok) {
            hold_core0(true);
            up_target = h->arg0;
            up_size = h->arg1;
            volatile upload_rec_t *r = rec();
            if (r->magic != REC_MAGIC || r->check != rec_check(r)) {
                memset((void *)r, 0, sizeof *r);
                r->magic = REC_MAGIC;
            }
            r->size[up_target] = 0;       /* invalid until END verifies */
            r->check = rec_check(r);
        }
        link_s_send_ctrl(s, LINK_OP_UPLOAD_BEGIN | 1, ok, 0, NULL, 0);
        return true;
    }
    case LINK_OP_UPLOAD_CHUNK: {
        const uint32_t off = h->arg0, n = h->arg1;
        bool ok = up_target && n && n <= LINK_UPLOAD_CHUNK && off + n <= up_size &&
                  !(n & 3u);
        if (ok) ok = link_s_bulk_recv(s, bounce, n);
        if (ok) memcpy(target_base(up_target) + off, bounce, n);
        link_s_send_ctrl(s, LINK_OP_UPLOAD_CHUNK | 1, ok, off, NULL, 0);
        return true;
    }
    case LINK_OP_UPLOAD_END: {
        uint32_t crc = 0;
        bool ok = false;
        if (up_target) {
            crc = link_crc32(target_base(up_target), up_size);
            ok = crc == h->arg0;
            volatile upload_rec_t *r = rec();
            r->size[up_target] = ok ? up_size : 0;
            r->crc[up_target] = crc;
            r->check = rec_check(r);
        }
        printf("upload: target %lu, %lu bytes, crc %08lx %s\n", (unsigned long)up_target,
               (unsigned long)up_size, (unsigned long)crc, ok ? "ok" : "MISMATCH");
        up_target = 0;
        link_s_send_ctrl(s, LINK_OP_UPLOAD_END | 1, crc, ok, NULL, 0);
        hold_core0(false);
        return true;
    }
    case LINK_OP_BOOT:
        link_s_send_ctrl(s, LINK_OP_BOOT | 1, 0, 0, NULL, 0);
        printf("upload: boot requested, restarting\n");
        sleep_ms(20);
        watchdog_reboot(0, 0, 0);
        for (;;) tight_loop_contents();
    case LINK_OP_INSERT:
        insert_req = (int)h->arg0;
        link_s_send_ctrl(s, LINK_OP_INSERT | 1, 0, 0, NULL, 0);
        return true;
    default:
        return false;
    }
}

/* ── Boot selection ────────────────────────────────────────────────────── */
static bool rec_valid(uint32_t target) {
    const volatile upload_rec_t *r = rec();
    return r->magic == REC_MAGIC && r->check == rec_check(r) && r->size[target] &&
           link_crc32(target_base(target), r->size[target]) == r->crc[target];
}

const uint8_t *upload_rom(uint32_t *size) {
    if (!rec_valid(LINK_UP_ROM)) return NULL;
    *size = rec()->size[LINK_UP_ROM];
    return target_base(LINK_UP_ROM);
}

const uint8_t *upload_ext_rom(void) {
    return rec_valid(LINK_UP_EXT) && rec()->size[LINK_UP_EXT] == 0x80000u
         ? target_base(LINK_UP_EXT) : NULL;
}

const uint8_t *upload_df0(void) {
    return rec_valid(LINK_UP_DF0) && rec()->size[LINK_UP_DF0] == FLOPPY_ADF_SIZE
         ? target_base(LINK_UP_DF0) : NULL;
}
