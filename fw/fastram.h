/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/fastram.h -- the 1K x 32 fast RAM at 0x0000_4000 (rtl/soc_fastram.v).
 *
 *   FASTRAM(unsigned char rowbuf[320]);      one attribute puts a variable there
 *   FASTRAM(struct render_state rs);
 *
 * Zeroed at start like .bss; 4 KB in all; the same bytes are visible uncached
 * at 0x8000_8000 (FASTRAM_UNCACHED(p) turns a pointer into that view), which
 * is the view to use for a buffer that is written once and then handed to
 * the DMA, so the writes never allocate cache lines.  A byte store here is
 * one bus cycle in either view; in the LPDDR it is a read-modify-write of
 * an 8-byte burst.
 */
#ifndef FASTRAM_H
#define FASTRAM_H
#define FASTRAM_BASE      0x00004000u
#define FASTRAM_SIZE      0x1000u
#define FASTRAM_UNCACHED_BASE 0x80008000u
#define FASTRAM(decl)     __attribute__((section(".fastram"))) decl
/* ONE VIEW PER CACHE LINE.  A line read through the cached view stays in the
 * data cache; a later write to those bytes through the uncached view updates
 * the RAM and not the cache, and there is no flush instruction in this
 * build.  So a buffer that is touched through BOTH views must own whole
 * cache lines: 32-byte aligned and a multiple of 32 bytes, so no line holds
 * bytes of two buffers.  FASTRAM_LINES() does that; a buffer used through
 * one view only needs nothing. */
#define FASTRAM_LINE      32u
#define FASTRAM_LINES(decl) __attribute__((section(".fastram"), aligned(32))) decl
#define FASTRAM_UNCACHED(p) ((void *)((unsigned int)(p) - FASTRAM_BASE + FASTRAM_UNCACHED_BASE))
#endif
