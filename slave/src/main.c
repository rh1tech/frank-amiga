/*
 * frank-amiga — slave (RP2350A, U6): the Amiga's bus side.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Core 0 runs the emulator: 68000, Agnus (DMA slots, Copper, blitter),
 * CIAs, Paula's timing, disk DMA. Every display and audio event leaves
 * through the message ring (host_link.c). Core 1 consumes the ring: in M2
 * it only measures the stream; in M3 it becomes the link pump.
 *
 * Console on UART1 (J4).
 */
#include <stdio.h>
#include <string.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/scb.h"
#include "hardware/regs/addressmap.h"

#include "clocks.h"
#include "link_pins.h"
#include "psram_init.h"

#include "Memory.h"
#include "Host.h"
#include "HostRing.h"
#include "host_link.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/CPU.h"
#include "../omega/DMA.h"
#include "../omega/Floppy.h"
#include "../omega/VideoStandard.h"
#include "m68k.h"

/* 68000 cycles per DMA slot (colour clock): 2 on a real A500. The CPU runs
 * in slices of OMEGA_CPU_SLICE_SLOTS slots (upstream main.c). */
#ifndef OMEGA_CPU_SLICE_SLOTS
#define OMEGA_CPU_SLICE_SLOTS 8
#endif
#ifndef OMEGA_CPU_CYCLES_PER_SLOT
#define OMEGA_CPU_CYCLES_PER_SLOT 2
#endif
#define DMA_CPU_BATCH \
    (((200 + OMEGA_CPU_SLICE_SLOTS - 1) / OMEGA_CPU_SLICE_SLOTS) * \
     OMEGA_CPU_SLICE_SLOTS)

/* ── Hard faults: recorded in PSRAM (uncached) so they survive a reset ── */
#define FAULT_MAGIC 0xfa017badu
enum { FAULT_MAGIC_W, FAULT_PC, FAULT_LR, FAULT_CFSR, FAULT_HFSR,
       FAULT_MMFAR, FAULT_BFAR, FAULT_SP, FAULT_PSR, FAULT_WORDS };
static volatile uint32_t *fault_record(void) {
    return (volatile uint32_t *)(0x15000000u + BOARD_MAP_BOOT_STATE_OFFSET + 0x10u);
}

static void report_previous_fault(void) {
    volatile uint32_t *f = fault_record();
    if (f[FAULT_MAGIC_W] != FAULT_MAGIC) return;
    printf("Previous HARDFAULT: pc=%08lx lr=%08lx cfsr=%08lx hfsr=%08lx "
           "mmfar=%08lx bfar=%08lx sp=%08lx psr=%08lx\n",
           (unsigned long)f[FAULT_PC], (unsigned long)f[FAULT_LR],
           (unsigned long)f[FAULT_CFSR], (unsigned long)f[FAULT_HFSR],
           (unsigned long)f[FAULT_MMFAR], (unsigned long)f[FAULT_BFAR],
           (unsigned long)f[FAULT_SP], (unsigned long)f[FAULT_PSR]);
    f[FAULT_MAGIC_W] = 0;
}

void __attribute__((noreturn, used)) hardfault_report(uint32_t *frame) {
    volatile uint32_t *f = fault_record();
    f[FAULT_PC] = frame[6];
    f[FAULT_LR] = frame[5];
    f[FAULT_CFSR] = scb_hw->cfsr;
    f[FAULT_HFSR] = scb_hw->hfsr;
    f[FAULT_MMFAR] = scb_hw->mmfar;
    f[FAULT_BFAR] = scb_hw->bfar;
    f[FAULT_SP] = (uint32_t)(uintptr_t)frame;
    f[FAULT_PSR] = frame[7];
    f[FAULT_MAGIC_W] = FAULT_MAGIC;
    printf("HARDFAULT: pc=%08lx lr=%08lx cfsr=%08lx hfsr=%08lx\n",
           (unsigned long)frame[6], (unsigned long)frame[5],
           (unsigned long)scb_hw->cfsr, (unsigned long)scb_hw->hfsr);
    for (;;) tight_loop_contents();
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b hardfault_report\n");
}

/* ── Pacing: M2 runs unthrottled ── */
void host_frame_paced(void) {}

