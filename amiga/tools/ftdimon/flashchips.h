/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* flashchips.h -- the SPI NOR parts known to ftdimon: the table fc_parts[] that flashchips_gen.py writes from flashrom
 * 1.4.0's flashchips.c (amiga/third_party/flashrom, GPL-2.0-or-later), and its lookup.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef FLASHCHIPS_H
#define FLASHCHIPS_H

enum { P_RDID, P_RDID4, P_REMS, P_RES1, P_RES2 };     /* how the part is identified: 0x9F (3 or 4 bytes), 0x90, 0xAB */
enum { W_PAGE, W_BYTE, W_AAI };                       /* page program 0x02, byte program 0x02, SST AAI 0xAD */

typedef struct { unsigned long size, count; } fc_region_t;
typedef struct { unsigned char op; fc_region_t r[4]; } fc_eraser_t;   /* an erase opcode and its block layout */
typedef struct {
    const char    *vendor, *name;
    unsigned short mfg, model;           /* manufacturer ID; model ID (two bytes of 0x9F, one of 0x90/0xAB) */
    unsigned long  size_kb;              /* total size in KB */
    unsigned short page;                 /* program page in bytes */
    unsigned char  probe, write, addr4;  /* addr4: 4-byte addressing (flashrom FEATURE_4BA*) */
    unsigned short vmin, vmax;           /* supply range, mV */
    unsigned char  nerase;
    fc_eraser_t    er[5];
} fc_part_t;

extern const fc_part_t fc_parts[];
extern const int fc_nparts;

/* the parts with this JEDEC ID (0x9F: manufacturer, then model high and low byte); n found, at most max in out */
int fc_lookup(unsigned int mfg, unsigned int model, const fc_part_t **out, int max);
#endif
