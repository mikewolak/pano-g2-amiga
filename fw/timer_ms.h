/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/timer_ms.h -- the millisecond time base on the 0x8000_2000 timer, in
 * EITHER of the two modes the hardware supports.
 *
 * Mike Wolak's timer_ms.h, retargeted from PicoRV32's private interrupt scheme
 * to ordinary machine mode and extended to support polling as well as
 * interrupts through ONE API.
 *
 * A program chooses a MODE, not a library.  get_millis() and sleep_milli() mean
 * the same thing in both, and nothing else in a program has to change to move
 * between them:
 *
 *     timer_ms_init(TIMER_MS_IRQ);     ... get_millis() ... sleep_milli(10);
 *     timer_ms_init(TIMER_MS_POLL);    ... get_millis() ... sleep_milli(10);
 *
 * TIMER_MS_IRQ installs a handler through fw/trap.c's table on mcause
 * 0x8000_0007 and opens mie.MTIE and mstatus.MIE.  The counter advances on its
 * own; a program can sleep, spin, or do anything else and time still passes.
 *
 * TIMER_MS_POLL leaves mie.MTIE CLOSED and installs no handler.  The timer runs
 * exactly as before -- counting is not gated by the interrupt mask -- and the
 * update flag SR.UIF is consumed by software instead.  get_millis() services
 * the flag before it answers, and sleep_milli() services it in its loop, so the
 * common uses need nothing extra.
 *
 * THE ONE THING POLLED MODE CANNOT DO, stated here because it is the whole
 * difference and it is invisible until it bites: SR.UIF IS A FLAG, NOT A COUNT.
 * If a program lets more than one period pass without servicing it, the second
 * update finds the flag already set and the tick is not recorded -- the clock
 * runs SLOW and nothing reports it.  In interrupt mode that cannot happen,
 * because the level re-enters the handler until it is cleared.  So a polled
 * program must reach a service point at least once per period, and
 * timer_ms_missed() is provided so it can prove that it did rather than assume
 * it: it returns the number of times a service call found the counter had been
 * left unserviced for longer than one period, measured from CNT.
 */
#ifndef TIMER_MS_H
#define TIMER_MS_H

#include <stdint.h>

#define TIMER_MS_IRQ    0u      /* handler on mcause 0x80000007, mie.MTIE open */
#define TIMER_MS_POLL   1u      /* no handler, mie.MTIE closed, software polls */

void         timer_ms_init(unsigned int mode);
unsigned int timer_ms_mode(void);

uint32_t get_millis(void);          /* wraps every ~49 days */
void     sleep_milli(int milliseconds);

/* Service the timer once.  Returns 1 if an update was consumed, 0 if none was
 * pending.  In interrupt mode it is a no-op returning 0 -- the handler has
 * already done it -- so a program that calls it in both modes is correct in
 * both.  A polled program's main loop should call it, or call get_millis(),
 * at least once per period. */
uint32_t timer_ms_service(void);

/* How many times a polled service found more than one period had elapsed, i.e.
 * how many ticks the clock is known to have lost.  Always 0 in interrupt mode.
 * A polled program that cares about its time base should check this. */
uint32_t timer_ms_missed(void);

/* Stop the timer and detach.  Leaves the peripheral idle and the flag clear. */
void timer_ms_stop(void);

/* Run `fn' on every millisecond tick, in whichever mode the clock is in.  For work that must happen far
 * more often than the caller's own loop reaches it -- see the note in timer_ms.c.  Pass 0 to remove it.
 * The hook must be BOUNDED: it runs inside the tick. */
void timer_ms_set_hook(void (*fn)(void));
/* Observe every tick in INTERRUPT mode with the interrupted pc, ra and stack pointer.  Must be bounded and must not print. */
void timer_ms_set_watch(void (*fn)(uint32_t epc, uint32_t ra, const uint32_t *sp));   /* sp: the interrupted stack */
#endif
