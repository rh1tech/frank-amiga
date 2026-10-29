/*
 * frank-amiga — master: keyboard and mouse input towards the slave.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything becomes LINK_EV_* events on the stream (stream.c); the slave
 * delivers one key per emulated frame through CIA-A's serial port, as the
 * real keyboard does. Sources: the UART console (type/key/mouse commands,
 * which is how the bench drives the machine) and USB HID.
 */
#include "input.h"
#include "pico/stdlib.h"

#include <ctype.h>
#include <string.h>

#include "link_stream.h"
#include "stream.h"

/* Amiga raw keycodes (US layout). */
enum {
    AK_GRAVE = 0x00, AK_MINUS = 0x0B, AK_EQUAL = 0x0C, AK_BACKSLASH = 0x0D,
    AK_LBRACKET = 0x1A, AK_RBRACKET = 0x1B, AK_SEMICOLON = 0x29,
    AK_QUOTE = 0x2A, AK_COMMA = 0x38, AK_PERIOD = 0x39, AK_SLASH = 0x3A,
    AK_SPACE = 0x40, AK_BACKSPACE = 0x41, AK_TAB = 0x42, AK_RETURN = 0x44,
    AK_ESC = 0x45, AK_DEL = 0x46, AK_LSHIFT = 0x60,
};

static const uint8_t qwerty_row1[] = "qwertyuiop";   /* 0x10.. */
static const uint8_t qwerty_row2[] = "asdfghjkl";    /* 0x20.. */
static const uint8_t qwerty_row3[] = "zxcvbnm";      /* 0x31.. */

/* ASCII -> (keycode, shift). Returns false for characters with no key. */
bool input_ascii_to_amiga(char ch, uint8_t *code, bool *shift) {
    static const char shifted_digits[] = ")!@#$%^&*(";
    *shift = false;
    if (ch >= 'A' && ch <= 'Z') { *shift = true; ch = (char)tolower(ch); }
    const char *p;
    if ((p = strchr((const char *)qwerty_row1, ch)) && ch) { *code = (uint8_t)(0x10 + (p - (const char *)qwerty_row1)); return true; }
    if ((p = strchr((const char *)qwerty_row2, ch)) && ch) { *code = (uint8_t)(0x20 + (p - (const char *)qwerty_row2)); return true; }
    if ((p = strchr((const char *)qwerty_row3, ch)) && ch) { *code = (uint8_t)(0x31 + (p - (const char *)qwerty_row3)); return true; }
    if (ch >= '1' && ch <= '9') { *code = (uint8_t)(0x01 + (ch - '1')); return true; }
    if (ch == '0') { *code = 0x0A; return true; }
    if ((p = strchr(shifted_digits, ch)) && ch) {
        const int d = (int)(p - shifted_digits);
        *code = d == 0 ? 0x0A : (uint8_t)(0x01 + d - 1);
        *shift = true;
        return true;
    }
    switch (ch) {
    case ' ':  *code = AK_SPACE; return true;
    case '\n': case '\r': *code = AK_RETURN; return true;
    case '\t': *code = AK_TAB; return true;
    case '\b': *code = AK_BACKSPACE; return true;
    case 27:   *code = AK_ESC; return true;
    case '`':  *code = AK_GRAVE; return true;
    case '~':  *code = AK_GRAVE; *shift = true; return true;
    case '-':  *code = AK_MINUS; return true;
    case '_':  *code = AK_MINUS; *shift = true; return true;
    case '=':  *code = AK_EQUAL; return true;
    case '+':  *code = AK_EQUAL; *shift = true; return true;
    case '\\': *code = AK_BACKSLASH; return true;
    case '|':  *code = AK_BACKSLASH; *shift = true; return true;
    case '[':  *code = AK_LBRACKET; return true;
    case '{':  *code = AK_LBRACKET; *shift = true; return true;
    case ']':  *code = AK_RBRACKET; return true;
    case '}':  *code = AK_RBRACKET; *shift = true; return true;
    case ';':  *code = AK_SEMICOLON; return true;
    case ':':  *code = AK_SEMICOLON; *shift = true; return true;
    case '\'': *code = AK_QUOTE; return true;
    case '"':  *code = AK_QUOTE; *shift = true; return true;
    case ',':  *code = AK_COMMA; return true;
    case '<':  *code = AK_COMMA; *shift = true; return true;
    case '.':  *code = AK_PERIOD; return true;
    case '>':  *code = AK_PERIOD; *shift = true; return true;
    case '/':  *code = AK_SLASH; return true;
    case '?':  *code = AK_SLASH; *shift = true; return true;
    default:   return false;
    }
}

bool input_key(uint8_t code, bool released) {
    return stream_input(LINK_EV_KEY, code, released ? 1 : 0, 0);
}

/* Queued text, fed into the stream a few events per request (a request
 * carries at most 24 events). */
static char text_q[256];
static uint32_t text_head, text_tail;

bool input_type(const char *s) {
    for (; *s; s++) {
        if (text_head - text_tail >= sizeof text_q) return false;
        text_q[text_head++ % sizeof text_q] = *s;
    }
    return true;
}

/* Typed text goes in at a typist's pace: each key held ~100 ms, ~150 ms
 * apart (about 4 a second). Faster, games that look at their last key
 * only now and then (Speedball 2's code entry) lost keys: each one
 * replaced the one before it unread. */
#define TYPE_HOLD_US 100000u
#define TYPE_GAP_US  150000u

void input_poll(void) {
    static int phase;                 /* 0: press next, 1: held, 2: gap */
    static uint64_t at;
    static uint8_t code;
    static bool shift;
    const uint64_t now = time_us_64();
    if (phase && now < at) return;
    if (phase == 1) {                 /* release */
        if (!input_key(code, true)) return;
        if (shift && !input_key(AK_LSHIFT, true)) return;
        text_tail++;
        phase = 2;
        at = now + TYPE_GAP_US;
        return;
    }
    phase = 0;
    while (text_tail != text_head) {
        const char ch = text_q[text_tail % sizeof text_q];
        if (!input_ascii_to_amiga(ch, &code, &shift)) { text_tail++; continue; }
        if (shift && !input_key(AK_LSHIFT, false)) return;
        if (!input_key(code, false)) return;
        phase = 1;
        at = now + TYPE_HOLD_US;
        return;
    }
}

void input_mouse(int dx, int dy, uint8_t buttons) {
    while (dx || dy) {
        const int sx = dx > 127 ? 127 : dx < -127 ? -127 : dx;
        const int sy = dy > 127 ? 127 : dy < -127 ? -127 : dy;
        if (!stream_input(LINK_EV_MOUSE, (uint8_t)(int8_t)sx, (uint8_t)(int8_t)sy, buttons))
            return;
        dx -= sx;
        dy -= sy;
    }
    stream_input(LINK_EV_MOUSE, 0, 0, buttons);
}
