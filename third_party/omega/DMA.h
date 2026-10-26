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
