/*
 * frank-amiga — clock bring-up shared by both halves.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Both chips run clk_sys at the same frequency: the link's receive PIO has
 * to finish its loop inside the transmitter's byte period, and each side
 * derives that from its own clk_sys.
 *
 * 528 MHz rather than the 504 the other FRANK Core 2 firmwares use, because
 * of HDMI. HSTX wants a 27 MHz pixel clock for 720x576p50 (TMDS 270 Mbit/s,
 * clk_hstx 135 MHz in DDR), and 504 MHz has no integer path to it. So the
 * USB PLL is dedicated to HSTX and clk_usb comes from PLL_SYS instead:
 * 528 / 11 = 48 MHz exactly. Switching video mode then retunes only
 * PLL_USB and never the CPUs, which matters because a coordinated re-clock
 * of both halves over the link would be fragile.
 *
 *   PLL_SYS  12 MHz x 132 = 1584 MHz VCO / 3 / 1 = 528 MHz -> clk_sys
 *   clk_usb  = PLL_SYS / 11 = 48 MHz
 *   clk_peri = PLL_SYS / 4  = 132 MHz (UART, SPI; SPI0 can reach 33 MHz)
 *   PLL_USB  = TMDS bit rate (270 or 252 MHz) -> clk_hstx = PLL_USB / 2
 *   QMI M0   = flash, /8 = 66 MHz
 *   QMI M1   = PSRAM, /4 = 132 MHz
 */
#include "clocks.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/qmi.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

/* Flash QMI divider for the new clk_sys. Has to run from RAM and before
 * clk_sys rises, or the XIP fetch of the very next instruction runs the
 * flash out of spec.
 *
 * Identical to frank-linux's flash_set_clkdiv, which is proven at 504 MHz on
 * this board. Note that RXDELAY is a 3-bit field: at divisor 8 the computed
 * delay of 8 masks to 0, and that is the configuration that has run for
 * months, so it is kept rather than "corrected" to something untested. */
static void __no_inline_not_in_flash_func(flash_set_clkdiv)(uint32_t clock_hz) {
    const uint32_t max_hz = FRANK_FLASH_MAX_MHZ * 1000000u;
    uint32_t divisor = (clock_hz + max_hz - 1u) / max_hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor > 255u) divisor = 255u;
    uint32_t rxdelay = divisor;
    if (clock_hz / divisor > 100000000u) rxdelay += 1u;

    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (divisor << QMI_M0_TIMING_CLKDIV_LSB) |
         ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS);
    qmi_hw->m[0].timing = t;
    __asm__ volatile("dmb" ::: "memory");
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("nop");
}

bool frank_clocks_init(void) {
    vreg_disable_voltage_limit();
    vreg_set_voltage(FRANK_CPU_VREG);
    sleep_ms(10);

    flash_set_clkdiv(FRANK_CPU_MHZ * 1000000u);
    if (!set_sys_clock_khz(FRANK_CPU_MHZ * 1000u, false))
        return false;

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    /* The SDK leaves clk_peri on PLL_USB, which HSTX is about to take. */
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    sys_hz, sys_hz / 4u);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    sys_hz, 48u * MHZ);
    return true;
}

bool frank_clocks_set_tmds(uint32_t bit_khz) {
    /* VCO 750..1600 MHz; find any integer configuration hitting the TMDS
     * bit rate exactly, then route PLL_USB / 2 to clk_hstx. */
    uint vco_hz, pd1, pd2;
    if (!check_sys_clock_hz(bit_khz * 1000u, &vco_hz, &pd1, &pd2))
        return false;
    pll_init(pll_usb, 1, vco_hz, pd1, pd2);
    return clock_configure(clk_hstx, 0,
                           CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                           bit_khz * 1000u, bit_khz * 500u);
}

void frank_clocks_report(void) {
    printf("clk_sys  %lu kHz (measured %lu)\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000u),
           (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));
    printf("clk_peri %lu kHz, clk_usb %lu kHz (measured %lu)\n",
           (unsigned long)(clock_get_hz(clk_peri) / 1000u),
           (unsigned long)(clock_get_hz(clk_usb) / 1000u),
           (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB));
    const uint32_t m0 = qmi_hw->m[0].timing, m1 = qmi_hw->m[1].timing;
    printf("flash /%lu, psram /%lu, vreg %s\n",
           (unsigned long)((m0 & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB),
           (unsigned long)((m1 & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB),
           FRANK_CPU_VREG == VREG_VOLTAGE_1_65 ? "1.65 V" : "other");
}
