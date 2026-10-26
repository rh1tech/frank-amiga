// Memory interface for the RP2350 PSRAM port of Omega.
// Provides the same API as the original Memory.h so all Omega source files
// compile unchanged.  The backing store is 8 MB PSRAM instead of a 16 MB
// static array.
//
// Amiga address-to-PSRAM mapping
//   0x000000–0x1FFFFF  → PSRAM chip RAM   (2 MB)
//   0xC00000–0xD7FFFF  → custom register mirror (no slow RAM, as an A500
//                        without trapdoor RAM)
//   0xF80000–0xFFFFFF  → Flash ROM        (read-only, XIP at ROM_FLASH_BASE)
//
// Everything else returns 0 / is silently discarded on writes.

#pragma once
#include <stdint.h>

#ifdef PICO_BUILD
#include "board_config.h"
#endif

// ── chip RAM size ─────────────────────────────────────────────────────────
#define CHIPTOP 0x1FFFFFu   // 2 MB chip RAM top

// ── ROM source ────────────────────────────────────────────────────────────
// Kickstart ROM is stored in flash starting at this absolute address.
// Place the ROM file at flash offset 0x200000 (2 MB from flash start) in
// your CMakeLists.txt / UF2 combined image so it lands at 0x10200000.
#ifdef PICO_BUILD
#define ROM_FLASH_BASE  BOARD_ROM_FLASH_BASE
#define ROM_SIZE        BOARD_ROM_SIZE
#else
#define ROM_FLASH_BASE  0x10200000u
#define ROM_SIZE        0x080000u   // 512 KB max (256 KB ROMs are mirrored)
#endif

// ── Direct chip RAM access helpers (used by Chipset.c and DMA.c) ─────────
// On desktop, chip RAM is the start of the low16Meg array.
// On RP2350, it is memory-mapped PSRAM.
#ifdef PICO_BUILD
#include "psram.h"
// Byte pointer to the start of chip RAM (PSRAM-backed, memory-mapped)
#define CHIPRAM_BASE_PTR  ((uint8_t*)(PSRAM_BASE + PSRAM_CHIPRAM_OFFSET))
#else
// low16Meg is declared by the desktop Memory.c
extern unsigned char low16Meg[16777216];
#define CHIPRAM_BASE_PTR  ((uint8_t*)low16Meg)
#endif

// ── public API ───────────────────────────────────────────────────────────
// Called once at startup; clears chip RAM and mirrors ROM if needed.
void memory_init(void);
// Select a 256 KB or 512 KB Kickstart image already resident in memory.
// Returns 1 when accepted; a 256 KB image is mirrored by address decoding.
int memory_set_rom(const uint8_t *data, uint32_t size);
// Wipe chip RAM from address 4 upward (called on 68k RESET)
void memory_clear_chipram(void);

// Raw Amiga address read/write used by the CPU and DMA
unsigned int chipReadByte(unsigned int address);
unsigned int chipReadWord(unsigned int address);
unsigned int chipReadLong(unsigned int address);
// Instruction fetch for Musashi's prefetch (RP2350): a page-table lookup
// (chip RAM, ROM), anything else through chipReadWord/Long.
unsigned int chipFetchWord(unsigned int address);
unsigned int chipFetchLong(unsigned int address);

#ifdef PICO_BUILD
// Host address of each 64 KB page of plain memory, NULL elsewhere (see
// Memory.c). chipFetchLongInline() reads code through it without a call;
// Musashi's prefetch uses it (m68kconf.h).
extern const uint8_t *chip_fetch_page[0x100];
static inline unsigned int chipFetchLongInline(unsigned int address) {
    const uint8_t *page =
        address < 0x1000000u ? chip_fetch_page[address >> 16] : (const uint8_t *)0;
    if (page) {
        uint32_t v;
        __builtin_memcpy(&v, page + (address & 0xFFFFu), sizeof(v));
        return __builtin_bswap32(v);
    }
    return chipFetchLong(address);
}
#endif

#ifdef PICO_BUILD
// Autoconfig space (0xE00000-0xF7FFFF): the register byte of the board
// waiting for its address or of the RTG card, else 0; writes configure
// the boards (MemoryBoot.c, kept out of SRAM). A word reaches the card's
// even port with its high byte and the odd one with its low byte.
unsigned int memory_expansion_read(unsigned int address);
unsigned int memory_expansion_read_word(unsigned int address);
void memory_expansion_write(unsigned int address, unsigned int value);
void memory_expansion_write_word(unsigned int address, unsigned int value);
// A CPU write of `size` bytes to the RTG card's VRAM.
void memory_vram_write(unsigned int address, unsigned int value, int size);
#endif

void chipWriteByte(unsigned int address, unsigned int value);
void chipWriteWord(unsigned int address, unsigned int value);
void chipWriteLong(unsigned int address, unsigned int value);
