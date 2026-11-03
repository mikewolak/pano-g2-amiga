/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/crc32.h -- the reflected CRC-32 (0xEDB88320, the Ethernet/zlib one), a table at a time: ~8 cycles a byte from
 * cached memory.  One implementation for bootsh's recv/send/flash and netsh's ring, so they cannot disagree. */
#ifndef CRC32_H
#define CRC32_H
#include <stdint.h>
/* running form: start with 0xFFFFFFFF, finish with ^ 0xFFFFFFFF */
uint32_t crc32_run(uint32_t crc, const uint8_t *p, uint32_t n);
/* the same CRC over whole little-endian words (slicing by four): for memory that is cheap a word at a time */
uint32_t crc32_run_words(uint32_t crc, const volatile uint32_t *w, uint32_t nwords);
#endif
