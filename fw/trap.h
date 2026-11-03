/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/trap.h -- standard RISC-V machine-mode trap handling.
 *
 * VexRiscv implements ordinary machine mode, so this is ordinary RISC-V: mtvec
 * points at one entry (direct mode), mcause says what happened, and the
 * vectoring is done here in software off mcause.
 *
 * mcause: bit 31 is the INTERRUPT flag; the low bits are the code, and the two
 * number spaces are SEPARATE.  Interrupt 3 and exception 3 are different things
 * (machine software interrupt vs breakpoint), which is why there are two tables.
 *
 *   interrupts (mcause bit 31 = 1)      exceptions (mcause bit 31 = 0)
 *     3  machine software                 0  instruction address misaligned
 *     7  machine timer                    1  instruction access fault
 *    11  machine external                 2  ILLEGAL INSTRUCTION
 *                                         3  breakpoint (ebreak)
 *                                         4  load address misaligned
 *                                         5  load access fault
 *                                         6  store address misaligned
 *                                         7  store access fault
 *                                        11  ecall from machine mode
 *
 * NONE OF THE THREE INTERRUPTS IS WIRED YET: rtl/pano_soc_vex.v ties
 * timerInterrupt, softwareInterrupt and externalInterruptArray to zero.  The
 * exception side is fully live and needs no hardware, which is what the trap
 * test exercises.
 *
 * A HANDLER RETURNS THE ADDRESS TO RESUME AT.  For an interrupt that is mepc.
 * For a synchronous exception mepc is the OFFENDING instruction, so returning
 * mepc unchanged re-executes it and the trap repeats for ever -- a hang with no
 * message.  A handler that means "step over this" returns mepc + 4.
 */
#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>

#define IRQ_M_SOFT        3
#define IRQ_M_TIMER       7
#define IRQ_M_EXT        11

#define EXC_INSN_MISALIGN 0
#define EXC_INSN_FAULT    1
#define EXC_ILLEGAL       2
#define EXC_BREAKPOINT    3
#define EXC_LOAD_MISALIGN 4
#define EXC_LOAD_FAULT    5
#define EXC_STORE_MISALIGN 6
#define EXC_STORE_FAULT   7
#define EXC_ECALL_M      11

/* Registers saved by trap_entry, in the order it pushes them. */
struct trap_frame {
    uint32_t ra, a0, a1, a2, a3, a4, a5, a6, a7;
    uint32_t t0, t1, t2, t3, t4, t5, t6;
};

/* Returns the address to resume at. */
typedef uint32_t (*trap_handler_t)(uint32_t cause, uint32_t epc, struct trap_frame *f);

void trap_init(void);
void trap_set_irq(unsigned int code, trap_handler_t fn);   /* mcause bit 31 = 1 */
void trap_set_exc(unsigned int code, trap_handler_t fn);   /* mcause bit 31 = 0 */

/* Global interrupt enable (mstatus.MIE) and the per-source mask (mie). */
void     irq_global_enable(void);
uint32_t irq_global_disable(void);          /* returns the previous mstatus.MIE */
void     irq_enable(unsigned int code);     /* an mie bit, e.g. IRQ_M_TIMER */
void     irq_disable(unsigned int code);

/* a screen for an unhandled exception to die on (fw/vgacon.c's vgacon_guru fits): null = console only */
extern void (*trap_screen)(const char *what, uint32_t cause, uint32_t epc);
extern struct trap_frame *trap_last_frame;    /* the frame of the exception fatal() reports (sp = frame + 64) */
extern volatile uint32_t trap_count_irq[32];
extern volatile uint32_t trap_count_exc[32];

uint32_t trap_dispatch(uint32_t cause, uint32_t epc, struct trap_frame *f);

#endif
