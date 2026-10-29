//
//  Memory.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 02/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <stdlib.h>
#include "Memory.h"
//#include "Kick13.h"
#include "Chipset.h"
#include "CIA.h"
#include "Floppy.h"
#include "debug.h"
#include "DMA.h"
#include "Gayle.h"
#include "../omega/m68k.h"
#include "KickSmash.h"
#include "Expansion.h"
#include "Picasso2.h"

unsigned char low16Meg[16777216];

//Chipset_t* chipset = (Chipset_t*)&low16Meg[0xDFF000];

void loadROM(){
    
    for(int i=0;i<524288;++i){
//         low16Meg[0xF80000+i] = kick13[i];
    }
}


// There is no slow (trapdoor/Ranger) RAM, as on the RP2350 build: 2 MB chip
// RAM. As on an A500 without it, the custom chips answer at
// 0xC00000-0xD7FFFF with their registers repeated every 512 bytes;
// Kickstart's memory sizing tells the mirror from RAM by writing INTENA
// through it and reading INTENAR. 0xD80000-0xD9FFFF reads 0.
#define MIRROR_START 0xC00000u
#define MIRROR_END   0xD80000u
static inline int inCustomMirror(unsigned int address){
    return address >= MIRROR_START && address < MIRROR_END;
}
static inline unsigned int customMirror(unsigned int address){
    return 0xDFF000u | (address & 0x1FFu);
}

// The Zorro II boards, once autoconfig has placed them (omega/Expansion.c):
// the Fast RAM is low16Meg at its own addresses, the Picasso II+'s VRAM
// p2Vram (omega/Picasso2.c). Elsewhere 0x200000-0x9FFFFF is open bus.
static unsigned int fastBase, fastSize, vramBase, vramSize, regsBase, regsSize;
static uint8_t p2Vram[P2_VRAM_SIZE];
// frank-amiga: OMEGA_A500=1 (main_native.c) makes the machine an A500 like
// the board's default: chip RAM mirrors every 512 KB (nativeChipMask) and
// 512 KB of slow RAM sits at 0xC00000 (served like Fast RAM, from low16Meg).
uint32_t nativeChipMask = CHIPTOP;
uint32_t nativeSlowSize;
#define IN_FAST_RAM(a) ((a) - fastBase < fastSize || (a) - 0xC00000u < nativeSlowSize)
#define IN_VRAM(a)     ((a) - vramBase < vramSize)
#define IN_P2_REGS(a)  ((a) - regsBase < regsSize)

void memoryNativeInit(void){
    static uint32_t state[P2_STATE_BYTES / 4];
    p2Init(p2Vram, expansionPicasso2Vram ? expansionPicasso2Vram
                                         : P2_VRAM_SIZE, state);
}

// The window draws the card's picture straight from VRAM.
void p2HostDirty(uint32_t offset, uint32_t length){
    (void)offset; (void)length;
}

void expansionHostMap(int board, uint32_t base){
    switch (board) {
    case EXPANSION_FAST_RAM:
        fastBase = base;
        fastSize = base >= 0x200000u &&
                   base + expansionFastRamSize <= 0xA00000u
                 ? expansionFastRamSize : 0;
        break;
    case EXPANSION_P2_VRAM:
        vramBase = base;
        vramSize = base ? expansionPicasso2Vram : 0;
        break;
    case EXPANSION_P2_REGS:
        regsBase = base;
        regsSize = base ? P2_REGS_SIZE : 0;
        if (!base)
            p2Reset();
        break;
    }
}

// VRAM writes: source data while a blit takes it from the CPU.
static void vramWrite(unsigned int address, uint8_t value){
    if (p2CpuBlt)
        p2BltFeed(value);
    else
        p2Vram[address - vramBase] = value;
}

