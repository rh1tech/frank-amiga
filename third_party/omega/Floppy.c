//
//  Floppy.c
//  The Omega Project
//  https://github.com/h5n1xp/Omega
//
//  Created by Matt Parsons on 02/02/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//  <h5n1xp@gmail.com>
//
//  Clocking and Checksum Code contributed by Dirk Hoffmann
//
//  This Source Code Form is subject to the terms of the
//  Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
//  with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "Floppy.h"
#include "Chipset.h"
#include "Memory.h"
#include <string.h>

#ifndef PICO_BUILD
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#else
#include "psram.h"   /* frank-amiga: platform header via include path */
#endif


void encodeBlock(uint8_t* source, uint8_t* destination,int size){
    
    for(int i=0;i<size;i++){
        destination[i] = (source[i] >> 1) & 0x55;
        destination[i+size] = (source[i]) & 0x55;
    }
    
}

uint8_t addClockBits(uint8_t previous, uint8_t value) {
    // Clear all previously set clock bits
    value &= 0x55;
    
    // Compute clock bits (clock bit values are inverted)
    uint8_t lShifted = (value << 1);
    uint8_t rShifted = (value >> 1) | (previous << 7);
    uint8_t cBitsInv = lShifted | rShifted;
    
    // Reverse the computed clock bits
    uint64_t cBits = cBitsInv ^ 0xAA;
    
    // Return original value with the clock bits added
    return value | cBits;
}

#ifndef PICO_BUILD
void ADF2MFM(int fd, uint8_t* mfm){

    int size =(int) lseek(fd, 0, SEEK_END);
    
    //printf("FloppySize: %d\n",size);
    
    //512 bytes per sector
    int sectors = size/512;
    
    //22 sectors per track (each side 11 sectors)
    int tracks = (sectors / 22);
    
    //each track is 12798 bytes in size, multiplied by 2 because there are 2 sides
    //int mfmSize = tracks * (12798 * 2);
    
    uint8_t* adf = malloc(size);
    // uint8_t mfm[mfmSize];
    
    uint8_t lowlevelSector[544]; //bytes per low level sector
    
    lseek(fd, 0, SEEK_SET);
    read(fd, adf, size);
    
    
    int count = 0;
    int s = 0;
    
    for(int track = 0;track<tracks;track++){
        for(int side=0;side<2;side++){
            for(int sector=0;sector<11;sector++){
                
                //int secCountDown = 11 - sector;
                
                
                //printf("%d: Track %d, Side: %d, Sector %d (%d)\n",s,track,side,sector,secCountDown);
                
                //Build the sector
                lowlevelSector[0] = 0x0;
                lowlevelSector[1] = 0x0;
                
                lowlevelSector[2] = 0xA1; // will be a sync mark
                lowlevelSector[3] = 0xA1; // will be a sync mark
                
                //sector info
                
                lowlevelSector[4] = 0xFF;
                lowlevelSector[5] = track << 1 | side;
                lowlevelSector[6] = sector;
                lowlevelSector[7] = 11 - sector;
                
                //Sector label
                lowlevelSector[8]  = 0x0;
                lowlevelSector[9]  = 0x0;
                lowlevelSector[10] = 0x0;
                lowlevelSector[11] = 0x0;
                
                lowlevelSector[12] = 0x0;
                lowlevelSector[13] = 0x0;
                lowlevelSector[14] = 0x0;
                lowlevelSector[15] = 0x0;
                
                lowlevelSector[16] = 0x0;
                lowlevelSector[17] = 0x0;
                lowlevelSector[18] = 0x0;
                lowlevelSector[19] = 0x0;
                
                lowlevelSector[20] = 0x0;
                lowlevelSector[21] = 0x0;
                lowlevelSector[22] = 0x0;
                lowlevelSector[23] = 0x0;
                
                
                //data
                for(int i=0;i<512;++i){
                    lowlevelSector[32+i] = adf[i+(count*512)];
                    
                }
                
                
                
                //***************
                
                //Encode
                
                mfm[s+0] = 0xAA;
                mfm[s+1] = 0xAA;
                mfm[s+2] = 0xAA;
                mfm[s+3] = 0xAA;
                
                mfm[s+4] = 0x44;
                mfm[s+5] = 0x89;
                mfm[s+6] = 0x44;
                mfm[s+7] = 0x89;
                
                //info
                encodeBlock(&lowlevelSector[4], &mfm[s+8], 4); // adds 8 bytes
                
                //Disklabel
                encodeBlock(&lowlevelSector[8], &mfm[s+16], 16);//adds 32 bytes
                
                //Data section
                encodeBlock(&lowlevelSector[32], &mfm[s+64], 512);
                
                
                //Header checksum
                uint8_t hcheck[4] = { 0, 0, 0, 0 };
                for(unsigned i = 8; i < 48; i += 4) {
                    hcheck[0] ^= mfm[s+i];
                    hcheck[1] ^= mfm[s+i+1];
                    hcheck[2] ^= mfm[s+i+2];
                    hcheck[3] ^= mfm[s+i+3];
                }
                
                lowlevelSector[24] = hcheck[0];
                lowlevelSector[25] = hcheck[1];
                lowlevelSector[26] = hcheck[2];
                lowlevelSector[27] = hcheck[3];
                
                //header checksum
                encodeBlock(&lowlevelSector[24], &mfm[s+48], 4); //adds 8 bytes
                
                
                // Data checksum
                uint8_t dcheck[4] = { 0, 0, 0, 0 };
                for(unsigned i = 64; i < 1088; i += 4) {
                    dcheck[0] ^= mfm[s+i];
                    dcheck[1] ^= mfm[s+i+1];
                    dcheck[2] ^= mfm[s+i+2];
                    dcheck[3] ^= mfm[s+i+3];
                }
                
                lowlevelSector[28] = dcheck[0];
                lowlevelSector[29] = dcheck[1];
                lowlevelSector[30] = dcheck[2];
                lowlevelSector[31] = dcheck[3];
                
                //Encode Data checksum
                encodeBlock(&lowlevelSector[28], &mfm[s+56], 4); //adds 8 bytes
                
                
                
                //Add clocking bits
                for(int i=8;i<1088;i++){
                    uint8_t previous = mfm[s+i-1];
                    
                    mfm[s+i] = addClockBits(previous,mfm[s+i]);
                    //mfm[s+i] = clocking(previous, mfm[s+i]);
                    
                }
                
                s += 1088;    //Why not 1088, which is the size of the data we've produced
                count +=1;
                
                
            }
            
            //Add clocking bits to the track gap
            mfm[s]   = addClockBits(mfm[s-1],0);
            mfm[s+1] = 0xA8;
            mfm[s+2] = 0x55;
            mfm[s+3] = 0x55;
            mfm[s+4] = 0xAA;
            
            for(int i=5;i<700;i++){
                uint8_t previous = mfm[s+i-1];
                
                mfm[s+i] = addClockBits(previous,0);
                //mfm[s+i] = clocking(previous, mfm[s+i]);
                
            }
            
            s += 830;   //pad track to make 12798 bytes to meet the ADF-EXT spec.
            //printf("\n");
        }
        
    }
    //printf("loaded");
    free(adf);
}
#endif // !PICO_BUILD

