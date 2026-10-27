/*
 * frank-amiga — slave: savestates (the whole emulated machine, in PSRAM).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The emulated machine is the .data/.bss of the Omega files plus Memory and
 * Collision (memmap_slave.ld brackets them: __emu_data_*, __emu_bss_*),
 * together with chip and slow RAM. A save copies those ranges into PSRAM at
 * a frame end; a load copies them back, so the next instruction is the one
 * after the saved frame. States belong to the firmware build that made them
 * (the ranges are raw memory): the header records the layout and a load
 * refuses anything else. The display then starts afresh (every row resent).
 *
 * Slot 0 (the only one for now): emulator ranges at PSRAM 0x7A0000 (the boot
 * opcode table's area, free after boot), chip + slow RAM at 0x200000 (the
 * Fast RAM window, unused: 1 MB, so 512 KB chip + 512 KB slow at most).
 */
#include "savestate.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "board_config.h"
#include "host_link.h"
#include "Memory.h"

extern uint8_t __emu_data_start__[], __emu_data_end__[];
extern uint8_t __emu_bss_start__[], __emu_bss_end__[];

#define STATE_MAGIC 0x41535631u            /* "ASV1" */
#define STATE_RANGES ((uint8_t *)(0x11000000u + 0x7A0000u))
#define STATE_RANGES_MAX (0x7FE000u - 0x7A0000u)
#define STATE_RAM ((uint8_t *)(0x11000000u + 0x200000u))
#define STATE_RAM_MAX 0x100000u

typedef struct {
    uint32_t magic;
    uint32_t data_start, data_len, bss_start, bss_len;
    uint32_t chip, slow;
    uint32_t frame;
} state_hdr_t;

static volatile int req = -1;          /* 0 save, 1 load, -1 none */

void savestate_request(int load, int slot) {
    (void)slot;
    req = load ? 1 : 0;
}

static state_hdr_t layout(void) {
    return (state_hdr_t){
        .magic = STATE_MAGIC,
        .data_start = (uint32_t)(uintptr_t)__emu_data_start__,
        .data_len = (uint32_t)(__emu_data_end__ - __emu_data_start__),
        .bss_start = (uint32_t)(uintptr_t)__emu_bss_start__,
        .bss_len = (uint32_t)(__emu_bss_end__ - __emu_bss_start__),
        .chip = chip_ram_size, .slow = slow_ram_size,
    };
}

static void save(void) {
    state_hdr_t h = layout();
    if (sizeof h + h.data_len + h.bss_len > STATE_RANGES_MAX ||
        h.chip + h.slow > STATE_RAM_MAX) {
        printf("state: machine too large to save (%lu KB chip + %lu KB slow)\n",
               (unsigned long)(h.chip >> 10), (unsigned long)(h.slow >> 10));
        return;
    }
    h.frame = host_frames_emulated;
    uint8_t *p = STATE_RANGES;
    memcpy(p + sizeof h, __emu_data_start__, h.data_len);
    memcpy(p + sizeof h + h.data_len, __emu_bss_start__, h.bss_len);
    for (uint32_t b = 0; b < (h.chip >> CHIP_BANK_SHIFT); ++b)
        memcpy(STATE_RAM + (b << CHIP_BANK_SHIFT), chip_bank[b], 1u << CHIP_BANK_SHIFT);
    if (h.slow)
        memcpy(STATE_RAM + h.chip, slow_ram, h.slow);
    memcpy(p, &h, sizeof h);           /* last: the state is complete */
    printf("state: saved at frame %lu (%lu KB)\n", (unsigned long)h.frame,
           (unsigned long)((h.data_len + h.bss_len + h.chip + h.slow) >> 10));
}

static void load(void) {
    state_hdr_t h, now = layout();
    memcpy(&h, STATE_RANGES, sizeof h);
    if (h.magic != STATE_MAGIC || h.data_start != now.data_start ||
        h.data_len != now.data_len || h.bss_start != now.bss_start ||
        h.bss_len != now.bss_len || h.chip != now.chip || h.slow != now.slow) {
        printf("state: none for this firmware and machine\n");
        return;
    }
    const uint8_t *p = STATE_RANGES;
    memcpy(__emu_data_start__, p + sizeof h, h.data_len);
    memcpy(__emu_bss_start__, p + sizeof h + h.data_len, h.bss_len);
    /* chip_bank[] is back as it was saved: the banks get their contents. */
    for (uint32_t b = 0; b < (h.chip >> CHIP_BANK_SHIFT); ++b)
        memcpy(chip_bank[b], STATE_RAM + (b << CHIP_BANK_SHIFT), 1u << CHIP_BANK_SHIFT);
    if (h.slow)
        memcpy(slow_ram, STATE_RAM + h.chip, h.slow);
    host_state_restored();
    printf("state: loaded (saved at frame %lu)\n", (unsigned long)h.frame);
}

void savestate_core0_frame(void) {
    const int r = req;
    if (r < 0)
        return;
    req = -1;
    if (r) load(); else save();
}
