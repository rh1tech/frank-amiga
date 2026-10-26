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

set(OMEGA_DIR ${REPO_ROOT}/third_party/omega)
set(OMEGA_SOURCES
    ${OMEGA_DIR}/Audio.c
    ${OMEGA_DIR}/Blitter.c
    ${OMEGA_DIR}/Chipset.c
    ${OMEGA_DIR}/CIA.c
    ${OMEGA_DIR}/CPU.c
    ${OMEGA_DIR}/debug.c
    ${OMEGA_DIR}/DMA.c
    ${OMEGA_DIR}/Floppy.c
    ${OMEGA_DIR}/Expansion.c
    ${OMEGA_DIR}/Gayle.c
    ${OMEGA_DIR}/KickSmash.c
    ${OMEGA_DIR}/m68kcpu.c
    ${OMEGA_DIR}/m68kdasm.c
    ${OMEGA_DIR}/m68kopac.c
    ${OMEGA_DIR}/m68kopdm.c
    ${OMEGA_DIR}/m68kopnz.c
    ${OMEGA_DIR}/m68kops.c
)

add_executable(frank-amiga-slave
    src/main.c
    src/host_link.c
    src/link_pump.c
    src/platform/Memory.c
    src/platform/MemoryBoot.c
    src/platform/Planar.c
    ${OMEGA_SOURCES}
    ${FRANK_PLATFORM_SOURCES}
)
# Platform headers (Memory.h, Host.h, board_config.h) must shadow anything
# of the same name; omega's own "../omega/X.h" includes resolve through
# ${OMEGA_DIR}.
target_include_directories(frank-amiga-slave PRIVATE
    src src/platform ${REPO_ROOT}/common/amiga ${OMEGA_DIR} ${FRANK_PLATFORM_DIR})
target_compile_definitions(frank-amiga-slave PRIVATE ${FRANK_DEFINITIONS}
    PICO_BUILD=1
    OMEGA_ENABLE_HDMI=1          # the direct (message ring) display path
    OMEGA_HDMI_AUDIO=0
    OMEGA_AUDIO_OUTPUT=1         # Paula line records, mixed on the master
    OMEGA_VIDEO_STANDARD=1       # PAL
    OMEGA_ENABLE_SDCARD=0
    OMEGA_ENABLE_FLASH_FLOPPY=0
    OMEGA_ENABLE_USB_HOST=0
    OMEGA_FAST_RAM=0
    OMEGA_RTG=0
    OMEGA_REALTIME=0
    OMEGA_HOT_OPCODES_IN_RAM=0
    OMEGA_CPU_CYCLES_PER_SLOT=2
    OMEGA_CPU_SLICE_SLOTS=8
    RING_WORDS=16384u
    PICO_STACK_SIZE=0x1000       # core 0: all of SCRATCH_Y
    PICO_CORE1_STACK_SIZE=0x1000 # core 1: all of SCRATCH_X
    PICO_HEAP_SIZE=0x100
)
target_compile_options(frank-amiga-slave PRIVATE
    -O3 -Wall -Wno-unused-function -Wno-unused-variable
    -Wno-implicit-fallthrough -Wno-unused-parameter)
target_link_libraries(frank-amiga-slave
    pico_stdlib pico_multicore hardware_clocks hardware_dma hardware_pio
    hardware_pll hardware_vreg hardware_watchdog link)

# Console on UART1 (J4).
pico_enable_stdio_uart(frank-amiga-slave 1)
pico_enable_stdio_usb(frank-amiga-slave 0)
# The whole slave image runs from SRAM (crt0 copies it there): the code is
# ~330 KB and the slave has nothing else to put in its 520 KB, so the XIP
# cache is left to the Kickstart ROM alone.
pico_set_binary_type(frank-amiga-slave copy_to_ram)
pico_add_extra_outputs(frank-amiga-slave)
