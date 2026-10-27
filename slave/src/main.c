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
#include "upload.h"
#include "savestate.h"
extern volatile uint32_t host_dpf_rows;
#include "Collision.h"
#include "DisplayLayout.h"
#include "DisplayLayout.h"
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

/* Where chip RAM is in use this frame, for the SRAM bank promotion. */
static void sample_banks(void) {
    uint32_t a[10], w[10];
    int n = 0;
    a[n] = m68k_get_reg(NULL, M68K_REG_PC); w[n++] = 8;
    const int planes = (chipset.bplcon0 >> 12) & 7;
    const uint32_t *bpl = &chipset.bpl1pt;
    for (int i = 0; i < planes && i < 6; ++i) { a[n] = bpl[i]; w[n++] = 4; }
    a[n] = chipset.cop1lc; w[n++] = 2;
    a[n] = chipset.bltdpt; w[n++] = 4;
    chip_bank_sample(a, w, n);
}

/* Kickstart's Alert (ROM 0xFC30A0..): the first one is recorded with its
 * number (D7) and the caller's stack, for the console. */
static volatile uint32_t alert_seen, alert_d7, alert_stack[8];
void frankTraceException(unsigned vector, unsigned pc) {
    pc &= 0xFFFFFFu;
    if (alert_seen || vector != 8 || pc < 0xFC3000u || pc >= 0xFC3100u) return;
    alert_d7 = m68k_get_reg(NULL, M68K_REG_D7);
    const uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    for (int i = 0; i < 8; i++) alert_stack[i] = m68k_read_memory_32(sp + 4u * (uint32_t)i);
    alert_seen = 1;
}

#ifdef FRANK_PC_RING
/* Debug: the last 256 instruction PCs, frozen when the CPU first enters
 * Kickstart 1.3's LED-flashing dead end (0xFC3000..). */
volatile uint32_t pc_ring[256], pc_ring_pos, pc_ring_frozen;
void __not_in_flash_func(frankPcRing)(unsigned pc) {
    if (pc_ring_frozen) return;
    pc &= 0xFFFFFFu;
    pc_ring[pc_ring_pos++ & 255u] = pc;
    if (pc >= 0xFC3000u && pc < 0xFC3100u) pc_ring_frozen = 1;
}

#endif

/* Joysticks (LINK_EV_JOY bits) by port. */
static uint8_t joy_bits[2];
static bool joy0_is_stick;

/* JOYxDAT of a joystick: right = bit 1, left = bit 9, bit 0 = down ^ right,
 * bit 8 = up ^ left. */
static uint16_t joy_word(uint8_t b) {
    const unsigned r = !!(b & LINK_JOY_RIGHT), l = !!(b & LINK_JOY_LEFT);
    const unsigned d = !!(b & LINK_JOY_DOWN), u = !!(b & LINK_JOY_UP);
    return (uint16_t)(r << 1 | (d ^ r) | l << 9 | (u ^ l) << 8);
}

