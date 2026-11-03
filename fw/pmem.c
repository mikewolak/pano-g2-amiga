/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/pmem.c -- memcpy, memset and memmove for THIS board, not for a PC.
 *
 * The C library's size-optimised routines are byte loops --
 *
 *     memcpy   9 instructions:  lb / sb / addi / bnez
 *     memset   7 instructions:  sb / addi / bnez
 *     memmove 15 instructions:  sb / addi / bnez
 *
 * On a PC that is merely unfashionable.  Here it is a disaster, for one measured reason:
 *
 *     A STORE TO THE LPDDR COSTS ~31 CYCLES WHATEVER ITS WIDTH.
 *
 * There is no byte mask on this board (MIG_DM_STATIC_LOW is a pinout constraint, not a choice), so
 * every write -- one byte or four -- is a burst READ, a merge and a burst WRITE.  Measured on
 * silicon: 32-bit stores 6.7 MB/s, byte stores 1.6 MB/s.  Same ~31 cycles each; four times the data
 * for the same price.  So a byte-at-a-time memcpy of N bytes costs 4x what it needs to, and a
 * byte-at-a-time memset costs 4x.
 *
 * ---- THE RULE THIS FILE IS WRITTEN TO ------------------------------------------------------------
 * LOADS ARE CHEAP AND STORES ARE NOT.  A load hits the 16 KB data cache or costs one burst read; a
 * store always costs the full read-modify-write.  So the shape here is not the usual "align both
 * pointers and bail out to bytes if they disagree" -- that is a PC's trade, where the two cost the
 * same.  Here we ALWAYS align the DESTINATION and always store whole words, and when the source is
 * misaligned relative to it we pay two cheap loads and a shift to assemble each word rather than
 * give up and store bytes.  Misaligned copies are common (arrays sliced at arbitrary offsets), so
 * this is the case that had to be fast, not the one to fall back on.
 *
 * Correctness is tested: a test program walks every length 0..64 at every source and
 * destination alignment 0..3 against a byte-exact reference, plus the overlap cases for memmove.
 */
#include <stddef.h>
#include <stdint.h>

/* picolibc declares these; we define them, and the linker prefers an object over an archive member. */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    uint32_t w = (uint32_t)(unsigned char)c;
    w |= w << 8; w |= w << 16;

    if (n >= 16u) {
        while (((uintptr_t)d & 3u) != 0u) { *d++ = (unsigned char)c; n--; }
        /* four words a turn: the loop overhead stops mattering next to the stores */
        while (n >= 16u) {
            ((uint32_t *)d)[0] = w; ((uint32_t *)d)[1] = w;
            ((uint32_t *)d)[2] = w; ((uint32_t *)d)[3] = w;
            d += 16; n -= 16u;
        }
        while (n >= 4u) { *(uint32_t *)d = w; d += 4; n -= 4u; }
    }
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (n >= 16u) {
        while (((uintptr_t)d & 3u) != 0u) { *d++ = *s++; n--; }

        if ((((uintptr_t)s) & 3u) == 0u) {
            /* both aligned: the easy half */
            while (n >= 16u) {
                uint32_t a = ((const uint32_t *)s)[0], b = ((const uint32_t *)s)[1];
                uint32_t c = ((const uint32_t *)s)[2], e = ((const uint32_t *)s)[3];
                ((uint32_t *)d)[0] = a; ((uint32_t *)d)[1] = b;
                ((uint32_t *)d)[2] = c; ((uint32_t *)d)[3] = e;
                d += 16; s += 16; n -= 16u;
            }
            while (n >= 4u) { *(uint32_t *)d = *(const uint32_t *)s; d += 4; s += 4; n -= 4u; }
        } else {
            /* SOURCE MISALIGNED, AND WE STILL STORE WORDS.  Two aligned loads and a shift per word
             * is far cheaper than four stores: loads are cached, stores are 31 cycles each.  This is
             * the case a PC implementation gives up on. */
            const uint32_t *sp = (const uint32_t *)((uintptr_t)s & ~(uintptr_t)3);
            unsigned int    sh = (unsigned int)(((uintptr_t)s & 3u) * 8u);
            unsigned int    ns = 32u - sh;
            uint32_t        lo = *sp++;
            while (n >= 4u) {
                uint32_t hi = *sp++;
                *(uint32_t *)d = (lo >> sh) | (hi << ns);
                lo = hi;
                d += 4; s += 4; n -= 4u;
            }
        }
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0u) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);   /* no overlap in the forward direction */

    /* Overlapping and moving up: copy backwards.  This case is rare, so it stays a byte loop --
     * a fast path nobody exercises is a fast path nobody has tested. */
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}
