//
//  CPU.c
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

#include <stdio.h>
#include "CPU.h"
#include "m68k.h"

#include "Memory.h"

#include "CIA.h"
#include "Expansion.h"


unsigned char* _emulatorMemory;

//#define NOSLOWRAM

//#define CHIPTOP  0x3FFFF // for 256K Chip
//#define CHIPTOP  0x7FFFF // for 512K
//#define CHIPTOP  0xFFFFF // for 1Meg
//#define CHIPTOP 0x1FFFFF // for 2Meg

/* Where the 68000 starts after a reset: the initial PC it reads from address
   4, which the ROM overlay maps to ROM offset 4. Kickstart puts its entry
   there and a JMP to the same address at offset 2 (Omega's historical start,
   ROM + 2); DiagROM keeps its "DG" marker at offset 2, so only the vector
   works for every ROM. */
static uint32_t cpu_reset_pc(void){
    const uint32_t pc = m68k_read_memory_32(0xF80004) & 0xFFFFFFu;
    return pc >= 0xF80000u ? pc : 0xF80002u;
}

/* Called when the CPU pulses the RESET line */
void cpu_pulse_reset(void){
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);

    printf("PC:%06lx\n",(unsigned long)pc);
    printf("SP:%06lx\n",(unsigned long)sp);
    
    printf("Machine reset...\n");

    // The reset line resets the chips, not memory: RAM survives, as
    // ColdReboot() callers expect (Kickstart 3.1.4+'s Early Startup
    // Control keeps its boot choices there across the reboot it does).
    ChipsetInit();
    expansionReset();   // the reset line unconfigures the Zorro boards
    m68k_set_reg(M68K_REG_PC, cpu_reset_pc()); // the ROM's reset vector
    
}


/* Keyboard reset (Ctrl + both Amiga keys): what the keyboard's reset line
   does to a real Amiga. Unlike the RESET instruction (cpu_pulse_reset()),
   the 68000 itself is reset: STOP cleared, supervisor mode, interrupts
   masked. Then both CIAs, the custom chips and Paula's interrupt
   registers are re-initialised and the
   CPU starts at the ROM's reset vector, as at power-on (no ROM overlay is
   emulated, so the vector is read from the ROM itself). Chip RAM is
   kept: a warm reset. Call it between emulation slices, never from inside
   dma_run(). */
void cpu_keyboard_reset(void){
    printf("Keyboard reset\n");
    m68k_pulse_reset();
    CIAInit(&CIAA, 32776);
    CIAInit(&CIAB, 40960);
    ChipsetInit();
    expansionReset();
    // Paula's interrupt enables and requests clear with the reset line, and
    // the 68000's interrupt input drops.
    chipset.intenar = 0;
    chipset.intreqr = 0;
    m68k_set_irq(0);
    m68k_set_reg(M68K_REG_SR, 0x2700);
    m68k_set_reg(M68K_REG_PC, cpu_reset_pc()); // the ROM's reset vector
}

/* The CPU model: a 68000 (A500), or a 68EC020 (A1200-like, 24-bit bus),
   which RTG drivers and 020-only software need. Takes effect at once after
   cpu_init(), so a caller selects it just before a reset. */
static unsigned int cpu_type = M68K_CPU_TYPE_68000;
static int cpu_ready;

void cpu_select_68020(int on){
    const unsigned int type = on ? M68K_CPU_TYPE_68EC020 : M68K_CPU_TYPE_68000;
    if (type != cpu_type)
        printf("CPU: %s\n", on ? "68EC020" : "68000");
    cpu_type = type;
    if (cpu_ready)
        m68k_set_cpu_type(cpu_type);
}

void cpu_init(){

    //set CIA IDs
    CIAInit(&CIAA, 32776);
    CIAInit(&CIAB, 40960);
    
    m68k_init();
    m68k_set_cpu_type(cpu_type);
    cpu_ready = 1;
    expansionReset();
    m68k_set_reg(M68K_REG_SR, 0x2700); // Reset enters supervisor mode, IRQs masked
    m68k_set_reg(M68K_REG_PC, cpu_reset_pc()); // the ROM's reset vector

}

void cpu_execute(){
 
/*
    int* dis = &disass;
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
    uint32_t A0 = m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t A1 = m68k_get_reg(NULL, M68K_REG_A1);
    uint32_t A2 = m68k_get_reg(NULL, M68K_REG_A2);
    uint32_t A3 = m68k_get_reg(NULL, M68K_REG_A3);
    uint32_t A4 = m68k_get_reg(NULL, M68K_REG_A4);
    uint32_t A5 = m68k_get_reg(NULL, M68K_REG_A5);
    uint32_t A6 = m68k_get_reg(NULL, M68K_REG_A6);
    uint32_t A7 = m68k_get_reg(NULL, M68K_REG_A7);
    uint32_t D0 = m68k_get_reg(NULL, M68K_REG_D0);
    uint32_t D1 = m68k_get_reg(NULL, M68K_REG_D1);
    uint32_t D2 = m68k_get_reg(NULL, M68K_REG_D2);
    uint32_t D3 = m68k_get_reg(NULL, M68K_REG_D3);
    uint32_t D4 = m68k_get_reg(NULL, M68K_REG_D4);
    uint32_t D5 = m68k_get_reg(NULL, M68K_REG_D5);
    uint32_t D6 = m68k_get_reg(NULL, M68K_REG_D6);
    uint32_t D7 = m68k_get_reg(NULL, M68K_REG_D7);
    
 
    if(pc==0xFC6CFC){
       // printf("Trap\n");
        //disass =1;
    }
    
    uint32_t* guru = low16Meg;
 */
    
    m68k_execute(16);

}




