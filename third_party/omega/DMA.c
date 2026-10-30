//
//  DMA.c
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

#include "DMA.h"
#include "m68k.h"
#include <stdio.h>
#include <stdlib.h>
#include "Memory.h"
#include "Chipset.h"
#include "CIA.h"
#include "Audio.h"
#include "Host.h"
#if defined(PICO_BUILD) && OMEGA_ENABLE_HDMI
#include "HostRing.h"
#else
#define hostDirectHiresFast hostDirectHires
#define hostDirectLoresFast hostDirectLores
#endif
#include "Blitter.h"

#include "CPU.h"


#include "debug.h"

#include "Floppy.h"
#include "Collision.h"
#include "VideoStandard.h"
#include "DisplayLayout.h"







































































void (*DMALores[])() = {
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    audio0Cycle,
    evenCycle,
    audio1Cycle,
    evenCycle,
    audio2Cycle,
    evenCycle,
    audio3Cycle,
    evenCycle,
    spriteCycle,    //00
    evenCycle,
    spriteCycle,    //00
    evenCycle,
    spriteCycle,    //01
    evenCycle,
    spriteCycle,    //01
    evenCycle,
    spriteCycle,    //02
    evenCycle,
    spriteCycle,    //02
    evenCycle,
    spriteCycle,    //03
    evenCycle,
    spriteCycle,    //03
    evenCycle,
    spriteCycle,    //04
    evenCycle,
    spriteCycle,    //04
    evenCycle,
    spriteCycle,    //05
    evenCycle,
    spriteCycle,    //05
    evenCycle,
    spriteCycle,    //06
    evenCycle,
    spriteCycle,    //06
    evenCycle,
    spriteCycle,    //07
    evenCycle,
    spriteCycle,    //07
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,   //Normal start
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    plane4,
    plane6,
    plane2,
    evenCycle,
    plane3,
    plane5,
    loresPlane1,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,

};


void (*DMAHires[]) (void) = {
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    dramCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    diskCycle,
    evenCycle,
    audio0Cycle,
    evenCycle,
    audio1Cycle,
    evenCycle,
    audio2Cycle,
    evenCycle,
    audio3Cycle,
    evenCycle,
    spriteCycle,//00
    evenCycle,
    spriteCycle,//00
    evenCycle,
    spriteCycle,//01
    evenCycle,
    spriteCycle,//01
    evenCycle,
    spriteCycle,//02
    evenCycle,
    spriteCycle,//02
    evenCycle,
    spriteCycle,//03
    evenCycle,
    spriteCycle,//03
    evenCycle,
    spriteCycle,//04
    evenCycle,
    spriteCycle,//04
    evenCycle,
    spriteCycle,//05
    evenCycle,
    spriteCycle,//05
    evenCycle,
    spriteCycle,//06
    evenCycle,
    spriteCycle,//06
    evenCycle,
    spriteCycle,//07
    evenCycle,
    spriteCycle,//07
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,    //Normal Plane Start
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    plane4,
    plane2,
    plane3,
    hiresPlane1,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
    evenCycle,
    oddCycle,
};


void waitFreeSlot(){
#ifndef PICO_BUILD
    while(SDL_AtomicGet(&cpuWait)){
        //SDL_Delay(1);
    }
#endif
}

typedef struct {
    int lastCycle;
    int hiresWords;
    int loresWords;
    uint8_t enabledMask;
    uint8_t fetchEligibleMask;
    uint8_t inDisplayWindow;        // the line was inside DIW's vertical range
    uint8_t fetchedMask;
    uint8_t fetchWindowComplete;
    uint32_t pointerAtFetchCompletion[4];
    uint8_t pointerHighWriteMask;
    uint8_t pointerLowWriteMask;
    uint8_t pointerReloadBeforeFetchMask;
} BitplaneLineState;

static BitplaneLineState bitplaneLine;
static uint8_t lastFetchedMask;
static uint32_t copperWaitPosition = 0;

static void resetBitplaneLine(void) {
    bitplaneLine.hiresWords = 0;
    bitplaneLine.loresWords = 0;
    bitplaneLine.enabledMask = 0;
    bitplaneLine.fetchEligibleMask = 0;
    bitplaneLine.inDisplayWindow = 0;
    bitplaneLine.fetchedMask = 0;
    bitplaneLine.fetchWindowComplete = 0;
    bitplaneLine.pointerHighWriteMask = 0;
    bitplaneLine.pointerLowWriteMask = 0;
    bitplaneLine.pointerReloadBeforeFetchMask = 0;
    for (unsigned plane = 0; plane < 4; plane++)
        bitplaneLine.pointerAtFetchCompletion[plane] = 0;
}

// frank-amiga: the first slot of the bitplane fetch. OCS compares only
// DDFSTRT's H8-H3 (vAmiga's ddfMask()), so a LORES fetch starts at its
// 8-slot group. Arte's "artists" face is LORES under the HIRES screen's
// DDFSTRT $64: the group's plane 2/4 slots fell before it, so those planes
// fetched a word fewer a line than 1/3/5 and the face showed in stripes.
// (HIRES keeps DDFSTRT as written: its layouts key on $3C.)
static inline int fetchStart(void) {
    return (chipset.bplcon0 & 0x8000) ? chipset.ddfstrt : (chipset.ddfstrt & ~7);
}

static void markBitplaneFetched(unsigned plane) {
    uint8_t planeMask = (uint8_t)(1u << (plane - 1));
    bitplaneLine.fetchedMask |= planeMask;
}

void dmaBitplanePointerWrite(unsigned plane, int highWord) {
    if (plane >= 1 && plane <= 8) {
        uint8_t planeMask = (uint8_t)(1u << (plane - 1));
        if (highWord)
            bitplaneLine.pointerHighWriteMask |= planeMask;
        else
            bitplaneLine.pointerLowWriteMask |= planeMask;
        if ((bitplaneLine.pointerHighWriteMask &
             bitplaneLine.pointerLowWriteMask & planeMask) != 0 &&
            internal.hPos < fetchStart())
            bitplaneLine.pointerReloadBeforeFetchMask |= planeMask;
    }
}

static void applyBitplaneModulo(uint8_t planes) {
    if (planes & 0x01)
        chipset.bpl1pt += chipset.bpl1mod;
    if (planes & 0x02)
        chipset.bpl2pt += chipset.bpl2mod;
    if (planes & 0x04)
        chipset.bpl3pt += chipset.bpl1mod;
    if (planes & 0x08)
        chipset.bpl4pt += chipset.bpl2mod;
    if (planes & 0x10)
        chipset.bpl5pt += chipset.bpl1mod;
    if (planes & 0x20)
        chipset.bpl6pt += chipset.bpl2mod;
    if (planes & 0x40)
        chipset.bpl7pt += chipset.bpl1mod;
    if (planes & 0x80)
        chipset.bpl8pt += chipset.bpl2mod;
}

static void advanceBitplanePointers(void) {
    uint8_t fetched = bitplaneLine.fetchedMask;

    if (fetched != 0) {
        lastFetchedMask = fetched;
        return;
    }

    // Like OCS Agnus, a line outside the vertical display window fetches
    // nothing and adds no modulo. Adding it there moved every plane of a
    // game that loads BPLxPT at the top of the frame (RemGame: HAM, modulo
    // 212, display from line $2c) 44 lines of modulo too far on. Inside the
    // window the compatibility advancement stays: Kickstart 2.x/3.x load
    // their pointers mid-line just before a few no-fetch lines, and the
    // display layout is calibrated with the modulo applied there.
    // HDMI direct frames anchor narrow layouts (DDFSTRT >= 0x40) at their
    // beam position (see lineRasterDdf), so those lines behave as the
    // hardware does: Kickstart 3.1.4's insert-disk screen loads its
    // pointers on line $61 and enables its planes on $63, and the modulo of
    // the lines between moved its picture 3 words. Full-width layouts
    // (xSysInfo) keep the calibration.
    // frank-amiga: nor does a line with no plane enabled (BPU 0 all
    // along): Agnus has no bitplane slot to add a modulo in. Desert Dream's
    // intro keeps BPLCON0 at $0200 from its window's top ($1c) to $5d and
    // loads the pointers above; the last frame's five planes' modulos on
    // those 65 lines moved its globe 160 pixels left, wrapping into the
    // next interleaved plane's row.
    if (!bitplaneLine.inDisplayWindow || !bitplaneLine.enabledMask ||
        (hostDirectActive && !omegaDdfIsFullWidth(chipset.ddfstrt)))
        return;
    fetched = bitplaneLine.fetchEligibleMask != 0
        ? bitplaneLine.fetchEligibleMask
        : lastFetchedMask;
    fetched &= (uint8_t)~(bitplaneLine.pointerReloadBeforeFetchMask &
                          bitplaneLine.enabledMask);
    applyBitplaneModulo(fetched);
}

static uint8_t enabledBitplaneMask(void) {
    return (uint8_t)internal.bitplaneMask;
}

// frank-amiga: the vertical display window is a flip-flop (as vAmiga's
// Sequencer): set on the line VSTART names, cleared on VSTOP's line or the
// frame's last; a DIWSTRT/DIWSTOP write on such a line acts at once. A
// window already open stays open when VSTART moves below the beam: Arte's
// "ARTE" picture sets VSTART $29 and then $1D on line $20, and with the
// window closed between the two the Copper ran ahead of the bitplane DMA,
// loaded the five planes' pointers inside the fetch window and the planes
// came out a word apart each.
// On OCS the missing ninth comparator bits are fixed: VSTART8 is zero and
// VSTOP8 is the inverse of VSTOP7 (see omegaDiwVerticalStop()). ECS/AGA
// DIWHIGH programmability is outside this OCS chipset model.
static int diwVFlop;

static void diwVFlopUpdate(int line_start) {
    const int v = internal.vPos;
    const int start = omegaDiwVerticalStart(chipset.diwstrt);
    const int stop = omegaDiwVerticalStop(chipset.diwstop);
    if (v == stop || (line_start && v == OMEGA_VIDEO_FRAME_LINES - 1))
        diwVFlop = 0;
    else if (v == start)
        diwVFlop = 1;
}

// DIWSTRT or DIWSTOP written (Chipset.c).
void dmaDiwWritten(void) {
    diwVFlopUpdate(0);
}

static int displayWindowContainsLine(int vpos) {
    (void)vpos;   // the current line's
    return diwVFlop;
}

// Draws one HIRES word read for display only (no pointer moved).
static void __attribute__((noinline)) hiresPeekedWord(int row, int x,
                                                      uint16_t p1, uint16_t p2,
                                                      uint16_t p3, uint16_t p4) {
    if (clxActive)
        clxHiresBlock(x, p1, p2, p3, p4);
    if (hostDirectActive) {
        hostDirectHires(row, x, p1, p2, p3, p4);
    } else {
        paletteArgbSync();
        hiresPlanar2Chunky(hostRasterPixels(row, x), p1, p2, p3, p4);
        hostRasterWritten(row, x, 16);
    }
}

static void hiresDisplayPrefetch(void) {
    if (!bitplaneLine.fetchWindowComplete ||
        (chipset.bplcon0 & 0x8000) == 0 ||
        omegaDdfIsFullWidth(chipset.ddfstrt) ||
        (chipset.dmaconr & 0x300) != 0x300 ||
        !displayWindowContainsLine(internal.vPos) ||
        host.pixels == NULL ||
        host.rasterRow < 0 || host.rasterRow >= HOST_RASTER_H ||
        host.rasterX < 0 || host.rasterX + 15 >= HOST_RASTER_W)
        return;

    uint16_t p1 = 0, p2 = 0, p3 = 0, p4 = 0;
    if (internal.bitplaneMask & 0x01)
        p1 = CHIPW(bitplaneLine.pointerAtFetchCompletion[0]);
    if (internal.bitplaneMask & 0x02)
        p2 = CHIPW(bitplaneLine.pointerAtFetchCompletion[1]);
    if (internal.bitplaneMask & 0x04)
        p3 = CHIPW(bitplaneLine.pointerAtFetchCompletion[2]);
    if (internal.bitplaneMask & 0x08)
        p4 = CHIPW(bitplaneLine.pointerAtFetchCompletion[3]);

    hiresPeekedWord(host.rasterRow, host.rasterX, p1, p2, p3, p4);
    host.rasterX += 16;
}

int dmaLineStateDirty = 1;
// Cached with the line state (see dmaUpdateLineState()).
static int lineBitplaneWindow;   // bitplane DMA on and line in the window
static int lineFullWidth;        // omegaDdfIsFullWidth(DDFSTRT)
static int lineHiresDisplayTop;  // DIWSTRT line + upper overscan
static int lineRowRotation;      // omegaDdfRowRotation(DDFSTRT, DDFSTOP)

