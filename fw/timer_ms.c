/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/timer_ms.c -- the millisecond time base, in interrupt or polled mode.
 *
 * Mike Wolak's timer_ms.c with its interrupt plumbing replaced and a
 * polled path added.  The original enabled interrupts with PicoRV32's custom
 * opcode
 *
 *     .insn r 0x0B, 6, 3, %0, %1, x0
 *
 * and relied on that core's single entry point and its q-register scheme.  None
 * of that exists on VexRiscv, and none of it needs to: the timer arrives as
 * mcause 0x8000_0007, fw/trap.c dispatches it out of a 32-entry table, and the
 * handler is an ordinary C function.
 *
 * WHAT DID NOT CHANGE, and should not: a 1 MHz tick (PSC = PANO_CYC_PER_US - 1:
 * 49 at 50 MHz, 66 at 66.667 MHz) divided down to 1 kHz (ARR = PANO_CYC_PER_MS / (PSC + 1) - 1: 999 at 50 MHz,
 * 994 at 66.667 MHz, where the tick is 995.0 kHz and the millisecond is 66,665 cycles, 0.002 % short).
 * The clock is fw/board.h's.
 *
 * ---- THE TWO MODES ARE THE SAME HARDWARE ---------------------------------
 *
 * There is no polled MODE in the peripheral.  The timer counts whenever CR
 * ENABLE is set, and it raises SR.UIF on every update, and neither depends on
 * mie.MTIE or on mstatus.MIE -- those decide only whether the CPU is DELIVERED
 * the interrupt the peripheral is already asserting.  fw/timertest.c proves
 * that directly, in the phase that runs the timer with MTIE closed and requires
 * UIF to be raised with no handler entered.  So "polled mode" here means
 * exactly one thing: nobody opened MTIE, and software consumes UIF itself.
 *
 * That is why one API can serve both, and why the interrupt handler and the
 * poll service below are the SAME two lines -- clear the flag, advance the
 * counter.  Everything else is bookkeeping.
 *
 * THE HANDLER CLEARS SR FIRST, in both paths.  The interrupt is a level: UIF
 * holds the CPU's timerInterrupt input high until it is written, so a handler
 * that returns without clearing is re-entered immediately.  That is correct
 * hardware behaviour, it is what fw/timertest.c's re-entry phase proves, and it
 * is why a missed clear becomes a live-lock rather than a silently dropped
 * tick -- the better of the two failures, because it is visible.
 *
 * ---- WHY POLLED MODE COUNTS ITS OWN LOSSES -------------------------------
 *
 * SR.UIF is a flag and not a count.  A polled program that lets two periods go
 * by between service calls sees one flag and records one tick: the clock runs
 * slow and nothing says so.  The interrupt path cannot do this -- the level
 * re-enters until it is cleared -- so the risk is specific to polling and it
 * has to be measurable rather than assumed away.  Each consumed update is
 * timestamped with mcycle and compared with the period; a gap of n periods
 * records n-1 lost ticks in timer_ms_missed().  mcycle is 32 bits and wraps
 * every 2^32 cycles (64 s at 66.667 MHz, 86 s at 50 MHz), and unsigned subtraction wraps with it, so the
 * comparison is correct across the wrap without a special case.
 */
#include "timer_ms.h"
#include "timer.h"
#include "board.h"
#include "trap.h"
#include "console.h"

/* PANO_CPU_HZ / (PSC + 1) / (ARR + 1) = 1000 Hz: PSC = 66, ARR = 994 at 66.667 MHz (the G2); PSC = 49, ARR = 999 at
 * 50 MHz (the G1).  The core
 * clock is defined once, in fw/board.h; nothing here holds a frequency. */
#define MS_PSC  (PANO_CYC_PER_US - 1u)
#define MS_ARR  (PANO_CYC_PER_MS / (MS_PSC + 1u) - 1u)
#define MS_PERIOD_CYCLES ((MS_PSC + 1u) * (MS_ARR + 1u))    /* 66,665 or 50,000 */

volatile uint32_t millis_counter;

static unsigned int      ms_mode = TIMER_MS_IRQ;
static volatile uint32_t ms_missed;
static volatile uint32_t ms_last_cycle;

/* A HOOK ON THE MILLISECOND TICK, for work that must run more often than its caller's own loop (an audio ring of
 * 10.7 ms refilled at 1 kHz has ten times the margin it needs).  A periodic source is used rather than an audio
 * engine's level watermark: a level that is already true when playback is enabled re-asserts after every top-up
 * and the CPU never leaves its handler.
 *
 * The hook runs with the source already cleared and the counter already advanced, so a slow hook delays
 * the next tick rather than losing this one -- and the polled path's period recovery covers the rest. */
static void (*ms_hook)(void);

