/*
 * frank-amiga — master: the on-screen menu (F12).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A text box drawn over the Amiga's picture by the scanout row callback
 * (osd_overlay_row(), from denise_row() on core 1); the menu logic runs on
 * core 0 from key events (USB, or the console's `osdkey`). While it is open
 * the Amiga gets no keyboard or mouse input.
 */
#include "osd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "ff.h"

#include "audio/audio.h"
#include "input.h"
#include "link_stream.h"
#include "media.h"
#include "stream.h"
#include "video/hstx_video.h"
#include "osd_font.h"
#include "usb_amiga.h"

#define COLS 36
#define ROWS 14
#define LIST_ROWS (ROWS - 4)

/* The text the overlay draws (core 1 reads it, core 0 writes it). */
static char text[ROWS][COLS];
static uint8_t highlight = 0xFF;          /* row drawn inverted */
static volatile bool visible;

/* ── Rendering (core 1, scanout interrupt) ─────────────────────────────── */
static uint32_t col_fg, col_bg, col_hl_fg, col_hl_bg, col_frame;
static uint32_t box_x0, box_row0;          /* in output pixels / source rows */

void __not_in_flash_func(osd_overlay_row)(uint32_t row, uint32_t *out, uint32_t width) {
    if (!visible) return;
    const uint32_t box_rows = ROWS * 8u + 4u;
    if (row < box_row0 || row >= box_row0 + box_rows) return;
    const uint32_t y = row - box_row0;
    const uint32_t box_w = COLS * 16u + 8u;
    uint32_t *o = out + box_x0;
    if (y < 2u || y >= box_rows - 2u) {
        for (uint32_t x = 0; x < box_w; x++) o[x] = col_frame;
        return;
    }
    o[0] = o[1] = o[2] = o[3] = col_frame;
    o[box_w - 1] = o[box_w - 2] = o[box_w - 3] = o[box_w - 4] = col_frame;
    const uint32_t ty = y - 2u, r = ty >> 3, gy = ty & 7u;
    const bool hl = r == highlight;
    const uint32_t fg = hl ? col_hl_fg : col_fg, bg = hl ? col_hl_bg : col_bg;
    uint32_t *p = o + 4;
    for (uint32_t c = 0; c < COLS; c++) {
        const uint8_t bits = osd_font[(uint8_t)text[r][c] * 8u + gy];
        for (uint32_t b = 0; b < 8; b++) {
            const uint32_t v = (bits >> b) & 1u ? fg : bg;
            p[0] = v;
            p[1] = v;
            p += 2;
        }
    }
    (void)width;
}

/* ── Menu (core 0) ─────────────────────────────────────────────────────── */
typedef enum { M_MAIN, M_FILES } mode_t;
static mode_t mode;
static int sel, top;
static int files_target;                   /* T_DF0, T_DF1, T_KICK */
enum { T_DF0, T_DF1, T_KICK };
#define MAX_FILES 256
static char (*files)[64];                  /* names, in PSRAM */
static int nfiles;

static void put(int r, const char *s) {
    memset(text[r], ' ', COLS);
    for (int c = 0; c < COLS && s[c]; c++) text[r][c] = s[c];
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

enum { I_DF0, I_DF1, I_KICK, I_RESET, I_SAVE, I_LOAD, I_VOL, I_JOY, I_EXIT, I_COUNT };

static void draw(void) {
    char line[COLS + 1];
    for (int r = 0; r < ROWS; r++) put(r, "");
    if (mode == M_MAIN) {
        put(0, "        frank-amiga  (F12)");
        snprintf(line, sizeof line, " DF0:  %.28s", media_config.df0[0] ? base(media_config.df0) : "(empty)");
        put(2 + I_DF0, line);
        snprintf(line, sizeof line, " DF1:  %.28s", media_config.df1[0] ? base(media_config.df1) : "(empty)");
        put(2 + I_DF1, line);
        snprintf(line, sizeof line, " Kick: %.28s", base(media_config.kick));
        put(2 + I_KICK, line);
        put(2 + I_RESET, " Reset (Ctrl-Amiga-Amiga)");
        put(2 + I_SAVE, " Save state");
        put(2 + I_LOAD, " Load state");
        snprintf(line, sizeof line, " Volume: %lu/256   (left/right)", (unsigned long)audio_get_volume());
        put(2 + I_VOL, line);
        snprintf(line, sizeof line, " Joystick keys: %s", usb_amiga_joykeys_name(media_config.joykeys));
        put(2 + I_JOY, line);
        put(2 + I_EXIT, " Back to the Amiga");
        put(ROWS - 1, " up/down, Enter, Esc");
        highlight = (uint8_t)(2 + sel);
    } else {
        put(0, files_target == T_KICK ? " Kickstart (/amiga/kick)"
             : files_target == T_DF1 ? " Disk for DF1 (/amiga/adf)" : " Disk for DF0 (/amiga/adf)");
        for (int i = 0; i < LIST_ROWS && top + i < nfiles; i++) {
            snprintf(line, sizeof line, " %.34s", files[top + i]);
            put(2 + i, line);
        }
        if (!nfiles) put(2, " (no files)");
        snprintf(line, sizeof line, " %d/%d  Enter: choose  Esc: back", nfiles ? sel + 1 : 0, nfiles);
        put(ROWS - 1, line);
        highlight = nfiles ? (uint8_t)(2 + sel - top) : 0xFF;
    }
}

static int cmp(const void *a, const void *b) { return strcasecmp(a, b); }

static void load_files(const char *dir) {
    nfiles = 0;
    if (!media_mount()) return;
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, dir) != FR_OK) return;
    while (nfiles < MAX_FILES && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & (AM_DIR | AM_HID) || fi.fname[0] == '.') continue;
        snprintf(files[nfiles++], 64, "%s", fi.fname);
    }
    f_closedir(&d);
    qsort(files, (size_t)nfiles, 64, cmp);
}

