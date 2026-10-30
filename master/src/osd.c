/*
 * frank-amiga — master: the on-screen menu (F12) and file manager (F11).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A text box drawn over the Amiga's picture by the scanout row callback
 * (osd_overlay_row(), from denise_row() on core 1); the menu logic runs on
 * core 0 from key events (USB, or the console's `osdkey`). While it is open
 * the Amiga gets no keyboard or mouse input.
 *
 * The file manager (F11) walks the card from /amiga: a disk image (.adf,
 * .scp, .ipf) goes into DF0 or DF1 or boots, a Kickstart (256/512 KB file) is
 * used, each optionally saved to frank-amiga.ini as the power-on choice.
 */
#include "osd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
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
#define ROWS 15
#define LIST_ROWS (ROWS - 4)
/* The file manager: a wide box of single-width characters. */
#define FM_COLS 76
#define FM_ROWS 26
#define FM_LIST (FM_ROWS - 6)

/* The text the overlay draws (core 1 reads it, core 0 writes it), and its
 * size: rows, columns, and 2 for double-width characters. */
static char text[FM_ROWS][FM_COLS];
static uint32_t ov_rows = ROWS, ov_cols = COLS, ov_scale = 2;
static uint8_t highlight = 0xFF;          /* row drawn inverted */
static volatile bool visible;

/* ── Rendering (core 1, scanout interrupt) ─────────────────────────────── */
static uint32_t col_fg, col_bg, col_hl_fg, col_hl_bg, col_frame;
static uint32_t box_x0, box_row0;          /* in output pixels / source rows */

/* ── Performance meter ─────────────────────────────────────────────────
 * Two lines bottom right: "EMU 15.2ms 131%" is the slave's time per
 * emulated frame (core 0, pacing waits excluded; the share of the frame
 * period it needs, inverted: above 100 % there is headroom), "FPS 49.9
 * 504MHz" the emulated frames per second. */
#define METER_COLS 16
#define METER_ROWS 2
static char meter_text[METER_ROWS][METER_COLS];
static volatile bool meter_on;
static uint32_t meter_x0, meter_row0;      /* box corner (output px, source rows) */

void osd_meter_set(bool on) {
    meter_x0 = video_width() - (METER_COLS * 16u + 8u) - 8u;
    meter_row0 = video_rows() - (METER_ROWS * 8u + 4u) - 4u;
    meter_on = on;
}

bool osd_meter_on(void) { return meter_on; }

