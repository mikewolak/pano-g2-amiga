/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/bitfile.h -- take a Xilinx .bit or a raw .bin and hand back the configuration bytes that go into the SPI flash. */
#ifndef BITFILE_H
#define BITFILE_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t *data; size_t len; int was_bit; char design[96], part[32], date[16], time[16]; } bitfile_t;
/* 0 = ok.  A .bit is recognised by its header; anything else is taken as raw bytes.  `sync_off' receives the offset of the
 * Xilinx sync word AA 99 55 66 in the returned data, or -1 if there is none (then it is not a bitstream at all). */
int  bitfile_load(const char *path, bitfile_t *out, long *sync_off, char *err, size_t errlen);
void bitfile_free(bitfile_t *b);
#endif
