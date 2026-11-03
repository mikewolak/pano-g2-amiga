/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/amiga_regs.h -- the register page of amiga/rtl/g2/amiga_g2.v (0x8000_E000, its REGISTER PAGE comment).
 * The offsets 0x000-0x070 are those of the subsystem slot (G2_SNES) (fw/snes/snes_regs.h), so fw/snes/snes_audio.c drains
 * the Amiga's audio FIFO unchanged. */
#ifndef AMIGA_REGS_H
#define AMIGA_REGS_H
#include <stdint.h>
#define AMI_BASE         0x8000E000u
#define AMI_R(o)         (*(volatile uint32_t *)(AMI_BASE + (o)))
#define AMI_ID           AMI_R(0x000)
#define AMI_CTRL         AMI_R(0x004)
#define AMI_STATUS       AMI_R(0x008)
#define AMI_CACHE        AMI_R(0x00C)
#define AMI_HEAD_SEL     AMI_R(0x01C)
#define AMI_NCO_INC      AMI_R(0x020)
#define AMI_AUD_INC      AMI_R(0x024)
#define AMI_FB_BASE      AMI_R(0x02C)
#define AMI_VID          AMI_R(0x030)
#define AMI_FRAMES       AMI_R(0x034)
#define AMI_FRAME_INFO   AMI_R(0x038)   /* {fast[26], lines[25:16], lace[2], buffer[1:0]} */
#define AMI_VID_FAST     AMI_R(0x0B8)   /* VID's hofs for 31 kHz lines */
#define AMI_LINE_DROPS   AMI_R(0x0BC)
#define AMI_IRQ          AMI_R(0x03C)
#define AMI_TS_NOW       AMI_R(0x040)
#define AMI_TS_FRAME     AMI_R(0x044)
#define AMI_TS_HEAD      AMI_R(0x048)
#define AMI_AUD_DATA     AMI_R(0x04C)
#define AMI_AUD_LEVEL    AMI_R(0x050)
#define AMI_AUD_WMARK    AMI_R(0x054)
#define AMI_CHIP_MISS    AMI_R(0x058)
#define AMI_CHIP_WB      AMI_R(0x060)
#define AMI_HELD         AMI_R(0x068)
#define AMI_OWED         AMI_R(0x06C)
#define AMI_DISP         AMI_R(0x070)
#define AMI_MCU_TX       AMI_R(0x080)
#define AMI_SDC_REQ      AMI_R(0x090)
#define AMI_SDC_SECTOR   AMI_R(0x094)
#define AMI_SDC_GO       AMI_R(0x098)
#define AMI_SDC_DATA     AMI_R(0x09C)
#define AMI_SDC_MOUNT    AMI_R(0x0A0)
#define AMI_SDC_SIZE     AMI_R(0x0A4)
#define AMI_MBX_ADDR     AMI_R(0x0A8)   /* the X-Surf board's host mailbox (AGA fabric): byte offset */
#define AMI_MBX_DATA     AMI_R(0x0AC)   /* one 16-bit word; each access moves MBX_ADDR on by 2 */
#define AMI_XS_MAC_HI    AMI_R(0x0B0)   /* station address bytes 0-1 (bits 15:8 byte 0) */
#define AMI_XS_MAC_LO    AMI_R(0x0B4)   /* bytes 2-5 */
#define AMI_MIDI_RX      AMI_R(0x0C0)   /* {valid[8], byte[7:0]}: Paula's transmitted bytes; a read pops one */
#define AMI_MIDI_TX      AMI_R(0x0C4)   /* R {ready[8]}; W a byte for Paula's receiver (31,250 baud) */
#define AMI_MIDI_ST      AMI_R(0x0C8)   /* R {CIA-B ctl in[19:16], {DTR,RTS}[13:12], TX overflow[8], TX fill[7:0]};
                                           W {RI, CD, DSR, CTS}[3:0] for CIA-B, 0 asserted (rtl/g2/amiga_serbridge.v) */
#define AMI_MIDI_SERPER  AMI_R(0x0CC)   /* [14:0] Paula's SERPER (bit time SERPER + 1 colour clocks); in FIFO mode
                                           {1, the G2 serial FIFO board's baud rate [30:0]} */
#define MIDI_TX_FIFO     255u           /* bytes the TX FIFO holds */
#define MIDI_VALID       0x100u
#define MIDI_READY       0x100u

#define AMI_ID_VAL       0x414D4947u   /* "AMIG" */
#define ACTRL_RUN        0x01u
#define ACTRL_RESET      0x02u         /* core_reset */
#define ACTRL_SDC_HOLD   0x04u
#define ACTRL_SER_IRQ    0x08u         /* the interrupt while the MIDI RX FIFO holds a byte (AMI_IRQ bit 3) */
#define ACTRL_MIRROR     0x20u
#define ACTRL_FRM_IRQ    0x40u
#define ACTRL_AUD_IRQ    0x80u
#define ACACHE_INV       0x1u
#define ACACHE_FLUSH     0x2u
#define ACACHE_AUD_CLEAR 0x8u
#define AMI_NCO_NTSC     0x929E4149u   /* 28.636364 MHz / 50 MHz x 2^32 */
#define AMI_NCO_PAL      0x9147E3C4u   /* 28.375160 MHz / 50 MHz x 2^32 */
#define AMI_AUD_NTSC     0x006DD9CFu   /* 48 kHz in NTSC core time */
#define AMI_AUD_PAL      0x006EDCAEu   /* 48 kHz in PAL core time: 48,000 / 28,375,160 x 2^32 */

#define SDC_PENDING      0x80000000u
#define SDC_DONE         0x40000000u
#define SDC_WRITE        0x00010000u

#define MCU_BUSY         0x80000000u
#define MCU_START        0x00010000u
#define MCU_SYS          0u            /* sysctrl.v */
#define MCU_HID          1u            /* hid.v */
#define MCU_OSD          2u            /* osd_u8g2.v */

/* chip memory: one 16 MB byte space at chip B + 0x0200_0000 (amiga_g2.v DDR_BASE), uncached CPU window */
#define AMI_CHIP_UC      0x96000000u
#define AMI_KICK_UC      (AMI_CHIP_UC + 0x00780000u)
#define AMI_FKICK_UC     0x93780000u    /* the Kickstart copy for turbo kick: fast-RAM byte 0x78_0000 (chip A, uncached) */
/* frame buffers in chip B: FB_BASE + n x 1 MB, n = 0..2; head 2's window 16 MB above head 1's */
#define AMI_FB_OFF       0x00800000u
#define AMI_FB_STEP      0x00100000u
#define AMI_HEAD2_DELTA  0x01000000u
/* the floppy images, held whole in chip A (uncached window), 2 MB a drive: chip A 0x0200_0000-0x027F_FFFF, above
 * the firmware's heap (fw/link_ddr.ld __heap_end 0x1200_0000) and below the simulation's RAM disk (0x0300_0000) */
#define AMI_DISK_UC      0x92000000u
#define AMI_DISK_STEP    0x00200000u

#define G2_IRQ_LINE_AMI  6u            /* externalInterruptArray[6]: the page OR the audio watermark */
#endif
