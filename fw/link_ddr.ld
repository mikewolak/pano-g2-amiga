/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/link_ddr.ld -- the LARGE-IMAGE script: code and data in LPDDR, stack in
 * block RAM, for programs that do not fit in 32 KB.
 *
 *   0x0000_0000 +----------------+
 *               | (unused)       |
 *               |       ^        |
 *               |       |        |
 *               | stack          |  16 KB of block RAM, all of it stack (32 KB until P4)
 *   0x0000_4000 +----------------+  _stack_top
 *
 *   0x1000_0000 +----------------+
 *               | .text .rodata  |
 *               | .data .bss     |
 *               +----------------+  _end = __heap_start
 *               | heap           |  the rest of the 32 MB
 *   0x1200_0000 +----------------+  __heap_end
 *
 * WHY THE STACK IS IN BLOCK RAM AND NOT AT THE TOP OF LPDDR.  The stack is the
 * hottest memory in the program and the smallest; block RAM answers in a cycle
 * and LPDDR was measured at a 310 ns random burst read (riscv_readme.md ss7).
 * Putting it on-chip costs nothing -- with .text in LPDDR the block RAM is
 * otherwise idle -- and it keeps every call, spill and trap frame off the DRAM
 * bus, where the instruction cache is already competing.
 *
 * EXECUTING FROM LPDDR IS VIABLE ON THIS CORE, and was not on the last one.
 * riscv_readme.md ss4 still says "do not execute from LPDDR"; that sentence was
 * written for PicoRV32, which has no cache and would have paid the full random
 * read latency on every instruction.  VexRiscv_Full has a 4 KB instruction
 * cache and fetches a line at a time, so straight-line code and loops are
 * served from the cache and the DRAM sees line fills, not fetches.  It will
 * still be slower than block RAM; it will not be 20 ns against 310 ns.
 *
 * THE LOAD PATH: an image linked here is loaded at 0x1000_0000 by the loader
 * (fw/amiga/amiga_ldr.c, the stick's \amiga\AMIGA.FW) and started there.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    fast (rwx) : ORIGIN = 0x00004000, LENGTH = 4K     /* rtl/soc_fastram.v, 1K x 32; see fw/fastram.h */
    ram (rw)   : ORIGIN = 0x00000000, LENGTH = 16K   /* all of it stack */
    /* the origin can be moved with -Wl,--defsym=__ddr_origin=0x11800000: a
     * program that sweeps the LPDDR (memcon_ddr) must not live where it sweeps */
    ddr (rwx)  : ORIGIN = DEFINED(__ddr_origin) ? __ddr_origin : 0x10000000,
                 LENGTH = 0x12000000 - (DEFINED(__ddr_origin) ? __ddr_origin : 0x10000000)
}

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
    } > ddr

    .preinit_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > ddr
    .init_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > ddr
    .fini_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > ddr

    .data : {
        . = ALIGN(4);
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > ddr

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > ddr


    _end = .;
    PROVIDE(end = _end);

    /* The stack is the whole of block RAM, and it is a separate region, so the
     * "image overflowed" check that link.ld needs is structural here. */
    _stack_top = ORIGIN(ram) + LENGTH(ram);
    /* THE FAST RAM.  NOLOAD: it is not part of the image (an image is one
     * contiguous run of bytes from its lowest address, and 0x4000 is 16 KB
     * from block RAM's zero and 256 MB from the LPDDR window), so start.S
     * zeroes it like .bss.  Put a variable here with FASTRAM() from
     * fw/fastram.h.  Uncached alias of the same bytes: 0x8000_8000. */
    .fastram (NOLOAD) : {
        . = ALIGN(4);
        _sfast = .;
        *(.fastram .fastram.*)
        . = ALIGN(4);
        _efast = .;
    } > fast


    /* Heap = everything after .bss, page-aligned so the first allocation is not
     * straddling a DRAM row boundary for no reason. */
    PROVIDE(__heap_start = ALIGN(_end, 4K));
    PROVIDE(__heap_end   = ORIGIN(ddr) + LENGTH(ddr));

    ASSERT(__heap_start < __heap_end, "image fills LPDDR: no heap left")

    /DISCARD/ : { *(.comment) *(.note.*) *(.riscv.attributes) }
}
