/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
#include "vgacon.h"
#include "board.h"
#include "vga.h"
#include "egafont14.h"

#ifdef VGACON_G2_BRAM
/* THE G2 BLOCK-RAM FRAME BUFFER (rtl/vga/vga_bram_fb.v).  Two 640x480 buffers of VGACON_G2_BPP bits a
 * pixel in the CPU window at 0x8200_0000 (buffer 1 directly after buffer 0), leftmost pixel in the least significant
 * field of a byte.  The head reads them as a one-byte-a-pixel paletted source whose buffer 1 starts at byte 307,200,
 * so the swap arithmetic below is unchanged.  The sixteen text colours fold onto the 4 (or 2) palette entries. */
#ifndef VGACON_G2_BPP
#define VGACON_G2_BPP 2u
#endif
#define G2_FB_CPU    0x82000000u
#define G2_BUF_BYTES (640u * 480u * VGACON_G2_BPP / 8u)
#if VGACON_G2_BPP == 2
static const unsigned char g2_c2i[16] = { 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 3, 2, 2, 3, 3 };
static const uint32_t g2_pal[4] = { 0x000000u, 0xAAAAAAu, 0xFF5555u, 0xFFFFFFu };   /* black, grey, light red, white */
#else
static const unsigned char g2_c2i[16] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const uint32_t g2_pal[2] = { 0x000000u, 0xAAAAAAu };                          /* black, grey */
#endif
#endif

/* the sixteen EGA text colours, as the head's 10:10:10 pixel words */
static const uint32_t pal[16] = {
    0x00000000u,                                  /* 0 black   */
    (0x00u<<22)|(0x00u<<12)|(0xAAu<<2),           /* 1 blue    */
    (0x00u<<22)|(0xAAu<<12)|(0x00u<<2),           /* 2 green   */
    (0x00u<<22)|(0xAAu<<12)|(0xAAu<<2),           /* 3 cyan    */
    (0xAAu<<22)|(0x00u<<12)|(0x00u<<2),           /* 4 red     */
    (0xAAu<<22)|(0x00u<<12)|(0xAAu<<2),           /* 5 magenta */
    (0xAAu<<22)|(0x55u<<12)|(0x00u<<2),           /* 6 brown   */
    (0xAAu<<22)|(0xAAu<<12)|(0xAAu<<2),           /* 7 grey    */
    (0x55u<<22)|(0x55u<<12)|(0x55u<<2),           /* 8 dark grey */
    (0x55u<<22)|(0x55u<<12)|(0xFFu<<2),           /* 9 light blue */
    (0x55u<<22)|(0xFFu<<12)|(0x55u<<2),           /* 10 light green */
    (0x55u<<22)|(0xFFu<<12)|(0xFFu<<2),           /* 11 light cyan */
    (0xFFu<<22)|(0x55u<<12)|(0x55u<<2),           /* 12 light red */
    (0xFFu<<22)|(0x55u<<12)|(0xFFu<<2),           /* 13 light magenta */
    (0xFFu<<22)|(0xFFu<<12)|(0x55u<<2),           /* 14 yellow */
    (0xFFu<<22)|(0xFFu<<12)|(0xFFu<<2)            /* 15 white  */
};

/* the geometries: mode id, source pixels, the cells that fit, and whether a pixel is a palette BYTE */
static const struct { unsigned int mode, w, h, cols, rows, bytes; } geom[3] = {
    { 0u, 512u, 378u, 64u, 27u, 0u },  /* VC_BOOT on older bitstreams: 640x480, a 1.25x stretch */
    { 4u, 320u, 238u, 40u, 17u, 0u },  /* VC_ZOOM: 320x240 at 15.7 kHz, pixel for pixel */
    { 0u, 640u, 480u, 80u, 34u, 1u }   /* VC_BOOT where the head can (VGA_CAP_PAL1024): 640x480 NATIVE, paletted */
};
#define G_NATIVE 2u
/* which table entry a request means on THIS bitstream */
static unsigned int pick(unsigned int which)
{
    if (which > VC_ZOOM) which = VC_BOOT;
#ifdef PANO_G2
    return which == VC_ZOOM ? 1u : G_NATIVE;         /* G2: native 640x480 in modes 0-2 (the head scales up), 320x240 in mode 4 */
#endif
    return (which == VC_BOOT && (VGA_PAL & VGA_CAP_PAL1024)) ? G_NATIVE : which;
}

