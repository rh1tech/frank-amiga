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
#include "DMA.h"
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

// frank-amiga: DF0 and DF1 take disks, each with its own ADF and track
// buffer (DF1's right after DF0's).
#define FLOPPY_DISK_DRIVES 2
#define drive_track_cache(d) (df0_track_cache + (d) * 0x4000)
static const uint8_t *drive_adf[FLOPPY_DISK_DRIVES];
static int cached_cylinder[FLOPPY_DISK_DRIVES] = { -1, -1 };
static int cached_side[FLOPPY_DISK_DRIVES] = { -1, -1 };

// frank-amiga: drives whose tracks come as MFM from the master (flux images,
// decoded there track by track): the track the head is on is asked for and
// reads as nothing until it arrives (as a drive's head settling).
static volatile int ext_mode[FLOPPY_DISK_DRIVES];
static volatile int ext_want[FLOPPY_DISK_DRIVES] = { -1, -1 };
static volatile int ext_have[FLOPPY_DISK_DRIVES] = { -1, -1 };
static volatile uint32_t ext_len[FLOPPY_DISK_DRIVES];
static uint16_t timedCells[FLOPPY_DISK_DRIVES][512];     /* see floppyWordDue() */
static volatile int timedCount[FLOPPY_DISK_DRIVES];

static int floppyEnsureTrackCached(int drive) {
    if (drive < FLOPPY_DISK_DRIVES && ext_mode[drive]) {
        const int t = df[drive].cylinder * 2 + df[drive].side;
        if (ext_have[drive] == t)
            return 1;
        ext_want[drive] = t;
        return 0;
    }
    if (drive >= FLOPPY_DISK_DRIVES || !drive_adf[drive])
        return 0;
    if (cached_cylinder[drive] == df[drive].cylinder &&
        cached_side[drive] == df[drive].side)
        return 1;
    if (!ADF2MFM_track_from_mem(drive_adf[drive], FLOPPY_ADF_SIZE,
                                df[drive].cylinder, df[drive].side,
                                drive_track_cache(drive)))
        return 0;
    cached_cylinder[drive] = df[drive].cylinder;
    cached_side[drive] = df[drive].side;
    return 1;
}

// An empty drive's track: a sync word and nothing else.
static void floppyEmptyTrack(int drive) {
    volatile uint32_t *track = (volatile uint32_t *)drive_track_cache(drive);
    track[0] = 0x8944u;
    for (int i = 1; i < DF0_TRACK_WORDS; i++)
        track[i] = 0;
}

int floppyMountExternal(int drive) {
    if (drive >= FLOPPY_DISK_DRIVES)
        return 0;
    drive_adf[drive] = NULL;
    ext_have[drive] = ext_want[drive] = -1;
    ext_len[drive] = 12668;
    ext_mode[drive] = 1;
    df[drive].mfmData = drive_track_cache(drive);
    df[drive].hasDisk = 0;
    df[drive].pra &= (uint8_t)~0x08;           // /WPRO: write protected
    floppyEmptyTrack(drive);
    return 1;
}

int floppyTrackWanted(int *drive, int *track) {
    for (int d = 0; d < FLOPPY_DISK_DRIVES; d++)
        if (ext_mode[d] && ext_want[d] >= 0 && ext_want[d] != ext_have[d]) {
            *drive = d;
            *track = ext_want[d];
            return 1;
        }
    return 0;
}

uint8_t *floppyTrackBuffer(int drive) { return drive_track_cache(drive); }

void floppyTrackLoaded(int drive, int track, uint32_t len, const uint16_t *cells, int ncells) {
    ext_len[drive] = len < 1000u ? 12668u : len;
    if (ncells > 512) ncells = 512;
    if (ncells > 0) memcpy(timedCells[drive], cells, (size_t)ncells * 2u);
    timedCount[drive] = ncells > 0 ? ncells : 0;
    __atomic_store_n(&ext_have[drive], track, __ATOMIC_RELEASE);
}

// A flux drive whose track has not arrived yet holds disk DMA back (the
// words would be gaps, and a loader that started its read at once would get
// a broken track and retry forever); the disk just reaches the data later.
// No drive at the port, or no disk in it: no bits come off it.
static int drive_connected[4];
int floppyPortEmpty(void) { return driveSelected >= 4 || !drive_connected[driveSelected]; }
int floppyHasMedia(void) {
    const int d = driveSelected;
    return d < 4 && drive_connected[d] && df[d].hasDisk;
}

