// PSRAM-backed Amiga memory for the RP2350 port of Omega.
// Replaces the original Memory.c (which used a 16 MB static array).
//
// Address translation:
//   Amiga 0x000000–0x1FFFFF → PSRAM[0x000000] (chip RAM, 2 MB)
//   Amiga 0x200000–0x3FFFFF → PSRAM[0x200000] (Fast RAM, once autoconfig
//                             has placed the Zorro II board there)
//   Amiga 0x400000–0x4FFFFF → PSRAM[0x400000] (the RTG card's VRAM)
//   0xE90000–0xE9FFFF       → its registers (omega/Picasso2.c)
//   0xE80000–0xE8FFFF       → autoconfig (omega/Expansion.c, MemoryBoot.c)
//   0xC00000–0xD7FFFF       → custom register mirror (no slow RAM, as an
//                             A500 without trapdoor RAM)
//   Amiga 0xF80000–0xFFFFFF → Flash ROM at ROM_FLASH_BASE (read-only)
//   0xBFD000–0xBFFFFF       → CIA (handled via CIARead/CIAWrite)
//   0xDFF000–0xDFF1FF       → Custom chipset registers
//   0xDA0000–0xDAFFFF       → Gayle IDE (Amiga 600/1200)
//   Everything else         → open bus (reads 0, writes dropped)

#include "Memory.h"
#include "psram.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/debug.h"
#include "../omega/DMA.h"
#include "../omega/Gayle.h"
#include "../omega/KickSmash.h"
#include <string.h>
#include <stdint.h>

// ── PSRAM region base pointers (set up by MemoryBoot.c) ───────────────────
uint8_t *chip_ram;          // PSRAM + PSRAM_CHIPRAM_OFFSET

// Fast RAM, once autoconfig has placed it at 0x200000 (MemoryBoot.c): it
// follows chip RAM in PSRAM, so chip_ram[a] is its byte too. 0: none. The
// RTG card's VRAM follows at 0x400000 and is part of this window: from
// vram_from on, writes go through memory_vram_write() (a blit may take
// them as its source; the display copies what changed).
#if OMEGA_FAST_RAM
uint32_t fast_ram_size;
#define IN_FAST_RAM(a) ((a) - 0x200000u < fast_ram_size)
#else
#define IN_FAST_RAM(a) 0
#endif
#if OMEGA_RTG
uint32_t vram_from = 0xFFFFFFFFu;
#define IN_VRAM(a) __builtin_expect((a) >= vram_from, 0)
#else
#define IN_VRAM(a) 0
#endif

// ── ROM (read-only: flash via XIP, or the SD card's copy in PSRAM) ────────
const uint8_t *rom_base;
extern const uint8_t *ext_rom_base;
uint32_t       rom_size;

// ── Helpers ───────────────────────────────────────────────────────────────

// Big-endian word stored in RAM ↔ little-endian ARM read/write helpers.
// One (possibly unaligned) load or store plus REV/REV16 instead of separate
// byte accesses; Cortex-M33 permits unaligned access to XIP/PSRAM and SRAM.
static inline uint16_t ram_read_word(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap16(v);
}
static inline uint32_t ram_read_long(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
}
static inline void ram_write_word(uint8_t *p, uint16_t v) {
    v = __builtin_bswap16(v);
    memcpy(p, &v, sizeof(v));
}
static inline void ram_write_long(uint8_t *p, uint32_t v) {
    v = __builtin_bswap32(v);
    memcpy(p, &v, sizeof(v));
}

// frank-amiga: chip RAM lives in 64 KB banks (chip_bank[], Memory.h), some
// in SRAM and the rest in PSRAM. A word or long that straddles two banks
// (an odd or 0xFFFE-0xFFFF offset) is assembled from bytes.
static inline uint16_t chip_read_word(uint32_t a) {
    if ((a & 0xFFFFu) <= 0xFFFEu) return ram_read_word(chip_ptr(a));
    return (uint16_t)(*chip_ptr(a) << 8 | *chip_ptr(a + 1u));
}
static inline uint32_t chip_read_long(uint32_t a) {
    if ((a & 0xFFFFu) <= 0xFFFCu) return ram_read_long(chip_ptr(a));
    return (uint32_t)chip_read_word(a) << 16 | chip_read_word(a + 2u);
}
static inline void chip_write_word(uint32_t a, uint16_t v) {
    if ((a & 0xFFFFu) <= 0xFFFEu) { ram_write_word(chip_ptr(a), v); return; }
    *chip_ptr(a) = (uint8_t)(v >> 8);
    *chip_ptr(a + 1u) = (uint8_t)v;
}
static inline void chip_write_long(uint32_t a, uint32_t v) {
    if ((a & 0xFFFFu) <= 0xFFFCu) { ram_write_long(chip_ptr(a), v); return; }
    chip_write_word(a, (uint16_t)(v >> 16));
    chip_write_word(a + 2u, (uint16_t)v);
}

