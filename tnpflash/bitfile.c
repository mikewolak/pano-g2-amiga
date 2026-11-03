/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/bitfile.c -- see bitfile.h.  The .bit container: a 13-byte preamble (00 09 0f f0 0f f0 0f f0 0f f0 00 00 01),
 * then tagged fields 'a' design, 'b' part, 'c' date, 'd' time (each: tag, 16-bit BE length, bytes) and 'e' + 32-bit BE length +
 * the configuration data, which is exactly what bitgen's .bin holds and what the FPGA reads from the flash at power-up. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bitfile.h"

static const uint8_t PRE[13] = { 0x00,0x09,0x0f,0xf0,0x0f,0xf0,0x0f,0xf0,0x0f,0xf0,0x00,0x00,0x01 };

int bitfile_load(const char *path, bitfile_t *o, long *sync_off, char *err, size_t en)
{
    FILE *f = fopen(path, "rb"); long n; uint8_t *b; size_t p;
    memset(o, 0, sizeof *o); if (sync_off) *sync_off = -1;
    if (!f) { snprintf(err, en, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || !(b = (uint8_t *)malloc((size_t)n))) { fclose(f); snprintf(err, en, "%s is empty or too large", path); return -1; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); snprintf(err, en, "short read on %s", path); return -1; }
    fclose(f);
    if (n > 13 && !memcmp(b, PRE, 13)) {
        p = 13;
        for (;;) {
            uint8_t tag; size_t len; char *dst = NULL; size_t cap = 0;
            if (p + 3 > (size_t)n) { free(b); snprintf(err, en, "%s: truncated .bit header", path); return -1; }
            tag = b[p++];
            if (tag == 'e') {
                if (p + 4 > (size_t)n) { free(b); snprintf(err, en, "%s: truncated .bit header", path); return -1; }
                len = ((size_t)b[p] << 24) | ((size_t)b[p+1] << 16) | ((size_t)b[p+2] << 8) | b[p+3]; p += 4;
                if (p + len > (size_t)n) { free(b); snprintf(err, en, "%s: .bit says %lu data bytes, file has %lu", path, (unsigned long)len, (unsigned long)((size_t)n - p)); return -1; }
                o->data = (uint8_t *)malloc(len); if (!o->data) { free(b); snprintf(err, en, "out of memory"); return -1; }
                memcpy(o->data, b + p, len); o->len = len; o->was_bit = 1; free(b);
                break;
            }
            len = ((size_t)b[p] << 8) | b[p+1]; p += 2;
            if (p + len > (size_t)n) { free(b); snprintf(err, en, "%s: truncated .bit field '%c'", path, tag); return -1; }
            if (tag == 'a') { dst = o->design; cap = sizeof o->design; } else if (tag == 'b') { dst = o->part; cap = sizeof o->part; }
            else if (tag == 'c') { dst = o->date; cap = sizeof o->date; } else if (tag == 'd') { dst = o->time; cap = sizeof o->time; }
            if (dst) { size_t c = len < cap - 1 ? len : cap - 1; memcpy(dst, b + p, c); dst[c] = 0; }
            p += len;
        }
    } else { o->data = b; o->len = (size_t)n; }
    if (sync_off) { size_t i; for (i = 0; i + 4 <= o->len && i < 4096; i++) if (o->data[i] == 0xAA && o->data[i+1] == 0x99 && o->data[i+2] == 0x55 && o->data[i+3] == 0x66) { *sync_off = (long)i; break; } }
    return 0;
}

void bitfile_free(bitfile_t *b) { free(b->data); b->data = NULL; b->len = 0; }