// Autoconfig space: the board's register (or the Picasso's registers),
// else an open bus (reads high; zero would describe a board and make
// firmware enumerate ghosts).
static unsigned int expansionRead(unsigned int address){
    if (IN_P2_REGS(address))
        return p2RegRead(address - regsBase);
    const int value = address < EXPANSION_END && address >= EXPANSION_BASE
                    ? expansionReadByte(address) : -1;
    return value < 0 ? 0xFF : (unsigned int)value;
}

static void expansionWrite(unsigned int address, uint8_t value){
    if (IN_P2_REGS(address))
        p2RegWrite(address - regsBase, value);
    else if (address < EXPANSION_END && address >= EXPANSION_BASE)
        expansionWriteByte(address, value);
}

// frank-amiga: an extended ROM at 0xE00000-0xE7FFFF (AROS ships its
// Kickstart in two halves); main_native.c loads it (OMEGA_EXT_ROM).
int extRomPresent;
#define IN_EXT_ROM(a) (extRomPresent && (a) >= 0xE00000u && (a) < 0xE80000u)

unsigned int chipReadByte(unsigned int address){
    if (IN_EXT_ROM(address))
        return low16Meg[address];
    if (address > 0xFFFFFFu)
        return 0xFF;
    if (IN_FAST_RAM(address))
        return low16Meg[address];
    if (IN_VRAM(address))
        return p2Vram[address - vramBase];
    //ROM
    if(address>=0xF80000){
        return low16Meg[address];
    }
    
    //Autoconfig space
    if(address>0xDFFFFF){
        return expansionRead(address);
    }
    
#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif
    
    //Chipregs
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        address = (address & 0x1FF);
        debugChipAddress = address;
        if (address >= 32)
            return 0;
        return getChipReg8[address]();
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
        return readGayleB(address);
        
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        return inCustomMirror(address) ? chipReadByte(customMirror(address)) : 0;
    }
    
    if(address>=0xBFD000 && address<0xC00000) DMA_CATCH_UP();
    //CIA A
    if(address>=0xBFE001){
        address = (address - 0xBFE001) >> 8;
        {
            static int ciaa_pra_count = 0;
            if (address == 0 && ciaa_pra_count < 200) {
                uint8_t result = CIARead(&CIAA, 0);
                uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
                uint32_t d0 = m68k_get_reg(NULL, M68K_REG_D0);
                uint32_t d1 = m68k_get_reg(NULL, M68K_REG_D1);
                printf("[CIAA.pra] read=0x%02X (/CHNG=%d /DKRDY=%d) D0=%08X D1=%08X drv=%d  PC=%08X (hit %d)\n",
                       result, (result>>2)&1, (result>>5)&1,
                       d0, d1, driveSelected, pc, ++ciaa_pra_count);
                fflush(stdout);
                return result;
            }
        }
        return CIARead(&CIAA,address);
    }

    //CIA B
    if(address>=0xBFD000){
        address = (address - 0xBFD000) >> 8;
        return CIARead(&CIAB,address);
    }

    //24bit space without Fast RAM
    if(address>0x1FFFFF){
        return 0;
    }

    //RAM
    address &= nativeChipMask;
    return low16Meg[address];
}
unsigned int chipReadWord(unsigned int address){
    if (address > 0xFFFFFFu)
        return 0xFFFF;
    if (IN_EXT_ROM(address))
        return (unsigned int)low16Meg[address] << 8 | low16Meg[address + 1];
    if (IN_FAST_RAM(address))
        return (unsigned int)low16Meg[address] << 8 | low16Meg[address + 1];
    if (IN_VRAM(address))
        return (unsigned int)p2Vram[address - vramBase] << 8 |
               p2Vram[(address + 1 - vramBase) & (P2_VRAM_SIZE - 1)];
    if (IN_P2_REGS(address))
        return expansionRead(address) << 8 | expansionRead(address + 1);
    //ROM
    if(address>0xF7FFFF){
        //return READ_WORD(chipset.rom,address-0xF80000);
        uint16_t value = *(uint16_t*)&low16Meg[address];
        value = (value << 8) | (value >> 8);
        return value;
        
    }
    
    //Autoconfig space
    if(address>0xDFFFFF){
        return expansionRead(address) << 8 | 0xFF;
    }
    
#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif
    
    //Chipregs
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        address = (address & 0x1FF) >> 1;
        
        if(address>16){
            
            if(address ==62){
                return chipset.deniseid;
            }
            
            printf("Attempt to read write-only register %s (returning DeniseID by default)\n",regNames[address]);
            debugChipAddress = address;
            return chipset.deniseid;    // the only read only register up that high is DeniseID...
        }
        debugChipAddress = address;
        return getChipReg16[address]();
        //return ChipsetRead(&chipset, address);
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
       return readGayle(address);
        
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        return inCustomMirror(address) ? chipReadWord(customMirror(address)) : 0;
    }
    
    //CIA A/B: each chip on its half of the bus (frank-amiga)
    if(address>=0xBFD000){
        DMA_CATCH_UP();
        return CIAReadWord(address);
    }
    
    //24bit fast ram
    if(address>0x1FFFFF){
        return 0;
    }
    
    //RAM - Incomplete address decding means chip addresses below 2meg wrap around
    address &= nativeChipMask;
    
    //return READ_WORD(chipset.chipram,address);
    uint16_t value = *(uint16_t*)&low16Meg[address];
    value = (value << 8) | (value >> 8);
    return value;
}
unsigned int chipReadLong(unsigned int address){
    if (IN_EXT_ROM(address))
        return (unsigned int)low16Meg[address] << 24 |
               (unsigned int)low16Meg[address + 1] << 16 |
               (unsigned int)low16Meg[address + 2] << 8 | low16Meg[address + 3];
    if (address > 0xFFFFFFu)
        return 0xFFFFFFFFu;
    if (IN_FAST_RAM(address))
        return (unsigned int)low16Meg[address] << 24 |
               (unsigned int)low16Meg[address + 1] << 16 |
               (unsigned int)low16Meg[address + 2] << 8 | low16Meg[address + 3];
    if (IN_VRAM(address) || IN_P2_REGS(address))
        return chipReadWord(address) << 16 | chipReadWord(address + 2);
    // KickSmash ROM switcher protocol (omega/KickSmash.c)
    if (address > 0xF7FFFFu && kickSmashWatches(address)) {
        uint32_t value;
        if (kickSmashReadLong(address, &value))
            return value;
    }
    
    //ROM
    if(address>0xF7FFFF){
        //return READ_LONG(chipset.rom,address-0xF80000);
        uint32_t value = *(uint32_t*)&low16Meg[address];
        value = ((value << 8) & 0xFF00FF00 ) | ((value >> 8) & 0xFF00FF );
        return value << 16 | value >> 16;
    }

    // Empty Zorro autoconfig space (checked before the custom-register
    // range, which otherwise catches every address above 0xDFF000).
    if(address>0xDFFFFF){
        return 0xFFFFFFFFu;
    }
    
#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif
    
    //Chipregs
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        address = (address & 0x1FF) >> 1;
        // getChipReg32 covers only the first 16 registers; everything else
        // reads 0 like its noReadL entries (as src/Memory.c).
        if (address >= 16u)
            return 0;
        debugChipAddress = address;
        return getChipReg32[address]();
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
        return readGayleL(address);
        
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        return inCustomMirror(address) ? chipReadLong(customMirror(address)) : 0;
    }
    
    //CIA A/B: two word accesses (frank-amiga)
    if(address>=0xBFD000){
        DMA_CATCH_UP();
        return (uint32_t)CIAReadWord(address) << 16 | CIAReadWord(address + 2);
    }
    
    //24bit fast ram
    if(address>0x1FFFFF){
        return 0;
    }
    
    //RAM - Incomplete address decding means chip addresses below 2meg wrap around
    address &= nativeChipMask;
    // frank-amiga: a long access is two word accesses; across the end of
    // chip RAM the second one wraps (the slave splits at every bank edge).
    if (address + 2u > nativeChipMask)
        return chipReadWord(address) << 16 | chipReadWord((address + 2u) & nativeChipMask);
    
    //return READ_LONG(chipset.chipram,address);
    uint32_t value = *(uint32_t*)&low16Meg[address];
    value = ((value << 8) & 0xFF00FF00 ) | ((value >> 8) & 0xFF00FF );
    return value << 16 | value >> 16;
}

