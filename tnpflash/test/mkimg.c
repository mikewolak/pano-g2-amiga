/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/test/mkimg.c -- test images for `make test': an 8 MB G2 revision C flash as the stock image lays it out
 * (multiboot header at 0 whose GENERAL2 word loads 0x380000, sync words at 0x040000 and 0x380000, a configuration table
 * at 0x6C0000 with a MAC tag pair) and a main-slot image of the given size that starts with a sync word.  The contents
 * are a fixed pattern, not a configuration.  Mike Wolak, EPROM Foundry Inc.
 *   mkimg flash <out>            the 8 MB flash
 *   mkimg slot <out> <bytes>     a main-slot image */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char a[0x800000];
static void put32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static const unsigned char sync[4] = { 0xAA, 0x99, 0x55, 0x66 };
int main(int argc, char **argv)
{
    FILE *f; unsigned long n, i;
    if (argc == 3 && !strcmp(argv[1], "flash")) {
        memset(a, 0xFF, sizeof a);
        memcpy(a + 4, sync, 4); a[0x10] = 0x32; a[0x11] = 0x81; a[0x12] = 0x03; a[0x13] = 0x38;
        for (i = 0x40000; i < 0x6C0000; i++) a[i] = (unsigned char)(i * 13u + (i >> 9));
        memcpy(a + 0x40000 + 16, sync, 4); memcpy(a + 0x380000 + 16, sync, 4);
        put32(a + 0x6C0000, 0x10000010ul); put32(a + 0x6C0004, 0x0000001Cul);
        put32(a + 0x6C0008, 0x10000011ul); put32(a + 0x6C000C, 0x02A1B2C3ul);
        n = sizeof a;
    } else if (argc == 4 && !strcmp(argv[1], "slot")) {
        n = strtoul(argv[3], NULL, 0);
        if (n < 64 || n > sizeof a) { fprintf(stderr, "mkimg: bad size\n"); return 2; }
        for (i = 0; i < n; i++) a[i] = (unsigned char)(i * 7u + 3u);
        memset(a, 0xFF, 16); memcpy(a + 16, sync, 4);
    } else { fprintf(stderr, "usage: mkimg flash <out> | mkimg slot <out> <bytes>\n"); return 2; }
    if (!(f = fopen(argv[2], "wb")) || fwrite(a, 1, n, f) != n) { fprintf(stderr, "mkimg: cannot write %s\n", argv[2]); return 1; }
    fclose(f);
    return 0;
}
