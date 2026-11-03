/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* term.c -- a VT100/VT220 terminal on a screen of its own: libvterm 0.3.3's state layer (amiga/third_party/libvterm,
 * Paul Evans, MIT) parses the byte stream and keeps the cursor, modes, pen and scroll region; this file keeps the
 * cells, draws them and scrolls the display.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 * 1 Two presentations, switched with Right Amiga F (the cells are kept; the terminal takes the new size):
 *   Full screen: a screen of its own in the host screen's display mode and size, 4 bitplanes with the 16 ANSI
 *   colours, no title bar, a borderless backdrop window for the keys, the pointer blanked; smooth scrolling (2.).
 *   Window: a sizeable window on the host screen (serialmon's), its size from 20 x 4 cells to the screen; the
 *   ANSI colours drawn in the host's pens (the opener's table); the lines are drawn off screen as in 2. and blitted
 *   into the window: the rows drawn since the last frame, or all of them after a scroll, at most every second frame
 *   (jump scrolling: a frame shows the scrolls since the last at once).
 *   Cells of 8 x 8 pixels: the ROM topaz 8 glyphs for code points 0x20-0xFF, glyphs made here for the DEC special
 *   graphics (box drawing, blocks, symbols).
 *
 * 2 Lines in a ring.  Line i of the terminal's history is kept in slot i mod N (N = three times the screen's
 *   rows, at least the rows plus 25), drawn at bitmap text line `slot`; the bitmap has N + rows text lines, and the slots below
 *   `rows` are drawn again at `slot + N` (the mirror: cells written to both copies, blitter moves copied), so the
 *   screen's rows are contiguous in
 *   the bitmap from any slot on.  The display shows the rows from
 *   pixel line D mod 8N: a whole-screen scroll by n lines only moves the target T = 8 x (index of the top line), and
 *   D follows T by a few pixel lines a frame (smooth scrolling); the lines scrolled out stay in their slots while the
 *   display passes over them.  D never trails T by more than 8 (N - rows) pixel lines: beyond that it jumps.
 *   Scrolls of part of the screen (DECSTBM regions, inserted and deleted lines and characters) move the slots'
 *   pixels and cells with the blitter and memmove.
 *
 * 3 Cells and drawing.  Each slot holds its cells (glyph, colours, underline).  libvterm's putglyph stores a cell and
 *   marks its columns; erased cells are blanked in the cell array and marked the same way, so each line is written
 *   once; at the end of each block of input the marked columns are drawn four cells at a time with
 *   32-bit writes: per glyph row the four glyph bytes form a longword G, and each plane gets (G & F) | (~G & B), F and
 *   B holding 0xFF in the bytes of the cells whose foreground or background colour has that plane's bit; four cells
 *   of one colour pair (the common case) go through draw4.s, which keeps the glyph rows in registers.  The cursor
 *   is its cell inverted (XOR of all planes in both copies), taken off
 *   at the start of a tick and put back at its end.
 *
 * 3b Double width and height.  libvterm's setlineinfo gives each row's DECDWL / DECDHL state; it is kept per slot
 *   (0 normal, 1 double width, 2 double-height upper half, 3 lower half), moved with the line by scrolls, and such a
 *   line is drawn here in C: its first cols/2 cells 16 pixels wide (each glyph bit doubled), the halves of a
 *   double-height line from the glyph's upper or lower four rows, each twice.  The cursor covers both bytes.
 *
 * 3a Scrollback.  Lines that a scroll moves off the top of the screen (a full-width scroll region starting at row 0,
 *   as libvterm's screen layer pushes them) are copied into a history of 256 KB of cells (819 lines at 80 columns),
 *   oldest overwritten.  Shift Up and Shift Down page through it (Intuition keeps Amiga with the cursor keys for the
 *   pointer): the screen's rows are drawn from the history and the
 *   screen's own lines, input waits in the queue, the bottom row shows the place in reverse; any other key goes back
 *   to the screen (redrawn from its cells) and is sent.  A new terminal size empties the history.
 *
 * 4 Characters.  As a VT220 the terminal takes 8-bit bytes: C1 controls 0x80-0x9F become ESC and the 7-bit final
 *   (the same control), 0xA0-0xFF are ISO 8859-1; libvterm runs in UTF-8 and gets them so encoded.  With
 *   term_set_utf8 the bytes pass unchanged (a UTF-8 host).
 *
 * 4a Plain text.  A run of printable ASCII bytes goes into the cells here, not through libvterm, when its parser is
 *   in the ground state and the state's text path would only place glyphs: no single shift, G0 the ASCII set or the
 *   UTF-8 decoder with no sequence open, insert
 *   mode off, no pending wrap, a single-width line; the run stops one column before the right margin, where the
 *   wrap rules apply, and the rest goes to libvterm.  The cursor column and the last glyph (for a combining mark that
 *   may follow) are set in libvterm's state as its own text path sets them.  This reads libvterm 0.3.3's internal
 *   structures (src/vterm_internal.h of the vendored version).
 *
 * 5 Pacing.  term_tick runs once a display frame: input is worked in 64 bytes at a time while the time used plus
 *   the cells marked at 25 us each (the measured cost of draw4.s) stays under 85 % of the budget, then the marked
 *   cells are drawn once (marks belong to slots, so scrolls between the blocks do not move them), the mirrors
 *   copied, the view offset set (ScrollVPort) or the window's rows blitted, and the cursor blinked. */
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/timer.h>
#include "vterm.h"
#include "vterm_internal.h"                              /* the plain-text path (4a) */
#include "term.h"

extern struct Device *TimerBase;                         /* opened by the program (timer.device) */
struct Library *KeymapBase;

#define EXTRA_MIN 25                                     /* ring lines beyond the screen's rows, at least */
#define ROWS_MAX  128
#define COLS_MAX  160
#define QSIZE     65536u
#define G_EXTRA   40                                     /* glyphs made here, after the 256 of topaz */

typedef struct { UWORD g; UBYTE col, ul; } cell_t;     /* glyph index, fg | bg << 4, underline */

struct term {
    struct Screen  *host;                                /* the opener's screen */
    struct Screen  *scr;                                 /* full screen: the terminal's own */
    struct Window  *win;
    int             full;                                /* 1 full screen, 0 window */
    int             jump;                                /* full screen: the view set to its target at once */
    int             toggle, resized;                     /* asked for by a key or a new window size */
    ULONG           req;                                 /* requests to the opener (term_requests) */
    int             wx, wy, ww, wh;                      /* the window's box, kept across full screen */
    UBYTE           wpen[16];                            /* window: the host's pen for each ANSI colour */
    UBYTE           cmap[256];                           /* cell colours (fg | bg << 4) to pens, this presentation */
    UBYTE          *shw;                                 /* window: slots drawn since the last frame */
    UBYTE          *lat;                                 /* each slot's line attribute (3b) */
    cell_t         *hist;                                /* scrollback (3a): hcap lines of cols cells */
    int             hcap, hcols, hcount, hhead, sb, sb_drawn;   /* lines, their width, held, next written, back, drawn */
    LONG            ptop;                                /* window: the top line of the last frame */
    ULONG           ticks_full;                          /* window: ticks with a scroll to show */
    struct BitMap  *bm;
    UBYTE          *pl[4];
    int             bpr, cols, rows, n;                  /* bytes a bitmap line, cells, ring lines */
    VTerm          *vt;
    VTermState     *vs;
    LONG            top;                                 /* history index of the screen's row 0 */
    LONG            tgt, shown;                          /* T and D, pixel lines (2.) */
    cell_t         *cells;                               /* n slots of cols cells */
    WORD           *dlo, *dhi;                           /* each slot's columns to draw (dlo > dhi: none) */
    UBYTE          *mir;                                 /* slots whose mirror is to be copied */
    int             crow, ccol, cvis, con, cslot, ccl;   /* cursor; con: drawn (in slot cslot, column ccl) */
    ULONG           blink;
    int             pfg, pbg, pbold, pul, prev, pcon;    /* the pen */
    UBYTE           pcol;                                /* its cell colours (pen_col) */
    ULONG           pend;                                /* cells marked and not yet drawn */
    VTermEncoding  *ascii, *utf8enc;                     /* libvterm's ASCII set and UTF-8 decoder (4a) */
    int             utf8;
    UBYTE           glyph[256 + G_EXTRA][8];
    UBYTE          *q;                                   /* the input queue */
    ULONG           qr, qw;
    term_out_fn     out;
    void           *user;
    UWORD          *blank_ptr;
    term_stats_t    st;
};

static const UWORD ansi[16] = {                          /* 0RGB, 4 bits a gun */
    0x000, 0xA00, 0x0A0, 0xA50, 0x00A, 0xA0A, 0x0AA, 0xAAA,
    0x555, 0xF55, 0x5F5, 0xFF5, 0x55F, 0xF5F, 0x5FF, 0xFFF
};

static unsigned long long now_e(void) { struct EClockVal v; ReadEClock(&v); return ((unsigned long long)v.ev_hi << 32) | v.ev_lo; }

/* ---- glyphs ---- */

/* the DEC special graphics and block characters libvterm hands over as Unicode, drawn on the 8 x 8 cell */
static const ULONG g_cp[G_EXTRA] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C,
    0x2592, 0x25C6, 0x2264, 0x2265, 0x03C0, 0x2260, 0x23BA, 0x23BB, 0x23BC, 0x23BD,
    0x2588, 0x2580, 0x2584, 0x258C, 0x2590, 0x2591, 0x2593, 0x2409, 0x240C, 0x240D, 0x240A, 0x2424, 0x240B,
    0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0x25AE
};

