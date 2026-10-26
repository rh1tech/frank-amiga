/*
 * frank-amiga — master: USB keyboard and mouse to the Amiga.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HID usages map to Amiga raw keycodes by key position (US layout). Keys an
 * Amiga lacks: GUI -> Amiga keys, Menu -> right Amiga, Insert/F11 -> Help,
 * Num Lock / Scroll Lock -> keypad ( ), Page Up/Down -> right Amiga + Up/Down
 * is left to the OS. F12 is reserved for the on-screen menu (osd.c).
 */
#include "usb_amiga.h"

#include <stdio.h>

#include "input.h"
#include "osd.h"
#include "usb/usbhid.h"

static uint8_t hid2amiga[256];

#define A(hid, ami) hid2amiga[hid] = (uint8_t)((ami) | 0x80u)  /* 0x80: mapped */

void usb_amiga_init(void) {
    static const uint8_t letters[26] = {      /* a..z */
        0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28,
        0x37, 0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32,
        0x15, 0x31,
    };
    for (int i = 0; i < 26; i++) A(0x04 + i, letters[i]);
    for (int i = 0; i < 9; i++) A(0x1E + i, 0x01 + i);   /* 1..9 */
    A(0x27, 0x0A);                                       /* 0 */
    A(0x28, 0x44); A(0x29, 0x45); A(0x2A, 0x41); A(0x2B, 0x42); A(0x2C, 0x40);
    A(0x2D, 0x0B); A(0x2E, 0x0C); A(0x2F, 0x1A); A(0x30, 0x1B); A(0x31, 0x0D);
    A(0x32, 0x2B); A(0x33, 0x29); A(0x34, 0x2A); A(0x35, 0x00); A(0x36, 0x38);
    A(0x37, 0x39); A(0x38, 0x3A); A(0x39, 0x62);
    for (int i = 0; i < 10; i++) A(0x3A + i, 0x50 + i);  /* F1..F10 */
    A(0x44, 0x5F);                                       /* F11 -> Help */
    A(0x49, 0x5F);                                       /* Insert -> Help */
    A(0x47, 0x5B); A(0x53, 0x5A);                        /* Scroll/Num -> ( ) */
    A(0x4C, 0x46);                                       /* Delete */
    A(0x4F, 0x4E); A(0x50, 0x4F); A(0x51, 0x4D); A(0x52, 0x4C);
    A(0x54, 0x5C); A(0x55, 0x5D); A(0x56, 0x4A); A(0x57, 0x5E); A(0x58, 0x43);
    static const uint8_t kp[10] = { 0x0F, 0x1D, 0x1E, 0x1F, 0x2D, 0x2E, 0x2F, 0x3D, 0x3E, 0x3F };
    for (int i = 0; i < 9; i++) A(0x59 + i, kp[1 + i]);  /* keypad 1..9 */
    A(0x62, kp[0]); A(0x63, 0x3C);                       /* keypad 0, . */
    A(0x64, 0x30);                                       /* non-US \ (<>) */
    A(0x65, 0x67);                                       /* Menu -> right Amiga */
    A(0xE0, 0x63); A(0xE4, 0x63);                        /* Ctrl */
    A(0xE1, 0x60); A(0xE5, 0x61);                        /* Shift L/R */
    A(0xE2, 0x64); A(0xE6, 0x65);                        /* Alt L/R */
    A(0xE3, 0x66); A(0xE7, 0x67);                        /* GUI -> Amiga L/R */
    /* usbhid_init() is done by bringup_init(). */
}

void usb_amiga_poll(void) {
    usbhid_task();
    uint8_t code;
    int down;
    while (usbhid_get_key_action(&code, &down)) {
        if (code == 0x45) {                              /* F12: menu */
            if (down) osd_toggle();
            continue;
        }
        if (osd_active()) {                              /* the menu has the keyboard */
            osd_key(code, down);
            continue;
        }
        const uint8_t a = hid2amiga[code];
        if (a & 0x80u) input_key(a & 0x7Fu, !down);
    }
    usbhid_mouse_state_t ms;
    usbhid_get_mouse_state(&ms);
    static uint8_t last_buttons;
    if (!osd_active() && (ms.has_motion || ms.buttons != last_buttons)) {
        input_mouse(ms.dx, ms.dy, ms.buttons);
        last_buttons = ms.buttons;
    }
}