void osd_meter_frame(uint32_t emu_tenths) {
    static uint64_t t0;
    static uint32_t frames;
    const uint64_t now = time_us_64();
    frames++;
    if (!t0) { t0 = now; frames = 0; return; }
    const uint64_t dt = now - t0;
    if (dt < 1000000u) return;
    /* The frame period the emulator is paced to: the display's refresh. */
    const uint32_t period = strstr(video_mode_name(), "p60") ? 16667u : 20000u;
    const uint32_t fps10 = (uint32_t)(((uint64_t)frames * 10000000u + dt / 2u) / dt);
    char l[2][32];
    snprintf(l[0], sizeof l[0], "EMU%3lu.%lums%4lu%%", (unsigned long)(emu_tenths / 10u),
             (unsigned long)(emu_tenths % 10u), (unsigned long)(emu_tenths ? period / emu_tenths : 0u));
    snprintf(l[1], sizeof l[1], "FPS%3lu.%lu%5luMHz", (unsigned long)(fps10 / 10u),
             (unsigned long)(fps10 % 10u), (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
    for (int r = 0; r < METER_ROWS; r++) {
        memset(meter_text[r], ' ', METER_COLS);
        const size_t n = strlen(l[r]);
        memcpy(meter_text[r], l[r], n < METER_COLS ? n : METER_COLS);
    }
    t0 = now;
    frames = 0;
}

static void __not_in_flash_func(meter_row)(uint32_t row, uint32_t *out) {
    const uint32_t box_rows = METER_ROWS * 8u + 4u, box_w = METER_COLS * 16u + 8u;
    if (row < meter_row0 || row >= meter_row0 + box_rows) return;
    const uint32_t y = row - meter_row0;
    uint32_t *o = out + meter_x0;
    if (y < 2u || y >= box_rows - 2u) {
        for (uint32_t x = 0; x < box_w; x++) o[x] = col_frame;
        return;
    }
    o[0] = o[1] = o[2] = o[3] = col_frame;
    o[box_w - 1] = o[box_w - 2] = o[box_w - 3] = o[box_w - 4] = col_frame;
    const uint32_t ty = y - 2u, r = ty >> 3, gy = ty & 7u;
    uint32_t *p = o + 4;
    for (uint32_t c = 0; c < METER_COLS; c++) {
        const uint8_t bits = osd_font[(uint8_t)meter_text[r][c] * 8u + gy];
        for (uint32_t b = 0; b < 8; b++) {
            const uint32_t v = (bits >> b) & 1u ? col_fg : col_bg;
            p[0] = p[1] = v;
            p += 2;
        }
    }
}

void __not_in_flash_func(osd_overlay_row)(uint32_t row, uint32_t *out, uint32_t width) {
    if (meter_on) meter_row(row, out);
    if (!visible) return;
    const uint32_t rows = ov_rows, cols = ov_cols, scale = ov_scale;
    const uint32_t box_rows = rows * 8u + 4u;
    if (row < box_row0 || row >= box_row0 + box_rows) return;
    const uint32_t y = row - box_row0;
    const uint32_t box_w = cols * 8u * scale + 8u;
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
    for (uint32_t c = 0; c < cols; c++) {
        const uint8_t bits = osd_font[(uint8_t)text[r][c] * 8u + gy];
        if (scale == 2) {
            for (uint32_t b = 0; b < 8; b++) {
                const uint32_t v = (bits >> b) & 1u ? fg : bg;
                p[0] = v;
                p[1] = v;
                p += 2;
            }
        } else {
            for (uint32_t b = 0; b < 8; b++) *p++ = (bits >> b) & 1u ? fg : bg;
        }
    }
    (void)width;
}

/* ── Menu (core 0) ─────────────────────────────────────────────────────── */
typedef enum { M_MAIN, M_FILES, M_FM, M_FM_ACTION } mode_t;
static mode_t mode;
static int sel, top;
static int files_target;                   /* T_DF0, T_DF1, T_KICK */
enum { T_DF0, T_DF1, T_KICK };
#define MAX_FILES 256
#define NAME_MAX_REC 257                   /* a type character and a long file name */
static char (*files)[NAME_MAX_REC];        /* names, in PSRAM */
static int nfiles;

static void put(int r, const char *s) {
    memset(text[r], ' ', FM_COLS);
    for (int c = 0; c < (int)ov_cols && s[c]; c++) text[r][c] = s[c];
}

/* The box's size and place on the screen. */
static void layout(uint32_t rows, uint32_t cols, uint32_t scale) {
    ov_rows = rows;
    ov_cols = cols;
    ov_scale = scale;
    box_x0 = (video_width() - (cols * 8u * scale + 8u)) / 2u;
    box_row0 = (video_rows() - (rows * 8u + 4u)) / 2u;
}

static const char *base(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

/* ── File manager (F11) ─────────────────────────────────────────────── */
/* Entries in `files`: a type character, then the name. */
enum { E_UP = '^', E_DIR = '/', E_DISK = 'd', E_KICK = 'k', E_OTHER = ' ' };
static char fm_dir[256] = "/amiga";        /* without the "0:" drive */
static int fm_action;

static bool ends_with(const char *s, const char *ext) {
    const size_t n = strlen(s), m = strlen(ext);
    return n > m && !strcasecmp(s + n - m, ext);
}

static int fm_cmp(const void *a, const void *b) {
    const char *x = a, *y = b;
    if ((x[0] == E_UP) != (y[0] == E_UP)) return x[0] == E_UP ? -1 : 1;
    if ((x[0] == E_DIR) != (y[0] == E_DIR)) return x[0] == E_DIR ? -1 : 1;
    return strcasecmp(x + 1, y + 1);
}

static void fm_scan(void) {
    nfiles = 0;
    if (strcmp(fm_dir, "/")) snprintf(files[nfiles++], NAME_MAX_REC, "%c..", E_UP);
    if (!media_mount()) return;
    char path[264];
    snprintf(path, sizeof path, "0:%s", fm_dir);
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, path) != FR_OK &&
        (!media_remount() || f_opendir(&d, path) != FR_OK)) return;
    while (nfiles < MAX_FILES && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & AM_HID || fi.fname[0] == '.') continue;
        char type = E_OTHER;
        if (fi.fattrib & AM_DIR) type = E_DIR;
        else if (ends_with(fi.fname, ".adf") || ends_with(fi.fname, ".scp") || ends_with(fi.fname, ".ipf")) type = E_DISK;
        else if (fi.fsize == 262144u || fi.fsize == 524288u) type = E_KICK;
        snprintf(files[nfiles++], NAME_MAX_REC, "%c%s", type, fi.fname);
    }
    f_closedir(&d);
    qsort(files, (size_t)nfiles, NAME_MAX_REC, fm_cmp);
}

