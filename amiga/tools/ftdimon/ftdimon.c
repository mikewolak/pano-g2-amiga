/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* ftdimon.c -- the FTDI FT2232H on the G2's USB hub from the Amiga: the chip's state and MPSSE settings, every pin of
 * channel A set and latched by hand, JTAG chain scan and loopback, SPI NOR flash (part detection, read, erase,
 * program, verify, files) and a hex editor with smooth scrolling for the flash and EEPROM images, in one window on a
 * screen of its own (amiga-ui-style, serialmon's frame).
 *
 *   ftdimon [DEVICE=g2serial.device] [UNIT=n] [PAGE=0-4]
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  Specification: docs/G2_FT2232H_PORT.pdf; README.md here.
 *
 * 1 Paths.  Channel A of the chip is in MPSSE mode (firmware AMIGA.INI FTDI=MPSSE); its command bytes travel through
 *   the serial driver of the G2 serial FIFO board (link.c, amiga/tools/jtag/mpsse.c).  The chip's control requests
 *   (latency timer, bit mode, pins, the configuration EEPROM, the channel's mode) go to the firmware's console
 *   command `ftdi' through the command block of HD0 (link.c fw_cmd).
 *
 * 2 Pages.  Device: the chip as the firmware sees it, the MPSSE clock (divisor, divide by 5, adaptive and three-phase
 *   clocking), the internal loopback, the latency timer, the channel's mode and the EEPROM.  Pins: ADBUS0-7 and
 *   ACBUS0-7, each an input or an output with its latched level, the levels read back.  JTAG: the chain from
 *   Test-Logic-Reset (panoflash's chain.c) and the TDI-TDO loopback test.  SPI Flash: ftdiflash's procedures
 *   (spi.c) with the part named by flashrom's table (flashchips.c); read, erase, program, verify, and all three in
 *   turn, a block at a time from the display update so the window keeps working.  Hex Editor: the flash buffer or
 *   the EEPROM's 512 bytes, edited in hex or text, opened from and saved to files.  Preferences (Project menu, key
 *   ","): the SPI bus (SCK level between transfers, sampling edge, bit order, chip select pin and level, ADBUS7),
 *   the MPSSE clock, the latency timer and the first page, kept as the variables ftdimon/<name> in ENV: and ENVARC:.
 *
 * 3 Display.  60 updates a second (timer.device UNIT_MICROHZ); the hex view scrolls by pixels toward its target line
 *   (ScrollRaster on the two bitplanes the dark panel uses, the uncovered lines blitted from a one-row bitmap).  The
 *   screen copies the Workbench's mode; the window is sizeable and the layout follows its size. */
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <hardware/blit.h>
#include <graphics/sprite.h>
#include <graphics/copper.h>
#include <graphics/view.h>
#include <proto/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include "link.h"
#include "spi.h"
#include "flashchips.h"
#include "jtag.h"
#include "chain.h"
#include "font8x8.h"
#include "crc32.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *GadToolsBase, *AslBase;
struct Device        *TimerBase;

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif
#define TICK_HZ 60u

/* ---- the clock and the vertical blank server (serialmon section 2) ---- */
#define HWREG    ((volatile struct Custom *)0xDFF000)
#define VPOSL     (*(volatile ULONG *)0xDFF004)            /* VPOSR:VHPOSR, line in bits 16:8 */
static ULONG b_lines = 313u, b_hz = 15625u, b_ns = 64000u;   /* the frame's lines, lines a second, ns a line */

static volatile ULONG  b_frames;                          /* vertical blanks served */

/* the frame's lines: the highest line seen through one whole frame, plus one; PAL from 290 lines on.  Bounded: at
 * most about 2 x 10^6 reads */
static void beam_measure(void)
{
    ULONG f0 = b_frames, n = 0, mx = 0, l;
    while (b_frames == f0 && ++n < 1000000u) ;
    f0 = b_frames;
    while (b_frames == f0 && ++n < 2000000u) { l = (VPOSL >> 8) & 0x1FFu; if (l > mx) mx = l; }
    if (n >= 2000000u || mx < 200u) return;               /* no vertical blank seen: the PAL figures stay */
    b_lines = mx + 1u;
    if (b_lines < 290u) { b_hz = 15734u; b_ns = 63556u; }
}

/* the vertical blank server: one clock read a frame, and the About box's frame signal while it is open.  The stub
 * returns with Z set, so the chain goes on. */
static struct Task *volatile ab_task;
static ULONG ab_sigmask;
static volatile ULONG ab_vbl_n;
void vbl_c(void) __attribute__((used));
void vbl_c(void)
{
    b_frames++;
    if (ab_task) { Signal(ab_task, ab_sigmask); ab_vbl_n++; }
}
void vbl_stub(void);
__asm__(".text\n\t.even\n_vbl_stub:\n\tjsr _vbl_c\n\tmoveq #0,d0\n\trts\n");

static struct Interrupt vbl_is = { { NULL, NULL, NT_INTERRUPT, 127, (char *)"serialmon clock" }, NULL, (void (*)())vbl_stub };   /* first in the chain */
static int have_vbl;

/* ---- E clock ------------------------------------------------------------------------------------------------ */

static ULONG efreq;

static unsigned long long eclock(void)
{
    struct EClockVal v;
    ReadEClock(&v);
    return ((unsigned long long)v.ev_hi << 32) | v.ev_lo;
}


/* ---- screen, palette, drawing (serialmon) ------------------------------------------------------------------- */

enum { P_BG, P_BLACK, P_WHITE, P_BLUE, P_TROUGH, P_GRN, P_AMB, P_RED, P_GRN_D, P_AMB_D, P_RED_D,
       P_TXT_GRN, P_TXT_RED, P_TXT_BLUE, P_GREY, P_PEAK };
static const struct ColorSpec colours[] = {
    { P_BG, 0xA, 0xA, 0xA }, { P_BLACK, 0, 0, 0 }, { P_WHITE, 0xF, 0xF, 0xF }, { P_BLUE, 0x5, 0x7, 0xA },
    { P_TROUGH, 0x1, 0x1, 0x2 }, { P_GRN, 0x3, 0xE, 0x4 }, { P_AMB, 0xF, 0xC, 0x2 }, { P_RED, 0xF, 0x4, 0x3 },
    { P_GRN_D, 0x1, 0x4, 0x2 }, { P_AMB_D, 0x4, 0x3, 0x1 }, { P_RED_D, 0x4, 0x1, 0x1 },
    { P_TXT_GRN, 0x0, 0x6, 0x1 }, { P_TXT_RED, 0xB, 0x0, 0x0 }, { P_TXT_BLUE, 0x1, 0x2, 0x6 },
    { P_GREY, 0x7, 0x7, 0x7 }, { P_PEAK, 0xF, 0xF, 0xE }, { -1, 0, 0, 0 }
};
static UWORD pens[] = { P_BG, P_BLACK, P_BLACK, P_WHITE, P_BLACK, P_BLUE, P_WHITE, P_BG, P_WHITE, P_BLACK,
                        P_WHITE, P_BLACK, (UWORD)~0 };
static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

static struct Screen   *scr;
static struct Window   *win;
static struct RastPort *rp;
static APTR             vi;
static struct TextFont *font;
static struct Gadget   *glist;
static struct Menu     *menu;
static int ox, oy, win_w, win_h;                           /* the window's inner area */
#define WIN_W_MIN 616
#define WIN_W_MAX 1280
#define WIN_H_MIN 176

static void pen_text(struct RastPort *r, int x, int y, int pen, const char *s)
{
    SetAPen(r, pen); SetBPen(r, P_BG); SetDrMd(r, JAM2);
    Move(r, x, y + r->TxBaseline); Text(r, (CONST_STRPTR)s, strlen(s));
}
static int tlen(struct RastPort *r, const char *s) { return TextLength(r, (CONST_STRPTR)s, strlen(s)); }
static void text_c(struct RastPort *r, int cx, int y, int pen, const char *s) { pen_text(r, cx - tlen(r, s) / 2, y, pen, s); }
static void text_r(struct RastPort *r, int rx, int y, int pen, const char *s) { pen_text(r, rx - tlen(r, s), y, pen, s); }
static void bold(struct RastPort *r, int on) { SetSoftStyle(r, on ? FSF_BOLD : FS_NORMAL, AskSoftStyle(r)); }

/* a value right-aligned to rx in a field of w pixels; only the characters that changed are drawn (serialmon) */
static void value_r(char *old, int rx, int y, int w, int pen, const char *s)
{
    char f[40];
    int n = w / 8, a, b, l = (int)strlen(s);
    if (n > 39) n = 39;
    if (l > n) l = n;
    memset(f, ' ', (size_t)(n - l)); memcpy(f + n - l, s, (size_t)l); f[n] = 0;
    if ((int)strlen(old) != n || old[0] == 0x7F) { a = 0; b = n; }
    else {
        for (a = 0; a < n && f[a] == old[a]; a++) ;
        if (a == n) return;
        for (b = n; b > a && f[b - 1] == old[b - 1]; b--) ;
    }
    SetAPen(rp, pen); SetBPen(rp, P_BG); SetDrMd(rp, JAM2);
    Move(rp, rx - (n - a) * 8, y + rp->TxBaseline); Text(rp, (CONST_STRPTR)f + a, b - a);
    strcpy(old, f);
}

static void group(int x, int y, int w, int h, const char *title)
{
    int tw;
    DrawBevelBox(rp, x, y, w, h, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    DrawBevelBox(rp, x + 2, y + 1, w - 4, h - 2, GT_VisualInfo, vi, TAG_DONE);
    bold(rp, 1);
    tw = tlen(rp, title);
    SetAPen(rp, P_BG); RectFill(rp, x + w / 2 - tw / 2 - 6, y - 4, x + w / 2 + tw / 2 + 6, y + 4);
    pen_text(rp, x + w / 2 - tw / 2, y - 4, P_TXT_BLUE, title);
    bold(rp, 0);
}

static void panel(struct RastPort *r, int x, int y, int w, int h)
{
    DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(r, P_TROUGH); RectFill(r, x + 2, y + 1, x + w - 3, y + h - 2);
}

static void panel_text(struct RastPort *r, int x, int y, int w, int ty, int pen, const char *s, int b)
{
    (void)y;
    SetAPen(r, P_TROUGH); RectFill(r, x + 2, ty, x + w - 3, ty + 7);
    if (b) bold(r, 1);
    SetAPen(r, pen); SetDrMd(r, JAM1);
    Move(r, x + w / 2 - tlen(r, s) / 2, ty + r->TxBaseline); Text(r, (CONST_STRPTR)s, strlen(s));
    if (b) bold(r, 0);
}

static void fmt_thousands(char *s, ULONG v)
{
    char t[16];
    int n = 0, i, o = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (i = n - 1; i >= 0; i--) { s[o++] = t[i]; if (i && i % 3 == 0) s[o++] = ','; }
    s[o] = 0;
}

/* ---- buttons drawn by the program (serialmon) ----------------------------------------------------------------- */

enum { ICON_NONE, ICON_PLAY, ICON_STOP, ICON_UP, ICON_DOWN };
typedef struct {
    struct Gadget  g;
    struct Window *w;
    const char    *label;
    int            icon, sel, dis, drawn, on;
} btn_t;

static void btn_init(btn_t *b, int id, int x, int y, int w, int h, const char *label, int icon)
{
    memset(b, 0, sizeof *b);
    b->g.LeftEdge = x; b->g.TopEdge = y; b->g.Width = w; b->g.Height = h;
    b->g.Flags = GFLG_GADGHNONE;
    b->g.Activation = GACT_RELVERIFY | GACT_IMMEDIATE;
    b->g.GadgetType = GTYP_BOOLGADGET;
    b->g.GadgetID = id;
    b->label = label; b->icon = icon; b->drawn = -1; b->on = 1;
}

static void btn_draw(btn_t *b)
{
    struct RastPort *r;
    int x = b->g.LeftEdge, y = b->g.TopEdge, w = b->g.Width, h = b->g.Height;
    int down, st, tw, iw, tx, ty, pen, i;
    if (!b->on || !b->w) return;
    r = b->w->RPort;
    down = (b->g.Flags & GFLG_SELECTED) && !b->dis; st = down | (b->sel << 1) | (b->dis << 2);
    if (st == b->drawn) return;
    b->drawn = st;
    SetAPen(r, b->sel ? P_BLUE : P_BG); RectFill(r, x + 2, y + 1, x + w - 3, y + h - 2);
    if (down || b->sel) DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    else DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, TAG_DONE);
    pen = b->sel ? (b->dis ? P_BG : P_WHITE) : (b->dis ? P_GREY : P_BLACK);
    if (b->icon == ICON_UP || b->icon == ICON_DOWN) {      /* a triangle 7 wide, 4 high, in the middle */
        int cx = x + w / 2 + down, cy = y + (h - 4) / 2 + down;
        SetAPen(r, pen);
        for (i = 0; i < 4; i++) { int k = b->icon == ICON_UP ? i : 3 - i; RectFill(r, cx - k, cy + i, cx + k, cy + i); }
        return;
    }
    tw = tlen(r, b->label);
    iw = b->icon ? 18 : 0;
    tx = x + (w - iw - tw) / 2 + down;
    ty = y + (h - 8) / 2 + down;
    if (b->icon == ICON_PLAY) {
        SetAPen(r, b->dis ? P_GREY : P_TXT_GRN);
        for (i = 0; i < 7; i++) { int ww = 4 * (i < 3 ? i : 6 - i); RectFill(r, tx, ty + i, tx + ww, ty + i); }
    } else if (b->icon == ICON_STOP) {
        SetAPen(r, b->dis ? P_GREY : P_TXT_RED); RectFill(r, tx, ty + 1, tx + 10, ty + 5);
    }
    SetAPen(r, pen); SetDrMd(r, JAM1);
    Move(r, tx + iw, ty + r->TxBaseline); Text(r, (CONST_STRPTR)b->label, strlen(b->label));
}

static void btn_add(btn_t *b, int n, struct Window *w)
{
    int i;
    for (i = 0; i < n; i++) { b[i].w = w; AddGadget(w, &b[i].g, ~0); }
    for (i = 0; i < n; i++) { b[i].drawn = -1; btn_draw(&b[i]); }
}

static void btn_set(btn_t *b, int sel, int dis)
{
    if (b->sel == sel && b->dis == dis) return;
    b->sel = sel; b->dis = dis;
    btn_draw(b);
}

/* ---- state ---------------------------------------------------------------------------------------------------- */

enum { PG_DEV, PG_PINS, PG_JTAG, PG_SPI, PG_HEX, PG_N };
static const char *const pg_lbl[PG_N] = { "Device", "Pins", "JTAG", "SPI Flash", "Hex Editor" };
static int page = PG_DEV;
static char dev_name[64] = "g2serial.device";
static ULONG dev_unit;

/* MPSSE settings (the chip keeps them; this is what was last sent) */
static const char *const clk_lbl[] = { "30 MHz", "15 MHz", "10 MHz", "6 MHz", "3 MHz", "1 MHz", "500 kHz", "100 kHz", NULL };
static const UWORD clk_div[] = { 0, 1, 2, 4, 9, 29, 59, 299 };
static int s_clk = 5, s_div5, s_adapt, s_3ph, s_loop, s_lat = 1, s_mode;   /* s_mode 0 MPSSE, 1 serial */
static const char *const mode_lbl[] = { "MPSSE (JTAG, SPI)", "Serial port", NULL };
/* the pins: value and direction (1 output) of ADBUS (0) and ACBUS (1); the levels read */
static UBYTE pin_val[2], pin_dir[2], pin_in[2];
static int pin_auto = 1, pin_read_ok;
static const char *const pin_fn[2][8] = { { "TCK", "TDI", "TDO", "TMS", "", "", "", "" },
                                          { "", "", "", "", "", "", "", "" } };
/* the chip as `ftdi' reports it */
static char ci_txt[10][28];
static char ci_old[10][40];
/* status line */
static char s_msg[160];
static int  s_msg_pen = P_GRN, s_msg_new = 1;

static void say(int pen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vsnprintf(s_msg, sizeof s_msg, fmt, ap); va_end(ap);
    s_msg_pen = pen; s_msg_new = 1;
}

/* buffers: the flash image and the EEPROM's 256 words (low byte first) */
static UBYTE *fbuf, *fdirty;
static ULONG  fbuf_len;
static UBYTE  ebuf[512], ebuf_rd[512], edirty[64];
static int    ebuf_ok;
static int    hv_which;                                   /* the hex editor's buffer: 0 flash, 1 EEPROM */
static char   fname[256] = "RAM:flash.bin";

static int buf_alloc(ULONG n)
{
    if (fbuf && fbuf_len == n) return 0;
    if (fbuf) { FreeVec(fbuf); fbuf = NULL; }
    if (fdirty) { FreeVec(fdirty); fdirty = NULL; }
    fbuf_len = 0;
    if (!n) return 0;
    fbuf = (UBYTE *)AllocVec(n, MEMF_ANY);
    fdirty = (UBYTE *)AllocVec((n + 7u) / 8u, MEMF_ANY | MEMF_CLEAR);
    if (!fbuf || !fdirty) {
        if (fbuf) FreeVec(fbuf);
        if (fdirty) FreeVec(fdirty);
        fbuf = fdirty = NULL;
        return -1;
    }
    memset(fbuf, 0xFF, n);
    fbuf_len = n;
    return 0;
}

/* the SPI part */
static UBYTE id9f[20];
static const fc_part_t *part, *cand[16];
static int ncand, cand_i, er_i;
static ULONG part_size;                                    /* bytes; the part's, or the ID's capacity byte */
static char sp_old[10][40];
static char op_start[16] = "0", op_len[16] = "";
static char jt_old[4][40];

/* JTAG results */
#define JL_MAX 24
static char jl_txt[JL_MAX][80];
static int  jl_n, jl_pen[JL_MAX];
static unsigned long jt_bytes, jt_errs, jt_bps;
static int jt_res = -1;                                    /* -1 none, 0 FAIL, 1 PASS */

/* ---- gadgets and layout ---------------------------------------------------------------------------------------- */

enum { G_CLK, G_LAT, G_DIV5, G_ADAPT, G_3PH, G_LOOP, G_MODE,
       G_PART, G_START, G_LEN, G_ERASE,
       G_HSCR, G_GOTO, G_JCLK, G_N };
static struct Gadget *gad[G_N];

enum { B_TAB0, B_TAB1, B_TAB2, B_TAB3, B_TAB4,
       B_REFRESH, B_APPLY, B_RECON, B_EE_RD, B_EE_WR, B_EE_ER,
       B_PIN_D0, B_PIN_D15 = B_PIN_D0 + 15, B_PIN_L0, B_PIN_L15 = B_PIN_L0 + 15, B_ALLIN, B_READP, B_AUTO,
       B_SCAN, B_LOOPI, B_LOOPE,
       B_DETECT, B_UNPROT, B_RD, B_VFY, B_ER, B_PRG, B_ALL, B_OPEN, B_SAVE,
       B_HFLASH, B_HEE, B_HOPEN, B_HSAVE, B_HFILL, B_HUP, B_HDN,
       B_N };
static btn_t bt[B_N];
#define BTN_ID 100

/* geometry of the page area */
#define TAB_Y   (oy + 4)
#define PG_Y    (oy + 26)
#define ST_Y    (oy + win_h - 15)
#define PG_H    (ST_Y - 4 - PG_Y)
#define GW      ((win_w - 24) / 2)
#define GA_X    (ox + 8)
#define GB_X    (ox + 16 + GW)
static int ROWP;                                           /* table row pitch: 10, or 11 on taller windows */
static int dv_y[8];                                        /* the Device page's rows of controls */
static int dev_tx, pin_cw, pin_x0[2], pin_ly;
static int jl_x, jl_y, jl_w, jl_rows;
static int bar_x, bar_y, bar_w, spi_ty, jt_ty, jt_px, jt_pw;

/* the hex view */
static struct BitMap  *row_bm;
static struct RastPort row_rp;
static int  hv_x, hv_y, hv_w, hv_rows, hv_cols, hv_digits;
static long hv_pos, hv_target;                             /* view top, pixels into the content */
static ULONG hv_cur;                                       /* cursor: byte index */
static int  hv_nib, hv_ascii, hv_hold;                     /* hv_hold: updates an arrow button has been held */

static UBYTE *hv_buf(void) { return hv_which ? ebuf : fbuf; }
static ULONG  hv_len(void) { return hv_which ? (ebuf_ok ? 512u : 0u) : fbuf_len; }
static int    hv_dirty(ULONG i) { const UBYTE *d = hv_which ? edirty : fdirty; return d ? (d[i >> 3] >> (i & 7)) & 1 : 0; }
static void   hv_mark(ULONG i) { UBYTE *d = hv_which ? edirty : fdirty; if (d) d[i >> 3] |= (UBYTE)(1u << (i & 7)); }
static long   hv_lines(void) { return (long)((hv_len() + 15u) / 16u); }
/* lines a step of the scroller: GTSC_Total is 16 bits, so a buffer of more than 32,767 lines (a 1 MB part has 65,536)
 * is counted in units of 2, 4, ... lines */
static int    hv_unit(void) { long l = hv_lines(); int k = 1; while (l / k > 32767) k <<= 1; return k; }

static struct Gadget *mk(int kind, struct Gadget *prev, int id, int x, int y, int w, int h, const char *text,
                         ULONG flags, ULONG t1, ULONG v1, ULONG t2, ULONG v2)
{
    struct NewGadget ng;
    memset(&ng, 0, sizeof ng);
    ng.ng_LeftEdge = x; ng.ng_TopEdge = y; ng.ng_Width = w; ng.ng_Height = h;
    ng.ng_GadgetText = (UBYTE *)text; ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = id; ng.ng_Flags = flags;
    ng.ng_VisualInfo = vi;
    return gad[id] = CreateGadget(kind, prev, &ng, t1, v1, t2, v2, TAG_DONE);
}

static void btn_page(int i, int x, int y, int w, const char *label)
{
    btn_init(&bt[i], BTN_ID + i, x, y, w, 12, label, ICON_NONE);
}

static const char *er_lbl[6];
static char er_txt[5][24];
static const char *cand_lbl[17];
static char cand_txt[16][40];

static void er_labels(void)
{
    int i, n = 0;
    if (part) for (i = 0; i < part->nerase && n < 5; i++) {
        ULONG sz = part->er[i].r[0].size;
        if (sz >= 1048576u) snprintf(er_txt[n], sizeof er_txt[n], "%lu MB (%02X)", (unsigned long)(sz / 1048576u), part->er[i].op);
        else snprintf(er_txt[n], sizeof er_txt[n], "%lu KB (%02X)", (unsigned long)(sz / 1024u), part->er[i].op);
        if (part->er[i].r[1].size) strcat(er_txt[n], "+");
        er_lbl[n] = er_txt[n]; n++;
    }
    if (!n) { snprintf(er_txt[0], sizeof er_txt[0], "64 KB (D8)"); er_lbl[0] = er_txt[0]; n = 1; }
    er_lbl[n] = NULL;
    if (er_i >= n) er_i = 0;
}

static void page_gadgets(struct Gadget *g)
{
    int x, y, i, j, w;
    switch (page) {
    case PG_DEV:
        /* group B: rows of 15 from PG_Y + 8 */
        x = GB_X + GW / 2;                                 /* gadgets right of the middle; labels right-aligned before */
        {   /* rows: clock and latency, two of checkboxes, mode, Apply, the EEPROM heading and its buttons, spread over
             * the group's height */
            int sp = (PG_H - 8 - 14 - 12) / 7;
            if (sp < 14) sp = 14;
            if (sp > 18) sp = 18;
            for (i = 0; i < 8; i++) dv_y[i] = PG_Y + 8 + i * sp;
            dv_y[6] = dv_y[5] + sp + 2;                    /* the rule under the heading */
            dv_y[7] = dv_y[6] + 5;
        }
        y = dv_y[0];
        g = mk(CYCLE_KIND, g, G_CLK, GB_X + 64, y, 96, 12, NULL, 0, GTCY_Labels, (ULONG)clk_lbl, GTCY_Active, (ULONG)s_clk);
        g = mk(INTEGER_KIND, g, G_LAT, GB_X + GW - 64, y, 40, 12, NULL, 0, GTIN_Number, (ULONG)s_lat, GTIN_MaxChars, 3);
        y = dv_y[1];
        g = mk(CHECKBOX_KIND, g, G_DIV5, GB_X + 12, y, 26, 11, NULL, 0, GTCB_Checked, (ULONG)s_div5, GTCB_Scaled, TRUE);
        g = mk(CHECKBOX_KIND, g, G_ADAPT, x + 4, y, 26, 11, NULL, 0, GTCB_Checked, (ULONG)s_adapt, GTCB_Scaled, TRUE);
        y = dv_y[2];
        g = mk(CHECKBOX_KIND, g, G_3PH, GB_X + 12, y, 26, 11, NULL, 0, GTCB_Checked, (ULONG)s_3ph, GTCB_Scaled, TRUE);
        g = mk(CHECKBOX_KIND, g, G_LOOP, x + 4, y, 26, 11, NULL, 0, GTCB_Checked, (ULONG)s_loop, GTCB_Scaled, TRUE);
        y = dv_y[3];
        g = mk(CYCLE_KIND, g, G_MODE, GB_X + 64, y, 160, 12, NULL, 0, GTCY_Labels, (ULONG)mode_lbl, GTCY_Active, (ULONG)s_mode);
        y = dv_y[4];
        w = 72;
        btn_page(B_APPLY, GB_X + GW / 2 - w - 4, y, w, "Apply");
        btn_page(B_RECON, GB_X + GW / 2 + 4, y, w + 16, "Reconnect");
        y = dv_y[7];
        btn_page(B_EE_RD, GB_X + GW / 2 - 3 * 64 / 2 - 8, y, 64, "Read");
        btn_page(B_EE_WR, GB_X + GW / 2 - 32, y, 64, "Write");
        btn_page(B_EE_ER, GB_X + GW / 2 + 32 + 8, y, 64, "Erase");
        btn_page(B_REFRESH, GA_X + GW / 2 - 40, PG_Y + PG_H - 18, 80, "Refresh");
        break;
    case PG_PINS:
        pin_cw = (GW - 16) / 8;
        for (j = 0; j < 2; j++) {
            pin_x0[j] = (j ? GB_X : GA_X) + 8;
            for (i = 0; i < 8; i++) {
                int cx = pin_x0[j] + i * pin_cw + pin_cw / 2, bw = pin_cw - 4 > 34 ? 34 : pin_cw - 4;
                btn_page(B_PIN_D0 + j * 8 + i, cx - bw / 2, PG_Y + 40, bw, (pin_dir[j] >> i) & 1 ? "Out" : "In");
                btn_page(B_PIN_L0 + j * 8 + i, cx - bw / 2, PG_Y + 55, bw, (pin_val[j] >> i) & 1 ? "Hi" : "Lo");
            }
        }
        pin_ly = PG_Y + 78;
        x = ox + win_w / 2 - (96 + 8 + 96 + 8 + 96) / 2;
        btn_page(B_ALLIN, x, pin_ly + 6, 96, "All Inputs");
        btn_page(B_READP, x + 104, pin_ly + 6, 96, "Read Pins");
        btn_page(B_AUTO, x + 208, pin_ly + 6, 96, "Auto Read");
        break;
    case PG_JTAG:
        jl_w = (win_w - 24) * 3 / 5;
        x = ox + 16 + jl_w + 8;                            /* group B's left edge */
        w = ox + win_w - 8 - x;
        g = mk(CYCLE_KIND, g, G_JCLK, x + 48, PG_Y + 8, 96, 12, NULL, 0, GTCY_Labels, (ULONG)clk_lbl, GTCY_Active, (ULONG)s_clk);
        btn_page(B_SCAN, x + w / 2 - 56, PG_Y + 26, 112, "Scan Chain");
        btn_page(B_LOOPI, x + w / 2 - 108, PG_Y + 44, 104, "Loop Inside");
        btn_page(B_LOOPE, x + w / 2 + 4, PG_Y + 44, 104, "Loop Wire");
        jt_px = x + 8; jt_pw = w - 16; jt_ty = PG_Y + 62;
        break;
    case PG_SPI:
        er_labels();
        y = PG_Y + 8;
        if (ncand > 1) {
            for (i = 0; i < ncand && i < 16; i++) {
                snprintf(cand_txt[i], sizeof cand_txt[i], "%s", cand[i]->name); cand_lbl[i] = cand_txt[i];
            }
            cand_lbl[i] = NULL;
            g = mk(CYCLE_KIND, g, G_PART, GA_X + 64, PG_Y + 8 + 8 * ROWP + 2, GW - 76, 12, NULL, 0,
                   GTCY_Labels, (ULONG)cand_lbl, GTCY_Active, (ULONG)cand_i);
        }
        btn_page(B_DETECT, GA_X + GW / 2 - 92, PG_Y + PG_H - 18, 88, "Detect");
        btn_page(B_UNPROT, GA_X + GW / 2 + 4, PG_Y + PG_H - 18, 88, "Unprotect");
        g = mk(STRING_KIND, g, G_START, GB_X + 64, y, 80, 12, NULL, 0, GTST_String, (ULONG)op_start, GTST_MaxChars, 10);
        g = mk(STRING_KIND, g, G_LEN, GB_X + GW - 92, y, 80, 12, NULL, 0, GTST_String, (ULONG)op_len, GTST_MaxChars, 10);
        y += 15;
        g = mk(CYCLE_KIND, g, G_ERASE, GB_X + 64, y, 120, 12, NULL, 0, GTCY_Labels, (ULONG)er_lbl, GTCY_Active, (ULONG)er_i);
        y += 17;
        w = (GW - 16 - 12) / 4;
        for (i = 0; i < 4; i++) btn_page(B_RD + i, GB_X + 8 + i * (w + 4), y, w, (const char *[]){ "Read", "Verify", "Erase", "Program" }[i]);
        y += 15;
        w = (GW - 16 - 8) / 3;
        btn_page(B_ALL, GB_X + 8, y, w, "Write All");
        btn_page(B_OPEN, GB_X + 8 + w + 4, y, w, "Open...");
        btn_page(B_SAVE, GB_X + 8 + 2 * (w + 4), y, w, "Save...");
        bar_y = y + 20;
        bar_x = GB_X + 14; bar_w = GW - 28;
        spi_ty = bar_y + 26;
        break;
    case PG_HEX: {
        int lines;
        hv_digits = hv_len() > 0x1000000u ? 7 : 6;
        hv_cols = hv_digits + 1 + 47 + 1 + 16;
        hv_w = hv_cols * 8 + 12;
        hv_x = ox + win_w / 2 - (hv_w + 18) / 2;
        hv_y = PG_Y + 8;
        hv_rows = (PG_H - 8 - 4 - 6 - 12 - 6) / 8;
        lines = (int)hv_lines();
        {
            struct NewGadget ng;
            memset(&ng, 0, sizeof ng);
            int k = hv_unit(), hs = hv_rows * 8 + 4 - 20;
            ng.ng_LeftEdge = hv_x + hv_w; ng.ng_TopEdge = hv_y; ng.ng_Width = 18; ng.ng_Height = hs;
            ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = G_HSCR; ng.ng_VisualInfo = vi;
            g = gad[G_HSCR] = CreateGadget(SCROLLER_KIND, g, &ng, PGA_Freedom, LORIENT_VERT,
                                           GTSC_Total, (ULONG)((lines + k - 1) / k), GTSC_Visible, (ULONG)(hv_rows / k ? hv_rows / k : 1),
                                           GTSC_Top, (ULONG)(hv_pos / 8 / k), GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
            btn_init(&bt[B_HUP], BTN_ID + B_HUP, hv_x + hv_w, hv_y + hs, 18, 10, "", ICON_UP);
            btn_init(&bt[B_HDN], BTN_ID + B_HDN, hv_x + hv_w, hv_y + hs + 10, 18, 10, "", ICON_DOWN);
        }
        y = hv_y + hv_rows * 8 + 4 + 6;
        x = ox + win_w / 2 - (40 + 72 + 12 + 5 * 68 + 4 * 4) / 2;
        g = mk(STRING_KIND, g, G_GOTO, x + 40, y, 72, 12, NULL, 0, GTST_String, (ULONG)"", GTST_MaxChars, 10);
        x += 40 + 72 + 12;
        btn_page(B_HFLASH, x, y, 68, "Flash");
        btn_page(B_HEE, x + 72, y, 68, "EEPROM");
        btn_page(B_HOPEN, x + 144, y, 68, "Open...");
        btn_page(B_HSAVE, x + 216, y, 68, "Save...");
        btn_page(B_HFILL, x + 288, y, 68, "Fill FF");
        break;
    }
    }
    (void)g;
}

static void tabs_init(void)
{
    int i, w = 96, x = ox + win_w / 2 - (PG_N * w + (PG_N - 1) * 4) / 2;
    for (i = 0; i < PG_N; i++) {
        btn_init(&bt[B_TAB0 + i], BTN_ID + B_TAB0 + i, x + i * (w + 4), TAB_Y, w, 14, pg_lbl[i], ICON_NONE);
        bt[B_TAB0 + i].sel = i == page;
    }
}

/* ---- drawing of the pages ------------------------------------------------------------------------------------- */

static const char *const ci_lbl[10] = { "USB port", "Device", "Channel", "Mode", "Packet size", "Latency",
                                        "Pins (0x0C)", "Bytes in", "Bytes out", "Errors" };
static const char *const sp_lbl[8] = { "JEDEC ID", "Vendor", "Part", "Size", "Page", "Erase blocks", "Supply",
                                       "Addressing" };

static void draw_status(void)
{
    int x = ox + 8, w = win_w - 16;
    SetAPen(rp, P_TROUGH); RectFill(rp, x + 2, ST_Y + 1, x + w - 3, ST_Y + 10);
    SetAPen(rp, s_msg_pen); SetDrMd(rp, JAM1);
    Move(rp, x + 6, ST_Y + 2 + rp->TxBaseline);
    Text(rp, (CONST_STRPTR)s_msg, (ULONG)(strlen(s_msg) > (size_t)((w - 12) / 8) ? (w - 12) / 8 : strlen(s_msg)));
    s_msg_new = 0;
}

static void ci_values(void)
{
    int i;
    if (page != PG_DEV) return;
    for (i = 0; i < 10; i++) value_r(ci_old[i], GA_X + GW - 12, PG_Y + 8 + i * ROWP, (GW - 24 - dev_tx) & ~7, P_BLACK, ci_txt[i]);
}

static void sp_values(void)
{
    char t[8][40], a[16];
    int i;
    if (page != PG_SPI) return;
    for (i = 0; i < 8; i++) t[i][0] = 0;
    if (id9f[0] || id9f[1]) snprintf(t[0], 40, "%02X %02X %02X", id9f[0], id9f[1], id9f[2]);
    if (part) {
        snprintf(t[1], 40, "%s", part->vendor);
        snprintf(t[2], 40, "%s", part->name);
        fmt_thousands(a, part->size_kb); snprintf(t[3], 40, "%s KB", a);
        snprintf(t[4], 40, "%u bytes", part->page);
        if (part->nerase) {
            int k; t[5][0] = 0;
            for (k = 0; k < part->nerase && strlen(t[5]) < 30; k++) {
                char e[8]; snprintf(e, sizeof e, "%s%02X", k ? " " : "", part->er[k].op); strcat(t[5], e);
            }
        }
        snprintf(t[6], 40, "%u.%u-%u.%u V", part->vmin / 1000, part->vmin % 1000 / 100, part->vmax / 1000, part->vmax % 1000 / 100);
        snprintf(t[7], 40, "%s", part->addr4 ? "4 bytes" : "3 bytes");
    } else if (part_size) {
        fmt_thousands(a, part_size / 1024u); snprintf(t[3], 40, "%s KB", a);
        snprintf(t[2], 40, "not in the table");
    }
    for (i = 0; i < 8; i++) value_r(sp_old[i], GA_X + GW - 12, PG_Y + 8 + i * ROWP, (GW - 24 - 104) & ~7, P_BLACK, t[i]);
}

static void pins_lamps(void)
{
    int j, i;
    if (page != PG_PINS) return;
    for (j = 0; j < 2; j++)
        for (i = 0; i < 8; i++) {
            int cx = pin_x0[j] + i * pin_cw + pin_cw / 2, on = (pin_in[j] >> i) & 1;
            SetAPen(rp, !pin_read_ok ? P_GREY : on ? P_GRN : P_GRN_D);
            RectFill(rp, cx - 8, PG_Y + 29, cx + 7, PG_Y + 34);
        }
    {
        char t[64];
        snprintf(t, sizeof t, "ADBUS %02X   ACBUS %02X", pin_in[0], pin_in[1]);
        text_c(rp, ox + win_w / 2, pin_ly + 24, pin_read_ok ? P_BLACK : P_GREY, t);
    }
}

static void jl_draw(void)
{
    int i;
    if (page != PG_JTAG) return;
    SetAPen(rp, P_TROUGH); RectFill(rp, jl_x + 2, jl_y + 1, jl_x + jl_w - 11, jl_y + jl_rows * 8 + 4);
    SetDrMd(rp, JAM1);
    for (i = 0; i < jl_n && i < jl_rows; i++) {
        SetAPen(rp, jl_pen[i]);
        Move(rp, jl_x + 6, jl_y + 3 + i * 8 + rp->TxBaseline);
        Text(rp, (CONST_STRPTR)jl_txt[i], (ULONG)(strlen(jl_txt[i]) > (size_t)((jl_w - 20) / 8) ? (jl_w - 20) / 8 : strlen(jl_txt[i])));
    }
}

static void jt_values(void)
{
    char a[16], t[3][40];
    int i;
    if (page != PG_JTAG) return;
    panel(rp, jt_px, jt_ty, jt_pw, 12);
    panel_text(rp, jt_px, jt_ty, jt_pw, jt_ty + 2, jt_res < 0 ? P_GREY : jt_res ? P_GRN : P_RED,
               jt_res < 0 ? "READY" : jt_res ? "PASS" : "FAIL", 1);
    fmt_thousands(a, jt_bytes); snprintf(t[0], 40, "%s", a);
    fmt_thousands(a, jt_errs); snprintf(t[1], 40, "%s", a);
    fmt_thousands(a, jt_bps); snprintf(t[2], 40, "%s", a);
    for (i = 0; i < 3; i++) value_r(jt_old[i], jt_px + jt_pw - 4, jt_ty + 18 + i * ROWP, 96, jt_res == 0 && i == 1 ? P_TXT_RED : P_BLACK, t[i]);
}

/* ---- the progress of a job: a bar of pixel resolution that eases toward the bytes done on every display update
 * (an eighth of the distance, at least a sixteenth of a pixel), and under it fixed fields drawn only where characters
 * change: per cent done (tenths), the time since the start (m:ss) and bytes a second (the bytes done since the 250 ms
 * sample of 1 s before, while running; the job's average at the end). */
static ULONG pr_done, pr_total;                            /* bytes done of the phase */
static LONG  pr_tq, pr_dq, pr_drawn = -1;                  /* target and shown width, 1/16 pixel; drawn width, pixels */
static int   pr_pen = P_GRN, pr_tn, pr_end;
static unsigned long long pr_t0, pr_ts[5];                /* samples every 250 ms: five span the 1 s window */
static ULONG pr_bs[5], pr_rate;
static char  pr_old[3][40];

static void pr_text(void)
{
    char t[16], a[20];
    ULONG pm, sec;
    int n = 0;
    if (page != PG_SPI) return;
    pm = !pr_total ? 0 : pr_total > 4000000u ? (pr_done >> 8) * 1000u / (pr_total >> 8) : pr_done * 1000u / pr_total;
    if (pm > 1000u) pm = 1000u;
    if (pm >= 1000u) t[n++] = '1';
    if (pm >= 100u) t[n++] = (char)('0' + pm / 100u % 10u);
    t[n++] = (char)('0' + pm / 10u % 10u); t[n++] = '.'; t[n++] = (char)('0' + pm % 10u);
    t[n++] = ' '; t[n++] = '%'; t[n] = 0;
    value_r(pr_old[0], bar_x + 56, bar_y + 13, 56, P_BLACK, t);
    sec = pr_t0 && efreq ? (ULONG)((pr_end ? pr_ts[(pr_tn - 1) % 5] : eclock()) - pr_t0) / efreq : 0;
    n = 0;
    if (sec >= 600u) t[n++] = (char)('0' + sec / 600u % 10u);
    t[n++] = (char)('0' + sec / 60u % 10u); t[n++] = ':';
    t[n++] = (char)('0' + sec % 60u / 10u); t[n++] = (char)('0' + sec % 10u); t[n] = 0;
    value_r(pr_old[1], bar_x + 112, bar_y + 13, 40, P_BLACK, t);
    fmt_thousands(a, pr_rate); strcat(a, " bytes/s");
    value_r(pr_old[2], bar_x + bar_w, bar_y + 13, 120, P_BLACK, a);
}

static void bar_frame(void)
{
    int i;
    DrawBevelBox(rp, bar_x - 4, bar_y - 2, bar_w + 8, 12, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(rp, P_TROUGH); RectFill(rp, bar_x - 2, bar_y - 1, bar_x + bar_w + 1, bar_y + 8);
    pr_drawn = 0;
    for (i = 0; i < 3; i++) pr_old[i][0] = 0x7F;
    pr_text();
}

/* the bar drawn at the shown width: only the columns that changed */
static void bar_draw(void)
{
    LONG px = pr_dq >> 4;
    if (page != PG_SPI || pr_drawn < 0) return;
    if (px > bar_w) px = bar_w;
    if (px > pr_drawn) { SetAPen(rp, pr_pen); RectFill(rp, bar_x + pr_drawn, bar_y, bar_x + px - 1, bar_y + 7); }
    else if (px < pr_drawn) { SetAPen(rp, P_TROUGH); RectFill(rp, bar_x + px, bar_y, bar_x + pr_drawn - 1, bar_y + 7); }
    pr_drawn = px;
}

/* a phase begins: the bar empties and takes the phase's colour */
static void pr_start(ULONG total, int pen)
{
    pr_done = 0; pr_total = total; pr_tq = pr_dq = 0; pr_pen = pen; pr_end = 0; pr_rate = 0;
    pr_t0 = eclock(); pr_tn = 1; pr_ts[0] = pr_t0; pr_bs[0] = 0;
    bar_draw();
    pr_text();
}

/* bytes done of the phase */
static void pr_set(ULONG done)
{
    pr_done = done;
    pr_tq = !pr_total ? 0 : (LONG)(((unsigned long long)done * (ULONG)bar_w << 4) / pr_total);
}

/* the phase ended (ok) or stopped: the average rate over the phase */
static void pr_stop(int ok)
{
    unsigned long long now = eclock();
    if (ok) pr_set(pr_total);
    else pr_pen = P_RED;
    pr_rate = now > pr_t0 ? (ULONG)((unsigned long long)pr_done * efreq / (now - pr_t0)) : 0;
    pr_ts[pr_tn++ % 5] = now; pr_end = 1;
    if (!ok && pr_drawn > 0) { LONG px = pr_drawn; pr_drawn = 0; pr_dq = (LONG)px << 4; bar_draw(); }
    pr_text();
}

/* one display update: the bar eases toward its target; the text four times a second */
static void pr_tick(ULONG n)
{
    if (pr_dq != pr_tq) {
        LONG d = (pr_tq - pr_dq) / 8;
        if (d == 0) d = pr_tq > pr_dq ? 1 : -1;
        pr_dq += d;
        bar_draw();
    }
    if (!(n % 15u) && pr_t0 && !pr_end) {                    /* 250 ms: a sample, and the rate over the last 1 s */
        unsigned long long now = eclock();
        int k = pr_tn % 5, o = pr_tn >= 4 ? (pr_tn - 4) % 5 : 0;   /* o: the sample 1 s before, or the start */
        pr_ts[k] = now; pr_bs[k] = pr_done;
        if (now > pr_ts[o]) pr_rate = (ULONG)((unsigned long long)(pr_done - pr_bs[o]) * efreq / (now - pr_ts[o]));
        pr_tn++;
        pr_text();
    } else if (!(n % 4u) && pr_t0 && !pr_end) pr_text();
}

static void hv_full(void);

/* the function of ADBUS/ACBUS pin i shown on the Pins page: JTAG, and chip select and CRESET as the SPI is set */
static const char *pin_label(int j, int i)
{
    spi_cfg_t c;
    spi_config_get(&c);
    if (!j && i == c.cs_pin) return "CS";
    if (!j && i == 7 && c.creset != 2) return "CRST";
    return pin_fn[j][i][0] ? pin_fn[j][i] : "-";
}

/* CRC-32 (PKZIP) of the file last loaded into the flash buffer and of the chip's bytes last read or verified */
static ULONG crc_file, crc_file_n, crc_chip, crc_chip_n;
static int   crc_file_ok, crc_chip_ok;
static char  crc_old[2][12];

static void crc_hex(char *t, ULONG v, int ok)
{
    int i;
    for (i = 7; i >= 0; i--) { t[i] = ok ? "0123456789ABCDEF"[v & 15u] : '-'; v >>= 4; }
    t[8] = 0;
}

/* the line "CRC32  file XXXXXXXX  chip XXXXXXXX": the chip's value green when it equals the file's over the same
 * length, red when it differs */
static void crc_line(int all)
{
    char t[12];
    int x = GB_X + GW / 2 - 35 * 4, pen = P_BLACK;
    if (page != PG_SPI) return;
    if (all) {
        bold(rp, 1); pen_text(rp, x, spi_ty, P_TXT_BLUE, "CRC32"); bold(rp, 0);
        pen_text(rp, x + 7 * 8, spi_ty, P_BLACK, "file");
        pen_text(rp, x + 22 * 8, spi_ty, P_BLACK, "chip");
        crc_old[0][0] = crc_old[1][0] = 0x7F;
    }
    crc_hex(t, crc_file, crc_file_ok);
    value_r(crc_old[0], x + 20 * 8, spi_ty, 64, crc_file_ok ? P_BLACK : P_GREY, t);
    if (crc_chip_ok && crc_file_ok && crc_chip_n == crc_file_n) pen = crc_chip == crc_file ? P_TXT_GRN : P_TXT_RED;
    crc_hex(t, crc_chip, crc_chip_ok);
    value_r(crc_old[1], x + 35 * 8, spi_ty, 64, crc_chip_ok ? pen : P_GREY, t);
}

static void draw_page(void)
{
    int i, j;
    SetAPen(rp, P_BG); RectFill(rp, ox, PG_Y - 6, ox + win_w - 1, ST_Y - 2);
    switch (page) {
    case PG_DEV:
        group(GA_X, PG_Y, GW, PG_H, "Chip");
        group(GB_X, PG_Y, GW, PG_H, "MPSSE and Control");
        dev_tx = 0;
        for (i = 0; i < 10; i++) { pen_text(rp, GA_X + 12, PG_Y + 8 + i * ROWP, P_BLACK, ci_lbl[i]); ci_old[i][0] = 0x7F; }
        dev_tx = 96;
        text_r(rp, GB_X + 58, dv_y[0] + 2, P_BLACK, "Clock");
        text_r(rp, GB_X + GW - 70, dv_y[0] + 2, P_BLACK, "Latency");
        pen_text(rp, GB_X + GW - 20, dv_y[0] + 2, P_BLACK, "ms");
        pen_text(rp, GB_X + 42, dv_y[1] + 2, P_BLACK, "Divide by 5");
        pen_text(rp, GB_X + GW / 2 + 34, dv_y[1] + 2, P_BLACK, "Adaptive");
        pen_text(rp, GB_X + 42, dv_y[2] + 2, P_BLACK, "Three-phase");
        pen_text(rp, GB_X + GW / 2 + 34, dv_y[2] + 2, P_BLACK, "Loopback");
        text_r(rp, GB_X + 58, dv_y[3] + 2, P_BLACK, "Mode");
        text_c(rp, GB_X + GW / 2, dv_y[5] + 2, P_TXT_BLUE, "Configuration EEPROM");
        SetAPen(rp, P_GREY); RectFill(rp, GB_X + 12, dv_y[6], GB_X + GW - 13, dv_y[6]);
        ci_values();
        break;
    case PG_PINS:
        group(GA_X, PG_Y, GW, 72, "ADBUS, Channel A Low Byte");
        group(GB_X, PG_Y, GW, 72, "ACBUS, Channel A High Byte");
        for (j = 0; j < 2; j++)
            for (i = 0; i < 8; i++) {
                char n[8];
                int cx = pin_x0[j] + i * pin_cw + pin_cw / 2;
                snprintf(n, sizeof n, "%s%d", j ? "AC" : "AD", i);
                text_c(rp, cx, PG_Y + 7, P_BLACK, n);
                text_c(rp, cx, PG_Y + 17, P_TXT_BLUE, pin_label(j, i));
                DrawBevelBox(rp, cx - 10, PG_Y + 28, 20, 8, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
            }
        text_c(rp, ox + win_w / 2, pin_ly + 36, P_TXT_BLUE, "Out: the pin drives its latched level.  In: the lamp shows the level read.");
        pins_lamps();
        break;
    case PG_JTAG: {
        int gx = ox + 16 + jl_w + 8 - 8, gw = ox + win_w - 8 - gx;
        group(ox + 8, PG_Y, jl_w + 8, PG_H, "Chain");
        group(gx, PG_Y, gw, PG_H, "Test");
        jl_x = ox + 16; jl_y = PG_Y + 8; jl_rows = (PG_H - 8 - 6 - 6) / 8;
        panel(rp, jl_x, jl_y, jl_w - 8, jl_rows * 8 + 6);
        text_r(rp, gx + 52, PG_Y + 10, P_BLACK, "TCK");
        for (i = 0; i < 3; i++) pen_text(rp, jt_px + 4, jt_ty + 18 + i * ROWP, P_BLACK, (const char *[]){ "Bytes", "Bit errors", "Bytes/s" }[i]);
        for (i = 0; i < 3; i++) jt_old[i][0] = 0x7F;
        text_c(rp, gx + gw / 2, PG_Y + PG_H - 22, P_TXT_BLUE, "AD0 TCK  AD1 TDI");
        text_c(rp, gx + gw / 2, PG_Y + PG_H - 13, P_TXT_BLUE, "AD2 TDO  AD3 TMS");
        jl_draw(); jt_values();
        break;
    }
    case PG_SPI:
        group(GA_X, PG_Y, GW, PG_H, "Part");
        group(GB_X, PG_Y, GW, PG_H, "Flash");
        for (i = 0; i < 8; i++) { pen_text(rp, GA_X + 12, PG_Y + 8 + i * ROWP, P_BLACK, sp_lbl[i]); sp_old[i][0] = 0x7F; }
        if (ncand > 1) text_r(rp, GA_X + 58, PG_Y + 10 + 8 * ROWP, P_BLACK, "Match");
        text_r(rp, GB_X + 58, PG_Y + 10, P_BLACK, "Start");
        text_r(rp, GB_X + GW - 98, PG_Y + 10, P_BLACK, "Length");
        text_r(rp, GB_X + 58, PG_Y + 25, P_BLACK, "Erase");
        bar_frame(); bar_draw();
        sp_values();
        {
            char t[48];
            spi_cfg_t c;
            spi_config_get(&c);
            snprintf(t, sizeof t, "Mode %d, %s, CS AD%d %s", c.cpol * 2 + c.cpha, c.lsb ? "LSB first" : "MSB first",
                     c.cs_pin, c.cs_high ? "high" : "low");
            text_c(rp, GB_X + GW / 2, PG_Y + PG_H - 12, P_TXT_BLUE, t);
            crc_line(1);
        }
        break;
    case PG_HEX: {
        char t[64], a[16];
        fmt_thousands(a, hv_len());
        snprintf(t, sizeof t, "%s Buffer, %s Bytes", hv_which ? "EEPROM" : "Flash", a);
        group(ox + 8, PG_Y, win_w - 16, PG_H, t);
        panel(rp, hv_x, hv_y, hv_w, hv_rows * 8 + 4);
        text_r(rp, ox + win_w / 2 - (40 + 72 + 12 + 5 * 68 + 4 * 4) / 2 + 36, hv_y + hv_rows * 8 + 12, P_BLACK, "Go to");
        if (!hv_len()) {
            SetAPen(rp, P_GREY); SetDrMd(rp, JAM1);
            {   static const char *const t[2] = { "The buffer is empty.", "Read the flash or the EEPROM, or open a file." };
                int k;
                for (k = 0; k < 2; k++) { Move(rp, hv_x + hv_w / 2 - tlen(rp, t[k]) / 2, hv_y + hv_rows * 4 - 6 + k * 12 + rp->TxBaseline);
                                          Text(rp, (CONST_STRPTR)t[k], strlen(t[k])); }
            }
        } else hv_full();
        break;
    }
    }
}

static void buttons_state(void)
{
    int i;
    for (i = 0; i < PG_N; i++) btn_set(&bt[B_TAB0 + i], i == page, 0);
    if (page == PG_PINS) btn_set(&bt[B_AUTO], pin_auto, 0);
    if (page == PG_HEX) { btn_set(&bt[B_HFLASH], hv_which == 0, 0); btn_set(&bt[B_HEE], hv_which == 1, !ebuf_ok); }
}

/* the window's gadgets and drawing for the page and size now */
static int layout(void)
{
    struct Gadget *g;
    int i;
    for (i = 0; i < B_N; i++) if (bt[i].on && bt[i].w) { RemoveGadget(win, &bt[i].g); bt[i].w = NULL; }
    if (glist) { RemoveGList(win, glist, -1); FreeGadgets(glist); glist = NULL; }
    for (i = 0; i < G_N; i++) gad[i] = NULL;
    for (i = 0; i < B_N; i++) bt[i].on = 0;
    ox = win->BorderLeft; oy = win->BorderTop;
    win_w = win->Width - ox - win->BorderRight;
    win_h = win->Height - oy - win->BorderBottom;
    ROWP = win_h >= 230 ? 11 : 10;
    SetAPen(rp, P_BG); RectFill(rp, ox, oy, ox + win_w - 1, oy + win_h - 1);
    if (!(g = CreateContext(&glist))) return 0;
    tabs_init();
    page_gadgets(g);
    panel(rp, ox + 8, ST_Y, win_w - 16, 12);
    draw_page();                                           /* the page first: the gadgets are drawn over it */
    AddGList(win, glist, (UWORD)~0, -1, NULL);
    RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    for (i = 0; i < B_N; i++) if (bt[i].on) { bt[i].w = win; AddGadget(win, &bt[i].g, ~0); }
    buttons_state();
    for (i = 0; i < B_N; i++) if (bt[i].on) { bt[i].drawn = -1; btn_draw(&bt[i]); }
    draw_status();
    return 1;
}

/* ---- the hex view ---------------------------------------------------------------------------------------------- */

static void hv_row_render(long line)
{
    UBYTE *b = hv_buf();
    ULONG len = hv_len(), base = (ULONG)line * 16u, i;
    char t[16];
    int x = 6, cx;
    SetAPen(&row_rp, P_TROUGH); RectFill(&row_rp, 0, 0, hv_w - 5, 7);
    if (line < 0 || base >= len || !b) return;
    SetDrMd(&row_rp, JAM1);
    snprintf(t, sizeof t, "%0*lX", hv_digits, (unsigned long)base);
    SetAPen(&row_rp, P_AMB); Move(&row_rp, x, row_rp.TxBaseline); Text(&row_rp, (CONST_STRPTR)t, (ULONG)hv_digits);
    for (i = 0; i < 16u && base + i < len; i++) {
        UBYTE v = b[base + i];
        int cur = base + i == hv_cur, ch = v >= 0x20 && v < 0x7F;
        char h[2], c = ch ? (char)v : '.';
        h[0] = "0123456789ABCDEF"[v >> 4]; h[1] = "0123456789ABCDEF"[v & 15];
        cx = x + (hv_digits + 1 + (int)i * 3) * 8;
        if (cur && !hv_ascii) { SetAPen(&row_rp, P_AMB); RectFill(&row_rp, cx + hv_nib * 8, 0, cx + hv_nib * 8 + 7, 7); }
        SetAPen(&row_rp, hv_dirty(base + i) ? P_RED : P_GRN);
        Move(&row_rp, cx, row_rp.TxBaseline); Text(&row_rp, (CONST_STRPTR)h, 2);
        if (cur && !hv_ascii) {
            SetAPen(&row_rp, P_TROUGH); Move(&row_rp, cx + hv_nib * 8, row_rp.TxBaseline);
            Text(&row_rp, (CONST_STRPTR)&h[hv_nib], 1);
        }
        cx = x + (hv_digits + 1 + 47 + 1 + (int)i) * 8;
        if (cur && hv_ascii) { SetAPen(&row_rp, P_AMB); RectFill(&row_rp, cx, 0, cx + 7, 7); }
        SetAPen(&row_rp, cur && hv_ascii ? P_TROUGH : (hv_dirty(base + i) ? P_RED : P_GRN));
        Move(&row_rp, cx, row_rp.TxBaseline); Text(&row_rp, (CONST_STRPTR)&c, 1);
    }
}

/* the content's pixel lines [c0, c1] to the screen, the view's top at hv_pos */
static void hv_band(long c0, long c1)
{
    long l, top = hv_pos, bot = hv_pos + hv_rows * 8 - 1;
    if (c0 < top) c0 = top;
    if (c1 > bot) c1 = bot;
    for (l = c0 / 8; l <= c1 / 8; l++) {
        long y0 = l * 8 > c0 ? l * 8 : c0, y1 = l * 8 + 7 < c1 ? l * 8 + 7 : c1;
        if (y0 > y1) continue;
        hv_row_render(l);
        BltBitMapRastPort(row_bm, 0, (WORD)(y0 - l * 8), rp, hv_x + 2, (WORD)(hv_y + 2 + y0 - top), hv_w - 4,
                          (WORD)(y1 - y0 + 1), 0xC0);
    }
}

static void hv_full(void) { if (page == PG_HEX && row_bm) hv_band(hv_pos, hv_pos + hv_rows * 8 - 1); }

static void hv_line(ULONG idx) { long l = (long)(idx / 16u); hv_band(l * 8, l * 8 + 7); }

static void hv_scroller(void)
{
    if (page == PG_HEX && gad[G_HSCR])
        GT_SetGadgetAttrs(gad[G_HSCR], win, NULL, GTSC_Top, (ULONG)((hv_target + 7) / 8 / hv_unit()), TAG_DONE);
}

static void hv_clamp_target(void)
{
    long max = (hv_lines() - hv_rows) * 8;
    if (max < 0) max = 0;
    if (hv_target > max) hv_target = max;
    if (hv_target < 0) hv_target = 0;
}

/* one step of the smooth scroll toward hv_target */
static void hv_tick(void)
{
    long d = hv_target - hv_pos, step, h = hv_rows * 8;
    if (!d || page != PG_HEX || !row_bm) return;
    step = (d > 0 ? d : -d) / 4;
    if (step < 2) step = (d > 0 ? d : -d) < 2 ? 1 : 2;
    if (step > h / 2) step = h / 2;
    if (step > (d > 0 ? d : -d)) step = d > 0 ? d : -d;
    if ((d > 0 ? d : -d) > h * 4) { hv_pos = hv_target; hv_full(); return; }   /* far away: jump */
    SetWrMsk(rp, 3); SetBPen(rp, P_TROUGH);                /* pens 4-7 differ in planes 0 and 1 only */
    ScrollRaster(rp, 0, (WORD)(d > 0 ? step : -step), hv_x + 2, hv_y + 2, hv_x + hv_w - 3, hv_y + 1 + (WORD)h);
    SetWrMsk(rp, 0xFF);
    if (d > 0) { hv_pos += step; hv_band(hv_pos + h - step, hv_pos + h - 1); }
    else { hv_pos -= step; hv_band(hv_pos, hv_pos + step - 1); }
}

static void hv_show_cursor(void)
{
    long l = (long)(hv_cur / 16u);
    if (l * 8 < hv_target) hv_target = l * 8;
    if (l * 8 + 8 > hv_target + hv_rows * 8) hv_target = l * 8 + 8 - hv_rows * 8;
    hv_clamp_target();
    hv_scroller();
}

static void hv_move(long delta)
{
    ULONG old = hv_cur, len = hv_len();
    long n;
    if (!len) return;
    n = (long)hv_cur + delta;
    if (n < 0) n = 0;
    if ((ULONG)n >= len) n = (long)len - 1;
    hv_cur = (ULONG)n;
    hv_nib = 0;
    hv_line(old); hv_line(hv_cur);
    hv_show_cursor();
}

/* an arrow button: a line, as the cursor key; with the cursor outside the view (after the scroller moved it), the
 * cursor first goes to the view's top (up) or bottom (down) line, same column */
static void hv_arrow(int dir)
{
    long l = (long)(hv_cur / 16u), top = (hv_target + 7) / 8, bot = top + hv_rows - 1;
    if (!hv_len()) return;
    if (l < top || l > bot) {
        ULONG old = hv_cur;
        long nl = dir < 0 ? top : bot;
        if (nl >= hv_lines()) nl = hv_lines() - 1;
        hv_cur = (ULONG)nl * 16u + hv_cur % 16u;
        if (hv_cur >= hv_len()) hv_cur = hv_len() - 1;
        hv_nib = 0; hv_line(old); hv_line(hv_cur);
        return;
    }
    hv_move(dir * 16);
}

static void hv_key(int c)
{
    UBYTE *b = hv_buf();
    if (!b || !hv_len()) return;
    if (hv_ascii) {
        if (c < 0x20 || c == 0x7F) return;
        b[hv_cur] = (UBYTE)c; hv_mark(hv_cur);
        hv_move(1);
        return;
    }
    if (c >= 'a' && c <= 'f') c -= 0x20;
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return;
    {
        int v = c <= '9' ? c - '0' : c - 'A' + 10;
        if (hv_nib == 0) b[hv_cur] = (UBYTE)((b[hv_cur] & 0x0F) | (v << 4));
        else b[hv_cur] = (UBYTE)((b[hv_cur] & 0xF0) | v);
        hv_mark(hv_cur);
        if (hv_nib == 0) { hv_nib = 1; hv_line(hv_cur); }
        else hv_move(1);
    }
}

static void hv_click(int mx, int my)
{
    int col = (mx - hv_x - 8) / 8, row = (int)((my - hv_y - 2 + hv_pos % 8) / 8);
    long line = hv_pos / 8 + row;
    ULONG old = hv_cur, idx;
    int c0 = hv_digits + 1, a0 = hv_digits + 1 + 47 + 1, i;
    if (my < hv_y || my >= hv_y + hv_rows * 8 + 2 || mx < hv_x || mx >= hv_x + hv_w) return;
    if (col >= a0 && col < a0 + 16) { i = col - a0; hv_ascii = 1; hv_nib = 0; }
    else if (col >= c0 && col < c0 + 47) { i = (col - c0) / 3; hv_ascii = 0; hv_nib = (col - c0) % 3 == 1; }
    else return;
    idx = (ULONG)line * 16u + (ULONG)i;
    if (idx >= hv_len()) return;
    hv_cur = idx;
    hv_line(old); hv_line(hv_cur);
}

/* ---- the chip: `ftdi' and the MPSSE settings ------------------------------------------------------------------ */

static char fwo[2048];

static void chip_refresh(void)
{
    const char *p;
    unsigned int port = 0, vid = 0, pid = 0, idx = 0, ifc = 0, pk = 0, lat = 0, pins = 0, in = 0, out = 0, ei = 0, eo = 0, dr = 0;
    char mode[16] = "";
    int i;
    for (i = 0; i < 10; i++) ci_txt[i][0] = 0;
    if (fw_cmd("ftdi", fwo, sizeof fwo) < 0) { say(P_RED, "Firmware: %s", fwo); ci_values(); return; }
    p = strstr(fwo, "ftdi: ");
    if (!p || strncmp(p + 6, "ok", 2)) { say(P_RED, "Firmware: %s", p ? p + 6 : fwo); snprintf(ci_txt[0], 28, "no device"); ci_values(); return; }
    sscanf(p, "ftdi: ok port %u vid %x pid %x index %u iface %u mode %15s packets %u", &port, &vid, &pid, &idx, &ifc, mode, &pk);
    if ((p = strstr(fwo, "latency ")) != NULL) sscanf(p, "latency %u", &lat);
    if ((p = strstr(fwo, "pins ")) != NULL) sscanf(p, "pins %x", &pins);
    if ((p = strstr(fwo, " in ")) != NULL) sscanf(p, " in %u out %u errors %u/%u dropped %u", &in, &out, &ei, &eo, &dr);
    snprintf(ci_txt[0], 28, "%u", port);
    snprintf(ci_txt[1], 28, "%04X:%04X%s", vid, pid, pid == 0x6010 ? " FT2232H" : pid == 0x6014 ? " FT232H" : pid == 0x6011 ? " FT4232H" : "");
    snprintf(ci_txt[2], 28, "%s (iface %u)", idx ? (idx == 1 ? "A" : "B") : "single", ifc);
    snprintf(ci_txt[3], 28, "%s", mode);
    snprintf(ci_txt[4], 28, "%u bytes", pk);
    snprintf(ci_txt[5], 28, "%u ms", lat);
    snprintf(ci_txt[6], 28, "%02X", pins);
    { char a[16]; fmt_thousands(a, in); snprintf(ci_txt[7], 28, "%s", a); fmt_thousands(a, out); snprintf(ci_txt[8], 28, "%s", a); }
    snprintf(ci_txt[9], 28, "%u in, %u out, %u lost", ei, eo, dr);
    s_mode = strcmp(mode, "mpsse") != 0;
    s_lat = (int)lat;
    if (page == PG_DEV && gad[G_LAT]) GT_SetGadgetAttrs(gad[G_LAT], win, NULL, GTIN_Number, (ULONG)s_lat, TAG_DONE);
    if (page == PG_DEV && gad[G_MODE]) GT_SetGadgetAttrs(gad[G_MODE], win, NULL, GTCY_Active, (ULONG)s_mode, TAG_DONE);
    ci_values();
}

static unsigned long tck_hz(void) { return (s_div5 ? 6000000ul : 30000000ul) / (clk_div[s_clk] + 1ul); }

static int mpsse_apply(void)
{
    UBYTE c[12];
    int n = 0;
    if (!lk_open) return -1;
    c[n++] = s_div5 ? 0x8B : 0x8A; c[n++] = s_adapt ? 0x96 : 0x97; c[n++] = s_3ph ? 0x8C : 0x8D;
    c[n++] = s_loop ? 0x84 : 0x85;
    c[n++] = 0x86; c[n++] = (UBYTE)clk_div[s_clk]; c[n++] = (UBYTE)(clk_div[s_clk] >> 8);
    return mpsse_cmd(&lk, c, n, NULL, 0);
}

static void reconnect(void)
{
    char why[160];
    if (link_open(dev_name, dev_unit, 30000000ul / (clk_div[s_clk] + 1ul), why, sizeof why)) { say(P_RED, "%s", why); return; }
    if (mpsse_apply()) { say(P_RED, "MPSSE settings not sent (error %d)", lk.err); return; }
    { char a[16]; fmt_thousands(a, tck_hz()); say(P_GRN, "MPSSE through %s unit %lu: sync 0xFA 0xAA, TCK %s Hz", dev_name, dev_unit, a); }
}

/* the chip's latency timer (SET_LATENCY_TIMER 0x09) through `ftdi ctl'; 0, or -1 with the reason said */
static int lat_set(LONG v)
{
    char line[64];
    if (v < 1) v = 1;
    if (v > 255) v = 255;
    if (v == s_lat) return 0;
    snprintf(line, sizeof line, "ftdi ctl 40:09 %lx -", (unsigned long)v);
    if (fw_cmd(line, fwo, sizeof fwo) < 0 || fw_ftdi_bytes(fwo, NULL, 0) < 0) { say(P_RED, "Latency not set: %s", fwo); return -1; }
    s_lat = (int)v;
    return 0;
}

static void settings_apply(void)
{
    LONG v = 0;
    if (gad[G_LAT]) GT_GetGadgetAttrs(gad[G_LAT], win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    if (lat_set(v)) return;
    if (mpsse_apply()) { say(P_RED, "MPSSE settings not sent: the stream is not open (Reconnect)"); return; }
    say(P_GRN, "Applied: TCK %lu Hz%s%s%s%s, latency %d ms", (unsigned long)tck_hz(), s_div5 ? ", divide by 5" : "",
        s_adapt ? ", adaptive" : "", s_3ph ? ", three-phase" : "", s_loop ? ", loopback" : "", s_lat);
    chip_refresh();
}

static void mode_set(int m)
{
    if (m == s_mode) return;
    if (m == 1 && EasyRequest(win, &(struct EasyStruct){ sizeof(struct EasyStruct), 0, (UBYTE *)"ftdimon",
            (UBYTE *)"Channel A becomes a serial port.\nJTAG, SPI and the pins stop working until MPSSE is set again.",
            (UBYTE *)"Serial|Cancel" }, NULL) != 1) {
        if (gad[G_MODE]) GT_SetGadgetAttrs(gad[G_MODE], win, NULL, GTCY_Active, (ULONG)s_mode, TAG_DONE);
        return;
    }
    if (fw_cmd(m ? "ftdi mode serial" : "ftdi mode mpsse", fwo, sizeof fwo) < 0 || !strstr(fwo, "ftdi: ok")) {
        say(P_RED, "Mode not changed: %s", fwo); return;
    }
    s_mode = m;
    if (m) { link_close(); say(P_AMB, "Channel A is a serial port (AMIGA.INI FTDI=SERIAL)"); }
    else reconnect();
    chip_refresh();
}

/* ---- the EEPROM through `ftdi ee' ------------------------------------------------------------------------------ */

static void ee_read(void)
{
    const char *p;
    int a = 0;
    if (fw_cmd("ftdi ee", fwo, sizeof fwo) < 0 || !(p = strstr(fwo, "ftdi: ok"))) { say(P_RED, "EEPROM not read: %s", fwo); return; }
    p += 8;
    while (*p && a < 256) {
        char *e;
        unsigned long w;
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        w = strtoul(p, &e, 16);
        if (e == p) break;
        ebuf[2 * a] = (UBYTE)w; ebuf[2 * a + 1] = (UBYTE)(w >> 8);
        a++; p = e;
    }
    if (a != 256) { say(P_RED, "EEPROM: %d of 256 words read", a); return; }
    memcpy(ebuf_rd, ebuf, sizeof ebuf); memset(edirty, 0, sizeof edirty);
    ebuf_ok = 1;
    {
        int blank = 1, i;
        for (i = 0; i < 512; i++) if (ebuf[i] != 0xFF) blank = 0;
        say(P_GRN, "EEPROM: 256 words read%s", blank ? ", all FFFF (blank or not fitted)" : "");
    }
}

/* ---- jobs: long operations a step at a time ------------------------------------------------------------------- */

enum { J_NONE, J_READ, J_VERIFY, J_ERASE, J_PROG, J_EEWR };
static int    job, job_all, job_fail, er_busy;          /* er_busy: an erase sent, its WIP bit polled */
static unsigned long long er_t0;
static ULONG  job_a, job_end, job_a0, job_n0, job_errs, job_crc;
static const char *job_name;

static ULONG parse_hex(const char *s, int *ok)
{
    char *e;
    unsigned long v;
    while (*s == ' ') s++;
    if (s[0] == '$') s++;
    else if (s[0] == '0' && (s[1] | 0x20) == 'x') s += 2;
    v = strtoul(s, &e, 16);
    *ok = e != s && (*e == 0 || *e == ' ');
    return (ULONG)v;
}

static int op_range(ULONG *a, ULONG *n)
{
    int ok1, ok2 = 1;
    ULONG size = part_size ? part_size : fbuf_len;
    if (gad[G_START]) strncpy(op_start, (const char *)((struct StringInfo *)gad[G_START]->SpecialInfo)->Buffer, sizeof op_start - 1);
    if (gad[G_LEN]) strncpy(op_len, (const char *)((struct StringInfo *)gad[G_LEN]->SpecialInfo)->Buffer, sizeof op_len - 1);
    *a = parse_hex(op_start, &ok1);
    *n = op_len[0] ? parse_hex(op_len, &ok2) : (size > *a ? size - *a : 0);
    if (!ok1 || !ok2) { say(P_RED, "Start and Length are hex numbers"); return -1; }
    if (!size) { say(P_RED, "No part detected: Detect first"); return -1; }
    if (*a >= size || !*n || *a + *n > size) { say(P_RED, "The range passes the end of the part (%lu bytes)", (unsigned long)size); return -1; }
    return 0;
}

static void job_start(int j, ULONG a, ULONG n, const char *name)
{
    job = j; job_a = job_a0 = a; job_end = a + n; job_n0 = n; job_errs = 0; job_fail = 0; job_name = name; er_busy = 0;
    job_crc = 0xFFFFFFFFu;
    pr_start(n, j == J_ERASE ? P_AMB : P_GRN);
    say(P_AMB, "%s 0x%06lX-0x%06lX...", name, (unsigned long)a, (unsigned long)(a + n - 1u));
}

static void job_done(const char *fmt, ...)
{
    va_list ap;
    char t[160];
    va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap);
    say(job_fail ? P_RED : P_GRN, "%s", t);
    pr_stop(!job_fail);
    job = J_NONE;
}

static int spi_ready(void)
{
    if (!lk_open) { say(P_RED, "The stream is not open: Reconnect on the Device page"); return 0; }
    if (job) { say(P_AMB, "%s is running", job_name); return 0; }
    return 1;
}

static void detect(void)
{
    int i;
    if (!spi_ready()) return;
    flash_power_up();
    if (flash_read_id(id9f, 20)) { say(P_RED, "No answer to 0x9F (error %d)", lk.err); return; }
    ncand = fc_lookup(id9f[0], (unsigned)id9f[1] << 8 | id9f[2], cand, 16);
    cand_i = 0;
    part = ncand ? cand[0] : NULL;
    part_size = part ? part->size_kb * 1024u : 0;
    if (!part && id9f[2] >= 0x10 && id9f[2] <= 0x1A && id9f[0] != 0xFF && id9f[0] != 0x00) part_size = 1ul << id9f[2];
    flash_addr4(part && part->addr4 && part_size > 16777216u);
    if (part && part->addr4 && part_size > 16777216u) flash_enter_4byte();
    for (i = 0; i < 4; i++) sp_old[i][0] = 0x7F;
    if (id9f[0] == 0xFF || id9f[0] == 0x00) say(P_RED, "JEDEC ID %02X %02X %02X: no part answers (MISO not connected, or held)", id9f[0], id9f[1], id9f[2]);
    else if (part) say(P_GRN, "%s %s, %lu KB%s", part->vendor, part->name, (unsigned long)part->size_kb, ncand > 1 ? ": several parts share this ID, chosen in Match" : "");
    else say(P_AMB, "JEDEC ID %02X %02X %02X is not in the table; %s", id9f[0], id9f[1], id9f[2],
             part_size ? "size from the capacity byte" : "size unknown");
    if (part_size && buf_alloc(part_size)) say(P_RED, "No memory for a %lu-byte buffer", (unsigned long)part_size);
    layout();
}

static int erase_op(ULONG *blk)
{
    if (part && er_i < part->nerase) { *blk = part->er[er_i].r[0].size; return part->er[er_i].op; }
    *blk = 65536u; return 0xD8;
}

/* one step of the job, from the display update: reads of 512 bytes (37 ms on the stream; 4,096-byte reads are 6 %
 * faster but hold the display for 280 ms), pages programmed for up to 12 ms, an erase sent and then its WIP bit
 * read once an update */
#define JOB_BLK 512u
static void job_step(void)
{
    unsigned char t[JOB_BLK];
    ULONG n, i;
    switch (job) {
    case J_READ:
        n = job_end - job_a; if (n > JOB_BLK) n = JOB_BLK;
        if (flash_read(job_a, fbuf + job_a, (int)n)) { job_fail = 1; job_done("Read failed at 0x%06lX (error %d)", (unsigned long)job_a, lk.err); return; }
        for (i = 0; i < n; i++) fdirty[(job_a + i) >> 3] &= (UBYTE)~(1u << ((job_a + i) & 7));
        job_crc = crc32_run(job_crc, fbuf + job_a, n);
        job_a += n;
        break;
    case J_VERIFY:
        n = job_end - job_a; if (n > JOB_BLK) n = JOB_BLK;
        if (flash_read(job_a, t, (int)n)) { job_fail = 1; job_done("Verify read failed at 0x%06lX", (unsigned long)job_a); return; }
        for (i = 0; i < n; i++) if (t[i] != fbuf[job_a + i]) job_errs++;
        job_crc = crc32_run(job_crc, t, n);
        job_a += n;
        break;
    case J_ERASE: {
        ULONG blk; int op = erase_op(&blk);
        ULONG a = job_a - job_a % blk;
        int chip = op == 0xC7 || op == 0x60;
        UBYTE sr = 0;
        if (!er_busy) {
            if (flash_write_enable() || (chip ? flash_bulk_erase() : flash_erase_block(op, a))) {
                job_fail = 1; job_done(chip ? "Chip erase failed" : "Erase failed at 0x%06lX", (unsigned long)a); return;
            }
            er_busy = 1; er_t0 = eclock();
            return;
        }
        if (flash_status(&sr)) { job_fail = 1; job_done("Erase: no status at 0x%06lX", (unsigned long)a); return; }
        if (sr & 0x01) {
            if (eclock() - er_t0 > (unsigned long long)efreq * (chip ? 400u : 10u)) {
                job_fail = 1; job_done("Erase at 0x%06lX: still busy after %u s", (unsigned long)a, chip ? 400u : 10u);
            }
            return;
        }
        er_busy = 0;
        job_a = chip ? job_end : a + blk;
        if (job_a > job_end) job_a = job_end;
        break;
    }
    case J_PROG: {
        ULONG pg = part ? part->page : 256u, k = 0;
        unsigned long long t0 = eclock(), lim = (unsigned long long)efreq * 12u / 1000u;
        if (part && part->write != W_PAGE) pg = 1;
        while (job_a < job_end && (!k || eclock() - t0 < lim)) {   /* pages for up to 12 ms */
            ULONG a = job_a, m = pg - a % pg;
            int blank = 1;
            if (m > job_end - a) m = job_end - a;
            for (i = 0; i < m; i++) if (fbuf[a + i] != 0xFF) { blank = 0; break; }
            if (!blank) {
                if (flash_write_enable() || flash_prog(a, fbuf + a, (int)m) || flash_wait(100u)) {
                    job_fail = 1; job_done("Program failed at 0x%06lX", (unsigned long)a); return;
                }
            }
            job_a += m; k++;
        }
        break;
    }
    case J_EEWR: {
        char line[48];
        while (job_a < job_end && !memcmp(&ebuf[job_a * 2], &ebuf_rd[job_a * 2], 2)) job_a++;
        if (job_a < job_end) {
            snprintf(line, sizeof line, "ftdi ee write %lx %02x%02x", (unsigned long)job_a, ebuf[job_a * 2 + 1], ebuf[job_a * 2]);
            if (fw_cmd(line, fwo, sizeof fwo) < 0 || !strstr(fwo, "ftdi: ok")) { job_fail = 1; job_done("EEPROM write failed at word %lu", (unsigned long)job_a); return; }
            job_a++;
        }
        break;
    }
    default: return;
    }
    pr_set(job_a - job_a0);
    if (job_a >= job_end) {
        ULONG a0 = job_a0, n0 = job_n0;
        switch (job) {
        case J_READ:
            crc_chip = job_crc ^ 0xFFFFFFFFu; crc_chip_n = n0; crc_chip_ok = 1; crc_line(0);
            job_done("Read %lu bytes from 0x%06lX, CRC32 %08lX", (unsigned long)n0, (unsigned long)a0, (unsigned long)crc_chip);
            hv_which = 0;
            break;
        case J_VERIFY: {
            ULONG cb = crc32(fbuf + a0, n0);
            crc_chip = job_crc ^ 0xFFFFFFFFu; crc_chip_n = n0; crc_chip_ok = 1; crc_line(0);
            job_fail = job_errs != 0 || crc_chip != cb;
            if (job_errs) job_done("Verify: %lu bytes differ in 0x%06lX-0x%06lX; CRC32 chip %08lX, buffer %08lX", (unsigned long)job_errs,
                                   (unsigned long)a0, (unsigned long)(a0 + n0 - 1u), (unsigned long)crc_chip, (unsigned long)cb);
            else job_done("Verify: 0x%06lX-0x%06lX equal, CRC32 %08lX (buffer %08lX)", (unsigned long)a0,
                          (unsigned long)(a0 + n0 - 1u), (unsigned long)crc_chip, (unsigned long)cb);
            if (job_all) job_all = 0;
            break;
        }
        case J_ERASE:
            if (job_all) { job_start(J_PROG, a0, n0, "Program"); return; }
            job_done("Erased 0x%06lX-0x%06lX", (unsigned long)a0, (unsigned long)(a0 + n0 - 1u));
            break;
        case J_PROG:
            if (job_all) { job_start(J_VERIFY, a0, n0, "Verify"); return; }
            job_done("Programmed %lu bytes at 0x%06lX", (unsigned long)n0, (unsigned long)a0);
            break;
        case J_EEWR:
            job_done("EEPROM written; read back to check");
            ee_read();
            break;
        }
    }
}

static int confirm(const char *what)
{
    return EasyRequest(win, &(struct EasyStruct){ sizeof(struct EasyStruct), 0, (UBYTE *)"ftdimon", (UBYTE *)"%s",
                                                  (UBYTE *)"Go|Cancel" }, NULL, (ULONG)what) == 1;
}

static void spi_op(int which)
{
    ULONG a, n;
    char q[160];
    if (!spi_ready() || op_range(&a, &n)) return;
    if (!fbuf || fbuf_len < a + n) { say(P_RED, "The buffer is smaller than the range"); return; }
    switch (which) {
    case B_RD: job_start(J_READ, a, n, "Read"); break;
    case B_VFY: job_start(J_VERIFY, a, n, "Verify"); break;
    case B_ER:
        snprintf(q, sizeof q, "Erase 0x%06lX-0x%06lX of %s?", (unsigned long)a, (unsigned long)(a + n - 1u), part ? part->name : "the part");
        if (confirm(q)) job_start(J_ERASE, a, n, "Erase");
        break;
    case B_PRG:
        snprintf(q, sizeof q, "Program 0x%06lX-0x%06lX of %s from the buffer?\n(the range must be erased)", (unsigned long)a,
                 (unsigned long)(a + n - 1u), part ? part->name : "the part");
        if (confirm(q)) job_start(J_PROG, a, n, "Program");
        break;
    case B_ALL:
        snprintf(q, sizeof q, "Erase, program and verify 0x%06lX-0x%06lX of %s?", (unsigned long)a, (unsigned long)(a + n - 1u),
                 part ? part->name : "the part");
        if (confirm(q)) { job_all = 1; job_start(J_ERASE, a, n, "Erase"); }
        break;
    }
}

/* ---- files ------------------------------------------------------------------------------------------------------ */

static int file_ask(int save, char *path, int max, const char *title)
{
    struct FileRequester *fr;
    char dir[256], *f;
    int ok = 0;
    if (!AslBase) { say(P_RED, "asl.library is not available"); return 0; }
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    f = (char *)FilePart((STRPTR)dir);
    {
        char file[108];
        strncpy(file, f, sizeof file - 1); file[sizeof file - 1] = 0;
        *f = 0;
        fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win, ASLFR_TitleText, (ULONG)title,
                                                          ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file,
                                                          ASLFR_DoSaveMode, (ULONG)save, ASLFR_SleepWindow, TRUE, TAG_DONE);
    }
    if (!fr) return 0;
    if (AslRequest(fr, NULL)) {
        strncpy(path, (const char *)fr->fr_Drawer, (size_t)max - 1); path[max - 1] = 0;
        AddPart((STRPTR)path, fr->fr_File, (ULONG)max);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

static void file_open(void)
{
    BPTR fh;
    LONG sz;
    UBYTE *b;
    if (!file_ask(0, fname, sizeof fname, hv_which ? "Load the EEPROM Buffer" : "Load the Flash Buffer")) return;
    if (!(fh = Open((CONST_STRPTR)fname, MODE_OLDFILE))) { say(P_RED, "%s does not open", fname); return; }
    Seek(fh, 0, OFFSET_END); sz = Seek(fh, 0, OFFSET_BEGINNING);
    if (hv_which) {
        LONG n = sz > 512 ? 512 : sz;
        if (Read(fh, ebuf, n) != n) say(P_RED, "%s: read error", fname);
        else { memset(edirty, 0xFF, sizeof edirty); say(P_GRN, "%s: %ld bytes into the EEPROM buffer, CRC32 %08lX", fname, (long)n, (unsigned long)crc32(ebuf, (ULONG)n)); }
        Close(fh);
    } else {
        ULONG need = part_size > (ULONG)sz ? part_size : (ULONG)sz;
        if (buf_alloc(need)) { Close(fh); say(P_RED, "No memory for %lu bytes", (unsigned long)need); return; }
        b = fbuf;
        if (Read(fh, b, sz) != sz) say(P_RED, "%s: read error", fname);
        else {
            char a[16]; fmt_thousands(a, (ULONG)sz);
            memset(fdirty, 0xFF, (fbuf_len + 7u) / 8u);
            crc_file = crc32(fbuf, (ULONG)sz); crc_file_n = (ULONG)sz; crc_file_ok = 1;
            say(P_GRN, "%s: %s bytes, CRC32 %08lX%s", fname, a, (unsigned long)crc_file, part_size && (ULONG)sz > part_size ? " (larger than the part)" : "");
            snprintf(op_len, sizeof op_len, "%lX", (unsigned long)sz);
        }
        Close(fh);
    }
    hv_cur = 0; hv_pos = hv_target = 0;
    layout();
}

static void file_save(void)
{
    BPTR fh;
    ULONG n = hv_len();
    if (!n) { say(P_RED, "The buffer is empty"); return; }
    if (!file_ask(1, fname, sizeof fname, hv_which ? "Save the EEPROM Buffer" : "Save the Flash Buffer")) return;
    if (!(fh = Open((CONST_STRPTR)fname, MODE_NEWFILE))) { say(P_RED, "%s does not open for writing", fname); return; }
    if (Write(fh, hv_buf(), (LONG)n) != (LONG)n) say(P_RED, "%s: write error", fname);
    else { char a[16]; fmt_thousands(a, n); say(P_GRN, "%s: %s bytes saved, CRC32 %08lX", fname, a, (unsigned long)crc32(hv_buf(), n)); }
    Close(fh);
}

/* ---- pins ------------------------------------------------------------------------------------------------------- */

static void pins_send(void)
{
    UBYTE c[6] = { 0x80, pin_val[0], pin_dir[0], 0x82, pin_val[1], pin_dir[1] };
    if (!lk_open) { say(P_RED, "The stream is not open: Reconnect on the Device page"); return; }
    if (mpsse_cmd(&lk, c, 6, NULL, 0)) say(P_RED, "Pins not set (error %d)", lk.err);
}

static void pins_read(void)
{
    UBYTE c[2] = { 0x81, 0x83 }, a[2];
    if (!lk_open || job) return;
    if (mpsse_cmd(&lk, c, 2, a, 2)) { pin_read_ok = 0; say(P_RED, "Pins not read (error %d)", lk.err); pins_lamps(); return; }
    pin_in[0] = a[0]; pin_in[1] = a[1]; pin_read_ok = 1;
    pins_lamps();
}

static void pin_button(int i)
{
    int j = i >= B_PIN_L0 ? (i - B_PIN_L0) / 8 : (i - B_PIN_D0) / 8, k = i >= B_PIN_L0 ? (i - B_PIN_L0) % 8 : (i - B_PIN_D0) % 8;
    btn_t *b = &bt[i];
    if (i >= B_PIN_L0) { pin_val[j] ^= (UBYTE)(1u << k); b->label = (pin_val[j] >> k) & 1 ? "Hi" : "Lo"; }
    else { pin_dir[j] ^= (UBYTE)(1u << k); b->label = (pin_dir[j] >> k) & 1 ? "Out" : "In"; }
    b->drawn = -1; btn_set(b, i >= B_PIN_L0 ? (pin_val[j] >> k) & 1 : (pin_dir[j] >> k) & 1, 0); btn_draw(b);
    pins_send();
    pins_read();
    say(P_GRN, "%s%d: %s, latched %s", j ? "AC" : "AD", k, (pin_dir[j] >> k) & 1 ? "output" : "input", (pin_val[j] >> k) & 1 ? "high" : "low");
}

static void pins_sel(void)
{
    int j, k;
    for (j = 0; j < 2; j++) for (k = 0; k < 8; k++) {
        btn_set(&bt[B_PIN_D0 + j * 8 + k], (pin_dir[j] >> k) & 1, 0);
        btn_set(&bt[B_PIN_L0 + j * 8 + k], (pin_val[j] >> k) & 1, 0);
    }
}

/* ---- JTAG ------------------------------------------------------------------------------------------------------- */

static void jl_add(int pen, const char *fmt, ...)
{
    va_list ap;
    if (jl_n >= JL_MAX) return;
    va_start(ap, fmt); vsnprintf(jl_txt[jl_n], sizeof jl_txt[0], fmt, ap); va_end(ap);
    jl_pen[jl_n++] = pen;
}

static void jtag_setup(void)
{
    UBYTE c[3] = { 0x80, 0x08, 0x0B };                     /* TMS high, TCK, TDI low; TCK, TDI, TMS outputs */
    mpsse_cmd(&lk, c, 3, NULL, 0);
}

static void jtag_scan(void)
{
    jtag_t j;
    chain_dev_t d[CHAIN_MAX];
    int n = 0, ir = 0, i, r;
    if (!spi_ready()) return;
    jl_n = 0;
    jtag_setup();
    if (jtag_init(&j, &lk, 6)) { jl_add(P_RED, "TAP reset failed (error %d)", lk.err); jl_draw(); return; }
    r = chain_scan(&j, d, &n, &ir);
    if (r) jl_add(P_RED, "Chain scan failed: %d (error %d)", r, lk.err);
    else if (!n) { jl_add(P_AMB, "No device on the chain (IR %d bits)", ir); jl_add(P_GREY, "TDO open or tied to TDI, or the target off"); }
    else {
        jl_add(P_GRN, "%d device%s, IR %d bits in all, TCK %lu Hz", n, n == 1 ? "" : "s", ir, tck_hz());
        for (i = 0; i < n; i++) {
            jl_add(P_AMB, "%d  IDCODE %08lX", i, (unsigned long)d[i].idcode);
            jl_add(P_GRN, "   %s %s, IR %d", d[i].vendor, d[i].part, d[i].irlen);
        }
    }
    jl_draw();
    say(r ? P_RED : P_GRN, "JTAG: %d device%s on the chain", n, n == 1 ? "" : "s");
}

static void jtag_loop(int internal)
{
    static UBYTE out[1024], in[1024];
    unsigned long errs = 0, total = 0, ones = 0;
    ULONG s0, s1, m0, m1;
    int r, i;
    if (!spi_ready()) return;
    jtag_setup();
    mpsse_loopback(&lk, internal);
    CurrentTime(&s0, &m0);
    for (r = 0; r < 20; r++) {
        for (i = 0; i < 1024; i++) out[i] = (UBYTE)(r & 1 ? (i * 37 + r) : (r & 2 ? 0x55 : 0xAA) ^ i);
        if (mpsse_xfer(&lk, out, in, 1024)) { errs = 0xFFFFFFFFul; break; }
        for (i = 0; i < 1024; i++) {
            UBYTE x = out[i] ^ in[i];
            while (x) { errs += x & 1u; x >>= 1; }
            x = in[i]; while (x) { ones += x & 1u; x >>= 1; }
        }
        total += 1024;
    }
    CurrentTime(&s1, &m1);
    mpsse_loopback(&lk, s_loop);
    {
        ULONG us = (s1 - s0) * 1000000u + m1 - m0;
        jt_bytes = total; jt_errs = errs == 0xFFFFFFFFul ? 0 : errs; jt_bps = us ? (ULONG)((unsigned long long)total * 1000000u / us) : 0;
    }
    jt_res = errs == 0 && total == 20480u;
    jl_n = 0;
    {
        char a[16], e[16];
        fmt_thousands(a, total); fmt_thousands(e, jt_errs);
        jl_add(P_AMB, "Loopback %s", internal ? "inside the chip (0x84)" : "through a wire, AD1 TDI to AD2 TDO");
        jl_add(jt_res ? P_GRN : P_RED, "%s bytes, %s bit errors: %s", a, e, jt_res ? "PASS" : "FAIL");
    }
    if (!jt_res && !internal && total && ones == total * 8u) jl_add(P_AMB, "TDO read 1 throughout: no wire from AD1 to AD2");
    if (!jt_res && !internal && total && !ones) jl_add(P_AMB, "TDO read 0 throughout: TDO held low");
    jl_draw(); jt_values();
    say(jt_res ? P_GRN : P_RED, "Loopback %s: %s", internal ? "inside" : "wire", jt_res ? "PASS" : "FAIL");
}

/* ---- menu, events, main ----------------------------------------------------------------------------------------- */

enum { MI_ABOUT = 1, MI_QUIT, MI_PREFS, MI_PAGE0 };
static struct NewMenu nm[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Device", (STRPTR)"1", 0, 0, (APTR)(MI_PAGE0 + 0) },
    { NM_ITEM, (STRPTR)"Pins", (STRPTR)"2", 0, 0, (APTR)(MI_PAGE0 + 1) },
    { NM_ITEM, (STRPTR)"JTAG", (STRPTR)"3", 0, 0, (APTR)(MI_PAGE0 + 2) },
    { NM_ITEM, (STRPTR)"SPI Flash", (STRPTR)"4", 0, 0, (APTR)(MI_PAGE0 + 3) },
    { NM_ITEM, (STRPTR)"Hex Editor", (STRPTR)"5", 0, 0, (APTR)(MI_PAGE0 + 4) },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Preferences...", (STRPTR)",", 0, 0, (APTR)MI_PREFS },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)MI_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MI_QUIT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static int quit;

static void page_set(int p)
{
    if (p == page || p < 0 || p >= PG_N) return;
    page = p;
    layout();
    if (page == PG_PINS) { pins_sel(); pins_send(); pins_read(); }   /* the pins as the page shows them */
}

/* the 60 Hz update timer (timer.device UNIT_WAITECLOCK), opened in main; the About box's timer uses its unit */
static struct MsgPort     *tport;
static struct timerequest *treq;
static struct Window      *mwin;                         /* the Preferences panel */
#define MBTN_ID 200

/* ---- About box ----------------------------------------------------------------------------------------------
 * A modal window in the middle of the screen (no drag bar: the animation addresses the screen's bitmap and the
 * sprites at a fixed place) with the program's name, what it does, EPROM Foundry, Inc., the copyright and the
 * version, and a dark panel animated on every displayed frame (the vertical blank server signals the task):
 *
 *   logo        "EPROMFOUNDRY, INC." on a turning sphere: each lit pixel of the text in the 8x8 font (font8x8.c)
 *               is a point whose column is a longitude (the text and a three-character gap go once round, 256
 *               steps) and whose row is a height; the points facing the viewer are dots 2, 3 or 4 pixels wide by
 *               their depth.  The CPU plots them into an off-screen plane (merc_init, merc_draw).
 *   cube        a wireframe cube in the middle of the sphere, drawn by the blitter in line mode into a second
 *               off-screen plane.
 *   copy        a QBSBlit chain, started when the beam has passed the panel, copies the logo plane into bitplanes 0,
 *               1 and 3 of the panel and the logo and cube planes together into plane 3: the panel (pen 4) shows
 *               the logo in pen 15 and the cube in pen 12.  The panel is never drawn while it is displayed.
 *   copper      a user copper list sets colour 15 on every line of the panel (the logo shaded top to bottom) and
 *               colour 12 white for the cube, and puts both back below the panel.
 *   starfield   hardware sprites 2 to 7, each reused down the panel by its own DMA list: AB_STARS one-line
 *               instances a sprite, three layers (sprites 2-3 far and dim, 4-5, 6-7 near and bright) moving left
 *               at 1, 2 and 3 pixels a frame.  The CPU only rewrites each instance's position words.
 *
 * PAL shows 50 frames a second: the animation advances on every one of them. */
#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif
#define AB_W     448
#define AB_PX    64                                        /* the panel's drawable area in the window */
/* the About window's height, the panel's y and the y of the two lines above it: 218 lines, and 198 on a screen of
 * fewer than 226 lines */
static int ab_h = 218, ab_py = 48, ab_t0 = 19, ab_t1 = 31;
#define AB_H     ab_h
#define AB_PY    ab_py
#define AB_PW    320
#define AB_PH    96
#define AB_SPR   6
#define AB_STARS 24
#define AB_LIST  (4 * AB_STARS + 2)                        /* words of a sprite's DMA list */
#define AB_OSB   (AB_PW / 8)                               /* bytes a line of an off-screen plane */

static struct Window   *awin;
static btn_t            abtn;
static struct Requester ab_req;
static int              ab_blocked;
static LONG             ab_sigbit = -1;
static BYTE             ab_pri_old;
static int              ab_x, ab_y;                        /* the panel's drawable area, screen coordinates */
static struct SimpleSprite ab_ss[AB_SPR];
static int              ab_have[AB_SPR];
static UWORD           *ab_sd;                             /* chip RAM: the sprites' DMA lists */
static WORD             ab_sx[AB_SPR][AB_STARS], ab_hlo, ab_hspan;
static int              ab_h0, ab_dh, ab_v0;               /* sprite position words for screen x 0, per 100 x, y 0 */
/* the animation repeats every AB_FRAMES frames (the logo turns 4/1,024 of a turn a frame, the cube 4/1,024 about x
 * and 8/1,024 about y), so each frame is drawn once into its slot of a chip-RAM cache, during the first turn, and
 * from then on only played: three blitter copies from the slot into the panel, after the beam has passed it.  A slot
 * holds the logo's front band, its back band and the cube's window.  When the cache does not fit in chip RAM one
 * slot is drawn every frame. */
#define AB_LY0    30                                       /* the logo's band in the panel: lines 30 to 65 */
#define AB_LH     36
#define AB_CW     144                                      /* the cube's window: 144 x 64, placed in the panel per frame */
#define AB_CB     (AB_CW / 8)
#define AB_CH     64
#define AB_FRAMES 256
#define AB_FSZ    (2 * AB_OSB * AB_LH + AB_CB * AB_CH)     /* 4,032 bytes */
static UBYTE           *ab_cache;                          /* chip RAM: AB_FRAMES slots, or one */
static ULONG            ab_cache_n;
static UBYTE            ab_drawn_f[AB_FRAMES];             /* slot k holds frame k */
static WORD             ab_cwx[AB_FRAMES], ab_cwy[AB_FRAMES];   /* frame k's cube window in the panel */
static UBYTE            ab_cbehind[AB_FRAMES];             /* frame k's cube is behind the sphere's centre */
static UBYTE *volatile  ab_show;                           /* the slot the next copy plays */
static ULONG            ab_show_k;
static WORD             ab_pwx = -1, ab_pwy;               /* the cube window last played */
static int              ab_behind = -1;
static UWORD            ab_top;                            /* the beam line of the panel's first line */
static UBYTE           *ab_plane[3];                       /* screen bitplanes 0 (cube), 1 (logo back), 3 (front) */
static ULONG            ab_stride;
static struct UCopList *ab_ucl;

/* ---- the blitter, owned by the task: a line in line mode ----------------------------------------------------- */
static void ab_line(volatile struct Custom *c, UBYTE *pl, ULONG stride, int x1, int y1, int x2, int y2)
{
    int dx, dy, adx, dmax, dmin, t;
    UWORD con1;
    if (y1 > y2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }   /* always down */
    dx = x2 - x1; dy = y2 - y1; adx = dx < 0 ? -dx : dx;
    if (adx >= dy) { dmax = adx; dmin = dy; con1 = 0x10u | (dx < 0 ? 0x04u : 0u); }   /* SUD; AUL left */
    else           { dmax = dy; dmin = adx; con1 = dx < 0 ? 0x08u : 0u; }             /* SUL left */
    if (4 * dmin - 2 * dmax < 0) con1 |= 0x40u;                                       /* SIGN */
    c->bltcon0 = (UWORD)(((x1 & 15) << 12) | 0x0BCA);    /* A, C, D; D = A B + !A C: a dot over the plane */
    c->bltcon1 = (UWORD)(((x1 & 15) << 12) | con1 | 1u); /* LINE */
    c->bltafwm = 0xFFFF; c->bltalwm = 0xFFFF;
    c->bltadat = 0x8000; c->bltbdat = 0xFFFF;
    c->bltbmod = (WORD)(4 * dmin);
    c->bltamod = (WORD)(4 * (dmin - dmax));
    c->bltapt = (APTR)(LONG)(4 * dmin - 2 * dmax);
    c->bltcpt = c->bltdpt = (APTR)(pl + (ULONG)y1 * stride + ((x1 >> 3) & ~1));
    c->bltcmod = c->bltdmod = (WORD)stride;
    c->bltsize = (UWORD)(((dmax + 1) << 6) | 2);
}

/* A to D: h lines of w words from a (packed) to d (modulo dmod) */
static void ab_copy1(volatile struct Custom *c, const UBYTE *a, UBYTE *d, int dmod, int h, int w)
{
    WaitBlit();
    c->bltcon0 = 0x09F0; c->bltcon1 = 0; c->bltafwm = 0xFFFF; c->bltalwm = 0xFFFF;
    c->bltapt = (APTR)a; c->bltamod = 0; c->bltdpt = (APTR)d; c->bltdmod = (WORD)dmod;
    c->bltsize = (UWORD)((h << 6) | w);
}

/* D = 0: h lines of w words, packed */
static void ab_clear1(volatile struct Custom *c, UBYTE *d, int h, int w)
{
    WaitBlit();
    c->bltcon0 = 0x0100; c->bltcon1 = 0; c->bltdpt = (APTR)d; c->bltdmod = 0;
    c->bltsize = (UWORD)((h << 6) | w);
}

/* a slot into the panel: the front band into bitplane 3, the back band into bitplane 1, the cube's window into
 * bitplane 0.  The panel is pen 4; the copper list colours the combinations: bitplane 3 (front) over everything,
 * then bitplane 0 (cube), then bitplane 1 (back). */
static void ab_play(const UBYTE *slot, ULONG k)
{
    volatile struct Custom *c = HWREG;
    ULONG band = (ULONG)(ab_y + AB_LY0) * ab_stride + (ab_x >> 3);
    OwnBlitter();
    ab_copy1(c, slot, ab_plane[2] + band, (int)(ab_stride - AB_OSB), AB_LH, AB_PW / 16);
    ab_copy1(c, slot + AB_OSB * AB_LH, ab_plane[1] + band, (int)(ab_stride - AB_OSB), AB_LH, AB_PW / 16);
    if (ab_pwx >= 0) {                                     /* the last frame's cube window cleared */
        WaitBlit();
        c->bltcon0 = 0x0100; c->bltcon1 = 0;
        c->bltdpt = (APTR)(ab_plane[0] + (ULONG)(ab_y + ab_pwy) * ab_stride + ((ab_x + ab_pwx) >> 3));
        c->bltdmod = (WORD)(ab_stride - AB_CB);
        c->bltsize = (UWORD)((AB_CH << 6) | (AB_CB / 2));
    }
    ab_copy1(c, slot + 2 * AB_OSB * AB_LH,
             ab_plane[0] + (ULONG)(ab_y + ab_cwy[k]) * ab_stride + ((ab_x + ab_cwx[k]) >> 3), (int)(ab_stride - AB_CB),
             AB_CH, AB_CB / 2);
    WaitBlit();
    DisownBlitter();
    ab_pwx = ab_cwx[k]; ab_pwy = ab_cwy[k];
}

/* ---- the turning logo (merc_init, merc_draw): each lit pixel of the text in the 8x8 font is a point whose column is
 * a longitude (the text and a three-character gap go once round) and whose row is a height; the points facing the
 * viewer (cosine > 0) are dots at x = r sin, 2, 3 or 4 pixels wide by their depth, 1 or 2 lines high (this screen's
 * pixels are 1.875 times as tall as they are wide).  Angles in 1,024 steps a turn, sines in Q14 (m_sin, a quarter
 * computed and mirrored), x rounded: a frame turns the text 2 steps, 1.8 pixels at the front.  A dot row is one
 * 68020 BFSET into the off-screen plane. ---- */
#define MERC_MAXP 768
static WORD           m_sin[1024 + 256];                   /* sin, and cos at +256 */
static UWORD          p_th[MERC_MAXP];                     /* longitude, 0..1023 */
static UWORD          p_line[MERC_MAXP];                   /* the byte offset of the point's line */
static int n_p, m_cx, m_r;

static void m_sin_init(void)
{
    static int done;
    int i;
    if (done) return;
    done = 1;
    for (i = 0; i <= 256; i++) m_sin[i] = (WORD)(16384.0 * sin(i * 6.283185307179586 / 1024.0) + 0.5);
    for (i = 257; i < 512; i++) m_sin[i] = m_sin[512 - i];
    for (i = 512; i < 1024; i++) m_sin[i] = (WORD)-m_sin[i - 512];
    for (i = 1024; i < 1024 + 256; i++) m_sin[i] = m_sin[i - 1024];
}

static void merc_init(const char *text, int cx, int cy, int r, int ystep)
{
    int len = 0, i, row, bit;
    unsigned int step;
    while (text[len]) len++;
    step = (unsigned int)((1024u * 256u) / (unsigned int)((len + 3) * 8));   /* longitude a column, 8.8 */
    n_p = 0; m_cx = cx; m_r = r;
    for (i = 0; i < len; i++) {
        unsigned int ch = (unsigned char)text[i];
        if (ch < 0x20 || ch > 0x7E) continue;
        for (row = 0; row < 8; row++) {
            unsigned int bits = font8x8[ch - 0x20][row];
            for (bit = 0; bit < 8; bit++) {
                if (!(bits & (0x80u >> bit)) || n_p >= MERC_MAXP) continue;
                p_th[n_p] = (UWORD)((((unsigned int)(i * 8 + bit) * step) >> 8) & 1023u);
                p_line[n_p] = (UWORD)((cy + (row - 4) * ystep - AB_LY0) * AB_OSB);   /* in the band */
                n_p++;
            }
        }
    }
}

static inline void bfset_row(UBYTE *p, int x, int w)
{
    __asm__ volatile ("bfset (%0){%1:%2}" : : "a"(p), "d"(x), "d"(w) : "memory", "cc");
}

/* the front into fb, the back (cosine <= 0, the text mirrored as it passes behind) into bk, one size smaller */
static void merc_draw(UBYTE *fb, UBYTE *bk, unsigned int rot)
{
    const UWORD *th = p_th, *ln = p_line;
    int i, r = m_r, cx = m_cx;
    for (i = n_p; i > 0; i--, th++, ln++) {
        unsigned int t = (*th + rot) & 1023u;
        int c = m_sin[t + 256], x;
        UBYTE *p;
        x = cx + ((r * m_sin[t] + 8192) >> 14);
        if (c <= 0) {                                      /* the back: one size smaller than the front's */
            if (c > -6450) { if (x >= 1 && x < AB_PW - 2) bfset_row(bk + *ln, x - 1, 2); }
            else if (x >= 1 && x < AB_PW - 3) { bfset_row(bk + *ln, x - 1, 3); if (c < -12900) bfset_row(bk + *ln + AB_OSB, x - 1, 3); }
            continue;
        }
        p = fb + *ln;
        if (c < 6450) { if (x >= 1 && x < AB_PW - 2) bfset_row(p, x - 1, 2); }                 /* 50/127 */
        else if (c < 12900) { if (x >= 1 && x < AB_PW - 3) { bfset_row(p, x - 1, 3); bfset_row(p + AB_OSB, x - 1, 3); } }
        else if (x >= 2 && x < AB_PW - 3) { bfset_row(p, x - 2, 4); bfset_row(p + AB_OSB, x - 2, 4); }
    }
}

/* ---- one frame: the slot drawn once (blitter and CPU), played by the blitter after the beam, the stars ---- */
/* the copy of the frame's slot, run when the beam has passed the panel (a timer.device request set at the vertical
 * blank for the line after it); it ends long before the beam is back at the panel */
static struct MsgPort     *ab_tport;
static struct timerequest *ab_treq;
static int                 ab_tbusy;
static UWORD               ab_after;                       /* the beam line after the panel */

static void ab_timer(LONG lines);

static void ab_chain(void)
{
    UWORD ln = (UWORD)((VPOSL >> 8) & 0x1FFu);
    UBYTE *slot = ab_show;
    if (!slot) return;
    if (ln + 4u >= ab_top && ln < ab_after) { ab_timer((LONG)ab_after - (LONG)ln); return; }   /* late: the panel is on */
    ab_play(slot, ab_show_k);
    ab_show = NULL;
    if (ab_cbehind[ab_show_k] != ab_behind) {              /* pen 7 (cube and back text): the nearer of the two */
        ab_behind = ab_cbehind[ab_show_k];
        if (ab_behind) SetRGB4(&scr->ViewPort, 7, 9, 7, 4); else SetRGB4(&scr->ViewPort, 7, 15, 15, 15);
    }
}

/* the copy's timer, `lines` scan lines from now */
static void ab_timer(LONG lines)
{
    unsigned long long t = eclock();
    if (lines < 1) lines = 1;
    t += ((ULONG)lines * 64u * (efreq >> 6)) / b_hz;      /* lines in E clock cycles */
    ab_treq->tr_node.io_Command = TR_ADDREQUEST;
    ab_treq->tr_time.tv_secs = (ULONG)(t >> 32); ab_treq->tr_time.tv_micro = (ULONG)t;
    SendIO((struct IORequest *)ab_treq); ab_tbusy = 1;
}

/* frame k drawn into a slot: the slot cleared and the cube's edges drawn by the blitter, the logo's dots by the CPU */
static void ab_render(UBYTE *slot, ULONG k)
{
    static const BYTE v[8][3] = { {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} };
    static const UBYTE e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    volatile struct Custom *c = HWREG;
    UBYTE *cube = slot + 2 * AB_OSB * AB_LH;
    WORD px[8], py[8];
    int sa = m_sin[(k * 4u) & 1023u], ca = m_sin[((k * 4u) & 1023u) + 256];
    int sb = m_sin[(k * 8u) & 1023u], cb = m_sin[((k * 8u) & 1023u) + 256];
    int i, wx, wy, x0 = 9999, x1 = -9999, y0 = 9999, y1 = -9999;
    /* the orbit: round the sphere's axis on a circle of 450 (x) by 700 (z), one turn in AB_FRAMES, bobbing 50 at
     * twice that; the eye 1,700 from the axis; scale 230: the cube spans the panel from side to side and never
     * leaves it (|x| at most (450 + 111) x 230 x 1.875 / 1,589 = 152, |y| (50 + 111) x 230 / 889 = 42) */
    LONG ox = (450L * sa) / 16384, oz = (700L * ca) / 16384;
    LONG oy = (50L * m_sin[(k * 8u) & 1023u]) / 16384;
    for (i = 0; i < 8; i++) {                              /* rotate about x, then y; a unit of 64 */
        LONG x = v[i][0] * 64, y = v[i][1] * 64, z = v[i][2] * 64, t;
        t = (y * ca - z * sa) >> 14; z = (y * sa + z * ca) >> 14; y = t;
        t = (x * cb + z * sb) >> 14; z = (z * cb - x * sb) >> 14; x = t;
        x += ox; y += oy; z += oz + 1700;
        px[i] = (WORD)(AB_PW / 2 + (x * 230 * 15 / 8) / z);   /* a pixel is 1.875 times as tall as it is wide */
        py[i] = (WORD)(AB_PH / 2 + (y * 230) / z);
        if (px[i] < x0) x0 = px[i];
        if (px[i] > x1) x1 = px[i];
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    wx = x0 & ~15;                                         /* the window on a word, inside the panel */
    if (wx > AB_PW - AB_CW) wx = AB_PW - AB_CW;
    if (wx < 0) wx = 0;
    wy = y0;
    if (wy > AB_PH - AB_CH) wy = AB_PH - AB_CH;
    if (wy < 0) wy = 0;
    ab_cwx[k] = (WORD)wx; ab_cwy[k] = (WORD)wy; ab_cbehind[k] = (UBYTE)(oz > 0);
    for (i = 0; i < 8; i++) { px[i] = (WORD)(px[i] - wx); py[i] = (WORD)(py[i] - wy); }
    (void)x1; (void)y1;
    OwnBlitter();
    ab_clear1(c, slot, 2 * AB_LH, AB_PW / 16);
    ab_clear1(c, cube, AB_CH, AB_CB / 2);
    for (i = 0; i < 12; i++) {
        WaitBlit();
        ab_line(c, cube, AB_CB, px[e[i][0]], py[e[i][0]], px[e[i][1]], py[e[i][1]]);
    }
    WaitBlit();
    DisownBlitter();
    merc_draw(slot, slot + AB_OSB * AB_LH, (unsigned int)(-(LONG)(k * 4u)));
}

/* ---- the starfield: sideways and flight, AB_MODE_FR frames (30 s) each ------------------------------------------
 *   sideways  three layers moving left at 1, 2 and 3 pixels a frame; each instance keeps its line, the CPU writes
 *             its horizontal position
 *   flight    AB_NST stars in 3D flown into (z falls 6 a frame), the field turning about the line of flight once in
 *             2,048 frames (41 s); each frame the stars are projected, sorted by line (a list a line) and dealt into
 *             the sprites of their depth (far: 2-3, middle: 4-5, near: 6-7), a sprite's instances top to bottom with a
 *             free line between (sprite DMA reads its next control words there); a star with no free sprite on its
 *             line waits a frame.  A star leaving the panel starts again far away. */
static void ab_parallax_init(void);
#define AB_MODE_FR 1500
#define AB_NST     120
static WORD  st_x[AB_NST], st_y[AB_NST], st_z[AB_NST], st_sx[AB_NST];
static BYTE  st_next[AB_NST];
static int   ab_smode;
static ULONG ab_rnd = 0x2545F491u;

static ULONG ab_rand(void) { ab_rnd = ab_rnd * 1103515245u + 12345u; return ab_rnd >> 8; }

static void ab_star_new(int i, int z)
{
    st_x[i] = (WORD)((LONG)(ab_rand() & 2047u) - 1024); st_y[i] = (WORD)((LONG)(ab_rand() & 2047u) - 1024);
    st_z[i] = (WORD)z;
}

static void ab_flight(ULONG f)
{
    static BYTE head[AB_PH];
    int i, s, ln, cnt[AB_SPR], last[AB_SPR];
    unsigned int phi = (f / 2u) & 1023u;
    LONG sn = m_sin[phi], cs = m_sin[phi + 256];
    memset(head, -1, sizeof head);
    for (i = 0; i < AB_NST; i++) {
        LONG rx, ry, sx, sy, z;
        st_z[i] = (WORD)(st_z[i] - 6);
        if (st_z[i] < 48) ab_star_new(i, 1024);
        z = st_z[i];
        rx = ((LONG)st_x[i] * cs - (LONG)st_y[i] * sn) >> 14;
        ry = ((LONG)st_x[i] * sn + (LONG)st_y[i] * cs) >> 14;
        sx = AB_PW / 2 + (rx * 75) / z;                    /* 40 x 1.875: a pixel is 1.875 times as tall as wide */
        sy = AB_PH / 2 + (ry * 40) / z;
        if (sx < 0 || sx >= AB_PW - 2 || sy < 0 || sy >= AB_PH - 1) { ab_star_new(i, 1024); continue; }
        st_sx[i] = (WORD)sx;
        st_next[i] = head[sy]; head[sy] = (BYTE)i;
    }
    for (s = 0; s < AB_SPR; s++) { cnt[s] = 0; last[s] = -2; }
    for (ln = 0; ln < AB_PH; ln++) {
        for (i = head[ln]; i >= 0; i = st_next[i]) {
            int g = st_z[i] < 300 ? 2 : st_z[i] < 600 ? 1 : 0, t;
            for (t = 0; t < 2; t++) {
                int sp = 2 * g + t;
                if (ab_have[sp] && cnt[sp] < AB_STARS && last[sp] + 2 <= ln) {
                    UWORD *d = ab_sd + sp * AB_LIST + 4 * cnt[sp];
                    int vs = ab_v0 + ab_y + ln, hs = (ab_hlo >> 4) + (st_sx[i] * ab_dh) / 100;
                    d[0] = (UWORD)(((vs & 0xFF) << 8) | ((hs >> 1) & 0xFF));
                    d[1] = (UWORD)((((vs + 1) & 0xFF) << 8) | (((vs >> 8) & 1) << 2) | ((((vs + 1) >> 8) & 1) << 1) | (hs & 1));
                    d[2] = 0x8000; d[3] = 0;
                    cnt[sp]++; last[sp] = ln;
                    break;
                }
            }
        }
    }
    for (s = 0; s < AB_SPR; s++) if (ab_have[s]) { UWORD *d = ab_sd + s * AB_LIST + 4 * cnt[s]; d[0] = 0; d[1] = 0; }
}

static void ab_parallax(void)
{
    int s, i;
    /* x in sixteenths of a sprite (lores) pixel, a layer's speed 8, 16 or 24 of them a frame; only the changing half
     * of each position pair is written */
    for (s = 0; s < AB_SPR; s++) {
        UWORD *d = ab_sd + s * AB_LIST;
        int sp = 8 * (1 + s / 2);
        WORD *x = ab_sx[s];
        if (!ab_have[s]) continue;
        for (i = 0; i < AB_STARS; i++, d += 4) {
            int hs;
            x[i] = (WORD)(x[i] - sp);
            if (x[i] < ab_hlo) x[i] = (WORD)(x[i] + ab_hspan);
            hs = x[i] >> 4;
            d[0] = (UWORD)((d[0] & 0xFF00) | ((hs >> 1) & 0xFF));
            d[1] = (UWORD)((d[1] & 0xFFFE) | (hs & 1));
        }
    }
}

static void ab_stars(ULONG f)
{
    int m = (int)((f / AB_MODE_FR) & 1u), i;
    if (!ab_sd) return;
    if (m != ab_smode) {
        ab_smode = m;
        if (m) for (i = 0; i < AB_NST; i++) ab_star_new(i, 48 + (int)(ab_rand() % 977u));
        else ab_parallax_init();
    }
    if (ab_smode) ab_flight(f); else ab_parallax();
}

/* at the vertical blank: the timer for the line after the panel; the stars; the frame's slot, drawn if it is not
 * yet */
static void ab_frame(void)
{
    ULONG f = ab_vbl_n, k = f % AB_FRAMES;
    UBYTE *slot;
    if (ab_tbusy || !ab_cache) return;                     /* the last frame's copy has not run */
    ab_timer((LONG)ab_after - (LONG)((VPOSL >> 8) & 0x1FFu));
    ab_stars(f);                                           /* before the beam reaches the panel */
    slot = ab_cache + (ab_cache_n > 1 ? k : 0) * AB_FSZ;
    if (ab_cache_n == 1 || !ab_drawn_f[k]) { ab_render(slot, k); ab_drawn_f[k] = 1; }
    ab_show = slot; ab_show_k = k;
}

/* ---- opening and closing --------------------------------------------------------------------------------- */
static void ab_sprites(void)
{
    struct ViewPort *vp = &scr->ViewPort;
    int s, h100, cal = 0;
    if (!(ab_sd = (UWORD *)AllocMem(AB_SPR * AB_LIST * 2, MEMF_CHIP | MEMF_CLEAR))) return;
    for (s = 0; s < AB_SPR; s++) {
        UWORD *d = ab_sd + s * AB_LIST;
        ab_ss[s].height = 1; ab_ss[s].x = 0; ab_ss[s].y = 0;
        if (GetSprite(&ab_ss[s], 2 + s) != 2 + s) continue;
        ab_have[s] = 1;
        ChangeSprite(vp, &ab_ss[s], d);
        if (!cal) {                                        /* the position words graphics makes for x 0 and 100 */
            cal = 1;
            ab_h0 = ((d[0] & 0xFF) << 1) | (d[1] & 1); ab_v0 = (d[0] >> 8) | ((d[1] & 4) << 6);
            MoveSprite(vp, &ab_ss[s], 100, 0);
            h100 = ((d[0] & 0xFF) << 1) | (d[1] & 1); ab_dh = h100 - ab_h0;
            /* hstart counts lores pixels: x in lores units moves it 100, in this hires screen's pixels 50 */
            if (ab_dh >= 100) ab_dh /= 2;
        }
    }
    /* the panel in sixteenths of a lores pixel */
    ab_hlo = (WORD)((ab_h0 + (ab_x * ab_dh) / 100) * 16);
    ab_hspan = (WORD)((((AB_PW - 2) * ab_dh) / 100) * 16);
    ab_parallax_init();
    SetRGB4(vp, 21, 5, 5, 6); SetRGB4(vp, 25, 9, 9, 10); SetRGB4(vp, 29, 15, 15, 15);
    SetRGB4(vp, 23, 5, 5, 6); SetRGB4(vp, 27, 9, 9, 10); SetRGB4(vp, 31, 15, 15, 15);
}

/* the sideways movement: every instance on a line of its own sprite's list, every fourth line; x at random */
static void ab_parallax_init(void)
{
    int s, i;
    for (s = 0; s < AB_SPR; s++) {
        UWORD *d = ab_sd + s * AB_LIST;
        int off = (s * 3) & 3;
        if (!ab_have[s]) continue;
        for (i = 0; i < AB_STARS; i++, d += 4) {
            int vs = ab_v0 + ab_y + i * 4 + off, hs;
            ab_sx[s][i] = (WORD)(ab_hlo + (WORD)((ULONG)rand() % (ULONG)ab_hspan));
            hs = ab_sx[s][i] >> 4;
            d[0] = (UWORD)(((vs & 0xFF) << 8) | ((hs >> 1) & 0xFF));
            d[1] = (UWORD)((((vs + 1) & 0xFF) << 8) | (((vs >> 8) & 1) << 2) | ((((vs + 1) >> 8) & 1) << 1) | (hs & 1));
            d[2] = 0x8000; d[3] = 0;                       /* one pixel, the sprite's colour 1 */
        }
        d[0] = 0; d[1] = 0;                                /* the list's end */
    }
}

static void ab_copper(void)
{
    struct ViewPort *vp = &scr->ViewPort;
    int i, k;
    if (!(ab_ucl = (struct UCopList *)AllocMem(sizeof *ab_ucl, MEMF_PUBLIC | MEMF_CLEAR))) return;
    UCopperListInit(ab_ucl, AB_PH * 7 + 26);
    /* the panel is pen 4 (bitplane 2); bitplane 0 the cube, 1 the logo's back, 3 its front: pen 5 white (the cube),
     * 6 the back's dim shade, 12 to 15 the front's shade, line by line; all put back below.  Pen 7 (the cube over
     * the back) is set per frame in the colour map: the cube's white in front of the sphere's centre, the back's
     * shade behind it */
    CWait(ab_ucl, ab_y - 1, 0); CBump(ab_ucl);
    CMove(ab_ucl, (APTR)&HWREG->color[5], 0xFFF); CBump(ab_ucl);
    for (i = 0; i < AB_PH; i++) {                          /* gold above the equator, chrome blue below */
        int r, g, b, t = (i * 32) / AB_PH, k, col, dim;    /* 0..31 */
        if (t < 16) { r = 15; g = 8 + (t * 7) / 16; b = 2 + (t * 13) / 16; }
        else { t -= 16; r = 15 - (t * 10) / 16; g = 15 - (t * 6) / 16; b = 15; }
        col = (r << 8) | (g << 4) | b;
        dim = ((r * 5 / 8) << 8) | ((g * 5 / 8) << 4) | (b * 5 / 8);
        CWait(ab_ucl, ab_y + i, 0); CBump(ab_ucl);
        for (k = 12; k < 16; k++) { CMove(ab_ucl, (APTR)&HWREG->color[k], col); CBump(ab_ucl); }
        CMove(ab_ucl, (APTR)&HWREG->color[6], dim); CBump(ab_ucl);
    }
    CWait(ab_ucl, ab_y + AB_PH, 0); CBump(ab_ucl);
    {
        static const UWORD back[7][2] = { { 5, 0x3E5 }, { 6, 0xFC2 }, { 7, 0xF43 }, { 12, 0xB00 }, { 13, 0x126 },
                                          { 14, 0x777 }, { 15, 0xFFE } };
        for (k = 0; k < 7; k++) { CMove(ab_ucl, (APTR)&HWREG->color[back[k][0]], back[k][1]); CBump(ab_ucl); }
    }
    CWait(ab_ucl, 10000, 255); CBump(ab_ucl);
    Forbid(); vp->UCopIns = ab_ucl; Permit();
    RethinkDisplay();
}

static void ab_close(void)
{
    int s;
    if (!awin) return;
    Forbid(); ab_task = NULL; Permit();
    if (ab_tbusy) { AbortIO((struct IORequest *)ab_treq); WaitIO((struct IORequest *)ab_treq); ab_tbusy = 0; }
    ab_show = NULL;
    if (ab_sigbit >= 0) SetTaskPri(FindTask(NULL), ab_pri_old);
    for (s = 0; s < AB_SPR; s++) if (ab_have[s]) { FreeSprite(2 + s); ab_have[s] = 0; }
    if (ab_ucl) { FreeVPortCopLists(&scr->ViewPort); RemakeDisplay(); ab_ucl = NULL; }
    SetRGB4(&scr->ViewPort, 7, 15, 4, 3);                  /* P_RED back */
    if (ab_sd) { FreeMem(ab_sd, AB_SPR * AB_LIST * 2); ab_sd = NULL; }
    if (ab_cache) { FreeMem(ab_cache, ab_cache_n * AB_FSZ); ab_cache = NULL; }
    if (ab_sigbit >= 0) { FreeSignal(ab_sigbit); ab_sigbit = -1; }
    if (ab_treq) { DeleteIORequest((struct IORequest *)ab_treq); ab_treq = NULL; }
    if (ab_tport) { DeleteMsgPort(ab_tport); ab_tport = NULL; }
    CloseWindow(awin); awin = NULL;
    if (ab_blocked) { EndRequest(&ab_req, win); ab_blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static void ab_text(int y, int pen, int b, const char *s)
{
    struct RastPort *r = awin->RPort;
    if (b) bold(r, 1);
    text_c(r, AB_W / 2, y, pen, s);
    if (b) bold(r, 0);
}

static void ab_open(void)
{
    struct BitMap *bm = scr->RastPort.BitMap;
    char v[64];
    int wx, wy;
    if (awin || mwin) return;
    if (scr->Height < 226) { ab_h = 198; ab_py = 42; ab_t0 = 15; ab_t1 = 26; }
    wx = ((scr->Width - AB_W) / 2) & ~15; wy = (scr->Height - AB_H) / 2;
    ab_vbl_n = 0;
    m_sin_init();
    btn_init(&abtn, MBTN_ID + 10, AB_W / 2 - 48, AB_H - (ab_h < 218 ? 21 : 24), 96, 14, "OK", ICON_NONE);
    awin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)wx, WA_Top, (ULONG)wy,
                          WA_Width, AB_W, WA_Height, AB_H, WA_Title, (ULONG)"About FTDI Monitor",
                          WA_DragBar, FALSE, WA_DepthGadget, FALSE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_RMBTrap, TRUE,
                          WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!awin) return;
    SetFont(awin->RPort, font);
    ab_x = wx + AB_PX; ab_y = wy + AB_PY;
    ab_text(ab_t0, P_TXT_BLUE, 1, "FTDI Monitor");
    ab_text(ab_t1, P_BLACK, 0, "The FT2232H: pins, JTAG, SPI flash and EEPROM");
    panel(awin->RPort, AB_PX - 2, AB_PY - 1, AB_PW + 4, AB_PH + 2);
    ab_text(AB_PY + AB_PH + (ab_h < 218 ? 7 : 10), P_BLACK, 1, "EPROM Foundry, Inc.");
    ab_text(AB_PY + AB_PH + (ab_h < 218 ? 17 : 22), P_BLACK, 0, "(c) 2026 Mike Wolak");
    snprintf(v, sizeof v, "pano-g2-amiga  version %s", GIT_HASH);
    ab_text(AB_PY + AB_PH + (ab_h < 218 ? 27 : 34), P_TXT_BLUE, 0, v);
    btn_add(&abtn, 1, awin);
    /* the planes the cube is drawn in, and the row stride (an interleaved bitmap's covers every plane) */
    ab_plane[0] = bm->Planes[0]; ab_plane[1] = bm->Planes[1]; ab_plane[2] = bm->Planes[3];   /* cube, back, front */
    ab_stride = bm->BytesPerRow;
    memset(ab_drawn_f, 0, sizeof ab_drawn_f);
    ab_cache_n = AB_FRAMES;
    if (!(ab_cache = (UBYTE *)AllocMem(AB_FRAMES * AB_FSZ, MEMF_CHIP))) {
        ab_cache_n = 1; ab_cache = (UBYTE *)AllocMem(AB_FSZ, MEMF_CHIP);
    }
    merc_init("EPROMFOUNDRY, INC.", AB_PW / 2, AB_PH / 2, 150, 4);
    ab_sprites();
    ab_copper();
    InitRequester(&ab_req);
    if (Request(&ab_req, win)) ab_blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    ab_show = NULL; ab_tbusy = 0; ab_pwx = -1; ab_behind = -1; ab_smode = 0;
    ab_top = (UWORD)(GfxBase->ActiView->DyOffset + scr->ViewPort.DyOffset + ab_y);
    ab_after = (UWORD)(ab_top + AB_PH + 1);
    if ((ab_tport = CreateMsgPort()) != NULL && (ab_treq = (struct timerequest *)CreateIORequest(ab_tport, sizeof *ab_treq)) != NULL) {
        ab_treq->tr_node.io_Device = treq->tr_node.io_Device;   /* the same unit (UNIT_WAITECLOCK) */
        ab_treq->tr_node.io_Unit = treq->tr_node.io_Unit;
    }
    if (ab_treq && (ab_sigbit = AllocSignal(-1)) >= 0) {
        ab_sigmask = 1ul << ab_sigbit;
        ab_pri_old = (BYTE)SetTaskPri(FindTask(NULL), 20);   /* the vertical blank's signal is taken at once */
        Forbid(); ab_task = FindTask(NULL); Permit();
    }
}

/* one event of the About box; 0 when it is to close */
static int ab_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_REFRESHWINDOW: BeginRefresh(awin); EndRefresh(awin, TRUE); break;
    case IDCMP_VANILLAKEY: if (code == 0x1B || code == '\r' || (code | 0x20) == 'o') return 0; break;
    case IDCMP_GADGETDOWN: btn_draw(&abtn); break;
    case IDCMP_GADGETUP: btn_draw(&abtn); if (g == &abtn.g) return 0; break;
    }
    return 1;
}

/* ---- preferences ------------------------------------------------------------------------------------------------
 * The SPI bus, the MPSSE clock, the latency timer and the first page, kept as the global variables ftdimon/<name>
 * (serialmon's GetVar/SetVar): Use sets ENV:, Save sets ENV: and ENVARC:.  The panel is a modal window (serialmon's
 * m_open, midimon's m_rebuild): two groups, labels right-aligned before the gadgets, a hint line under each, Save,
 * Use and Cancel. */

static spi_cfg_t s_spi = { 0, 0, 0, 4, 0, 0 };
static int s_page0, s_latp = 1;                            /* the first page; the latency preference, ms */

static const struct { const char *name; int *v; int lo, hi; } pf[] = {
    { "SCKIdle", &s_spi.cpol, 0, 1 }, { "SampleEdge", &s_spi.cpha, 0, 1 }, { "LSBFirst", &s_spi.lsb, 0, 1 },
    { "CSPin", &s_spi.cs_pin, 3, 7 }, { "CSHigh", &s_spi.cs_high, 0, 1 }, { "CReset", &s_spi.creset, 0, 2 },
    { "Clock", &s_clk, 0, 7 }, { "Divide5", &s_div5, 0, 1 }, { "Adaptive", &s_adapt, 0, 1 },
    { "ThreePhase", &s_3ph, 0, 1 }, { "Latency", &s_latp, 1, 255 }, { "StartPage", &s_page0, 0, PG_N - 1 } };
#define PF_VARS (int)(sizeof pf / sizeof pf[0])

static void prefs_load(void)
{
    char n[40], b[8];
    int i;
    for (i = 0; i < PF_VARS; i++) {
        snprintf(n, sizeof n, "ftdimon/%s", pf[i].name);
        if (GetVar((CONST_STRPTR)n, (STRPTR)b, sizeof b, GVF_GLOBAL_ONLY) > 0) {
            int v = atoi(b);
            if (v >= pf[i].lo && v <= pf[i].hi) *pf[i].v = v;
        }
    }
    spi_config(&s_spi);
}

/* 0, or -1 when a variable was not set */
static int prefs_store(int save)
{
    char n[40], b[8];
    int i, bad = 0;
    for (i = 0; i < PF_VARS; i++) {
        snprintf(n, sizeof n, "ftdimon/%s", pf[i].name);
        snprintf(b, sizeof b, "%d", *pf[i].v);
        if (!SetVar((CONST_STRPTR)n, (STRPTR)b, (LONG)strlen(b), GVF_GLOBAL_ONLY | (save ? GVF_SAVE_VAR : 0))) bad = 1;
    }
    return bad ? -1 : 0;
}

enum { PF_IDLE, PF_EDGE, PF_ORDER, PF_CSPIN, PF_CSACT, PF_CRST, PF_CLK, PF_LAT, PF_DIV5, PF_ADAPT, PF_3PH, PF_PAGE, PF_N };
enum { PB_SAVE, PB_USE, PB_CANCEL, PB_N };
static const char *const lbl_lohi[] = { "Low", "High", NULL };
static const char *const lbl_edge[] = { "Leading", "Trailing", NULL };
static const char *const lbl_order[] = { "MSB first", "LSB first", NULL };
static const char *const lbl_cspin[] = { "AD3", "AD4", "AD5", "AD6", "AD7", NULL };
static const char *const lbl_crst[] = { "Held low", "Held high", "Not driven", NULL };
static const char *const lbl_page[] = { "Device", "Pins", "JTAG", "SPI Flash", "Hex Editor", NULL };
static const char *const p_lbl[PF_N] = { "SCK idle", "Sample edge", "Bit order", "Chip select", "CS active",
                                         "AD7 (CRESET)", "Clock", "Latency", "Divide by 5", "Adaptive", "Three-phase",
                                         "First page" };
static struct Gadget *mglist, *pgad[PF_N];
static btn_t pbtn[PB_N];
static struct Requester p_req;
static int p_blocked;
static spi_cfg_t p_spi;                                    /* the panel's copy until Save or Use */
static int p_clk, p_div5, p_adapt, p_3ph, p_lat, p_pg;
static int p_w, p_gw, p_gx[2], p_gy, p_gh, p_row[6];

/* group, drawn into the panel's window */
static void p_group(int x, int y, int w, int h, const char *title)
{
    struct RastPort *keep = rp;
    rp = mwin->RPort; group(x, y, w, h, title); rp = keep;
}

/* the gadget columns: x of the gadgets in group k */
#define P_GX(k) (p_gx[k] + ((k) ? 104 : 112))

static struct Gadget *p_mk(int kind, struct Gadget *prev, int id, int k, int row, int w, ULONG t1, ULONG v1, ULONG t2, ULONG v2)
{
    struct NewGadget ng;
    memset(&ng, 0, sizeof ng);
    ng.ng_LeftEdge = P_GX(k); ng.ng_TopEdge = p_row[row]; ng.ng_Width = w;
    ng.ng_Height = kind == CHECKBOX_KIND ? 11 : 12;
    ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = id; ng.ng_VisualInfo = vi;
    return pgad[id] = CreateGadget(kind, prev, &ng, t1, v1, t2, v2, TAG_DONE);
}

static int p_gadgets(void)
{
    struct Gadget *g;
    int w0 = p_gw - 120, w1 = p_gw - 112;
    memset(pgad, 0, sizeof pgad);
    if (!(g = CreateContext(&mglist))) return 0;
    g = p_mk(CYCLE_KIND, g, PF_IDLE, 0, 0, w0, GTCY_Labels, (ULONG)lbl_lohi, GTCY_Active, (ULONG)p_spi.cpol);
    g = p_mk(CYCLE_KIND, g, PF_EDGE, 0, 1, w0, GTCY_Labels, (ULONG)lbl_edge, GTCY_Active, (ULONG)p_spi.cpha);
    g = p_mk(CYCLE_KIND, g, PF_ORDER, 0, 2, w0, GTCY_Labels, (ULONG)lbl_order, GTCY_Active, (ULONG)p_spi.lsb);
    g = p_mk(CYCLE_KIND, g, PF_CSPIN, 0, 3, w0, GTCY_Labels, (ULONG)lbl_cspin, GTCY_Active, (ULONG)(p_spi.cs_pin - 3));
    g = p_mk(CYCLE_KIND, g, PF_CSACT, 0, 4, w0, GTCY_Labels, (ULONG)lbl_lohi, GTCY_Active, (ULONG)p_spi.cs_high);
    if (p_spi.cs_pin != 7)                                 /* ADBUS7 as chip select has no CRESET */
        g = p_mk(CYCLE_KIND, g, PF_CRST, 0, 5, w0, GTCY_Labels, (ULONG)lbl_crst, GTCY_Active, (ULONG)p_spi.creset);
    g = p_mk(CYCLE_KIND, g, PF_CLK, 1, 0, w1, GTCY_Labels, (ULONG)clk_lbl, GTCY_Active, (ULONG)p_clk);
    g = p_mk(INTEGER_KIND, g, PF_LAT, 1, 1, 40, GTIN_Number, (ULONG)p_lat, GTIN_MaxChars, 3);
    g = p_mk(CHECKBOX_KIND, g, PF_DIV5, 1, 2, 26, GTCB_Checked, (ULONG)p_div5, GTCB_Scaled, TRUE);
    g = p_mk(CHECKBOX_KIND, g, PF_ADAPT, 1, 3, 26, GTCB_Checked, (ULONG)p_adapt, GTCB_Scaled, TRUE);
    g = p_mk(CHECKBOX_KIND, g, PF_3PH, 1, 4, 26, GTCB_Checked, (ULONG)p_3ph, GTCB_Scaled, TRUE);
    g = p_mk(CYCLE_KIND, g, PF_PAGE, 1, 5, w1, GTCY_Labels, (ULONG)lbl_page, GTCY_Active, (ULONG)p_pg);
    return g != NULL;
}

/* the hint lines: the SPI mode, and the clock the settings give */
static void p_hints(void)
{
    struct RastPort *r = mwin->RPort;
    char t[48], a[16];
    int y = p_gy + p_gh - 14, k;
    for (k = 0; k < 2; k++) { SetAPen(r, P_BG); RectFill(r, p_gx[k] + 6, y, p_gx[k] + p_gw - 7, y + 8); }
    snprintf(t, sizeof t, "SPI mode %d (CPOL %d, CPHA %d)", p_spi.cpol * 2 + p_spi.cpha, p_spi.cpol, p_spi.cpha);
    text_c(r, p_gx[0] + p_gw / 2, y, P_TXT_BLUE, t);
    fmt_thousands(a, (p_div5 ? 6000000ul : 30000000ul) / (clk_div[p_clk] + 1ul));
    snprintf(t, sizeof t, "TCK and SCK %s Hz", a);
    text_c(r, p_gx[1] + p_gw / 2, y, P_TXT_BLUE, t);
}

/* the gadgets made again (chip select on ADBUS7 removes the CRESET row); the drawn buttons follow them */
static void p_rebuild(void)
{
    struct RastPort *r = mwin->RPort;
    if (mglist) {
        RemoveGList(mwin, mglist, -1);
        FreeGadgets(mglist); mglist = NULL;
    }
    SetAPen(r, P_BG); RectFill(r, P_GX(0), p_row[5], p_gx[0] + p_gw - 7, p_row[5] + 11);
    if (!p_gadgets()) { if (mglist) FreeGadgets(mglist); mglist = NULL; return; }
    if (p_spi.cs_pin == 7) pen_text(r, P_GX(0) + 4, p_row[5] + 2, P_GREY, "AD7 is chip select");
    AddGList(mwin, mglist, ~0, -1, NULL);
    RefreshGList(mglist, mwin, NULL, -1);
    GT_RefreshWindow(mwin, NULL);
    btn_add(pbtn, PB_N, mwin);
    p_hints();
}

static void p_close(void)
{
    if (!mwin) return;
    CloseWindow(mwin); mwin = NULL;
    FreeGadgets(mglist); mglist = NULL;
    memset(pgad, 0, sizeof pgad);
    if (p_blocked) { EndRequest(&p_req, win); p_blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static void p_open(void)
{
    int bt = scr->WBorTop + scr->Font->ta_YSize + 1, bl = scr->WBorLeft, y, h, i, k;
    struct RastPort *r;
    if (mwin || awin) return;
    spi_config_get(&p_spi);
    p_clk = s_clk; p_div5 = s_div5; p_adapt = s_adapt; p_3ph = s_3ph; p_lat = s_lat > 0 ? s_lat : s_latp; p_pg = s_page0;
    p_w = 576;
    p_gw = (p_w - bl - scr->WBorRight - 24) / 2;
    p_gx[0] = bl + 8; p_gx[1] = bl + 16 + p_gw;
    p_gy = bt + 8;
    for (i = 0; i < 6; i++) p_row[i] = p_gy + 8 + i * 14;
    p_gh = 8 + 6 * 14 + 16;
    y = p_gy + p_gh + 8;
    btn_init(&pbtn[PB_SAVE], MBTN_ID + PB_SAVE, p_w / 2 - 48 - 16 - 96, y, 96, 14, "Save", ICON_NONE);
    btn_init(&pbtn[PB_USE], MBTN_ID + PB_USE, p_w / 2 - 48, y, 96, 14, "Use", ICON_NONE);
    btn_init(&pbtn[PB_CANCEL], MBTN_ID + PB_CANCEL, p_w / 2 + 48 + 16, y, 96, 14, "Cancel", ICON_NONE);
    h = y + 14 + 8 + scr->WBorBottom;
    mwin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)((scr->Width - p_w) / 2),
                          WA_Top, (ULONG)((scr->Height - h) / 2), WA_Width, (ULONG)p_w, WA_Height, (ULONG)h,
                          WA_Title, (ULONG)"FTDI Monitor Preferences", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                          WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, CYCLEIDCMP | CHECKBOXIDCMP | INTEGERIDCMP | IDCMP_CLOSEWINDOW | IDCMP_GADGETDOWN |
                                    IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!mwin) return;
    r = mwin->RPort;
    SetFont(r, font);
    p_group(p_gx[0], p_gy, p_gw, p_gh, "SPI Bus");
    p_group(p_gx[1], p_gy, p_gw, p_gh, "MPSSE and Start");
    for (k = 0; k < 2; k++)
        for (i = 0; i < 6; i++) text_r(r, P_GX(k) - 8, p_row[i] + 2, P_BLACK, p_lbl[k * 6 + i]);
    pen_text(r, P_GX(1) + 48, p_row[1] + 2, P_BLACK, "ms");
    p_rebuild();
    InitRequester(&p_req);
    if (Request(&p_req, win)) p_blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
}

/* the panel's settings taken: into the program, the SPI and the chip, and into ENV: (and ENVARC: when save) */
static void p_take(int save)
{
    LONG v = p_lat;
    if (pgad[PF_LAT]) GT_GetGadgetAttrs(pgad[PF_LAT], mwin, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    if (v < 1) v = 1;
    if (v > 255) v = 255;
    s_spi = p_spi; spi_config(&s_spi);
    s_clk = p_clk; s_div5 = p_div5; s_adapt = p_adapt; s_3ph = p_3ph; s_latp = (int)v; s_page0 = p_pg;
    if (lk_open) mpsse_apply();
    if (lat_set(v)) return;
    if (prefs_store(save)) { say(P_RED, "Preferences not stored in ENV:%s", save ? " and ENVARC:" : ""); return; }
    say(P_GRN, "Preferences %s: SPI mode %d, %s, CS AD%d %s active, TCK %lu Hz, latency %ld ms", save ? "saved" : "in use",
        s_spi.cpol * 2 + s_spi.cpha, s_spi.lsb ? "LSB first" : "MSB first", s_spi.cs_pin, s_spi.cs_high ? "high" : "low",
        tck_hz(), (long)v);
}

/* one event of the panel; 0 when it is to close */
static int p_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_CLOSEWINDOW: return 0;
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(mwin); GT_EndRefresh(mwin, TRUE); break;
    case IDCMP_VANILLAKEY:
        if (code == 0x1B) return 0;                        /* Esc */
        if (code == '\r') { p_take(0); return 0; }
        switch (code | 0x20) {
        case 's': p_take(1); return 0;
        case 'u': p_take(0); return 0;
        case 'c': return 0;
        }
        break;
    case IDCMP_GADGETDOWN:
        if (g->GadgetID >= MBTN_ID) btn_draw(&pbtn[g->GadgetID - MBTN_ID]);
        break;
    case IDCMP_GADGETUP:
        if (g->GadgetID >= MBTN_ID) {
            btn_t *b = &pbtn[g->GadgetID - MBTN_ID];
            btn_draw(b);
            if (b->dis) break;
            if (b == &pbtn[PB_SAVE]) p_take(1);
            else if (b == &pbtn[PB_USE]) p_take(0);
            return 0;
        }
        switch (g->GadgetID) {
        case PF_IDLE:  p_spi.cpol = code; break;
        case PF_EDGE:  p_spi.cpha = code; break;
        case PF_ORDER: p_spi.lsb = code; break;
        case PF_CSPIN: p_spi.cs_pin = code + 3; p_rebuild(); break;
        case PF_CSACT: p_spi.cs_high = code; break;
        case PF_CRST:  p_spi.creset = code; break;
        case PF_CLK:   p_clk = code; break;
        case PF_DIV5:  p_div5 = (g->Flags & GFLG_SELECTED) != 0; break;
        case PF_ADAPT: p_adapt = (g->Flags & GFLG_SELECTED) != 0; break;
        case PF_3PH:   p_3ph = (g->Flags & GFLG_SELECTED) != 0; break;
        case PF_PAGE:  p_pg = code; break;
        }
        p_hints();
        break;
    }
    return 1;
}


static void button(int i)
{
    if (i >= B_TAB0 && i < B_TAB0 + PG_N) { page_set(i - B_TAB0); return; }
    if (i >= B_PIN_D0 && i <= B_PIN_L15) { pin_button(i); return; }
    switch (i) {
    case B_REFRESH: chip_refresh(); break;
    case B_APPLY:   settings_apply(); break;
    case B_RECON:   reconnect(); chip_refresh(); break;
    case B_EE_RD:   ee_read(); if (ebuf_ok) { hv_which = 1; hv_cur = 0; hv_pos = hv_target = 0; page_set(PG_HEX); } break;
    case B_EE_WR:
        if (!ebuf_ok) { say(P_RED, "Read the EEPROM first"); break; }
        if (!memcmp(ebuf, ebuf_rd, 512)) { say(P_AMB, "The EEPROM buffer is unchanged"); break; }
        if (confirm("Write the changed words of the EEPROM buffer to the chip?\nA wrong VID, PID or channel mode can make the chip unusable here.")) job_start(J_EEWR, 0, 256, "EEPROM write");
        break;
    case B_EE_ER:
        if (confirm("Erase the configuration EEPROM (every word FFFF)?\nThe chip then runs on its defaults.")) {
            if (fw_cmd("ftdi ee erase", fwo, sizeof fwo) < 0 || !strstr(fwo, "ftdi: ok")) say(P_RED, "EEPROM erase failed: %s", fwo);
            else { say(P_GRN, "EEPROM erased"); ee_read(); }
        }
        break;
    case B_ALLIN:  pin_dir[0] = pin_dir[1] = 0; pins_send(); layout(); pins_sel(); pins_read(); say(P_GRN, "Every pin an input"); break;
    case B_READP:  pins_read(); break;
    case B_AUTO:   pin_auto ^= 1; btn_set(&bt[B_AUTO], pin_auto, 0); break;
    case B_SCAN:   jtag_scan(); break;
    case B_LOOPI:  jtag_loop(1); break;
    case B_LOOPE:  jtag_loop(0); break;
    case B_DETECT: detect(); break;
    case B_UNPROT:
        if (spi_ready() && confirm("Clear the part's status register (block protection off)?")) {
            if (flash_unprotect()) say(P_RED, "Unprotect failed"); else { UBYTE sr = 0; flash_status(&sr); say(P_GRN, "Status register now %02X", sr); }
        }
        break;
    case B_RD: case B_VFY: case B_ER: case B_PRG: case B_ALL: spi_op(i); break;
    case B_OPEN: case B_HOPEN: file_open(); break;
    case B_SAVE: case B_HSAVE: file_save(); break;
    case B_HFLASH: hv_which = 0; hv_cur = 0; hv_pos = hv_target = 0; layout(); break;
    case B_HEE: if (ebuf_ok) { hv_which = 1; hv_cur = 0; hv_pos = hv_target = 0; layout(); } break;
    case B_HFILL:
        if (hv_which == 0 && fbuf && confirm("Fill the whole flash buffer with FF?")) {
            memset(fbuf, 0xFF, fbuf_len); memset(fdirty, 0xFF, (fbuf_len + 7u) / 8u); hv_full();
        }
        break;
    }
}

/* the keys of the pages (the Hex Editor's keys edit) */
static void page_key(int c)
{
    static const char keys[PG_N][10] = { "race", "ipu", "siw", "drvepwos", "" };
    static const int act[PG_N][8] = {
        { B_REFRESH, B_APPLY, B_RECON, B_EE_RD },
        { B_ALLIN, B_READP, B_AUTO },
        { B_SCAN, B_LOOPI, B_LOOPE },
        { B_DETECT, B_RD, B_VFY, B_ER, B_PRG, B_ALL, B_OPEN, B_SAVE },
        { 0 } };
    const char *k = strchr(keys[page], c);
    if (c && k) button(act[page][k - keys[page]]);
}

static void gadget_up(struct Gadget *g, UWORD code)
{
    switch (g->GadgetID) {
    case G_CLK: case G_JCLK: s_clk = code; if (lk_open) { mpsse_apply(); say(P_GRN, "TCK %lu Hz", tck_hz()); } break;
    case G_DIV5: s_div5 = (g->Flags & GFLG_SELECTED) != 0; break;
    case G_ADAPT: s_adapt = (g->Flags & GFLG_SELECTED) != 0; break;
    case G_3PH: s_3ph = (g->Flags & GFLG_SELECTED) != 0; break;
    case G_LOOP: s_loop = (g->Flags & GFLG_SELECTED) != 0; break;
    case G_MODE: mode_set(code); break;
    case G_PART:
        cand_i = code; part = cand[cand_i]; part_size = part->size_kb * 1024u; er_i = 0;
        if (buf_alloc(part_size)) say(P_RED, "No memory for the buffer");
        layout();
        break;
    case G_ERASE: er_i = code; break;
    case G_HSCR: hv_target = (long)code * hv_unit() * 8; hv_clamp_target(); break;
    case G_GOTO: {
        int ok;
        ULONG a = parse_hex((const char *)((struct StringInfo *)g->SpecialInfo)->Buffer, &ok);
        if (ok && a < hv_len()) { ULONG old = hv_cur; hv_cur = a; hv_nib = 0; hv_line(old); hv_line(hv_cur); hv_show_cursor(); }
        else say(P_RED, "Go to: a hex address below %lX", (unsigned long)hv_len());
        break;
    }
    }
}

static void rawkey(UWORD code, UWORD qual)
{
    long page_l = hv_rows > 1 ? hv_rows - 1 : 1;
    int sh = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    if (page != PG_HEX) return;
    switch (code) {
    case 0x4C: hv_move(sh ? -16 * page_l : -16); break;     /* up */
    case 0x4D: hv_move(sh ? 16 * page_l : 16); break;       /* down */
    case 0x4F: if (sh) hv_move(-(long)(hv_cur % 16u)); else if (!hv_ascii && hv_nib) { hv_nib = 0; hv_line(hv_cur); } else hv_move(-1); break;
    case 0x4E: if (sh) hv_move(15 - (long)(hv_cur % 16u)); else hv_move(1); break;
    }
}

int main(int argc, char **argv)
{
    static LONG opt[3];
    struct RDArgs *ra = NULL;
    struct Screen *wb;
    ULONG mode, sw, sh, sigs, hn = 0;
    unsigned long long next, period;
    int i, t_open = 0;
    (void)argv;
    prefs_load();
    page = s_page0;
    if (argc > 0) {
        if (!(ra = ReadArgs((CONST_STRPTR)"DEVICE/K,UNIT/N,PAGE/N", opt, NULL))) {
            printf("usage: ftdimon [DEVICE=g2serial.device] [UNIT=n] [PAGE=0-4]\n"); return 20;
        }
        if (opt[0]) { strncpy(dev_name, (const char *)opt[0], sizeof dev_name - 1); }
        if (opt[1]) dev_unit = (ULONG)*(LONG *)opt[1];
        if (opt[2] && *(LONG *)opt[2] >= 0 && *(LONG *)opt[2] < PG_N) page = (int)*(LONG *)opt[2];
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (!IntuitionBase || !GfxBase || !GadToolsBase) { printf("ftdimon: needs AmigaOS 3.0 or later\n"); goto out; }
    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof *treq) : NULL;
    if (!treq || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_WAITECLOCK, (struct IORequest *)treq, 0)) { printf("ftdimon: timer.device\n"); goto out; }
    t_open = 1; TimerBase = treq->tr_node.io_Device;
    { struct EClockVal v; efreq = ReadEClock(&v); }
    AddIntServer(INTB_VERTB, &vbl_is); have_vbl = 1;
    beam_measure();
    period = efreq / TICK_HZ;
    if (!(wb = LockPubScreen((CONST_STRPTR)"Workbench"))) goto out;
    mode = GetVPModeID(&wb->ViewPort); sw = wb->Width; sh = wb->Height;
    if (sw < 640) { ULONG hm = (mode & MONITOR_ID_MASK) | HIRES_KEY | (mode & LORESLACE_KEY); if (!ModeNotAvailable(hm)) { mode = hm; sw = 640; } }
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, sw, SA_Height, sh, SA_Depth, 4, SA_Font, (ULONG)&topaz8,
                         SA_Pens, (ULONG)pens, SA_Colors, (ULONG)colours, SA_Title, (ULONG)"FTDI Monitor", SA_ShowTitle, TRUE,
                         SA_Type, CUSTOMSCREEN, SA_Interleaved, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, wb);
    if (!scr) { printf("ftdimon: the screen did not open\n"); goto out; }
    font = OpenFont(&topaz8);
    vi = GetVisualInfo(scr, TAG_DONE);
    if (!vi || !font) goto out;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0, WA_Width, (ULONG)scr->Width,
                         WA_Height, (ULONG)scr->Height, WA_Title, (ULONG)"FTDI Monitor", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBRight, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_ReportMouse, TRUE,
                         WA_IDCMP, BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP | SCROLLERIDCMP | CHECKBOXIDCMP | INTEGERIDCMP |
                                   IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK |
                                   IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS, TAG_DONE);
    if (!win) { printf("ftdimon: the window did not open\n"); goto out; }
    rp = win->RPort; SetFont(rp, font);
    {
        int bw = win->BorderLeft + win->BorderRight, bh = win->BorderTop + win->BorderBottom;
        int maxw = scr->Width < WIN_W_MAX + bw ? scr->Width : WIN_W_MAX + bw;
        if (scr->Width < WIN_W_MIN + bw || scr->Height < WIN_H_MIN + bh) { printf("ftdimon: the screen is too small (640 x 200 is the least)\n"); goto out; }
        WindowLimits(win, (WORD)(WIN_W_MIN + bw), (WORD)(WIN_H_MIN + bh), (UWORD)maxw, (UWORD)scr->Height);
        if (win->Width > maxw) ChangeWindowBox(win, 0, 0, (WORD)maxw, win->Height);
    }
    row_bm = AllocBitMap(1024, 8, 4, BMF_CLEAR, scr->RastPort.BitMap);
    if (row_bm) { InitRastPort(&row_rp); row_rp.BitMap = row_bm; SetFont(&row_rp, font); }
    if ((menu = CreateMenus(nm, TAG_DONE)) != NULL && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    say(P_GREY, "Connecting...");
    if (!layout()) goto out;
    reconnect();
    chip_refresh();
    if (s_lat > 0 && s_latp != s_lat && !lat_set(s_latp)) chip_refresh();   /* the latency preference */
    if (page == PG_PINS) { pins_sel(); pins_send(); pins_read(); }

    next = eclock() + period;
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
    SendIO((struct IORequest *)treq);
    while (!quit) {
        struct IntuiMessage *im;
        ULONG msig = awin ? (1ul << awin->UserPort->mp_SigBit) | ab_sigmask | (ab_tport ? 1ul << ab_tport->mp_SigBit : 0) : 0;
        if (mwin) msig |= 1ul << mwin->UserPort->mp_SigBit;
        sigs = Wait((1ul << win->UserPort->mp_SigBit) | (1ul << tport->mp_SigBit) | msig | SIGBREAKF_CTRL_C);
        if (awin && ab_tbusy && ab_tport && (sigs & (1ul << ab_tport->mp_SigBit)) && GetMsg(ab_tport)) { ab_tbusy = 0; ab_chain(); }
        if (awin && (sigs & ab_sigmask)) ab_frame();
        if (sigs & SIGBREAKF_CTRL_C) quit = 1;
        if ((sigs & (1ul << tport->mp_SigBit)) && GetMsg(tport)) {
            unsigned long long now = eclock();
            hn++;
            if (job) job_step();
            if (!awin) {                                   /* behind the About box nothing is drawn */
                hv_tick();
                if (page == PG_HEX) {                      /* an arrow held: a line every 3 updates after 300 ms */
                    int up = (bt[B_HUP].g.Flags & GFLG_SELECTED) != 0, dn = (bt[B_HDN].g.Flags & GFLG_SELECTED) != 0;
                    if (up || dn) { if (++hv_hold > 18 && !(hv_hold % 3)) hv_arrow(up ? -1 : 1); } else hv_hold = 0;
                }
                pr_tick(hn);
                if (page == PG_PINS && pin_auto && !(hn % 6u)) pins_read();
                if (s_msg_new) draw_status();
                for (i = 0; i < B_N; i++) if (bt[i].on) btn_draw(&bt[i]);
            } else btn_draw(&abtn);
            if (mwin) for (i = 0; i < PB_N; i++) btn_draw(&pbtn[i]);
            next += period;
            if (next < now) next = now + period;
            treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
            SendIO((struct IORequest *)treq);
        }
        while (awin && (im = GT_GetIMsg(awin->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (!ab_event(cls, code, g)) { ab_close(); break; }
        }
        while (mwin && (im = GT_GetIMsg(mwin->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (!p_event(cls, code, g)) { p_close(); layout(); if (page == PG_PINS) pins_sel(); break; }
        }
        while ((im = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code, qual = im->Qualifier;
            WORD mx = im->MouseX, my = im->MouseY;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); continue; }
            if (awin || mwin) continue;                    /* the About box and the Preferences are modal */
            switch (cls) {
            case IDCMP_NEWSIZE: if (!layout()) quit = 1; if (page == PG_PINS) pins_sel(); break;
            case IDCMP_CLOSEWINDOW: quit = 1; break;
            case IDCMP_GADGETDOWN:
                if (g->GadgetID >= BTN_ID && g->GadgetID < BTN_ID + B_N) btn_draw(&bt[g->GadgetID - BTN_ID]);
                if (g->GadgetID == G_HSCR) { hv_target = (long)code * hv_unit() * 8; hv_clamp_target(); }
                if (g->GadgetID == BTN_ID + B_HUP) { hv_hold = 0; hv_arrow(-1); }
                if (g->GadgetID == BTN_ID + B_HDN) { hv_hold = 0; hv_arrow(1); }
                break;
            case IDCMP_MOUSEMOVE: if (g && g != (struct Gadget *)win && g->GadgetID == G_HSCR) { hv_target = (long)code * hv_unit() * 8; hv_clamp_target(); } break;
            case IDCMP_GADGETUP:
                if (g->GadgetID >= BTN_ID && g->GadgetID < BTN_ID + B_N) {
                    int b = g->GadgetID - BTN_ID;
                    btn_draw(&bt[b]);
                    if (!bt[b].dis) button(b);
                } else gadget_up(g, code);
                break;
            case IDCMP_MOUSEBUTTONS: if (code == SELECTDOWN && page == PG_HEX) hv_click(mx, my); break;
            case IDCMP_RAWKEY: rawkey(code, qual); break;
            case IDCMP_VANILLAKEY:
                if (page == PG_HEX && code != 0x1B && code != '\t') { hv_key(code); break; }
                if (code == '\t' && page == PG_HEX) { hv_ascii ^= 1; hv_nib = 0; hv_line(hv_cur); break; }
                if (code == 0x1B) quit = 1;
                else if (code >= '1' && code <= '5') page_set(code - '1');
                else if ((code | 0x20) == 'q') quit = 1;
                else if (code == '?') ab_open();
                else if (code == ',') p_open();
                else page_key(code | 0x20);
                break;
            case IDCMP_MENUPICK:
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    ULONG id;
                    if (!it) break;
                    id = (ULONG)GTMENUITEM_USERDATA(it);
                    if (id == MI_QUIT) quit = 1;
                    else if (id == MI_ABOUT) ab_open();
                    else if (id == MI_PREFS) p_open();
                    else if (id >= MI_PAGE0 && id < MI_PAGE0 + PG_N) page_set((int)(id - MI_PAGE0));
                    code = it->NextSelect;
                }
                break;
            }
        }
    }
    AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq);
out:
    ab_close();
    p_close();
    link_close();
    if (win) { ClearMenuStrip(win); CloseWindow(win); }
    if (menu) FreeMenus(menu);
    if (glist) FreeGadgets(glist);
    if (row_bm) FreeBitMap(row_bm);
    if (vi) FreeVisualInfo(vi);
    if (font) CloseFont(font);
    if (scr) CloseScreen(scr);
    buf_alloc(0);
    if (have_vbl) RemIntServer(INTB_VERTB, &vbl_is);
    if (t_open) CloseDevice((struct IORequest *)treq);
    if (treq) DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (ra) FreeArgs(ra);
    return 0;
}
