// Boot-time setup of the Amiga memory map (Memory.c). Kept apart from
// Memory.c, whose code is copied to SRAM (src/ld/default_text_excludes.incl):
// this runs once, so it stays in flash.

#include "Memory.h"
#include "psram.h"
#include "../omega/Expansion.h"
#if OMEGA_RTG
#include "../omega/Picasso2.h"
#endif
#include "hardware/regs/addressmap.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Memory.c's state.
extern uint8_t *chip_ram;
extern const uint8_t *rom_base;
extern uint32_t rom_size;
extern const uint8_t *chip_fetch_page[];
#if OMEGA_FAST_RAM
extern uint32_t fast_ram_size;
#else
static uint32_t fast_ram_size;          // no Fast RAM board: stays 0
#endif
#if OMEGA_RTG
extern uint32_t vram_from;
#else
static uint32_t vram_from;              // no RTG card: unused
#endif

// frank-amiga: an extended ROM at 0xE00000-0xE7FFFF (AROS splits its
// Kickstart in two 512 KB halves), or NULL.
const uint8_t *ext_rom_base;
static void memory_map_pages(void);

void memory_set_ext_rom(const uint8_t *data) {
    ext_rom_base = data;
    memory_map_pages();
}

#define PAGE_SHIFT 16
#define PAGE_COUNT (0x1000000u >> PAGE_SHIFT)

// One entry per 64 KB page: chip RAM, Fast RAM, the ROM with its 256 KB
// mirror, or NULL (see fetch_ptr() in Memory.c).
static void memory_map_pages(void) {
    for (uint32_t p = 0; p < PAGE_COUNT; ++p) {
        const uint32_t a = p << PAGE_SHIFT;
        const uint8_t *mem = NULL;
        if (a <= CHIPTOP) {
            mem = chip_ram + a;
        } else if (a - 0x200000u < fast_ram_size) {
            mem = chip_ram + a;     // Fast RAM follows chip RAM in PSRAM
        } else if (a >= 0xE00000u && a < 0xE80000u && ext_rom_base) {
            mem = ext_rom_base + (a - 0xE00000u);
        } else if (a >= 0xF80000u && rom_base) {
            uint32_t offset = a - 0xF80000u;
            if (rom_size <= 0x40000u) offset &= 0x3FFFFu;
            mem = rom_base + offset;
        }
        chip_fetch_page[p] = mem;
    }
}

void memory_init(void) {
    chip_ram  = psram_ptr(PSRAM_CHIPRAM_OFFSET);

    // ROM lives in flash; the user must place the Kickstart binary at
    // flash offset 0x200000 (i.e. absolute address ROM_FLASH_BASE).
    rom_base = (const uint8_t *)ROM_FLASH_BASE;
    rom_size = ROM_SIZE;

    // Validate ROM presence: first byte of a valid Kickstart is 0x11
    if (rom_base[0] != 0x11) {
        // ROM not found – ROM region will return 0 / open bus
        rom_base = NULL;
        rom_size = 0;
        printf("ROM: no Kickstart header at flash 0x%08x\n",
               (unsigned)ROM_FLASH_BASE);
    } else {
        uint16_t opcode = (uint16_t)(rom_base[2] << 8 | rom_base[3]);
        uint32_t entry = (uint32_t)rom_base[4] << 24 |
                         (uint32_t)rom_base[5] << 16 |
                         (uint32_t)rom_base[6] << 8 | rom_base[7];
        printf("ROM: flash 0x%08x, header=%02x%02x opcode=%04x entry=%08lx\n",
               (unsigned)ROM_FLASH_BASE, rom_base[0], rom_base[1], opcode,
               (unsigned long)entry);
    }

    memory_map_pages();
    expansionFastRamSize = BOARD_MAP_FAST_RAM_SIZE;
#if OMEGA_RTG
    // The card is offered to autoconfig only for KickSmash banks with
    // "rtg <n> on" (main.c, load_bank_from_sd()).
    p2Init(psram_ptr(BOARD_MAP_RTG_VRAM_OFFSET), BOARD_MAP_RTG_VRAM_SIZE,
           psram_ptr(BOARD_MAP_RTG_STATE_OFFSET));
    expansionPicasso2Vram = 0;
#endif

    printf("Memory: clearing chip RAM\n");
    memory_clear_chipram();
    printf("Memory: PSRAM clear complete\n");
}

int memory_set_rom(const uint8_t *data, uint32_t size) {
    if (!data || (size != 0x40000u && size != 0x80000u) || data[0] != 0x11) {
        return 0;
    }
    rom_base = data;
    rom_size = size;
    memory_map_pages();
    return 1;
}

void memory_clear_chipram(void) {
    // Leave longword at 0x000000 (Guru indicator) intact; clear from 4 up
    memset(chip_ram + 4, 0, PSRAM_CHIPRAM_SIZE - 4);
}

// Autoconfig placed a board at base (Expansion.c), or the reset line
// unmapped it (base 0). The Fast RAM is PSRAM at BOARD_MAP_FAST_RAM_OFFSET,
// right after chip RAM, and the RTG card's VRAM follows it, so Memory.c
// reaches both through chip_ram as one window: only the bases Kickstart
// gives them, 0x200000 and 0x400000, are taken.
_Static_assert(BOARD_MAP_FAST_RAM_OFFSET ==
               BOARD_MAP_CHIPRAM_OFFSET + 0x200000u,
               "Memory.c expects Fast RAM right after chip RAM in PSRAM");