static void fm_enter(const char *name) {
    const size_t n = strlen(fm_dir);
    snprintf(fm_dir + n, sizeof fm_dir - n, "%s%s", strcmp(fm_dir, "/") ? "/" : "", name);
    fm_scan();
    sel = top = 0;
}

static void fm_up(void) {
    char *p = strrchr(fm_dir, '/');
    if (!p) return;
    if (p == fm_dir) fm_dir[1] = 0;
    else *p = 0;
    fm_scan();
    sel = top = 0;
}

static const char *const disk_actions[] = {
    "Insert into DF0", "Insert into DF1", "Boot from it (DF0, reset)",
    "Boot from it and make it the power-on disk", "Cancel",
};
static const char *const kick_actions[] = {
    "Use as Kickstart (reset)", "Use as Kickstart and make it the power-on one", "Cancel",
};

static int fm_action_count(void) {
    return files[sel][0] == E_DISK ? 5 : 3;
}

static void fm_do(void) {
    char path[400];
    snprintf(path, sizeof path, "%s%s%s", fm_dir, strcmp(fm_dir, "/") ? "/" : "", files[sel] + 1);
    const bool disk = files[sel][0] == E_DISK;
    const int a = fm_action;
    if (a == fm_action_count() - 1) {           /* Cancel */
        mode = M_FM;
        return;
    }
    visible = false;                           /* show the machine while it loads */
    mode = M_FM;
    if (disk && a < 2) {
        media_insert(a, path);
        return;
    }
    if (disk) {
        snprintf(media_config.df0, sizeof media_config.df0, "0:%s", path);
    } else {
        snprintf(media_config.kick, sizeof media_config.kick, "0:%s", path);
        /* An extended ROM only belongs to AROS. */
        if (!strstr(path, "aros")) media_config.ext[0] = 0;
    }
    if ((disk && a == 3) || (!disk && a == 1)) media_save_ini();
    media_boot();
    sleep_ms(300);
    stream_start();
}

static void fm_draw(void) {
    char line[FM_COLS + 1];
    for (int r = 0; r < FM_ROWS; r++) put(r, "");
    snprintf(line, sizeof line, " File manager (F11)   %.52s", fm_dir);
    put(0, line);
    if (mode == M_FM_ACTION) {
        snprintf(line, sizeof line, " %.70s", files[sel] + 1);
        put(2, line);
        const char *const *acts = files[sel][0] == E_DISK ? disk_actions : kick_actions;
        for (int i = 0; i < fm_action_count(); i++) {
            snprintf(line, sizeof line, "   %s", acts[i]);
            put(4 + i, line);
        }
        highlight = (uint8_t)(4 + fm_action);
        put(FM_ROWS - 1, " up/down, Enter: do it   Esc: back");
        return;
    }
    for (int i = 0; i < FM_LIST && top + i < nfiles; i++) {
        const char *e = files[top + i];
        if (e[0] == E_UP) snprintf(line, sizeof line, "   ..");
        else if (e[0] == E_DIR) snprintf(line, sizeof line, "   %.71s/", e + 1);
        else snprintf(line, sizeof line, " %s %.72s", e[0] == E_DISK ? "*" : e[0] == E_KICK ? "K" : " ",
                      e + 1);
        put(2 + i, line);
    }
    if (!nfiles) put(2, "   (empty)");
    highlight = nfiles ? (uint8_t)(2 + sel - top) : 0xFF;
    snprintf(line, sizeof line, " DF0: %.32s   DF1: %.30s",
             media_config.df0[0] ? base(media_config.df0) : "(empty)",
             media_config.df1[0] ? base(media_config.df1) : "(empty)");
    put(FM_ROWS - 4, line);
    snprintf(line, sizeof line, " Kick: %.68s", base(media_config.kick));
    put(FM_ROWS - 3, line);
    put(FM_ROWS - 1, " Enter: open/choose  Backspace: up  * disk  K Kickstart  F11/Esc: close");
}

