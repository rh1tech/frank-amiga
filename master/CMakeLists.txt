# frank-amiga — master firmware (RP2350B, U3): Denise, HDMI, audio, SD, USB.
#
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.13)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(PICO_BOARD_HEADER_DIRS ${REPO_ROOT}/common/boards CACHE PATH "" FORCE)
set(PICO_BOARD "frank_core2_master" CACHE STRING "Pico board type" FORCE)

include(${REPO_ROOT}/common/frank.cmake)
include(${REPO_ROOT}/pico_sdk_import.cmake)
project(frank-amiga-master C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${REPO_ROOT}/common/link link)
add_subdirectory(lib/fatfs)
include(lib/sdcard/sdcard.cmake)

add_executable(frank-amiga-master
    src/main.c
    src/console.c
    src/bringup.c
    src/video/hstx_video.c
    src/video/hdmi_island.c
    src/audio/audio.c
    src/usb/hid_app.c
    src/usb/xinput_host.c
    src/stream.c
    src/input.c
    src/media.c
    src/scp.c
    src/osd.c
    src/usb_amiga.c
    src/denise/denise_c1.c
    src/denise/denise_frames.c
    src/denise/denise_audio.c
    ${REPO_ROOT}/third_party/omega/AudioMix.c
    ${FRANK_PLATFORM_SOURCES}
)
target_include_directories(frank-amiga-master PRIVATE src src/usb
    ${REPO_ROOT}/common/amiga ${REPO_ROOT}/third_party/omega ${FRANK_PLATFORM_DIR})
target_compile_definitions(frank-amiga-master PRIVATE ${FRANK_DEFINITIONS}
    PICO_BUILD=1 OMEGA_VIDEO_STANDARD=1 OMEGA_ENABLE_HDMI=1 OMEGA_RTG=0
    RING_WORDS=12288u)   # 48 KB render ring (master SRAM is tight)
target_compile_options(frank-amiga-master PRIVATE ${FRANK_COPTS})
target_link_libraries(frank-amiga-master
    pico_stdlib pico_multicore hardware_clocks hardware_dma hardware_pio
    hardware_pll hardware_vreg hardware_watchdog link sdcard fatfs
    tinyusb_host tinyusb_board)
# microSD J7 on SPI0.
target_compile_definitions(frank-amiga-master PRIVATE
    SDCARD_SPI_BUS=spi0 SDCARD_PIN_SPI0_SCK=6 SDCARD_PIN_SPI0_MOSI=7
    SDCARD_PIN_SPI0_MISO=4 SDCARD_PIN_SPI0_CS=5)

# Console on UART0 (J2), read through the master's probe.
pico_generate_pio_header(frank-amiga-master ${CMAKE_CURRENT_LIST_DIR}/src/audio/audio_i2s.pio)

pico_enable_stdio_uart(frank-amiga-master 1)
pico_enable_stdio_usb(frank-amiga-master 0)
pico_add_extra_outputs(frank-amiga-master)
