//
//  Expansion.h
//  Omega
//
//  Zorro II autoconfig at 0xE80000. The boards answer one after another,
//  in this order: Fast RAM, then the Picasso II+'s VRAM and register
//  boards (omega/Picasso2.c). Kickstart's expansion code reads each
//  board's description and gives it a base address: 0x200000 for 2 MB of
//  Fast RAM, then 0x400000 for the VRAM, 0xE90000 for the registers. The
//  host keeps the memory and maps each board (expansionHostMap).
//

#ifndef Expansion_h
#define Expansion_h

#include <stdint.h>

#define EXPANSION_BASE 0xE80000u
#define EXPANSION_END  0xE90000u

enum {
    EXPANSION_FAST_RAM,     // expansionFastRamSize bytes, in the memory list
    EXPANSION_P2_VRAM,      // Picasso II+ VRAM, 1 or 2 MB (2167/11)
    EXPANSION_P2_REGS,      // Picasso II+ registers, 64 KB (2167/12)
    EXPANSION_BOARDS
};

// The boards present, set by the host before the first reset: the Fast
// RAM board's size (a power of two from 64 KB to 8 MB, or 0 for none) and
// the Picasso II+'s VRAM (1 or 2 MB, or 0 for no card).
extern uint32_t expansionFastRamSize;
extern uint32_t expansionPicasso2Vram;

// The reset line: every board is unconfigured again and unmapped (memory
// contents stay, as on real hardware).
void expansionReset(void);
// A byte read in 0xE80000-0xE8FFFF: the register of the board waiting for
// its address, or -1 when none answers.
int expansionReadByte(uint32_t address);
// A byte write there (er_BaseAddress at 0x48 configures, 0x4C shuts up).
void expansionWriteByte(uint32_t address, uint8_t value);
// The board's base address (0: not configured).
uint32_t expansionBoardBase(int board);

// Host side: board was given base, or unmapped (base 0, at a reset).
void expansionHostMap(int board, uint32_t base);

#endif /* Expansion_h */
