/*
 * frank-amiga — master: USB keyboard and mouse to the Amiga.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HID usages map to Amiga raw keycodes by key position (US layout). Keys an
 * Amiga lacks: GUI -> Amiga keys, Menu -> right Amiga, Insert/F11 -> Help,
 * Num Lock / Scroll Lock -> keypad ( ), Page Up/Down -> right Amiga + Up/Down
 * is left to the OS. F12 is reserved for the on-screen menu (osd.c).
 *
 * Joysticks: USB gamepad 1 is the joystick in port 2 (the games' port),
 * gamepad 2 the one in port 1 (the mouse port); d-pad or left stick, A
 * fire, B second fire. The keyboard can stand in for port 2's joystick
 * (media_config.joykeys).
 */
#include "usb_amiga.h"

#include <stdio.h>

#include <string.h>

#include "input.h"
#include "link_stream.h"
#include "media.h"
#include "stream.h"
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

uint8_t usb_amiga_joykeys_parse(const char *s) {
    for (uint8_t m = 0; m < JOYKEYS_COUNT; m++)
        if (!strcmp(s, usb_amiga_joykeys_name(m))) return m;
    return JOYKEYS_OFF;
}

const char *usb_amiga_joykeys_name(uint8_t mode) {
    static const char *const names[] = { "off", "arrows", "numpad" };
    return mode < JOYKEYS_COUNT ? names[mode] : names[0];
}

/* The LINK_JOY_* bit a key is in the current keyboard joystick, or 0. */
static uint8_t joykey_bit(uint8_t hid) {
    switch (media_config.joykeys) {
    case JOYKEYS_ARROWS:
        switch (hid) {
        case 0x52: return LINK_JOY_UP;
        case 0x51: return LINK_JOY_DOWN;
        case 0x50: return LINK_JOY_LEFT;
        case 0x4F: return LINK_JOY_RIGHT;
        case 0xE4: return LINK_JOY_FIRE;      /* Right Ctrl */
        case 0xE6: return LINK_JOY_FIRE2;     /* Right Alt */
        }
        break;
    case JOYKEYS_NUMPAD:
        switch (hid) {
        case 0x60: return LINK_JOY_UP;        /* 8 */
        case 0x5A: case 0x5D: return LINK_JOY_DOWN;   /* 2, 5 */
        case 0x5C: return LINK_JOY_LEFT;      /* 4 */
        case 0x5E: return LINK_JOY_RIGHT;     /* 6 */
        case 0x5F: return LINK_JOY_UP | LINK_JOY_LEFT;   /* 7 */
        case 0x61: return LINK_JOY_UP | LINK_JOY_RIGHT;  /* 9 */
        case 0x59: return LINK_JOY_DOWN | LINK_JOY_LEFT; /* 1 */
        case 0x5B: return LINK_JOY_DOWN | LINK_JOY_RIGHT;/* 3 */
        case 0x62: return LINK_JOY_FIRE;      /* 0 */
        case 0x63: return LINK_JOY_FIRE2;     /* . */
        }
        break;
    }
    return 0;
}

static uint8_t key_joy;              /* LINK_JOY_* bits held on the keyboard */
static uint8_t key_joy_count[8];     /* keys holding each bit */

static void joykey(uint8_t bits, int down) {
    for (int b = 0; b < 8; b++) {
        if (!(bits & (1u << b))) continue;
        if (down) key_joy_count[b]++;
        else if (key_joy_count[b]) key_joy_count[b]--;
        if (key_joy_count[b]) key_joy |= (uint8_t)(1u << b);
        else key_joy &= (uint8_t)~(1u << b);
    }
}

static uint8_t pad_bits(int idx) {
    if (!usbhid_gamepad_connected_idx(idx)) return 0;
    usbhid_gamepad_state_t g;
    usbhid_get_gamepad_state_idx(idx, &g);
    uint8_t b = 0;
    if ((g.dpad & 0x01) || g.axis_y < -64) b |= LINK_JOY_UP;
    if ((g.dpad & 0x02) || g.axis_y > 64) b |= LINK_JOY_DOWN;
    if ((g.dpad & 0x04) || g.axis_x < -64) b |= LINK_JOY_LEFT;
    if ((g.dpad & 0x08) || g.axis_x > 64) b |= LINK_JOY_RIGHT;
    if (g.buttons & 0x0001) b |= LINK_JOY_FIRE;
    if (g.buttons & 0x0002) b |= LINK_JOY_FIRE2;
    return b;
}

/* Console override (`joy`), merged with the USB sources. */
static uint8_t forced[2];
void usb_amiga_force_joy(int port, uint8_t bits) {
    if (port == 0 || port == 1) forced[port] = bits;
}

/* Sends each port's state when it changes. Port 1 (the mouse port) only
 * turns into a joystick on the slave once its joystick does something. */
static void joy_poll(void) {
    static uint8_t sent[2];
    uint8_t now[2] = { (uint8_t)(pad_bits(1) | forced[0]),
                       (uint8_t)(pad_bits(0) | key_joy | forced[1]) };
    if (osd_active()) now[0] = now[1] = 0;
    for (int p = 0; p < 2; p++)
        if (now[p] != sent[p] && stream_input(LINK_EV_JOY, (uint8_t)p, now[p], 0))
            sent[p] = now[p];
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
        const uint8_t jb = joykey_bit(code);
        if (jb) {
            joykey(jb, down);
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
    joy_poll();
}
