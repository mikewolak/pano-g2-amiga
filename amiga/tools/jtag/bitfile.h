/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* bitfile.h -- Xilinx .bit container: header fields a..d (design, part, date,
 * time) and the 'e' section holding the raw configuration bytes. */
#ifndef BITFILE_H
#define BITFILE_H
#include <stddef.h>
#include <stdint.h>
typedef struct bitfile { char design[256], part[64], date[32], time[32]; const uint8_t *cfg; size_t cfg_len; uint8_t *blob; } bitfile_t;
int  bitfile_load(bitfile_t *b, const char *path);       /* 0 ok; keeps the file in b->blob */
int  bitfile_parse(bitfile_t *b, const uint8_t *p, size_t n);   /* same, over bytes already in memory (not owned) */
void bitfile_free(bitfile_t *b);
#endif
