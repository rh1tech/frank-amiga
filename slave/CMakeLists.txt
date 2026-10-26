# frank-amiga — slave firmware (RP2350A, U6): 68000 + Agnus/Paula/CIA side.
#
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.13)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${REPO_ROOT}/common/boards CACHE PATH "" FORCE)
set(PICO_BOARD "frank_core2_slave" CACHE STRING "Pico board type" FORCE)

include(${REPO_ROOT}/common/frank.cmake)
include(${REPO_ROOT}/pico_sdk_import.cmake)
project(frank-amiga-slave C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${REPO_ROOT}/common/link link)

add_executable(frank-amiga-slave
    src/main.c
    ${FRANK_PLATFORM_SOURCES}
)
target_include_directories(frank-amiga-slave PRIVATE src ${FRANK_PLATFORM_DIR})
target_compile_definitions(frank-amiga-slave PRIVATE ${FRANK_DEFINITIONS})
target_compile_options(frank-amiga-slave PRIVATE ${FRANK_COPTS})
target_link_libraries(frank-amiga-slave
    pico_stdlib hardware_clocks hardware_dma hardware_pio hardware_pll
    hardware_vreg hardware_watchdog link)

# Console on UART1 (J4).
pico_enable_stdio_uart(frank-amiga-slave 1)
pico_enable_stdio_usb(frank-amiga-slave 0)
pico_add_extra_outputs(frank-amiga-slave)
