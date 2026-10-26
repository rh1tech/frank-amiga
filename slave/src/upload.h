/*
 * frank-amiga — slave: Kickstart, extended ROM and disks from the master.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_UPLOAD_H
#define FRANK_UPLOAD_H

#include <stdbool.h>
#include <stdint.h>
#include "link_session.h"

/* Core 1: handle an upload/boot/insert control op; false if not ours. */
bool upload_handle(link_session_t *s, uint16_t op);

/* Core 0, once per frame: holds while an upload rewrites memory, performs
 * a requested disk change. */
void upload_core0_frame(void);
void upload_set_running(bool on);

/* What the master uploaded before the last reset (verified by CRC), or
 * NULL. */
const uint8_t *upload_rom(uint32_t *size);
const uint8_t *upload_ext_rom(void);
/* LINK_BOOT_* flags the master booted us with. */
uint32_t upload_boot_flags(void);
/* The uploaded disk for DF0 / DF1, or NULL. */
const uint8_t *upload_df(int drive);

#endif
