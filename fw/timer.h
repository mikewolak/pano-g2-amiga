/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/timer.h -- the timer at 0x8000_2000.
 *
 * A port of Mike Wolak's timer_regs.h (an iCE40 RISC-V SoC of his), with two
 * changes and no others:
 *
 *   - the base is 0x8000_2000, not 0x8000_0020.  On this SoC the peripheral
 *     page is decoded on byte address bits [15:12] (pano_soc_vex.v, per_idx),
 *     so peripherals are 4 KB apart: console 0x8000_1000, timer 0x8000_2000,
 *     DMA 0x8000_3000.  The register OFFSETS inside the block are unchanged.
 *
 *   - timer_config() takes a uint32_t prescaler, not a uint16_t.  The hardware
 *     register is 32 bits wide here (see rtl/soc_timer.v, deviation 2): a
 *     32-bit CPU gets 32-bit registers, and a 16-bit parameter would put the
 *     truncation back in the header after it was taken out of the RTL.
 *
 * THE CONTRACT:
 *
 *     update rate = SYSCLK / (PSC + 1) / (ARR + 1)
 *
 * SYSCLK is the CPU clock, PANO_CPU_HZ in fw/board.h (66.667 MHz on the G2, 50 MHz
 * on the G1).  PSC = PANO_CYC_PER_US - 1 (66 or 49) gives a 1 MHz tick (995 kHz
 * on the G2) and ARR = PANO_CYC_PER_MS / (PSC + 1) - 1 (994 or 999) the 1 kHz millisecond interrupt that fw/timer_ms.c is built
 * on; the same PSC with ARR = 16666 gives 60 Hz.
 *
 * THE INTERRUPT IS A LEVEL.  UIF stays raised, and the CPU's timerInterrupt
 * input stays asserted, until software writes 1 to SR.  A handler that returns
 * without doing so is re-entered immediately and for ever -- which is the
 * architectural behaviour of mip.MTIP, not a defect.  timer_clear_irq() is the
 * first thing a handler should call.
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_BASE      0x80002000u

#define TIMER_CR   (*(volatile uint32_t *)(TIMER_BASE + 0x00u))
#define TIMER_SR   (*(volatile uint32_t *)(TIMER_BASE + 0x04u))
#define TIMER_PSC  (*(volatile uint32_t *)(TIMER_BASE + 0x08u))
#define TIMER_ARR  (*(volatile uint32_t *)(TIMER_BASE + 0x0Cu))
#define TIMER_CNT  (*(volatile uint32_t *)(TIMER_BASE + 0x10u))

#define TIMER_CR_ENABLE     (1u << 0)
#define TIMER_CR_ONE_SHOT   (1u << 1)
#define TIMER_SR_UIF        (1u << 0)

/* Stopped, continuous, no interrupt pending. */
static inline void timer_init(void)
{
    TIMER_CR = 0u;
    TIMER_SR = TIMER_SR_UIF;
}

static inline void timer_config(uint32_t psc, uint32_t arr)
{
    TIMER_PSC = psc;
    TIMER_ARR = arr;
}

/* Enabling from stopped loads CNT from ARR and the prescaler from PSC, so a
 * start is always from a known state.  Writing CR while ALREADY enabled does
 * not reload -- that is how ONE_SHOT is changed without disturbing the count. */
static inline void timer_start(void)          { TIMER_CR = TIMER_CR_ENABLE; }
static inline void timer_start_oneshot(void)  { TIMER_CR = TIMER_CR_ENABLE | TIMER_CR_ONE_SHOT; }
static inline void timer_stop(void)           { TIMER_CR = 0u; }

static inline void     timer_clear_irq(void)    { TIMER_SR = TIMER_SR_UIF; }
static inline uint32_t timer_irq_pending(void)  { return TIMER_SR & TIMER_SR_UIF; }
static inline uint32_t timer_read_counter(void) { return TIMER_CNT; }
static inline uint32_t timer_enabled(void)      { return TIMER_CR & TIMER_CR_ENABLE; }

#endif
