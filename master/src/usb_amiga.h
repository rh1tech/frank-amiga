/*
 * frank-amiga — master: USB keyboard and mouse to the Amiga.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_USB_AMIGA_H
#define FRANK_USB_AMIGA_H

#include <stdint.h>

/* Keyboard joystick (port 2): its keys stop reaching the Amiga. */
enum { JOYKEYS_OFF, JOYKEYS_ARROWS, JOYKEYS_NUMPAD, JOYKEYS_COUNT };
uint8_t usb_amiga_joykeys_parse(const char *s);
const char *usb_amiga_joykeys_name(uint8_t mode);

/* Console: hold LINK_JOY_* `bits` on Amiga port `port` (0 or 1). */
void usb_amiga_force_joy(int port, uint8_t bits);

void usb_amiga_init(void);
/* Call from the main loop: TinyUSB, keys and mouse into the stream. */
void usb_amiga_poll(void);

#endif
