//
//  Blitter.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 20/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
//

/*

 cycle 0:   set up blitter;
            if(mode = copy mode){
                cycle = 0;
            }
            if(mode = fill mode){
                cycle = 5;
            }
            if(mode = line mode){
                cycle = -1
            }
 
 cycle 1:  if(A needs load){
                load A;
                cycle = 2;
            }else if{B needs to load){
                load B;
                cycle = 3;
            } else if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 2:  if{B needs to load){
                load B;
                cycle = 3;
            } else if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 3:  if{C needs to load){
                Load C;
                cycle = 4;
            }
 
 cycle 4:   mask A;
            Shift A;
            Shift B;
            apply logic functions
            set clear zero flag
 
            if(Store D needed){
                Store D;
            }
 
            update v and h counters
            cycle = 1 ; //repeat the operation
 
            if(v == end condition){
                reset v = starting value.
                increment h
            }
 
            if(h == end conditon{
                clear blitter busy flag
                Generate interrupt;
                cycle = 0;  // rest the blitter state
            }
 
 cycle 5:   fill mode
 
 
 */


#include "Memory.h"   /* frank-amiga: CHIPW() */
#include "Blitter.h"


void blitterCycle(void){
    
}

void (*blitterOperation[]) (void) = {blitterCycle,blitterCycle};

int OblitterExecute(){
    
    static int cycle;
    
    //check if blitter DMA is on; if not then return 0
    //Check if blitter needs to run (blitter busy flag is set); if not then return 0;
    
    blitterOperation[cycle]();

    return 1; //if we got here we probably used the memory cycle...
    
    
}

//********************* BLITER SECTION *****************************

uint16_t logicFunction(int minterm,uint16_t wordA, uint16_t wordB, uint16_t wordC){
    
    //Not section
    uint16_t notA = ~wordA;
    uint16_t notB = ~wordB;
    uint16_t notC = ~wordC;
    uint16_t channelD = 0;
    
    //Logic Section
    
    if(minterm & 0x80){
        channelD =            (wordA & wordB & wordC);}
    if(minterm & 0x40){
        channelD = channelD | (wordA & wordB & notC);}
    if(minterm & 0x20){
        channelD = channelD | (wordA & notB  & wordC);}
    if(minterm & 0x10){
        channelD = channelD | (wordA & notB  & notC);}
    if(minterm & 0x08){
        channelD = channelD | (notA  & wordB & wordC);}
    if(minterm & 0x04){
        channelD = channelD | (notA  & wordB & notC);}
    if(minterm & 0x02){
        channelD = channelD | (notA  & notB  & wordC);}
    if(minterm & 0x01){
        channelD = channelD | (notA  & notB  & notC);}
    
    return channelD;
}

// frank-amiga: the minterm as eight all-ones/all-zeros masks, computed once
// per blit, and evaluated branch-free as a 3-level multiplexer (A selects the
// high or low nibble, B a pair, C the bit) - same result as logicFunction().
typedef struct { uint16_t m[8]; } MintermMasks;
static inline MintermMasks mintermMasks(int minterm) {
    MintermMasks k;
    for (int i = 0; i < 8; ++i)
        k.m[i] = (uint16_t)-(uint16_t)((minterm >> i) & 1);
    return k;
}
static inline uint16_t mintermApply(const MintermMasks *k, uint16_t a,
                                    uint16_t b, uint16_t c) {
    const uint16_t nc = (uint16_t)~c;
    const uint16_t b1 = (uint16_t)((c & k->m[7]) | (nc & k->m[6]));
    const uint16_t b0 = (uint16_t)((c & k->m[5]) | (nc & k->m[4]));
    const uint16_t a1 = (uint16_t)((b & b1) | ((uint16_t)~b & b0));
    const uint16_t b1n = (uint16_t)((c & k->m[3]) | (nc & k->m[2]));
    const uint16_t b0n = (uint16_t)((c & k->m[1]) | (nc & k->m[0]));
    const uint16_t a0 = (uint16_t)((b & b1n) | ((uint16_t)~b & b0n));
    return (uint16_t)((a & a1) | ((uint16_t)~a & a0));
}

