/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/snes/link_snes_ldr.ld -- the G2 subsystem-slot fabric's block-RAM layout (fw/link_g2.ld with 32 KB): everything in the
 * 32 KB of block RAM at 0x0000_0000 (rtl/g2/pano_g2_top.v SOC_RAM_AW 13).
 *
 *   0x0000_0000  .text .rodata .data .bss, then the heap, then the stack (8 KB) at the top
 *   0x0000_8000  end of block RAM
 *
 * The firmware the loader starts runs from DDR2 chip A (fw/link_ddr.ld) with its stack in the lower 16 KB of
 * this block RAM, over the loader, which is finished by then.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ram (rwx)  : ORIGIN = 0x00000000, LENGTH = 32K
}

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
    } > ram

    /* newlib registers atexit handlers and (for C++ and __attribute__((constructor)))
     * runs .init_array from __libc_init_array.  Both must be KEEP: --gc-sections
     * sees no reference to a constructor and would drop it. */
    .preinit_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > ram
    .init_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > ram
    .fini_array : {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > ram

    .data : {
        . = ALIGN(4);
        __global_pointer$ = . + 0x800;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > ram

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > ram


    _end = .;
    PROVIDE(end = _end);
    _stack_top = ORIGIN(ram) + LENGTH(ram);
    .fastram (NOLOAD) : {
        . = ALIGN(4);
        _sfast = .;
        *(.fastram .fastram.*)
        . = ALIGN(4);
        _efast = .;
    } > ram


    PROVIDE(__heap_start = _end);
    PROVIDE(__heap_end   = ORIGIN(ram) + LENGTH(ram) - 8K);

    ASSERT(_end <= ORIGIN(ram) + LENGTH(ram) - 8K, "image leaves under 8 KB for the stack")

    /DISCARD/ : { *(.comment) *(.note.*) *(.riscv.attributes) }
}
