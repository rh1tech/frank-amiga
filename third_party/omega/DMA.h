//
//  DMA.h
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

#ifndef DMA_h
#define DMA_h

#include <stdint.h>

#include <stdio.h>

void dma_execute();
// Runs several DMA slots; equivalent to calling dma_execute() `slots` times.
void dma_run(int slots);
// Raster column (hires pixels) where a colour register write at the
// current beam position takes effect: column 0 is the first pixel of the
// line's first fetched word. May be negative or past the image.
int dmaBeamColumn(void);
// Set by writes to DIWSTRT/DIWSTOP, DDFSTRT/DDFSTOP, BPLCON0 and DMACON:
// dma_execute() then recomputes its cached per-line display state.
extern int dmaLineStateDirty;
// dmaIdleUntil()'s cached horizon; cleared by every CPU write to a custom
// register (and at reset), which may start the blitter or move the Copper.
extern int dmaIdleCacheValid;
void dmaBitplanePointerWrite(unsigned plane, int highWord);
int copperExecute();
int blitterExecute();

void waitFreeSlot();

// frank-amiga: catch-up scheduling (DMA.c dmaRunSlice()): the CPU runs a
// slice first; its custom/CIA register accesses bring the chipset up to its
// cycle first.
extern int dmaInCpu;
void dmaCatchUp(void);
int dmaRunSlice(int *cpuBalance);   // returns the slots run
#ifdef FRANK_PARALLEL
// frank-amiga: the chipset runs on the other core (slave/src/parallel.h).
void parSync(void);
#define DMA_CATCH_UP() parSync()
// A custom register access (word offset `reg`, `write`): some need not
// wait for the chipset (parSkipSync()); a write that does makes the CPU's
// shortcuts invalid until the chipset has run on (parWrote()).
int parSkipSync(int write, unsigned reg);
void parWrote(void);
// VPOSR/VHPOSR at the CPU's slot without waiting (slave link_pump.c).
int parBeamRead(uint32_t *vposr, uint32_t *vhposr);
#define DMA_CATCH_UP_REG(write, reg) do { \
        if (!parSkipSync((write), (reg))) { parSync(); if (write) parWrote(); } \
    } while (0)
#else
#define DMA_CATCH_UP() do { if (dmaInCpu) dmaCatchUp(); } while (0)
#define DMA_CATCH_UP_REG(write, reg) DMA_CATCH_UP()
#endif
// frank-amiga: slots until a computed blit's end (state 3), 0 if none is
// waiting out its time; whether the blitter is idle or only waiting (its
// registers may be written without effect on a blit).
int dmaBlitWaitRemaining(void);
int dmaBlitterFree(void);
// frank-amiga: the beam at the next slot (see DMA.c); 0 if not followable.
int dmaBeamNext(uint32_t *pos, uint32_t *info, uint32_t *hi);

void dmaVideoStandardInit(int shown_rows);   // image rows the display shows: 240 or 256
void evenCycle(void);
void oddCycle(void);
void dramCycle(void);
void diskCycle(void);

void audio0Cycle(void);
void audio1Cycle(void);
void audio2Cycle(void);
void audio3Cycle(void);


void spriteCycle(void);


void plane4(void);
void plane6(void);
void plane2(void);
void plane3(void);
void plane5(void);

void loresPlane1(void);
void hiresPlane1(void);

int blitterCopyCycle();
int blitterLineCycle();

#endif /* DMA_h */