// ── RP2350 version: ADF data already in a memory buffer ──────────────────
void ADF2MFM_from_mem(const uint8_t* adf, uint32_t size, uint8_t* mfm) {
    int sectors = (int)(size / 512);
    int tracks  = sectors / 22;

    uint8_t lowlevelSector[544];

    int count = 0;
    int s = 0;

    for (int track = 0; track < tracks; track++) {
        for (int side = 0; side < 2; side++) {
            for (int sector = 0; sector < 11; sector++) {
                lowlevelSector[0] = 0x00;
                lowlevelSector[1] = 0x00;
                lowlevelSector[2] = 0xA1;
                lowlevelSector[3] = 0xA1;

                lowlevelSector[4] = 0xFF;
                lowlevelSector[5] = (uint8_t)((track << 1) | side);
                lowlevelSector[6] = (uint8_t)sector;
                lowlevelSector[7] = (uint8_t)(11 - sector);

                for (int i = 8; i < 32; i++) lowlevelSector[i] = 0;

                for (int i = 0; i < 512; i++)
                    lowlevelSector[32 + i] = adf[i + count * 512];

                mfm[s+0] = 0xAA; mfm[s+1] = 0xAA;
                mfm[s+2] = 0xAA; mfm[s+3] = 0xAA;
                mfm[s+4] = 0x44; mfm[s+5] = 0x89;
                mfm[s+6] = 0x44; mfm[s+7] = 0x89;

                encodeBlock(&lowlevelSector[4],  &mfm[s+8],  4);
                encodeBlock(&lowlevelSector[8],  &mfm[s+16], 16);
                encodeBlock(&lowlevelSector[32], &mfm[s+64], 512);

                uint8_t hcheck[4] = {0,0,0,0};
                for (unsigned i = 8; i < 48; i += 4) {
                    hcheck[0] ^= mfm[s+i];   hcheck[1] ^= mfm[s+i+1];
                    hcheck[2] ^= mfm[s+i+2]; hcheck[3] ^= mfm[s+i+3];
                }
                lowlevelSector[24] = hcheck[0]; lowlevelSector[25] = hcheck[1];
                lowlevelSector[26] = hcheck[2]; lowlevelSector[27] = hcheck[3];
                encodeBlock(&lowlevelSector[24], &mfm[s+48], 4);

                uint8_t dcheck[4] = {0,0,0,0};
                for (unsigned i = 64; i < 1088; i += 4) {
                    dcheck[0] ^= mfm[s+i];   dcheck[1] ^= mfm[s+i+1];
                    dcheck[2] ^= mfm[s+i+2]; dcheck[3] ^= mfm[s+i+3];
                }
                lowlevelSector[28] = dcheck[0]; lowlevelSector[29] = dcheck[1];
                lowlevelSector[30] = dcheck[2]; lowlevelSector[31] = dcheck[3];
                encodeBlock(&lowlevelSector[28], &mfm[s+56], 4);

                for (int i = 8; i < 1088; i++) {
                    mfm[s+i] = addClockBits(mfm[s+i-1], mfm[s+i]);
                }

                s     += 1088;
                count += 1;
            }

            mfm[s]   = addClockBits(mfm[s-1], 0);
            mfm[s+1] = 0xA8; mfm[s+2] = 0x55;
            mfm[s+3] = 0x55; mfm[s+4] = 0xAA;
            for (int i = 5; i < 700; i++)
                mfm[s+i] = addClockBits(mfm[s+i-1], 0);
            s += 830;
        }
    }
}