static void make_glyphs(term_t *t)
{
    static const char *const two[] = { "HT", "FF", "CR", "LF", "NL", "VT" };
    static const ULONG cps[] = { 0x2409, 0x240C, 0x240D, 0x240A, 0x2424, 0x240B };
    static const struct { ULONG cp; UBYTE d[8]; } pix[] = {
        { 0x25C6, { 0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00 } },
        { 0x2264, { 0x0C, 0x30, 0xC0, 0x30, 0x0C, 0x00, 0xFC, 0x00 } },
        { 0x2265, { 0xC0, 0x30, 0x0C, 0x30, 0xC0, 0x00, 0xFC, 0x00 } },
        { 0x03C0, { 0x00, 0x00, 0xFE, 0x6C, 0x6C, 0x6C, 0x6C, 0x00 } },
        { 0x2260, { 0x00, 0x0C, 0xFE, 0x30, 0xFE, 0x60, 0x00, 0x00 } },
        { 0x2190, { 0x00, 0x20, 0x60, 0xFE, 0x60, 0x20, 0x00, 0x00 } },
        { 0x2192, { 0x00, 0x08, 0x0C, 0xFE, 0x0C, 0x08, 0x00, 0x00 } },
        { 0x2191, { 0x10, 0x38, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x00 } },
        { 0x2193, { 0x10, 0x10, 0x10, 0x10, 0x7C, 0x38, 0x10, 0x00 } },
        { 0x25AE, { 0x7C, 0x7C, 0x7C, 0x7C, 0x7C, 0x7C, 0x7C, 0x00 } },
    };
    int i, r, k;
    for (i = 0; i < G_EXTRA; i++) {
        UBYTE *g = t->glyph[256 + i];
        ULONG c = g_cp[i];
        int up = 0, dn = 0, lf = 0, rt = 0;
        memset(g, 0, 8);
        switch (c) {
        case 0x2500: lf = rt = 1; break;
        case 0x2502: up = dn = 1; break;
        case 0x250C: dn = rt = 1; break;
        case 0x2510: dn = lf = 1; break;
        case 0x2514: up = rt = 1; break;
        case 0x2518: up = lf = 1; break;
        case 0x251C: up = dn = rt = 1; break;
        case 0x2524: up = dn = lf = 1; break;
        case 0x252C: lf = rt = dn = 1; break;
        case 0x2534: lf = rt = up = 1; break;
        case 0x253C: up = dn = lf = rt = 1; break;
        case 0x2592: for (r = 0; r < 8; r++) g[r] = (r & 1) ? 0x55 : 0xAA; break;
        case 0x2591: for (r = 0; r < 8; r++) g[r] = (r & 1) ? 0x44 : 0x11; break;
        case 0x2593: for (r = 0; r < 8; r++) g[r] = (r & 1) ? 0x77 : 0xDD; break;
        case 0x2588: memset(g, 0xFF, 8); break;
        case 0x2580: memset(g, 0xFF, 4); break;
        case 0x2584: memset(g + 4, 0xFF, 4); break;
        case 0x258C: memset(g, 0xF0, 8); break;
        case 0x2590: memset(g, 0x0F, 8); break;
        case 0x23BA: g[0] = 0xFF; break;
        case 0x23BB: g[2] = 0xFF; break;
        case 0x23BC: g[5] = 0xFF; break;
        case 0x23BD: g[7] = 0xFF; break;
        case 0x2022: g[3] = g[4] = 0x18; break;
        default:
            for (k = 0; k < (int)(sizeof pix / sizeof pix[0]); k++) if (pix[k].cp == c) memcpy(g, pix[k].d, 8);
            for (k = 0; k < 6; k++) if (cps[k] == c) {   /* HT FF CR LF NL VT: two small letters, one above the other */
                for (r = 0; r < 4; r++) g[r] = (UBYTE)(t->glyph[(UBYTE)two[k][0]][r * 2] & 0xF0);
                for (r = 0; r < 4; r++) g[r + 4] = (UBYTE)((t->glyph[(UBYTE)two[k][1]][r * 2] >> 4) & 0x0F);
            }
            break;
        }
        if (lf) g[3] |= 0xF8;                            /* lines through the cell's middle (row 3, columns 3-4) */
        if (rt) g[3] |= 0x1F;
        for (r = 0; r < 8; r++) if ((up && r <= 3) || (dn && r >= 3)) g[r] |= 0x18;
    }
}

/* topaz 8 from the ROM: the strike's bits of each character into 8 bytes */
static int load_topaz(term_t *t)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, FPF_ROMFONT };
    struct TextFont *f = OpenFont(&ta);
    int c, r;
    if (!f) return 0;
    memset(t->glyph, 0, sizeof t->glyph);
    for (c = f->tf_LoChar; c <= f->tf_HiChar && c < 256; c++) {
        ULONG loc = ((ULONG *)f->tf_CharLoc)[c - f->tf_LoChar];
        UWORD off = (UWORD)(loc >> 16), wid = (UWORD)loc;
        if (wid > 8) wid = 8;
        for (r = 0; r < 8 && r < f->tf_YSize; r++) {
            const UBYTE *row = (const UBYTE *)f->tf_CharData + r * f->tf_Modulo;
            UBYTE b = 0;
            int x;
            for (x = 0; x < wid; x++) if (row[(off + x) >> 3] & (0x80 >> ((off + x) & 7))) b |= (UBYTE)(0x80 >> x);
            t->glyph[c][r] = b;
        }
    }
    CloseFont(f);
    return 1;
}