int floppyDataReady(void) {
    const int d = driveSelected;
    return d >= FLOPPY_DISK_DRIVES || !ext_mode[d] || !df[d].hasDisk ||
           floppyEnsureTrackCached(d);
}

static inline uint32_t floppyTrackLength(int drive) {
    return drive < FLOPPY_DISK_DRIVES && ext_mode[drive] ? ext_len[drive] : 12668u;
}

int floppyMountADF(int drive, const uint8_t *adf, uint32_t size) {
    if (drive >= FLOPPY_DISK_DRIVES || !adf || size != FLOPPY_ADF_SIZE)
        return 0;
    ext_mode[drive] = 0;
    drive_adf[drive] = adf;
    cached_cylinder[drive] = -1;
    cached_side[drive] = -1;
    df[drive].mfmData = drive_track_cache(drive);
    df[drive].hasDisk = 0;
    // /WPRO: only a disk in PSRAM can take writes.
#ifndef FLOPPY_ADF_WRITABLE
#define FLOPPY_ADF_WRITABLE(a) ((a) >= 0x11000000u && (a) < 0x12000000u)
#endif
    if (FLOPPY_ADF_WRITABLE((uintptr_t)adf))
        df[drive].pra |= 0x08;
    else
        df[drive].pra &= (uint8_t)~0x08;

    // Before insertion expose an invalid header rather than mounted data, so
    // Kickstart can finish its no-disk retry and display the hand screen.
    floppyEmptyTrack(drive);
    return 1;
}

// DF1 present (an external drive, detected by Kickstart at boot).
static int drive_connected[4] = { 1, 0, 0, 0 };
void floppyConnect(int drive) {
    if (drive < FLOPPY_DISK_DRIVES) {
        drive_connected[drive] = 1;
        if (!df[drive].mfmData) {
            df[drive].mfmData = drive_track_cache(drive);
            floppyEmptyTrack(drive);
        }
    }
}

// ── Writes ─────────────────────────────────────────────────────────────────
// The written MFM stream collects in the drive's track buffer (the read
// cache, invalidated afterwards); at the end its AmigaDOS sectors are
// decoded into the ADF in PSRAM. Other formats are not kept.
static uint32_t write_pos;

void floppyWriteByte(uint8_t b) {
    const int d = driveSelected;
    if (d >= FLOPPY_DISK_DRIVES || !df[d].hasDisk || ext_mode[d] || write_pos >= 0x4000)
        return;
    ((volatile uint8_t *)drive_track_cache(d))[write_pos++] = b;
}

void floppyWriteAbort(void) { write_pos = 0; }

