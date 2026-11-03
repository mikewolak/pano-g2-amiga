/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ch7301.h -- the two Chrontel CH7301C DVI encoders of the Pano Logic G2, set up over their shared I2C bus.
 *
 * Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
 *
 * The bus is bit-banged through the video page's pin register, 0x8000_601C (rtl/vga/g2_video.v):
 *   write {hdmi_reset_n[5], dvi_reset_n[4], sda_low[1], scl_low[0]}, read {..., sda[1], scl[0]}.
 * Both lines are open drain.  Encoder serial addresses (data sheet pin AS, address 1,1,1,0,1,AS*,AS):
 * 0x75 (AS = 1) and 0x76 (AS = 0); which of the two is the DVI-I encoder is not established, so every write
 * goes to both, as the only public G2 design does.
 */
#ifndef CH7301_H
#define CH7301_H

#define CH7301_ADDR_A 0x75u
#define CH7301_ADDR_B 0x76u
#define CH7301_DID    0x17u    /* register 4Bh, device ID, read only */
#define CH7301_VID    0x95u    /* register 4Ah, version ID, read only */

/* Pulse both RESET* pins (the page drives them; they are released after FPGA reset), then program both
 * encoders for video mode `mode_id' (fw/vga.h numbering).  Returns a bit mask: bit 0 set when 0x75 acknowledged
 * every byte, bit 1 when 0x76 did.  0 means neither encoder answered.  Modes 3 and 4 return -1 (the G2 head does
 * not drive them). */
int ch7301_init(unsigned int mode_id);

/* The mode-dependent registers only (DVI PLL, 33h/34h/36h).  Same return value as ch7301_init. */
int ch7301_set_mode(unsigned int mode_id);

/* One register write / read at 7-bit address `addr'.  Write returns 1 when all three bytes were acknowledged;
 * read returns the byte, or -1 when a byte was not acknowledged. */
int ch7301_write(unsigned int addr, unsigned int reg, unsigned int val);
int ch7301_read(unsigned int addr, unsigned int reg);

#endif