// Per-line display state. It only changes at a line boundary or when one of
// the registers listed in DMA.h is written, so it is recomputed then instead
// of on every slot. OR-ing unchanged plane masks again is a no-op, so the
// accumulated masks are identical to per-slot recomputation.
// ── Idle slot skipping ──────────────────────────────────────────────────────
// Most slots only offer themselves to the Copper and the blitter. While the
// Copper waits for a beam position not yet reached (or is off or frozen) and
// the blitter is idle, those slots do nothing, so dma_run() jumps over them
// in one step and only ticks the E clock. slotNext[h] is the first slot >= h
// that can do anything else: bitplane slots inside the fetch window, sprite
// slots with sprite DMA on, audio slots of enabled channels, disk slots
// (DSKLEN is not tracked), the fetch-window end and the last slot of a line.
#define SLOT_LAST 0xE3   // last slot of a long line
// Slot functions of the current line: DMALores/DMAHires, or a copy whose
// bitplane fetch starts before their first fetch group (0x38 LORES, 0x34
// HIRES). A display data fetch that starts early (overscan, e.g. DDFSTRT
// 0x20 in the Amiga Test Kit's crosshatch) takes those slots from sprite
// DMA, as on OCS; the fetch pattern keeps the tables' phase.
static void (**slotTable)() = DMALores;
static void (*earlyTable[SLOT_LAST + 1])();
static int earlyTableKey = -1;
// DDFSTRT that raster column 0 belongs to: an early LORES fetch places its
// first words left of the image (beam positions before 0x38), so the
// standard 320-pixel area keeps its columns and sprites their offset.
static int lineRasterDdf = 0x38;

// BPLCON1 fine scroll (HDMI direct frames): the playfield is delayed by
// PF1H lores pixels, 2 image columns each in LORES and HIRES alike. Both
// playfields use PF1H (different PF2H values, dual-playfield parallax, are
// not split yet).
static inline int bplScrollColumns(void) {
    return 2 * (chipset.bplcon1 & 0xf);
}

int dmaBeamColumn(void) {
    const int hires = (chipset.bplcon0 & 0x8000) != 0;
    const int first = 2 * lineRasterDdf +
                      (hires ? OMEGA_SPRITE_HIRES_OFFSET
                             : OMEGA_SPRITE_LORES_OFFSET);
    // A colour register write shows one colour clock (4 columns) before
    // the bitplane pixel reference: ATK's RGB palette screen, whose Copper
    // lines must meet its bitplane frame, left a 4-column gap otherwise.
    return 2 * (2 * internal.hPos - first) - 4;
}

static void slotTableUpdate(int hires) {
    void (**base)() = hires ? DMAHires : DMALores;
    const int first = hires ? 0x34 : 0x38;
    const int start = chipset.ddfstrt & (hires ? ~3 : ~7);
    if (!lineBitplaneWindow || start >= first) {
        slotTable = base;
        return;
    }
    const int key = hires | start << 1;
    if (key != earlyTableKey) {
        static void (*const lores[8])() = {
            evenCycle, plane4, plane6, plane2,
            evenCycle, plane3, plane5, loresPlane1,
        };
        static void (*const hiresPattern[4])() = {
            plane4, plane2, plane3, hiresPlane1,
        };
        for (int h = 0; h <= SLOT_LAST; ++h)
            earlyTable[h] = base[h];
        for (int h = start < 0 ? 0 : start; h < first; ++h)
            earlyTable[h] = hires ? hiresPattern[h & 3] : lores[h & 7];
        earlyTableKey = key;
    }
    slotTable = earlyTable;
}
// Last slot of the current line: PAL lines are 227 colour clocks (hPos
// 0..0xE2); NTSC lines alternate 228 and 227. A 228-slot PAL line made the
// CIA E clock count 71,364 ticks per 5 frames instead of 71,051, which the
// Amiga Test Kit reads as an NTSC machine.
static int lineLast = SLOT_LAST - 1;   // dmaVideoStandardInit() for NTSC
static uint8_t slotNext[SLOT_LAST + 1];
// Inside the bitplane fetch window, slotRunEnd[h] is the first slot >= h
// that is not a bitplane or free slot of the window (h itself elsewhere).
// While the Copper and the blitter stay idle, such a run only fetches planes
// and hands finished blocks to the host, so dma_run() calls the run's active
// slot functions back to back and does the per-slot bookkeeping (E clock,
// beam, skip checks) once for the run.
static uint8_t slotRunEnd[SLOT_LAST + 1];
// In a fetch run: 1 for a plane-1 slot (it also hands the block to the
// host), 2..6 for the slot of an enabled plane, which dma_run() fetches
// inline; 0 for anything else (called through the slot table).
static uint8_t slotPlane[SLOT_LAST + 1];
#define SLOT_COPPER 7
#define SLOT_BLITTER 8
// frank-amiga: while the Copper executes (so every even slot may be its)
// and the blitter is idle, an odd slot that is not active only offers
// itself to the blitter and does nothing. slotCopperNext[h] is the first
// slot >= h that is even or active; dma_run() jumps over the others
// (copper-heavy lines: State of the Art's chunky part, colour per slot).
static uint8_t slotCopperNext[SLOT_LAST + 1];
static uint32_t slotNextKey = ~0u;

static int slotIsActive(void (*f)(), int h) {
    // The last slot of a short line too, so one table serves both lengths
    // (rebuilding it whenever NTSC's line length changed cost a third of
    // the speed).
    if (h >= SLOT_LAST - 1 || h == bitplaneLine.lastCycle)
        return 1;
    // Audio slots do nothing per slot (Audio.c advances once per line); with
    // audio DMA on they keep the slot from the blitter, which never runs
    // while slots are skipped.
    if (f == evenCycle || f == oddCycle || f == dramCycle ||
        f == audio0Cycle || f == audio1Cycle || f == audio2Cycle ||
        f == audio3Cycle)
        return 0;
    if (f == spriteCycle)
        return (chipset.dmaconr & 0x220) == 0x220 &&
               internal.vPos >= OMEGA_SPRITE_FIRST_LINE;
    if (f == loresPlane1 || f == hiresPlane1)
        return lineBitplaneWindow && h >= fetchStart() &&
               h <= bitplaneLine.lastCycle;
    // A disabled plane's slot only clears its data latch (done once per line
    // in dmaUpdateLineState()) and offers the slot to the blitter.
    int plane = f == plane2 ? 2 : f == plane3 ? 3 : f == plane4 ? 4 :
                f == plane5 ? 5 : f == plane6 ? 6 : 0;
    if (plane)
        return lineBitplaneWindow && h >= fetchStart() &&
               h <= bitplaneLine.lastCycle &&
               (internal.bitplaneMask >> (plane - 1)) & 1;
    return 1;   // anything else always runs
}

// A slot a fetch run may include: inside the window, and a bitplane slot or
// one that only offers itself to the Copper and the blitter.
static int slotInFetchRun(void (*f)(), int h) {
    if (h >= SLOT_LAST - 1 || !lineBitplaneWindow || h < fetchStart() ||
        h > bitplaneLine.lastCycle)
        return 0;
    return f == hiresPlane1 || f == loresPlane1 || f == plane2 ||
           f == plane3 || f == plane4 || f == plane5 || f == plane6 ||
           f == evenCycle || f == oddCycle || f == dramCycle;
}

static void slotNextUpdate(void) {
    const int hires = (chipset.bplcon0 & 0x8000) != 0;
    const int sprites = (chipset.dmaconr & 0x220) == 0x220 &&
                        internal.vPos >= OMEGA_SPRITE_FIRST_LINE;
    const uint32_t key = (uint32_t)hires | (uint32_t)lineBitplaneWindow << 1 |
                         (uint32_t)sprites << 2 |
                         (uint32_t)(chipset.ddfstrt & 0xff) << 7 |
                         (uint32_t)(bitplaneLine.lastCycle & 0x1ff) << 15 |
                         (uint32_t)(internal.bitplaneMask & 0x3f) << 24;
    if (key == slotNextKey)
        return;
    slotNextKey = key;
    void (**table)() = slotTable;
    int next = SLOT_LAST;
    int copperNext = SLOT_LAST;
    int runEnd = SLOT_LAST;
    for (int h = SLOT_LAST; h >= 0; --h) {
        void (*f)() = table[h];
        const int active = slotIsActive(f, h);
        if (active)
            next = h;
        slotNext[h] = (uint8_t)next;
        if (active || !(h & 1))
            copperNext = h;
        slotCopperNext[h] = (uint8_t)copperNext;
        if (!slotInFetchRun(f, h))
            runEnd = h;
        slotRunEnd[h] = (uint8_t)runEnd;
        int plane = 0;
        if (slotInFetchRun(f, h)) {
            plane = f == hiresPlane1 || f == loresPlane1 ? 1 :
                    f == plane2 ? 2 : f == plane3 ? 3 : f == plane4 ? 4 :
                    f == plane5 ? 5 : f == plane6 ? 6 : 0;
            if (plane > 1 && !((internal.bitplaneMask >> (plane - 1)) & 1))
                plane = 0;  // disabled: its slot is inactive anyway
        }
        // frank-amiga: 7/8 for a slot that only offers itself to the
        // Copper / the blitter (dmaCopperRun()).
        if (!plane)
            plane = f == evenCycle ? SLOT_COPPER : f == oddCycle ? SLOT_BLITTER : 0;
        // A plane 2-6 slot that fetches nothing (plane disabled, or outside
        // the window) is free (planeN() -> freeCycle(); its data latch is
        // cleared once per line): the Copper's if even, the blitter's if
        // odd; plane2() gives a disabled plane's slot in the window to the
        // blitter only. With 3 planes, the Copper's free slots in the window
        // are plane 4-6 slots (State of the Art's chunky part: 12k MOVE
        // words a frame went through planeN() and evenCycle()).
        if (!plane && h < SLOT_LAST - 1 && h != bitplaneLine.lastCycle &&
            (f == plane2 || f == plane3 || f == plane4 || f == plane5 || f == plane6) &&
            !slotIsActive(f, h)) {
            const int inWindow = lineBitplaneWindow && h >= fetchStart() &&
                                 h <= bitplaneLine.lastCycle;
            plane = (f == plane2 && inWindow) || (h & 1) ? SLOT_BLITTER : SLOT_COPPER;
        }
        slotPlane[h] = (uint8_t)plane;
    }
}

static inline int dmaIdleUntil(int limit);

static void dmaUpdateLineState(void) {
    uint8_t enabledPlanes = enabledBitplaneMask();
    int dmaEnabled = (chipset.dmaconr & 0x300) == 0x300;
    int displayWindowActive = displayWindowContainsLine(internal.vPos);
    bitplaneLine.enabledMask |= enabledPlanes;
    bitplaneLine.inDisplayWindow |= (uint8_t)displayWindowActive;
    if (dmaEnabled && displayWindowActive)
        bitplaneLine.fetchEligibleMask |= enabledPlanes;
    lineBitplaneWindow = dmaEnabled && displayWindowActive;
    lineFullWidth = omegaDdfIsFullWidth(chipset.ddfstrt);
    lineHiresDisplayTop = (chipset.diwstrt >> 8) +
                          omegaDdfUpperOverscan(chipset.ddfstop);
    lineRowRotation = omegaDdfRowRotation(chipset.ddfstrt, chipset.ddfstop);

    if(chipset.bplcon0 & 0x8000){
        // The 0x3c full-width window consumes one more word than the narrower
        // 0x40 fetch window. The latter's right-edge word is supplied by
        // hiresDisplayPrefetch() without changing its stride.
        bitplaneLine.lastCycle = chipset.ddfstop +
            omegaDdfHiresFetchTail(chipset.ddfstrt, chipset.ddfstop);
        // frank-amiga: a DDFSTOP in the middle of an 8-slot fetch unit
        // fetches through that unit and one more (the 0x3c/0xd0 case
        // above, 40 words). Monkey Island's 0x38/0xcc screen got 39 words a
        // line from the fixed tail, so its 80-byte rows sheared 16 pixels a
        // line.
        {
            const int span = (int)chipset.ddfstop - (int)chipset.ddfstrt;
            if (span > 0 && (span & 7) == 4) {
                const int last = chipset.ddfstrt + 8 * (span / 8 + 2) - 1;
                bitplaneLine.lastCycle = last > 0xDF ? 0xDF : last;
            }
        }
    }else{
        // LORES: one word per plane every 8 slots (plane 1 last, at +7),
        // in groups from DDFSTRT (the tables' phase) through the group
        // DDFSTOP falls in: 0x38-0xD0 is 20 words, the Test Kit's overscan
        // 0x20-0xD8 is 24. (A fixed 20-word span gave narrow and wide
        // windows the wrong line stride.)
        const int start = chipset.ddfstrt & ~7;
        int words = ((int)chipset.ddfstop - start) / 8 + 1;
        if (words < 1)
            words = 1;
        int last = start + 8 * words - 1;
        if (last > 0xDF)
            last = 0xDF;   // the last fetch group of the tables
        bitplaneLine.lastCycle = last;
    }
    {
        const int hires = (chipset.bplcon0 & 0x8000) != 0;
        slotTableUpdate(hires);
        // Full-width LORES rows are placed by beam position (raster
        // column 0 at lores pixel 0x81, where HIRES 0x3C and LORES 0x38
        // start), so sprites keep their place across rows of either
        // resolution: Workbench's hires screen ends in LORES 0x3C lines,
        // where the pointer was drawn 16 columns left.
        // An early HIRES fetch (DDFSTRT < 0x38; 0x38 itself is the
        // rotated layout) is placed the same way, against HIRES 0x3C.
        // So is a narrow HDMI direct one (DDFSTRT >= 0x40; Kickstart 2.04
        // and 3.1.4's insert-disk screens): its frame maps raster column
        // x to image column x - HOST_FETCH_LEAD, so column 0 is that many
        // columns (4 a slot) before 0x3C. Against DDFSTRT, both pictures
        // and their sprites sat 40 columns left. A narrow LORES one the
        // same way, before 0x38: anchored at its fetch, Arte's centred
        // texture box (DDFSTRT $50) sat at the left edge, its left 20
        // pixels cut off.
        // frank-amiga: so is a HIRES fetch from 0x38 outside the rotated
        // layout (DDFSTOP 0xD8): Arte's "ARTE (...)" line, HIRES at
        // DDFSTRT $38 under a LORES $38 picture, sat 8 pixels right of it.
        lineRasterDdf = !hires && omegaDdfIsFullWidth(chipset.ddfstrt) ? 0x38
                      : hires && chipset.ddfstrt < 0x3C && !lineRowRotation ? 0x3C
                      : hostDirectActive && !omegaDdfIsFullWidth(chipset.ddfstrt)
                      ? (hires ? 0x3C : 0x38) - HOST_FETCH_LEAD / 4
                      : fetchStart();
        // frank-amiga: core 1 places colour writes by beam position with
        // this (dmaBeamColumn()).
        hostLineLayout(2 * lineRasterDdf + (hires ? OMEGA_SPRITE_HIRES_OFFSET
                                                  : OMEGA_SPRITE_LORES_OFFSET),
                       (chipset.bplcon0 & 0x800) != 0);
    }
    // Disabled planes' slots may be skipped (see slotIsActive()); clear their
    // data latches here, as those slots would.
    if (lineBitplaneWindow) {
        const unsigned mask = internal.bitplaneMask;
        if (!(mask & 0x02)) chipset.bpl2dat = 0;
        if (!(mask & 0x04)) chipset.bpl3dat = 0;
        if (!(mask & 0x08)) chipset.bpl4dat = 0;
        if (!(mask & 0x10)) chipset.bpl5dat = 0;
        if (!(mask & 0x20)) chipset.bpl6dat = 0;
    }
    slotNextUpdate();
    dmaLineStateDirty = 0;
}

