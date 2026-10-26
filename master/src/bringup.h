/*
 * frank-amiga — M0 bench bring-up commands on the master.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_BRINGUP_H
#define FRANK_BRINGUP_H

#include "hardware/watchdog.h"

void bringup_init(void);
void bringup_poll(void);

void bringup_cmd_video(int argc, char **argv);
void bringup_cmd_reboot(int argc, char **argv);
void bringup_cmd_tone(int argc, char **argv);
void bringup_cmd_sd(int argc, char **argv);
void bringup_cmd_sdread(int argc, char **argv);
void bringup_cmd_flashtest(int argc, char **argv);

#define BRINGUP_COMMANDS \
    { "video",  bringup_cmd_video,  "video [50|60|stat]: test pattern" }, \
    { "reboot", bringup_cmd_reboot, "reboot the master" }, \
    { "tone",   bringup_cmd_tone,   "tone [hz] [seconds] [vol/256]: I2S sine" }, \
    { "sd",     bringup_cmd_sd,     "sd [dir]: list a directory" }, \
    { "sdread", bringup_cmd_sdread, "sdread <file>: read throughput" }, \
    { "flashtest", bringup_cmd_flashtest, "flashtest [div] [passes]: XIP read integrity per RXDELAY" },

#endif
