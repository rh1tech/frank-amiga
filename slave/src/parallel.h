/*
 * frank-amiga — slave: the 68000 and the chipset on separate cores.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * With FRANK_PARALLEL, core 1 runs the 68000 (Musashi) between its link
 * pump duties and core 0 runs the chipset (DMA slots, Copper, blitter,
 * CIAs, Paula, disk) behind it. Time is counted in DMA slots since the
 * start (two CPU cycles a slot):
 *
 *  - par_cpu_slot: how far the CPU has run, published by core 1;
 *  - par_dma_slot: how far the chipset has run, published by core 0.
 *
 * The chipset never runs past the CPU; the CPU runs at most PAR_LEAD_SLOTS
 * ahead of the chipset. A CPU access to a custom or CIA register
 * (DMA_CATCH_UP()) publishes the CPU's exact slot and waits until the
 * chipset has reached it, then makes the access while the chipset waits
 * for the CPU to move on: the CPU sees and moves the chipset at its own
 * time, as with the catch-up scheduling on one core. Interrupts the chipset
 * raises reach the CPU at its next instruction (m68k_set_irq_deferred()),
 * up to PAR_LEAD_SLOTS late; chip RAM is shared without locks, so the CPU
 * may read what the chipset writes up to as late.
 *
 * Core 0 can stop the CPU (bank promotion, keyboard reset, savestates):
 * par_stop_req, answered by par_cpu_parked (1: between instructions, 2:
 * waiting inside one).
 */
#ifndef FRANK_PARALLEL_H
#define FRANK_PARALLEL_H

#include <stdbool.h>
#include <stdint.h>

#define PAR_LEAD_SLOTS 128u    /* the CPU's lead over the chipset */
#define PAR_CPU_CHUNK 96       /* CPU cycles per m68k_execute() call */
/* CPU cycles per DMA slot as a shift: 1 = 2 cycles (7.09 MHz, A500),
 * 2 = 4 cycles (14.19 MHz, A1200). */
#ifndef PAR_CPU_SHIFT
#define PAR_CPU_SHIFT 1
#endif

extern volatile uint32_t par_cpu_slot, par_dma_slot;
extern volatile bool par_running;          /* core 1 runs the CPU */
extern volatile int par_stop_req, par_cpu_parked;

/* Core 0: stop the CPU; with `between`, only between two instructions.
 * Returns false (CPU not stopped, nothing to undo) if it could not. */
bool par_stop(bool between);
void par_resume(void);

/* Core 1, from inside an instruction: wait for the chipset to reach the
 * CPU's current slot (DMA_CATCH_UP()). */
void parSync(void);

/* Core 0, after running the chipset up to `slot` and before publishing it:
 * what the blitter is doing, for parSkipSync() (link_pump.c). */
void par_publish_blit(uint32_t slot);

/* Core 1: VPOSR and VHPOSR at the CPU's current slot, from the beam core 0
 * last published, without waiting for it; false if it must sync. */
int parBeamRead(uint32_t *vposr, uint32_t *vhposr);

#ifdef FRANK_PROF
/* Profiling (DWT cycles, each core its own counter): where the cores'
 * time goes; main.c prints them per frame. */
#define PROF_CYC (*(volatile uint32_t *)0xE0001004u)
static inline void prof_dwt_on(void) {
    *(volatile uint32_t *)0xE000EDFCu |= 1u << 24;   /* DEMCR.TRCENA */
    *(volatile uint32_t *)0xE0001000u |= 1u;         /* CYCCNTENA */
}
extern volatile uint32_t prof_c0_dma, prof_c0_wait_cpu;
extern volatile uint32_t prof_c1_cpu, prof_c1_sync, prof_c1_syncs, prof_c1_lead, prof_c1_pump;
#endif
#endif