static void spriteFrameStart(void);
static void spriteRenderLine(void);
// Raster row of this line's bitplane blocks, or -1 (no block this line).
static int spriteLineRow = -1;

// The rotated layout shows a row's last OMEGA_DDF_ROTATED_PIXELS columns
// from the next line's first words. When that line fetches nothing (Kickstart
// 3.1.4's Copper turns the planes off on the line after its screen), those
// words are read here without moving the pointers.
static int tailRow = -1;          // last line's raster row, if rotated
static unsigned tailPlanes;       // and its bitplane mask
static int tailScroll;            // and its first column

static void rotatedTailEndOfLine(void) {
    const int row = tailRow;
    tailRow = -1;
    if (lineRowRotation && bitplaneLine.hiresWords && internal.bitplaneMask) {
        tailRow = host.rasterRow;
        tailPlanes = internal.bitplaneMask;
        tailScroll = bplScrollColumns();
        return;
    }
    // This line drew no rotated HIRES words (its planes are off: it still
    // counts LORES slots), so the last row's right edge is drawn here.
    if (row < 0 || row >= HOST_RASTER_H || host.pixels == NULL)
        return;
    const uint32_t pt[4] = { chipset.bpl1pt, chipset.bpl2pt,
                             chipset.bpl3pt, chipset.bpl4pt };
    for (int i = 0, x = tailScroll; x < OMEGA_DDF_ROTATED_PIXELS;
         ++i, x += 16) {
        uint16_t p[4] = { 0, 0, 0, 0 };
        for (int n = 0; n < 4; ++n)
            if (tailPlanes & (1u << n))
                p[n] = CHIPW(pt[n] + (uint32_t)i);
        hiresPeekedWord(row, x, p[0], p[1], p[2], p[3]);
    }
}

#if OMEGA_DYNAMIC_FIRST_LINE
// frank-amiga: the HDMI output shows image rows DIRECT_SHOWN_FIRST ..
// +DIRECT_SHOWN_ROWS-1 (PAL 640x480: 240 of the 256). Each frame notes the
// beam lines that fetched bitplanes; when they leave that window the image's
// first line moves just enough to bring them back (the top wins when they
// do not fit), once the same move was wanted for a few frames in a row.
// frank-amiga: or all 256 (dmaVideoStandardInit(), a 640x512 display).
static int directShownFirst = 8, directShownRows = 240;
#define DIRECT_SHOWN_FIRST directShownFirst
#define DIRECT_SHOWN_ROWS directShownRows
#if OMEGA_VIDEO_RUNTIME
// NTSC: lines 23..262 are the shown rows; nothing comes later to follow.
#define DIRECT_FIRST_LINE_STD (omegaVideoNtsc ? 15 : 44)
#else
#define DIRECT_FIRST_LINE_STD 44
#endif
#if OMEGA_VIDEO_RUNTIME
#define DIRECT_FIRST_LINE_MIN (omegaVideoNtsc ? 15 : 20)
#else
#define DIRECT_FIRST_LINE_MIN 20
#endif
#define DIRECT_SETTLE_FRAMES 5
int omegaDirectFirstLine = 44;   // PAL's; dmaVideoStandardInit()
static int contentTop = 0x7fff, contentBottom = -1;
static int wantFirst = 44, wantFrames;

static void directWindowFrame(void) {
    const int top = contentTop, bottom = contentBottom;
    contentTop = 0x7fff;
    contentBottom = -1;
    if (bottom < 0)
        return;                         // no bitplanes: keep the window
    int first = DIRECT_FIRST_LINE_STD;
    if (bottom > first + DIRECT_SHOWN_FIRST + DIRECT_SHOWN_ROWS - 1)
        first = bottom - (DIRECT_SHOWN_FIRST + DIRECT_SHOWN_ROWS - 1);
    if (top < first + DIRECT_SHOWN_FIRST)
        first = top - DIRECT_SHOWN_FIRST;
    if (first < DIRECT_FIRST_LINE_MIN)
        first = DIRECT_FIRST_LINE_MIN;
    if (first == omegaDirectFirstLine) {
        wantFrames = 0;
        return;
    }
    if (first != wantFirst) {
        wantFirst = first;
        wantFrames = 0;
    }
    if (++wantFrames >= DIRECT_SETTLE_FRAMES) {
        omegaDirectFirstLine = first;
        wantFrames = 0;
    }
}
#endif

// End of a line (rare): kept out of the per-slot path so dma_run() stays
// small and saves few registers.
static void __attribute__((noinline)) dmaEndOfLine(void) {
    // Denise still presents the next HIRES word at the right edge even
    // though Agnus does not consume it as part of the line stride. Peek
    // at it for display only; do not advance any bitplane pointer.
    hiresDisplayPrefetch();
    rotatedTailEndOfLine();

    audioLine(lineLast + 1);
    floppyLineTick();
#if OMEGA_DYNAMIC_FIRST_LINE
    if (bitplaneLine.loresWords | bitplaneLine.hiresWords) {
        if (internal.vPos < contentTop) contentTop = internal.vPos;
        contentBottom = internal.vPos;
    }
#endif
    if (clxActive)
        clxLineEnd();
    if (hostDirectActive) {
        // The line's messages go out (or are replaced if its image row is
        // unchanged); a row without blocks gets its border colour.
        const int row = internal.vPos - OMEGA_DIRECT_FIRST_LINE;
        hostDirectRowEnd(row >= 0 && row < OMEGA_DIRECT_ROWS ? row : -1,
                         (bitplaneLine.loresWords |
                          bitplaneLine.hiresWords) != 0);
    }
    dmaIdleCacheValid = 0;   // the Copper's comparisons depend on the line
    internal.hPos = 0;
    if (OMEGA_VIDEO_LONG_LINES)
        lineLast ^= SLOT_LAST ^ (SLOT_LAST - 1);   // long and short alternate
    internal.vPos +=1;
    CIATODEvent(&CIAB);

    advanceBitplanePointers();
    resetBitplaneLine();
    dmaLineStateDirty = 1;  // new line: masks cleared, vPos changed

    //VBL Time
    // Interlace (BPLCON0 LACE): fields alternate between long frames (LOF
    // set: 313 lines PAL, 263 NTSC) and short ones (one line fewer), and
    // programs read LOF from VPOSR to pick the field's bitplanes. Without
    // LACE every frame is long.
    if(internal.vPos >= OMEGA_VIDEO_FRAME_LINES - (internal.LOF ? 0 : 1)){
        internal.vPos = 0;
        internal.LOF = (chipset.bplcon0 & 0x4) ? !internal.LOF : 1;
        copperWaitPosition = 0;
        spriteFrameStart();
        clxFrame();
#if OMEGA_DYNAMIC_FIRST_LINE
        directWindowFrame();
#endif

        //Reset Copper.
        putChipReg16[COPJMP1](0);
        CIATODEvent(&CIAA);

        //need to generate a vbl int
        putChipReg16[INTREQ](0x8020);

        host.rasterRow = 0;
        host.rasterX = 0;

        hostDisplay(); //Call the host to update the display.
    }
    diwVFlopUpdate(1);
}

// Fetch-window end (rare, once per line).
static void __attribute__((noinline)) dmaFetchWindowComplete(void) {
    spriteRenderLine();
    spriteLineRow = -1;
    bitplaneLine.fetchWindowComplete = 1;
    bitplaneLine.pointerAtFetchCompletion[0] = chipset.bpl1pt;
    bitplaneLine.pointerAtFetchCompletion[1] = chipset.bpl2pt;
    bitplaneLine.pointerAtFetchCompletion[2] = chipset.bpl3pt;
    bitplaneLine.pointerAtFetchCompletion[3] = chipset.bpl4pt;
    if (bitplaneLine.fetchedMask != 0) {
        lastFetchedMask = bitplaneLine.fetchedMask;
        applyBitplaneModulo(bitplaneLine.fetchedMask);
    }
}

// Runs `slots` DMA slots (one colour clock each). Batching avoids a call and
// register save per slot from the main loop.
static inline void hiresPlane1Fetch(void);
// frank-amiga: the horizontal display window (HDMI direct frames). Pixels
// outside DIWSTRT/DIWSTOP show the border colour. The line's window goes to
// core 1 in raster columns before its first block; core 1 applies it where
// it writes the row, after each plane set is placed at its own scroll
// (dual playfield, BPLCON1 PF1H != PF2H). Raster column c is at LORES beam
// position first + c / 2, as for sprites. Arte's texture and end planet
// keep columns of unrelated data outside DIWSTRT $C0 - DIWSTOP $80.
static FRANK_COLD void diwLineWindow(int row, int hires) {
    if (lineRowRotation)
        return;   // rotated rows wrap; left as they were
    const int first = 2 * lineRasterDdf +
                      (hires ? OMEGA_SPRITE_HIRES_OFFSET
                             : OMEGA_SPRITE_LORES_OFFSET);
    const int x0 = 2 * ((chipset.diwstrt & 0xff) - first);
    const int x1 = 2 * (((chipset.diwstop & 0xff) | 0x100) - first);
    // Image column 0 is raster column HOST_FETCH_LEAD on a narrow layout.
    const int lead = omegaDdfIsFullWidth(chipset.ddfstrt) ? 0 : HOST_FETCH_LEAD;
    if (x0 <= lead && x1 >= lead + HOST_RASTER_W)
        return;   // the window shows the whole image
    hostDirectClip(row, x0, x1);
}

static inline void loresPlane1Fetch(void);
// planeN() for an enabled plane inside the fetch window.
#define FETCH_PLANE(n) do { \
        bitplaneLine.fetchedMask |= 1u << ((n) - 1); \
        chipset.bpl##n##dat = CHIPW(chipset.bpl##n##pt); \
        chipset.bpl##n##pt += 1; \
    } while (0)

// Advances the E clock by k slots: it ticks every fifth slot.
// frank-amiga: slots run since reset (blit end deadlines, see blitTime).
uint32_t dmaSlotClock;

static inline void dmaEClock(int k) {
    dmaSlotClock += (uint32_t)k;
    const int counter = internal.eClockCounter - k;
    if (counter < 0) {
        const int ticks = (4 - counter) / 5;
        internal.eClockCounter = counter + 5 * ticks;
        CIAClock(ticks);
    } else {
        internal.eClockCounter = counter;
    }
}

// VHPOSR is only read by the CPU, between calls, and by the Copper's SKIP
// (which uses the beam directly), so it is stored once on return: the
// position of the last slot run, as if it were written at every slot.
#ifdef DMA_SLOT_STATS
uint32_t dma_stat_slow, dma_stat_batched, dma_stat_skipped, dma_stat_cop, dma_stat_blit;
uint32_t dma_cyc_slots, dma_cyc_copper, dma_cyc_colour, dma_cyc_blit, dma_cyc_cpu, dma_cyc_dma, dma_cyc_wait, dma_cyc_eol, dma_cyc_p1;   // DWT cycles (PICO)
#ifdef PICO_BUILD
#define DWT_CYC (*(volatile uint32_t *)0xE0001004u)
#else
#define DWT_CYC 0u
#endif
#define DSTAT(x, n) (x += (n))
#else
#define DSTAT(x, n) ((void)0)
#endif
static int blitterState;   // defined with the blitter code below
// The slot after x in a Copper run (< end): with the blitter idle, odd
// slots nothing could use are jumped over (see slotCopperNext[]).
static inline int dmaCopperStep(int x, int end) {
    if (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240)
        return x + 1;
    const int next = slotCopperNext[x + 1];
    return next < end ? next : end;
}
#ifndef OMEGA_NO_COPPER_FAST
static int copperSkipPending, copperSkip, copperJumpList;   // see evenCycle()

