/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/g2.h -- Pano Logic G2 board support for the G2 build of the shell (PANO_G2): the watchdog, IPROG reconfiguration,
 * DDR calibration status, the SPI write guard's status, the button, and the factory data block in flash.
 * Register addresses: fw/g2_regs.h. */
#ifndef G2_H
#define G2_H
#include <stdint.h>
#include "g2_regs.h"

/* 1 once g2_init() has read G2_REC_ID_VAL from the recovery page; every recovery-page access is skipped while it is 0
 * (a bitstream without the page answers 0 there and counts a bus error per access) */
extern int g2_sys_present;
/* 1 once g2_init() has read G2_BOARD_ID_VAL: the fabric has the SPI byte engine and guard status at 0x8000_C000 */
extern int g2_spi_present;

/* read the recovery page's ID and kick the watchdog once; call first in main() */
void g2_init(void);

/* RESTART THE WATCHDOG COUNT.  Called from the shell's input poll (every pass of the command loop), from netsh_poll
 * (every long network operation), from the SPI flash WIP poll (erases) and from memtest's inner loops.  NEVER from an
 * interrupt handler: a handler keeps running when the main loop has hung, and a kick there would hide the hang. */
static inline void g2_wdt_kick(void) { if (g2_sys_present) G2_WDT_KICK = G2_WDT_KEY; }

/* IPROG to the image at SPI flash byte address addr (the recovery page's ICAP_SPARTAN6 sequence: WBSTAR = addr,
 * GENERAL3/4 = 0x040000 as the fallback, CMD IPROG).  Returns only if the reconfiguration did not happen within
 * 100 ms (-1), or the recovery page is absent (-2). */
int  g2_iprog(uint32_t addr);

int  g2_ddr_ready(void);                     /* 1: the DDR port reports calibration done */
unsigned int g2_guard_refusals(void);        /* the SPI write guard's refusal count, G2_SPI_STATUS[31:16] */
/* the DDR windows of chip A: cached 0x1000_0000 and uncached 0x9000_0000, 64 MB each.  1 if [addr, addr + n) touches
 * either; such a range may be accessed only when g2_ddr_ready(). */
int  g2_in_ddr(uint32_t addr, uint32_t n);
int  g2_in_ddr_whole(uint32_t addr, uint32_t n); /* 1 if the range lies wholly inside one of those windows */
int  g2_in_bram(uint32_t addr, uint32_t n);      /* 1 if the range lies wholly inside block RAM */

/* THE FACTORY DATA BLOCK at SPIF_G2_CONFIG: big-endian {tag, value} word pairs, tag 0x1000_00RR for configuration
 * register RR, ended by 0xFFFFFFFF.  Register 0x00 is the board ID (G2 rev C: 0x0801_0002), 0x10 holds the MAC's upper
 * two bytes and 0x11 its lower four (for example 0x0000_001C, 0x0264_CBD9 -> 00:1C:02:64:CB:D9).  Returns 1 and fills the
 * outputs when the table holds the register(s), else 0.  Read only. */
int  g2_cfg_reg(unsigned reg, uint32_t *val);
int  g2_factory_mac(uint8_t mac[6]);

#endif
