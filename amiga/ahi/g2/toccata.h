/*-
 * Copyright (c) 1998, 1999, 2001, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg and Ignatios Souvatzis.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Toccata Zorro II register map.  Offsets and status bits are the NetBSD
 * amiga toccata driver's; the decode they meet is
 * amiga/third_party/fpga-toccata/toccata.sv (register select = address
 * bits 14, 13, 11).
 */

#ifndef G2_TOCCATA_H
#define G2_TOCCATA_H

#define TOCC_MANUFACTURER 18260         /* 0x4754 */
#define TOCC_PRODUCT      12

/* Board offsets */
#define TOCC_STATUS       0x0000        /* read: IRQ status, write: control */
#define TOCC_FIFO_DATA    0x2000        /* playback FIFO, 0x2000-0x27ff */
#define TOCC_CODEC_ADDR   0x67FF        /* AD1848 index (low byte lane) */
#define TOCC_CODEC_REG    0x6801        /* AD1848 indexed data (low byte lane) */

/* Status bits, read.  Reading clears them. */
#define TOCC_FIFO_INT     0x80          /* active low; with one of: */
#define TOCC_FIFO_PBHE    0x08          /* playback FIFO half empty */
#define TOCC_FIFO_CPHF    0x04          /* capture FIFO half full */

/* Control bits, write */
#define TOCC_RST          0x02
#define TOCC_ACT          0x01          /* alone: FIFO reset, IRQ clear, DAC mute */
#define TOCC_MAGIC        0x04          /* FIFO connected to the codec */
#define TOCC_PB_INTENA    0x80
#define TOCC_PB_FILL      0x10

#define TOCC_PB_PREP      (TOCC_ACT + TOCC_PB_FILL)            /* 0x11 */
#define TOCC_PB_TAIL      (TOCC_PB_PREP + TOCC_MAGIC)          /* 0x15 */
#define TOCC_PB_MAIN      (TOCC_PB_TAIL + TOCC_PB_INTENA)      /* 0x95 */

/* AD1848 indexed registers and bits used here */
#define AD_MCE            0x40          /* mode change enable, index register */
#define AD_INIT           0x80          /* index register reads 0x80 while busy */
#define AD_LDAC           0x06          /* left DAC: bit 7 mute, 5:0 att 1.5 dB */
#define AD_RDAC           0x07          /* right DAC */
#define AD_FORMAT         0x08          /* clock and data format */
#define AD_FMT_16         0x40          /* 16-bit two's complement */
#define AD_FMT_STEREO     0x10
#define AD_FMT_CSS        0x01          /* 16.9344 MHz crystal */

/* FIFO: 1024 bytes, interrupt as the fill falls through 504 after
 * reaching 512 (toccata_fifo.sv); full at 1023. */
#define TOCC_FIFO_SIZE    1024
#define TOCC_FIFO_HALF    512
#define TOCC_FIFO_PREFILL 768           /* 3/4, as the NetBSD start sequence */

#endif /* G2_TOCCATA_H */