// frank-amiga: the Copper's fast path. While it executes (not waiting, not
// frozen), dma_run() runs the rest of the line here, slot by slot as the
// slow path would but without its per-slot bookkeeping (E clock, beam once
// for the run): plane fetches inline, the odd slots only the blitter could
// use skipped while it is idle (slotCopperNext[]), and the Copper's own
// slots taking its two common steps inline (a first word; the second word
// of a MOVE to a register it may write). Anything else (WAIT, SKIP,
// COPJMP, a skip pending, the refused slot) goes through evenCycle(). A
// register write that changes the line's layout, or the Copper stopping,
// ends the run. Returns 0 to leave the slot to the slow path.
static int dmaCopperRun(int *slots, int *beamV, int *beamH) {
    const int h = internal.hPos;
    int end = h + *slots + 1;
    if (end > lineLast)
        end = lineLast;                 // the line end runs in the slow path
    if (end <= h + 1)
        return 0;
    void (**table)() = slotTable;
    const int hires = (chipset.bplcon0 & 0x8000) != 0;
    const int refused = lineLast > 0xE2 ? 0xE1 : 0xE0;
    const uint32_t clock0 = dmaSlotClock;
    int x = h;
    while (x < end) {
        dmaSlotClock = clock0 + (uint32_t)(x - h);   // blit deadlines
        internal.hPos = x;
        switch (slotPlane[x]) {
        case 1:
            if (hires)
                hiresPlane1Fetch();
            else
                loresPlane1Fetch();
            break;
        case 2: FETCH_PLANE(2); break;
        case 3: FETCH_PLANE(3); break;
        case 4: FETCH_PLANE(4); break;
        case 5: FETCH_PLANE(5); break;
        case 6: FETCH_PLANE(6); break;
        case SLOT_COPPER: {
            const int cycle = internal.copperCycle;
            if (x == refused || (chipset.dmaconr & 0x280) != 0x280) {
                evenCycle();
            } else if (cycle == 0 && !copperSkipPending) {
                // First word (copperExecute() state 0).
                uint16_t w = CHIPW(internal.copperPC);
                w = (uint16_t)((w << 8) | (w >> 8));
                internal.IR1 = w;
                internal.copperPC += 1;
                internal.copperCycle = (w & 1) ? 2 : 1;
            } else if (cycle == 1 && !copperSkip &&
                       ((internal.IR1 >> 1) & 255) >= 0x40 &&
                       ((internal.IR1 >> 1) & 255) != 0x44 &&
                       ((internal.IR1 >> 1) & 255) != 0x45) {
                // A MOVE's second word and its write (state 1).
                const unsigned reg = (internal.IR1 >> 1) & 255;
                uint16_t v = CHIPW(internal.copperPC);
                internal.copperPC += 1;
                v = (uint16_t)((v << 8) | (v >> 8));
                internal.IR1 = (uint16_t)reg;
                internal.IR2 = v;
                debugChipAddress = (int)reg;
                debugChipValue = v;
                internal.copperCycle = 0;
                if (reg - 0xC0u < 32u)
                    setColour(reg - 0xC0u, v);
                else
                    putChipReg16[reg](v);
            } else {
                evenCycle();
            }
            break;
        }
        case SLOT_BLITTER:
            if (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240)
                oddCycle();
            break;
        default:
            table[x]();
            break;
        }
        if (x == bitplaneLine.lastCycle)
            dmaFetchWindowComplete();
        x = dmaCopperStep(x, end);
        if (dmaLineStateDirty || internal.copperCycle == 3 ||
            internal.copperCycle == 4 || (chipset.dmaconr & 0x280) != 0x280)
            break;
    }
    dmaSlotClock = clock0;
    const int k = x - h;
    DSTAT(dma_stat_batched, (uint32_t)k);
    internal.hPos = x;
    *slots -= k - 1;
    *beamV = internal.vPos;
    *beamH = x - 1;
    dmaEClock(k);
    return 1;
}
#endif

void dma_run(int slots){
    int beamV = -1, beamH = 0;  // last slot run
    while (slots-- > 0) {
        // VPOSR only changes with the line.
        if (internal.hPos == 0)
            chipset.vposr = (uint16_t)(internal.LOF << 15) |
                            OMEGA_VIDEO_VPOSR_ID | (internal.vPos >> 8);
        if (dmaLineStateDirty)
            dmaUpdateLineState();

        // Jump over slots that would do nothing (see slotNext[]) and run
        // fetch runs back to back (see slotRunEnd[]).
        // -DOMEGA_NO_SLOT_SKIP disables both (A/B comparisons).
#ifndef OMEGA_NO_SLOT_SKIP
        // frank-amiga: an executing Copper (not waiting, not frozen) makes
        // dmaIdleUntil() answer "here" for every slot; skip the lookups.
        if ((chipset.dmaconr & 0x280) != 0x280 || internal.copperCycle == 4 ||
            internal.copperCycle == 3)
        {
            const int h = internal.hPos;
            int end = slotRunEnd[h];
            if (end > h + 1) {
                if (end > h + slots + 1)
                    end = h + slots + 1;
                end = dmaIdleUntil(end);
                const int k = end - h;
                if (k > 1) {
                    void (**table)() = slotTable;
                    // Inactive slots do nothing while the Copper and the
                    // blitter are idle; run only the others. Inside the
                    // window bitplaneActive() holds, so plane 2-6 slots are
                    // fetched inline and plane-1 slots skip that test (the
                    // Copper/blitter tails of the slot functions are no-ops
                    // here).
                    const int hires = (chipset.bplcon0 & 0x8000) != 0;
                    for (int x = slotNext[h]; x < end; x = slotNext[x + 1]) {
                        switch (slotPlane[x]) {
                        case 1:
                            internal.hPos = x;
                            if (hires)
                                hiresPlane1Fetch();
                            else
                                loresPlane1Fetch();
                            break;
                        case 2: FETCH_PLANE(2); break;
                        case 3: FETCH_PLANE(3); break;
                        case 4: FETCH_PLANE(4); break;
                        case 5: FETCH_PLANE(5); break;
                        case 6: FETCH_PLANE(6); break;
                        default:
                            internal.hPos = x;
                            table[x]();
                            break;
                        }
                    }
                    if (end - 1 == bitplaneLine.lastCycle)
                        dmaFetchWindowComplete();
                    DSTAT(dma_stat_batched, (uint32_t)k);
                    internal.hPos = end;
                    slots -= k - 1;
                    beamV = internal.vPos;
                    beamH = end - 1;
                    dmaEClock(k);
                    continue;
                }
            }
            int next = slotNext[h];
            if (next > h) {
                if (next > h + slots + 1)
                    next = h + slots + 1;
                next = dmaIdleUntil(next);
                const int k = next - h;
                if (k > 0) {
                    DSTAT(dma_stat_skipped, (uint32_t)k);
                    internal.hPos = next;
                    slots -= k - 1;
                    beamV = internal.vPos;
                    beamH = next - 1;
                    dmaEClock(k);
                    continue;
                }
            }
        }
#ifndef OMEGA_NO_COPPER_FAST
        else if (dmaCopperRun(&slots, &beamV, &beamH)) {
            continue;
        }
#else
        else if (slotRunEnd[internal.hPos] > internal.hPos + 1) {
            // frank-amiga: the Copper executes inside the fetch window. Run
            // the window's slots back to back as above, but every one of
            // them (the Copper takes the even free slots, the blitter the
            // odd ones), the plane fetches inline. A register write that
            // changes the line's layout ends the run.
            const int h = internal.hPos;
            int end = slotRunEnd[h];
            if (end > h + slots + 1)
                end = h + slots + 1;
            void (**table)() = slotTable;
            const int hires = (chipset.bplcon0 & 0x8000) != 0;
            const uint32_t clock0 = dmaSlotClock;
            int x = h;
            while (x < end) {
                dmaSlotClock = clock0 + (uint32_t)(x - h);   // blit deadlines
                switch (slotPlane[x]) {
                case 1: {
                    internal.hPos = x;
#ifdef DMA_SLOT_STATS
                    const uint32_t c0 = DWT_CYC;
#endif
                    if (hires)
                        hiresPlane1Fetch();
                    else
                        loresPlane1Fetch();
#ifdef DMA_SLOT_STATS
                    dma_cyc_p1 += DWT_CYC - c0;
#endif
                    break;
                }
                case 2: FETCH_PLANE(2); break;
                case 3: FETCH_PLANE(3); break;
                case 4: FETCH_PLANE(4); break;
                case 5: FETCH_PLANE(5); break;
                case 6: FETCH_PLANE(6); break;
                default: {
                    internal.hPos = x;
                    void (*const fn)() = table[x];
                    if (fn == oddCycle) {
                        // Only the blitter could use it.
                        if (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240)
                            oddCycle();
                    } else if (fn == evenCycle && internal.copperCycle != 3 &&
                               internal.copperCycle != 4 &&
                               x != (lineLast > 0xE2 ? 0xE1 : 0xE0)) {
                        // The executing Copper's slot (evenCycle(), with the
                        // checks this branch already made).
                        if (copperExecute() != 1)
                            oddCycle();
                    } else {
                        fn();
                    }
                    break;
                }
                }
                x = dmaCopperStep(x, end);
                if (dmaLineStateDirty)
                    break;
            }
            dmaSlotClock = clock0;
            const int k = x - h;
            if (x - 1 == bitplaneLine.lastCycle)
                dmaFetchWindowComplete();
            DSTAT(dma_stat_batched, (uint32_t)k);
            internal.hPos = x;
            slots -= k - 1;
            beamV = internal.vPos;
            beamH = x - 1;
            dmaEClock(k);
            continue;
        } else if (blitterState == 0 && (chipset.dmaconr & 0x4240) != 0x4240) {
            // The Copper executes and the blitter is idle: jump over the
            // odd slots only the blitter could use (see slotCopperNext[]).
            const int h = internal.hPos;
            int next = slotCopperNext[h];
            if (next > h) {
                if (next > h + slots + 1)
                    next = h + slots + 1;
                const int k = next - h;
                DSTAT(dma_stat_skipped, (uint32_t)k);
                internal.hPos = next;
                slots -= k - 1;
                beamV = internal.vPos;
                beamH = next - 1;
                dmaEClock(k);
                continue;
            }
        }
        if ((chipset.dmaconr & 0x280) == 0x280 && internal.copperCycle != 4 &&
            internal.copperCycle != 3 && slotRunEnd[internal.hPos] <= internal.hPos + 1) {
            // frank-amiga: the Copper executes outside a fetch run (the
            // blank, border lines): run the slots back to back up to the
            // line's last slot, each through its slot function as below,
            // the Copper's own and the idle blitter's ones inline.
            const int h = internal.hPos;
            int end = h + slots + 1;
            if (end > lineLast)
                end = lineLast;             // the line end runs below
            if (end > h + 1) {
                void (**table)() = slotTable;
                const uint32_t clock0 = dmaSlotClock;
                const int copperRefused = lineLast > 0xE2 ? 0xE1 : 0xE0;
                int x = h;
                while (x < end) {
                    dmaSlotClock = clock0 + (uint32_t)(x - h);
                    internal.hPos = x;
                    void (*const fn)() = table[x];
                    if (fn == oddCycle) {
                        if (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240)
                            oddCycle();
                    } else if (fn == evenCycle && internal.copperCycle != 3 &&
                               internal.copperCycle != 4 && x != copperRefused) {
                        if (copperExecute() != 1)
                            oddCycle();
                    } else {
                        fn();
                    }
                    if (x == bitplaneLine.lastCycle)
                        dmaFetchWindowComplete();
                    x = dmaCopperStep(x, end);
                    if (dmaLineStateDirty || slotRunEnd[x] > x + 1)
                        break;
                }
                dmaSlotClock = clock0;
                const int k = x - h;
                DSTAT(dma_stat_batched, (uint32_t)k);
                internal.hPos = x;
                slots -= k - 1;
                beamV = internal.vPos;
                beamH = x - 1;
                dmaEClock(k);
                continue;
            }
        }
#endif
#endif

        // SDL_AtomicSet(&cpuWait, 1); // single-threaded on RP2350
        DSTAT(dma_stat_slow, 1u);
#ifdef DMA_SLOT_STATS
        if ((chipset.dmaconr & 0x280) == 0x280 && internal.copperCycle != 4 && internal.copperCycle != 3)
            dma_stat_cop++;
        if (blitterState != 0) dma_stat_blit++;
        const uint32_t c0 = DWT_CYC;
#endif
        slotTable[internal.hPos]();
#ifdef DMA_SLOT_STATS
        dma_cyc_slots += DWT_CYC - c0;
#endif

        if (internal.hPos == bitplaneLine.lastCycle)
            dmaFetchWindowComplete();

        // CIA timers: the E clock ticks every fifth slot (eclock_execute(),
        // inlined; see CIAClock()).
        dmaSlotClock++;
        if (--internal.eClockCounter < 0) {
            internal.eClockCounter = 4;
            CIAClock(1);
        }

        beamV = internal.vPos;
        beamH = internal.hPos;
        // End of line (227 or 228 colour clocks).
        if (++internal.hPos > lineLast) {
#ifdef DMA_SLOT_STATS
            const uint32_t c0 = DWT_CYC;
            dmaEndOfLine();
            dma_cyc_eol += DWT_CYC - c0;
#else
            dmaEndOfLine();
#endif
        }
    }
    if (beamV >= 0)
        chipset.vhposr = (uint16_t)(beamV << 8 | beamH);
}