static int sig_byte_hook_count = 0;
// frank-amiga native: OMEGA_WATCH=<hex lo>[-<hex hi>] logs CPU writes into
// that range (chip RAM offsets, mirrors folded), the first per writing PC.
static void watchWrite(unsigned address, unsigned size, unsigned value) {
    static long lo = -2, hi;
    static unsigned pcs[256], npcs;
    if (lo == -2) {
        const char *e = getenv("OMEGA_WATCH");
        lo = -1;
        if (e) { char *n; lo = strtol(e, &n, 16); hi = *n == '-' ? strtol(n + 1, NULL, 16) : lo; }
    }
    if (lo < 0) return;
    unsigned a = address < 0x200000u ? (address & nativeChipMask) : address;
    if (a + size <= (unsigned)lo || a > (unsigned)hi) return;
    const unsigned pc = m68k_get_reg(NULL, M68K_REG_PPC) & 0xffffff;
    for (unsigned i = 0; i < npcs; i++) if (pcs[i] == pc) return;
    if (npcs < 256) pcs[npcs++] = pc;
    extern unsigned long native_frame_counter;
    printf("  vbl=%lu WATCH %06x.%u = %0*x  PC=%06x\n", native_frame_counter, address, size,
           (int)size * 2, value, pc);
}

void chipWriteByte(unsigned int address,unsigned int value){
    watchWrite(address & 0xffffff, 1, value & 0xff);   //ROM
    // Catch any byte write into tc_SigRecvd (0xC00C22..0xC00C25)
    if (address >= 0xC00C22u && address <= 0xC00C25u && sig_byte_hook_count < 20) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[SIGRECVD-B] write_byte @%08X = 0x%02X  PC=%08X  (hit %d)\n",
               address, value & 0xFF, pc, ++sig_byte_hook_count);
        fflush(stdout);
    }
    // Catch byte writes to Zorro II autoconfig space (0xE80000-0xE8FFFF)
    if (address >= 0xE80000u && address <= 0xE8FFFFu) {
        static int autoconf_bwrite_count = 0;
        if (autoconf_bwrite_count < 40) {
            uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
            printf("[AUTOCONF-B] write_byte @%08X = 0x%02X  PC=%08X  (hit %d)\n",
                   address, value & 0xFF, pc, ++autoconf_bwrite_count);
            fflush(stdout);
        }
    }
    if (IN_FAST_RAM(address)) {
        low16Meg[address] = (uint8_t)value;
        return;
    }
    if (IN_VRAM(address)) {
        vramWrite(address, (uint8_t)value);
        return;
    }
    if(address>=0xF80000){
        return;
    }

    // Zorro/autoconfig space: the board's registers, never decoded as
    // out-of-range custom registers.
    if(address>0xDFFFFF){
        expansionWrite(address, (uint8_t)value);
        return;
    }

