// frank-amiga — slave (RP2350A, U6) board and emulator memory map.
//
// SPDX-License-Identifier: MPL-2.0
// Derived from github.com/fboldog/rp2350-amiga src/board_config.h. Only the
// emulator's memory map is kept: the slave has no video, SD or buttons; its
// pins are in common/link/link_pins.h.
#pragma once
#include <stdint.h>

#define BOARD_NAME "FRANK Core 2 slave"

#define BOARD_PSRAM_CS_PIN       0
#define BOARD_PSRAM_BASE         0x11000000u // CS1 XIP window (cached)
#define BOARD_PSRAM_SIZE_BYTES   0x800000u   // 8 MB ESP-PSRAM64H

// ---------------------------------------------------------------------------
//  Amiga memory map inside the slave's PSRAM
// ---------------------------------------------------------------------------
//  0x000000..0x1FFFFF  Chip RAM        (2 MB)      Amiga 0x000000..0x1FFFFF
//  0x200000..0x3FFFFF  Fast RAM        (2 MB)      Zorro II autoconfig
//  0x400000..0x4FFFFF  RTG VRAM        (reserved; no RTG card on this port)
//  0x500000..0x57FFFF  Kickstart ROM   (512 KB, loaded from flash or the link)
//  0x580000..0x583FFF  DF0 MFM track   (16 KB)
//  0x584000..0x77FFFF  Free (ADF images, slow RAM later)
//  0x780000..0x79FFFF  68000 opcode table scratch (128 KB, boot only)
//  0x7A0000..0x7FEFFF  Free
//  0x7FF000..0x7FF00B  Boot state (survives resets only)
#define BOARD_MAP_CHIPRAM_OFFSET   0x000000u
#define BOARD_MAP_FAST_RAM_OFFSET  0x200000u
#define BOARD_MAP_RTG_VRAM_OFFSET  0x400000u
#define BOARD_MAP_ROM_OFFSET       0x500000u
#define BOARD_MAP_DF0_OFFSET       0x580000u
#define BOARD_MAP_OPCODE_BUILD_OFFSET 0x780000u
#define BOARD_MAP_BOOT_STATE_OFFSET 0x7FF000u
#define BOARD_MAP_RTG_STATE_OFFSET 0x7FE000u

#define BOARD_MAP_CHIPRAM_SIZE     0x200000u   // 2 MB
#ifndef OMEGA_FAST_RAM
#define OMEGA_FAST_RAM 0
#endif
#ifndef OMEGA_RTG
#define OMEGA_RTG 0
#endif
#define BOARD_MAP_FAST_RAM_SIZE    (OMEGA_FAST_RAM ? 0x200000u : 0u)
#define BOARD_MAP_RTG_VRAM_SIZE    0x100000u
#define BOARD_MAP_FLOPPY_SIZE      0x004000u   // 16 KB (one DF0 MFM track)

// Kickstart image in the slave's flash (M2: written there over SWD with
// tools/flash_rom.sh). Later the master uploads it over the link instead.
#define BOARD_ROM_FLASH_BASE       0x10200000u
#define BOARD_ROM_SIZE             0x080000u   // 512 KB (256 KB ROMs mirrored)
// Optional extended ROM (AROS), right after it; used when it starts 0x11.
#define BOARD_EXT_ROM_FLASH_BASE   0x10280000u
// DF0 test disk (raw ADF, 901120 bytes) in flash until the master uploads
// disks over the link; used when present (not erased flash).
#define BOARD_ADF_FLASH_BASE       0x10300000u