// ── ROM helper: 256 KB ROM mirrored to fill 512 KB slot ──────────────────
static inline uint32_t rom_addr(uint32_t amiga_addr) {
    uint32_t offset = amiga_addr - 0xF80000u;
    // If only a 256 KB ROM (0x40000), mirror it twice
    if (rom_size <= 0x40000u) offset &= 0x3FFFFu;
    return offset;
}
static inline const uint8_t *rom_ptr(uint32_t amiga_addr) {
    return rom_base + rom_addr(amiga_addr);
}

// ── Page table for instruction fetches (filled by MemoryBoot.c) ──────────
// One entry per 64 KB page of the 24-bit 68000 address space: the host
// address of the page's first byte where the page is plain memory (chip
// RAM, ROM with its 256 KB mirror), NULL elsewhere (registers, CIAs, Gayle,
// unmapped). An instruction fetch then costs one lookup wherever the code
// runs. Data accesses keep their range checks: a lookup in front of them
// slowed register accesses (idle screens -1 %) without helping.
#define PAGE_SHIFT 16
#define PAGE_COUNT (0x1000000u >> PAGE_SHIFT)
const uint8_t *chip_fetch_page[PAGE_COUNT];

// Host pointer for a 68000 address in plain memory, or NULL.
static inline const uint8_t *fetch_ptr(uint32_t address) {
    if (address >= 0x1000000u)
        return NULL;
    const uint8_t *page = chip_fetch_page[address >> PAGE_SHIFT];
    return page ? page + (address & ((1u << PAGE_SHIFT) - 1)) : NULL;
}

// ── 0xC00000–0xD7FFFF: custom register mirror ───────────────────────────
// There is no slow (trapdoor/Ranger) RAM: 2 MB chip RAM is enough. As on an
// A500 without it, the custom chips answer in this range with their
// registers repeated every 512 bytes; Kickstart's memory sizing tells the
// mirror from RAM by writing INTENA through it and reading INTENAR.
#define MIRROR_START 0xC00000u
#define MIRROR_END   0xD80000u
static inline int in_custom_mirror(uint32_t address) {
    return address >= MIRROR_START && address < MIRROR_END;
}
static inline uint32_t custom_mirror(uint32_t address) {
    return 0xDFF000u | (address & 0x1FFu);
}

// ── chipReadByte ──────────────────────────────────────────────────────────
unsigned int chipReadByte(unsigned int address) {
    // Chip RAM is by far the most common target; test it first.
    if (address <= CHIPTOP) return *chip_ptr(address);
    if (IN_FAST_RAM(address)) return chip_ram[address];
    // ROM
    if (address >= 0xF80000u) {
        if (!rom_base) return 0;
        return rom_ptr(address)[0];
    }
    // Autoconfig / Z2 space above chipset
    if (address > 0xDFFFFFu) return memory_expansion_read(address);
    // Custom chipset registers
    if (address > 0xDFEFFFu) {
        uint32_t off = address - 0xDFF000u;
        debugChipAddress = off;
        if (off >= 32u) return 0;
        return getChipReg8[off]();
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) return readGayleB(address);
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu)
        return in_custom_mirror(address) ? chipReadByte(custom_mirror(address)) : 0;
    // CIA A
    if (address >= 0xBFE001u) return CIARead(&CIAA, (address - 0xBFE001u) >> 8);
    // CIA B
    if (address >= 0xBFD000u) return CIARead(&CIAB, (address - 0xBFD000u) >> 8);
    // Chip RAM (2 MB, address wraps)
    address &= CHIPTOP;
    return *chip_ptr(address);
}