static UWORD glyph_index(ULONG cp)
{
    int i;
    if (cp < 256) return (UWORD)cp;
    for (i = 0; i < G_EXTRA; i++) if (g_cp[i] == cp) return (UWORD)(256 + i);
    return (UWORD)'?';
}

/* ---- colours ---- */

static int col16(term_t *t, VTermColor c, int fg)
{
    int i, best = 0;
    ULONG bd = ~0ul;
    if (fg ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c)) return fg ? 7 : 0;
    if (VTERM_COLOR_IS_INDEXED(&c) && c.indexed.idx < 16) return c.indexed.idx;
    vterm_state_convert_color_to_rgb(t->vs, &c);
    for (i = 0; i < 16; i++) {                           /* the nearest of the 16 */
        int r = ((ansi[i] >> 8) & 15) * 17 - c.rgb.red, g = ((ansi[i] >> 4) & 15) * 17 - c.rgb.green,
            b = (ansi[i] & 15) * 17 - c.rgb.blue;
        ULONG d = (ULONG)(r * r + g * g + b * b);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* the pen's cell colours: bold brightens 0-7, reverse swaps, conceal hides */
static UBYTE pen_col(const term_t *t)
{
    int fg = t->pfg, bg = t->pbg, x;
    if (t->pbold && fg < 8) fg += 8;
    if (t->prev) { x = fg; fg = bg; bg = x; }
    if (t->pcon) fg = bg;
    return (UBYTE)(fg | bg << 4);
}

/* the erase colour: the pen's background (reverse: its foreground) */
static int erase_bg(const term_t *t) { return t->prev ? t->pfg : t->pbg; }

/* ---- the ring ---- */

static int slot_of(const term_t *t, int row) { LONG i = (t->top + row) % t->n; return (int)(i < 0 ? i + t->n : i); }
static cell_t *cells_of(const term_t *t, int slot) { return t->cells + slot * t->cols; }

static void mark(term_t *t, int slot, int lo, int hi)
{
    int was = t->dlo[slot] <= t->dhi[slot] ? t->dhi[slot] - t->dlo[slot] + 1 : 0;
    if (lo < t->dlo[slot]) t->dlo[slot] = (WORD)lo;
    if (hi > t->dhi[slot]) t->dhi[slot] = (WORD)hi;
    t->pend += (ULONG)(t->dhi[slot] - t->dlo[slot] + 1 - was);   /* cells to draw, for the tick's budget */
}

/* draw4.s: four cells of one colour pair (the common case), from registers */
void term_draw4(UBYTE **pl, LONG off, LONG bpr, const UBYTE *g0, const UBYTE *g1, const UBYTE *g2, const UBYTE *g3,
                ULONG col, ULONG ul, LONG off2);

/* columns lo..hi of slot drawn: four cells (a longword of each plane line) at a time */
static void draw_cells(term_t *t, int slot, const cell_t *c, int lo, int hi)
{
    int g0 = lo & ~3, p, r, k, mirror = slot < t->rows;    /* a slot with a mirror is written twice (2.) */
    LONG mo = (LONG)t->n * 8 * t->bpr;                      /* the mirror's distance */
    for (; g0 <= hi && g0 < t->cols; g0 += 4) {
        if (g0 + 3 < t->cols && c[g0].col == c[g0 + 1].col && c[g0].col == c[g0 + 2].col && c[g0].col == c[g0 + 3].col &&
            c[g0].ul == c[g0 + 1].ul && c[g0].ul == c[g0 + 2].ul && c[g0].ul == c[g0 + 3].ul) {
            term_draw4(t->pl, (LONG)(slot * 8 * t->bpr + g0), t->bpr, t->glyph[c[g0].g], t->glyph[c[g0 + 1].g],
                       t->glyph[c[g0 + 2].g], t->glyph[c[g0 + 3].g], t->cmap[c[g0].col], c[g0].ul,
                       mirror ? (LONG)(slot * 8 * t->bpr + g0) + mo : 0);
            t->st.cells += 4;
        } else if (g0 + 3 < t->cols) {
            ULONG F[4] = { 0, 0, 0, 0 }, B[4] = { 0, 0, 0, 0 }, U = 0;
            const UBYTE *g[4];
            ULONG *d[4];
            for (k = 0; k < 4; k++) {
                const cell_t *e = c + g0 + k;
                ULONG m = 0xFF000000ul >> (k * 8);
                UBYTE cl = t->cmap[e->col];
                g[k] = t->glyph[e->g];
                for (p = 0; p < 4; p++) {
                    if ((cl >> p) & 1) F[p] |= m;
                    if ((cl >> (p + 4)) & 1) B[p] |= m;
                }
                if (e->ul) U |= m;
            }
            for (p = 0; p < 4; p++) d[p] = (ULONG *)(t->pl[p] + slot * 8 * t->bpr + g0);
            for (r = 0; r < 8; r++) {
                ULONG G = ((ULONG)g[0][r] << 24) | ((ULONG)g[1][r] << 16) | ((ULONG)g[2][r] << 8) | g[3][r], N;
                if (r == 7) G |= U;
                N = ~G;
                for (p = 0; p < 4; p++) {
                    ULONG v = (G & F[p]) | (N & B[p]);
                    *d[p] = v;
                    if (mirror) *(ULONG *)((UBYTE *)d[p] + mo) = v;
                    d[p] = (ULONG *)((UBYTE *)d[p] + t->bpr);
                }
            }
            t->st.cells += 4;
        } else {                                         /* the last, short group: a byte at a time */
            for (k = g0; k < t->cols; k++) {
                const cell_t *e = c + k;
                const UBYTE *gl = t->glyph[e->g];
                UBYTE cl = t->cmap[e->col];
                for (p = 0; p < 4; p++) {
                    UBYTE *d = t->pl[p] + slot * 8 * t->bpr + k;
                    UBYTE fm = ((cl >> p) & 1) ? 0xFF : 0, bm = ((cl >> (p + 4)) & 1) ? 0xFF : 0;
                    for (r = 0; r < 8; r++, d += t->bpr) {
                        UBYTE gg = (UBYTE)(r == 7 && e->ul ? 0xFF : gl[r]);
                        *d = (UBYTE)((gg & fm) | (~gg & bm));
                        if (mirror) d[mo] = *d;
                    }
                }
                t->st.cells++;
            }
        }
    }
}

/* a double-width or double-height line (3b): the whole slot */
static void draw_wide(term_t *t, int slot)
{
    static UWORD wide[256];
    const cell_t *c = cells_of(t, slot);
    int k, p, r, a = t->lat[slot], mirror = slot < t->rows;
    LONG mo = (LONG)t->n * 8 * t->bpr;
    if (!wide[1]) {                                      /* each bit doubled: 8 pixels into 16 */
        for (k = 0; k < 256; k++) { UWORD w = 0; for (p = 0; p < 8; p++) if (k & (1 << p)) w |= (UWORD)(3u << (2 * p)); wide[k] = w; }
    }
    for (k = 0; k < t->cols / 2; k++) {
        const UBYTE *g = t->glyph[c[k].g];
        UBYTE cl = t->cmap[c[k].col];
        for (p = 0; p < 4; p++) {
            UBYTE *d = t->pl[p] + slot * 8 * t->bpr + 2 * k;
            UWORD fm = ((cl >> p) & 1) ? 0xFFFF : 0, bm = ((cl >> (p + 4)) & 1) ? 0xFFFF : 0;
            for (r = 0; r < 8; r++, d += t->bpr) {
                int sr = a == 2 ? r / 2 : a == 3 ? 4 + r / 2 : r;
                UWORD e = (r == 7 && c[k].ul && a != 2) ? 0xFFFF : wide[g[sr]], v = (UWORD)((e & fm) | (~e & bm));
                d[0] = (UBYTE)(v >> 8); d[1] = (UBYTE)v;
                if (mirror) { d[mo] = (UBYTE)(v >> 8); d[mo + 1] = (UBYTE)v; }
            }
        }
    }
    if (t->cols & 1) {                                   /* an odd last column: blank */
        for (p = 0; p < 4; p++) { UBYTE *d = t->pl[p] + slot * 8 * t->bpr + t->cols - 1; for (r = 0; r < 8; r++, d += t->bpr) { *d = 0; if (mirror) d[mo] = 0; } }
    }
    t->st.cells += (ULONG)(t->cols / 2);
}

static void draw_slot(term_t *t, int slot, int lo, int hi)
{
    if (t->lat[slot]) draw_wide(t, slot); else draw_cells(t, slot, cells_of(t, slot), lo, hi);
}

static void draw_marked(term_t *t)
{
    int s;
    unsigned long long a = now_e();
    WaitBlit();                                          /* a blitter fill or move of these lines has finished */
    for (s = 0; s < t->n; s++) if (t->dlo[s] <= t->dhi[s]) {
        draw_slot(t, s, t->dlo[s], t->dhi[s]);
        t->dlo[s] = 0x7FFF; t->dhi[s] = -1;
        t->shw[s] = 1;                                   /* drawn in both copies: no mirror copy */
    }
    t->pend = 0;
    t->st.e_put += (ULONG)(now_e() - a);
}

static void mirror_copy(term_t *t)
{
    int s;
    for (s = 0; s < t->rows; s++) if (t->mir[s]) {     /* only these slots have a mirror (2.) */
        BltBitMap(t->bm, 0, s * 8, t->bm, 0, (s + t->n) * 8, t->cols * 8, 8, 0xC0, 0x0F, NULL);
        t->st.mirrors++;
    }
    memset(t->mir, 0, (size_t)t->n);
}

/* the cursor's cell inverted in both copies of its slot */
static void cursor_xor(term_t *t, int slot, int col)
{
    int p, r, k;
    int w = t->lat[slot] ? 2 : 1;
    if (w == 2) { col *= 2; if (col + 1 >= t->cols) return; }
    t->shw[slot] = 1;
    WaitBlit();
    for (k = 0; k < (slot < t->rows ? 2 : 1); k++)
        for (p = 0; p < 4; p++) {
            UBYTE *d = t->pl[p] + (slot + k * t->n) * 8 * t->bpr + col;
            for (r = 0; r < 8; r++, d += t->bpr) { d[0] ^= 0xFF; if (w == 2) d[1] ^= 0xFF; }
        }
}

static void cursor_off(term_t *t) { if (t->con) { cursor_xor(t, t->cslot, t->ccl); t->con = 0; } }

static void cursor_on(term_t *t)
{
    if (t->con || t->sb || !t->cvis || t->crow < 0 || t->crow >= t->rows || t->ccol < 0 || t->ccol >= t->cols) return;
    if ((t->blink >> 5) & 1) return;                     /* blinks: 32 frames on, 32 off */
    t->cslot = slot_of(t, t->crow); t->ccl = t->ccol;
    cursor_xor(t, t->cslot, t->ccl);
    t->con = 1;
}

/* cells of screen rows r0..r1-1, columns c0..c1-1 blanked in colour bg and marked to be drawn */
static void fill(term_t *t, int r0, int r1, int c0, int c1, int bg)
{
    int row, k;
    UBYTE col = (UBYTE)(bg | bg << 4);
    if (c0 >= c1) return;
    for (row = r0; row < r1; row++) {
        int s = slot_of(t, row);
        cell_t *c = cells_of(t, s);
        for (k = c0; k < c1; k++) { c[k].g = ' '; c[k].col = col; c[k].ul = 0; }
        mark(t, s, c0, c1 - 1);
    }
}

/* ---- scrollback (3a) ---- */

static void hist_push(term_t *t, const cell_t *row)
{
    if (!t->hist) return;
    memcpy(t->hist + t->hhead * t->cols, row, (size_t)t->cols * sizeof(cell_t));
    t->hhead = (t->hhead + 1) % t->hcap;
    if (t->hcount < t->hcap) t->hcount++;
}

/* the line `i` of history and screen together (0 the oldest held) */
static const cell_t *line_at(const term_t *t, int i)
{
    if (i < t->hcount) return t->hist + ((t->hhead - t->hcount + i + t->hcap) % t->hcap) * t->cols;
    return cells_of(t, slot_of(t, i - t->hcount));
}

/* the screen's rows drawn from the place `sb` lines back, the bottom row showing where */
static void sb_draw(term_t *t)
{
    static cell_t bar[COLS_MAX];
    char txt[COLS_MAX + 1];
    int r, k, n, first = t->hcount - t->sb;
    WaitBlit();
    for (r = 0; r < t->rows - 1; r++) draw_cells(t, slot_of(t, r), line_at(t, first + r), 0, t->cols - 1);
    n = snprintf(txt, sizeof txt, " Scrollback: %d lines back of %d   Shift Up, Down: page   any other key: back ",
                 t->sb, t->hcount);
    for (k = 0; k < t->cols; k++) { bar[k].g = (UWORD)(UBYTE)(k < n ? txt[k] : ' '); bar[k].col = 0x70; bar[k].ul = 0; }
    draw_cells(t, slot_of(t, t->rows - 1), bar, 0, t->cols - 1);
    for (r = 0; r < t->rows; r++) t->shw[slot_of(t, r)] = 1;
    t->ptop = -1;
    t->sb_drawn = t->sb;
}

static void sb_end(term_t *t)
{
    int r;
    t->sb = 0; t->sb_drawn = 0;
    for (r = 0; r < t->rows; r++) mark(t, slot_of(t, r), 0, t->cols - 1);
    t->ptop = -1;
}

/* ---- libvterm's state callbacks ---- */

static int cb_putglyph(VTermGlyphInfo *info, VTermPos pos, void *u)
{
    term_t *t = (term_t *)u;
    int s;
    cell_t *c;
    if (pos.row < 0 || pos.row >= t->rows || pos.col < 0 || pos.col >= t->cols) return 1;
    s = slot_of(t, pos.row);
    c = cells_of(t, s) + pos.col;
    c->g = glyph_index(info->chars[0] ? info->chars[0] : ' ');
    c->col = t->pcol; c->ul = (UBYTE)(t->pul != 0);
    if (info->width == 2 && pos.col + 1 < t->cols) { c[1].g = ' '; c[1].col = c->col; c[1].ul = 0; mark(t, s, pos.col, pos.col + 1); }
    else mark(t, s, pos.col, pos.col);
    return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos old, int visible, void *u)
{
    term_t *t = (term_t *)u;
    (void)old;
    t->crow = pos.row; t->ccol = pos.col; t->cvis = visible;
    t->blink = 0;
    return 1;
}

/* a scroll of rect by `down` lines and `right` columns: the whole screen by lines moves the ring's top; any other
 * scroll moves the cells and their pixels (blitter); then the area left behind is erased in the pen's background */
static int cb_scrollrect(VTermRect r, int down, int right, void *u)
{
    term_t *t = (term_t *)u;
    int rows = r.end_row - r.start_row, cols = r.end_col - r.start_col, k, bg = erase_bg(t);
    int ad = down < 0 ? -down : down, ar = right < 0 ? -right : right;
    if (down > 0 && right == 0 && r.start_row == 0 && r.start_col == 0 && r.end_col == t->cols)
        for (k = 0; k < ad && k < rows; k++) hist_push(t, cells_of(t, slot_of(t, k)));
    if (ad >= rows || ar >= cols) { fill(t, r.start_row, r.end_row, r.start_col, r.end_col, bg); return 1; }
    if (right == 0 && r.start_row == 0 && r.end_row == t->rows && r.start_col == 0 && r.end_col == t->cols && ad <= t->n - t->rows) {
        t->top += down;
        t->tgt = t->top * 8;
        t->st.scrolls++;
        if (down > 0) for (k = t->rows - down; k < t->rows; k++) t->lat[slot_of(t, k)] = 0;   /* new lines: single (3b) */
        else for (k = 0; k < -down; k++) t->lat[slot_of(t, k)] = 0;
        if (down > 0) fill(t, t->rows - down, t->rows, 0, t->cols, bg);
        else fill(t, 0, -down, 0, t->cols, bg);
        return 1;
    }
    if (down) {                                          /* lines move: row by row in the order that keeps the source */
        for (k = 0; k < rows - ad; k++) {
            int dr = down > 0 ? r.start_row + k : r.end_row - 1 - k, sr = dr + down;
            int ds = slot_of(t, dr), ss = slot_of(t, sr);
            memmove(cells_of(t, ds) + r.start_col, cells_of(t, ss) + r.start_col, (size_t)cols * sizeof(cell_t));
            if (cols == t->cols) t->lat[ds] = t->lat[ss];
            BltBitMap(t->bm, r.start_col * 8, ss * 8, t->bm, r.start_col * 8, ds * 8, cols * 8, 8, 0xC0, 0x0F, NULL);
            if (t->dlo[ss] <= t->dhi[ss]) mark(t, ds, t->dlo[ss], t->dhi[ss]);
            t->mir[ds] = 1; t->shw[ds] = 1;
        }
        if (cols == t->cols) for (k = 0; k < ad; k++) t->lat[slot_of(t, down > 0 ? r.end_row - 1 - k : r.start_row + k)] = 0;
        if (down > 0) fill(t, r.end_row - ad, r.end_row, r.start_col, r.end_col, bg);
        else fill(t, r.start_row, r.start_row + ad, r.start_col, r.end_col, bg);
    }
    if (right) {                                         /* characters move within each line */
        for (k = r.start_row; k < r.end_row; k++) {
            int s = slot_of(t, k);
            cell_t *c = cells_of(t, s);
            if (right > 0) {
                memmove(c + r.start_col, c + r.start_col + right, (size_t)(cols - right) * sizeof(cell_t));
                BltBitMap(t->bm, (r.start_col + right) * 8, s * 8, t->bm, r.start_col * 8, s * 8, (cols - right) * 8, 8, 0xC0, 0x0F, NULL);
            } else {
                memmove(c + r.start_col + ar, c + r.start_col, (size_t)(cols - ar) * sizeof(cell_t));
                BltBitMap(t->bm, r.start_col * 8, s * 8, t->bm, (r.start_col + ar) * 8, s * 8, (cols - ar) * 8, 8, 0xC0, 0x0F, NULL);
            }
            if (t->dlo[s] <= t->dhi[s]) mark(t, s, r.start_col, r.end_col - 1);
            t->mir[s] = 1; t->shw[s] = 1;
        }
        if (right > 0) fill(t, r.start_row, r.end_row, r.end_col - ar, r.end_col, bg);
        else fill(t, r.start_row, r.end_row, r.start_col, r.start_col + ar, bg);
    }
    return 1;
}

static int cb_erase(VTermRect r, int selective, void *u)
{
    term_t *t = (term_t *)u;
    (void)selective;
    fill(t, r.start_row, r.end_row, r.start_col, r.end_col, erase_bg(t));
    return 1;
}

static int cb_initpen(void *u)
{
    term_t *t = (term_t *)u;
    t->pfg = 7; t->pbg = 0; t->pbold = t->pul = t->prev = t->pcon = 0;
    t->pcol = pen_col(t);
    return 1;
}

static int cb_setpenattr(VTermAttr a, VTermValue *v, void *u)
{
    term_t *t = (term_t *)u;
    switch (a) {
    case VTERM_ATTR_BOLD:       t->pbold = v->boolean; break;
    case VTERM_ATTR_UNDERLINE:  t->pul = v->number; break;
    case VTERM_ATTR_REVERSE:    t->prev = v->boolean; break;
    case VTERM_ATTR_CONCEAL:    t->pcon = v->boolean; break;
    case VTERM_ATTR_FOREGROUND: t->pfg = col16(t, v->color, 1); break;
    case VTERM_ATTR_BACKGROUND: t->pbg = col16(t, v->color, 0); break;
    default: break;
    }
    t->pcol = pen_col(t);
    return 1;
}

static int cb_settermprop(VTermProp p, VTermValue *v, void *u)
{
    term_t *t = (term_t *)u;
    if (p == VTERM_PROP_CURSORVISIBLE) t->cvis = v->boolean;
    return 1;
}

static int cb_setlineinfo(int row, const VTermLineInfo *ni, const VTermLineInfo *oi, void *u)
{
    term_t *t = (term_t *)u;
    int s, a;
    (void)oi;
    if (row < 0 || row >= t->rows) return 1;
    s = slot_of(t, row);
    a = ni->doubleheight == 1 ? 2 : ni->doubleheight == 2 ? 3 : ni->doublewidth ? 1 : 0;
    if (a != t->lat[s]) { t->lat[s] = (UBYTE)a; mark(t, s, 0, t->cols - 1); }
    return 1;
}

static int cb_bell(void *u) { term_t *t = (term_t *)u; DisplayBeep(t->full ? t->scr : t->host); return 1; }

static const VTermStateCallbacks cbs = {
    cb_putglyph, cb_movecursor, cb_scrollrect, NULL, cb_erase, cb_initpen, cb_setpenattr, cb_settermprop, cb_bell,
    NULL, cb_setlineinfo, NULL
};

static void cb_output(const char *s, size_t len, void *u)
{
    term_t *t = (term_t *)u;
    t->st.sent += (ULONG)len;
    if (t->out) t->out((const UBYTE *)s, (ULONG)len, t->user);
}

/* ---- the ring and the presentations ---- */

/* the cells, marks and bitmap for cols x rows (2.); the screen rows of an earlier ring are copied into it */
static int ring_alloc(term_t *t, int cols, int rows)
{
    int n = rows * 3 < rows + EXTRA_MIN ? rows + EXTRA_MIN : rows * 3, i, r;
    ULONG sz = (ULONG)(n * cols) * sizeof(cell_t) + (ULONG)n * 7u;
    UBYTE *m = (UBYTE *)AllocMem(sz, MEMF_ANY | MEMF_CLEAR);
    struct BitMap *bm;
    cell_t *nc;
    if (!m) return 0;
    if (!(bm = AllocBitMap((ULONG)cols * 8, (ULONG)((n + rows) * 8), 4, BMF_CLEAR | BMF_DISPLAYABLE, NULL)) ||
        (GetBitMapAttr(bm, BMA_FLAGS) & BMF_INTERLEAVED)) {
        if (bm) FreeBitMap(bm);
        FreeMem(m, sz); return 0;
    }
    nc = (cell_t *)m;
    for (i = 0; i < n * cols; i++) { nc[i].g = ' '; nc[i].col = 7; }
    if (t->cells) {                                      /* the old screen's rows, from the top */
        for (r = 0; r < rows && r < t->rows; r++) {
            const cell_t *oc = cells_of(t, slot_of(t, r));
            memcpy(nc + r * cols, oc, (size_t)(cols < t->cols ? cols : t->cols) * sizeof(cell_t));
        }
        WaitBlit();
        FreeBitMap(t->bm);
        FreeMem(t->cells, (ULONG)(t->n * t->cols) * sizeof(cell_t) + (ULONG)t->n * 7u);
    }
    t->cells = nc; t->bm = bm; t->n = n; t->cols = cols; t->rows = rows;
    if (t->hist) FreeMem(t->hist, (ULONG)(t->hcap * t->hcols) * sizeof(cell_t));
    t->hcap = (int)(262144u / ((ULONG)cols * sizeof(cell_t)));
    t->hcols = cols; t->hcount = t->hhead = t->sb = t->sb_drawn = 0;
    t->hist = (cell_t *)AllocMem((ULONG)(t->hcap * cols) * sizeof(cell_t), MEMF_ANY);
    t->dlo = (WORD *)(m + n * cols * sizeof(cell_t)); t->dhi = t->dlo + n;
    t->mir = (UBYTE *)(t->dhi + n); t->shw = t->mir + n; t->lat = t->shw + n;
    for (i = 0; i < n; i++) { t->dlo[i] = 0x7FFF; t->dhi[i] = -1; }
    for (r = 0; r < rows; r++) mark(t, r, 0, cols - 1);  /* everything drawn into the new bitmap */
    t->top = 0; t->tgt = t->shown = 0; t->ptop = -1; t->con = 0;
    t->bpr = bm->BytesPerRow;
    for (i = 0; i < 4; i++) t->pl[i] = (UBYTE *)bm->Planes[i];
    return 1;
}

static void colour_map(term_t *t)
{
    int f, b;
    for (f = 0; f < 16; f++) for (b = 0; b < 16; b++)
        t->cmap[f | b << 4] = (UBYTE)(t->full ? (f | b << 4) : (t->wpen[f] | t->wpen[b] << 4));
}

/* the current presentation closed (the ring stays) */
static void present_close(term_t *t)
{
    if (t->win) {
        if (!t->full) { t->wx = t->win->LeftEdge; t->wy = t->win->TopEdge; t->ww = t->win->Width; t->wh = t->win->Height; }
        CloseWindow(t->win); t->win = NULL;
    }
    if (t->scr) {                                        /* its bitmap is the ring's (SA_BitMap): it stays */
        WaitBlit();
        CloseScreen(t->scr); t->scr = NULL;
    }
}

/* full screen: the ring at the host's size, shown as the screen's bitmap */
static int open_full(term_t *t)
{
    struct ColorSpec cs[17];
    int i, w = t->host->Width, h = t->host->Height, cols = w / 8, rows = h / 8;
    if (cols > COLS_MAX) cols = COLS_MAX;
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    if (!t->bm || t->cols != cols || t->rows != rows) { if (!ring_alloc(t, cols, rows)) return 0; }
    for (i = 0; i < 16; i++) {
        cs[i].ColorIndex = (WORD)i; cs[i].Red = (UWORD)((ansi[i] >> 8) & 15);
        cs[i].Green = (UWORD)((ansi[i] >> 4) & 15); cs[i].Blue = (UWORD)(ansi[i] & 15);
    }
    cs[16].ColorIndex = -1;
    t->full = 1; colour_map(t);
    t->scr = OpenScreenTags(NULL, SA_DisplayID, GetVPModeID(&t->host->ViewPort), SA_Width, (ULONG)(cols * 8),
                            SA_Height, (ULONG)(rows * 8), SA_Depth, 4, SA_BitMap, (ULONG)t->bm, SA_Colors, (ULONG)cs,
                            SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, SA_Title, (ULONG)"Terminal",
                            TAG_DONE);
    if (!t->scr) return 0;
    t->win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)t->scr, WA_Left, 0, WA_Top, 0, WA_Width, (ULONG)(cols * 8),
                            WA_Height, (ULONG)(rows * 8), WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE,
                            WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
                            WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
    if (!t->win) return 0;
    if (t->blank_ptr) SetPointer(t->win, t->blank_ptr, 1, 16, 0, 0);
    return 1;
}

