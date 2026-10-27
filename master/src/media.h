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
    char kick[128], ext[128], df0[128], df1[128];
    bool drive1;                /* DF1 connected (also when df1 is set) */
    uint8_t joykeys;            /* usb_amiga.h JOYKEYS_* */
    uint16_t chip_kb, slow_kb;  /* 512/1024/2048, 0/512/1024 */
} media_config_t;
extern media_config_t media_config;

void media_init(link_session_t *sess);
bool media_mount(void);
bool media_load_config(void);
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
/* Main loop: saves written disk tracks back to their ADF files. */
void media_poll(void);
bool media_sdput(uint32_t psram_offset, uint32_t size, const char *path);

#endif
