/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/spiflash.h -- the M25P80 from the CPU, bit-banged through board page op 5 (the same claim fw/stage1.c uses). */
#ifndef SPIFLASH_H
#define SPIFLASH_H
#include <stdint.h>

#ifdef PANO_G2
/* THE FLASH MAP IS CHOSEN BY THE PART, NOT BY THE BUILD.  spif_map() reads the JEDEC ID (RDID 0x9F) and returns the map
 * of that part; every SPIF_ name below is a field of it.  An ID that is neither part (no answer, a misread, another
 * device) gives the empty map: size 0, so spif_write_allowed() refuses every range and the shell refuses every flash
 * write, and RDID is read again at the next call.  The SPI block's write guard (rtl/g2/g2_spi_guard.v) refuses the same
 * commands in hardware for the map the fabric was built with.
 *
 * Revision C: M25P64, 8 MB, 64 KB sectors
 *   0x000000-0x03FFFF  multiboot header                  never written
 *   0x040000-0x37FFFF  golden image (stock firmware)     never written
 *   0x380000-0x6BFFFF  multiboot image                   SPIF_G2_MAIN
 *   0x6C0000-0x6CFFFF  configuration table and MAC       never written
 *   0x6D0000-0x7FFFFF  free                              SPIF_G2_FREE: U-Boot 0x6D0000, DDR shell 0x750000,
 *                                                        boot-select word 0x7FFFF0
 * Revision B: M25P128, 16 MB, 256 KB sectors
 *   0x000000-0x03FFFF  multiboot header                  never written
 *   0x040000-0x47FFFF  golden image (stock firmware)     never written
 *   0x480000-0x8BFFFF  multiboot image                   SPIF_G2_MAIN
 *   0x8C0000-0x8FFFFF  configuration table and MAC       never written
 *   0x900000-0xFFFFFF  free                              SPIF_G2_FREE: U-Boot 0x900000, DDR shell 0x980000, cube
 *                                                        0x9C0000, U-Boot environment 0xF80000 and 0xFC0000,
 *                                                        boot-select word 0xFFFFF0 */
typedef struct {
    uint32_t    rdid, size, sector;
    uint32_t    golden, main, main_end, config, free, free_end, uboot, ldr_max, bootsel, ddrshell;
    const char *part_name, *part_s, *sector_s, *map_s, *writable_s, *protect_s;
    const char *main_s, *uboot_s, *bootsel_s, *ddrshell_s;
} spif_map_t;
const spif_map_t *spif_map(void);              /* the map of the part RDID names; the empty map for any other ID */
#define SPIF_PAGE        256u
#define SPIF_RDID_VAL    (spif_map()->rdid)     /* 0 in the empty map: no ID equals it */
#define SPIF_SIZE        (spif_map()->size)
#define SPIF_SECTOR      (spif_map()->sector)
#define SPIF_G2_GOLDEN   (spif_map()->golden)
#define SPIF_G2_MAIN     (spif_map()->main)
#define SPIF_G2_MAIN_END (spif_map()->main_end)
#define SPIF_G2_CONFIG   (spif_map()->config)
#define SPIF_G2_FREE     (spif_map()->free)
#define SPIF_G2_FREE_END (spif_map()->free_end)
#define SPIF_G2_UBOOT    (spif_map()->uboot)    /* the PANOLDR image stage 1 starts */
#define SPIF_G2_LDR_MAX  (spif_map()->ldr_max)  /* the PANOLDR region below the next reserved sector */
#define SPIF_G2_BOOTSEL  (spif_map()->bootsel)  /* the stage-1 boot-select word */
#define SPIF_G2_DDRSHELL (spif_map()->ddrshell) /* the DDR shell slot of the U-Boot layout */
#define SPIF_PART_NAME   (spif_map()->part_name)
#define SPIF_PART_S      (spif_map()->part_s)
#define SPIF_SECTOR_S    (spif_map()->sector_s)
#define SPIF_MAP_S       (spif_map()->map_s)
#define SPIF_WRITABLE_S  (spif_map()->writable_s)
#define SPIF_PROTECT_S   (spif_map()->protect_s)
#define SPIF_MAIN_S      (spif_map()->main_s)
#define SPIF_UBOOT_S     (spif_map()->uboot_s)
#define SPIF_BOOTSEL_S   (spif_map()->bootsel_s)
#define SPIF_DDRSHELL_S  (spif_map()->ddrshell_s)
/* 1 if [addr, addr + n) lies wholly inside one writable region, else 0 */
int      spif_write_allowed(uint32_t addr, uint32_t n);
#else
#define SPIF_SIZE        0x100000u     /* 1 MB */
#define SPIF_SECTOR      0x10000u      /* 64 KB */
#define SPIF_PAGE        256u
#define SPIF_RDID_VAL    0x202014u     /* M25P80 */
#define SPIF_LOADER_OFF  0x0C0000u     /* the loader region; everything below it is the bitstream (12 x 64 KB exactly) */
#endif

uint32_t spif_rdid(void);                              /* SPIF_RDID_VAL */
uint8_t  spif_rdsr(void);
void     spif_read(uint32_t addr, void *dst, uint32_t n);
/* each returns 0 on success, -1 on a WIP timeout, -2 (PANO_G2) an address outside the writable regions; they WREN
 * themselves */
int      spif_sector_erase(uint32_t addr);             /* SE 0xD8: 64 KB, up to 3 s */
int      spif_page_program(uint32_t addr, const void *src, uint32_t n);   /* PP 0x02: n <= 256, must not cross a page */
int      spif_sector_erase_start(uint32_t addr);       /* SE 0xD8 without the wait: 0 sent, -2 refused */
int      spif_wip(void);                               /* 1 while a write or erase is in progress */
#endif