/* window: its inner area in the pen of ANSI black (the part beyond whole cells stays so) */
static void win_clear(term_t *t)
{
    struct Window *w = t->win;
    SetAPen(w->RPort, t->wpen[0]);
    RectFill(w->RPort, w->BorderLeft, w->BorderTop, w->Width - w->BorderRight - 1, w->Height - w->BorderBottom - 1);
}

/* window: on the host screen, the ring at the window's inner size */
static int open_window(term_t *t)
{
    struct Screen *h = t->host;
    int bw, bh, cols, rows;
    char *title = "Terminal     Right Amiga F: full screen, Q: close";
    t->full = 0; colour_map(t);
    if (!t->ww) { t->ww = h->Width * 3 / 4; t->wh = h->Height * 3 / 4; t->wx = (h->Width - t->ww) / 2; t->wy = (h->Height - t->wh) / 2; }
    t->win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)h, WA_Left, (ULONG)t->wx, WA_Top, (ULONG)t->wy,
                            WA_Width, (ULONG)t->ww, WA_Height, (ULONG)t->wh, WA_Title, (ULONG)title,
                            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                            WA_SizeBBottom, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_RMBTrap, TRUE,
                            WA_IDCMP, IDCMP_RAWKEY | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!t->win) return 0;
    bw = t->win->BorderLeft + t->win->BorderRight; bh = t->win->BorderTop + t->win->BorderBottom;
    WindowLimits(t->win, (WORD)(20 * 8 + bw), (WORD)(4 * 8 + bh), (UWORD)h->Width, (UWORD)h->Height);
    cols = (t->win->Width - bw) / 8; rows = (t->win->Height - bh) / 8;
    if (cols > COLS_MAX) cols = COLS_MAX;
    if (rows > ROWS_MAX) rows = ROWS_MAX;
    if (!t->bm || t->cols != cols || t->rows != rows) { if (!ring_alloc(t, cols, rows)) return 0; }
    else { int r; for (r = 0; r < rows; r++) mark(t, slot_of(t, r), 0, cols - 1); }
    win_clear(t);
    t->ptop = -1;
    return 1;
}