static inline uint32_t mfmLong(const volatile uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint32_t mfmDecode(const volatile uint8_t *p, int span) {
    return (mfmLong(p) & 0x55555555u) << 1 | (mfmLong(p + span) & 0x55555555u);
}

void floppyWriteDone(void) {
    const int d = driveSelected;
    const uint32_t n = write_pos;
    write_pos = 0;
    if (d >= FLOPPY_DISK_DRIVES || !drive_adf[d] || !df[d].hasDisk)
        return;
    // Only the uploaded copy in PSRAM is writable (not a flash image).
    const uintptr_t a = (uintptr_t)drive_adf[d];
    if (!FLOPPY_ADF_WRITABLE(a))
        return;
    uint8_t *adf = (uint8_t *)a;
    const volatile uint8_t *t = drive_track_cache(d);
    const int track = df[d].cylinder * 2 + df[d].side;
    int sectors = 0;
    for (uint32_t i = 0; i + 4 + 1080 <= n; ) {
        if (!(t[i] == 0x44 && t[i + 1] == 0x89 && t[i + 2] == 0x44 && t[i + 3] == 0x89)) {
            i++;
            continue;
        }
        const volatile uint8_t *h = t + i + 4;
        const uint32_t info = mfmDecode(h, 4);
        const int trk = (int)(info >> 16 & 0xff), sec = (int)(info >> 8 & 0xff);
        // Data checksum: XOR of the raw data longs, masked.
        uint32_t sum = 0;
        for (int k = 0; k < 256; k++) sum ^= mfmLong(h + 56 + 4 * k);
        sum &= 0x55555555u;
        if ((info >> 24) == 0xff && trk == track && sec < 11 &&
            sum == mfmDecode(h + 48, 4)) {
            uint8_t *dst = adf + (uint32_t)(trk * 11 + sec) * 512u;
            for (int k = 0; k < 128; k++) {
                const uint32_t v = mfmDecode(h + 56 + 4 * k, 512);
                dst[4 * k] = (uint8_t)(v >> 24);
                dst[4 * k + 1] = (uint8_t)(v >> 16);
                dst[4 * k + 2] = (uint8_t)(v >> 8);
                dst[4 * k + 3] = (uint8_t)v;
            }
            sectors++;
        }
        i += 4 + 1080;
    }
    cached_cylinder[d] = -1;            // the buffer held the write
    if (sectors) {
        __atomic_fetch_or(&floppyDirtyTracks[d][track >> 3], (uint8_t)(1u << (track & 7)),
                          __ATOMIC_RELAXED);
        floppyDirty[d] = 1;
    }
    printf("DF%d: wrote track %d, %d sectors\n", d, track, sectors);
}
#else
static int drive_connected[4] = { 1, 0, 0, 0 };
int floppyDataReady(void) { return 1; }
int floppyHasMedia(void) { return driveSelected == 0 && df[0].hasDisk; }
int floppyPortEmpty(void) { return driveSelected != 0; }
void floppyWriteByte(uint8_t b) { (void)b; }
void floppyWriteDone(void) {}
void floppyWriteAbort(void) {}
#endif
#ifdef FRANK_DSK_SUM
// Debug: a checksum of the words each disk DMA read stores, printed when it
// completes (compare the board with the native build).
uint32_t frankDskSum;
static int dskSumN;
void frankDskDone(void) {
    if (dskSumN++ < 400)
        printf("DSKSUM %d cyl %d side %d sum %08lx\n", dskSumN, df[driveSelected].cylinder,
               df[driveSelected].side, (unsigned long)frankDskSum);
    frankDskSum = 0;
}
#endif
volatile uint8_t floppyDirty[2];
volatile uint8_t floppyDirtyTracks[2][20];

#ifndef PICO_BUILD
// frank-amiga native: DF0 from raw MFM tracks (the master's flux decoding,
// dumped over SWD): track t at nativeTracks + t * 16 KB, nativeLen[t] bytes,
// a timed track's cells 14 KB on (nativeTiming[t] entries).
static const uint8_t *nativeTracks;
static uint32_t nativeLen[168];
static uint16_t nativeTiming[168];
void floppyNativeTracks(const uint8_t *tracks, const uint32_t *len, const uint32_t *timing) {
    nativeTracks = tracks;
    memcpy(nativeLen, len, sizeof nativeLen);
    for (int t = 0; t < 168; t++) nativeTiming[t] = timing ? (uint16_t)timing[t] : 0;
}
#endif

// frank-amiga: timed tracks. A flux image whose bit cells vary along a
// track (a copy protection's sector written at another rate, which it then
// times, as Rob Northen Copylock does) comes with the cells' length per
// 256-bit block. Such a track turns at its real speed: floppyLineTick()
// runs a clock, and disk DMA takes words only as the disk would deliver
// them (floppyWordDue()). Every other track reads as fast as DMA asks.
static int32_t timedCreditNs;
#ifdef PICO_BUILD
static const uint16_t *timedTable(int d, int *n) {
    if (d >= FLOPPY_DISK_DRIVES || !ext_mode[d] || !timedCount[d] ||
        ext_have[d] != df[d].cylinder * 2 + df[d].side)
        return NULL;
    *n = timedCount[d];
    return timedCells[d];
}
static uint32_t timedLength(int d) { return ext_len[d]; }
#else
static const uint16_t *timedTable(int d, int *n) {
    if (d != 0 || !nativeTracks) return NULL;
    const int t = df[0].cylinder * 2 + df[0].side;
    if (!nativeTiming[t]) return NULL;
    *n = nativeTiming[t] < 512 ? nativeTiming[t] : 512;
    return (const uint16_t *)(nativeTracks + (uint32_t)t * 0x4000u + 0x3800u);
}
static uint32_t timedLength(int d) {
    (void)d;
    const int t = df[0].cylinder * 2 + df[0].side;
    return nativeLen[t] ? nativeLen[t] : 12668u;
}
#endif

static inline int32_t timedCell(const uint16_t *tab, int n, uint32_t byte) {
    uint32_t b = byte >> 5;              /* 256-bit blocks */
    if (b >= (uint32_t)n) b = (uint32_t)n - 1u;
    return tab[b];
}

int floppyWordDue(void) {
    int n;
    const uint16_t *tab = timedTable(driveSelected, &n);
    if (!tab) return 1;
    const int32_t need = 16 * timedCell(tab, n, (uint32_t)df[driveSelected].index);
    if (timedCreditNs < need) {
        return 0;
    }
    timedCreditNs -= need;
    return 1;
}

// frank-amiga: DSKBYTR. On a timed track the head's bit position is worked
// out from the line clock and the beam's place on the line (a colour clock is
// ~282 ns), so a program polling WORDEQUAL with disk DMA off sees each sync
// word go by (Copylock times them with a CIA). WORDEQUAL is reported if the
// sync passed since the last read, the low byte is the one under the head.
// Other tracks keep the word-sync flag disk DMA leaves.
static const uint8_t *timedBits(int d) {
#ifdef PICO_BUILD
    return drive_track_cache(d);
#else
    (void)d;
    return nativeTracks + (uint32_t)(df[0].cylinder * 2 + df[0].side) * 0x4000u;
#endif
}

uint16_t floppyByteReg(void) {
    const int d = driveSelected;
    int n;
    const uint16_t *tab = timedTable(d, &n);
    const uint16_t dmaon = (chipset.dsklen & 0x8000) && (chipset.dmaconr & 0x210) == 0x210 ? 0x4000 : 0;
    if (!tab || !df[d].hasDisk)
        return (uint16_t)((chipset.dskbytr & ~0x4000) | dmaon);
    /* Bit positions on the track: `pos` is under the head now; bytes end on
     * the grid of the last sync seen (as Paula's byte counter restarts
     * there), and each one is "ready" once, until DSKBYTR is read. */
    static uint32_t lastPos, syncEnd, lastByteEnd;
    const uint8_t *bits = timedBits(d);
    const uint32_t total = timedLength(d) * 8u;
    const int32_t cell = timedCell(tab, n, (uint32_t)df[d].index);
    uint32_t pos = (uint32_t)df[d].index * 8u +
                   (uint32_t)((timedCreditNs + internal.hPos * 282) / (cell ? cell : 2000));
    pos %= total;
    const uint32_t from = lastPos <= pos && pos - lastPos <= 256u ? lastPos : (pos >= 256u ? pos - 256u : 0u);
    lastPos = pos;
    uint16_t r = dmaon;
    uint32_t w = 0;
    for (uint32_t p = from >= 15u ? from - 15u : 0u; p < pos; p++) {
        w = (w << 1 | ((bits[p >> 3] >> (7 - (p & 7))) & 1u)) & 0xFFFFu;
        if (p >= from && p >= 15u && w == chipset.dsksync) {
            r |= 0x1000;                 /* WORDEQUAL */
            syncEnd = p + 1u;
        }
    }
    const uint32_t off = (pos + 8u - syncEnd % 8u) % 8u;
    const uint32_t byteEnd = pos >= off ? pos - off : 0u;
    if (lastByteEnd > total) lastByteEnd = 0;
    if (byteEnd != lastByteEnd && byteEnd >= 8u) {
        lastByteEnd = byteEnd;
        r |= 0x8000;                     /* BYTEREADY */
    }
    uint32_t byte = 0;
    for (uint32_t p = lastByteEnd >= 8u ? lastByteEnd - 8u : 0u; p < lastByteEnd; p++)
        byte = byte << 1 | ((bits[p >> 3] >> (7 - (p & 7))) & 1u);
    return (uint16_t)(r | (byte & 0xFF));
}

// frank-amiga: a spinning disk pulses /INDEX once a revolution (300 rpm:
// every 0.2 s, 3125 PAL lines) whether or not disk DMA reads it; loaders
// wait for it with DMA off (Humans 2 polls CIA-B's FLG). DMA reads keep
// their own pulse at the track's end (floppyDataRead()), so this one only
// runs while no disk DMA is active (or DMA waits for a flux track). A timed
// track instead turns under the head line by line, its index at its start.
void floppyLineTick(void) {
    static int lines;
    Fd_t *d = &df[driveSelected];
    if (!d->hasDisk || (d->prb & 0x80)) {
        return;
    }
    int n;
    const uint16_t *tab = timedTable(driveSelected, &n);
    if (tab) {
        timedCreditNs += 64000;          /* a PAL line */
        const int dmaReads = (chipset.dmaconr & 0x210) && (chipset.dsklen & 0x8000) &&
                             (chipset.dsklen & 0x3FFF) && !(chipset.dsklen & 0x4000);
        if (!dmaReads) {
            const uint32_t len = timedLength(driveSelected);
            /* Paula sees the sync word go by with or without DMA. */
            static uint32_t window;
            const uint8_t *bits = timedBits(driveSelected);
            d->index &= ~1;              /* whole words, as disk DMA reads them */
            for (;;) {
                const int32_t need = 16 * timedCell(tab, n, (uint32_t)d->index);
                if (timedCreditNs < need) break;
                timedCreditNs -= need;
                const uint32_t word = (uint32_t)bits[d->index] << 8 | bits[d->index + 1];
                for (int k = 15; k >= 0; k--) {
                    window = window << 1 | ((word >> k) & 1u);
                    if ((window & 0xFFFFu) == chipset.dsksync)
                        putChipReg16[INTREQ](0x9000);   /* DSKSYNC */
                }
                d->index += 2;
                if ((uint32_t)d->index >= len) {
                    d->index = 0;
                    CIAIndex(&CIAB);
                }
            }
        }
        if (timedCreditNs > 128000) timedCreditNs = 128000;
        return;
    }
    if ((chipset.dsklen & 0x8000) && floppyDataReady()) {
        return;
    }
    if (++lines >= 3125) {
        lines = 0;
        CIAIndex(&CIAB);
    }
}

void floppyIndexReset(){
    
       df[driveSelected].index = 4;
    
}


uint8_t floppyDataRead(){ //this function should be called by the DMA
#ifndef PICO_BUILD
    if (nativeTracks && driveSelected == 0) {
        const int t = df[0].cylinder * 2 + df[0].side;
        const uint32_t len = nativeLen[t] ? nativeLen[t] : 12668u;
        if ((uint32_t)df[0].index >= len) {
            df[0].index = 0;
            CIAIndex(&CIAB);
        }
        return nativeLen[t] ? nativeTracks[(uint32_t)t * 0x4000u + (uint32_t)df[0].index++] : 0;
    }
#endif

#ifdef PICO_BUILD
    // DF0/DF1 hold one track: the ADF's current one, or the empty drive's.
    int streamed = driveSelected < FLOPPY_DISK_DRIVES;
    if (streamed && df[driveSelected].hasDisk &&
        !floppyEnsureTrackCached(driveSelected))
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

#ifdef PICO_BUILD
    if ((uint32_t)df[driveSelected].index >= floppyTrackLength(driveSelected)) {
#else
    if(df[driveSelected].index>12667){ //ADF track has 12798 bytes, but on a normal AmigaOS disk 12668 are used.
#endif
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
#ifdef PICO_BUILD
    if (drive >= FLOPPY_DISK_DRIVES || !drive_connected[drive])
        return;     // only DF0 and a connected DF1 take disks
#else
    if(drive != 0){
        return;     // only DF0 takes disks
    }
#endif

    if(df[drive].idMode < 0){
        // frank-amiga: only drives Kickstart has identified (DF1's ID mode
        // now comes back at every motor off, so not "!= 0")
        return;
    }
    
    if(df[drive].hasDisk){
        df[drive].hasDisk = 0;
        df[drive].pra &= 0xFB;      // /CHNG=0 (change: disk removed)
        df[drive].pra |= 0x20;      // /DKRDY=1 (no media ready)
        printf("Disk ejected from df%d:\n",drive);
    }else{
#ifdef PICO_BUILD
        if (drive_adf[drive] && !floppyEnsureTrackCached(drive))
            return;
        if (drive < FLOPPY_DISK_DRIVES && ext_mode[drive])
            floppyEnsureTrackCached(drive);     // ask for it now
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
    // DF0 always has an MFM track buffer (in PSRAM); DF1 once connected.
    df[drive].mfmData = drive == 0 ? psram_ptr(PSRAM_DF0_OFFSET) : NULL;
    if (drive < FLOPPY_DISK_DRIVES) {
        drive_adf[drive] = NULL;
        cached_cylinder[drive] = -1;
        cached_side[drive] = -1;
        drive_connected[drive] = drive == 0;
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

// One selected drive's response to a CIAB PRB write (motor, step, side).
static void floppyDriveState(uint8_t PRB){
#ifndef PICO_BUILD
    static int count = 0;
#endif
    //ID mode... to identify external drives...
     // ID mode (frank-amiga): while the motor is off after an ID reload,
     // each selection shows the next ID bit on /RDY (drive present: 1s,
     // /RDY low). It only changes /RDY: steps and side select still work,
     // and turning the motor on ends it.
     if (df[driveSelected].idMode > 0 && !(PRB & 0x80))
         df[driveSelected].idMode = 0;
     int idBit = 0;
     if (df[driveSelected].idMode > 0) {
         idBit = drive_connected[driveSelected] || df[driveSelected].hasDisk;
         df[driveSelected].idMode -= 1;
     }

    // If no change in state just return
    if(PRB == df[driveSelected].prb){
        if (idBit)
            df[driveSelected].pra &= 0xDF;
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
        } else if (driveSelected != 0 && !(df[driveSelected].prb & 0x80)) {
            // frank-amiga: an external drive reloads its ID at every motor
            // off, so trackloaders can identify DF1 themselves at any time.
            df[driveSelected].idMode = 32;
        }
        
        df[driveSelected].pra  |= 0x20;     //Drive not ready
        
    }else{
        //printf(" Motor On ");
        //df[driveSelected].prb  &= 0x7F;
        
        // Report drive ready (/DKRDY=0) for drives that physically exist.
        // Drive 0 is always present; without this, KS 2.04's 200-read polling
        // loop sees /DKRDY=1 and falls back to its internal ROM disk instead
        // of showing the insert-disk screen when no ADF is loaded.
        if(df[driveSelected].hasDisk || drive_connected[driveSelected]){
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
        // frank-amiga: only with a disk in the drive; an empty drive keeps
        // /CHNG asserted however often it steps (trackloaders step and poll
        // /CHNG to wait for the next disk).
        if (df[driveSelected].hasDisk)
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
#ifdef PICO_BUILD
    // A flux-image drive asks for the track under its head as soon as the
    // head gets there (the master needs a moment to decode it).
    if (driveSelected < FLOPPY_DISK_DRIVES && ext_mode[driveSelected] &&
        df[driveSelected].hasDisk)
        floppyEnsureTrackCached(driveSelected);
#endif
    if (idBit)
        df[driveSelected].pra &= 0xDF;   // ID bit: /RDY low
    
}

void floppySetState(){            //To be called when Writes to CIAB prb happen.
    const uint8_t PRB = CIAB.prb;
    // frank-amiga: every drive whose /SELx is low takes the motor, step and
    // side lines, and their status outputs are wired-OR (active low) on the
    // bus. Loaders that wait for a disk "in any drive" select them all and
    // step them together; stepping only one would leave DF0's /CHNG set.
    const int sel = (~PRB >> 3) & 0xF;
    if (!sel) {
        CIAA.pra &=0xC3;    //Need to take the Drives off the floppy bus.
        return;
    }
    if (!(sel & (sel - 1))) {
        driveSelected = __builtin_ctz(sel);
        floppyDriveState(PRB);
        floppyState();
        // No drive at that port: nothing pulls the status lines low (a
        // trackloader polling DF1 would take /TK0 for an empty drive and
        // wait on it for the next disk).
        if (!drive_connected[driveSelected])
            CIAA.pra |= 0x3C;
        return;
    }
    uint8_t lines = 0x3C;
    for (int d = 3; d >= 0; d--) {   // DF0 last: the lowest drive's data
        if (!(sel & (1 << d))) continue;
        driveSelected = d;
        floppyDriveState(PRB);
        if (drive_connected[d])
            lines &= df[d].pra;
    }
    CIAA.pra &= 0xC3;
    CIAA.pra |= lines & 0x3C;
}