//*******************************



int floppySync = 0;
int driveSelected=0;
Fd_t df[4];

#ifndef PICO_BUILD
// Native builds keep DF0's whole disk as MFM (tracks * sides).
static uint8_t df0_mfm_image[FLOPPY_MFM_TRACK_SIZE * 82 * 2];
#endif

#ifdef PICO_BUILD
// The flash-backed DF0 path keeps only the selected side as MFM, in the
// DF0 track area of PSRAM (an empty drive's track when no ADF is mounted). It is accessed through the uncached
// XIP alias so disk streaming never evicts emulator code or chip RAM from
// the shared 16 KB XIP cache; SRAM is left for the core-1 ring.
#define df0_track_cache ((uint8_t *)(PSRAM_BASE - XIP_BASE + \
                                     XIP_NOCACHE_NOALLOC_BASE + \
                                     PSRAM_DF0_OFFSET))
#define DF0_TRACK_WORDS ((FLOPPY_MFM_TRACK_SIZE + 3) / 4)
// Streams MFM bytes, clock bits included, as 32-bit writes to the
// uncached PSRAM track buffer.
typedef struct {
    volatile uint32_t *dst;
    uint32_t word;
    unsigned bytes;
    uint8_t prev;
} MfmWriter;

static inline void mfmPut(MfmWriter *w, uint8_t value) {
    w->word |= (uint32_t)value << (8u * w->bytes);
    w->prev = value;
    if (++w->bytes == 4u) {
        *w->dst++ = w->word;
        w->word = 0;
        w->bytes = 0;
    }
}

static inline void mfmPutClocked(MfmWriter *w, uint8_t data) {
    mfmPut(w, addClockBits(w->prev, data));
}

// encodeBlock() followed by the clock-bit pass, streamed.
static void mfmPutBlock(MfmWriter *w, const uint8_t *src, int size) {
    for (int i = 0; i < size; i++)
        mfmPutClocked(w, (uint8_t)(src[i] >> 1));
    for (int i = 0; i < size; i++)
        mfmPutClocked(w, src[i]);
}

// XOR of encodeBlock(src, size) by byte lane, as the sector checksums take
// it. Both halves of byte i land in lane i % 4 (size is a multiple of 4).
static uint32_t mfmBlockSum(const uint32_t *src, int size) {
    uint32_t sum = 0;
    for (int i = 0; i < size / 4; i++)
        sum ^= (src[i] ^ (src[i] >> 1)) & 0x55555555u;
    return sum;
}