static unsigned char s_ch[VC_MAXROWS][VC_MAXCOLS];   /* what is on the screen, so a redraw needs no pixels */
static unsigned char s_at[VC_MAXROWS][VC_MAXCOLS];   /* fg in the low nibble, bg in the high one */
static volatile uint32_t *s_fb;
static unsigned int s_base, s_which, s_top, s_col, s_row, s_ready;
/* THE MIRROR: a second frame buffer at s_mirror bytes above the first (the G2's second head scans its own 16 MB window,
 * 0x0100_0000 above the first head's), every store repeated there and every base swap issued to both heads. */
static unsigned long s_mirror;
static unsigned int s_head_mode;                  /* the head mode the native geometry is shown in: 0, 1 or 2 */
void vgacon_head_mode(unsigned int m) { s_head_mode = m; }
extern void vga_select(unsigned int head);
void vgacon_mirror(unsigned int delta_bytes) { s_mirror = delta_bytes; }
static void swap_heads(unsigned int byte_addr)
{
    vga_swap(byte_addr);
    if (s_mirror) { vga_select(1u); vga_swap(byte_addr); vga_select(0u); }
}
static unsigned int buf_head(unsigned int i);
static unsigned int s_cur;
/* until every head scans buffer s_cur (each reads back the base it took at its last vertical blank), so the other
 * buffer can be written; at most 400,000 reads a head, longer than one frame */
static void swap_wait(void)
{
    unsigned int n = 0u, want = (buf_head(s_cur) & 0x01FFFFFFu) >> 1;
    while (VGA_SWAP != want && ++n < 400000u) ;
    if (s_mirror) { vga_select(1u); n = 0u; while (VGA_SWAP != want && ++n < 400000u) ; vga_select(0u); }
}
static unsigned int s_w, s_h, s_cols, s_rows, s_lines, s_bytes;
static unsigned char s_fg = VC_GREY, s_bg = VC_BLACK;
/* DOUBLE BUFFER (native paletted geometry; the 32-bit geometry too after vgacon_double(1)): two buffers of exactly
 * one screen, s_base and s_base + one buffer's bytes.
 * s_cur is the one on screen.  A scroll builds the next screen in the other buffer -- the text rows copied up one row,
 * the bottom row cleared -- and hands it to the head at the next vblank, so nothing is ever drawn into the picture
 * being scanned except new characters.  The 32-bit geometry otherwise has one buffer and scrolls in place. */
static unsigned int s_nbuf = 1u;
/* s_dbl (vgacon_double): two buffers in the 32-bit geometry as well, for a caller whose memory has room for them.
 * s_batch (vgacon_frame_begin .. vgacon_frame_end): every drawing call writes the buffer not on screen only. */
static unsigned int s_dbl, s_batch;
/* WHAT EACH BUFFER SHOWS: the character and attribute last drawn into every cell of buffer 0 and buffer 1 (s_fbi is
 * the buffer s_fb points at).  A composed frame changes only the cells (s_ch, s_at); vgacon_frame_end then draws the
 * cells that differ from what the buffer about to be shown already holds.  A selection bar that moves one line costs
 * two rows of glyphs instead of a whole screen (640x480: 76,800 word stores to clear it, 2,720 glyphs to fill it, each
 * repeated for the second head).  s_shok[i] 0: buffer i holds pixels the cells do not describe (a scroll copied rows
 * into it), so the next frame shown from it is drawn whole. */
static unsigned char s_sch[2][VC_MAXROWS][VC_MAXCOLS], s_sat[2][VC_MAXROWS][VC_MAXCOLS];
static unsigned int s_shok[2], s_fbi;
static unsigned int nbuf_for(unsigned int bytes) { return (bytes || s_dbl) ? 2u : 1u; }
/* THE HEADER: rows 0..s_hdr-1 are not scrolled (vgacon_header).  Row 0 is the title bar, row 1 blank. */
static unsigned int s_hdr;
static char s_hleft[VC_MAXCOLS + 1], s_hright[VC_MAXCOLS + 1];

unsigned int vgacon_ready(void) { return s_ready; }
unsigned int vgacon_which(void) { return s_which; }
unsigned int vgacon_cols(void)  { return s_cols; }
unsigned int vgacon_rows(void)  { return s_rows; }
void vgacon_color(unsigned int fg, unsigned int bg) { s_fg = (unsigned char)(fg & 15u); s_bg = (unsigned char)(bg & 15u); }