void dma_execute(){
    dma_run(1);
}

// frank-amiga: catch-up scheduling. A slice lets the CPU run first, for as
// long as nothing it could notice happens in the chipset: up to the Copper's
// or the blitter's next action (dmaIdleUntil()), at most 64 slots and never
// past the end of the line; at least 8 slots, as before, when they are
// busy. Every CPU access to a custom or CIA register first runs the
// chipset up to the CPU's cycle (dmaCatchUp()), so the CPU sees and moves
// the chipset at its own time, and the rest of the slice runs afterwards.
// Batching the slots this way saves the per-call cost of alternating
// dma_run() and m68k_execute() every 8 slots.
#ifndef OMEGA_CPU_CYCLES_PER_SLOT
#define OMEGA_CPU_CYCLES_PER_SLOT 2
#endif
int dmaInCpu, dmaInCatchUp;
static int dmaSliceSlots, dmaCaught;

void dmaCatchUp(void) {
    int due = m68k_cycles_run() / OMEGA_CPU_CYCLES_PER_SLOT;
    // The slice's last slot stays for afterwards: the end of the line (and
    // of the frame) never runs inside an instruction.
    if (due > dmaSliceSlots - 1) due = dmaSliceSlots - 1;
    due -= dmaCaught;
    if (due <= 0) return;
    dmaInCatchUp = 1;
    dma_run(due);
    dmaInCatchUp = 0;
    dmaCaught += due;
}

int dmaRunSlice(int *cpuBalance) {
    const int h = internal.hPos;
    const int toLineEnd = lineLast + 1 - h;
    int n = (toLineEnd < 64 ? toLineEnd : 64);
    if (dmaLineStateDirty)
        dmaUpdateLineState();
    n = dmaIdleUntil(h + n) - h;
    if (n < 8) n = toLineEnd < 8 ? toLineEnd : 8;
    dmaSliceSlots = n;
    dmaCaught = 0;
    *cpuBalance += OMEGA_CPU_CYCLES_PER_SLOT * n;
#ifdef DMA_SLOT_STATS
    const uint32_t c0 = DWT_CYC;
#endif
    if (*cpuBalance > 0) {
        dmaInCpu = 1;
        *cpuBalance -= m68k_execute(*cpuBalance);
        dmaInCpu = 0;
    }
#ifdef DMA_SLOT_STATS
    const uint32_t c1 = DWT_CYC;
    dma_cyc_cpu += c1 - c0;
#endif
    if (dmaCaught < n)
        dma_run(n - dmaCaught);
#ifdef DMA_SLOT_STATS
    dma_cyc_dma += DWT_CYC - c1;
#endif
    return n;
}


// Copper WAIT comparison (copperExecute() state 3).
// The Copper compares only the low 8 bits of the beam line (VP7-VP0), as on
// OCS: lines 256+ are reached by a wait near the end of line 255 followed by
// low-byte waits, which then compare against line - 256. On hardware the
// Copper wakes from that first wait late enough that the next WAIT is first
// compared on line 256; here it can be fetched still on line 255, so such a
// WAIT (fetched on line 255 for an earlier low-byte line) is held until the
// beam wraps.
static uint8_t copperWaitNextBank;

static inline uint32_t copperBeam(void) {
    return ((uint32_t)(internal.vPos & 0xff) << 8) | internal.hPos;
}

// frank-amiga: the comparison mask always includes VP7 (IR2 bit 15 is BFD,
// not a mask bit), and with BFD clear the blitter must be done too.
static inline uint32_t copperCompareMask(void) {
    return ((uint32_t)internal.IR2 | 0x8000u) & 0xFFFEu;
}
static inline int copperWaitReached(void) {
    if (copperWaitNextBank && (internal.vPos & 0xff) == 0xff)
        return 0;
    if (!(internal.IR2 & 0x8000u) && (chipset.dmaconr & 0x4000u))
        return 0;
    return (copperBeam() & copperCompareMask()) >= copperWaitPosition;
}

// frank-amiga: Copper timing after vAmiga's state machine (CopperEvents.cpp):
// a WAIT or SKIP takes two more slots after its second word (WAIT1/2,
// SKIP1/2); a SKIP compares at the next instruction's fetch, and a skipped
// MOVE is still fetched; a MOVE to COPJMPx jumps two slots later (JMP1/2).
// copperCycle 5/6 = WAIT1/2, 7/8 = SKIP1/2, 9/10 = JMP1/2.
static int copperSkipPending, copperSkip, copperJumpList;

void evenCycle(void){

    // An idle Copper (DMA off, frozen until VBL, or waiting for a beam
    // position not yet reached) does not take the slot; skip the call. On
    // the slot a WAIT completes copperExecute() still runs and returns 0.
    if ((chipset.dmaconr & 0x280) == 0x280 && internal.copperCycle != 4 &&
        (internal.copperCycle != 3 || copperWaitReached())) {
        // frank-amiga: the Copper is refused cycle $E0 ($E1 on a long
        // line), and when it asks for it nobody gets the slot (as vAmiga's
        // Agnus::busIsFree()). A MOVE run across the line end slips by a
        // slot there: State of the Art's per-line Copper loops reach their
        // exit SKIP one compare position later and leave the loop.
        if (internal.hPos == (lineLast > 0xE2 ? 0xE1 : 0xE0))
            return;
#ifdef DMA_SLOT_STATS
        const uint32_t c0 = DWT_CYC;
        const int took = copperExecute();
        dma_cyc_copper += DWT_CYC - c0;
        if (took == 1) return;
#else
        if(copperExecute()==1){
            return;
        }
#endif
    }
    
    //if the copper doesn't want the even cycle, give it to the odd cycle devices.
    oddCycle();

}

static int blitterState = 0;
// frank-amiga: state 3 (a computed blit waiting out its duration) ends when
// dmaSlotClock reaches this; meanwhile the blitter does nothing, so idle
// slots may be skipped up to it.
static uint32_t blitDeadline;

void oddCycle(void){
    
    // An idle Blitter (no blit started) does nothing; skip the call.
    if (blitterState == 0 && (chipset.dmaconr & 0x4240) != 0x4240)
        return;
#ifdef DMA_SLOT_STATS
    const uint32_t c0 = DWT_CYC;
    blitterExecute();
    dma_cyc_blit += DWT_CYC - c0;
#else
    blitterExecute();
#endif
    
    //A Free slot for CPU... but the CPU isn't currently bound to the DMA timing
    // SDL_AtomicSet(&cpuWait, 0); // single-threaded on RP2350
}

// A bitplane slot that fetches nothing is free: the Copper may use it only
// if it is even (OCS gives the Copper even cycles), the blitter/CPU if odd.
// Falling back to evenCycle() on odd slots let the Copper run a cycle early
// or late (ATK's colour bars alternated 84/92 columns instead of 88).
static inline void freeCycle(void) {
    if (internal.hPos & 1)
        oddCycle();
    else
        evenCycle();
}

// Returns the first slot in [hPos, limit) where the Copper or the blitter
// may act on this line, or limit if neither can before it. Conservative:
// any doubt returns hPos (no skip).
static int __attribute__((noinline)) dmaIdleCompute(int limit) {
    const int h = internal.hPos;
    if (blitterState != 3 &&
        (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240))
        return h;
    if ((chipset.dmaconr & 0x280) != 0x280 || internal.copperCycle == 4)
        return limit;
    if (internal.copperCycle != 3)
        return h;
    // frank-amiga: a WAIT with BFD clear also waits for the blitter. While
    // a computed blit waits out its time (state 3) its end is known: the
    // Copper cannot act before it, whatever the beam. Arte's dot landscape
    // starts ~50 blits a frame from the Copper, each followed by such a
    // WAIT (mask 0): they were stepped slot by slot (30 fps on the board).
    int floor = h;
    if (!(internal.IR2 & 0x8000u) && (chipset.dmaconr & 0x4000u)) {
        if (blitterState != 3)
            return h;
        const int32_t rem = (int32_t)(blitDeadline - dmaSlotClock);
        if (rem <= 0)
            return h;
        if (h + rem >= limit)
            return limit;
        floor = h + rem;
    }
    if (!(copperWaitNextBank && (internal.vPos & 0xff) == 0xff) &&
        (copperBeam() & copperCompareMask()) >= copperWaitPosition)
        return floor;
    // Waiting. With the standard horizontal compare mask the masked beam
    // grows with hPos, so the wait completes at its horizontal position if
    // it completes on this line at all.
    // frank-amiga: with no horizontal compare bits (Lemmings' WAITs: mask
    // $FF00) the masked beam does not change along the line, so a wait not
    // reached now is not reached before the next line.
    if ((internal.IR2 & 0xfe) == 0)
        return limit;
    if ((internal.IR2 & 0xfe) != 0xfe)
        return floor;
    if (copperWaitNextBank && (internal.vPos & 0xff) == 0xff)
        return limit;
    const uint32_t lineEnd = ((uint32_t)(internal.vPos & 0xff) << 8) | (uint32_t)lineLast;
    if ((lineEnd & internal.IR2) < copperWaitPosition)
        return limit;               // not before the next line
    const int waitH = (int)(copperWaitPosition & 0xfe);
    if (waitH <= floor)
        return floor;
    return waitH < limit ? waitH : limit;
}

// dmaIdleCompute() for the whole line, cached. Its inputs (blitter state,
// DMACON, the Copper's state and wait, the line) only change when the Copper
// or the blitter act, which they cannot before the horizon, at the end of
// the line, or by a CPU write to a custom register between slices; so a
// query on the same line before the horizon gives the same answer.
int dmaIdleCacheValid;
static int idleHorizon;   // first slot where the Copper/blitter may act
static int idleLine;      // internal.vPos it was computed for

static void __attribute__((noinline)) dmaIdleRefresh(void) {
    idleHorizon = dmaIdleCompute(lineLast + 1);
    idleLine = internal.vPos;
    dmaIdleCacheValid = 1;
}

// Inlined into dma_run(): a cache hit is a few compares, no call.
static inline __attribute__((always_inline)) int dmaIdleUntil(int limit) {
    // frank-amiga: the two states in which nothing can be skipped (a busy
    // blitter, a Copper executing rather than waiting) answer "here" with
    // a couple of loads, as dmaIdleCompute() would. Through the cache they
    // cost a refresh on every slot (~23 % of the CPU in State of the Art's
    // blitter-heavy part).
    if (blitterState == 3) {
        // Only the blit's end is due: run up to it.
        const int32_t rem = (int32_t)(blitDeadline - dmaSlotClock);
        if (rem <= 0)
            return internal.hPos;
        if (internal.hPos + rem < limit)
            limit = internal.hPos + rem;
    } else if (blitterState != 0 || (chipset.dmaconr & 0x4240) == 0x4240) {
        return internal.hPos;
    }
    if ((chipset.dmaconr & 0x280) == 0x280 && internal.copperCycle != 4 &&
        internal.copperCycle != 3)
        return internal.hPos;
    if (!dmaIdleCacheValid || idleLine != internal.vPos ||
        internal.hPos >= idleHorizon)
        dmaIdleRefresh();
    return idleHorizon < limit ? idleHorizon : limit;
}

void dramCycle(void){
    
}

int turboFloppy = 8;    //8 for turbo
uint64_t floppyBits;    // frank-amiga: the last bits read off the disk (diskCycle)
int floppyPhase;        // the sync's bit offset in them, which the words keep