static void choose_file(void) {
    if (!nfiles) return;
    char path[128];
    snprintf(path, sizeof path, "%s/%s", files_target == T_KICK ? "/amiga/kick" : "/amiga/adf", files[sel]);
    visible = false;                       /* show the machine while it loads */
    if (files_target == T_KICK) {
        snprintf(media_config.kick, sizeof media_config.kick, "0:%s", path);
        /* An extended ROM only belongs to AROS. */
        if (!strstr(files[sel], "aros")) media_config.ext[0] = 0;
        media_boot();
        sleep_ms(300);
        stream_start();
    } else {
        media_insert(files_target == T_DF1, path);
    }
    mode = M_MAIN;
    sel = files_target == T_KICK ? I_KICK : files_target == T_DF1 ? I_DF1 : I_DF0;
}

void osd_key(uint8_t hid, bool down) {
    if (!down) return;
    const int n = mode == M_MAIN ? I_COUNT : nfiles;
    switch (hid) {
    case 0x52: if (sel > 0) sel--; break;                       /* up */
    case 0x51: if (sel < n - 1) sel++; break;                   /* down */
    case 0x4B: sel = sel > LIST_ROWS ? sel - LIST_ROWS : 0; break; /* page up */
    case 0x4E: sel = sel + LIST_ROWS < n ? sel + LIST_ROWS : (n ? n - 1 : 0); break;
    case 0x50: case 0x4F:                                       /* left/right */
        if (mode == M_MAIN && sel == I_VOL) {
            int v = (int)audio_get_volume() + (hid == 0x4F ? 16 : -16);
            audio_set_volume((uint32_t)(v < 0 ? 0 : v > 256 ? 256 : v));
        } else if (mode == M_MAIN && sel == I_JOY) {
            const int n = JOYKEYS_COUNT, d = hid == 0x4F ? 1 : n - 1;
            media_config.joykeys = (uint8_t)((media_config.joykeys + d) % n);
        }
        break;
    case 0x29:                                                  /* Esc */
        if (mode == M_FILES) {
            mode = M_MAIN;
            sel = files_target == T_KICK ? I_KICK : files_target == T_DF1 ? I_DF1 : I_DF0;
        }
        else visible = false;
        break;
    case 0x28: case 0x58:                                       /* Enter */
        if (mode == M_FILES) { choose_file(); break; }
        switch (sel) {
        case I_DF0: case I_DF1: case I_KICK:
            files_target = sel == I_KICK ? T_KICK : sel == I_DF1 ? T_DF1 : T_DF0;
            load_files(files_target == T_KICK ? "0:/amiga/kick" : "0:/amiga/adf");
            mode = M_FILES;
            sel = top = 0;
            break;
        case I_RESET: stream_input(LINK_EV_RESET, 0, 0, 0); visible = false; break;
        case I_SAVE: media_state(false); visible = false; break;
        case I_LOAD: media_state(true); visible = false; break;
        case I_JOY: media_config.joykeys = (uint8_t)((media_config.joykeys + 1) % JOYKEYS_COUNT); break;
        case I_EXIT: visible = false; break;
        }
        break;
    }
    if (mode == M_FILES) {
        if (sel < top) top = sel;
        if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
    }
    draw();
}

void osd_toggle(void) {
    visible = !visible;
    if (visible) { mode = M_MAIN; sel = 0; draw(); }
}

bool osd_active(void) { return visible; }

void osd_init(void) {
    files = (char (*)[64])(0x11000000u + 0x100000u);   /* master PSRAM, 16 KB */
    col_fg = video_rgb12_to_888(0xEEE);
    col_bg = video_rgb12_to_888(0x125);
    col_hl_fg = video_rgb12_to_888(0x000);
    col_hl_bg = video_rgb12_to_888(0xFA0);
    col_frame = video_rgb12_to_888(0x8AF);
    box_x0 = (video_width() - (COLS * 16u + 8u)) / 2u;
    box_row0 = (video_rows() - (ROWS * 8u + 4u)) / 2u;
    draw();
}