/* a colour as the framebuffer holds it: the 30-bit word, or the palette index in every byte of a word */
#ifdef VGACON_G2_BRAM
static uint32_t pixword(unsigned int c) { return (uint32_t)g2_c2i[c & 15u] * (VGACON_G2_BPP == 2u ? 0x55555555u : 0xFFFFFFFFu); }
#else
static uint32_t pixword(unsigned int c) { return s_bytes ? (c & 15u) * 0x01010101u : pal[c & 15u]; }
#endif

/* whole lines of one colour.  A paletted line is s_w bytes = s_w/4 words (640 and 320 both divide): ALWAYS word
 * stores -- there is no byte mask on this board, so a byte store costs a whole read-modify-write burst. */
#ifdef VGACON_G2_BRAM
static unsigned int buf_bytes(void) { return s_lines * s_w; }        /* the head's one-byte-a-pixel view */
static volatile uint32_t *buf_cpu(unsigned int i) { return (volatile uint32_t *)(unsigned long)(G2_FB_CPU + i * G2_BUF_BYTES); }
#else
static unsigned int buf_bytes(void) { return s_lines * s_w * (s_bytes ? 1u : 4u); }
/* buffer i as the CPU writes it: the CACHED alias.  The data cache is write-through, so every store still reaches LPDDR
 * for the head, and the scroll's copy reads the screen through the cache at line-refill rate instead of one uncached
 * burst per word.  Only the CPU writes these buffers, so the cache never holds a stale line of them. */
#ifdef PANO_G2
static volatile uint32_t *buf_cpu(unsigned int i) { return (volatile uint32_t *)(unsigned long)(s_base + i * buf_bytes()); }   /* the uncached chip B window */
#else
static volatile uint32_t *buf_cpu(unsigned int i) { return (volatile uint32_t *)(unsigned long)(((s_base + i * buf_bytes()) & 0x0FFFFFFFu) | 0x10000000u); }
#endif
#endif
#ifdef VGACON_G2_BRAM
static unsigned int buf_head(unsigned int i) { return i * buf_bytes(); }
#else
static unsigned int buf_head(unsigned int i) { return s_base + i * buf_bytes(); }
#endif

static void set_fb(unsigned int i) { s_fb = buf_cpu(i); s_fbi = i; }

static void fill(unsigned int line0, unsigned int nlines, uint32_t word)
{
#ifdef VGACON_G2_BRAM
    unsigned long per = s_w * VGACON_G2_BPP / 32u;
#else
    unsigned long per = s_bytes ? s_w / 4u : s_w;
#endif
    volatile uint32_t *p = s_fb + (unsigned long)line0 * per;
    unsigned long n = (unsigned long)nlines * per, i;
    for (i = 0; i < n; i++) p[i] = word;
    if (s_mirror) { volatile uint32_t *q = (volatile uint32_t *)((volatile uint8_t *)p + s_mirror); for (i = 0; i < n; i++) q[i] = word; }
}

/* four glyph bits -> four pixel bytes, leftmost pixel in the LOWEST byte (the head reads bytes in address order) */
static const uint32_t nib[16] = {
    0x00000000u, 0xFF000000u, 0x00FF0000u, 0xFFFF0000u, 0x0000FF00u, 0xFF00FF00u, 0x00FFFF00u, 0xFFFFFF00u,
    0x000000FFu, 0xFF0000FFu, 0x00FF00FFu, 0xFFFF00FFu, 0x0000FFFFu, 0xFF00FFFFu, 0x00FFFFFFu, 0xFFFFFFFFu
};