/* ── Core 1 (M2): drain the ring, measure the stream per frame ── */
static uint32_t msg_words(const uint32_t *ring, uint32_t ring_words,
                          uint32_t slot, uint32_t avail) {
    const uint32_t h = ring[slot];
    const uint32_t type = h & 15u;
    switch (type) {
    case MSG_BEGIN:    return 4;
    case MSG_HIRES:    return 1 + 2 * (((h >> MSG_RUN_SHIFT) & (MSG_RUN_MAX - 1)) + 1);
    case MSG_LORES:    return 1 + 3 * (((h >> MSG_RUN_SHIFT) & (MSG_RUN_MAX - 1)) + 1);
    case MSG_PALETTE:  return 17;
    case MSG_END:      return 2;
    case MSG_SPRITE:   return (h >> 28) & 1u ? 3 : 2;   /* attached: extra bit 5 */
    case MSG_ROW_END:  return 1;
    case MSG_COLOUR:   return 2;
    case MSG_ROW_SAME: return 1;
    case MSG_AUDIO:    return 1 + (h >> 23);
    default:           return 0;                        /* corrupt */
    }
    (void)ring_words; (void)avail;
}

static volatile uint32_t c1_frames, c1_words_last, c1_audio_words_last, c1_bad;

static void core1_stats(void) {
    const uint32_t *ring = host_ring_base();
    const uint32_t ring_words = host_ring_words();
    uint32_t words = 0, audio_words = 0;
    for (;;) {
        uint32_t slot;
        uint32_t avail = host_ring_pending(&slot);
        if (!avail) { __wfe(); continue; }
        while (avail) {
            const uint32_t n = msg_words(ring, ring_words, slot, avail);
            if (!n || n > avail) { c1_bad++; n ? (void)0 : (void)0; }
            const uint32_t take = (!n || n > avail) ? avail : n;
            const uint32_t type = ring[slot] & 15u;
            if (type == MSG_AUDIO) audio_words += take;
            words += take;
            if (type == MSG_END) {
                c1_words_last = words;
                c1_audio_words_last = audio_words;
                words = audio_words = 0;
                c1_frames++;
            }
            host_ring_consume(take);
            avail -= take;
            slot += take;
            if (slot >= ring_words) slot -= ring_words;
        }
    }
}

/* ── Emulation loop (upstream main.c) ── */
static void __not_in_flash_func(emulation_loop)(void) {
    static int cpu_cycle_balance;
    uint64_t next_report = time_us_64() + 2000000u;
    uint32_t last_frames = 0;
    for (;;) {
        for (int i = 0; i < DMA_CPU_BATCH; i += OMEGA_CPU_SLICE_SLOTS) {
            dma_run(OMEGA_CPU_SLICE_SLOTS);
            cpu_cycle_balance += OMEGA_CPU_CYCLES_PER_SLOT * OMEGA_CPU_SLICE_SLOTS;
            if (cpu_cycle_balance > 0)
                cpu_cycle_balance -= m68k_execute(cpu_cycle_balance);
        }
        if (hostResetRequested) {
            hostResetRequested = 0;
            cpu_keyboard_reset();
        }
        const uint64_t now = time_us_64();
        if (now >= next_report) {
            const uint32_t f = host_frames_emulated;
            printf("emu: %.1f frames/s, stream %lu words/frame (audio %lu), "
                   "ring bad %lu, pc %06lx\n",
                   (double)(f - last_frames) * 1e6 / (double)(now - next_report + 2000000u),
                   (unsigned long)c1_words_last, (unsigned long)c1_audio_words_last,
                   (unsigned long)c1_bad, (unsigned long)m68k_get_reg(NULL, M68K_REG_PC));
            last_frames = f;
            next_report = now + 2000000u;
        }
    }
}

int main(void) {
    const bool clk_ok = frank_clocks_init();
    stdio_init_all();
    printf("\n\nfrank-amiga slave (%s %s)\n", __DATE__, __TIME__);
    if (!clk_ok) printf("WARNING: clock setup failed\n");
    frank_clocks_report();

    psram_init_with_freq(S_PSRAM_CS_PIN, FRANK_PSRAM_MAX_MHZ);
    report_previous_fault();
    printf("Video: %s (%d lines)\n", OMEGA_VIDEO_NAME, OMEGA_VIDEO_FRAME_LINES);

    memory_init();
    if (!memory_set_rom((const uint8_t *)BOARD_ROM_FLASH_BASE, BOARD_ROM_SIZE))
        printf("ROM: none at flash 0x%08x\n", (unsigned)BOARD_ROM_FLASH_BASE);

    multicore_launch_core1(core1_stats);
    hostInit();
    cpu_init();
    ChipsetInit();
    for (int drive = 0; drive < 4; ++drive)
        floppyInit(drive);
    /* An empty DF0: every track reads as a sync word and nothing else, so
     * trackdisk times out and Kickstart shows its insert-disk screen. */
    if (df[0].mfmData) {
        memset(df[0].mfmData, 0, BOARD_MAP_FLOPPY_SIZE);
        df[0].mfmData[0] = 0x44;
        df[0].mfmData[1] = 0x89;
    }
    printf("Entering emulation loop\n");
    emulation_loop();
}