#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif
    
    //Chipregs
    // The 68000 drives a byte on both halves of the data bus and the custom
    // chips latch the whole word: the register gets the byte twice.
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        chipWriteWord(address & ~1u, (value & 0xFFu) * 0x0101u);
        return;
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
        writeGayleB(address, value);
        return;
        
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        if (inCustomMirror(address)) chipWriteByte(customMirror(address), value);
        return;
    }
    
    if(address>=0xBFD000 && address<0xC00000) DMA_CATCH_UP();
    //CIA A
    if(address>=0xBFE001){
        address = (address - 0xBFE001) >> 8;
        CIAWrite(&CIAA,address,value);return;
    }
    
    //CIA B
    if(address>=0xBFD000){
        address = (address - 0xBFD000) >> 8;
        static int ciab_prb_count = 0;
        if (address == 1 && ciab_prb_count < 200) {
            uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
            printf("[CIAB.prb] write 0x%02X  pra=0x%02X hasDisk=%d idMode=%d  PC=%08X (hit %d)\n",
                   value, CIAA.pra, df[0].hasDisk, df[0].idMode, pc, ++ciab_prb_count);
            fflush(stdout);
        }
        CIAWrite(&CIAB,address,value);return;
    }
    
    //CIA A - weird ROM 3 thing...
    if(address>=0xBFA001){
        address = (address - 0xBFA001) >> 8;
        CIAWrite(&CIAA,address,value);return;
    }
    
    //24bit fast ram
    if(address>0x1FFFFF){
        return;
    }
    
    //RAM - Incomplete address decding means chip addresses below 2meg wrap around
    address &= nativeChipMask;
    low16Meg[address] = value;
}

