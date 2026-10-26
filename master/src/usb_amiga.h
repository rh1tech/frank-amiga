/*
 * frank-amiga — master: USB keyboard and mouse to the Amiga.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_USB_AMIGA_H
#define FRANK_USB_AMIGA_H

void usb_amiga_init(void);
/* Call from the main loop: TinyUSB, keys and mouse into the stream. */
void usb_amiga_poll(void);

#endif