static void glyph_at(unsigned int col, unsigned int row)
{
    const unsigned char *g = egafont14 + (unsigned int)s_ch[row][col] * VC_CH;
    uint32_t fg = pal[s_at[row][col] & 15u], bg = pal[(s_at[row][col] >> 4) & 15u];
    unsigned int y;
    if (col >= s_cols || row >= s_rows) return;
    if (s_nbuf == 2u) { s_sch[s_fbi][row][col] = s_ch[row][col]; s_sat[s_fbi][row][col] = s_at[row][col]; }
#ifdef VGACON_G2_BRAM
    {   /* a glyph row is 8 pixels: one halfword store at 2 bpp, one byte store at 1 bpp */
        unsigned int fi = g2_c2i[s_at[row][col] & 15u], bi = g2_c2i[(s_at[row][col] >> 4) & 15u], k;
        (void)fg; (void)bg;
        for (y = 0; y < VC_CH; y++) {
            unsigned long px = (unsigned long)(s_top + row * VC_CH + y) * s_w + col * VC_CW;
            unsigned char b = g[y];
            unsigned int v = 0;
            for (k = 0; k < 8u; k++) v |= ((b & (0x80u >> k)) ? fi : bi) << (k * VGACON_G2_BPP);
            if (VGACON_G2_BPP == 2u) *(volatile uint16_t *)((volatile uint8_t *)s_fb + px / 4u) = (uint16_t)v;
            else                     *((volatile uint8_t *)s_fb + px / 8u) = (uint8_t)v;
        }
        return;
    }
#endif
    if (s_bytes) {                                  /* paletted: a glyph row is 8 bytes = two word stores */
        uint32_t fw = pixword(s_at[row][col]), bw = pixword(s_at[row][col] >> 4);
        for (y = 0; y < VC_CH; y++) {
            volatile uint32_t *p = s_fb + ((unsigned long)(s_top + row * VC_CH + y) * s_w + col * VC_CW) / 4u;
            unsigned char b = g[y];
            uint32_t m0 = nib[b >> 4], m1 = nib[b & 15u];
            p[0] = (fw & m0) | (bw & ~m0);
            p[1] = (fw & m1) | (bw & ~m1);
            if (s_mirror) { volatile uint32_t *q = (volatile uint32_t *)((volatile uint8_t *)p + s_mirror); q[0] = p[0]; q[1] = p[1]; }
        }
        return;
    }
    for (y = 0; y < VC_CH; y++) {
        volatile uint32_t *p = s_fb + (unsigned long)(s_top + row * VC_CH + y) * s_w + col * VC_CW;
        unsigned char b = g[y];
        p[0] = (b & 0x80u) ? fg : bg; p[1] = (b & 0x40u) ? fg : bg;
        p[2] = (b & 0x20u) ? fg : bg; p[3] = (b & 0x10u) ? fg : bg;
        p[4] = (b & 0x08u) ? fg : bg; p[5] = (b & 0x04u) ? fg : bg;
        p[6] = (b & 0x02u) ? fg : bg; p[7] = (b & 0x01u) ? fg : bg;
        if (s_mirror) { volatile uint32_t *q = (volatile uint32_t *)((volatile uint8_t *)p + s_mirror); unsigned int k; for (k = 0; k < 8u; k++) q[k] = p[k]; }
    }
}

static void redraw_all(void)
{
    unsigned int r, c;
    fill(0, s_lines, pixword(s_bg));
    for (r = 0; r < s_rows; r++) for (c = 0; c < s_cols; c++) glyph_at(c, r);
    if (s_nbuf == 2u) s_shok[s_fbi] = 1u;
}

