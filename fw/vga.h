/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/vga.h -- the video head as a driver, with the mode changing under a running program.
 *
 * The mode changes while a program runs.  The video head does the hard half: `mode' is resampled only at a frame boundary and
 * a base/geometry swap is taken at the next vblank, so nothing here has to stop the scan, blank the DAC or
 * count clocks -- it writes the registers and lets the frame boundary do the rest.  What was missing was the
 * C side: a mode table, an atomic set that waits for the change to be ON SCREEN before it returns, and a
 * source geometry that follows the mode.
 *
 * The page is 0x8000_6000:
 *   +0x00  {mode[3:1], enable[0]}      the mode is latched at the next frame
 *   +0x04  base, in 16-BIT LPDDR WORDS (byte address / 2)
 *   +0x08  {h[26:16], w[10:0]}         the SOURCE geometry; the head scales it up to the mode
 *   +0x0C  write = take base+geometry at the next vblank; read = the base being displayed
 *   +0x10  frames scanned (free running)
 *   +0x14  LPDDR runs granted
 */
#ifndef VGA_H
#define VGA_H
#include <stdint.h>

#define VGA_BASE_ADDR 0x80006000u   /* head 1 (the G1's one page); head 2 of the G2 is 0x8000_7000 */
extern volatile uint32_t *vga_regs;   /* the page in use: vga_select() */
extern unsigned int       vga_fb_window;   /* the CPU address of the head's frame-buffer window origin */
void vga_select(unsigned int head);   /* 0 = 0x8000_6000 (window 0x9400_0000 on the G2), 1 = 0x8000_7000 (0x9500_0000) */
#define VGA_R(o)      (vga_regs[(o) >> 2])
#define VGA_CTRL      VGA_R(0x00)
#define VGA_FBBASE    VGA_R(0x04)
#define VGA_WH        VGA_R(0x08)
#define VGA_SWAP      VGA_R(0x0C)
#define VGA_FRAMES    VGA_R(0x10)
#define VGA_PAL       VGA_R(0x18)   /* write: {index[31:24], r[23:16], g[15:8], b[7:0]}.  READ: capabilities (below) */
#define VGA_CAP_PAL1024 1u          /* a paletted source line may be up to 1,024 pixels (older bitstreams read 0: 512) */
#define VGA_CTRL_PAL  0x10u         /* control bit 4: the source is one BYTE a pixel, looked up in the head */
#define VGA_CTRL_565  0x20u         /* control bit 5: the source is one 16-bit RGB565 word a pixel (G2 heads, capability bit 3) */
#define VGA_RUNS      VGA_R(0x14)

/* The head's own table (rtl/vga/vga_scan.v).  Mode 3 is not in its case statement and falls to the default,
 * which is the 1600x1200 reduced-blanking mode at a 100 MHz pixel clock -- it is listed here so nobody
 * discovers that by writing a 3 and losing the picture. */
typedef struct {
    unsigned int id;             /* what goes in CTRL[3:1] */
    unsigned int w, h;           /* the mode's own active pixels */
    unsigned int pixclk_khz;     /* the head's average NCO rate */
    unsigned int hz;             /* refresh, rounded */
    unsigned int hsync_hz;       /* line rate: what a monitor locks to first */
    const char  *name;
} vga_mode_info_t;

#define VGA_NMODES 5
/* the mode the boot screen comes up in: 800x600, one every tested monitor displays
 * (640x480 and 320x240@15.7kHz do too; 800x600 gives the text the most pixels to be sharp in) */
#define VGA_MODE_BOOT 1u
extern const vga_mode_info_t vga_modes[VGA_NMODES];

/* Turn the head on in a mode, with a source image of w x h 32-bit pixels at `byte_addr' in LPDDR.  The head
 * scales the source up to the mode (it is upscale-only, so the source must not be larger than the mode).
 * Returns 0, or -1 if the mode is out of range or the source is bigger than the mode. */
int  vga_set_mode(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h);
/* ---- PALETTED SOURCES ----------------------------------------------------------------
 * In paletted mode a source pixel is ONE BYTE, an index into the head's own 256-entry table, and the head
 * does the lookup during scanout.  The CPU stops expanding indices into colours -- which on this board is
 * not a detail: expanding a 320x200 byte frame is 64,000 stores of 32 bits into LPDDR, measured at 41 ms, because there is no byte mask here and every store is a burst read-modify-write.  Paletted, the
 * frame the CPU hands over is the byte frame it already drew, so that whole cost is gone, and scanout
 * reads a quarter of the memory it did.
 *
 * Write all 256 entries BEFORE enabling it: the table has no reset value, because a palette is meaningless
 * until it is written and a default would only be wrong in a different way.
 *
 * r, g, b are 8-bit, placed at [9:2] of the head's 10-bit fields -- exactly the bits the THS8135 latches,
 * so what reaches the DAC is the byte you wrote, neither shifted nor rounded. */
void vga_set_palette_entry(unsigned int idx, unsigned int r, unsigned int g, unsigned int b);
/* w is in PIXELS (bytes) here, not 32-bit words. Otherwise exactly vga_set_mode. */
int  vga_set_mode_paletted(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h);
int  vga_set_mode_565(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h);   /* RGB565 source */
/* Point the head at another buffer of the SAME geometry, taken at the next vblank (double buffering). */
void vga_swap(unsigned int byte_addr);
/* Block until the head has finished a frame, or until `budget' polls run out; returns the frames seen. */
unsigned int vga_wait_frame(unsigned int budget);
void vga_off(void);
const vga_mode_info_t *vga_mode_now(void);   /* the mode last set, 0 if the head is off */
#endif