void blitter_execute(Chipset_t* chipset){
    
    //Blitter DMA Enabled and blitter busy flag set (which means bltsize was written to and blitting should start).
    if( (chipset->dmaconr & 16960) == 16960 ){
        
        /*
         //make it so the blitter doesn't blit immediatly
         static int delay;
         
         delay -=1;
         
         if(delay>0){
         return;
         }
         delay = 10;
         */
        
        

        if(chipset->bltcon1 & 1){
            // frank-amiga: line mode stepped like the hardware (after
            // WinUAE's blitter_line*): A is BLTADAT shifted by ASH, B one
            // bit of the texture rotated per pixel, C read at the current
            // address and D written there (the first pixel to BLTDPT). The
            // error term lives in BLTAPT (A modulo when it is >= 0, B modulo
            // when < 0, SIGN its start); SUD/SUL/AUL pick the octant and SING
            // keeps one pixel per row (outlines for area fill). Pointers,
            // error, ASH, texture and SIGN are left as the chip leaves them,
            // so programs can chain lines.
            const uint16_t con0 = chipset->bltcon0, con1 = chipset->bltcon1;
            const int minterm = con0 & 0xFF;
            const int sud = (con1 >> 4) & 1, sul = (con1 >> 3) & 1, aul = (con1 >> 2) & 1;
            const int sing = (con1 >> 1) & 1;
            const int32_t cmod = chipset->bltcmod;
            const int16_t amod = chipset->bltamod, bmod = chipset->bltbmod;
            int ashift = con0 >> 12;
            int bshift = con1 >> 12;
            uint16_t texture = chipset->bltbdat;
            int16_t err = (int16_t)chipset->bltapt;
            int sign = (con1 >> 6) & 1;
            uint32_t cpt = chipset->bltcpt, dpt = chipset->bltdpt;
            const uint16_t adat = chipset->bltadat & chipset->bltafwm;
            int onedot = 0;
            const int length = chipset->bltsizv;
            for (int i = 0; i < length; ++i) {
                const uint32_t ca = (cpt & 0x1FFFFE) >> 1;
                uint16_t c = CHIPW(ca);
                c = (uint16_t)(c << 8 | c >> 8);
                const uint16_t a = adat >> ashift;
                const uint16_t b = ((texture >> bshift) & 1) ? 0xFFFF : 0;
                uint16_t dval = logicFunction(minterm, a, b, c);
                const int draw = !sing || !onedot;
                onedot++;
                if (draw) {
                    const uint32_t da = ((i == 0 ? dpt : cpt) & 0x1FFFFE) >> 1;
                    CHIPW(da) = (uint16_t)(dval << 8 | dval >> 8);
                }
                dpt = cpt;
                // Step: the error term and the minor axis when it is >= 0,
                // then the major axis.
                err = (int16_t)(err + (sign ? bmod : amod));
                if (!sign) {
                    if (sud) {
                        if (sul) { cpt -= cmod; } else { cpt += cmod; }
                        onedot = 0;
                    } else {
                        if (sul) { if (ashift-- == 0) { ashift = 15; cpt -= 2; } }
                        else     { if (++ashift == 16) { ashift = 0; cpt += 2; } }
                    }
                }
                if (sud) {
                    if (aul) { if (ashift-- == 0) { ashift = 15; cpt -= 2; } }
                    else     { if (++ashift == 16) { ashift = 0; cpt += 2; } }
                } else {
                    if (aul) { cpt -= cmod; } else { cpt += cmod; }
                    onedot = 0;
                }
                sign = err < 0;
                bshift = (bshift - 1) & 15;   // the texture's next bit
            }
            chipset->bltcpt = cpt;
            chipset->bltdpt = dpt;
            chipset->bltapt = (chipset->bltapt & 0xFFFF0000u) | (uint16_t)err;
            chipset->bltcon0 = (uint16_t)((con0 & 0x0FFF) | (ashift << 12));
            chipset->bltcon1 = (uint16_t)((con1 & 0x0FBF) | (bshift << 12) | (sign << 6));
            chipset->bltsizh = 0; // all done;
            chipset->bltsizv = 0; // all done;
            blitterFinished();   // frank-amiga: busy/interrupt when its time is up (DMA.c)
            
            
        }else{
            
            //Area Copy Blitter
            
            int useMask = chipset->bltcon0 >> 8;
            int shiftA = chipset->bltcon0 >> 12;
            int shiftB = chipset->bltcon1 >> 12;
            int minterm = chipset->bltcon0 & 255;
            const MintermMasks mk = mintermMasks(minterm);
            int xIncrement = 1;
            int fillmode = (chipset->bltcon1 & 0x18) >> 3; // 1= Inclusive Fill, 2 = Exclusive Fill
            int fci = (chipset->bltcon1 >> 2) & 1;       // fill carry input

            //if descend mode is on.
            if((chipset->bltcon1 & 2)){
                xIncrement = -1;
            }
            
            
            //all pointers are word addressed and masked for 2meg only
            int apt=(chipset->bltapt >> 1);
            int bpt=(chipset->bltbpt >> 1);
            int cpt=(chipset->bltcpt >> 1);
            int dpt=(chipset->bltdpt >> 1);
            
            //Shifted seems to work properly
            int amod=chipset->bltamod >> 1;
            int bmod=chipset->bltbmod >> 1;
            int cmod=chipset->bltcmod >> 1;
            int dmod=chipset->bltdmod >> 1;
            
            //these are signed so need to be divided not shifted... but doesn't work properly
            //int amod=chipset->bltamod / 2;
            //int bmod=chipset->bltbmod / 2;
            //int cmod=chipset->bltcmod / 2;
            //int dmod=chipset->bltdmod / 2;
            
            uint16_t adat=chipset->bltadat;
            uint16_t bdat=chipset->bltbdat;
            uint16_t cdat=chipset->bltcdat;
            // int ddat=chipset->bltddat;
            
            uint16_t afwm = chipset->bltafwm;
            uint16_t alwm = chipset->bltalwm;
            
            int sizeh = chipset->bltsizh;
            int sizev = chipset->bltsizv;
            int lastHWord=sizeh - 1;
            
            //All operations start with a Zero Flag
            chipset->dmaconr = chipset->dmaconr | 0x2000;   //set zero flag
            
            // The shifters' previous A and B words carry over from one line
            // to the next (only a new blit starts them at zero): bits shifted
            // out at the end of a line enter the next line's first word.
            // DiagROM's scroll test relies on it to wrap its band around.
            uint16_t previousA = 0;
            uint16_t previousB = 0;

            for(int y=0;y<sizev;++y){

                int carry = fci;

                // frank-amiga: an ascending row without fill whose channels
                // each stay inside one 64 KB chip RAM bank runs through
                // direct pointers: no bank lookup or channel test per word,
                // the zero flag tested once a row. Same words, same order
                // (a channel reading what D wrote earlier in the row still
                // sees it). HeroQuest's language screen: blitter ~55 % of
                // the chipset's core.
                if (xIncrement == 1 && !fillmode) {
                    uint16_t *pa = NULL, *pb = NULL, *pc = NULL, *pd = NULL;
                    int fast = 1;
#define BLT_ROW_PTR(en, wpt, ptr) \
                    if (fast && (useMask & (en))) { \
                        const uint32_t b0 = (uint32_t)(wpt) << 1; \
                        const uint32_t b1 = b0 + 2u * (uint32_t)(sizeh - 1); \
                        if ((b0 >> 16) != (b1 >> 16)) fast = 0; \
                        else (ptr) = (uint16_t *)chip_ptr(b0); \
                    }
                    BLT_ROW_PTR(8, apt, pa)
                    BLT_ROW_PTR(4, bpt, pb)
                    BLT_ROW_PTR(2, cpt, pc)
                    BLT_ROW_PTR(1, dpt, pd)
#undef BLT_ROW_PTR
                    if (fast) {
                        uint16_t any = 0;
                        for (int x = 0; x < sizeh; ++x) {
                            if (pa) { const uint16_t w = pa[x]; adat = (uint16_t)(w << 8 | w >> 8); }
                            if (pb) { const uint16_t w = pb[x]; bdat = (uint16_t)(w << 8 | w >> 8); }
                            if (pc) { const uint16_t w = pc[x]; cdat = (uint16_t)(w << 8 | w >> 8); }
                            uint16_t A = adat;
                            if (x == 0) A &= afwm;
                            if (x == lastHWord) A &= alwm;
                            const uint16_t channelA = (uint16_t)((previousA << (16 - shiftA)) | (A >> shiftA));
                            const uint16_t channelB = (uint16_t)((previousB << (16 - shiftB)) | (bdat >> shiftB));
                            previousA = A;
                            previousB = bdat;
                            const uint16_t d = mintermApply(&mk, channelA, channelB, cdat);
                            any |= d;
                            if (pd) pd[x] = (uint16_t)(d << 8 | d >> 8);
                        }
                        if (any)
                            chipset->dmaconr = chipset->dmaconr & 0xDFFF; // clear zero flag
                        if (pa) apt += sizeh;
                        if (pb) bpt += sizeh;
                        if (pc) cpt += sizeh;
                        if (pd) dpt += sizeh;
                        apt += amod;
                        bpt += bmod;
                        cpt += cmod;
                        dpt += dmod;
                        continue;
                    }
                }
                
                for(int x=0;x<sizeh;++x){
                    uint16_t channelD = 0;
                    
                    //Channel A
                    if(useMask & 8){
                        adat = CHIPW(apt);
                        adat = adat << 8 | adat >> 8;
                        apt +=xIncrement;
                    }
                    uint16_t channelA = adat;
                    
                    //Channel B
                    if(useMask & 4){
                        bdat = CHIPW(bpt);
                        bdat = bdat << 8 | bdat >> 8;
                        bpt +=xIncrement;
                    }
                    uint16_t channelB = bdat;
                    
                    //Channel C
                    if(useMask & 2){
                        cdat = CHIPW(cpt);
                        cdat = cdat << 8 | cdat >> 8;
                        cpt +=xIncrement;
                    }
                    
                    
                    //Masking section
                    
                    if(x==0){
                        channelA = channelA & afwm;
                    }
                    
                    if(x==lastHWord){
                        channelA = channelA & alwm;
                    }
                    
                    uint16_t A = channelA; //Need to record the masked A for next cycle (Thanks to aros-sg on eab)
                    
                    //shifting section
                    if(xIncrement==-1){
                        
                        channelA = (previousA >> (16-shiftA)) | (channelA << shiftA);
                        channelB = (previousB >> (16-shiftB)) | (channelB << shiftB);
                        
                    }else{
                        
                        channelA = (previousA << (16-shiftA)) | (channelA >> shiftA);
                        channelB = (previousB << (16-shiftB)) | (channelB >> shiftB);
                        
                    }
                    //previousA = adat;
                    previousA = A;
                    previousB = bdat;
                    
                    
                    channelD = mintermApply(&mk, channelA, channelB, cdat);

                    // Area fill (IFE/EFE): carry propagates LSB→MSB, resets to fci each row
                    if (fillmode) {
                        // frank-amiga: the bit loop as a prefix XOR. The
                        // carry into bit b is fci ^ (bits 0..b-1), so the
                        // exclusive prefix of channelD gives all 16 at once.
                        uint32_t p = channelD;
                        p ^= p << 1; p ^= p << 2; p ^= p << 4; p ^= p << 8;
                        uint32_t cv = (p << 1) & 0xFFFFu;
                        if (carry) cv ^= 0xFFFFu;
                        channelD = (uint16_t)(fillmode == 1 ? (channelD | cv)
                                                            : (channelD ^ cv));
                        carry ^= (int)((p >> 15) & 1u);
                    }

                    //Zero Flag
                    if(channelD!=0){
                        chipset->dmaconr = chipset->dmaconr & 0xDFFF; // clear zero flag
                    }

                    //Channel D
                    if(useMask & 1){
                        
                        channelD = channelD << 8 | channelD >>8;
                        CHIPW(dpt) = channelD;
                        dpt +=xIncrement;
                    }
                    
                }
                
                if(xIncrement == -1){
                    apt -= amod;
                    bpt -= bmod;
                    cpt -= cmod;
                    dpt -= dmod;
                }else{
                    apt += amod;
                    bpt += bmod;
                    cpt += cmod;
                    dpt += dmod;
                }
                
            }
            
            
            chipset->bltsizh = 0; // all done;
            chipset->bltsizv = 0; // all done;
            
            //save blitter state - something might depend upon the blitter being in a known state
            chipset->bltapt = apt << 1;
            chipset->bltbpt = bpt << 1;
            chipset->bltcpt = cpt << 1;
            chipset->bltdpt = dpt << 1;
            chipset->bltadat = adat;
            chipset->bltbdat = bdat;
            chipset->bltcdat = cdat;
            //chipset->bltddat = ddat;
            
            blitterFinished();   // frank-amiga: busy/interrupt when its time is up (DMA.c)
            return;
        }
    }
    
}

