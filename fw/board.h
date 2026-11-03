/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/board.h -- the board page (0x8000_5000) as firmware sees it, and the clock the CPU counts. */
#ifndef BOARD_H
#define BOARD_H
#define SOC_BUILD_ID  (*(volatile unsigned int *)0x80005010u)   /* the git hash this bitstream was built from; 0 = none */

/* THE CPU CLOCK, the one definition per board.  mcycle (rdcycle), the timer at 0x8000_2000 (rtl/soc_timer.v), the
 * G2's recovery watchdog count and SPI byte engine, and every bit-banged bus (SPI, I2C, MDIO) run on this clock, and
 * every cycle count in fw/ that stands for a time is derived from these three definitions.
 *   PANO_G2  rtl/g2/pano_g2_top.v clk_cpu: 66.667 MHz (rtl/g2/g2_mcb_clk.v: 1000 MHz VCO / 15).  PANO_CYC_PER_US rounds to the
 *            nearest integer (67): a cycle wait of N us runs 0.5 % long, never short.  The G2's 50 MHz fabric domain (the video heads, audio, RasterIX,
 *            the USB host) is reached through bridges and is not this clock.
 *   G1       rtl/pano_g1_top.v: 50 MHz. */
#if defined(PANO_G2) && defined(G2_SNES)
#define PANO_CPU_HZ      50000000u     /* the subsystem-slot fabric (G2_SNES): the CPU on clk_sys */
#elif defined(PANO_G2)
#define PANO_CPU_HZ      66666667u
#else
#define PANO_CPU_HZ      50000000u
#endif
#define PANO_CYC_PER_US  ((PANO_CPU_HZ + 500000u) / 1000000u)
#define PANO_CYC_PER_MS  (PANO_CPU_HZ / 1000u)
#endif
