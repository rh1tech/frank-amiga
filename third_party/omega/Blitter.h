//
//  Blitter.h
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

#ifndef Blitter_h
#define Blitter_h

#include <stdio.h>
#include <stdint.h>
#include "Chipset.h"

//keep internal blitter state in one place.
typedef struct{
    uint16_t sizev;
}Blitter_t;

int OblitterExecute(void);

void blitter_execute(Chipset_t*);
// frank-amiga (DMA.c): blitter_execute() computes a blit at once, but its
// end (BBUSY clear, INTREQ BLIT) comes after the time the blit takes on the
// real chip: programs that clear INTREQ and enable the blitter interrupt
// right after BLTSIZE (Desert Dream) otherwise lose the interrupt.
void blitterFinished(void);
// A BLTSIZE write: ends a blit whose time is still running.
void blitterNewBlit(void);
// frank-amiga: BLTBDAT as shifted when written (BSH and DESC at that time),
// what channel B gives every word while its DMA is off (Chipset.c).
extern uint16_t blitterBHold;
void blitterHoldB(uint16_t value);

#endif /* Blitter_h */
