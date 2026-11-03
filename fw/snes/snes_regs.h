/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/snes/snes_regs.h -- the register page of the subsystem slot (G2_SNES) and the g2_audio
 * watermark register (rtl/g2/g2_audio.v 0x18). */
#ifndef SNES_REGS_H
#define SNES_REGS_H
#include <stdint.h>
#define SNES_BASE        0x8000E000u
#define SNES_R(o)        (*(volatile uint32_t *)(SNES_BASE + (o)))
#define SNES_ID          SNES_R(0x000)
#define SNES_CTRL        SNES_R(0x004)
#define SNES_STATUS      SNES_R(0x008)
#define SNES_CACHE       SNES_R(0x00C)
#define SNES_ROM_TYPE    SNES_R(0x010)
#define SNES_ROM_MASK    SNES_R(0x014)
#define SNES_RAM_MASK    SNES_R(0x018)
#define SNES_HEAD_SEL    SNES_R(0x01C)
#define SNES_NCO_INC     SNES_R(0x020)
#define SNES_PAD1        SNES_R(0x024)
#define SNES_PAD2        SNES_R(0x028)
#define SNES_FB_BASE     SNES_R(0x02C)
#define SNES_VID         SNES_R(0x030)
#define SNES_FRAMES      SNES_R(0x034)
#define SNES_FRAME_INFO  SNES_R(0x038)
#define SNES_IRQ         SNES_R(0x03C)
#define SNES_TS_NOW      SNES_R(0x040)
#define SNES_TS_FRAME    SNES_R(0x044)
#define SNES_TS_HEAD     SNES_R(0x048)
#define SNES_AUD_DATA    SNES_R(0x04C)
#define SNES_AUD_LEVEL   SNES_R(0x050)
#define SNES_AUD_WMARK   SNES_R(0x054)
#define SNES_CART_MISS   SNES_R(0x058)
#define SNES_WRAM_MISS   SNES_R(0x05C)
#define SNES_CART_WB     SNES_R(0x060)
#define SNES_WRAM_WB     SNES_R(0x064)
#define SNES_HELD        SNES_R(0x068)
#define SNES_OWED        SNES_R(0x06C)
#define SNES_DISP        SNES_R(0x070)

#define SNES_ID_VAL      0x534E4553u
#define CTRL_RUN         0x01u
#define CTRL_RESET       0x02u
#define CTRL_PAL         0x04u
#define CTRL_BLEND       0x08u
#define CTRL_DSP_FREQ    0x10u
#define CTRL_MIRROR      0x20u
#define CTRL_FRM_IRQ     0x40u
#define CTRL_AUD_IRQ     0x80u
#define CACHE_INV        0x1u
#define CACHE_FLUSH      0x2u
#define CACHE_FILL       0x4u
#define CACHE_AUD_CLEAR  0x8u
#define VID_NARROW       (1u << 9)
#define VID_PROG         (1u << 10)
#define VID_PITCH512     (1u << 11)    /* read only: the narrow mode writes lines at a 512-byte pitch */
#define NCO_NTSC         0x6DF6B0DFu   /* 21.477273 MHz / 50 MHz x 2^32 */

/* chip B memory of the subsystem (cached / uncached CPU windows) */
#define SNES_ROM_UC      0x96000000u
#define SNES_BSRAM_UC    0x96800000u
#define SNES_WRAM_UC     0x97000000u
#define SNES_ROM_MAX     0x00800000u
#define SNES_FB_OFF      0x00800000u   /* frame buffers in a head window: FB_OFF + n x 0x80000, n = 0..2 */
#define SNES_FB_STEP     0x00080000u
#define SNES_HEAD2_DELTA 0x01000000u   /* head 2's window is 16 MB above head 1's in chip B */

/* g2_audio (page 0x8000_9000) */
#define AUD_R(o)         (*(volatile uint32_t *)(0x80009000u + (o)))
#define AUD_CTRL         AUD_R(0x00)
#define AUD_STATUS       AUD_R(0x04)
#define AUD_DATA         AUD_R(0x08)
#define AUD_COUNT        AUD_R(0x0C)
#define AUD_UNDER        AUD_R(0x10)
#define AUD_ID           AUD_R(0x14)
#define AUD_WMARK        AUD_R(0x18)
#define AUD_FIFO         2048u

#define G2_IRQ_LINE_SNES 6u            /* externalInterruptArray[6]: the subsystem page OR the audio watermark */
#endif
