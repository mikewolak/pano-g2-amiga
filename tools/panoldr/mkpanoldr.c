/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* mkpanoldr.c -- wrap a firmware .bin in the flash payload header, ready for
 *     panoflash --headless --flash-write loader.img --addr 0xC0000
 *
 * See tools/panoldr/panoldr.h for the header and why a raw blob will not do.  This is the host half; the
 * first-stage boot code reads the same struct back out of flash, checks both CRCs, copies `length' bytes to
 * `load' and jumps to `entry'.
 *
 * It also REFUSES to build an image that would not fit the region, because the alternative is finding out
 * when a flash write runs off the end of the device or over something else.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "panoldr.h"

static unsigned int crc32_buf(const unsigned char *p, unsigned int n)
{
    unsigned int crc = 0xFFFFFFFFu, i;
    int k;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (unsigned int)(-(int)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void usage(const char *me)
{
    fprintf(stderr,
        "usage: %s -i IN.bin -o OUT.img [-l LOAD_HEX] [-e ENTRY_HEX] [-m MAX_HEX]\n"
        "  -i  the firmware image to wrap\n"
        "  -o  the flash payload to write with panoflash --flash-write --addr 0x%X\n"
        "  -l  address the first stage copies the payload to   (default 0x11C00000)\n"
        "  -e  address it jumps to afterwards                  (default: the load address)\n"
        "  -m  region size to check against                    (default 0x%X)\n",
        me, PANOLDR_FLASH_OFF, PANOLDR_FLASH_MAX);
}

int main(int argc, char **argv)
{
    const char *in = 0, *out = 0;
    unsigned long load = 0x11C00000ul, entry = 0ul, max = PANOLDR_FLASH_MAX;
    int c, have_entry = 0;
    FILE *f;
    unsigned char *buf, hdr[PANOLDR_HDR_BYTES];
    long n;

    while ((c = getopt(argc, argv, "i:o:l:e:m:h")) != -1) {
        switch (c) {
            case 'i': in = optarg; break;
            case 'o': out = optarg; break;
            case 'l': load = strtoul(optarg, 0, 16); break;
            case 'e': entry = strtoul(optarg, 0, 16); have_entry = 1; break;
            case 'm': max = strtoul(optarg, 0, 16); break;
            default: usage(argv[0]); return 2;
        }
    }
    if (!in || !out) { usage(argv[0]); return 2; }
    if (!have_entry) entry = load;

    f = fopen(in, "rb");
    if (!f) { perror(in); return 1; }
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "%s: not a readable file\n", in); fclose(f); return 1;
    }
    buf = malloc((size_t)n);
    if (!buf) { fprintf(stderr, "out of memory\n"); fclose(f); return 1; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "%s: short read\n", in); fclose(f); free(buf); return 1; }
    fclose(f);

    if ((unsigned long)n + PANOLDR_HDR_BYTES > max) {
        fprintf(stderr, "%s: %ld bytes + %u header = %lu, which will not fit the %lu-byte region.\n",
                in, n, PANOLDR_HDR_BYTES, (unsigned long)n + PANOLDR_HDR_BYTES, max);
        fprintf(stderr, "  Either the region moved or the loader has outgrown it -- do not just raise -m,\n"
                        "  check what else is in the flash first.\n");
        free(buf); return 1;
    }

    memset(hdr, 0, sizeof hdr);
    put32(hdr +  0, PANOLDR_MAGIC0);
    put32(hdr +  4, PANOLDR_MAGIC1);
    put32(hdr +  8, PANOLDR_VERSION);
    put32(hdr + 12, (unsigned int)n);
    put32(hdr + 16, (unsigned int)load);
    put32(hdr + 20, (unsigned int)entry);
    put32(hdr + 24, crc32_buf(buf, (unsigned int)n));
    put32(hdr + 28, crc32_buf(hdr, 28u));            /* the header checks itself, so a half-written
                                                      * sector cannot present a plausible length */

    f = fopen(out, "wb");
    if (!f) { perror(out); free(buf); return 1; }
    if (fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr ||
        fwrite(buf, 1, (size_t)n, f) != (size_t)n) { perror(out); fclose(f); free(buf); return 1; }
    if (fclose(f)) { perror(out); free(buf); return 1; }

    printf("%s: %ld bytes payload, CRC32 %08X\n", in, n, crc32_buf(buf, (unsigned int)n));
    printf("%s: %lu bytes total, load 0x%08lX entry 0x%08lX\n", out, (unsigned long)n + PANOLDR_HDR_BYTES, load, entry);
    printf("write it with:  panoflash --headless --flash-write %s --addr 0x%X\n", out, PANOLDR_FLASH_OFF);
    free(buf);
    return 0;
}
