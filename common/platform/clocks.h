/*
 * frank-amiga — clock bring-up shared by both halves.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_CLOCKS_H
#define FRANK_CLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#ifndef FRANK_CPU_MHZ
#define FRANK_CPU_MHZ 528
#endif
#ifndef FRANK_CPU_VREG
#define FRANK_CPU_VREG VREG_VOLTAGE_1_65
#endif
#ifndef FRANK_FLASH_MAX_MHZ
#define FRANK_FLASH_MAX_MHZ 66
#endif
#ifndef FRANK_PSRAM_MAX_MHZ
#define FRANK_PSRAM_MAX_MHZ 133
#endif

/* Raise the core voltage, retime flash, set clk_sys, and move clk_peri and
 * clk_usb onto PLL_SYS so PLL_USB is free for HSTX. Call first in main(),
 * before stdio, PSRAM or core 1. */
bool frank_clocks_init(void);

/* Retune PLL_USB to the TMDS bit rate and feed clk_hstx = PLL_USB / 2.
 * 270000 for 720x576p50 / 720x480p60, 252000 for 640x480p60. */
bool frank_clocks_set_tmds(uint32_t bit_khz);

void frank_clocks_report(void);

#endif