void timer_ms_set_hook(void (*fn)(void)) { ms_hook = fn; }
/* a second, optional observer of the tick in INTERRUPT mode: handed the interrupted pc and ra */
static void (*ms_watch)(uint32_t epc, uint32_t ra, const uint32_t *sp);
void timer_ms_set_watch(void (*fn)(uint32_t epc, uint32_t ra, const uint32_t *sp)) { ms_watch = fn; }

/* The whole of a tick, shared by both paths so that they cannot drift apart. */
static inline void ms_tick(void)
{
    timer_clear_irq();              /* clear the SOURCE first, always */
    millis_counter++;
    if (ms_hook) ms_hook();
}

static uint32_t on_tick(uint32_t cause, uint32_t epc, struct trap_frame *f)
{
    (void)cause;
    ms_tick();
    if (ms_watch) ms_watch(epc, f->ra, (const uint32_t *)f + 16);   /* the trap entry pushed 16 words: f + 16 is the interrupted sp */
    return epc;                     /* an interrupt resumes at mepc unchanged */
}

void timer_ms_init(unsigned int mode)
{
    ms_mode = (mode == TIMER_MS_POLL) ? TIMER_MS_POLL : TIMER_MS_IRQ;

    timer_init();                   /* stopped, UIF clear */
    timer_config(MS_PSC, MS_ARR);
    millis_counter = 0;
    ms_missed      = 0;

    if (ms_mode == TIMER_MS_IRQ) {
        trap_set_irq(IRQ_M_TIMER, on_tick);
        irq_enable(IRQ_M_TIMER);    /* mie.MTIE */
        irq_global_enable();        /* mstatus.MIE */
    } else {
        /* No handler, and the mask stays SHUT.  Nothing about the peripheral
         * changes; only who consumes the flag. */
        irq_disable(IRQ_M_TIMER);
    }

    ms_last_cycle = rdcycle();
    timer_start();
}

unsigned int timer_ms_mode(void) { return ms_mode; }

uint32_t timer_ms_missed(void)   { return ms_missed; }

void timer_ms_stop(void)
{
    timer_stop();
    timer_clear_irq();
    if (ms_mode == TIMER_MS_IRQ)
        irq_disable(IRQ_M_TIMER);
}

uint32_t timer_ms_service(void)
{
    uint32_t now, gap, periods;

    if (ms_mode == TIMER_MS_IRQ)
        return 0u;                  /* the handler has already done it */

    if (!timer_irq_pending())
        return 0u;

    now = rdcycle();

    /* How long since the update we last consumed?  One period means we kept up; n periods means n-1 more
     * came and went while the flag was already set.
     *
     * ---- THOSE TICKS ARE RECOVERED, NOT MERELY COUNTED --------------------------------------
     * Advancing the counter by ONE and recording the rest in timer_ms_missed() would make a polled program's
     * clock run slow in proportion to how rarely it calls: a loop of 93 periods that reads the clock a few
     * times a loop would see a clock about fifty times slow.
     *
     * The flag is one bit and cannot count, but mcycle can, and this function already computed the number.
     * Adding it is the whole fix: the polled clock now tracks real time no matter how seldom it is serviced,
     * and timer_ms_missed() keeps its meaning -- ticks that WOULD have been lost, i.e. how far behind the
     * caller is running, which is still worth knowing and is still nobody's business but the caller's.
     *
     * mcycle is 32 bits and wraps every 2^32 cycles (64 s at 66.667 MHz). A program that goes that long without calling gets a
     * wrong gap, but such a program has no time base worth the name; the one-period floor below keeps the
     * clock monotonic whatever happens. */
    gap     = now - ms_last_cycle;
    periods = gap / MS_PERIOD_CYCLES;
    if (periods == 0u)
        periods = 1u;                       /* serviced early: still one real update, never go backwards */
    timer_clear_irq();                      /* clear the SOURCE first, always -- as ms_tick() does */
    millis_counter += periods;
    if (periods > 1u)
        ms_missed += periods - 1u;
    ms_last_cycle = now;

    return periods;
}

uint32_t get_millis(void)
{
    if (ms_mode == TIMER_MS_POLL)
        (void)timer_ms_service();
    return millis_counter;
}

/* The original compared with < and had a wrap path that could spin for the
 * whole 49-day period if it was entered at the wrong moment.  Unsigned
 * SUBTRACTION wraps correctly on its own, so the wrap case disappears rather
 * than being handled: (now - start) is the elapsed count whatever the counter
 * did in between.
 *
 * In polled mode this loop IS the service point, which is what makes
 * sleep_milli() mean the same thing in both modes. */
void sleep_milli(int milliseconds)
{
    uint32_t start, want;

    if (milliseconds <= 0)
        return;
    want = (uint32_t)milliseconds;

    if (ms_mode == TIMER_MS_POLL)
        (void)timer_ms_service();
    start = millis_counter;

    for (;;) {
        if (ms_mode == TIMER_MS_POLL)
            (void)timer_ms_service();
        if ((millis_counter - start) >= want)
            return;
    }
}