// frank-amiga: while the chipset catches up with the CPU inside an
// instruction (DMA.c dmaCatchUp()), a new level waits for the instruction's
// end.
extern int dmaInCatchUp;
static inline void setIrq(unsigned int level){
    if (dmaInCatchUp) m68k_set_irq_deferred(level);
    else m68k_set_irq(level);
}

void checkInterrupt(Chipset_t* chipset){
    
    if(chipset->intenar & 0x4000){ // if master interrupt switch is enabled
        
        uint16_t intMask = chipset->intreqr & chipset->intenar; //only set the int level, if bits are enabled
        
        if(intMask !=0){
            
            m68k_end_timeslice();
            
            if(intMask & 8192){  // External int
                setIrq(6);
            }else if(intMask & 4096){ // Disk sync
                setIrq(5);
            }else if(intMask & 2048){ // Serial receive buffer full
                setIrq(5);
            }else if(intMask & 1024){ // Audio 3
                setIrq(4);
            }else if(intMask & 512){  // Audio 2
                setIrq(4);
            }else if(intMask & 256){  // Audio 1
                setIrq(4);
            }else if(intMask & 128){  // Audio 0
                setIrq(4);
            }else if(intMask & 64){   // Blitter finished
                setIrq(3);
            }else if(intMask & 32){   // VBL
                setIrq(3);
            }else if(intMask & 16){   // Copper
                setIrq(3);
            }else if(intMask & 8){    // Ports, IO and timers
                setIrq(2);
            }else if(intMask & 4){    // Software int
                setIrq(1);
            }else if(intMask & 2){    // Disk block finished
                setIrq(1);
            }else if(intMask & 1){    // Serial Transmit buffer empty
                setIrq(1);
            }
            
        }else{
            setIrq(0);    // no interupt pending
        }
        
    }else{
        setIrq(0);    // no interupt if master INT disabled
    }
    
}




//Emulator functions

unsigned int cpu_read_byte(unsigned int address){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    return chipReadByte(address);
    
}

unsigned int cpu_read_word(unsigned int address){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    return chipReadWord(address);
    
}

unsigned int cpu_read_long(unsigned int address){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    return chipReadLong(address);
    
}


void cpu_write_byte(unsigned int address,unsigned int value){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    chipWriteByte(address, value);
    
}

void cpu_write_word(unsigned int address,unsigned int value){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    chipWriteWord(address, value);
    
}

void cpu_write_long(unsigned int address,unsigned int value){
    
    //If Address > 0xFFFFFF (the low16Meg), then access the BCM2837 chip
    //Not needed if running on a real RaspberryPi. just allow the CPU to access the memory.
    
    chipWriteLong(address, value);

}






// Called when the CPU acknowledges an interrupt
int cpu_irq_ack(int level)
{
    return level;
    /*
     switch(level)
     {
     case 0:
     return 0;//nmi_device_ack();
     case 1:
     return 0;// input_device_ack();
     case 2:
     return 0;// output_device_ack();
     }
     */
    return M68K_INT_ACK_SPURIOUS;
}


unsigned int cpu_read_word_dasm(unsigned int address){
    if(address > 16777216)
        printf("Disassembler attempted to read word from address %x", address);
    return cpu_read_word(address);
}

unsigned int cpu_read_long_dasm(unsigned int address){
    if(address > 16777216)
        printf("Dasm attempted to read long from address %x", address);
    return cpu_read_long(address);
}





/* Disassembler */

void make_hex(char* buff, unsigned int pc, unsigned int length){
    char* ptr = buff;
    
    for(;length>0;length -= 2)
    {
        sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
        pc += 2;
        ptr += 4;
        if(length > 2)
            *ptr++ = ' ';
    }
}

void disassemble_program(){
    unsigned int pc;
    unsigned int instr_size;
    char buff[100];
    char buff2[100];
    
    pc = cpu_read_long_dasm(4);
    
    while(pc <= 0x16e)
    {
        instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
        make_hex(buff2, pc, instr_size);
        printf("%03x: %-20s: %s\n", pc, buff2, buff);
        pc += instr_size;
    }
    fflush(stdout);
}



void cpu_instr_callback(){

    
    
    // The following code will print out instructions as they are executed
    static char buff[100];
    static char buff2[100];
    static unsigned int pc;
    static unsigned int instr_size;
    
    pc = m68k_get_reg(NULL, M68K_REG_PC);
    instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
    
    
    
    if(disass<1){
        return;
    }
    
    
    
    make_hex(buff2, pc, instr_size);
        
    //fprintf(globalFD,"E %03x: %-20s: %s\n", pc, buff2, buff);
    printf("E %03x: %-20s: %s\n", pc, buff2, buff);
    fflush(stdout);
    //SDL_Delay(75);

    
    
    
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                            //
//                                                                                                            //
//                                                                                                            //
//                                                                                                            //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
