/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/flash.h -- the stock image's SPI-flash controller, as Pano Logic's host tool drives it.
 *   0x5001  address latch: 0x80000000 | WORD address (a 64 KB sector is 0x4000 words)
 *   0x5000  data: a read returns the word at the latched address; a write programs it (only while 0xFC = 1)
 *   0x5002  writing 0 erases the 64 KB sector holding the latched address (Pano's tool waits 3 s)
 *   0xFC    program enable: 1 before writing, 0 after
 * A flash word is the four file bytes b0 b1 b2 b3 read big-endian: the bytes land in the flash in file order.
 * WORDS >= 0x38000 (bytes >= 0xE0000) HOLD THE MAC ADDRESS AND THE SPI LOCK: never erased, never written by this tool. */
#ifndef FLASH_H
#define FLASH_H
#include "stream.h"
#define FLASH_BYTES     0x100000u
#define FLASH_WORDS     (FLASH_BYTES / 4u)
#define SECTOR_WORDS    0x4000u
#define PROTECT_WORD    0x38000u          /* G1: first word the tool refuses to erase or write */
/* G2 rev C flash map (Pano's Series 2 updater): multiboot header at byte 0, golden image 0x040000..0x37FFFF, main (multiboot)
 * image 0x380000..0x6BFFFF, configuration table with the MAC at 0x6C0000.  `write' refuses a G2; `g2write' writes the main image only. */
#define G2C_GOLDEN      0x040000u
#define G2C_MAIN        0x380000u
#define G2C_CONFIG      0x6C0000u
#define REG_SPI_DATA    0x5000u
#define REG_SPI_ADDR    0x5001u
#define REG_SPI_ERASE   0x5002u
#define REG_PROG_EN     0xFCu

typedef void (*progress_fn)(uint32_t done, uint32_t total, const char *what);
/* set from another thread to abandon a long read or write; the call returns -99 and the device is left consistent */
extern volatile int flash_cancel;
extern unsigned flash_read_depth;   /* batches in flight during a read (1 = the old stop-and-wait) */
/* read `nwords' words starting at word `w0' into buf (big-endian bytes, file order).  Pipelines `batch' reads per packet.  0 ok */
int flash_read(tnp_stream_t *c, uint32_t w0, uint32_t nwords, uint8_t *buf, unsigned batch, progress_fn pf);
/* is this a G1 we know?  cfg0 = config register 0.  Returns the name or NULL */
const char *g1_board_name(uint32_t cfg0);
/* THE BOARD, for the read-only commands.  series = (cfg0 >> 26) & 0x3F: 0 = G1, 2 = G2 (Pano's Series 2 updater accepts only
 * that).  A G2's flash follows cfg1 bits [25:23]: 1 = rev C (Spartan-6 LX100, M25P64, 8 MB), 0 = rev A/B (LX150, M25P128,
 * 16 MB).  bytes = 0 means unknown.  The register interface (0x5000..0x5002, 0xFC) is the same on both series. */
typedef struct { const char *name; unsigned series; uint32_t bytes; } board_t;
void board_identify(uint32_t cfg0, uint32_t cfg1, board_t *b);
#endif

/* ---- writing --------------------------------------------------------------------------------------------------------- */
typedef struct { const char *path; uint32_t byte_off; uint8_t *data; size_t len; } region_t;
/* erase the sectors the regions cover (below PROTECT_WORD only), program every word the way Pano's updater does (one word per
 * packet, latch then data, a 10 ms pause every 64 words), read everything back and compare.  0 ok; on any failure the return
 * says where and the device is left with 0xFC = 0. */
int flash_write_regions(tnp_stream_t *c, const region_t *r, unsigned nr, uint32_t limit_word, progress_fn pf, char *err, size_t errlen);   /* limit_word: first word that may not be touched */
/* read back and compare only: 0 identical, >0 = number of differing words, <0 error */
long flash_verify_regions(tnp_stream_t *c, const region_t *r, unsigned nr, progress_fn pf, char *err, size_t errlen);