// ── chipReadWord ─────────────────────────────────────────────────────────
unsigned int chipReadWord(unsigned int address) {
    if (address <= CHIPTOP) return chip_read_word(address);
    if (IN_FAST_RAM(address)) return ram_read_word(&chip_ram[address]);
    // ROM
    if (address > 0xF7FFFFu) {
        if (!rom_base) return 0;
        return ram_read_word(rom_ptr(address));
    }
    // Autoconfig space
    if (address > 0xDFFFFFu) return memory_expansion_read_word(address);
    // Custom chipset registers
    if (address > 0xDFEFFFu) {
        uint32_t off = (address - 0xDFF000u) >> 1;
        // Only a handful of registers are readable; DeniseID is the fallback
        if (off > 16) {
            if (off == 62) return chipset.deniseid;
            debugChipAddress = off;
            return chipset.deniseid;
        }
        debugChipAddress = off;
        return getChipReg16[off]();
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) return readGayle(address);
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu)
        return in_custom_mirror(address) ? chipReadWord(custom_mirror(address)) : 0;
    // CIA A/B
    if (address >= 0xBFE001u) return CIARead(&CIAA, (address - 0xBFE001u) >> 8);
    if (address >= 0xBFD000u) return CIARead(&CIAB, (address - 0xBFD000u) >> 8);
    // 24-bit expansion fast RAM (not present → open bus)
    if (address > 0x1FFFFFu) return 0;
    // Chip RAM
    address &= CHIPTOP;
    return chip_read_word(address);
}

// ── chipReadLong ─────────────────────────────────────────────────────────
unsigned int chipReadLong(unsigned int address) {
    if (address <= CHIPTOP) return chip_read_long(address);
    if (IN_FAST_RAM(address)) return ram_read_long(&chip_ram[address]);
    // ROM (and the KickSmash ROM switcher's messages: omega/KickSmash.c)
    if (address > 0xF7FFFFu) {
        if (__builtin_expect(kickSmashWatches(address), 0)) {
            uint32_t value;
            if (kickSmashReadLong(address, &value)) return value;
        }
        if (!rom_base) return 0;
        return ram_read_long(rom_ptr(address));
    }
    // frank-amiga: the extended ROM (MemoryBoot.c), before the custom
    // registers' range swallows it.
    if (address >= 0xE00000u && address < 0xE80000u && ext_rom_base)
        return ram_read_long(&ext_rom_base[address - 0xE00000u]);
    // Custom chipset registers (32-bit = two 16-bit reads)
    if (address > 0xDFEFFFu) {
        uint32_t off = (address - 0xDFF000u) >> 1;
        // getChipReg32 covers only the first 16 registers; everything else
        // (including autoconfig space) reads 0 like its noReadL entries.
        if (off >= 16u) return 0;
        debugChipAddress = off;
        return getChipReg32[off]();
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) return readGayleL(address);
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu)
        return in_custom_mirror(address) ? chipReadLong(custom_mirror(address)) : 0;
    // CIA A/B
    if (address >= 0xBFE001u) return CIARead(&CIAA, (address - 0xBFE001u) >> 8);
    if (address >= 0xBFD000u) return CIARead(&CIAB, (address - 0xBFD000u) >> 8);
    // 24-bit fast RAM
    if (address > 0x1FFFFFu) return 0;
    // Chip RAM
    address &= CHIPTOP;
    return chip_read_long(address);
}

// ── chipWriteByte ─────────────────────────────────────────────────────────
void chipWriteByte(unsigned int address, unsigned int value) {
    if (address <= CHIPTOP) { *chip_ptr(address) = (uint8_t)value; return; }
    if (IN_FAST_RAM(address)) {
        if (IN_VRAM(address))
            memory_vram_write(address, value, 1);
        else
            chip_ram[address] = (uint8_t)value;
        return;
    }
    if (address >= 0xF80000u) return;  // ROM: no-op
    if (address > 0xDFFFFFu) {         // autoconfig
        memory_expansion_write(address, value);
        return;
    }
    // Custom chipset: the 68000 drives a byte on both halves of the data
    // bus and the custom chips latch the whole word, so a byte write sets
    // the register to the byte twice (DiagROM's raster test writes COLOR00's
    // low byte: 0x47 -> 0x4747, colour 0x747).
    if (address > 0xDFEFFFu) {
        chipWriteWord(address & ~1u, (value & 0xFFu) * 0x0101u);
        return;
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) { writeGayleB(address, value); return; }
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu) {
        if (in_custom_mirror(address))
            chipWriteByte(custom_mirror(address), value);
        return;
    }
    // CIA A
    if (address >= 0xBFE001u) { CIAWrite(&CIAA, (address - 0xBFE001u) >> 8, (uint8_t)value); return; }
    // CIA B
    if (address >= 0xBFD000u) { CIAWrite(&CIAB, (address - 0xBFD000u) >> 8, (uint8_t)value); return; }
    // Alternative CIA A mirror seen in KS3
    if (address >= 0xBFA001u) { CIAWrite(&CIAA, (address - 0xBFA001u) >> 8, (uint8_t)value); return; }
    // Chip RAM
    if (address > 0x1FFFFFu) return;
    address &= CHIPTOP;
    *chip_ptr(address) = (uint8_t)value;
}