// Encode one cylinder side into a 12,798-byte MFM track, bit-identical to
// ADF2MFM(). The raw ADF remains in flash; the track is written straight to
// `mfm` in PSRAM without a staging copy in SRAM.
static int ADF2MFM_track_from_mem(const uint8_t *adf, uint32_t size,
                                  int track, int side, uint8_t *mfm) {
    if (!adf || !mfm || track < 0 || track >= 80 || side < 0 || side > 1)
        return 0;

    // Sector bytes 4..543 of lowlevelSector in ADF2MFM(): info, label,
    // header and data checksums, then the 512 data bytes.
    uint32_t sector_words[135];
    uint8_t *info = (uint8_t *)sector_words;
    MfmWriter w = { (volatile uint32_t *)mfm, 0, 0, 0 };

    for (int sector = 0; sector < 11; sector++) {
        uint32_t source_offset =
            (uint32_t)(((track * 2 + side) * 11 + sector) * 512);
        if (source_offset + 512u > size)
            return 0;

        info[0] = 0xFF;
        info[1] = (uint8_t)((track << 1) | side);
        info[2] = (uint8_t)sector;
        info[3] = (uint8_t)(11 - sector);
        memset(&info[4], 0, 16);
        memcpy(&info[28], adf + source_offset, 512);
        sector_words[5] = mfmBlockSum(&sector_words[0], 4) ^
                          mfmBlockSum(&sector_words[1], 16);
        sector_words[6] = mfmBlockSum(&sector_words[7], 512);

        mfmPut(&w, 0xAA); mfmPut(&w, 0xAA);
        mfmPut(&w, 0xAA); mfmPut(&w, 0xAA);
        mfmPut(&w, 0x44); mfmPut(&w, 0x89);
        mfmPut(&w, 0x44); mfmPut(&w, 0x89);
        mfmPutBlock(&w, &info[0], 4);
        mfmPutBlock(&w, &info[4], 16);
        mfmPutBlock(&w, &info[20], 4);
        mfmPutBlock(&w, &info[24], 4);
        mfmPutBlock(&w, &info[28], 512);
    }

    mfmPutClocked(&w, 0);
    mfmPut(&w, 0xA8); mfmPut(&w, 0x55);
    mfmPut(&w, 0x55); mfmPut(&w, 0xAA);
    for (int i = 5; i < 700; i++)
        mfmPutClocked(&w, 0);
    for (int i = 11 * 1088 + 700; i < FLOPPY_MFM_TRACK_SIZE; i++)
        mfmPut(&w, 0);
    if (w.bytes)
        *w.dst = w.word;    // the buffer is padded to a word multiple
    return 1;
}

static const uint8_t *df0_adf;
static uint32_t df0_adf_size;
static int df0_cached_cylinder = -1;
static int df0_cached_side = -1;

static int floppyEnsureTrackCached(int drive) {
    if (drive != 0 || !df0_adf)
        return 0;
    if (df0_cached_cylinder == df[0].cylinder &&
        df0_cached_side == df[0].side)
        return 1;
    if (!ADF2MFM_track_from_mem(df0_adf, df0_adf_size,
                                df[0].cylinder, df[0].side,
                                df0_track_cache))
        return 0;
    df0_cached_cylinder = df[0].cylinder;
    df0_cached_side = df[0].side;
    return 1;
}

int floppyMountADF(int drive, const uint8_t *adf, uint32_t size) {
    if (drive != 0 || !adf || size != FLOPPY_ADF_SIZE)
        return 0;
    df0_adf = adf;
    df0_adf_size = size;
    df0_cached_cylinder = -1;
    df0_cached_side = -1;
    df[0].mfmData = df0_track_cache;
    df[0].hasDisk = 0;

    // Before insertion expose an invalid header rather than mounted data, so
    // Kickstart can finish its no-disk retry and display the hand screen.
    volatile uint32_t *track = (volatile uint32_t *)df0_track_cache;
    track[0] = 0x8944u;
    for (int i = 1; i < DF0_TRACK_WORDS; i++)
        track[i] = 0;
    return 1;
}
#endif

void floppyIndexReset(){
    
       df[driveSelected].index = 4;
    
}

