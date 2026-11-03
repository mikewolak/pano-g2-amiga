/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* crc32.c -- see crc32.h.  The table is built as fw/crc32.c builds it (bit-serial definition, then the three tables
 * of slicing by four derived from it).  Two loops in 68020 assembler:
 *
 *   crc32_bytes   one byte a step: crc = T[(crc ^ b) & 0xFF] ^ (crc >> 8)
 *   crc32_words   four bytes a step from a big-endian longword v.  The reflected CRC wants the bytes as a
 *                 little-endian word; instead of swapping v, the loop keeps the CRC byte-swapped (c' = swap(crc)) and
 *                 uses tables of byte-swapped entries: y = c' ^ v, c' = C'[y >> 24] ^ B'[(y >> 16) & 0xFF] ^
 *                 A'[(y >> 8) & 0xFF] ^ T'[y & 0xFF] (T, A, B, C as fw/crc32.c's s_t, s_t4[0], s_t4[1], s_t4[2]).
 *                 Sixteen instructions for four bytes; the CRC is swapped once before and once after.
 *
 * crc32_run takes single bytes up to a longword boundary, the longwords, then the remaining bytes.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include "crc32.h"

static ULONG s_t[256];                                     /* T */
static ULONG s_sw[4][256];                                 /* T', A', B', C': byte-swapped */
static int s_init;

static ULONG swap32(ULONG v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }

static void crc32_init(void)
{
    static ULONG t4[3][256];
    ULONG i;
    for (i = 0; i < 256u; i++) {
        ULONG v = i; int k;
        for (k = 0; k < 8; k++) v = (v >> 1) ^ (0xEDB88320u & (ULONG)(-(LONG)(v & 1u)));
        s_t[i] = v;
    }
    for (i = 0; i < 256u; i++) {
        t4[0][i] = (s_t[i] >> 8) ^ s_t[s_t[i] & 0xFFu];
        t4[1][i] = (t4[0][i] >> 8) ^ s_t[t4[0][i] & 0xFFu];
        t4[2][i] = (t4[1][i] >> 8) ^ s_t[t4[1][i] & 0xFFu];
    }
    for (i = 0; i < 256u; i++) {
        s_sw[0][i] = swap32(s_t[i]); s_sw[1][i] = swap32(t4[0][i]);
        s_sw[2][i] = swap32(t4[1][i]); s_sw[3][i] = swap32(t4[2][i]);
    }
    s_init = 1;
}

/* ULONG crc32_bytes(ULONG crc, const UBYTE *p, ULONG n, const ULONG *t): n >= 1 */
ULONG crc32_bytes(ULONG crc, const UBYTE *p, ULONG n, const ULONG *t);
__asm__(".text\n\t.even\n"
        "_crc32_bytes:\n\t"
        "movem.l d2-d3,-(sp)\n\t"
        "move.l 12(sp),d0\n\t"                             /* crc */
        "move.l 16(sp),a0\n\t"                             /* p */
        "move.l 20(sp),d3\n\t"                             /* n */
        "move.l 24(sp),a1\n\t"                             /* T */
        "moveq #0,d1\n"                                    /* bits 8-31 of the index stay 0 */
        "1:\tmove.b (a0)+,d1\n\t"
        "eor.b d0,d1\n\t"
        "lsr.l #8,d0\n\t"
        "move.l (a1,d1.l*4),d2\n\t"
        "eor.l d2,d0\n\t"
        "subq.l #1,d3\n\t"
        "bne.s 1b\n\t"
        "movem.l (sp)+,d2-d3\n\t"
        "rts\n");

/* ULONG crc32_words(ULONG c_swapped, const ULONG *w, ULONG n, const ULONG *sw): n >= 1, w longword-aligned */
ULONG crc32_words(ULONG c, const ULONG *w, ULONG n, const ULONG *sw);
__asm__(".text\n\t.even\n"
        "_crc32_words:\n\t"
        "movem.l d2-d4/a2-a4,-(sp)\n\t"
        "move.l 28(sp),d0\n\t"                             /* c' */
        "move.l 32(sp),a0\n\t"                             /* w */
        "move.l 36(sp),d4\n\t"                             /* n */
        "move.l 40(sp),a4\n\t"                             /* T' */
        "lea 1024(a4),a3\n\t"                              /* A' */
        "lea 2048(a4),a2\n\t"                              /* B' */
        "lea 3072(a4),a1\n\t"                              /* C' */
        "moveq #0,d2\n"
        "1:\tmove.l (a0)+,d1\n\t"
        "eor.l d0,d1\n\t"                                  /* y */
        "move.b d1,d2\n\t"
        "move.l (a4,d2.l*4),d0\n\t"                        /* T'[y & 0xFF] */
        "lsr.l #8,d1\n\t"
        "move.b d1,d2\n\t"
        "move.l (a3,d2.l*4),d3\n\t"                        /* A'[(y >> 8) & 0xFF] */
        "eor.l d3,d0\n\t"
        "lsr.l #8,d1\n\t"
        "move.b d1,d2\n\t"
        "move.l (a2,d2.l*4),d3\n\t"                        /* B'[(y >> 16) & 0xFF] */
        "eor.l d3,d0\n\t"
        "lsr.l #8,d1\n\t"
        "move.l (a1,d1.l*4),d3\n\t"                        /* C'[y >> 24] */
        "eor.l d3,d0\n\t"
        "subq.l #1,d4\n\t"
        "bne.s 1b\n\t"
        "movem.l (sp)+,d2-d4/a2-a4\n\t"
        "rts\n");

ULONG crc32_run(ULONG crc, const UBYTE *p, ULONG n)
{
    ULONG k;
    if (!s_init) crc32_init();
    k = (4u - ((ULONG)p & 3u)) & 3u;
    if (k > n) k = n;
    if (k) { crc = crc32_bytes(crc, p, k, s_t); p += k; n -= k; }
    if (n >= 4u) {
        k = n >> 2;
        crc = swap32(crc32_words(swap32(crc), (const ULONG *)p, k, &s_sw[0][0]));
        p += k << 2; n &= 3u;
    }
    if (n) crc = crc32_bytes(crc, p, n, s_t);
    return crc;
}

ULONG crc32(const UBYTE *p, ULONG n) { return crc32_run(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu; }
