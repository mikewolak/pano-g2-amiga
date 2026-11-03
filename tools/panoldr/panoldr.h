/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tools/panoldr/panoldr.h -- the flash payload header: what sits after the bitstream, and what the
 * first-stage boot code reads to find and trust it.
 *
 * The payload is written to the flash right after the FPGA bitstream; a write erases only the sectors covering its
 * span, so a payload cannot disturb the bitstream below it.  This header lets the code that reads it back find it,
 * check it and start it.
 *
 * A RAW BLOB IS NOT ENOUGH, and the reasons are all failure modes rather than tidiness:
 *   - The first stage has to know HOW MANY BYTES to copy.  Erased flash reads as 0xFF for ever; without a
 *     length it would either copy the whole 256 KB region every boot or guess.
 *   - It has to know the payload is THERE AT ALL.  A board whose loader sector was never written, or was
 *     half written when the cable dropped, must fail loudly rather than jump into 0xFFFFFFFF.
 *   - It has to know the payload is WHOLE.  The flash is the one part of this system nobody can watch while
 *     it fails: a bit that rots in a sector shows up as a boot that dies somewhere random, weeks later.  A
 *     CRC32 over the payload turns that into one line at power-on.
 *   - Load and entry are SEPARATE because they are not always the same.  A compressed or headered image
 *     wants to land in one place and start in another, and hard-coding them into the first stage would mean
 *     an RTL rebuild -- 40 minutes and a reflash of the bitstream -- every time the loader moved.
 *
 * Everything is little-endian, which is the CPU's byte order, so the first stage reads fields with plain
 * loads and no shuffling.
 */
#ifndef PANOLDR_H
#define PANOLDR_H

#define PANOLDR_MAGIC0   0x4F4E4150u      /* "PANO" */
#define PANOLDR_MAGIC1   0x2152444Cu      /* "LDR!" */
#define PANOLDR_VERSION  1u

/* Where it lives in the M25P80.  0x0C0000 is the first 64 KB sector clear of a 723,685-byte bitstream
 * (twelve sectors), leaving 256 KB -- room for the 77 KB loader several times over. */
#define PANOLDR_FLASH_OFF 0x000C0000u
#define PANOLDR_FLASH_MAX 0x00040000u

#define PANOLDR_HDR_BYTES 32u

typedef struct {
    unsigned int magic0;     /* PANOLDR_MAGIC0 */
    unsigned int magic1;     /* PANOLDR_MAGIC1 */
    unsigned int version;    /* PANOLDR_VERSION */
    unsigned int length;     /* payload bytes following this header */
    unsigned int load;       /* address the payload is copied to */
    unsigned int entry;      /* address to jump to once it is there */
    unsigned int crc32;      /* pkzip CRC32 of the payload, NOT including this header */
    unsigned int hdr_crc32;  /* pkzip CRC32 of the first 28 bytes of this header */
} panoldr_hdr_t;

#endif
