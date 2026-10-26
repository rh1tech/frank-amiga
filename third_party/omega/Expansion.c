//
//  Expansion.c
//  Omega
//
//  Zorro II autoconfig (see Expansion.h). A board describes itself in
//  nibbles: each register byte is read as two words, its high nibble at
//  offset 4n and its low nibble at 4n+2, in data bits 15-12. All registers
//  but er_Type (0x00) and the control registers (0x40 up) read inverted.
//  The boards answer in turn: the next one appears when the current one is
//  configured or shut up.
//

#include "Expansion.h"
#include <stdio.h>

uint32_t expansionFastRamSize;
uint32_t expansionPicasso2Vram;

#define VILLAGE_TRONIC 2167u
#define OMEGA_MFR      0x1212u      // unregistered, "Omega"

typedef struct {
    uint32_t size;          // address space
    uint16_t mfr;
    uint8_t  product;
    uint8_t  memlist;       // ERTF_MEMLIST: Kickstart adds it as free memory
    uint8_t  flags;         // er_Flags
    uint32_t serial;
} BoardDesc;

static BoardDesc boards[EXPANSION_BOARDS];
static uint32_t base[EXPANSION_BOARDS];
static int current;         // the board answering at 0xE80000, or -1

static int present(int board) {
    switch (board) {
    case EXPANSION_FAST_RAM: return expansionFastRamSize != 0;
    case EXPANSION_P2_VRAM:
    case EXPANSION_P2_REGS:  return expansionPicasso2Vram != 0;
    }
    return 0;
}

static void describe(void) {
    // Fast RAM: memory, preferring the 8 MB space.
    boards[EXPANSION_FAST_RAM] = (BoardDesc){
        expansionFastRamSize, OMEGA_MFR, 1, 1, 0x80, 0 };
    // Picasso II+: its VRAM (not system memory; the drivers take the
    // board's size as the VRAM's), then 64 KB of registers. Serial
    // 0x00100000 tells a II+ (GD5428) from a II.
    boards[EXPANSION_P2_VRAM] = (BoardDesc){
        expansionPicasso2Vram, VILLAGE_TRONIC, 11, 0, 0x00, 0x00100000u };
    boards[EXPANSION_P2_REGS] = (BoardDesc){
        0x10000u, VILLAGE_TRONIC, 12, 0, 0x00, 0x00100000u };
}

static void next_board(int from) {
    current = -1;
    for (int b = from; b < EXPANSION_BOARDS; b++)
        if (present(b)) {
            current = b;
            return;
        }
}

// er_Type: Zorro II, maybe linked into the memory list, the size code.
static uint8_t type_byte(const BoardDesc *d) {
    uint8_t code = 0;
    for (uint32_t size = 64u << 10; size < (8u << 20); size <<= 1)
        if (size == d->size)
            break;
        else
            code++;
    // 64 KB is code 1 ... 4 MB is code 7, 8 MB is code 0.
    code = (uint8_t)((code + 1) & 7);
    return 0xC0 | (d->memlist ? 0x20 : 0) | code;
}

// The board's register bytes, at offsets 0x00, 0x04, ... (before inversion).
static uint8_t register_byte(const BoardDesc *d, unsigned reg) {
    switch (reg) {
    case 0x00: return type_byte(d);
    case 0x04: return d->product;
    case 0x08: return d->flags;
    case 0x10: return (uint8_t)(d->mfr >> 8);
    case 0x14: return (uint8_t)d->mfr;
    case 0x18: return (uint8_t)(d->serial >> 24);
    case 0x1C: return (uint8_t)(d->serial >> 16);
    case 0x20: return (uint8_t)(d->serial >> 8);
    case 0x24: return (uint8_t)d->serial;
    default:   return 0x00;     // diag vector: none
    }
}

void expansionReset(void) {
    describe();
    for (int b = 0; b < EXPANSION_BOARDS; b++)
        if (base[b]) {
            base[b] = 0;
            expansionHostMap(b, 0);
        }
    next_board(0);
}

uint32_t expansionBoardBase(int board) {
    return board >= 0 && board < EXPANSION_BOARDS ? base[board] : 0;
}

int expansionReadByte(uint32_t address) {
    if (current < 0 || (address & 1))
        return -1;
    const unsigned offset = address & 0xFFu;
    const uint8_t reg = register_byte(&boards[current], offset & ~3u);
    uint8_t nibble = (offset & 2) ? (reg & 0x0F) : (reg >> 4);
    if (offset >= 0x04 && offset < 0x40)
        nibble = ~nibble & 0x0F;
    return (nibble << 4) | 0x0F;
}

void expansionWriteByte(uint32_t address, uint8_t value) {
    if (current < 0)
        return;
    const int board = current;
    switch (address & 0xFFu) {
    case 0x48:      // er_BaseAddress, A23-A16: the board is configured
        base[board] = (uint32_t)value << 16;
        printf("Expansion: board %d/%d (%lu KB) at 0x%06lx\n",
               boards[board].mfr, boards[board].product,
               (unsigned long)(boards[board].size >> 10),
               (unsigned long)base[board]);
        next_board(board + 1);
        expansionHostMap(board, base[board]);
        break;
    case 0x4C:      // shut up: the board stays away until the next reset
        next_board(board + 1);
        break;
    }
}