int vgacon_geometry(unsigned int which)
{
    unsigned int g, i;
    if (which > VC_ZOOM) return -1;
    s_which = which;
    g = pick(which);
    s_w     = geom[g].w;
    s_h     = geom[g].h;
    s_cols  = geom[g].cols;
    s_rows  = geom[g].rows;
    s_bytes = geom[g].bytes;
    /* ONE WINDOW PLUS ONE ROW, not two windows.  The tall buffer made scrolling a base move and cost 1.5 MB,
     * and everything that shares the first 8 MB with it -- a program's heap and framebuffers -- has to fit
     * around it (a heap that reaches the console's buffer blacks the screen).  803 KB fits
     * with room to spare; a scroll now shifts the cells and redraws, which the boot log does rarely. */
    s_nbuf  = nbuf_for(s_bytes);
    s_lines = s_nbuf == 2u ? s_h : s_h + VC_CH;  /* double buffer: exactly one screen each; single: one spare row */
    s_top   = 0;
    s_cur   = 0;
    s_fb    = s_nbuf == 2u ? buf_cpu(0) : (volatile uint32_t *)(unsigned long)s_base;
    s_fbi   = 0;
    if (s_hdr >= s_rows) s_hdr = 0;
    if (s_row >= s_rows) s_row = s_rows - 1u;
    if (s_col >= s_cols) s_col = 0;
    s_batch = 0;
    redraw_all();
    if (s_nbuf == 2u) {                                              /* the other buffer holds the same screen */
        set_fb(1); redraw_all(); set_fb(0);
    }
    if (s_bytes) {
        /* the 16 colours into the head's table BEFORE it is switched on (it has no reset value), the rest black;
         * every time, because other programs load their own */
#ifdef VGACON_G2_BRAM
        VGA_R(0x24) = 1u;                                            /* the head reads the block-RAM buffers */
        for (i = 0; i < 256u; i++) {
            uint32_t v = i < sizeof g2_pal / sizeof g2_pal[0] ? g2_pal[i] : 0u;
            vga_set_palette_entry(i, (v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu);
        }
#else
        for (i = 0; i < 256u; i++) {
            uint32_t v = i < 16u ? pal[i] : 0u;
            vga_set_palette_entry(i, (v >> 22) & 0xFFu, (v >> 12) & 0xFFu, (v >> 2) & 0xFFu);
        }
#endif
        if (vga_set_mode_paletted(g == G_NATIVE ? s_head_mode : geom[g].mode, buf_head(0), s_w, s_h) != 0) { s_ready = 0; return -2; }
    } else if (vga_set_mode(geom[g].mode, s_base, s_w, s_h) != 0) { s_ready = 0; return -2; }
    s_ready = 1;
    return 0;
}

static void header_cells(void);
void vgacon_clear(void)
{
    unsigned int r, c, i;
    for (r = 0; r < VC_MAXROWS; r++) for (c = 0; c < VC_MAXCOLS; c++) { s_ch[r][c] = ' '; s_at[r][c] = (unsigned char)((s_bg << 4) | s_fg); }
    header_cells();
    s_col = 0; s_row = s_hdr; s_top = 0;
    if (s_batch) return;                            /* a composed frame: the cells only, drawn by frame_end */
    for (i = 0; i < s_nbuf; i++) {
        unsigned int keep = s_fbi;
        if (s_nbuf == 2u) set_fb(i);
        redraw_all();
        if (s_nbuf == 2u) set_fb(keep);
    }
    swap_heads(s_nbuf == 2u ? buf_head(s_cur) : s_base);
}

void vgacon_init(unsigned int which, unsigned int fb_byte_addr)
{
    s_base = fb_byte_addr;
    s_ready = 0; s_col = 0; s_row = 0; s_top = 0; s_cur = 0;
    s_w = geom[pick(which)].w;                       /* so clear() below knows the stride */
    s_h = geom[pick(which)].h;
    s_bytes = geom[pick(which)].bytes;
    s_cols = geom[pick(which)].cols;
    s_rows = geom[pick(which)].rows;
    s_nbuf  = nbuf_for(s_bytes);
    s_lines = s_nbuf == 2u ? s_h : s_h + VC_CH;
    s_fb    = s_nbuf == 2u ? buf_cpu(0) : (volatile uint32_t *)(unsigned long)fb_byte_addr;
    s_fbi   = 0;
    vgacon_clear();
    vgacon_geometry(which);
}

/* ---- the header ----------------------------------------------------------------------------------------- */
static void header_cells(void)
{
    unsigned int c, nl, nr;
    unsigned char bar = (unsigned char)((VC_GREY << 4) | VC_BLACK);
    if (!s_hdr) return;
    nl = 0; while (s_hleft[nl]) nl++;
    nr = 0; while (s_hright[nr]) nr++;
    unsigned int end = s_cols;
    for (c = 0; c < s_cols; c++) { s_ch[0][c] = ' '; s_at[0][c] = bar; }
    for (c = 0; c < nl && c + 1u < end; c++) s_ch[0][c + 1u] = (unsigned char)s_hleft[c];
    for (c = 0; c < nr && nr + 1u <= end; c++) s_ch[0][end - 1u - nr + c] = (unsigned char)s_hright[c];
    for (c = 0; c < s_cols; c++) { s_ch[1][c] = ' '; s_at[1][c] = (unsigned char)((s_bg << 4) | s_fg); }
}

void vgacon_header(const char *left, const char *right)
{
    unsigned int i, c, r;
    for (i = 0; i < VC_MAXCOLS && left && left[i]; i++) s_hleft[i] = left[i];
    s_hleft[i] = 0;
    for (i = 0; i < VC_MAXCOLS && right && right[i]; i++) s_hright[i] = right[i];
    s_hright[i] = 0;
    s_hdr = 2u;
    if (s_row < s_hdr) { s_row = s_hdr; s_col = 0; }
    header_cells();
    if (!s_ready) return;
    if (s_batch) return;                                /* a composed frame: drawn by frame_end */
    for (i = 0; i < s_nbuf; i++) {                      /* the header is the same in both buffers */
        unsigned int keep = s_fbi;
        if (s_nbuf == 2u) set_fb(i);
        for (r = 0; r < s_hdr; r++) for (c = 0; c < s_cols; c++) glyph_at(c, r);
        if (s_nbuf == 2u) set_fb(keep);
    }
}

void vgacon_gotoxy(unsigned int col, unsigned int row)
{
    if (col < s_cols) s_col = col;
    if (row < s_rows) s_row = row;
}

/* the window moves down one text row.  The cell array shifts with it, so a later geometry change redraws the
 * text that is on screen and not what used to be there. */
static void cursor_hide(void);
static void scroll(void)
{
    cursor_hide();
    unsigned int r, c;
    for (r = s_hdr + 1u; r < s_rows; r++) for (c = 0; c < s_cols; c++) { s_ch[r-1][c] = s_ch[r][c]; s_at[r-1][c] = s_at[r][c]; }
    for (c = 0; c < s_cols; c++) { s_ch[s_rows-1][c] = ' '; s_at[s_rows-1][c] = (unsigned char)((s_bg << 4) | s_fg); }

    if (s_batch) return;                            /* a composed frame: drawn from the cells by frame_end */
    if (s_nbuf == 2u) {
        /* the next screen, built off screen.  The previous swap must have been taken (the head reads back the base
         * it is scanning) before the buffer it left is written: that buffer is the one about to be drawn into. */
        unsigned int nxt = s_cur ^ 1u;
#ifdef VGACON_G2_BRAM
        unsigned long rowb = (unsigned long)VC_CH * s_w * VGACON_G2_BPP / 8u;  /* bytes in one text row */
#else
        unsigned long rowb = (unsigned long)VC_CH * s_w * (s_bytes ? 1u : 4u); /* bytes in one text row */
#endif
        volatile uint32_t *src = buf_cpu(s_cur), *dst = buf_cpu(nxt);
        unsigned long i, w0 = (unsigned long)(s_hdr + 1u) * rowb / 4u, w1 = (unsigned long)s_rows * rowb / 4u, up = rowb / 4u;
        unsigned long wh = (unsigned long)s_hdr * rowb / 4u;
        swap_wait();
        for (i = 0; i < wh; i++) dst[i] = src[i];                             /* the header rows in place: after a
                                                                                 composed frame the other buffer is older */
        for (i = w0; i < w1; i++) dst[i - up] = src[i];                       /* text rows s_hdr+1.. -> s_hdr.. */
        if (s_mirror) {
            volatile uint32_t *s2 = (volatile uint32_t *)((volatile uint8_t *)src + s_mirror), *d2 = (volatile uint32_t *)((volatile uint8_t *)dst + s_mirror);
            for (i = 0; i < wh; i++) d2[i] = s2[i];
            for (i = w0; i < w1; i++) d2[i - up] = s2[i];
        }
        set_fb(nxt); s_shok[nxt] = 0u;                                        /* rows copied: not what its cells say */
        fill((s_rows - 1u) * VC_CH, VC_CH, pixword(s_bg));                    /* the new bottom row */
        s_cur = nxt;
        swap_heads(buf_head(s_cur));
        return;
    }
    if (s_top + s_h + VC_CH <= s_lines) {
        s_top += VC_CH;                        /* the one spare row: a base move, free */
        fill(s_top + s_h - VC_CH, VC_CH, pixword(s_bg));
        swap_heads(s_base + s_top * s_w * (s_bytes ? 1u : 4u));
    } else {
        s_top = 0;                             /* and then redraw from the cells, which is why we keep them */
        redraw_all();
        swap_heads(s_base);
    }
}

/* COMPOSED FRAMES.  frame_begin waits until the heads scan the current buffer (the previous swap taken), then points
 * every drawing call at the other one; frame_end hands that buffer to the heads, which take it at their next vertical
 * blank (vga_swap), so a whole screen changes between two scanned frames and no partial drawing is ever shown.  The
 * buffer left behind holds the older screen: drawing outside a frame goes to the screen on show, and a scroll copies
 * every text row, header included, from it.  With one buffer both calls do nothing and drawing is direct. */
void vgacon_double(int on) { s_dbl = on ? 1u : 0u; }
void vgacon_frame_begin(void)
{
    if (!s_ready || s_nbuf != 2u || s_batch) return;
    cursor_hide();
    swap_wait();
    s_batch = 1u;
    set_fb(s_cur ^ 1u);
}
void vgacon_frame_end(void)
{
    unsigned int r, c, b;
    if (!s_batch) return;
    s_batch = 0u;
    b = s_fbi;
    if (!s_shok[b]) redraw_all();
    else for (r = 0; r < s_rows; r++) for (c = 0; c < s_cols; c++)
        if (s_ch[r][c] != s_sch[b][r][c] || s_at[r][c] != s_sat[b][r][c]) glyph_at(c, r);
    s_cur = b;
    swap_heads(buf_head(s_cur));
}

/* THE CURSOR: the cell under it drawn with foreground and background swapped.  It is taken down before anything else is
 * drawn, so output never has to know about it. */
static unsigned int s_cur_on, s_cur_c, s_cur_r;
static void cursor_hide(void)
{
    if (!s_cur_on) return;
    s_cur_on = 0;
    if (s_cur_r < s_rows && s_cur_c < s_cols) glyph_at(s_cur_c, s_cur_r);
}
void vgacon_cursor(int on)
{
    cursor_hide();
    if (!on || !s_ready || s_col >= s_cols) return;
    {
        unsigned char at = s_at[s_row][s_col];
        s_at[s_row][s_col] = (unsigned char)(((at & 15u) << 4) | (at >> 4));
        glyph_at(s_col, s_row);
        s_at[s_row][s_col] = at;
    }
    s_cur_on = 1; s_cur_c = s_col; s_cur_r = s_row;
}

void vgacon_putc(char c)
{
    if (!s_ready) return;
    cursor_hide();
    if (c == '\r') { s_col = 0; return; }
    if (c == '\n') { s_col = 0; if (s_row + 1u < s_rows) s_row++; else scroll(); return; }
    if (c == '\b') { if (s_col) s_col--; return; }
    if (c == '\t') { do { vgacon_putc(' '); } while (s_col & 7u); return; }
    if (s_col >= s_cols) { s_col = 0; if (s_row + 1u < s_rows) s_row++; else scroll(); }
    s_ch[s_row][s_col] = (unsigned char)c;
    s_at[s_row][s_col] = (unsigned char)((s_bg << 4) | s_fg);
    if (!s_batch) glyph_at(s_col, s_row);
    s_col++;
}

/* ---- cells at a position: never wrapped, never scrolled ------------------------------------------------------ */
static void cell(unsigned int col, unsigned int row, unsigned char ch)
{
    if (col >= s_cols || row >= s_rows) return;
    s_ch[row][col] = ch; s_at[row][col] = (unsigned char)((s_bg << 4) | s_fg);
    if (!s_batch) glyph_at(col, row);
}
void vgacon_cell(unsigned int col, unsigned int row, unsigned char ch) { if (s_ready) { cursor_hide(); cell(col, row, ch); } }
void vgacon_text(unsigned int col, unsigned int row, unsigned int width, const char *s)
{
    unsigned int i;
    if (!s_ready) return;
    cursor_hide();
    for (i = 0; i < width && col + i < s_cols; i++) cell(col + i, row, (unsigned char)(s && *s ? *s++ : ' '));
}
void vgacon_fill(unsigned int col, unsigned int row, unsigned int w, unsigned int h, unsigned char ch)
{
    unsigned int r, c;
    if (!s_ready) return;
    cursor_hide();
    for (r = row; r < row + h && r < s_rows; r++) for (c = col; c < col + w && c < s_cols; c++) cell(c, r, ch);
}

void vgacon_puts(const char *s) { while (*s) vgacon_putc(*s++); }

void vgacon_hex32(uint32_t v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) vgacon_putc(h[(v >> i) & 0xFu]);
}
void vgacon_dec(uint32_t v)
{
    char b[12]; int i = 11; b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10u); v /= 10u; }
    vgacon_puts(b + i);
}
void vgacon_dec_pad(uint32_t v, unsigned int width)
{
    char b[12]; int i = 11, n; b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10u); v /= 10u; }
    for (n = 11 - i; (unsigned int)n < width; n++) vgacon_putc(' ');
    vgacon_puts(b + i);
}

