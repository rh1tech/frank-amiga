// frank-amiga — Denise collision detection (CLXCON/CLXDAT).
//
// SPDX-License-Identifier: MPL-2.0
// Omega never set CLXDAT. Each image row collects, per raster column, where
// the odd and even bitplanes match CLXCON and where each sprite pair group
// has pixels; the line end ANDs the groups into CLXDAT. The work only runs
// while a program reads CLXDAT (clxActive counts frames down from its last
// read), so titles that never look at collisions pay one test per block.
#ifndef COLLISION_H
#define COLLISION_H

#include <stdint.h>

extern int clxActive;

void clxWriteCon(uint16_t value);
uint16_t clxRead(void);
void clxFrame(void);

void clxHiresBlock(int x, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4);
void clxLoresBlock(int x, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4,
                   uint16_t p5, uint16_t p6);
// Sprite `n` (0-7): `bits` = its pixels (plane A | plane B), bit 15 at
// raster column x (2 columns per lores pixel).
void clxSprite(int n, int x, uint16_t bits);
void clxLineEnd(void);

#endif
