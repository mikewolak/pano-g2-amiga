/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* spi.h -- SPI NOR flash through the FT2232H's channel A in MPSSE mode: ftdiflash's procedures (amiga/third_party/
 * ftdiflash, Clifford Wolf and Dean Miller, ISC) on the stream of link.c, with the part's facts from flashchips.h.
 *
 * Pins: ADBUS0 SCK, ADBUS1 MOSI, ADBUS2 MISO; chip select on ADBUS3-7 (ftdiflash: ADBUS4, low active); ADBUS7 the
 * CRESET of an iCE40 (ftdiflash holds it low between commands), held low, held high or not driven.  The bus settings
 * (spi_cfg_t) are set with spi_config; the default is SPI mode 0, MSB first, chip select ADBUS4 low active, CRESET
 * held low.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef SPI_H
#define SPI_H
#include "flashchips.h"

typedef struct {
    int cpol;                        /* SCK level between transfers: 0 low, 1 high */
    int cpha;                        /* MISO sampled on the 0 leading, 1 trailing edge of SCK; MOSI set on the other */
    int lsb;                         /* 1: least significant bit first */
    int cs_pin;                      /* ADBUS bit of chip select, 3 to 7 */
    int cs_high;                     /* 1: chip select high active */
    int creset;                      /* ADBUS7 when it is not chip select: 0 held low, 1 held high, 2 not driven */
} spi_cfg_t;

void spi_config(const spi_cfg_t *c);
void spi_config_get(spi_cfg_t *c);
/* the MPSSE opcodes of the mode: bytes out, bytes in, bytes out and in */
void spi_opcodes(unsigned char *wr, unsigned char *rd, unsigned char *wrrd);

/* 0x9F: up to 20 bytes of the answer (ftdiflash reads 20) into id; 0 or -1 */
int  flash_read_id(unsigned char *id, int n);
/* REMS (0x90) and RES (0xAB) answers, for parts that have no 0x9F */
int  flash_read_rems(unsigned char *mfg, unsigned char *dev);
int  flash_read_res(unsigned char *sig);
int  flash_power_up(void);
int  flash_power_down(void);
int  flash_write_enable(void);
int  flash_bulk_erase(void);                                  /* 0xC7 */
int  flash_erase_block(int op, unsigned long addr);           /* 0x20/0x52/0xD8/... at addr */
int  flash_prog(unsigned long addr, const unsigned char *data, int n);   /* page program 0x02, n <= page */
int  flash_read(unsigned long addr, unsigned char *data, int n);         /* 0x03, n <= 4096 */
int  flash_status(unsigned char *sr);                         /* 0x05 */
int  flash_wait(unsigned long timeout_ms);                    /* until WIP is 0; 0, or -1 on the timeout */
int  flash_unprotect(void);                                   /* status register 0: WREN (and EWSR 0x50), 0x01 0x00 */
int  flash_enter_4byte(void);                                 /* 0xB7: 4-byte addresses (parts above 16 MB) */
void flash_addr4(int on);                                     /* addresses of 4 bytes from now on */
#endif
