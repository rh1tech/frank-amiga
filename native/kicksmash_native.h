#ifndef OMEGA_KICKSMASH_NATIVE_H
#define OMEGA_KICKSMASH_NATIVE_H
#include <stddef.h>

// Sets up the KickSmash banks from the Kickstart's directory (see
// kicksmash_native.c); rom_path may be NULL (built-in Kickstart).
void kicksmash_native_init(const char *rom_path);
// Path of a bank's ROM file: 0 if the bank has none.
int kicksmash_native_bank_path(int bank, char *out, size_t size);
// The bank's ROM was loaded and the Amiga reset into it.
void kicksmash_native_switched(int bank);

#endif
