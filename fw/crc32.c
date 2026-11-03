/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/crc32.c -- see crc32.h.  The table (1 KB, .bss) is built on first use from the bit-serial definition. */
#include "crc32.h"

static uint32_t s_t[256];
static int s_init;

uint32_t crc32_run(uint32_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!s_init) {
        for (i = 0; i < 256u; i++) {
            uint32_t v = i; int k;
            for (k = 0; k < 8; k++) v = (v >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(v & 1u)));
            s_t[i] = v;
        }
        s_init = 1;
    }
    for (i = 0; i < n; i++) crc = s_t[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

/* SLICING BY FOUR: whole little-endian words, one load and four table lookups per four bytes.  For data that is cheap
 * to load a word at a time and costly a byte at a time (fast RAM, uncached) -- NETSH's bulk CRC pass spent 23 cycles a
 * byte in the routine above.  Tables 1..3 are derived from table 0, so the two routines cannot disagree. */
static uint32_t s_t4[3][256];
static int s_init4;

uint32_t crc32_run_words(uint32_t crc, const volatile uint32_t *w, uint32_t nwords)
{
    uint32_t i;
    if (!s_init4) {
        (void)crc32_run(0, (const uint8_t *)"", 0);                /* builds table 0 */
        for (i = 0; i < 256u; i++) {
            s_t4[0][i] = (s_t[i] >> 8) ^ s_t[s_t[i] & 0xFFu];
            s_t4[1][i] = (s_t4[0][i] >> 8) ^ s_t[s_t4[0][i] & 0xFFu];
            s_t4[2][i] = (s_t4[1][i] >> 8) ^ s_t[s_t4[1][i] & 0xFFu];
        }
        s_init4 = 1;
    }
    for (i = 0; i < nwords; i++) {
        uint32_t v = crc ^ w[i];
        crc = s_t4[2][v & 0xFFu] ^ s_t4[1][(v >> 8) & 0xFFu] ^ s_t4[0][(v >> 16) & 0xFFu] ^ s_t[v >> 24];
    }
    return crc;
}