static void size_to_vterm(term_t *t)
{
    int r, c;
    vterm_get_size(t->vt, &r, &c);
    if (r != t->rows || c != t->cols) vterm_set_size(t->vt, t->rows, t->cols);
}

void term_close(term_t *t)
{
    if (!t) return;
    present_close(t);
    if (t->bm) { WaitBlit(); FreeBitMap(t->bm); }
    if (t->cells) FreeMem(t->cells, (ULONG)(t->n * t->cols) * sizeof(cell_t) + (ULONG)t->n * 7u);
    if (t->hist) FreeMem(t->hist, (ULONG)(t->hcap * t->hcols) * sizeof(cell_t));
    if (t->blank_ptr) FreeMem(t->blank_ptr, 12);
    if (t->vt) vterm_free(t->vt);
    if (t->q) FreeMem(t->q, QSIZE);
    if (KeymapBase) { CloseLibrary(KeymapBase); KeymapBase = NULL; }
    FreeMem(t, sizeof *t);
}

term_t *term_open(struct Screen *host, const UBYTE *wpen, int full, term_out_fn out, void *user)
{
    term_t *t = (term_t *)AllocMem(sizeof(term_t), MEMF_PUBLIC | MEMF_CLEAR);
    int i;
    if (!t) return NULL;
    t->out = out; t->user = user; t->host = host;
    for (i = 0; i < 16; i++) t->wpen[i] = (UBYTE)(wpen ? wpen[i] : i);
    if (!(KeymapBase = OpenLibrary((CONST_STRPTR)"keymap.library", 37)) || !load_topaz(t) ||
        !(t->q = (UBYTE *)AllocMem(QSIZE, MEMF_ANY))) { term_close(t); return NULL; }
    t->blank_ptr = (UWORD *)AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
    make_glyphs(t);
    if (!(full ? open_full(t) : open_window(t))) { term_close(t); return NULL; }
    if (!(t->vt = vterm_new(t->rows, t->cols))) { term_close(t); return NULL; }
    vterm_set_utf8(t->vt, 1);
    vterm_output_set_callback(t->vt, cb_output, t);
    t->vs = vterm_obtain_state(t->vt);
    cb_initpen(t);
    vterm_state_set_callbacks(t->vs, &cbs, t);
    vterm_state_reset(t->vs, 1);
    t->ascii = vterm_lookup_encoding(ENC_SINGLE_94, 'B');
    t->utf8enc = vterm_lookup_encoding(ENC_UTF8, 'u');
    t->cvis = 1;
    return t;
}