void host_frame_paced(void) {
    pace();
    sample_banks();
    upload_core0_frame();
    savestate_core0_frame();
    link_input_ev_t ev;
    while (pump_input_pop(&ev)) {
        switch (ev.type) {
        case LINK_EV_KEY:
            if (key_head - key_tail < KEYQ)
                key_q[key_head++ % KEYQ] = (uint8_t)((ev.a & 0x7F) | (ev.b ? 0x80 : 0));
            break;
        case LINK_EV_MOUSE:
            joy0_is_stick = false;
            mouse_dx += (int8_t)ev.a;
            mouse_dy += (int8_t)ev.b;
            mouse_buttons = ev.c;
            break;
        case LINK_EV_JOY:
            if (ev.a < 2) {
                joy_bits[ev.a] = ev.b;
                if (ev.a == 0) joy0_is_stick = true;
            }
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
    /* JOY0DAT: vertical counter in the high byte, horizontal in the low
     * (a mouse), or a joystick's directions once port 0 has one. */
    if (joy0_is_stick) {
        chipset.joy0dat = joy_word(joy_bits[0]);
    } else {
        const uint8_t h = (uint8_t)((chipset.joy0dat & 0xFF) + dx);
        const uint8_t v = (uint8_t)((chipset.joy0dat >> 8) + dy);
        chipset.joy0dat = (uint16_t)(v << 8 | h);
    }
    chipset.joy1dat = joy_word(joy_bits[1]);
    const bool fire0 = (mouse_buttons & 1) || (joy_bits[0] & LINK_JOY_FIRE);
    const bool fire1 = joy_bits[1] & LINK_JOY_FIRE;
    if (fire0) CIAA.pra &= (uint8_t)~0x40; else CIAA.pra |= 0x40;
    if (fire1) CIAA.pra &= (uint8_t)~0x80; else CIAA.pra |= 0x80;
    /* Second buttons: POTGOR DATLY (port 0) / DATRY (port 1); third: DATLX. */
    if ((mouse_buttons & 2) || (joy_bits[0] & LINK_JOY_FIRE2)) chipset.potinp &= (uint16_t)~0x0400; else chipset.potinp |= 0x0400;
    if (mouse_buttons & 4) chipset.potinp &= (uint16_t)~0x0100; else chipset.potinp |= 0x0100;
    if (joy_bits[1] & LINK_JOY_FIRE2) chipset.potinp &= (uint16_t)~0x4000; else chipset.potinp |= 0x4000;
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
                   "%lu resyncs, audio drops %lu, bank swaps %lu, bplcon0 %04x, dpf rows %lu, clx %d, line0 %d, pc %06lx\n",
                   (double)(f - last_frames) * 1e6 / (double)(now - next_report + 2000000u),
                   (unsigned long)pump_requests, (unsigned long)pump_chunks,
                   (unsigned long)(pump_words / 1000u), (unsigned long)pump_resyncs,
                   (unsigned long)hostAudioDrops, (unsigned long)chip_bank_swaps,
                   chipset.bplcon0, (unsigned long)host_dpf_rows, clxActive != 0, omegaDirectFirstLine, (unsigned long)m68k_get_reg(NULL, M68K_REG_PC));
            next_report = now + 2000000u;
            if (alert_seen == 1) {
                alert_seen = 2;
                printf("ALERT %08lx, stack %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                       (unsigned long)alert_d7, (unsigned long)alert_stack[0], (unsigned long)alert_stack[1],
                       (unsigned long)alert_stack[2], (unsigned long)alert_stack[3], (unsigned long)alert_stack[4],
                       (unsigned long)alert_stack[5], (unsigned long)alert_stack[6], (unsigned long)alert_stack[7]);
            }
#ifdef DMA_SLOT_STATS
            extern uint32_t dma_stat_slow, dma_stat_batched, dma_stat_skipped;
            printf("slots/frame: slow %lu batched %lu skipped %lu\n",
                   (unsigned long)(dma_stat_slow / (f - last_frames + 1)),
                   (unsigned long)(dma_stat_batched / (f - last_frames + 1)),
                   (unsigned long)(dma_stat_skipped / (f - last_frames + 1)));
            dma_stat_slow = dma_stat_batched = dma_stat_skipped = 0;
#endif
            last_frames = f;
#ifdef CHIP_BANK_PROFILE
            printf("banks:");
            for (uint32_t b = 0; b < CHIP_BANKS; b++) {
                printf(" %lu", (unsigned long)(chip_bank_hits[b] / 1000u));
                chip_bank_hits[b] = 0;
            }
            printf("\n");
#endif
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

    {   /* The machine's RAM, as the master booted us (an A500's by default). */
        const uint32_t bf = upload_boot_flags();
        static const uint32_t chip[4] = { 0x80000u, 0x100000u, 0x200000u, 0x80000u };
        static const uint32_t slow[4] = { 0x80000u, 0u, 0x100000u, 0x80000u };
        memory_configure(chip[(bf >> LINK_BOOT_CHIP_SHIFT) & 3u],
                         slow[(bf >> LINK_BOOT_SLOW_SHIFT) & 3u]);
    }
    memory_init();
    multicore_launch_core1(pump_core1_entry);

    /* The machine's ROM, extended ROM and DF0 come from the master (it
     * uploads them from its microSD and reboots us with LINK_OP_BOOT). With
     * no upload, wait for the master a while, then fall back to images
     * written to our own flash (bench development). */
    uint32_t rom_size = 0;
    const uint8_t *rom = upload_rom(&rom_size);
    for (int i = 0; !rom && i < 40; i++) {
        sleep_ms(100);                  /* the master may upload and BOOT */
    }
    const uint8_t *ext = NULL, *adf = NULL, *adf1 = NULL;
    bool df1_on = false;
    if (rom) {
        ext = upload_ext_rom();
        const uint32_t flags = upload_boot_flags();
        if (flags & LINK_BOOT_DF0_DISK) adf = upload_df(0);
        if (flags & LINK_BOOT_DF1_DISK) adf1 = upload_df(1);
        df1_on = (flags & LINK_BOOT_DF1) != 0;
        printf("ROM: uploaded by the master (%lu KB)%s%s%s\n", (unsigned long)(rom_size / 1024u),
               ext ? ", extended ROM" : "", adf ? ", DF0 disk" : "",
               adf1 ? ", DF1 disk" : df1_on ? ", DF1 empty" : "");
    } else {
        rom = (const uint8_t *)BOARD_ROM_FLASH_BASE;
        rom_size = BOARD_ROM_SIZE;
        if (((const uint8_t *)BOARD_EXT_ROM_FLASH_BASE)[0] == 0x11)
            ext = (const uint8_t *)BOARD_EXT_ROM_FLASH_BASE;
        if (*(const uint32_t *)BOARD_ADF_FLASH_BASE != 0xFFFFFFFFu)
            adf = (const uint8_t *)BOARD_ADF_FLASH_BASE;
        printf("ROM: nothing uploaded, using flash images\n");
    }
    if (!memory_set_rom(rom, rom_size))
        printf("ROM: not a Kickstart image\n");
    if (ext) memory_set_ext_rom(ext);

    hostInit();
    cpu_init();
    ChipsetInit();
    for (int drive = 0; drive < 4; ++drive)
        floppyInit(drive);
    /* DF0: the disk if there is one. Otherwise an empty drive: every track
     * reads as a sync word and nothing else, so trackdisk times out and
     * Kickstart shows its insert-disk screen. */
    if (adf && floppyMountADF(0, adf, FLOPPY_ADF_SIZE)) {
        df[0].hasDisk = 1;
        df[0].pra &= 0xFB;
        printf("DF0: disk inserted\n");
    } else if (df[0].mfmData) {
        memset(df[0].mfmData, 0, BOARD_MAP_FLOPPY_SIZE);
        df[0].mfmData[0] = 0x44;
        df[0].mfmData[1] = 0x89;
    }
    if (df1_on) {
        floppyConnect(1);
        /* As DF0 above: floppyInsert() waits for the drive's ID mode. */
        if (adf1 && floppyMountADF(1, adf1, FLOPPY_ADF_SIZE)) {
            df[1].hasDisk = 1;
            df[1].pra &= 0xFB;
            printf("DF1: disk inserted\n");
        }
    }
    upload_set_running(true);
    printf("Entering emulation loop\n");
    emulation_loop();
}