static void fm_key(uint8_t hid) {
    if (mode == M_FM_ACTION) {
        const int n = fm_action_count();
        switch (hid) {
        case 0x52: fm_action = (fm_action + n - 1) % n; break;
        case 0x51: fm_action = (fm_action + 1) % n; break;
        case 0x29: case 0x2A: mode = M_FM; break;
        case 0x28: case 0x58: fm_do(); break;
        }
        if (visible) fm_draw();
        return;
    }
    switch (hid) {
    case 0x52: if (sel > 0) sel--; break;                                  /* up */
    case 0x51: if (sel < nfiles - 1) sel++; break;                         /* down */
    case 0x4B: sel = sel > FM_LIST ? sel - FM_LIST : 0; break;             /* page up */
    case 0x4E: sel = sel + FM_LIST < nfiles ? sel + FM_LIST : (nfiles ? nfiles - 1 : 0); break;
    case 0x4A: sel = 0; break;                                             /* home */
    case 0x4D: sel = nfiles ? nfiles - 1 : 0; break;                       /* end */
    case 0x2A: fm_up(); break;                                             /* backspace */
    case 0x29: visible = false; return;                                    /* Esc */
    case 0x28: case 0x58:                                                  /* Enter */
        if (!nfiles) break;
        if (files[sel][0] == E_UP) fm_up();
        else if (files[sel][0] == E_DIR) fm_enter(files[sel] + 1);
        else if (files[sel][0] == E_DISK || files[sel][0] == E_KICK) {
            mode = M_FM_ACTION;
            fm_action = 0;
        }
        break;
    }
    if (sel < top) top = sel;
    if (sel >= top + FM_LIST) top = sel - FM_LIST + 1;
    fm_draw();
}

void osd_fm_toggle(void) {
    if (visible && (mode == M_FM || mode == M_FM_ACTION)) {
        visible = false;
        return;
    }
    visible = false;
    layout(FM_ROWS, FM_COLS, 1);
    mode = M_FM;
    fm_scan();
    sel = top = 0;
    fm_draw();
    visible = true;
}

enum { I_DF0, I_DF1, I_KICK, I_RESET, I_SAVE, I_LOAD, I_VOL, I_JOY, I_METER, I_FLOPPY, I_VIDEO, I_EXIT, I_COUNT };

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
        snprintf(line, sizeof line, " Performance meter: %s", meter_on ? "on" : "off");
        put(2 + I_METER, line);
        snprintf(line, sizeof line, " Floppy speed: %s", media_config.floppy_real ? "real" : "turbo");
        put(2 + I_FLOPPY, line);
        snprintf(line, sizeof line, " Video: %s   (Enter: restart)", media_config.ntsc ? "NTSC 60 Hz" : "PAL 50 Hz");
        put(2 + I_VIDEO, line);
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
    if (f_opendir(&d, dir) != FR_OK &&
        (!media_remount() || f_opendir(&d, dir) != FR_OK)) return;
    while (nfiles < MAX_FILES && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & (AM_DIR | AM_HID) || fi.fname[0] == '.') continue;
        snprintf(files[nfiles++], NAME_MAX_REC, "%s", fi.fname);
    }
    f_closedir(&d);
    qsort(files, (size_t)nfiles, NAME_MAX_REC, cmp);
}

static void choose_file(void) {
    if (!nfiles) return;
    char path[400];
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
    if (mode == M_FM || mode == M_FM_ACTION) {
        fm_key(hid);
        return;
    }
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
        } else if (mode == M_MAIN && sel == I_FLOPPY) {
            media_set_floppy(!media_config.floppy_real);
        } else if (mode == M_MAIN && sel == I_METER) {
            osd_meter_set(!meter_on);
            media_config.meter = meter_on;
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
        case I_METER: osd_meter_set(!meter_on); media_config.meter = meter_on; break;
        case I_FLOPPY: media_set_floppy(!media_config.floppy_real); break;
        case I_VIDEO: media_set_standard(!media_config.ntsc); break;
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
    if (visible && mode != M_MAIN && mode != M_FILES) {
        visible = false;                       /* F12 over the file manager */
    }
    visible = !visible;
    if (visible) {
        layout(ROWS, COLS, 2);
        mode = M_MAIN;
        sel = 0;
        draw();
    }
}

bool osd_active(void) { return visible; }

void osd_init(void) {
    files = (char (*)[NAME_MAX_REC])(0x11000000u + 0x100000u);   /* master PSRAM, 66 KB */
    col_fg = video_rgb12_to_888(0xEEE);
    col_bg = video_rgb12_to_888(0x125);
    col_hl_fg = video_rgb12_to_888(0x000);
    col_hl_bg = video_rgb12_to_888(0xFA0);
    col_frame = video_rgb12_to_888(0x8AF);
    layout(ROWS, COLS, 2);
    draw();
}