static int dsklen_hook_count = 0;
static int intena_hook_count = 0;
static int dmacon_hook_count = 0;
void chipWriteWord(unsigned int address,unsigned int value){
    watchWrite(address & 0xffffff, 2, value & 0xffff);
    // DSKLEN write (0xDFF024): disk DMA start
    if (address == 0xDFF024u && dsklen_hook_count < 30) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[DSKLEN] write_word @0xDFF024 = 0x%04X  dmaconr=0x%04X  PC=%08X  (hit %d)\n",
               value, chipset.dmaconr, pc, ++dsklen_hook_count);
        fflush(stdout);
    }
    // DMACON write (0xDFF096): log disk DMA enable/disable
    if (address == 0xDFF096u && dmacon_hook_count < 40) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[DMACON] write 0x%04X  dmaconr=0x%04X  PC=%08X  (hit %d)\n",
               value, chipset.dmaconr, pc, ++dmacon_hook_count);
        fflush(stdout);
    }
    // INTENA write (0xDFF09A): log when DSKBLK (bit1) is set or cleared
    if (address == 0xDFF09Au && intena_hook_count < 40) {
        if (value & 0x8002u) {  // SET bit1 (DSKBLK)
            uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
            printf("[INTENA] DSKBLK SET 0x%04X  PC=%08X  (hit %d)\n",
                   value, pc, ++intena_hook_count);
            fflush(stdout);
        } else if (value & 0x0002u) {  // CLR bit1
            uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
            printf("[INTENA] DSKBLK CLR 0x%04X  PC=%08X  (hit %d)\n",
                   value, pc, ++intena_hook_count);
            fflush(stdout);
        }
    }
    // INTREQ write (0xDFF09C): log DSKBLK done and DSKSYNC
    {
        static int intreq_hook_count = 0;
        if (address == 0xDFF09Cu && intreq_hook_count < 60) {
            if (value & 0x0003u) {   // bit0=TBE, bit1=DSKBLK, bit2=SOFT — any disk-related
                uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
                printf("[INTREQ] 0x%04X  PC=%08X  (hit %d)\n",
                       value, pc, ++intreq_hook_count);
                fflush(stdout);
            } else if (value & 0x1000u) {  // bit12=DSKSYNC
                uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
                printf("[INTREQ-DSKSYNC] 0x%04X  PC=%08X  (hit %d)\n",
                       value, pc, ++intreq_hook_count);
                fflush(stdout);
            }
        }
    }
    if (IN_FAST_RAM(address)) {
        low16Meg[address] = (uint8_t)(value >> 8);
        low16Meg[address + 1] = (uint8_t)value;
        return;
    }
    if (IN_VRAM(address) && IN_VRAM(address + 1)) {
        vramWrite(address, (uint8_t)(value >> 8));
        vramWrite(address + 1, (uint8_t)value);
        return;
    }
    if (IN_P2_REGS(address)) {
        // an index/data pair: the high byte to the even port
        expansionWrite(address, (uint8_t)(value >> 8));
        expansionWrite(address + 1, (uint8_t)value);
        return;
    }
    //ROM
    if(address>=0xF80000){
        return;
    }

    if(address>0xDFFFFF){
        // autoconfig: the high byte
        expansionWrite(address, (uint8_t)(value >> 8));
        return;
    }