uint8_t floppyDataRead(){ //this function should be called by the DMA

#ifdef PICO_BUILD
    // DF0 holds one track: the ADF's current one, or the empty drive's.
    int streamed = driveSelected == 0;
    if (streamed && df[0].hasDisk && !floppyEnsureTrackCached(0))
        return 0;
    int position = streamed
        ? df[driveSelected].index
        : (df[driveSelected].cylinder * (FLOPPY_MFM_TRACK_SIZE * 2)) +
          (df[driveSelected].side * FLOPPY_MFM_TRACK_SIZE) +
          df[driveSelected].index;
#else
    int position = (df[driveSelected].cylinder * (FLOPPY_MFM_TRACK_SIZE * 2)) +
                   (df[driveSelected].side * FLOPPY_MFM_TRACK_SIZE) +
                   df[driveSelected].index;
#endif
    
    df[driveSelected].index +=1;

    if(df[driveSelected].index>12667){ //ADF track has 12798 bytes, but on a normal AmigaOS disk 12668 are used.
        df[driveSelected].index= 0;
        CIAIndex(&CIAB);    // generate CIAB index interupt
        
        //int stinkmog = (df[driveSelected].track * (12798 * 2)) + (df[driveSelected].side  * 12798) + df[driveSelected].index;
        //uint8_t* dri = &df[driveSelected].mfmData[stinkmog];
        //printf("wartest du!");
        
    }
    
    // DF1-DF3 are unconnected and have no data.
    uint8_t retVal = df[driveSelected].mfmData
                   ? df[driveSelected].mfmData[position] : 0;
    
    /*
    if(retVal==0){
        //printf("Uh oh!");
    }
    */
    
    return retVal;
}


void floppyInsert(int drive){
    if(drive != 0){
        return;     // only DF0 takes disks
    }

    if(df[drive].idMode !=0){
        //Only Vaild drives have an ID Mode == 0 
        return;
    }
    
    if(df[drive].hasDisk){
        df[drive].hasDisk = 0;
        df[drive].pra &= 0xFB;      // /CHNG=0 (change: disk removed)
        df[drive].pra |= 0x20;      // /DKRDY=1 (no media ready)
        printf("Disk ejected from df%d:\n",drive);
    }else{
#ifdef PICO_BUILD
        if (drive == 0 && df0_adf && !floppyEnsureTrackCached(0))
            return;
#endif
        df[drive].hasDisk = 1;
        df[drive].pra &= 0xFB;      // /CHNG=0 (change: disk inserted)
        // The motor-control output may not change while Kickstart polls the
        // hand screen, so floppySetState() may have no edge on which to
        // recalculate readiness. Present the inserted medium immediately;
        // a subsequent motor-off write restores /DKRDY=1.
        df[drive].pra &= 0xDF;      // /DKRDY=0 (inserted medium ready)
        df[drive].index = 0;
        printf("Disk inserted in df%d:\n",drive);
    }
    
}

uint8_t* floppyInit(int drive){
    df[drive].idMode = -1;
    df[drive].index = 0;
    df[drive].cylinder = 0;
    df[drive].side = 0;
    df[drive].hasDisk = 0;
    df[drive].pra  |= 0x04;   // /CHNG=1 (stable, no change at power-on)
    df[drive].pra  &= 0xEF;   // cylinder 0 (bit4=0)
    df[drive].pra  |= 0x20;   // drive not ready (/DKRDY=1, no disk)
#ifdef PICO_BUILD
    // Only DF0 has an MFM buffer (in PSRAM); DF1-DF3 are unconnected.
    df[drive].mfmData = drive == 0 ? psram_ptr(PSRAM_DF0_OFFSET) : NULL;
    if (drive == 0) {
        df0_adf = NULL;
        df0_adf_size = 0;
        df0_cached_cylinder = -1;
        df0_cached_side = -1;
    }
#else
    df[drive].mfmData = drive == 0 ? df0_mfm_image : NULL;
#endif
    return df[drive].mfmData;
}

void floppyState(){
    CIAA.pra &= 0xC3;
    CIAA.pra |= df[driveSelected].pra;
}