void diskCycle(void){
    
        
    if( (chipset.dmaconr & 0x210) && chipset.dsklen & 0x8000 ){
        
        if(chipset.dsklen & 0x3FFF){
            
            // frank-amiga: DSKLEN WRITE — the words go to the drive
            // (Floppy.c decodes the track's sectors into its ADF).
            if (chipset.dsklen & 0x4000) {
                for (int i = 0; i < turboFloppy; ++i) {
                    if (floppyRealSpeed && !floppyWordDue())
                        return;     // frank-amiga: the next word is not due yet
                    floppyWriteByte(*chip_ptr(chipset.dskpt & CHIPTOP));
                    floppyWriteByte(*chip_ptr((chipset.dskpt + 1) & CHIPTOP));
                    chipset.dskpt += 2;
                    chipset.dsklen -= 1;
                    if ((chipset.dsklen & 0x3FFF) == 0) {
                        floppyWriteDone();
                        putChipReg16[INTREQ](0x8002);   //Disk block done INT
                        return;
                    }
                }
                return;
            }

            if (!floppyDataReady())
                return;     // frank-amiga: flux track still on its way
            // frank-amiga: a sync word that cannot come (no drive or no disk
            // at the selected port) is not searched for in endless zeros.
            if (!floppySync && (chipset.adkconr & 0x400) && !floppyHasMedia()) {
                // No drive at all at that port: the read ends at once with
                // nothing read (fast loading: a loader polling every port for
                // a disk would otherwise wait out its timeout on each).
                if (floppyPortEmpty()) {
                    for (; chipset.dsklen & 0x3FFF; chipset.dsklen--) {
                        *chip_ptr(chipset.dskpt & CHIPTOP) = 0;
                        *chip_ptr((chipset.dskpt + 1) & CHIPTOP) = 0;
                        chipset.dskpt += 2;
                    }
                    putChipReg16[INTREQ](0x8002);   //Disk block done INT
                }
                return;
            }

            // frank-amiga: without ADKCON WORDSYNC the transfer starts at
            // once instead of at the next sync word.
            if (!(chipset.adkconr & 0x400) && !floppySync) {
                floppySync = 1;
                floppyPhase = 0;
            }

            // frank-amiga: Paula finds the sync word at any bit position, and
            // the words after it keep that alignment (custom formats and
            // protections such as Rob Northen Copylock place sync words off
            // the 16-bit grid). The bits stream through a window; while
            // waiting, each new word's 16 bit phases are compared.
            for(int i=0;i<turboFloppy;++i){
                if (!floppyWordDue())
                    return;     // frank-amiga: a timed track's next word is not under the head yet
                const uint8_t b1 = floppyDataRead();
                const uint8_t b2 = floppyDataRead();
                floppyBits = floppyBits << 16 | (uint32_t)(b1 << 8 | b2);

                int found = -1;
                const uint32_t sync = chipset.dsksync;
                for (int k = 15; k >= 0; k--)
                    if (((floppyBits >> k) & 0xFFFFu) == sync) { found = k; break; }
                if (found >= 0) {
                    putChipReg16[INTREQ](0x9000);   //DSKSYNC INT
                    chipset.dskbytr |= 0x1000;  // set word sync bit
                    if (!floppySync) {
                        floppySync = 1;
                        floppyPhase = found;
                        continue;               // the data starts after it
                    }
                }

                if (floppySync) {
                    const uint16_t word = (uint16_t)(floppyBits >> floppyPhase);
#ifdef FRANK_DSK_SUM
                    { extern uint32_t frankDskSum; frankDskSum = frankDskSum * 31u + word; }
#endif
                    *chip_ptr(chipset.dskpt & CHIPTOP) = (uint8_t)(word >> 8);
                    chipset.dskpt += 1;
                    *chip_ptr(chipset.dskpt & CHIPTOP) = (uint8_t)word;
                    chipset.dskpt += 1;

                    chipset.dsklen -= 1;

                    if( (chipset.dsklen & 0x3FFF) == 0){
#ifdef FRANK_DSK_SUM
                        { extern uint32_t frankDskSum; extern void frankDskDone(void); frankDskDone(); }
#endif
                        putChipReg16[INTREQ](0x8002);   //Disk block loaded INT
                        floppySync = 0;
                        return;
                    }
                }
            }
            
            
            return;
        }
       
    }
    
    oddCycle();
}


// Audio DMA slots: with the channel's DMA on the slot is Paula's (the data
// itself is fetched by Audio.c once per line); otherwise the blitter may
// use it.
#define AUDIO_SLOT(c) \
    void audio##c##Cycle(void){ \
        if ((chipset.dmaconr & (0x200u | (1u << (c)))) != (0x200u | (1u << (c)))) \
            oddCycle(); \
    }
AUDIO_SLOT(0)
AUDIO_SLOT(1)
AUDIO_SLOT(2)
AUDIO_SLOT(3)


// ── Sprites ───────────────────────────────────────────────────────────────
// Sprite n owns two DMA slots per line (0x15 + 4n and 0x17 + 4n). On the
// first line after vertical blanking it fetches SPRxPOS/SPRxCTL; from
// VSTART it fetches SPRxDATA/SPRxDATB on every line until VSTOP, where the
// next POS/CTL pair follows (a sprite reused further down). Words go
// through the register handlers, so DATA arms and CTL disarms the sprite
// exactly as CPU writes do. Unused slots stay free for the blitter.
enum { SPRITE_CONTROL, SPRITE_WAIT, SPRITE_ACTIVE, SPRITE_DONE };
enum { SPRITE_FETCH_NONE, SPRITE_FETCH_CONTROL, SPRITE_FETCH_DATA };
static uint8_t spriteState[8];
static uint8_t spriteLineFetch[8];

static inline uint16_t *spriteRegisters(unsigned n) {
    return &chipset.spr0pos + 4u * n;  // POS, CTL, DATA, DATB
}

static inline int spriteVStart(const uint16_t *r) {
    return (r[0] >> 8) | ((r[1] & 0x4) << 6);
}

static inline int spriteVStop(const uint16_t *r) {
    return (r[1] >> 8) | ((r[1] & 0x2) << 7);
}

static void spriteFrameStart(void) {
    for (unsigned n = 0; n < 8; ++n)
        spriteState[n] = SPRITE_CONTROL;
}

void spriteCycle(void){
    const unsigned slot = internal.hPos - 0x15u;
    const unsigned n = slot >> 2;
    const unsigned second = (slot >> 1) & 1u;
    if (n > 7u || (chipset.dmaconr & 0x220) != 0x220 ||
        internal.vPos < OMEGA_SPRITE_FIRST_LINE) {
        oddCycle();
        return;
    }
    uint16_t *r = spriteRegisters(n);
    if (!second) {
        uint8_t fetch = SPRITE_FETCH_NONE;
        switch (spriteState[n]) {
        case SPRITE_CONTROL:
            fetch = SPRITE_FETCH_CONTROL;
            break;
        case SPRITE_WAIT:
            if (internal.vPos == spriteVStart(r)) {
                spriteState[n] = SPRITE_ACTIVE;
                fetch = internal.vPos == spriteVStop(r)
                      ? SPRITE_FETCH_CONTROL : SPRITE_FETCH_DATA;
            }
            break;
        case SPRITE_ACTIVE:
            fetch = internal.vPos == spriteVStop(r)
                  ? SPRITE_FETCH_CONTROL : SPRITE_FETCH_DATA;
            break;
        default:
            break;
        }
        spriteLineFetch[n] = fetch;
    }
    const uint8_t fetch = spriteLineFetch[n];
    if (fetch == SPRITE_FETCH_NONE) {
        oddCycle();
        return;
    }
    uint32_t *pt = &chipset.spr0pt + n;
    uint16_t word = CHIPW(*pt);
    word = (uint16_t)((word << 8) | (word >> 8));   // chip RAM is byte-swapped
    *pt += 1;
    const unsigned reg = fetch == SPRITE_FETCH_CONTROL ? SPR0POS : SPR0DATA;
    putChipReg16[reg + 4u * n + second](word);
    if (fetch == SPRITE_FETCH_CONTROL && second)
        spriteState[n] = (r[0] == 0 && r[1] == 0) ? SPRITE_DONE : SPRITE_WAIT;
}


// Draws one sprite (or attached pair) into the ARGB raster (native build
// and the RP2350 fallback layouts).
static void spriteDrawArgb(int row, int x, int colour_base, int attached,
                           int behind, uint16_t a, uint16_t b, uint16_t c,
                           uint16_t d) {
    for (int i = 0; i < 16; ++i) {
        const int bit = 15 - i;
        unsigned v = ((a >> bit) & 1u) | (((b >> bit) & 1u) << 1);
        if (attached)
            v |= (((c >> bit) & 1u) << 2) | (((d >> bit) & 1u) << 3);
        if (!v)
            continue;
        const uint32_t colour = internal.palette[colour_base + v];
        for (int j = 0; j < 2; ++j) {
            int px = x + 2 * i + j;
            // Rotated rows show raster columns 0..rotation-1 at their right
            // edge.
            if (lineRowRotation && px >= HOST_RASTER_W)
                px -= HOST_RASTER_W;
            if (px < 0 || px >= HOST_RASTER_W)
                continue;
            uint32_t *p = hostRasterPixels(row, px);
            if (behind && *p != internal.palette[0])
                continue;
            *p = colour;
        }
    }
}

// Shows the armed sprites of this line over its bitplane pixels. Lower
// numbered sprites are in front; an odd sprite with ATT set joins its even
// partner as one 15-colour sprite. BPLCON2 (PF2P, single playfield) puts
// sprite pairs at or above its code behind the playfield.
static void spriteRenderLine(void) {
    const int row = spriteLineRow;
    if (row < 0 || row >= HOST_RASTER_H || !spriteArmed)
        return;
    // Raster column 0 is the first pixel of the first fetched word; OCS
    // shows it at lores position 2 * DDFSTRT + 17 (LORES) or + 9 (HIRES).
    const int hires = (chipset.bplcon0 & 0x8000) != 0;
    const int first = 2 * lineRasterDdf +
                      (hires ? OMEGA_SPRITE_HIRES_OFFSET
                             : OMEGA_SPRITE_LORES_OFFSET);
    // The rotated layout (Kickstart 3.1.4's desktop) keeps the bitplanes'
    // OCS column 16, the display window's first pixel at DIWSTRT 0x81, at
    // raster column OMEGA_DDF_ROTATED_PIXELS; sprites follow them there
    // (they were drawn 64 columns left of the mouse position).
    const int shift = lineRowRotation ? lineRowRotation - 16 : 0;
    const unsigned front_pairs = (chipset.bplcon2 >> 3) & 7u;
    // Sprites show only inside the display window: DIWSTRT/DIWSTOP low
    // bytes in lores beam positions (OCS: HSTOP8 is fixed at 1).
    const int diw_start = chipset.diwstrt & 0xff;
    const int diw_stop = (chipset.diwstop & 0xff) | 0x100;
    // Collisions see each armed sprite on its own (clipped like its pixels).
    for (int n = 0; clxActive && n < 8; ++n) {
        if (!((spriteArmed >> n) & 1u))
            continue;
        const uint16_t *r = spriteRegisters((unsigned)n);
        const int hstart = ((r[0] & 0xff) << 1) | (r[1] & 1);
        uint32_t mask = 0xffffu;
        if (hstart < diw_start)
            mask &= diw_start - hstart >= 16 ? 0u : 0xffffu >> (diw_start - hstart);
        if (hstart + 16 > diw_stop)
            mask &= hstart >= diw_stop ? 0u
                  : (0xffffu << (hstart + 16 - diw_stop)) & 0xffffu;
        clxSprite(n, 2 * (hstart - first) + shift, (uint16_t)((r[2] | r[3]) & mask));
    }
    for (int pair = 3; pair >= 0; --pair) {
        const unsigned even = 2u * (unsigned)pair, odd = even + 1u;
        const uint16_t *re = spriteRegisters(even), *ro = spriteRegisters(odd);
        const int behind = (unsigned)pair >= front_pairs;
        // OCS: the odd sprite's ATT bit makes the pair one 15-colour sprite
        // (colours 17-31). Each keeps its own position; their bits combine
        // where they overlap, so a pair at one position is drawn together
        // and otherwise each half is drawn with its attached colours.
        const int attached = (ro[1] & 0x80) != 0;
        const int even_on = (spriteArmed >> even) & 1u;
        const int odd_on = (spriteArmed >> odd) & 1u;
        const int hs_even = ((re[0] & 0xff) << 1) | (re[1] & 1);
        const int hs_odd = ((ro[0] & 0xff) << 1) | (ro[1] & 1);
        // Lower sprites are in front: odd first, then even over it.
        for (int k = 1; k >= 0; --k) {
            const int is_odd = k;
            if (!(is_odd ? odd_on : even_on))
                continue;
            if (attached && is_odd && even_on && hs_odd == hs_even)
                continue;               // drawn with the even half
            const uint16_t *rn = is_odd ? ro : re;
            const int hstart = is_odd ? hs_odd : hs_even;
            const int x = 2 * (hstart - first) + shift;
            // Clip to the display window: bit 15 is at hstart.
            uint32_t mask = 0xffffu;
            if (hstart < diw_start)
                mask &= diw_start - hstart >= 16 ? 0u
                      : 0xffffu >> (diw_start - hstart);
            if (hstart + 16 > diw_stop)
                mask &= hstart >= diw_stop ? 0u
                      : (0xffffu << (hstart + 16 - diw_stop)) & 0xffffu;
            if (!mask)
                continue;
            uint16_t a = 0, b = 0, c = 0, d = 0;
            int base = 16 + 4 * pair, att = 0;
            if (!attached) {
                a = rn[2] & mask; b = rn[3] & mask;
            } else {
                base = 16; att = 1;
                if (!is_odd) {
                    a = re[2] & mask; b = re[3] & mask;
                    if (odd_on && hs_odd == hs_even) {
                        c = ro[2] & mask; d = ro[3] & mask;
                    }
                } else {
                    c = ro[2] & mask; d = ro[3] & mask;  // bits 2-3 only
                }
            }
            if (!(a | b | c | d))
                continue;
            if (hostDirectActive)
                hostDirectSprite(row, x, base, att, behind, a, b, c, d);
            else {
                paletteArgbSync();
                spriteDrawArgb(row, x, base, att, behind, a, b, c, d);
            }
        }
    }
}