#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif
    
    //Chipregs
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        address = (address & 0x1FF) >> 1;
        
        debugChipAddress = address;    // used for debugging to identify the register being called
        debugChipValue = value;
        dmaIdleCacheValid = 0;
        putChipReg16[address](value);
        return;
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
        writeGayle(address,value);
        return;
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        if (inCustomMirror(address)) chipWriteWord(customMirror(address), value);
        return;
    }
    
    //CIA A/B: each chip takes its half of the word (frank-amiga)
    if(address>=0xBFD000){
        DMA_CATCH_UP();
        CIAWriteWord(address, (uint16_t)value);
        return;
    }
    
    //24bit fast ram
    if(address>0x1FFFFF){
        return;
    }
    
    //RAM - Incomplete address decding means chip addresses below 2meg wrap around
    address &= nativeChipMask;
    
    //WRITE_WORD(chipset.chipram,address,value);
    uint16_t* dest = (uint16_t*)&low16Meg[address];
    value = (value << 8) | (value >> 8);
    *dest = value;return;

}
static int sig_hook_count  = 0;
static int q_hook_count    = 0;
static int rp_hook_count   = 0;  // reply port message list
static int sig2_hook_count = 0;  // tc_SigRecvd of input.device @C026E2
static int tr_hook_count   = 0;  // TaskReady lh_TailPred
static int execlib_sig_count = 0; // tc_SigRecvd of exec.library task @C01570
static int condev_sig_count  = 0; // tc_SigRecvd of console.device @C0A7E0
static int tdsig_hook_count  = 0; // tc_SigRecvd of trackdisk.device @C0485E
void chipWriteLong(unsigned int address,unsigned int value){
    watchWrite(address & 0xffffff, 4, value);
    // Catch Signal() to trackdisk.device @C0485E: tc_SigRecvd at +0x1A = 0xC04878
    if (address == 0xC04878u && tdsig_hook_count < 40) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[TRACKDISK-SIG] write_long @0xC04878 = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++tdsig_hook_count);
        fflush(stdout);
    }
    // Catch writes to tc_SigRecvd (0xC00C22) — old hook (stale)
    if (address == 0xC00C22 && sig_hook_count < 20) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[SIGRECVD] write_long @0xC00C22 = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++sig_hook_count);
        fflush(stdout);
    }
    // Catch Signal() to input.device @C026E2: tc_SigRecvd at +0x1A = 0xC026FC
    if (address == 0xC026FCu && sig2_hook_count < 30) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[INPUTDEV-SIG] write_long @0xC026FC = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++sig2_hook_count);
        fflush(stdout);
    }
    // Catch Signal() to exec.library boot task @C01570: tc_SigRecvd at +0x1A = 0xC0158A
    if (address == 0xC0158Au && execlib_sig_count < 30) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[EXECLIB-SIG] write_long @0xC0158A = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++execlib_sig_count);
        fflush(stdout);
    }
    // Catch Signal() to console.device task @C0A7E0: tc_SigRecvd at +0x1A = 0xC0A7FA
    if (address == 0xC0A7FAu && condev_sig_count < 30) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[CONDEV-SIG] write_long @0xC0A7FA = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++condev_sig_count);
        fflush(stdout);
    }
    // Catch AddTail() to TaskReady (ExecBase=0xC00276; lh_TailPred at ExecBase+0x19E=0xC00414)
    if (address == 0xC00414u && tr_hook_count < 30) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[TASKREADY] write_long @0xC00414 = 0x%08X  PC=%08X  (hit %d)\n",
               value, pc, ++tr_hook_count);
        fflush(stdout);
    }
    // Catch AddTail() to ROM disk queue at 0xC061E8 (lh_TailPred at +8 = 0xC061F0)
    if ((address == 0xC061E8 || address == 0xC061F0 || address == 0xC061F4) && q_hook_count < 6) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[ROMDISK-Q1] write_long @%08X = 0x%08X  PC=%08X  (hit %d)\n",
               address, value, pc, ++q_hook_count);
        fflush(stdout);
    }
    // Catch writes to reply port mp_MsgList (0xC075EE+0x14..+0x1C) to detect ReplyMsg()
    // lh_Head=0xC07602, lh_Tail=0xC07606, lh_TailPred=0xC0760A
    if (address >= 0xC07602u && address <= 0xC0760Cu && rp_hook_count < 20) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        printf("[REPLYPORT] write_long @%08X = 0x%08X  PC=%08X  (hit %d)\n",
               address, value, pc, ++rp_hook_count);
        fflush(stdout);
    }
    if (IN_FAST_RAM(address)) {
        low16Meg[address] = (uint8_t)(value >> 24);
        low16Meg[address + 1] = (uint8_t)(value >> 16);
        low16Meg[address + 2] = (uint8_t)(value >> 8);
        low16Meg[address + 3] = (uint8_t)value;
        return;
    }
    if (IN_VRAM(address) || IN_P2_REGS(address)) {
        chipWriteWord(address, value >> 16);
        chipWriteWord(address + 2, value & 0xFFFF);
        return;
    }
    //ROM
    if(address>=0xF80000){
        return;
    }

    // Catch writes to Zorro II autoconfig space (0xE80000-0xE8FFFF)
    // On real HW this is where KS probes for expansion boards; Omega returns 0x00 (should be 0xFF)
    if (address >= 0xE80000u && address <= 0xE8FFFFu) {
        static int autoconf_write_count = 0;
        if (autoconf_write_count < 40) {
            uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
            printf("[AUTOCONF-W] write_long @%08X = 0x%08X  PC=%08X  (hit %d)\n",
                   address, value, pc, ++autoconf_write_count);
            fflush(stdout);
        }
    }

    if(address>0xDFFFFF){
        return;
    }

