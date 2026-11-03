/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
#include "bitfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rd_field(const uint8_t *p, size_t n, size_t *pos, char key, char *out, size_t outsz)
{
    if (*pos + 3 > n || p[*pos] != (uint8_t)key) return -1;
    size_t len = (size_t)p[*pos + 1] << 8 | p[*pos + 2];
    if (*pos + 3 + len > n) return -1;
    size_t c = len < outsz - 1 ? len : outsz - 1;
    memcpy(out, p + *pos + 3, c); out[c] = 0;
    *pos += 3 + len;
    return 0;
}
int bitfile_load(bitfile_t *b, const char *path)
{
    memset(b, 0, sizeof *b);
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    b->blob = malloc((size_t)sz); if (!b->blob) { fclose(f); return -1; }
    if (fread(b->blob, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b->blob); b->blob = 0; return -1; }
    fclose(f);
    return bitfile_parse(b, b->blob, (size_t)sz);
}
int bitfile_parse(bitfile_t *b, const uint8_t *p, size_t n)
{
    size_t pos = 0;
    uint8_t *keep = b->blob; memset(b, 0, sizeof *b); b->blob = keep;
    /* magic: 00 09 0f f0 0f f0 0f f0 0f f0 00 00 01 */
    static const uint8_t magic[13] = {0x00,0x09,0x0f,0xf0,0x0f,0xf0,0x0f,0xf0,0x0f,0xf0,0x00,0x00,0x01};
    if (n < 13 || memcmp(p, magic, 13)) return -2;
    pos = 13;
    if (rd_field(p, n, &pos, 'a', b->design, sizeof b->design)) return -3;
    if (rd_field(p, n, &pos, 'b', b->part, sizeof b->part)) return -3;
    if (rd_field(p, n, &pos, 'c', b->date, sizeof b->date)) return -3;
    if (rd_field(p, n, &pos, 'd', b->time, sizeof b->time)) return -3;
    if (pos + 5 > n || p[pos] != 'e') return -4;
    size_t len = (size_t)p[pos+1] << 24 | (size_t)p[pos+2] << 16 | (size_t)p[pos+3] << 8 | p[pos+4];
    pos += 5;
    if (pos + len > n) return -5;
    b->cfg = p + pos; b->cfg_len = len;
    return 0;
}
void bitfile_free(bitfile_t *b) { free(b->blob); b->blob = 0; b->cfg = 0; }