/* full screen <-> window; 0 when neither could open (the terminal is then to close) */
static int toggle(term_t *t)
{
    int was = t->full;
    present_close(t);
    if (was) { if (!open_window(t) && !open_full(t)) return 0; }
    else if (!open_full(t) && !open_window(t)) return 0;
    size_to_vterm(t);
    return 1;
}

void term_set_utf8(term_t *t, int on) { t->utf8 = on; }

void term_set_smooth(term_t *t, int on) { t->jump = !on; }

void term_front(term_t *t)
{
    if (t->full) ScreenToFront(t->scr); else { ScreenToFront(t->host); WindowToFront(t->win); }
    ActivateWindow(t->win);
}

/* ---- input, pacing ---- */

ULONG term_input(term_t *t, const UBYTE *b, ULONG n)
{
    ULONG k = 0;
    while (k < n && ((t->qw + 1u) & (QSIZE - 1u)) != t->qr) { t->q[t->qw] = b[k++]; t->qw = (t->qw + 1u) & (QSIZE - 1u); }
    return k;
}
/* the UTF-8 decoder's state (libvterm encoding.c struct UTF8DecoderData): its first field, bytes_remaining, is 0 when
 * no sequence is open; read through memcpy so no pointer of another type aliases it */
static int utf8_idle(const void *data)
{
    int remaining;
    memcpy(&remaining, data, sizeof remaining);
    return remaining == 0;
}


