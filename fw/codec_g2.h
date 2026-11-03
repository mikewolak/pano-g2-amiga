/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/codec_g2.h -- the WM8750BL codec and the audio page of the G2 (rtl/g2/g2_audio.v, 0x8000_9000), shared by the
 * DDR shell and the programs it launches.  Control: the codec's write-only 2-wire port (7-bit 0x1A, CSB low) on the
 * second bit-banged bus of rtl/g2/g2_usb_host.v +0x28 (U17/AB17).  Data: stereo frames {L[31:16], R[15:0]} of 16-bit
 * two's complement samples at DATA, clocked out at 48 kHz, 24-bit I2S, the codec the slave. */
#ifndef CODEC_G2_H
#define CODEC_G2_H
#include <stdint.h>
#define AUDG2_R(o)     (*(volatile uint32_t *)(0x80009000u + (o)))
#define AUDG2_CTRL     AUDG2_R(0x00)      /* [0] run, [1] flush (self-clearing), [2] wide: two DATA words a frame, L then R, 24-bit left-justified; R [8] MCLK locked */
#define AUDG2_STATUS   AUDG2_R(0x04)      /* [12:0] words in the FIFO (a frame is one word, two in wide mode), [15] full, [16] empty, [17] crossing FIFO full */
#define AUDG2_DATA     AUDG2_R(0x08)      /* W one stereo frame {L16, R16}; wide: one 24-bit sample {S[23:0], 8'b0}, L then R */
#define AUDG2_WIDE     4u
#define AUDG2_COUNT    AUDG2_R(0x0C)      /* frames handed to the serialiser */
#define AUDG2_UNDER    AUDG2_R(0x10)      /* [15:0] underruns, [31:16] frames dropped at DATA */
#define AUDG2_ID       AUDG2_R(0x14)      /* 0x4155_4431 */
#define AUDG2_ID_VALUE 0x41554431u
#define AUDG2_FIFO     2048u
#define AUDG2_LEVEL()  (AUDG2_STATUS & 0x1FFFu)
int codec_g2_present(void);                                   /* 1 when the fabric carries the audio page */
int codec_g2_init(unsigned int *ack_mask);
int codec_g2_reset(void);                                  /* R15 RESET: every register to its power-on value; 1 when acknowledged */                    /* the register program; 1 when every write was acknowledged */
#endif