void floppySetState(){            //To be called when Writes to CIAB prb happen.
    
    static uint8_t PRB;
#ifndef PICO_BUILD
    static int count = 0;
#endif
    
    PRB = CIAB.prb;
    
    switch (PRB & 0x78) {
        case 0x78:
            CIAA.pra &=0xC3;    //Need to take the Drives off the floppy bus.
            return;
            break;
            
        case 0x70:
            driveSelected = 0;
            break;

        case 0x68:
            driveSelected = 1;
            break;

        case 0x58:
            driveSelected = 2;
            break;

        case 0x38:
            driveSelected = 3;
            break;
            
        default:
            break;
    }
    //count++;

    //printf("%d: ",count);
    
    //ID mode... to identify external drives...
     if(df[driveSelected].idMode>0){   // Id mode
         // Report drive present (/DKRDY=0) only for drives that physically exist.
         // Drive 0 is always present (even with no ADF loaded). Drives 1-3 are only
         // present if an ADF is loaded. Without this distinction, the ROM sees four
         // 3.5" DD drives, times out reading all of them, then falls to ROM disk.
         if(driveSelected == 0 || df[driveSelected].hasDisk){
             df[driveSelected].pra  &= 0xDF;  // /DKRDY=0 → drive present, ID=0x00000000
         }
         df[driveSelected].idMode -=1;
         CIAA.pra = (CIAA.pra & 0xC3) | (df[driveSelected].pra & 0x3C);
         return;
     }
    
    

    
    // If no change in state just return
    if(PRB == df[driveSelected].prb){
        floppyState();
        //printf("---\n");
        return;
    }
    


    

    //printf("DF%d - ",driveSelected);
    
    if(PRB & 0x80){
        //printf(" Motor Off ");
        //df[driveSelected].prb  |= 0x80;
        
        if(df[driveSelected].idMode==-1){   //is this the first time the motor has been turned off?
            df[driveSelected].idMode = 32;  //if so activate ID Mode
        }
        
        df[driveSelected].pra  |= 0x20;     //Drive not ready
        
    }else{
        //printf(" Motor On ");
        //df[driveSelected].prb  &= 0x7F;
        
        // Report drive ready (/DKRDY=0) for drives that physically exist.
        // Drive 0 is always present; without this, KS 2.04's 200-read polling
        // loop sees /DKRDY=1 and falls back to its internal ROM disk instead
        // of showing the insert-disk screen when no ADF is loaded.
        if(df[driveSelected].hasDisk || driveSelected == 0){
            df[driveSelected].pra  &= 0xDF;  // /DKRDY=0 → drive ready
        } else {
            df[driveSelected].pra  |= 0x20;  // non-existent drive → not ready
        }
        //floppySync=0;
    }
    

    
    //Step head (don't step again if we've already stepped)
    if( (PRB & 0x1) && !(df[driveSelected].prb & 0x1) ){
#ifndef PICO_BUILD
        printf("%04x - DF%d Click\n",count,driveSelected);
#endif
        
        if(PRB & 0x2){
            df[driveSelected].cylinder -=1;
            //printf("DF%d Head Stepped back: %d\n",driveSelected,df[driveSelected].cylinder);
        }else{
            df[driveSelected].cylinder +=1;
           //printf("DF%d Head Stepped forward: %d\n",driveSelected,df[driveSelected].cylinder);
        }
        
        if(df[driveSelected].cylinder < 0){
            df[driveSelected].cylinder = 0;
        }
    
        
        if(df[driveSelected].cylinder >79){    //not sure why sometimes the drive tries to go up to track 80.. with ks1.3
            df[driveSelected].cylinder = 79;
        }

        // A real Amiga drive keeps /CHNG asserted after an insertion or
        // ejection until the controller steps the head. Clearing it merely on
        // drive selection makes a waiting Kickstart miss runtime insertion.
        df[driveSelected].pra |= 0x04;
        
        //floppySync = 0;
        
        //printf(" to track %d|",df[driveSelected].track);
        
    }
    
    
    if(df[driveSelected].cylinder==0){
        df[driveSelected].pra  &= 0xEF; //Track 0 reached
    }else{
        df[driveSelected].pra  |= 0X10; // not track 0;
    }
    
    
    if(PRB & 0x4){
        df[driveSelected].side = 0;
        //df[driveSelected].prb |= 0x4;
        //printf(" -lower surface.\n");
        //floppySync = 0;
        
    }else{
        df[driveSelected].side = 1;
        //df[driveSelected].prb &= 0xFB;
        //printf(" -upper surface.\n");
        //floppySync = 0;
    }
    
    
    df[driveSelected].prb = PRB;
    
    //printf("\n");
    CIAA.pra &= 0xC3;
    CIAA.pra |= df[driveSelected].pra;
    
    //CIAB.prb |= df[driveSelected].prb & 0x87;
    

}
