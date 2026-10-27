/*
 * frank-amiga — line console on the master's UART0.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_CONSOLE_H
#define FRANK_CONSOLE_H

typedef struct {
    const char *name;
    void (*fn)(int argc, char **argv);
    const char *help;
} console_cmd_t;

/* Never returns. `idle` runs whenever no input is pending. */
void console_run(const console_cmd_t *cmds, void (*idle)(void));
/* Moves received characters into the console's buffer. Long jobs (the
 * stream's chunk bursts) call it between steps: the UART holds only 32. */
void console_rx_poll(void);

#endif
