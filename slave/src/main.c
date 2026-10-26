/*
 * frank-amiga — slave (RP2350A, U6): the Amiga's bus side.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Core 0 runs the emulator: 68000, Agnus (DMA slots, Copper, blitter),
 * CIAs, Paula's timing, disk DMA. Every display and audio event leaves
 * through the message ring (host_link.c). Core 1 is the link pump: it
 * streams the ring to the master and brings back input (link_pump.c).
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
#include "link_pump.h"
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
       FAULT_MMFAR, FAULT_BFAR, FAULT_SP, FAULT_PSR, FAULT_R0, FAULT_R1,
       FAULT_R2, FAULT_R3, FAULT_R12, FAULT_CORE, FAULT_STACK,
       FAULT_STACK_WORDS = 24, FAULT_WORDS = FAULT_STACK + FAULT_STACK_WORDS };
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
    printf("  core %lu r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\n  stack:",
           (unsigned long)f[FAULT_CORE], (unsigned long)f[FAULT_R0],
           (unsigned long)f[FAULT_R1], (unsigned long)f[FAULT_R2],
           (unsigned long)f[FAULT_R3], (unsigned long)f[FAULT_R12]);
    for (int i = 0; i < FAULT_STACK_WORDS; i++)
        printf(" %08lx", (unsigned long)f[FAULT_STACK + i]);
    printf("\n");
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
    f[FAULT_R0] = frame[0]; f[FAULT_R1] = frame[1]; f[FAULT_R2] = frame[2];
    f[FAULT_R3] = frame[3]; f[FAULT_R12] = frame[4];
    f[FAULT_CORE] = get_core_num();
    for (int i = 0; i < FAULT_STACK_WORDS; i++)
        f[FAULT_STACK + i] = frame[8 + i];
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

/* ── Input from the master, applied once per frame (upstream
 * src/usb_input.c usb_input_frame()) ── */
#define KEYQ 32u
static uint8_t key_q[KEYQ];
static uint32_t key_head, key_tail;
static int mouse_dx, mouse_dy;
static uint8_t mouse_buttons;

static int clamp_delta(int d) { return d > 127 ? 127 : d < -127 ? -127 : d; }

/* Pacing: at most one emulated frame ahead of the master's display, so
 * emulation runs at exactly the refresh rate and Paula's 960 samples per
 * frame match the DAC's 48 kHz. If the master stops refreshing (not up
 * yet, being reflashed), run free rather than stall. */
static uint32_t pace_base;
static bool pace_locked;

static void pace(void) {
    const uint32_t frames = host_frames_emulated;
    for (;;) {
        const uint64_t now = time_us_64();
        if (now - pump_refresh_us > 100000u) { pace_locked = false; return; }
        const uint32_t refreshes = pump_refreshes;
        if (!pace_locked) {             /* (re)lock: count from here */
            pace_locked = true;
            pace_base = frames - refreshes;
        }
        const int32_t ahead = (int32_t)(frames - (refreshes + pace_base));
        /* Behind (a heavy stretch): forget the deficit rather than sprint
         * to make it up, which would only produce audio faster than the
         * DAC plays it. */
        if (ahead < -2) pace_base = frames - refreshes;
        if (ahead <= 1) return;
        tight_loop_contents();
    }
}

void host_frame_paced(void) {
    pace();
    link_input_ev_t ev;
    while (pump_input_pop(&ev)) {
        switch (ev.type) {
        case LINK_EV_KEY:
            if (key_head - key_tail < KEYQ)
                key_q[key_head++ % KEYQ] = (uint8_t)((ev.a & 0x7F) | (ev.b ? 0x80 : 0));
            break;
        case LINK_EV_MOUSE:
            mouse_dx += (int8_t)ev.a;
            mouse_dy += (int8_t)ev.b;
            mouse_buttons = ev.c;
            break;
        case LINK_EV_RESET:
            hostResetRequested = 1;
            break;
        default:
            break;
        }
    }
    const int dx = clamp_delta(mouse_dx), dy = clamp_delta(mouse_dy);
    mouse_dx -= dx;
    mouse_dy -= dy;
    /* JOY0DAT: vertical counter in the high byte, horizontal in the low. */
    const uint8_t h = (uint8_t)((chipset.joy0dat & 0xFF) + dx);
    const uint8_t v = (uint8_t)((chipset.joy0dat >> 8) + dy);
    chipset.joy0dat = (uint16_t)(v << 8 | h);
    if (mouse_buttons & 1) CIAA.pra &= (uint8_t)~0x40; else CIAA.pra |= 0x40;
    if (mouse_buttons & 2) chipset.potinp &= (uint16_t)~0x0400; else chipset.potinp |= 0x0400;
    if (mouse_buttons & 4) chipset.potinp &= (uint16_t)~0x0100; else chipset.potinp |= 0x0100;
    /* One key per frame: the keyboard's serial handshake cannot take more. */
    if (key_tail != key_head) {
        const uint8_t e = key_q[key_tail++ % KEYQ];
        hostAmigaKey(e & 0x7F, e & 0x80);
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
            printf("emu: %.1f frames/s, link %lu req %lu chunks %lu kwords "
                   "%lu resyncs, audio drops %lu, pc %06lx\n",
                   (double)(f - last_frames) * 1e6 / (double)(now - next_report + 2000000u),
                   (unsigned long)pump_requests, (unsigned long)pump_chunks,
                   (unsigned long)(pump_words / 1000u), (unsigned long)pump_resyncs,
                   (unsigned long)hostAudioDrops,
                   (unsigned long)m68k_get_reg(NULL, M68K_REG_PC));
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
    const uint8_t *ext = (const uint8_t *)BOARD_EXT_ROM_FLASH_BASE;
    if (ext[0] == 0x11 && ext[1] == 0x14) {
        memory_set_ext_rom(ext);
        printf("ROM: extended ROM at flash 0x%08x\n", (unsigned)BOARD_EXT_ROM_FLASH_BASE);
    }

    multicore_launch_core1(pump_core1_entry);
    hostInit();
    cpu_init();
    ChipsetInit();
    for (int drive = 0; drive < 4; ++drive)
        floppyInit(drive);
    /* DF0: an ADF in flash if there is one. Otherwise an empty drive: every
     * track reads as a sync word and nothing else, so trackdisk times out
     * and Kickstart shows its insert-disk screen. */
    const uint8_t *adf = (const uint8_t *)BOARD_ADF_FLASH_BASE;
    if (*(const uint32_t *)adf != 0xFFFFFFFFu &&
        floppyMountADF(0, adf, FLOPPY_ADF_SIZE)) {
        df[0].hasDisk = 1;
        df[0].pra &= 0xFB;
        printf("DF0: ADF at flash 0x%08x inserted\n", (unsigned)BOARD_ADF_FLASH_BASE);
    } else if (df[0].mfmData) {
        memset(df[0].mfmData, 0, BOARD_MAP_FLOPPY_SIZE);
        df[0].mfmData[0] = 0x44;
        df[0].mfmData[1] = 0x89;
    }
    printf("Entering emulation loop\n");
    emulation_loop();
}