_Static_assert(BOARD_MAP_RTG_VRAM_OFFSET ==
               BOARD_MAP_CHIPRAM_OFFSET + 0x400000u,
               "Memory.c expects the VRAM at Amiga address 0x400000 in PSRAM");
static uint32_t fast_base, vram_base, regs_base;

void expansionHostMap(int board, uint32_t base) {
    switch (board) {
    case EXPANSION_FAST_RAM: fast_base = base; break;
#if OMEGA_RTG
    case EXPANSION_P2_VRAM:  vram_base = base; break;
    case EXPANSION_P2_REGS:
        regs_base = base;
        if (!base)
            p2Reset();
        return;
#endif
    default: return;
    }
    if (base && base != (board == EXPANSION_FAST_RAM ? 0x200000u : 0x400000u))
        printf("Expansion: board %d at 0x%06lx not mapped\n", board,
               (unsigned long)base);
    const int fast = fast_base == 0x200000u &&
                     expansionFastRamSize <= BOARD_MAP_FAST_RAM_SIZE;
    const int vram = fast && expansionFastRamSize == 0x200000u &&
                     vram_base == 0x400000u;
    fast_ram_size = fast ? expansionFastRamSize + (vram ? expansionPicasso2Vram : 0)
                         : 0;
    vram_from = vram ? 0x400000u : 0xFFFFFFFFu;
    memory_map_pages();
}

#if OMEGA_RTG
// VRAM pages (4 KB) the CPU or a blit changed since the display last
// copied them (rtg_display.c).
uint32_t rtg_dirty[BOARD_MAP_RTG_VRAM_SIZE >> 17];

void p2HostDirty(uint32_t offset, uint32_t length) {
    if (!length)
        return;
    // Core 1 reads VRAM uncached (rtg_display.c), so the new bytes are
    // written out of the XIP cache (write-back) first: a "clean by address"
    // per 8-byte line at the maintenance alias. Only then the pages are
    // marked, or core 1 could copy stale bytes and clear the mark.
    const uintptr_t base = PSRAM_BASE - XIP_BASE + BOARD_MAP_RTG_VRAM_OFFSET;
    for (uint32_t a = offset & ~7u; a < offset + length; a += 8u)
        *(volatile uint8_t *)(XIP_MAINTENANCE_BASE + base +
                              (a & (BOARD_MAP_RTG_VRAM_SIZE - 1)) + 3u) = 0;
    __asm volatile("dsb" ::: "memory");
    const uint32_t pages = (uint32_t)sizeof rtg_dirty * 8u;
    uint32_t first = offset >> 12, last = (offset + length - 1) >> 12;
    if (last - first >= pages)
        last = first + pages - 1;
    for (uint32_t p = first; p <= last; p++)
        rtg_dirty[(p % pages) >> 5] |= 1u << (p & 31);
}

void memory_vram_write(unsigned int address, unsigned int value, int size) {
    if (p2CpuBlt) {
        // a blit's source data, most significant byte first
        for (int i = size - 1; i >= 0; i--)
            p2BltFeed((uint8_t)(value >> (8 * i)));
        return;
    }
    for (int i = 0; i < size; i++)
        chip_ram[address + i] = (uint8_t)(value >> (8 * (size - 1 - i)));
    p2HostDirty(address - 0x400000u, (uint32_t)size);
}

static int in_regs(unsigned int address) {
    return regs_base && address - regs_base < P2_REGS_SIZE;
}
#else
// Without the RTG card: no registers to answer.
#define in_regs(address) 0
#define p2RegRead(offset) 0u
#define p2RegWrite(offset, value) ((void)0)
#endif

unsigned int memory_expansion_read(unsigned int address) {
    if (ext_rom_base && address >= 0xE00000u && address < 0xE80000u)
        return ext_rom_base[address - 0xE00000u];
    if (in_regs(address))
        return p2RegRead(address - regs_base);
    const int value = address >= EXPANSION_BASE && address < EXPANSION_END
                    ? expansionReadByte(address) : -1;
    return value < 0 ? 0 : (unsigned int)value;
}

unsigned int memory_expansion_read_word(unsigned int address) {
    if (ext_rom_base && address >= 0xE00000u && address < 0xE80000u)
        return (unsigned int)ext_rom_base[address - 0xE00000u] << 8 |
               ext_rom_base[address + 1u - 0xE00000u];
    if (in_regs(address))
        return p2RegRead(address - regs_base) << 8 |
               p2RegRead(address + 1 - regs_base);
    return memory_expansion_read(address) << 8;
}

void memory_expansion_write(unsigned int address, unsigned int value) {
    if (in_regs(address))
        p2RegWrite(address - regs_base, (uint8_t)value);
    else if (address >= EXPANSION_BASE && address < EXPANSION_END)
        expansionWriteByte(address, (uint8_t)value);
}

void memory_expansion_write_word(unsigned int address, unsigned int value) {
    if (in_regs(address)) {
        p2RegWrite(address - regs_base, (uint8_t)(value >> 8));
        p2RegWrite(address + 1 - regs_base, (uint8_t)value);
    } else {
        memory_expansion_write(address, value >> 8);
    }
}