ULONG term_pending(const term_t *t) { return (t->qw - t->qr) & (QSIZE - 1u); }

/* the plain-text path (4a): of the n printable ASCII bytes at b, those placed here; 0 when libvterm must see them */
static ULONG fast_text(term_t *t, const UBYTE *b, ULONG n)
{
    VTerm *vt = t->vt;
    VTermState *st = t->vs;
    int row = st->pos.row, col = st->pos.col, s, k;
    ULONG m;
    cell_t *c;
    const VTermEncodingInstance *e = &st->encoding[st->gl_set];
    /* G0: the ASCII set, or the UTF-8 decoder (libvterm in UTF-8 loads it into every G set) with no sequence open:
     * its first field, bytes_remaining, is 0 */
    if (vt->parser.state != NORMAL || vt->parser.in_esc || st->gsingle_set ||
        !(e->enc == t->ascii || (e->enc == t->utf8enc && utf8_idle(e->data))) ||
        st->mode.insert || st->at_phantom || st->lineinfo[row].doublewidth || col >= st->cols - 1 ||
        row < 0 || row >= t->rows || st->cols != t->cols)
        return 0;
    m = (ULONG)(st->cols - 1 - col);
    if (m > n) m = n;
    s = slot_of(t, row);
    c = cells_of(t, s) + col;
    for (k = 0; k < (int)m; k++) { c[k].g = b[k]; c[k].col = t->pcol; c[k].ul = (UBYTE)(t->pul != 0); }
    mark(t, s, col, col + (int)m - 1);
    st->combine_chars[0] = b[m - 1]; st->combine_chars[1] = 0;
    st->combine_width = 1; st->combine_pos.row = row; st->combine_pos.col = col + (int)m - 1;
    st->pos.col = col + (int)m;
    t->crow = row; t->ccol = st->pos.col; t->blink = 0;
    t->st.fast += m;
    return m;
}

/* up to n queued bytes to libvterm, 8-bit bytes encoded as section 4 says; runs of printable ASCII through the
 * plain-text path when it applies (4a) */
static void feed(term_t *t, ULONG n)
{
    UBYTE b[600], run[128];
    ULONG k = 0, i;
    unsigned long long a = now_e();
    for (i = 0; i < n; ) {
        UBYTE c = t->q[t->qr];
        if (!t->utf8 && c >= 0x20 && c < 0x7F) {
            ULONG r = 0, m;
            while (i < n && r < sizeof run && (c = t->q[t->qr]) >= 0x20 && c < 0x7F) {
                run[r++] = c; t->qr = (t->qr + 1u) & (QSIZE - 1u); i++;
            }
            if (k) { vterm_input_write(t->vt, (const char *)b, k); k = 0; }
            for (m = 0; m < r; ) {                         /* the plain-text path where it applies, else a byte to */
                ULONG f = fast_text(t, run + m, r - m);    /* libvterm (the end of an escape sequence, the margin) */
                if (f) m += f;
                else { vterm_input_write(t->vt, (const char *)run + m, 1); m++; }
            }
            continue;
        }
        t->qr = (t->qr + 1u) & (QSIZE - 1u); i++;
        if (t->utf8 || c < 0x80) b[k++] = c;
        else if (c < 0xA0) { b[k++] = 0x1B; b[k++] = (UBYTE)(c - 0x40); }
        else { b[k++] = (UBYTE)(0xC0 | (c >> 6)); b[k++] = (UBYTE)(0x80 | (c & 0x3F)); }
    }
    if (k) vterm_input_write(t->vt, (const char *)b, k);
    t->st.e_vt += (ULONG)(now_e() - a);
    t->st.bytes += n;
}

/* full screen: the view toward its target, 2 pixel lines a frame, faster as the distance grows, never further than
 * the ring (jump scrolling: at once); at the start of a tick, so the change falls in the display's vertical blank
 * when the caller ticks after it (WaitTOF) and the lines it shows were drawn in the tick before */
static void scroll_view(term_t *t)
{
    LONG lag = t->tgt - t->shown, step, off;
    if (lag > (LONG)(t->n - t->rows) * 8) { t->shown = t->tgt - (LONG)(t->n - t->rows) * 8; t->st.jumps++; }
    else if (lag < -(LONG)(t->n - t->rows) * 8) { t->shown = t->tgt + (LONG)(t->n - t->rows) * 8; t->st.jumps++; }
    lag = t->tgt - t->shown;
    if (t->jump) t->shown = t->tgt;
    else if (lag) {
        step = 2 + (lag > 0 ? lag : -lag) / 24;
        if (lag > 0) t->shown += lag < step ? lag : step; else t->shown -= -lag < step ? -lag : step;
    }
    off = t->shown % (LONG)(t->n * 8);
    if (off < 0) off += t->n * 8;
    if (t->scr->ViewPort.RasInfo->RyOffset != (WORD)off) {
        t->scr->ViewPort.RasInfo->RyOffset = (WORD)off;
        ScrollVPort(&t->scr->ViewPort);
    }
}

