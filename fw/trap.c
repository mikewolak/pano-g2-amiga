/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/trap.c -- the software vector table, off mcause.
 *
 * mtvec is in DIRECT mode, so every trap arrives here and the split between
 * interrupts and exceptions is made on mcause bit 31.  The two code spaces are
 * separate, so there are two tables.
 *
 * ONE CAUSE ARRIVES AT A TIME.  Unlike PicoRV32's pending BITMASK, mcause holds a
 * single code -- the hardware has already chosen, by the architectural priority
 * order, which pending interrupt to present.  So there is no mask to walk and no
 * multiple-pending case to get wrong HERE: if two interrupts are pending, the
 * second is still pending when we mret and we trap again immediately.  Priority
 * lives in the hardware, and the software order below only decides which table
 * is consulted.
 */
#include "trap.h"
#include "console.h"

volatile uint32_t trap_count_irq[32];
volatile uint32_t trap_count_exc[32];

static trap_handler_t irq_tab[32];
static trap_handler_t exc_tab[32];

static inline uint32_t csr_read_mstatus(void)
{
    uint32_t v; __asm__ volatile ("csrr %0, mstatus" : "=r"(v)); return v;
}

void trap_init(void)
{
    unsigned int i;
    for (i = 0; i < 32; i++) {
        irq_tab[i] = 0; exc_tab[i] = 0;
        trap_count_irq[i] = 0; trap_count_exc[i] = 0;
    }
    __asm__ volatile ("csrw mie, zero");
    __asm__ volatile ("csrci mstatus, 8");        /* MIE = 0 */
}

void trap_set_irq(unsigned int code, trap_handler_t fn) { if (code < 32) irq_tab[code] = fn; }
void trap_set_exc(unsigned int code, trap_handler_t fn) { if (code < 32) exc_tab[code] = fn; }

void irq_global_enable(void)  { __asm__ volatile ("csrsi mstatus, 8" ::: "memory"); }

uint32_t irq_global_disable(void)
{
    uint32_t prev = csr_read_mstatus();
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
    return prev & 8u;
}

void irq_enable(unsigned int code)
{
    uint32_t bit = 1u << code;
    __asm__ volatile ("csrs mie, %0" :: "r"(bit) : "memory");
}

void irq_disable(unsigned int code)
{
    uint32_t bit = 1u << code;
    __asm__ volatile ("csrc mie, %0" :: "r"(bit) : "memory");
}

static void hex(uint32_t v) { con_hex32(v); }

/* An exception nobody claimed.  Returning mepc unchanged would re-execute the
 * offending instruction for ever, so say what happened and stop somewhere the
 * host can see it.  The console FIFO drops rather than blocks when full, so this
 * cannot itself wedge. */
/* A PROGRAM MAY OFFER A SCREEN TO DIE ON.  fw/vgacon.c's Guru Meditation is one; a program that has brought
 * the video head up points this at it and a trap then shows a blinking red box with the cause and the PC
 * instead of a line nobody is draining.  Left null, the behaviour is exactly as it was. */
void (*trap_screen)(const char *what, uint32_t cause, uint32_t epc) = 0;
struct trap_frame *trap_last_frame = 0;

static uint32_t fatal(uint32_t cause, uint32_t epc)
{
    con_puts_nb("\r\n!! TRAP mcause=");
    hex(cause);
    con_puts_nb(" mepc=");
    hex(epc);
    con_puts_nb(" -- halted\r\n");
    if (trap_screen) trap_screen("unhandled exception", cause, epc);   /* never returns */
    for (;;)
        ;
    return epc;                 /* not reached; the compiler wants it said */
}

uint32_t trap_dispatch(uint32_t cause, uint32_t epc, struct trap_frame *f)
{
    uint32_t code = cause & 0x1fu;

    if (cause & 0x80000000u) {
        trap_count_irq[code]++;
        if (irq_tab[code])
            return irq_tab[code](cause, epc, f);
        /* An interrupt with no handler cannot simply be resumed: the source is
         * still asserted and we would trap straight back in, for ever.  Mask it
         * and carry on -- the count records that it happened. */
        irq_disable(code);
        return epc;
    }

    trap_count_exc[code]++;
    if (exc_tab[code])
        return exc_tab[code](cause, epc, f);
    trap_last_frame = f;
    return fatal(cause, epc);
}

/* The console's "is anybody draining this?" latch.  It lives here rather than in
 * console.h because trap.c is in COMMON and so is linked into every program,
 * which makes this ONE answer per program instead of one per translation unit.
 * See fw/console.h for why the question is asked about the console and not
 * about each byte. */
unsigned char con_no_reader;
