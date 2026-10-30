/*
 * frank-amiga — master: Kickstart and disks from the microSD to the slave.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_MEDIA_H
#define FRANK_MEDIA_H

#include <stdbool.h>
#include <stdint.h>
#include "link_session.h"

typedef struct {
    char kick[256], ext[256], df0[256], df1[256];
    bool drive1;                /* DF1 connected (also when df1 is set) */
    uint8_t joykeys;            /* usb_amiga.h JOYKEYS_* */
    bool meter;                 /* performance meter shown (osd.h) */
    bool ntsc;                  /* NTSC machine (60 Hz), else PAL */
    bool display480;            /* PAL on 640x480 (240 rows) instead of 640x512 */
    bool floppy_real;           /* disks at real speed, else as fast as DMA asks */
    uint16_t chip_kb, slow_kb;  /* 512/1024/2048, 0/512/1024 */
} media_config_t;
extern media_config_t media_config;

void media_init(link_session_t *sess);
bool media_mount(void);
/* Mounts the card afresh (after a swap). */
bool media_remount(void);
bool media_load_config(void);
/* PAL/NTSC: saved to the ini, then the board restarts. */
void media_set_standard(bool ntsc);
/* Disks at real speed or turbo: applied at once (the slave reads it
 * between frames) and saved to the ini. */
void media_set_floppy(bool real);
/* Writes the current configuration to frank-amiga.ini. */
bool media_save_ini(void);
/* Upload the configured ROMs and disk, then reboot the slave from them. */
bool media_boot(void);
bool media_upload(uint32_t target, const char *path);
/* Change the disk in DF0/DF1 while the machine runs. A DF1 that was not
 * connected at boot is connected now, though AmigaDOS only finds drives as
 * it boots (trackloaders see it). Path "none" ejects. */
bool media_insert(int drive, const char *path);
/* Savestate on the slave (PSRAM, this firmware build only): save or load
 * at its next frame end. */
bool media_state(bool load);
/* Bench: a flux image's track as MFM into master PSRAM 0x100000; bytes. */
uint32_t media_scp_raw(int drive, int track);
/* Bench: stop decoding flux tracks into the cache until the next sdput. */
void media_bench_hold(void);
uint32_t media_flux_dump(int drive, int track, uint32_t *timing);
/* Main loop: saves written disk tracks back to their ADF files. */
void media_poll(void);
bool media_sdput(uint32_t psram_offset, uint32_t size, const char *path);

#endif
