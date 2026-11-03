/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
#include "vga.h"

/* the page and the frame-buffer window in use.  The G1 has one head at 0x8000_6000 scanning the LPDDR
 * (window 0x9000_0000, uncached); the G2 has two heads, each with a 16 MB window in DDR2 chip B
 * (0x9400_0000 and 0x9500_0000 uncached, 0x1400_0000 and 0x1500_0000 cached), addressed in full by the head's
 * 23-bit base (16-bit words). */
volatile uint32_t *vga_regs = (volatile uint32_t *)VGA_BASE_ADDR;
#ifdef PANO_G2
unsigned int vga_fb_window = 0x94000000u;
void vga_select(unsigned int head)
{
    vga_regs = (volatile uint32_t *)(head ? 0x80007000u : VGA_BASE_ADDR);
    vga_fb_window = head ? 0x95000000u : 0x94000000u;
}
#else
unsigned int vga_fb_window = 0x90000000u;
void vga_select(unsigned int head) { (void)head; }
#endif

/* The numbers are the head's, not a guess: pix_inc is a Q32.32 fraction of the 100 MHz fabric clock, so the
 * pixel rate is 100 MHz x that fraction, and the line and frame rates follow from the blanking in the same
 * case statement (rtl/vga/vga_scan.v).  hsync_hz is the figure a monitor locks to first and the one to quote
 * when it says a mode is out of spec. */
const vga_mode_info_t vga_modes[VGA_NMODES] = {
    { 0u,  640u,  480u, 25175u, 60u, 31469u, "640x480@60"     },   /* 800 x 525 total, syncs negative */
    { 1u,  800u,  600u, 40000u, 60u, 37879u, "800x600@60"     },   /* 1056 x 628 total, syncs positive */
    { 2u, 1024u,  768u, 65000u, 60u, 48363u, "1024x768@60"    },   /* 1344 x 806 total, syncs negative */
    { 3u, 1600u, 1200u,100000u, 46u, 56818u, "1600x1200RB@46" },   /* the head's default arm: no case of its own */
    { 4u,  320u,  240u,  6700u, 60u, 15748u, "320x240@15.7kHz"},   /* 240p: the arcade/15 kHz mode */
};

static const vga_mode_info_t *s_now;

/* THE HEAD COUNTS 16-BIT WORDS FROM THE START OF LPDDR, NOT FROM A CPU ADDRESS .  A caller naturally passes the pointer it writes
 * through -- 0x9080_0000 in the uncached window, or 0x1080_0000 cached -- and base is a 22-bit field, so
 * byte_addr >> 1 of either one truncates to a base that points nowhere.  Strip the window and keep the offset
 * inside the 32 MB part; a bare offset (already window-free) passes through unchanged. */
#ifdef PANO_G2
static unsigned int lp_offset(unsigned int byte_addr) { return (byte_addr - vga_fb_window) & 0x00FFFFFFu; }   /* inside the head's 16 MB window, either alias */
#else
static unsigned int lp_offset(unsigned int byte_addr) { return byte_addr & 0x01FFFFFFu; }
#endif

/* AND THE HEAD CANNOT SEE PAST 8 MB.  base is 22 bits of 16-BIT WORDS (rtl/pano_soc_vex.v: vga_base <=
 * m_mosi[21:0], vga_scan's swap_base is [21:0]), so the highest framebuffer it can scan starts at word
 * 0x3FFFFF -- byte 0x7FFFFE.  A buffer above that truncates silently and the head scans somewhere else
 * entirely: at exactly 8 MB it scans address 0, which is the running program, and the screen fills with
 * what looks like one frame of noise.  So it is a refusal. */
#ifdef PANO_G2
#define VGA_BASE_LIMIT 0x01000000u   /* G2 heads: base is 23 bits of words (rtl/vga/g2_video.v), 16 MB a head */
#else
#define VGA_BASE_LIMIT 0x00800000u
#endif

const vga_mode_info_t *vga_mode_now(void) { return s_now; }

/* sticky: set by vga_set_mode_paletted, so a later plain vga_swap or mode change keeps the source format */
static unsigned int s_pal;
static unsigned int s_565;                         /* one call's worth, as s_pal: vga_set_mode_565 */

unsigned int vga_wait_frame(unsigned int budget)
{
    unsigned int f0 = VGA_FRAMES, n = 0;
    while (VGA_FRAMES == f0 && ++n < budget) ;
    return VGA_FRAMES;
}

void vga_set_palette_entry(unsigned int idx, unsigned int r, unsigned int g, unsigned int b)
{
    /* one store per colour: {index, r, g, b} is exactly 32 bits, so a whole palette is 256 stores */
    VGA_PAL = ((idx & 0xFFu) << 24) | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

int vga_set_mode_paletted(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h)
{
    s_pal = 1u;
    return vga_set_mode(mode_id, byte_addr, w, h);
}

int vga_set_mode_565(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h)
{
    s_565 = 1u;
    return vga_set_mode(mode_id, byte_addr, w, h);
}

int vga_set_mode(unsigned int mode_id, unsigned int byte_addr, unsigned int w, unsigned int h)
{
    unsigned int i, sel = VGA_NMODES;
    for (i = 0; i < VGA_NMODES; i++) if (vga_modes[i].id == mode_id) sel = i;
    if (sel == VGA_NMODES) return -1;
    if (w > vga_modes[sel].w || h > vga_modes[sel].h) return -1;    /* the scaler only goes up */
    if (lp_offset(byte_addr) >= VGA_BASE_LIMIT) return -2;          /* beyond the head's base register */

    /* THE ORDER MATTERS AND IT IS THE ONLY THING THAT DOES.  Geometry and base first, then the swap that arms
     * them, then the mode: the head takes base and geometry at the next vblank and resamples the mode at the
     * next frame boundary, so writing them in this order means no frame is ever scanned with one mode's timing
     * and another mode's geometry.  Nothing is blanked and the scan never stops. */
    VGA_FBBASE = lp_offset(byte_addr) >> 1;         /* the head counts 16-bit LPDDR words, from 0 */
    VGA_WH     = (h << 16) | (w & 0x7FFu);
    VGA_SWAP   = 1u;
    VGA_CTRL   = (mode_id << 1) | 1u | (s_pal ? VGA_CTRL_PAL : 0u) | (s_565 ? VGA_CTRL_565 : 0u);
    s_pal = 0u; s_565 = 0u;                                    /* ONE call's worth: vga_set_mode means direct colour.  It stayed set,
                                                     * so the console that `bars' handed the screen back to was scanned
                                                     * as bytes */
    s_now = &vga_modes[sel];
    vga_wait_frame(4000000u);                       /* return with the new mode actually on the wire */
    return 0;
}

void vga_swap(unsigned int byte_addr)
{
    VGA_FBBASE = lp_offset(byte_addr) >> 1;
    VGA_SWAP   = 1u;
}

void vga_off(void) { VGA_CTRL = 0u; s_now = 0; }