/* one line rewritten in place; the bar is sized to whatever geometry is current */
void vgacon_progress(const char *what, uint32_t done, uint32_t total, uint32_t kb_per_s)
{
    unsigned int pct = total ? (unsigned int)((unsigned long long)done * 100ull / total) : 0u;
    /* round UP so the last cell fills at 100 %: pct*width/100 truncates, and a bar one cell short of the end
     * reads as a failure even when the transfer succeeded */
    unsigned int width = s_cols > 34u ? 24u : 12u;
    unsigned int fill_n = (pct * width + 99u) / 100u, i;
    unsigned int row = s_row;
    s_col = 0;
    vgacon_puts(what); vgacon_putc(' '); vgacon_putc('[');
    for (i = 0; i < width; i++) vgacon_putc(i < fill_n ? (char)0xDBu : (char)0xB0u);
    vgacon_puts("] "); vgacon_dec_pad(pct, 3); vgacon_puts("% ");
    vgacon_dec(kb_per_s / 1024u); vgacon_putc('.'); vgacon_dec((kb_per_s % 1024u) * 10u / 1024u);
    vgacon_puts(" MB/s  ");
    s_col = 0; s_row = row;          /* stay put: the next call overwrites this line */
}

/* ---- THE GURU MEDITATION: a trap or error on a black screen inside a blinking red box ------------------------
 *
 * The Amiga's alert said everything in two lines and a blinking red rectangle, and it said it on a machine
 * that had already lost: no allocator, no file system, nothing to call.  This one holds to that -- it writes
 * the framebuffer directly and calls nothing that can fail, so it works from a trap handler with the machine
 * in whatever state broke it.  If the head was never brought up it brings it up first; if it was, the mode and
 * geometry are already right and the screen simply goes black.
 *
 * The blink is the border only, as the Amiga's was, on the same roughly half-second cadence. */
