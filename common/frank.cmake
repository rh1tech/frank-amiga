# frank-amiga — settings shared by the master and slave builds.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Both halves must agree on CPU_MHZ: the link's receive PIO has to finish
# its loop inside the transmitter's byte period, and each side derives that
# from its own clk_sys. build.sh passes the same value to both.

set(CPU_MHZ       "504" CACHE STRING "clk_sys of both halves, MHz")
set(PSRAM_MAX_MHZ "133" CACHE STRING "PSRAM ceiling, MHz")
set(FLASH_MAX_MHZ "66"  CACHE STRING "Flash ceiling, MHz")

set(FRANK_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/platform)
set(FRANK_PLATFORM_SOURCES
    ${FRANK_PLATFORM_DIR}/clocks.c
    ${FRANK_PLATFORM_DIR}/membench.c
    ${FRANK_PLATFORM_DIR}/psram_init.c
)
set(FRANK_DEFINITIONS
    FRANK_CPU_MHZ=${CPU_MHZ}
    FRANK_PSRAM_MAX_MHZ=${PSRAM_MAX_MHZ}
    FRANK_FLASH_MAX_MHZ=${FLASH_MAX_MHZ}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_MAX_MHZ}
    # The SDK's own PLL setup is replaced by frank_clocks_init().
    PICO_STDIO_UART_SUPPORT_CHARS_AVAILABLE_CALLBACK=0
)
set(FRANK_COPTS -O2 -Wall -Wno-unused-parameter -Wno-unused-function)
