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
    char kick[128], ext[128], df0[128];
} media_config_t;
extern media_config_t media_config;

void media_init(link_session_t *sess);
bool media_mount(void);
bool media_load_config(void);
/* Upload the configured ROMs and disk, then reboot the slave from them. */
bool media_boot(void);
bool media_upload(uint32_t target, const char *path);
/* Change the disk in DF0 while the machine runs. */
bool media_insert_df0(const char *path);
bool media_sdput(uint32_t psram_offset, uint32_t size, const char *path);

#endif