// ── chipWriteWord ─────────────────────────────────────────────────────────
void chipWriteWord(unsigned int address, unsigned int value) {
    if (address <= CHIPTOP) {
        chip_write_word(address, (uint16_t)value);
        return;
    }
    if (IN_FAST_RAM(address)) {
        if (IN_VRAM(address))
            memory_vram_write(address, value, 2);
        else
            ram_write_word(&chip_ram[address], (uint16_t)value);
        return;
    }
    if (address >= 0xF80000u) return;
    if (address > 0xDFFFFFu) {         // autoconfig, the RTG card's registers
        memory_expansion_write_word(address, value);
        return;
    }
    // Custom chipset registers
    if (address > 0xDFEFFFu) {
        uint32_t off = (address - 0xDFF000u) >> 1;
        // The tables cover 0xDFF000..0xDFF1FF. A runaway 68k can write
        // anywhere up to the ROM; ignore it rather than jump through a
        // pointer read past the table.
        if (off >= 256u) return;
        debugChipAddress = off;
        debugChipValue   = value;
        dmaIdleCacheValid = 0;
        putChipReg16[off]((uint16_t)value);
        return;
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) { writeGayle(address, value); return; }
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu) {
        if (in_custom_mirror(address))
            chipWriteWord(custom_mirror(address), value);
        return;
    }
    // CIA: word writes to CIA space are no-ops (byte-wide chips)
    if (address >= 0xBFD000u) return;
    // Chip RAM
    if (address > 0x1FFFFFu) return;
    address &= CHIPTOP;
    chip_write_word(address, (uint16_t)value);
}

// ── chipWriteLong ─────────────────────────────────────────────────────────
void chipWriteLong(unsigned int address, unsigned int value) {
    if (address <= CHIPTOP) {
        chip_write_long(address, value);
        return;
    }
    if (IN_FAST_RAM(address)) {
        if (IN_VRAM(address))
            memory_vram_write(address, value, 4);
        else
            ram_write_long(&chip_ram[address], value);
        return;
    }
    if (address >= 0xF80000u) return;
    // Custom chipset registers
    if (address > 0xDFEFFFu) {
        uint32_t off = (address - 0xDFF000u) >> 1;
        if (off >= 256u) return;    // see chipWriteWord
        debugChipAddress = off;
        dmaIdleCacheValid = 0;
        putChipReg32[off](value);
        return;
    }
    // Gayle/IDE
    if (address > 0xD9FFFFu) { writeGayleL(address, value); return; }
    // Custom register mirror (no slow RAM)
    if (address > 0xBFFFFFu) {
        if (in_custom_mirror(address))
            chipWriteLong(custom_mirror(address), value);
        return;
    }
    // CIA: no long writes
    if (address >= 0xBFD000u) return;
    // 24-bit fast RAM area (not present)
    if (address > 0x1FFFFFu) return;
    // Chip RAM
    address &= CHIPTOP;
    chip_write_long(address, value);
}

// ── Instruction fetch (Musashi prefetch) ─────────────────────────────────
// Code runs from ROM, chip or Fast RAM: one page-table lookup; anything
// else takes the general data path.
unsigned int chipFetchLong(unsigned int address) {
    const uint8_t *p = fetch_ptr(address);
    if (p)
        return ram_read_long(p);
    return chipReadLong(address);
}

unsigned int chipFetchWord(unsigned int address) {
    const uint8_t *p = fetch_ptr(address);
    if (p)
        return ram_read_word(p);
    return chipReadWord(address);
}
