/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/g2_regs.h -- Pano Logic G2 (revision C) stage-1 SoC registers as the G2 firmware (PANO_G2) uses them.
 *
 * SOURCE: the fabric's own register comments -- rtl/g2/pano_soc_g2.v (map, board page), rtl/g2/g2_recovery.v
 * (0x8000_D000), rtl/g2/g2_spi.v and rtl/g2/g2_spi_guard.v (0x8000_C000).  The register map is the authority once
 * it exists; where it differs, this file changes to match it.
 *
 * Carried over from the G1 SoC unchanged, and used by the shared drivers as they are:
 *   0x8000_1000  console RX {bit8 valid, [7:0] data}, reading pops; +0x04 TX   fw/console.h (a UART on the G2:
 *                                                                              115,207 baud, AB19 TX, AA21 RX)
 *   0x8000_2000  timer                                                         fw/timer.h
 *   0x8000_5000  board page write [31:28] op: 4 LEDs, 5 SPI pins, 6 MDIO pins   fw/spiflash.c, fw/ethphy.c
 *   0x8000_5010  build ID                                                      fw/board.h
 *   0x8000_501C  read: [0] SPI MISO, [1] MDIO in, [2] AA4 level, [4] button    fw/spiflash.c, fw/ethphy.c
 *   0x8000_A000  MAC RX ring / TX buffer; 0x8000_B000 MAC registers            fw/ethmac.h
 * Any address the fabric does not decode is acknowledged, reads 0 and counts a bus error (board page +0x20/+0x24).
 */
#ifndef G2_REGS_H
#define G2_REGS_H
#include "board.h"                         /* PANO_CPU_HZ */

#define G2_REG(a)           (*(volatile unsigned int *)(a))

/* ---- memory map ------------------------------------------------------------------------------------------------- */
#define G2_BRAM_BASE        0x00000000u      /* block RAM, cached; reset vector */
#define G2_BRAM_SIZE        0x00040000u      /* 256 KB */
#define G2_DDR_A_BASE       0x10000000u      /* DDR2 chip A (MCB1), cached */
#define G2_DDR_A_SIZE       0x04000000u      /* 64 MB, MT47H32M16 */
#define G2_DDR_B_BASE       0x14000000u      /* DDR2 chip B (MCB3), a later stage */
#define G2_UNCACHED         0x80000000u      /* address bit 31: the uncached alias of the DDR window */

/* ---- board page additions --------------------------------------------------------------------------------------- */
#define G2_DDR_STATUS       G2_REG(0x80005004u)   /* R [0] DDR port ready (MCB calibration done); an access while 0 is
                                                   * a bus error */
#define G2_DDR_A_CALIB      0x1u
#define G2_DDR_B_CALIB      0x2u             /* not implemented in stage 1: reads 0 */
#define G2_BRD_PINS         G2_REG(0x8000501Cu)
#define G2_PIN_BUTTON       0x10u            /* [4] button H12, pressed = 1 */
#define G2_BUS_ERRORS       G2_REG(0x80005020u)   /* R undecoded accesses since configuration */
#define G2_BUS_ERR_ADDR     G2_REG(0x80005024u)   /* R the last one's address */
#define G2_EXT_TIMEOUTS     G2_REG(0x80005028u)   /* R external-port accesses the fabric answered after 4,096 clocks */
#define G2_CLK_STATUS       G2_REG(0x8000502Cu)   /* R [0] PLL locked, [1] PLL released from reset */
#define G2_BOARD_ID         G2_REG(0x80005030u)   /* R 0x4732_0001: "G2", fabric revision 1 */
#define G2_USB_TIMEOUTS     G2_REG(0x80005034u)   /* R USB-window accesses the fabric answered after 1,024 clocks (read 0xDEAD_0000) */
#define G2_USB_TO_ADDR      G2_REG(0x80005038u)   /* R the last one's address */
#define G2_BRD_PINCTL       G2_REG(0x80005040u)   /* RW [2:0] output enable, [6:4] level, R [10:8] pad: AA6, C10, C13 */
#define G2_BOARD_ID_VAL     0x47320001u

/* ---- SPI flash master and write guard, 0x8000_C000 --------------------------------------------------------------- */
#define G2_SPI_CTRL         G2_REG(0x8000C000u)   /* R/W [0] chip select asserted, [15:8] SCK half period in clocks */
#define G2_SPI_DATA         G2_REG(0x8000C004u)   /* W [7:0] byte to shift (mode 0, MSB first); R [7:0] byte in, [8] BUSY */
#define G2_SPI_STATUS       G2_REG(0x8000C008u)   /* R [0] BUSY [1] BLOCKED [2] bit-bang owns the pins [31:16] refusals */
#define G2_SPI_GUARD        G2_REG(0x8000C00Cu)   /* R last refusal: [7:0] opcode, [15:8] address[23:16], [16] at the address */
#define G2_SPI_PASSED       G2_REG(0x8000C010u)   /* R [15:0] write/erase commands whose address passed the guard */
#define G2_SPI_BUSY         0x100u
#define G2_SPI_DIV          ((PANO_CPU_HZ + 2u * 12500000u - 1u) / (2u * 12500000u))   /* SCK half period in clocks, rounded up: SCK <= 12.5 MHz (3 at 66.667 MHz: 11.1 MHz) */

/* ---- recovery: IPROG, watchdog, button, 0x8000_D000 ------------------------------------------------------------- */
#define G2_WDT_CTRL         G2_REG(0x8000D000u)   /* R [0] EN [1] EXPIRED; W 0x5A5A_000e sets EN = e */
#define G2_WDT_LOAD         G2_REG(0x8000D004u)   /* R/W timeout in clocks; reset value 2,000,000,000 (30 s at 66.667 MHz) */
#define G2_WDT_KICK         G2_REG(0x8000D008u)   /* W G2_WDT_KEY restarts the count; R kicks accepted */
#define G2_WDT_COUNT        G2_REG(0x8000D00Cu)   /* R clocks left */
#define G2_IPROG            G2_REG(0x8000D010u)   /* W G2_IPROG_KEY reconfigures from G2_IPROG_ADDR; R requests issued */
#define G2_IPROG_ADDR       G2_REG(0x8000D014u)   /* R/W [23:0] flash byte address of the image; reset 0x040000 */
#define G2_REC_STATUS       G2_REG(0x8000D018u)   /* R [0] button pressed [1] CPU held [2] button seen in the reset
                                                   * window [4:3] cause of the last request [5] request in flight
                                                   * [31:16] ICAP sequences completed */
#define G2_REC_ID           G2_REG(0x8000D01Cu)   /* R 0x4732_5243 ("G2RC") */
#define G2_REC_ID_VAL       0x47325243u
#define G2_WDT_KEY          0x4B49434Bu      /* "KICK" */
#define G2_IPROG_KEY        0x49505247u      /* "IPRG" */
#define G2_WDT_EN           0x1u
#define G2_REC_BUTTON       0x1u
#define G2_REC_INFLIGHT     0x20u

#endif
