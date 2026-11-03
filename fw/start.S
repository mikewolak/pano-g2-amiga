/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/start.S -- reset and trap entry for the bring-up SoC (VexRiscv, RV32IM).
 *
 * THIS IS ORDINARY RISC-V.  VexRiscv implements standard machine mode: mtvec,
 * mcause, mepc, mstatus.  That is the reason the project moved to it -- the
 * previous core's interrupts were a private scheme (q registers, retirq, one
 * fixed entry point, no mcause) and everything written against it was
 * throwaway.  Nothing below is core-specific; it is what any RV32 machine-mode
 * platform looks like.
 *
 * The entry point is whatever the loader put in externalResetVector ('G <addr>'
 * over the console), so there is no fixed reset address to lay out around and no
 * trampoline.  mtvec is set here, in software, to trap_entry.
 *
 * DIRECT MODE, not vectored: mtvec's low two bits are 0, so every trap lands at
 * one address and the vectoring is done in C off mcause.  Vectored mode would
 * only split interrupts by cause and would still funnel every exception to base;
 * one entry point and a table is simpler and covers both.
 *
 * WHY EVERY CALLER-SAVED REGISTER IS SAVED.  A trap lands at an arbitrary
 * instruction, not at an ABI call boundary, so "the caller saved what it cared
 * about" does not hold: the interrupted code may be mid-expression with live
 * values in a0-a7 and t0-t6.  A partial save is a defect, not an optimisation,
 * and its symptom is intermittent corruption that depends on where the trap fell.
 */
    .section .text.init, "ax"
    .global _start
_start:
    la      sp, _stack_top
    la      gp, __global_pointer$

    la      t0, trap_entry
    csrw    mtvec, t0               /* direct mode: low 2 bits clear */
    csrw    mie, zero               /* every interrupt source masked          */
    csrw    mstatus, zero           /* MIE clear: interrupts globally off     */

    /* .data needs no copy: the loader wrote the whole image into RAM. */
    la      a0, _sbss
    la      a1, _ebss
1:  bgeu    a0, a1, 2f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       1b

    /* the fast RAM (.fastram, NOLOAD, 0x4000) is zeroed the same way.
     * norelax: 0x4000 is within 2 KB of gp for some images and not others,
     * and the linker's gp-relative relaxation of `la` truncated for one of
     * them (libctest_sim, R_RISCV_GPREL_I); an absolute pair is always right. */
2:  .option push
    .option norelax
    la      a0, _sfast
    la      a1, _efast
    .option pop
4:  bgeu    a0, a1, 2f
    sw      zero, 0(a0)
    addi    a0, a0, 4
    j       4b

    /* __libc_init_array runs .init_array: C++ static constructors, and every
     * __attribute__((constructor)) function.  fw/syscalls.c uses one to force
     * stdout unbuffered before main gets a chance to print, so this call is
     * load-bearing, not boilerplate -- and link.ld KEEPs .init_array for the
     * same reason, since --gc-sections sees no reference to a constructor and
     * would otherwise throw it away. */
2:  call    __libc_init_array

    li      a0, 0                   /* argc = 0    */
    li      a1, 0                   /* argv = NULL */
    call    main

    /* main returned.  Hand it to exit() rather than parking here: exit runs
     * the atexit/.fini_array handlers and flushes stdio, so a program whose
     * last printf is still in a buffer does not lose it.  _exit parks. */
    call    exit
3:  j       3b                      /* exit does not return; belt and braces */

/*------------------------------------------------------------------------------
 * trap_entry -- one entry for interrupts and exceptions alike.
 *
 * trap_dispatch(mcause, mepc, sp) RETURNS THE ADDRESS TO RESUME AT, and that is
 * the whole reason it takes mepc as an argument.  For an interrupt, mepc is the
 * instruction that was interrupted and resuming there is right.  For a SYNCHRONOUS
 * exception -- ecall, illegal instruction -- mepc is the address of the offending
 * instruction, so returning to it re-executes it and the trap repeats for ever.
 * That is a hang with no explanation, and it is the single easiest thing to get
 * wrong here, so the decision is made in C where it can be read.
 *----------------------------------------------------------------------------*/
    .balign 4
    .global trap_entry
trap_entry:
    addi    sp, sp, -64
    sw      ra,  0(sp)
    sw      a0,  4(sp)
    sw      a1,  8(sp)
    sw      a2, 12(sp)
    sw      a3, 16(sp)
    sw      a4, 20(sp)
    sw      a5, 24(sp)
    sw      a6, 28(sp)
    sw      a7, 32(sp)
    sw      t0, 36(sp)
    sw      t1, 40(sp)
    sw      t2, 44(sp)
    sw      t3, 48(sp)
    sw      t4, 52(sp)
    sw      t5, 56(sp)
    sw      t6, 60(sp)
    /* s0-s11 are callee-saved: the C handler preserves them itself. */

    csrr    a0, mcause
    csrr    a1, mepc
    mv      a2, sp
    call    trap_dispatch
    csrw    mepc, a0                /* where to resume, decided in C */

    lw      ra,  0(sp)
    lw      a0,  4(sp)
    lw      a1,  8(sp)
    lw      a2, 12(sp)
    lw      a3, 16(sp)
    lw      a4, 20(sp)
    lw      a5, 24(sp)
    lw      a6, 28(sp)
    lw      a7, 32(sp)
    lw      t0, 36(sp)
    lw      t1, 40(sp)
    lw      t2, 44(sp)
    lw      t3, 48(sp)
    lw      t4, 52(sp)
    lw      t5, 56(sp)
    lw      t6, 60(sp)
    addi    sp, sp, 64
    mret