int bitplaneActive(){
    //check if DMA is on and the line is inside the display window (cached
    //per line); if not let the Copper and Blitter run.
    if (!lineBitplaneWindow)
        return 0;
    
    //too early horisonal position let the Copper and Blitter run
    if(internal.hPos<fetchStart()){
        return 0;
    }
 
    if (internal.hPos > bitplaneLine.lastCycle) {
        return 0;
    }
    
    return 1;
}



void plane6(void){
    
    if(bitplaneActive()==0){
        freeCycle(); // a free slot
        return;
    }
    
    chipset.bpl6dat = 0;
    if( (internal.bitplaneMask & 0x20)  == 0x20){
        markBitplaneFetched(6);
        uint16_t* p = &CHIPW(chipset.bpl6pt);
        chipset.bpl6pt +=1;
        chipset.bpl6dat = *p;
        return;
    }
    
    freeCycle();
}


void plane5(void){
    
    if(bitplaneActive()==0){
        freeCycle(); // a free slot
        return;
    }
    
    chipset.bpl5dat = 0;
    if( (internal.bitplaneMask & 0x10)  == 0x10){
        markBitplaneFetched(5);
        uint16_t* p = &CHIPW(chipset.bpl5pt);
        chipset.bpl5pt +=1;
        chipset.bpl5dat = *p;
        return;
    }

    freeCycle();
    
}
// This line's LORES rows are anchored at DIWSTRT (full width, one raster row
// per line).
static int loresRowsFromDiw;

static inline void loresPlane1Fetch(void);

void loresPlane1(void){
    if(bitplaneActive()==0){
        return;
    }
    loresPlane1Fetch();
}

// loresPlane1() inside the fetch window (also called by dma_run()'s fetch
// runs, where the window is known).
static inline void loresPlane1Fetch(void){
    if(host.pixels == NULL){
        return;
    }
    // A full-width LORES row's first word sits at its beam position:
    // DDFSTRT 0x3C starts 16 columns right of 0x38, an early fetch left of
    // column 0 (from its 8-slot group).
    const int raster_x0 =
        !omegaDdfIsFullWidth(chipset.ddfstrt) ? 0 :
        ((chipset.ddfstrt & ~7) - 0x38) * 4;
    if (bitplaneLine.loresWords++ == 0 && hostDirectActive) {
        host.rasterRow = internal.vPos - OMEGA_DIRECT_FIRST_LINE;
        host.rasterX = (omegaDdfIsFullWidth(chipset.ddfstrt) ? raster_x0
                        : (fetchStart() - lineRasterDdf) * 4) +
                       bplScrollColumns();
        loresRowsFromDiw = 1;   // rows are beam lines: no first-line skip
    } else if (bitplaneLine.loresWords == 1) {
        int full_width = omegaDdfIsFullWidth(chipset.ddfstrt);
        int alternate_rows =
            full_width &&
            omegaLoresUsesAlternateRasterRows(chipset.diwstrt,
                                               chipset.diwstop);
        // Overscan screens such as the Kickstart 1.3 requester start their
        // display window before the standard visible raster. Anchor those
        // rows at the first visible beam line instead of DIWSTRT, otherwise
        // the artwork is shifted down and its lower edge is clipped.
        int raster_origin = alternate_rows
                          ? OMEGA_LORES_FIRST_RENDER_LINE
                          : full_width ? (chipset.diwstrt >> 8)
                                       : OMEGA_DISPLAY_RASTER_ORIGIN;
        int display_line = internal.vPos - raster_origin;
        host.rasterRow =
            alternate_rows ? display_line * 2 : display_line;
        host.rasterX = raster_x0;
        loresRowsFromDiw = full_width && !alternate_rows;
    }
    chipset.bpl1dat = 0;
    if( (internal.bitplaneMask & 0x1)  == 0x1){
        markBitplaneFetched(1);
        host.displayIsLores = 1;
        uint16_t* p = &CHIPW(chipset.bpl1pt);
        chipset.bpl1pt +=1;
        chipset.bpl1dat = *p;
    }

    // Layouts anchored at a fixed first line start rendering there; rows
    // anchored at DIWSTRT are valid from the window's first line (RemGame's
    // NTSC window opens at line 34).
    if (!loresRowsFromDiw && internal.vPos < OMEGA_LORES_FIRST_RENDER_LINE) {
        freeCycle();
        return;
    }
    // HDMI clips a block at the right edge (core 1); the ARGB raster takes
    // whole blocks only.
    if (host.rasterRow < 0 || host.rasterRow >= HOST_RASTER_H ||
        host.rasterX < 0 || host.rasterX >= HOST_RASTER_W ||
        (!hostDirectActive && host.rasterX + 31 >= HOST_RASTER_W)) {
        host.rasterX += 32;   // words outside the image keep their place
        return;
    }
    
    spriteLineRow = host.rasterRow;
    if (clxActive)
        clxLoresBlock(host.rasterX, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat, chipset.bpl5dat, chipset.bpl6dat);
    if (hostDirectActive) {
        if (bitplaneLine.loresWords == 1)
            diwLineWindow(host.rasterRow, 0);
        hostDirectLoresFast(host.rasterRow, host.rasterX, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat, chipset.bpl5dat, chipset.bpl6dat, chipset.bplcon0 & 0x800);
    } else {
        uint32_t *pixels = hostRasterPixels(host.rasterRow, host.rasterX);
        paletteArgbSync();
        if(chipset.bplcon0 & 0x800){
            loresHAM2Chunky(pixels, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat,chipset.bpl5dat, chipset.bpl6dat);
        }else{
            loresPlanar2Chunky(pixels, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat,chipset.bpl5dat, chipset.bpl6dat);
        }
        hostRasterWritten(host.rasterRow, host.rasterX, 32);
    }
    host.rasterX += 32;
    
}



void plane4(){
    
    if(bitplaneActive()==0){
        freeCycle(); // a free slot
        return;
    }
    
    chipset.bpl4dat = 0;
    if( (internal.bitplaneMask & 0x8)  == 0x8){
        markBitplaneFetched(4);
        uint16_t* p = &CHIPW(chipset.bpl4pt);
        chipset.bpl4pt +=1;
        chipset.bpl4dat = *p;
        return;
    }
    
    freeCycle();
    
}

void plane2(){
    
    if(bitplaneActive()==0){
        freeCycle(); // a free slot
        return;
    }
    
    chipset.bpl2dat = 0;
    if( (internal.bitplaneMask & 0x2)  == 0x2){
        markBitplaneFetched(2);
        uint16_t* p = &CHIPW(chipset.bpl2pt);
        chipset.bpl2pt +=1;
        chipset.bpl2dat = *p;
        return;
    }
    
    oddCycle();
    
}

void plane3(){
    
    if(bitplaneActive()==0){
        freeCycle(); // a free slot
        return;
    }
    
    chipset.bpl3dat = 0;
    if( (internal.bitplaneMask & 0x4)  == 0x4){
        markBitplaneFetched(3);
        uint16_t* p = &CHIPW(chipset.bpl3pt);
        chipset.bpl3pt +=1;
        chipset.bpl3dat = *p;
        return;
    }
    
    freeCycle();
    
}

static inline void hiresPlane1Fetch(void);

void hiresPlane1(){

    if(bitplaneActive()==0){

        freeCycle(); // a free slot
        return;
    }
    hiresPlane1Fetch();
}

// hiresPlane1() inside the fetch window (also called by dma_run()'s fetch
// runs, where the window is known).
static inline void hiresPlane1Fetch(void){
    if(host.pixels == NULL){
        return;
    }
    chipset.bpl1dat = 0;
    bitplaneLine.hiresWords++;
    if( (internal.bitplaneMask & 0x1)  == 0x1){
        markBitplaneFetched(1);
        host.displayIsLores = 0;
        uint16_t* p = &CHIPW(chipset.bpl1pt);
        chipset.bpl1pt +=1;
        chipset.bpl1dat = *p;
    }

    // The full-width raster begins 40 PAL beam lines below DIWSTRT. Remove
    // that upper overscan so all 200 useful rows fit in the host framebuffer.
    if (hostDirectActive) {
        if (bitplaneLine.hiresWords == 1) {
            // An early fetch (Agony's intro: DDFSTRT 0x30) starts left of
            // column 0 by its beam distance from 0x3C, 4 columns a slot; a
            // narrow one right of it (see lineRasterDdf).
            host.rasterRow = internal.vPos - OMEGA_DIRECT_FIRST_LINE;
            host.rasterX = ((chipset.ddfstrt < 0x3C && !lineRowRotation) ||
                            !omegaDdfIsFullWidth(chipset.ddfstrt)
                            ? ((chipset.ddfstrt & ~3) - lineRasterDdf) * 4
                            : 0) +
                           bplScrollColumns();
        }
    } else if (lineFullWidth) {
        int display_line = internal.vPos - lineHiresDisplayTop;
        if (display_line < 0)
            return;
        if (bitplaneLine.hiresWords == 1) {
            host.rasterRow = display_line;
            host.rasterX = 0;
        }
    } else if (bitplaneLine.hiresWords == 1) {
        host.rasterRow = internal.vPos - OMEGA_DISPLAY_RASTER_ORIGIN;
        host.rasterX = 0;
    }

    if (!hostDirectActive && internal.vPos < OMEGA_DISPLAY_RASTER_ORIGIN) {
        freeCycle();
        return;
    }
    if (host.rasterRow < 0 || host.rasterRow >= HOST_RASTER_H ||
        host.rasterX < 0 || host.rasterX >= HOST_RASTER_W ||
        (!hostDirectActive && host.rasterX + 15 >= HOST_RASTER_W)) {
        host.rasterX += 16;   // words outside the image keep their place
        return;
    }

    int raster_row = host.rasterRow;
    int row_rotation = lineRowRotation;
    spriteLineRow = host.rasterRow;
    // The leading words in this layout are the pipeline suffix of the
    // preceding logical scanline. Keep that association in the raw raster.
    if (row_rotation && host.rasterX < row_rotation) {
        if (raster_row == 0) {
            host.rasterX += 16;
            return;
        }
        raster_row--;
    }

    if (clxActive)
        clxHiresBlock(host.rasterX, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat);
    if (hostDirectActive) {
        if (bitplaneLine.hiresWords == 1)
            diwLineWindow(raster_row, 1);
        hostDirectHiresFast(raster_row, host.rasterX, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat);
    } else {
        uint32_t *line = hostRasterPixels(raster_row, host.rasterX);
        paletteArgbSync();
        hiresPlanar2Chunky(line, chipset.bpl1dat, chipset.bpl2dat, chipset.bpl3dat, chipset.bpl4dat);
        hostRasterWritten(raster_row, host.rasterX, 16);
    }
    host.rasterX += 16;
    return;
}







int copperExecute(){
    
    if((chipset.dmaconr & 0x280) != 0x280){
        return 0;
    }
        
    switch(internal.copperCycle){
        case 0:
            if (copperSkipPending) {
                copperSkipPending = 0;
                copperSkip = (copperBeam() & copperCompareMask()) >= copperWaitPosition &&
                             ((internal.IR2 & 0x8000u) || !(chipset.dmaconr & 0x4000u));
            }
            internal.IR1 = CHIPW(internal.copperPC);
            internal.IR1 = (internal.IR1 <<8) | (internal.IR1 >>8);
            internal.copperPC += 1;
            internal.copperCycle = 1;
            
            if( (internal.IR1 & 0x1) == 0x1){
                internal.copperCycle = 2;
            }
            return 1;
            break;
            
        case 1:
            internal.IR2 = CHIPW(internal.copperPC);
            internal.copperPC += 1;
            
            internal.IR1 = (internal.IR1 >> 1) & 255;   // divide by 2 and mask off bad bits
            
            if(internal.IR1<0x20){
                internal.copperCycle = 4;return 1;};         //pause copper until next vbl
            if(internal.IR1<0x40 && chipset.copcon==0){
                internal.copperCycle = 4;return 1;};         //pause copper until next vbl, unless we allow copper access to blitter
            

            
            //Move
            internal.IR2 = (internal.IR2 <<8) | (internal.IR2 >> 8);
            debugChipAddress = internal.IR1; //Debug what the Copper is writing to...
            debugChipValue   = internal.IR2;

            internal.copperCycle = 0;
            if (copperSkip) {               // skipped: fetched, not done
                copperSkip = 0;
                return 1;
            }
            if (internal.IR1 == 0x44 || internal.IR1 == 0x45) {
                copperJumpList = internal.IR1 - 0x43;   // 1 or 2
                internal.copperCycle = 9;
                return 1;
            }
            // frank-amiga: colour registers (most Copper MOVEs) straight
            // to setColour(), past the register table and COLORxx.
            if ((unsigned)internal.IR1 - 0xC0u < 32u) {
                setColour((unsigned)internal.IR1 - 0xC0u, internal.IR2);
                return 1;
            }
            putChipReg16[internal.IR1](internal.IR2);
            return 1;
            break;
            
        case 2:
            internal.IR2 = CHIPW(internal.copperPC);
            internal.copperPC += 1;
            
            internal.IR2 = (internal.IR2 <<8) | (internal.IR2 >>8);

            // FFFF FFFE is the Copper-list terminator.  It is commonly
            // described as an unreachable WAIT, but a PAL beam position with
            // bit 8 set compares above 0xFFFE in this extended representation.
            // Freeze explicitly so execution cannot fall into adjacent data
            // after scanline 255.
            if (internal.IR1 == 0xFFFF && internal.IR2 == 0xFFFE) {
                internal.copperCycle = 4;
                return 1;
            }
            
            internal.comparisonMask = (uint16_t)copperCompareMask();

            internal.IR1 &= internal.comparisonMask; //mask the wait position

            copperWaitPosition = internal.IR1;
            // frank-amiga: a WAIT read at the very end of line 255 (after
            // WAIT $FFDF) is for the next bank; earlier on that line it is
            // compared as is (State of the Art's per-line Copper loops wait
            // for VP7 there and must pass).
            copperWaitNextBank = (internal.vPos & 0xff) == 0xff &&
                                 internal.hPos >= 0xD8 &&
                                 (internal.IR1 >> 8) < 0xff;
            copperSkip = 0;

            // WAIT1/WAIT2 or SKIP1/SKIP2 follow.
            internal.copperCycle = (internal.IR2 & 1) ? 7 : 5;
            return 1;
            break;
        case 5: internal.copperCycle = 6; return 1;     // WAIT1
        case 6: internal.copperCycle = 3; return 1;     // WAIT2: now compare
        case 7: internal.copperCycle = 8; return 1;     // SKIP1
        case 8:                                          // SKIP2
            internal.copperCycle = 0;
            copperSkipPending = 1;
            return 1;
        case 9: internal.copperCycle = 10; return 1;    // JMP1
        case 10:                                         // JMP2
            internal.copperPC = copperJumpList == 2 ? chipset.cop2lc : chipset.cop1lc;
            internal.copperCycle = 0;
            return 1;
            
        case 3:
            
            /* old debugging stuff
            if(1){}
            uint16_t vhposr = chipset.vhposr;
            uint16_t wait = internal.IR1;
            uint16_t comparison = internal.IR2;
            */
            
            //Wait
            if(copperWaitReached()){
                internal.copperCycle = 0;
            }
            
            break;
        case 4:
            //Copper operation frozen until next VBL/copper reset
            break;
    }
    
    return 0;
}