static void guru_box(unsigned int on)
{
    unsigned int c, r;
    unsigned int r0 = 1u, r1 = 5u, c0 = 1u, c1 = s_cols - 2u;
    unsigned char at = (unsigned char)((VC_BLACK << 4) | (on ? VC_LRED : VC_BLACK));
    for (c = c0; c <= c1; c++) {
        s_ch[r0][c] = 0xDBu; s_at[r0][c] = at; glyph_at(c, r0);
        s_ch[r1][c] = 0xDBu; s_at[r1][c] = at; glyph_at(c, r1);
    }
    for (r = r0; r <= r1; r++) {
        s_ch[r][c0] = 0xDBu; s_at[r][c0] = at; glyph_at(c0, r);
        s_ch[r][c1] = 0xDBu; s_at[r][c1] = at; glyph_at(c1, r);
    }
}

void vgacon_guru(const char *what, uint32_t cause, uint32_t epc)
{
    unsigned int on = 1u, i;
    if (!s_ready) vgacon_init(VC_BOOT, 0x90600000u);     /* nothing was on the screen: put something there */
    s_batch = 0u;                                        /* a trap inside a composed frame: draw on the screen shown */
    if (s_nbuf == 2u) set_fb(s_cur);
    s_fg = VC_LRED; s_bg = VC_BLACK;
    vgacon_clear();

    vgacon_color(VC_LRED, VC_BLACK);
    vgacon_gotoxy(4u, 2u); vgacon_puts("Software Failure.  Press RESET to continue.");
    vgacon_gotoxy(4u, 3u); vgacon_puts("Guru Meditation #");
    vgacon_hex32(cause); vgacon_putc('.'); vgacon_hex32(epc);
    vgacon_color(VC_GREY, VC_BLACK);
    vgacon_gotoxy(4u, 7u); vgacon_puts(what ? what : "unhandled exception");
    vgacon_gotoxy(4u, 8u); vgacon_puts("mcause "); vgacon_hex32(cause);
    vgacon_puts("   mepc "); vgacon_hex32(epc);
    vgacon_gotoxy(4u, 9u);
    vgacon_puts(cause == 2u ? "illegal instruction -- the fetch came back wrong, or the code did" :
                cause == 1u ? "instruction access fault" :
                cause == 4u ? "load address misaligned" :
                cause == 5u ? "load access fault" :
                cause == 6u ? "store address misaligned" :
                cause == 7u ? "store access fault" : "see the RISC-V mcause table");

    /* and it blinks, for ever, exactly as it should */
    for (;;) {
        guru_box(on);
        on ^= 1u;
        for (i = 0; i < 240u * PANO_CYC_PER_MS; i++) __asm__ volatile ("" ::: "memory");   /* ~2 cycles an iteration: ~0.5 s */
    }
}
