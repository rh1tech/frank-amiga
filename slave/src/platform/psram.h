// frank-amiga — PSRAM aliases for the Omega memory code (slave).
//
// SPDX-License-Identifier: MPL-2.0
// Derived from github.com/fboldog/rp2350-amiga src/psram.h. QMI setup is
// common/platform/psram_init.c, run from main() before anything here.
#pragma once
#include <stdint.h>
#include "pico/stdlib.h"
#include "board_config.h"

#define PSRAM_BASE      BOARD_PSRAM_BASE
#define PSRAM_SIZE      BOARD_PSRAM_SIZE_BYTES

#define PSRAM_CHIPRAM_OFFSET    BOARD_MAP_CHIPRAM_OFFSET
#define PSRAM_DF0_OFFSET        BOARD_MAP_DF0_OFFSET
#define PSRAM_ROM_OFFSET        BOARD_MAP_ROM_OFFSET

#define PSRAM_CHIPRAM_SIZE      BOARD_MAP_CHIPRAM_SIZE
#define PSRAM_FLOPPY_SIZE       BOARD_MAP_FLOPPY_SIZE

static inline uint8_t *psram_ptr(uint32_t offset) {
    return (uint8_t *)(PSRAM_BASE + offset);
}
