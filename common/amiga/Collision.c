// frank-amiga — Denise collision detection (CLXCON/CLXDAT).
//
// SPDX-License-Identifier: MPL-2.0
// See Collision.h. Rows are bitmaps of raster columns, most significant bit
// first (column c is bit 31 - c % 32 of word c / 32), like the plane words.
#include "Collision.h"

#include <string.h>

#include "../omega/Chipset.h"

#define CLX_WORDS 32                 // 1024 raster columns
#define CLX_ACTIVE_FRAMES 100

int clxActive;

static uint16_t con;                 // CLXCON
static uint16_t dat;                 // CLXDAT (cleared by a read)
static uint32_t pfOdd[CLX_WORDS + 1], pfEven[CLX_WORDS + 1];
static uint32_t spr[4][CLX_WORDS + 1];
// Where the bitplanes match with no data (all planes 0): the columns the
// line's blocks did not cover.
static uint32_t oddIdle, evenIdle;
static int lo = CLX_WORDS, hi = -1;  // words touched this line
static unsigned groups;              // sprite groups with pixels this line

// Columns where the planes of one playfield (index 0: planes 1/3/5, 1:
// planes 2/4/6) match CLXCON.
static inline uint16_t pfMatch(int pf, const uint16_t *p) {
    uint16_t m = 0xffff;
    for (int i = pf; i < 6; i += 2) {
        if (!(con & (0x40u << i)))
            continue;
        m &= (con >> i) & 1u ? p[i] : (uint16_t)~p[i];
    }
    return m;
}

static inline int idleMatch(int pf) {
    for (int i = pf; i < 6; i += 2)
        if ((con & (0x40u << i)) && ((con >> i) & 1u))
            return 0;
    return 1;
}

static void resetLine(void) {
    for (int w = 0; w <= CLX_WORDS; ++w) {
        pfOdd[w] = oddIdle;
        pfEven[w] = evenIdle;
    }
    memset(spr, 0, sizeof spr);
}

void clxWriteCon(uint16_t value) {
    con = value;
    oddIdle = idleMatch(0) ? ~0u : 0u;
    evenIdle = idleMatch(1) ? ~0u : 0u;
    resetLine();
    lo = CLX_WORDS;
    hi = -1;
    groups = 0;
}

uint16_t clxRead(void) {
    if (!clxActive)
        clxWriteCon(con);            // start from a clean line
    clxActive = CLX_ACTIVE_FRAMES;
    const uint16_t v = dat | 0x8000u;
    dat = 0;
    return v;
}

void clxFrame(void) {
    if (clxActive)
        --clxActive;
}

// Puts `width` (16 or 32) columns, left aligned in `bits`, at column x.
static inline void put(uint32_t *row, int x, uint32_t bits, int width, int or) {
    if (x < 0 || x > CLX_WORDS * 32 - 32)
        return;
    const int w = x >> 5, s = x & 31;
    const uint64_t mask = (uint64_t)(width == 32 ? ~0u : 0xffff0000u) << 32 >> s;
    const uint64_t v = (uint64_t)bits << 32 >> s;
    const uint32_t mh = (uint32_t)(mask >> 32), ml = (uint32_t)mask;
    if (or) {
        row[w] |= (uint32_t)(v >> 32);
        row[w + 1] |= (uint32_t)v;
    } else {
        row[w] = (row[w] & ~mh) | (uint32_t)(v >> 32);
        row[w + 1] = (row[w + 1] & ~ml) | (uint32_t)v;
    }
    if (w < lo) lo = w;
    if (w + 1 > hi) hi = w + 1;
}

// Each of 16 pixels as two columns.
static inline uint32_t doubled(uint16_t v) {
    uint32_t x = v;
    x = (x | x << 8) & 0x00ff00ffu;
    x = (x | x << 4) & 0x0f0f0f0fu;
    x = (x | x << 2) & 0x33333333u;
    x = (x | x << 1) & 0x55555555u;
    return x | x << 1;
}

// PF2H - PF1H in columns (2 per lores pixel, LORES and HIRES alike): the
// even planes' offset from the odd ones.
static inline int evenShift(void) {
    return 2 * ((int)((chipset.bplcon1 >> 4) & 15) - (int)(chipset.bplcon1 & 15));
}

void clxHiresBlock(int x, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4) {
    const uint16_t p[6] = { p1, p2, p3, p4, 0, 0 };
    put(pfOdd, x, (uint32_t)pfMatch(0, p) << 16, 16, 0);
    put(pfEven, x + evenShift(), (uint32_t)pfMatch(1, p) << 16, 16, 0);
}

void clxLoresBlock(int x, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4,
                   uint16_t p5, uint16_t p6) {
    const uint16_t p[6] = { p1, p2, p3, p4, p5, p6 };
    put(pfOdd, x, doubled(pfMatch(0, p)), 32, 0);
    put(pfEven, x + evenShift(), doubled(pfMatch(1, p)), 32, 0);
}

void clxSprite(int n, int x, uint16_t bits) {
    // Odd sprites count only with their CLXCON ENSP bit.
    if ((n & 1) && !(con & (0x1000u << (n >> 1))))
        return;
    if (!bits)
        return;
    put(spr[n >> 1], x, doubled(bits), 32, 1);
    groups |= 1u << (n >> 1);
}

void clxLineEnd(void) {
    if (hi < 0)
        return;
    uint16_t d = 0;
    for (int w = lo; w <= hi; ++w) {
        const uint32_t o = pfOdd[w], e = pfEven[w];
        if (o & e) d |= 1u;
        if (!groups)
            continue;
        const uint32_t s0 = spr[0][w], s1 = spr[1][w], s2 = spr[2][w], s3 = spr[3][w];
        if (o & s0) d |= 1u << 1;
        if (o & s1) d |= 1u << 2;
        if (o & s2) d |= 1u << 3;
        if (o & s3) d |= 1u << 4;
        if (e & s0) d |= 1u << 5;
        if (e & s1) d |= 1u << 6;
        if (e & s2) d |= 1u << 7;
        if (e & s3) d |= 1u << 8;
        if (s0 & s1) d |= 1u << 9;
        if (s0 & s2) d |= 1u << 10;
        if (s0 & s3) d |= 1u << 11;
        if (s1 & s2) d |= 1u << 12;
        if (s1 & s3) d |= 1u << 13;
        if (s2 & s3) d |= 1u << 14;
    }
    dat |= d;
    for (int w = lo; w <= hi; ++w) {
        pfOdd[w] = oddIdle;
        pfEven[w] = evenIdle;
        spr[0][w] = spr[1][w] = spr[2][w] = spr[3][w] = 0;
    }
    lo = CLX_WORDS;
    hi = -1;
    groups = 0;
}
