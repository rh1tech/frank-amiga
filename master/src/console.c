/*
 * frank-amiga — line console on the master's UART0.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every command answers with a final line starting "OK" or "ERR" so a host
 * script can wait for completion without knowing the command's output.
 */
#include "console.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#define LINE_MAX 256
#define ARGS_MAX 12

static void run_line(const console_cmd_t *cmds, char *line) {
    char *argv[ARGS_MAX];
    int argc = 0;
    /* Words, or "quoted strings" (file names with spaces). */
    for (char *p = line; *p && argc < ARGS_MAX;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *end;
        if (*p == '"') {
            argv[argc++] = ++p;
            end = strchr(p, '"');
        } else {
            argv[argc++] = p;
            end = p + strcspn(p, " \t");
        }
        if (!end || !*end) break;
        *end = 0;
        p = end + 1;
    }
    if (!argc)
        return;
    if (!strcmp(argv[0], "help")) {
        for (const console_cmd_t *c = cmds; c->name; c++)
            printf("  %-8s %s\n", c->name, c->help);
        printf("OK\n");
        return;
    }
    for (const console_cmd_t *c = cmds; c->name; c++) {
        if (!strcmp(argv[0], c->name)) {
            c->fn(argc, argv);
            printf("OK %s\n", c->name);
            return;
        }
    }
    printf("ERR unknown command '%s'\n", argv[0]);
}

void console_run(const console_cmd_t *cmds, void (*idle)(void)) {
    char line[LINE_MAX];
    size_t n = 0;
    printf("> ");
    for (;;) {
        const int ch = getchar_timeout_us(1000);
        if (ch == PICO_ERROR_TIMEOUT) {
            if (idle) idle();
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            line[n] = 0;
            printf("\n");
            if (n) run_line(cmds, line);
            n = 0;
            printf("> ");
        } else if ((ch == 8 || ch == 127) && n) {
            n--;
        } else if (ch >= 32 && n < LINE_MAX - 1) {
            line[n++] = (char)ch;
        }
    }
}
