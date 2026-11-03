/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/vgacon.h -- a text console on the VGA output, in the IBM EGA 8x14 font.
 *
 * The screen is set up at start-up and carries every boot, initialisation and enumeration message; it boots at
 * 640x480 and switches to 320x240 for zoom.
 *
 * The font -- rtl/vga/egafont14.hex, the table tools/egafont.c located inside the IBM EGA BIOS ROM and
 * proved by rendering before a byte reached an FPGA, the same bytes the fabric overlay's ROM is initialised
 * with.  The overlay in vga_scan.v is only 64x4 cells, so a whole screen of text is drawn into the framebuffer
 * instead: identical glyphs, no fabric change, works on any bitstream with the head.
 *
 * THE HEAD'S THREE HARD LIMITS, read out of the RTL rather than assumed:
 *   1. base is 22 bits of 16-BIT WORDS (rtl/pano_soc_vex.v: vga_base <= m_mosi[21:0]), so a framebuffer must
 *      live in the FIRST 8 MB of LPDDR.  A buffer above that truncates silently and the head scans elsewhere.
 *   2. a SOURCE line is at most 512 pixels (vga_scan is built with LB_AW=10 in this SoC).
 *   3. the scaler only goes UP, so the source must not exceed the mode.
 *
 * Two geometries follow from that, and the console keeps CHARACTERS, not pixels, so it can redraw either:
 *   BOOT  640x480 from a 512x378 source: 64 columns x 27 rows, a 1.25x stretch -- the DOS shape.
 *   ZOOM  320x240 at a 15.7 kHz line rate from a 320x238 source: 40 x 17, pixel for pixel, nothing scaled.
 *
 * NATIVE 640x480.  Limit 2 is a limit
 * of DIRECT colour.  A PALETTED line is one byte a pixel, two pixels a line-buffer entry, so 640 pixels are 320 entries
 * -- once vga_scan's paletted read index uses the whole buffer, which it does in bitstreams whose VGA_PAL register READS
 * VGA_CAP_PAL1024.  On those, VC_BOOT is 640x480 pixel for pixel: 80 columns x 34 rows of the same 8x14 font (476
 * lines, 4 of border), 16 colours through the head's palette, a 316 KB buffer instead of 803 KB, and two word stores a
 * glyph row instead of eight.  On older bitstreams VC_BOOT is the 512x378 stretch it always was: one image runs on both.
 * Scrolling costs nothing in either: the buffer is twice as tall as the window and the head's base register
 * moves down one text row, exactly as a text card scrolls.  Only at the bottom is anything copied. */
#ifndef VGACON_H
#define VGACON_H
#include <stdint.h>

#define VC_MAXCOLS 80u
#define VC_MAXROWS 34u
#define VC_CW      8u
#define VC_CH      14u

/* the two geometries, by name */
#define VC_BOOT    0u
#define VC_ZOOM    1u

/* EGA text colours, by index: the console stores an attribute per cell so a redraw at another geometry keeps it */
#define VC_BLACK 0u
#define VC_BLUE  1u
#define VC_GREEN 2u
#define VC_CYAN  3u
#define VC_RED   4u
#define VC_MAG   5u
#define VC_BROWN 6u
#define VC_GREY  7u
#define VC_DGREY 8u
#define VC_LBLUE 9u
#define VC_LGREEN 10u
#define VC_LCYAN 11u
#define VC_LRED  12u
#define VC_LMAG  13u
#define VC_YELLOW 14u
#define VC_WHITE 15u

void vgacon_init(unsigned int which, unsigned int fb_byte_addr);  /* VC_BOOT or VC_ZOOM */
int  vgacon_geometry(unsigned int which);       /* switch geometry live; the text is redrawn.  0 ok, <0 refused */
unsigned int vgacon_which(void);
void vgacon_color(unsigned int fg, unsigned int bg);
void vgacon_putc(char c);
void vgacon_puts(const char *s);
void vgacon_hex32(uint32_t v);
void vgacon_dec(uint32_t v);
void vgacon_dec_pad(uint32_t v, unsigned int width);
void vgacon_clear(void);
/* A title bar pinned to the top: left text and right text on row 0 in black on grey, row 1 blank; rows 2.. scroll
 * under it.  Native 640x480 geometry. */
void vgacon_header(const char *left, const char *right);
/* show (1) or hide (0) a block cursor at the output position; any output hides it */
void vgacon_cursor(int on);
void vgacon_gotoxy(unsigned int col, unsigned int row);
void vgacon_progress(const char *what, uint32_t done, uint32_t total, uint32_t kb_per_s);
unsigned int vgacon_ready(void);
unsigned int vgacon_cols(void);
unsigned int vgacon_rows(void);
/* the Amiga's alert, on a machine that has already lost: black screen, blinking red box, cause and PC.
 * Calls nothing that can fail, so a trap handler can use it.  Never returns. */
void vgacon_guru(const char *what, uint32_t cause, uint32_t epc);
void vgacon_mirror(unsigned int delta_bytes);   /* a second frame buffer this many bytes up (the G2's second head); 0 = off */
void vgacon_head_mode(unsigned int m);          /* the head mode (0, 1, 2) the native 640x480 console is shown in */
/* Two buffers in the 32-bit geometry too (call before vgacon_init; the caller provides twice the memory).  A composed
 * frame: vgacon_frame_begin() sends every drawing call to the buffer not on screen, vgacon_frame_end() shows it at the
 * heads' next vertical blank.  Without a second buffer both do nothing. */
void vgacon_double(int on);
void vgacon_frame_begin(void);
void vgacon_frame_end(void);
/* Cells at a position in the current colours, for screens laid out by position: no wrap, no scroll, the output
 * position unchanged.  vgacon_text writes exactly width cells, the string clipped or padded with blanks; vgacon_fill a
 * rectangle of one character (0xC4, 0xB3, 0xDA.. are the font's line-drawing characters, code page 437). */
void vgacon_cell(unsigned int col, unsigned int row, unsigned char ch);
void vgacon_text(unsigned int col, unsigned int row, unsigned int width, const char *s);
void vgacon_fill(unsigned int col, unsigned int row, unsigned int w, unsigned int h, unsigned char ch);
#endif
