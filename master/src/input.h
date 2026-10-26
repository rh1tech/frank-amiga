/*
 * frank-amiga — master: keyboard and mouse input towards the slave.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_INPUT_H
#define FRANK_INPUT_H

#include <stdbool.h>
#include <stdint.h>

bool input_ascii_to_amiga(char ch, uint8_t *code, bool *shift);
/* One Amiga raw keycode transition. */
bool input_key(uint8_t code, bool released);
/* Queue text to type (press/release per character, Shift as needed). */
bool input_type(const char *s);
/* Mouse motion (counts) and buttons (bit 0 left, 1 right, 2 middle). */
void input_mouse(int dx, int dy, uint8_t buttons);
/* Feed queued text into the stream; call from the main loop. */
void input_poll(void);

#endif
