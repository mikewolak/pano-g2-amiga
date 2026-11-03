/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/console.h -- the SoC console: the JTAG cable pretending to be a UART.
 *
 *   0x8000_1000  RX   read: {bit8 valid, [7:0] data}.  READING POPS -- there is
 *                     no peek, so read once and use both fields of the result.
 *   0x8000_1004  TX   read: {bit8 ready}; write: [7:0] pushes a byte
 *
 * The output FIFO is 256 bytes.  The HARDWARE drops a byte written while it is
 * full and latches tx_ovf (which 'V' reports).
 *
 * GIVE UP ON THE READER, NOT ON THE BYTE.  A board booting standalone from
 * flash with no cable stops at a console line when the FIFO fills: it is not
 * the storage path.  The loader prints 226 bytes to get through its storage line and the
 * FIFO is 256, so the NEXT line is the one that overflows -- the stuck point was
 * chosen by the FIFO size, and the last whole line that fits happened to be the
 * one naming the stick.  The board's own clock proved it was alive the whole
 * time: "[t=114885ms countdown end]", a 3-second countdown finishing 114.9 s in.
 *
 * Both of the old primitives were wrong for a standalone boot, in two different
 * ways.  con_putc spun for ever, so a program whose banner and error
 * paths are blocking con_puts parked inside a print at byte 257 on a cold boot
 * with no reader.  con_putc_nb bounded the
 * spin but gave up ON THAT BYTE, so it paid the FULL budget again for the very
 * next one: with the FIFO full every remaining byte of the boot cost a complete
 * spin, which is where those 112 seconds went.  Per-byte patience is the bug in
 * both -- the question "is anybody draining this?" is about the CONSOLE, and its
 * answer is the same for every byte until something changes.
 *
 * So ask it once.  If the FIFO stays full for a whole second -- far longer than
 * any attached reader's poll interval, but a one-time cost at boot -- conclude
 * nobody is listening and latch it; from then on each byte costs a single probe
 * and is dropped.  The first probe that finds room clears the latch, so plugging
 * the cable in mid-run restores full output with nothing further to do.  A
 * program whose output IS the result loses nothing, because a host that drains
 * never lets the FIFO stay full for a second and the latch never trips.
 */
#ifndef CONSOLE_H
#define CONSOLE_H
#include "board.h"                         /* PANO_CPU_HZ: the clock rdcycle counts */

#define CON_RX (*(volatile unsigned int *)0x80001000u)
#define CON_TX (*(volatile unsigned int *)0x80001004u)

/* mcycle, not rdcycle.  We run in machine mode, where mcycle (0xB00) is the
 * architectural counter; rdcycle (0xC00) is its user-mode shadow and a core is
 * free not to map it.  Reading the one that is guaranteed to be there costs
 * nothing and removes a way for a banner to turn into an illegal instruction. */
static inline unsigned int rdcycle(void)
{
    unsigned int v;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(v));
    return v;
}

/* ONE latch for the whole program, not one per translation unit: a file-scope
 * static in a header would give a large program fifty separate answers to the same
 * question and make each of them pay the second of patience.  It lives in
 * trap.c, which is in COMMON and so is linked into every program there is. */
extern unsigned char con_no_reader;

#define CON_TX_PATIENCE PANO_CPU_HZ               /* one second of cycles (fw/board.h) */

static inline void con_putc(char c)
{
    unsigned int t0;
    if (CON_TX & 0x100u) { con_no_reader = 0u; CON_TX = (unsigned char)c; return; }
    if (con_no_reader) return;                /* asked and answered: one probe, then drop */
    t0 = rdcycle();
    while (!(CON_TX & 0x100u))
        if (rdcycle() - t0 > CON_TX_PATIENCE) { con_no_reader = 1u; return; }
    CON_TX = (unsigned char)c;
}

/* The _nb twin existed because con_putc could park for ever and a
 * program whose real output is the screen must not.  con_putc cannot park any
 * more, so the two have collapsed into one -- but the name stays, because some
 * thirty call sites say _nb to mean "this is only a log" and that intent is
 * worth keeping legible even now that it costs nothing. */
static inline void con_putc_nb(char c) { con_putc(c); }

static inline void con_puts_nb(const char *s)
{
    while (*s) con_putc_nb(*s++);
}

static inline int con_getc(void)     /* -1 when nothing is waiting */
{
    unsigned int r = CON_RX;
    return (r & 0x100u) ? (int)(r & 0xffu) : -1;
}

static inline void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static inline void con_hex32(unsigned int v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        con_putc("0123456789ABCDEF"[(v >> i) & 0xf]);
}

/* the non-blocking twin, for the same reason con_puts_nb exists: a program whose log is the screen must
 * never park inside a console write waiting for a reader that is not there */
static inline void con_hex32_nb(unsigned int v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        con_putc_nb("0123456789ABCDEF"[(v >> i) & 0xf]);
}

#endif