void term_tick(term_t *t, ULONG budget_us)
{
    struct EClockVal v;
    ULONG ef = ReadEClock(&v);
    unsigned long long e0 = ((unsigned long long)v.ev_hi << 32) | v.ev_lo, lim = e0 + (unsigned long long)budget_us * ef * 85u / 100000000u;
    ULONG us;
    t->st.ticks++;
    t->blink++;
    if (t->full) scroll_view(t);
    cursor_off(t);
    if (t->sb && t->sb != t->sb_drawn) sb_draw(t);
    while (t->qr != t->qw && !t->sb) {                   /* input in blocks of up to 64 bytes (5.); none in scrollback */
        ULONG n = term_pending(t);
        feed(t, n > 64u ? 64u : n);
        if (now_e() + (unsigned long long)t->pend * 25u * ef / 1000000u > lim) break;
    }
    draw_marked(t);
    { unsigned long long a = now_e(); mirror_copy(t); t->st.e_mir += (ULONG)(now_e() - a); }
    if (!t->full) {                                      /* window: the rows drawn, or all of them after a scroll */
        struct Window *w = t->win;
        int r, s0 = slot_of(t, 0);
        cursor_on(t);
        if (t->ptop != t->top && (t->ticks_full++ & 1)) {  /* after a scroll: the whole area, every second frame */
            for (r = 0; r < t->rows; r++) t->shw[slot_of(t, r)] = 0;
            goto done;
        }
        if (t->ptop != t->top) {
            BltBitMapRastPort(t->bm, 0, s0 * 8, w->RPort, w->BorderLeft, w->BorderTop, t->cols * 8, t->rows * 8, 0xC0);
            t->ptop = t->top; t->shown = t->tgt;
        } else for (r = 0; r < t->rows; r++) {
            int s = slot_of(t, r);
            if (t->shw[s]) BltBitMapRastPort(t->bm, 0, s * 8, w->RPort, w->BorderLeft, w->BorderTop + r * 8, t->cols * 8, 8, 0xC0);
        }
        memset(t->shw, 0, (size_t)t->n);
        goto done;
    }
    memset(t->shw, 0, (size_t)t->n);
    cursor_on(t);
done:
    t->st.frames++;
    us = (ULONG)((now_e() - e0) * 1000000ull / ef);
    if (us > t->st.max_us) t->st.max_us = us;
    if (us > budget_us + 2000u) t->st.late++;
}

/* ---- keys ---- */

ULONG term_sigmask(const term_t *t) { return 1ul << t->win->UserPort->mp_SigBit; }

static void key(term_t *t, VTermKey k, int mod) { vterm_keyboard_key(t->vt, k, (VTermModifier)mod); }

int term_events(term_t *t)
{
    struct IntuiMessage *im;
    int keep = 1;
    while ((im = (struct IntuiMessage *)GetMsg(t->win->UserPort)) != NULL) {
        if (im->Class == IDCMP_RAWKEY && !(im->Code & IECODE_UP_PREFIX)) {
            UWORD q = im->Qualifier, c = im->Code;
            t->st.keys++;
            if (t->sb && !(q & IEQUALIFIER_RCOMMAND) && c < 0x60 &&
                !((c == 0x4C || c == 0x4D) && (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))))
                sb_end(t);                               /* back to the screen; the key is sent */
            int mod = ((q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? VTERM_MOD_SHIFT : 0) |
                      ((q & IEQUALIFIER_CONTROL) ? VTERM_MOD_CTRL : 0) |
                      ((q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? VTERM_MOD_ALT : 0);
            if (q & IEQUALIFIER_RCOMMAND) {              /* Right Amiga: the terminal's own commands */
                if (c == 0x10) keep = 0;                 /* Q: close */
                else if (c == 0x23) t->toggle = 1;       /* F: full screen or window */
                else if (c == 0x21) t->jump ^= 1;        /* S: smooth or jump scrolling */
                else if (c == 0x16) t->req |= TERM_REQ_SEND;   /* U: the opener's file sending */
            } else if ((c == 0x4C || c == 0x4D) && (mod & VTERM_MOD_SHIFT)) {   /* Shift Up, Down: scrollback (3a) */
                int pg = t->rows - 1;
                if (c == 0x4C) t->sb = t->sb + pg > t->hcount ? t->hcount : t->sb + pg;
                else t->sb = t->sb > pg ? t->sb - pg : 0;
                if (!t->sb && t->sb_drawn) sb_end(t);
            } else if (c == 0x4C) key(t, VTERM_KEY_UP, mod);
            else if (c == 0x4D) key(t, VTERM_KEY_DOWN, mod);
            else if (c == 0x4E) key(t, VTERM_KEY_RIGHT, mod);
            else if (c == 0x4F) key(t, VTERM_KEY_LEFT, mod);
            else if (c >= 0x50 && c <= 0x59) key(t, VTERM_KEY_FUNCTION(c - 0x4F), mod);
            else if (c == 0x46) key(t, VTERM_KEY_DEL, mod);
            else if (c == 0x41) key(t, VTERM_KEY_BACKSPACE, mod);
            else if (c == 0x44) key(t, VTERM_KEY_ENTER, mod);
            else if (c == 0x43) key(t, VTERM_KEY_KP_ENTER, mod);
            else if (c == 0x42) key(t, VTERM_KEY_TAB, mod);
            else if (c == 0x45) key(t, VTERM_KEY_ESCAPE, mod);
            else {
                struct InputEvent ie;
                char buf[8];
                LONG n, i;
                memset(&ie, 0, sizeof ie);
                ie.ie_Class = IECLASS_RAWKEY; ie.ie_Code = c; ie.ie_Qualifier = q;
                ie.ie_EventAddress = (APTR *)*((ULONG *)im->IAddress);
                n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
                for (i = 0; i < n; i++) {                /* Latin-1 from the keymap, sent as bytes (section 4) */
                    UBYTE ch = (UBYTE)buf[i];
                    t->st.sent++;
                    if (t->out) t->out(&ch, 1, t->user);
                }
            }
        }
        else if (im->Class == IDCMP_NEWSIZE) t->resized = 1;
        else if (im->Class == IDCMP_CLOSEWINDOW) keep = 0;
        else if (im->Class == IDCMP_REFRESHWINDOW) { BeginRefresh(t->win); EndRefresh(t->win, TRUE); t->ptop = -1; }
        ReplyMsg((struct Message *)im);
    }
    if (keep && t->toggle) {
        t->toggle = t->resized = 0;
        cursor_off(t);
        if (!toggle(t)) keep = 0;
    }
    if (keep && t->resized && !t->full) {                /* the window's new size: a ring of that many cells */
        int bw = t->win->BorderLeft + t->win->BorderRight, bh = t->win->BorderTop + t->win->BorderBottom;
        int cols = (t->win->Width - bw) / 8, rows = (t->win->Height - bh) / 8;
        t->resized = 0;
        if (cols > COLS_MAX) cols = COLS_MAX;
        if (rows > ROWS_MAX) rows = ROWS_MAX;
        cursor_off(t);
        win_clear(t);
        if (cols != t->cols || rows != t->rows) {
            if (!ring_alloc(t, cols, rows)) keep = 0;
            else size_to_vterm(t);
        }
        t->ptop = -1;
    }
    return keep;
}

void term_stats(const term_t *t, term_stats_t *s) { *s = t->st; }

ULONG term_requests(term_t *t) { ULONG r = t->req; t->req = 0; return r; }

void term_window(term_t *t)
{
    if (t->full) { cursor_off(t); if (!toggle(t)) return; }
    term_front(t);
}