#ifdef THREADED_CPU
    waitFreeSlot(); //CPU must wait for DMA to complete;
#endif

    //Chipregs
    if(address>0xDFEFFF){
        DMA_CATCH_UP();
        // A 68020 long write is two adjacent custom-register word writes.
        // The old 32-bit dispatch table contains mostly no-op placeholders,
        // which discarded common writes such as BPLxPTH/BPLxPTL pairs.
        chipWriteWord(address, value >> 16);
        chipWriteWord(address + 2, value & 0xffff);
        return;
    }
    
    //IDE Interface
    if(address>0xD9FFFF){
        
        writeGayleL(address, value);
        
    }
    
    //No slow RAM: custom register mirror (see MIRROR_START)
    if(address>0xBFFFFF){
        if (inCustomMirror(address)) chipWriteLong(customMirror(address), value);
        return;
    }
    
    //CIA A/B: two word writes (frank-amiga)
    if(address>=0xBFD000){
        DMA_CATCH_UP();
        CIAWriteWord(address, (uint16_t)(value >> 16));
        CIAWriteWord(address + 2, (uint16_t)value);
        return;
    }
    
    //24bit space without Fast RAM
    if(address>0x1FFFFF){
        return;
    }
    
    //RAM - Incomplete address decding means chip addresses below 2meg wrap around
    address &= nativeChipMask;
    if (address + 2u > nativeChipMask) {    // see chipReadLong
        chipWriteWord(address, value >> 16);
        chipWriteWord((address + 2u) & nativeChipMask, value & 0xffff);
        return;
    }
    
    //WRITE_LONG(chipset.chipram,address,value);
    uint32_t* dest = (uint32_t*)&low16Meg[address];
    value = ((value << 8) & 0xFF00FF00 ) | ((value >> 8) & 0xFF00FF );
    value = value << 16 | value >> 16;
    *dest = value; return;

}