//********************Blitter



// frank-amiga: the running blit's duration in colour clocks (> 0 while it
// is deferred; its end is blitDeadline on dmaSlotClock).
static int32_t blitTime;

static int32_t blitEstimate(void) {
    const int32_t h = chipset.bltsizh ? chipset.bltsizh : 64;
    const int32_t v = chipset.bltsizv ? chipset.bltsizv : 1024;
    if (chipset.bltcon1 & 1)
        return 4 * v;                       // line: ~4 cycles a pixel
    const int n = __builtin_popcount((chipset.bltcon0 >> 8) & 0xF);
    return h * v * (n <= 2 ? 2 : n);        // cycles a word by channels
}

static void blitEnd(void) {
    chipset.dmaconr &= 0xBFFF;              // BBUSY off
    putChipReg16[INTREQ](0x8040);           // BLIT interrupt
}

void blitterFinished(void) {
    if (blitTime <= 0)
        blitEnd();
}

int dmaBlitWaitRemaining(void) {
    if (blitterState != 3)
        return 0;
    const int32_t rem = (int32_t)(blitDeadline - dmaSlotClock);
    return rem > 0 ? (int)rem : 0;
}

// frank-amiga: where the next slot runs, for the CPU's core to work out
// VPOSR/VHPOSR at its own time (slave link_pump.c parBeamRead()). pos is
// vPos << 16 | hPos; info the line's last slot << 16 | the frame's first
// line past its end; hi VPOSR's top bits (LOF, chip ID). 0 if the lines
// alternate in length (NTSC), which the reader does not follow.
int dmaBeamNext(uint32_t *pos, uint32_t *info, uint32_t *hi) {
    if (OMEGA_VIDEO_LONG_LINES)
        return 0;
    *pos = (uint32_t)internal.vPos << 16 | (uint32_t)internal.hPos;
    *info = (uint32_t)lineLast << 16 |
            (uint32_t)(OMEGA_VIDEO_FRAME_LINES - (internal.LOF ? 0 : 1));
    *hi = (uint32_t)(internal.LOF << 15) | OMEGA_VIDEO_VPOSR_ID;
    return 1;
}

int dmaBlitterFree(void) {
    return blitterState == 3 ||
           (blitterState == 0 && !(chipset.dmaconr & 0x4000));
}

void blitterNewBlit(void) {
    if (blitterState == 3) {
        blitTime = 0;
        blitterState = 0;
        blitEnd();
    }
}

int blitterExecute(){
    
    int state = blitterState;
    if (state == 3) {
        if ((int32_t)(dmaSlotClock - blitDeadline) >= 0) {
            blitTime = 0;
            blitterState = 0;
            blitEnd();
        }
        return 0;
    }
    if (state == 0 && (chipset.dmaconr & 0x4240) == 0x4240) {
#ifndef PICO_BUILD
        {   // frank-amiga native: OMEGA_BLIT_LOG=from:to logs blits by frame.
            extern unsigned long native_frame_counter;
            static long from = -2, to;
            if (from == -2) {
                const char *e = getenv("OMEGA_BLIT_LOG");
                from = -1;
                if (e) sscanf(e, "%ld:%ld", &from, &to);
            }
            if (from >= 0 && (long)native_frame_counter >= from && (long)native_frame_counter <= to)
                printf("BLIT f%lu v%d h%d con0 %04x con1 %04x size %dx%d A %06x B %06x C %06x D %06x"
                       " mod %d %d %d %d fwm %04x lwm %04x adat %04x bdat %04x cdat %04x\n",
                       native_frame_counter, internal.vPos, internal.hPos, chipset.bltcon0, chipset.bltcon1,
                       chipset.bltsizh, chipset.bltsizv, chipset.bltapt, chipset.bltbpt, chipset.bltcpt,
                       chipset.bltdpt, chipset.bltamod, chipset.bltbmod, chipset.bltcmod, chipset.bltdmod,
                       chipset.bltafwm, chipset.bltalwm, chipset.bltadat, chipset.bltbdat, chipset.bltcdat);
        }
#endif
        blitTime = blitEstimate();
        blitDeadline = dmaSlotClock + (uint32_t)blitTime;
    }
    
    
    switch(state){
            
        case 0:
            //Blitter DMA Enabled and blitter busy flag set (which means bltsize was written to and blitting should start).
            if( (chipset.dmaconr & 0x4240) == 0x4240 ){
                
                if(chipset.bltcon1 & 1){
                    //Line Mode
                    //printf("Line Mode\n");
                    state = 1;
                }else{
                    //Copy Mode
                    //printf("Copy Mode\n");
                    /*
                    internal.useMask = chipset.bltcon0 >> 8;
                    internal.shiftA = chipset.bltcon0 >> 12;
                    internal.shiftB = chipset.bltcon1 >> 12;
                    internal.minterm = chipset.bltcon0 & 255;
                    internal.xIncrement = 1;
                    internal.fillmode = (chipset.bltcon1 & 0x18) >> 3; // 1= Inclusive Fill, 2 = Exclusive Fill
                    
                    //all pointers are word addressed and masked for 2meg only
                    internal.apt=(chipset.bltapt >> 1);
                    internal.bpt=(chipset.bltbpt >> 1);
                    internal.cpt=(chipset.bltcpt >> 1);
                    internal.dpt=(chipset.bltdpt >> 1);
                    
                    
                    internal.amod=chipset.bltamod / 2; //these are signed so need to be divided not shifted
                    internal.bmod=chipset.bltbmod / 2;
                    internal.cmod=chipset.bltcmod / 2;
                    internal.dmod=chipset.bltdmod / 2;
                    
                    //internal.lastHWord=chipset.bltsizh - 1;
                    internal.sizeh = chipset.bltsizh;
                    
                    Internal_t* debug = &internal;
                    */
                    state = 2;
                }
            }
            break;
            
        case 1:
            //Line Mode
            
            //No DMA cycle mode yet
            
            //Immediate mode Blitter
            blitter_execute(&chipset);
            state = blitTime > 0 ? 3 : 0;
            
            break;
            
        case 2:
            //Copy Mode
            //if descend mode is on.
            if((chipset.bltcon1 & 2)){
                internal.xIncrement = -1;
                
                if(internal.fillmode>0){
                    printf("NO FILL MODE YET!!");
                }
                
            }
            
            //DMA cycle mode
            //state = blitterCopyCycle();   //doesn't work at all.
            
            
            //Immediate mode Blitter
            blitter_execute(&chipset);
            state = blitTime > 0 ? 3 : 0;
            
            break;
    }
    
    blitterState = state;
    
    return 0;
}


int blitterCopyCycle(){
    
    
    //Internal_t* debug = &internal;
    
        static uint16_t previousA = 0;
        static uint16_t previousB = 0;
        
            uint16_t channelD = 0;
            
            //Channel A
            if(internal.useMask & 8){
                chipset.bltadat = CHIPW(internal.apt);
                chipset.bltadat = chipset.bltadat << 8 | chipset.bltadat >> 8;
                internal.apt +=internal.xIncrement;
            }
            uint16_t channelA = chipset.bltadat;
            
            //Channel B
            if(internal.useMask & 4){
                chipset.bltbdat = CHIPW(internal.bpt);
                chipset.bltbdat = chipset.bltbdat << 8 | chipset.bltbdat >> 8;
                internal.bpt +=internal.xIncrement;
            }
            uint16_t channelB = chipset.bltbdat;
            
            //Channel C
            if(internal.useMask & 2){
                chipset.bltcdat = CHIPW(internal.cpt);
                chipset.bltcdat = chipset.bltcdat << 8 | chipset.bltcdat >> 8;
                internal.cpt +=internal.xIncrement;
            }
            
            
            //Masking section
            
            if(chipset.bltsizh==internal.sizeh){
                channelA = channelA & chipset.bltafwm;
            }
            
            if(chipset.bltsizh==1){
                channelA = channelA & chipset.bltalwm;
            }
            
            //shifting section
            if(internal.xIncrement==-1){
                
                channelA = (previousA >> (16-internal.shiftA)) | (channelA << internal.shiftA);
                channelB = (previousB >> (16-internal.shiftB)) | (channelB << internal.shiftB);
                
            }else{
                
                channelA = (previousA << (16-internal.shiftA)) | (channelA >> internal.shiftA);
                channelB = (previousB << (16-internal.shiftB)) | (channelB >> internal.shiftB);
                
            }
            previousA = chipset.bltadat;
            previousB = chipset.bltbdat;
            
            
            channelD = logicFunction(internal.minterm, channelA, channelB, chipset.bltcdat);
            
            //Zero Flag
            if(channelD==0){
                chipset.dmaconr = chipset.dmaconr | 0x2000;   //set zero flag
            }else{
                chipset.dmaconr = chipset.dmaconr & 0xDFFF; // clear zero flag
            }
            
            //Channel D
            if(internal.useMask & 1){
                
                channelD = channelD << 8 | channelD >>8;
                CHIPW(internal.dpt) = channelD;
                internal.dpt +=internal.xIncrement;
            }
            
        
        
        if(internal.xIncrement == -1){
            internal.apt -= internal.amod;
            internal.bpt -= internal.bmod;
            internal.cpt -= internal.cmod;
            internal.dpt -= internal.dmod;
        }else{
            internal.apt += internal.amod;
            internal.bpt += internal.bmod;
            internal.cpt += internal.cmod;
            internal.dpt += internal.dmod;
        }
        
        chipset.bltsizh -= 1;
        
        if(chipset.bltsizh == 0x0){
            chipset.bltsizh = internal.sizeh;
            chipset.bltsizv -=1;
            previousA = 0;
            previousB = 0;

        }
        
        if(chipset.bltsizv==0x0){
            //chipset.bltsizh = 0;
            
            //save blitter state - something might depend upon the blitter being in a known state
            //chipset.bltapt = internal.apt << 1;
            chipset.bltbpt = internal.bpt << 1;
            chipset.bltcpt = internal.cpt << 1;
            chipset.bltdpt = internal.dpt << 1;
            
            chipset.dmaconr = chipset.dmaconr & 49151; //clear blitter busy bit
            putChipReg16[INTREQ](0x8040);              // generate an interrupt!
            return 0; // no more copy to do
        }
    
    return 2; // more copy to do!
}

// frank-amiga: the line length for the video standard (NTSC starts with a
// long line, 228 colour clocks), before the chipset runs; the image's first
// line too (DisplayLayout.h).
void dmaVideoStandardInit(int shown_rows) {
#if OMEGA_DYNAMIC_FIRST_LINE
    directShownRows = shown_rows;
    directShownFirst = (256 - shown_rows) / 2;
#endif
    lineLast = OMEGA_VIDEO_LONG_LINES ? SLOT_LAST : SLOT_LAST - 1;
#if OMEGA_DYNAMIC_FIRST_LINE
    omegaDirectFirstLine = wantFirst = DIRECT_FIRST_LINE_STD;
#endif
}
#if OMEGA_VIDEO_RUNTIME
int omegaVideoNtsc = OMEGA_VIDEO_STANDARD == OMEGA_VIDEO_NTSC;
#endif
