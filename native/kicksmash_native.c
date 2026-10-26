// Native backend of the KickSmash protocol (omega/KickSmash.c): the banks
// are the *.rom files of the Kickstart's directory (OMEGA_KS_DIR, else the
// directory of OMEGA_ROM), sorted by name; kicksmash.cfg there keeps the
// switcher's settings and may map banks to files ("bank <n> <file>").
// "Switch and Reboot" loads the bank's file and resets the Amiga
// (main_native.c), as the board does, on the bank's CPU ("cpu <n> 68020").

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CPU.h"
#include "KickSmash.h"
#include "kicksmash_native.h"

static KickSmashState state;
static char ks_dir[512];

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void config_path(char *out, size_t size) {
    snprintf(out, size, "%s/kicksmash.cfg", ks_dir);
}

void kicksmash_native_init(const char *rom_path) {
    kickSmashInit(&state);
    const char *dir = getenv("OMEGA_KS_DIR");
    const char *rom_file = rom_path;
    if (dir) {
        snprintf(ks_dir, sizeof ks_dir, "%s", dir);
    } else if (rom_path && strrchr(rom_path, '/')) {
        snprintf(ks_dir, sizeof ks_dir, "%.*s",
                 (int)(strrchr(rom_path, '/') - rom_path), rom_path);
    } else {
        snprintf(ks_dir, sizeof ks_dir, ".");
    }
    if (rom_path && strrchr(rom_path, '/'))
        rom_file = strrchr(rom_path, '/') + 1;

    // Default banks: the directory's ROM files, sorted.
    char *names[64];
    int count = 0;
    DIR *d = opendir(ks_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && count < 64) {
            const size_t n = strlen(e->d_name);
            if (n > 4 && n < KS_FILE_MAX &&
                !strcasecmp(e->d_name + n - 4, ".rom"))
                names[count++] = strdup(e->d_name);
        }
        closedir(d);
    }
    qsort(names, (size_t)count, sizeof names[0], by_name);
    kickSmashDefaultBanks((const char *const *)names, count);
    for (int i = 0; i < count; i++)
        free(names[i]);

    int bank = kickSmashFindBank(rom_file);
    state.bi.bank_poweron = bank >= 0 ? (uint8_t)bank : 0;

    char path[600];
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) {
        static char text[4096];
        const size_t got = fread(text, 1, sizeof text - 1, f);
        text[got] = '\0';
        fclose(f);
        kickSmashParseConfig(text);
        bank = kickSmashFindBank(rom_file);
    }
    state.bi.bank_current = bank >= 0 ? (uint8_t)bank : 0xff;
    cpu_select_68020(kickSmashBankCpu020(bank));
}

int kicksmash_native_bank_path(int bank, char *out, size_t size) {
    if (bank < 0 || bank >= KS_ROM_BANKS || !state.file[bank][0])
        return 0;
    snprintf(out, size, "%s/%s", ks_dir, state.file[bank]);
    return 1;
}

void kicksmash_native_switched(int bank) {
    state.bi.bank_current = (uint8_t)bank;
    state.bi.bank_nextreset = 0xff;
    cpu_select_68020(kickSmashBankCpu020(bank));
}

void kickSmashHostSettingsChanged(void) {
    char text[2048], path[600];
    const int n = kickSmashFormatConfig(text, sizeof text);
    config_path(path, sizeof path);
    FILE *f = n > 0 ? fopen(path, "w") : NULL;
    if (!f) {
        printf("[KickSmash] cannot save %s\n", path);
        return;
    }
    fwrite(text, 1, (size_t)n, f);
    fclose(f);
    printf("[KickSmash] settings saved to %s\n", path);
}
