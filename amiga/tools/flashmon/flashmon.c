/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* flashmon.c -- the G2's configuration flash from the Amiga (panoflash.device): the part, its map and the write
 * guard; read, verify, write and erase of any range, CRC32 by the firmware, backups of any size to a file, the
 * bitstream of a slot extracted as a Xilinx .bit and a raw .bin, and a hex editor with smooth scrolling, in one
 * window on a screen of its own (amiga-ui-style, ftdimon's frame).
 *
 *   flashmon [PAGE=0-1]
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  README.md here; the device: amiga/panoflash/panoflash.h.
 *
 * 1 Paths.  Every flash access is a panoflash.device request: unit 0 (the whole part, read only) for reads and
 *   CRCs, unit 1 (main slot) or unit 2 (free region) for writes and erases.  The device carries them to the
 *   firmware's flash window on HD0 (fw/amiga/pflash.c); the multiboot header, the golden image and the configuration
 *   table lie in no writable unit.
 *
 * 2 Jobs.  A job (read, dump to a file, verify, write, erase, CRC) runs in a process of its own, a device request of at most
 *   64 KB (a sector) at a time, so the window keeps its 60 updates a second; the job's button turns into Stop.
 *
 * 3 Bitstream.  The buffer's configuration packets are walked from the sync word (AA 99 55 66 in the first 64
 *   bytes) to the write of DESYNC and the NO-OPs after it (tools/g2flash bit_walk, UG380 5); the bytes from the
 *   buffer's start to there are the bitstream, saved with a Xilinx .bit header (fields a-e as tools/panoflash
 *   bitfile.c reads them; part from the IDCODE register write) for a JTAG load into another unit, or raw when the
 *   file name ends in .bin.  Files are written only when the volume has room (RAM: from free memory); DOS
 *   requesters are off, so a full volume is an error message and the partial file is deleted.
 *
 * 4 Display.  As ftdimon: 60 updates a second (timer.device UNIT_WAITECLOCK), the hex view scrolled by pixels, the
 *   screen copied from the Workbench's mode, the layout following the window's size. */
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
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
#include "panoflash.h"
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

static struct Interrupt vbl_is = { { NULL, NULL, NT_INTERRUPT, 127, (char *)"flashmon clock" }, NULL, (void (*)())vbl_stub };   /* first in the chain */
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

enum { PG_FL, PG_HEX, PG_N };
static const char *const pg_lbl[PG_N] = { "Flash", "Hex Editor" };
static int page = PG_FL;

/* status line */
static char s_msg[160];
static int  s_msg_pen = P_GRN, s_msg_new = 1;

static void say(int pen, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vsnprintf(s_msg, sizeof s_msg, fmt, ap); va_end(ap);
    s_msg_pen = pen; s_msg_new = 1;
}

/* the buffer: bytes of the flash from fbase on (a read, or a file placed at Start) */
static UBYTE *fbuf, *fdirty;
static ULONG  fbuf_len, fbase;
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

/* the part and its map (PFL_CMD_INFO) */
static struct PanoFlashInfo fi;
static int  fi_ok;
static char fl_old[8][40];
static char op_start[16] = "380000", op_len[16] = "340000";

/* the regions of the Region cycle; the last is the range typed into Start and Length */
enum { R_CHIP, R_MAIN, R_FREE, R_GOLDEN, R_HEADER, R_CONFIG, R_CUSTOM, R_N };
static const char *const rg_lbl[R_N + 1] = { "Whole chip", "Main slot", "Free region", "Golden image", "Header",
                                             "Configuration", "Typed range", NULL };
static int rg_i = R_MAIN;

static int region(int r, ULONG *a, ULONG *n)
{
    if (!fi_ok) return -1;
    switch (r) {
    case R_CHIP:   *a = 0; *n = fi.pfi_ChipSize; break;
    case R_MAIN:   *a = fi.pfi_Main; *n = fi.pfi_MainEnd - fi.pfi_Main; break;
    case R_FREE:   *a = fi.pfi_Free; *n = fi.pfi_FreeEnd - fi.pfi_Free; break;
    case R_GOLDEN: *a = fi.pfi_Golden; *n = fi.pfi_Main - fi.pfi_Golden; break;
    case R_HEADER: *a = 0; *n = fi.pfi_Golden; break;
    case R_CONFIG: *a = fi.pfi_Config; *n = fi.pfi_Free - fi.pfi_Config; break;
    default: return -1;
    }
    return 0;
}

/* the unit that may write [a, a + n), and the offset in it; -1 when the range lies in neither writable region */
static int wr_unit(ULONG a, ULONG n, ULONG *off)
{
    if (!fi_ok || !n) return -1;
    if (a >= fi.pfi_Main && a + n <= fi.pfi_MainEnd) { *off = a - fi.pfi_Main; return PFL_UNIT_MAIN; }
    if (a >= fi.pfi_Free && a + n <= fi.pfi_FreeEnd) { *off = a - fi.pfi_Free; return PFL_UNIT_FREE; }
    return -1;
}

static const char *pfl_err(int e)
{
    switch (e) {
    case 0: return "ok";
    case IOERR_OPENFAIL: return "panoflash.device does not open (DEVS:)";
    case IOERR_BADADDRESS: return "outside the unit, or not on a sector boundary";
    case PFLERR_PROTECTED: return "protected range";
    case PFLERR_GUARD: return "the fabric's write guard refused a command";
    case PFLERR_TIMEOUT: return "the flash stayed busy";
    case PFLERR_VERIFY: return "the bytes read back differ";
    case PFLERR_DATACRC: return "the data window was not received intact";
    case PFLERR_NOPART: return "the flash ID is not a known part";
    case PFLERR_NOGUARD: return "the fabric has no write guard: no write is made";
    case PFLERR_BP: return "block-protect bits set";
    case PFLERR_NOWINDOW: return "no flash window on HD0 (AMIGA.FW without it, or the disk's layout)";
    case PFLERR_NOANSWER: return "the firmware did not finish a request";
    case PFLERR_DISK: return "scsi.device unit 0 failed";
    default: return "device error";
    }
}

/* one request on a unit from the calling task, the device opened and closed around it */
static int dev_do(int unit, UWORD cmd, ULONG off, APTR data, ULONG len, ULONG *actual)
{
    struct MsgPort *mp = CreateMsgPort();
    struct IOStdReq *io = mp ? (struct IOStdReq *)CreateIORequest(mp, sizeof *io) : NULL;
    int e;
    if (!io) { if (mp) DeleteMsgPort(mp); return IOERR_OPENFAIL; }
    if ((e = OpenDevice((CONST_STRPTR)PANOFLASH_NAME, (ULONG)unit, (struct IORequest *)io, 0)) == 0) {
        io->io_Command = cmd; io->io_Offset = off; io->io_Data = data; io->io_Length = len;
        e = DoIO((struct IORequest *)io);
        if (actual) *actual = io->io_Actual;
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io); DeleteMsgPort(mp);
    return e;
}

/* ---- gadgets and layout ---------------------------------------------------------------------------------------- */

enum { G_REGION, G_START, G_LEN, G_HSCR, G_GOTO, G_N };
static struct Gadget *gad[G_N];

enum { B_TAB0, B_TAB1,
       B_REFRESH, B_INFO, B_RD, B_VFY, B_WR, B_ER, B_CRC, B_DUMP, B_OPEN, B_SAVE, B_BIT,
       B_HOPEN, B_HSAVE, B_HFILL, B_HUP, B_HDN,
       B_N };
static btn_t bt[B_N];
#define BTN_ID 100
static const char *const op_lbl[] = { "Read", "Verify", "Write", "Erase", "CRC", "Dump...", "Open...", "Save..." };

/* geometry of the page area */
#define TAB_Y   (oy + 4)
#define PG_Y    (oy + 26)
#define ST_Y    (oy + win_h - 15)
#define PG_H    (ST_Y - 4 - PG_Y)
#define GW      ((win_w - 24) / 2)
#define GA_X    (ox + 8)
#define GB_X    (ox + 16 + GW)
static int ROWP;                                           /* table row pitch: 10, or 11 on taller windows */
static int bar_x, bar_y, bar_w, spi_ty, map_y;

/* the hex view */
static struct BitMap  *row_bm;
static struct RastPort row_rp;
static int  hv_x, hv_y, hv_w, hv_rows, hv_cols, hv_digits;
static long hv_pos, hv_target;                             /* view top, pixels into the content */
static ULONG hv_cur;                                       /* cursor: byte index */
static int  hv_nib, hv_ascii, hv_hold;                     /* hv_hold: updates an arrow button has been held */

static UBYTE *hv_buf(void) { return fbuf; }
static ULONG  hv_len(void) { return fbuf_len; }
static int    hv_dirty(ULONG i) { return fdirty ? (fdirty[i >> 3] >> (i & 7)) & 1 : 0; }
static void   hv_mark(ULONG i) { if (fdirty) fdirty[i >> 3] |= (UBYTE)(1u << (i & 7)); }
static long   hv_lines(void) { return (long)((hv_len() + 15u) / 16u); }
/* lines a step of the scroller: GTSC_Total is 16 bits, so a buffer of more than 32,767 lines is counted in units of
 * 2, 4, ... lines */
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

static void page_gadgets(struct Gadget *g)
{
    int x, y, i, w;
    switch (page) {
    case PG_FL:
        map_y = PG_Y + 8 + 7 * ROWP + 4;
        btn_page(B_REFRESH, GA_X + GW / 2 - 144, PG_Y + PG_H - 18, 72, "Refresh");
        btn_page(B_INFO, GA_X + GW / 2 - 68, PG_Y + PG_H - 18, 104, "Device Info");
        btn_page(B_BIT, GA_X + GW / 2 + 40, PG_Y + PG_H - 18, 104, "Bitstream...");
        y = PG_Y + 8;
        g = mk(CYCLE_KIND, g, G_REGION, GB_X + 64, y, 128, 12, NULL, 0, GTCY_Labels, (ULONG)rg_lbl, GTCY_Active, (ULONG)rg_i);
        y += 15;
        g = mk(STRING_KIND, g, G_START, GB_X + 64, y, 80, 12, NULL, 0, GTST_String, (ULONG)op_start, GTST_MaxChars, 10);
        g = mk(STRING_KIND, g, G_LEN, GB_X + GW - 92, y, 80, 12, NULL, 0, GTST_String, (ULONG)op_len, GTST_MaxChars, 10);
        y += 17;
        w = (GW - 16 - 12) / 4;
        for (i = 0; i < 4; i++) btn_page(B_RD + i, GB_X + 8 + i * (w + 4), y, w, op_lbl[i]);
        y += 15;
        for (i = 0; i < 4; i++) btn_page(B_CRC + i, GB_X + 8 + i * (w + 4), y, w, op_lbl[4 + i]);
        bar_y = y + 20;
        bar_x = GB_X + 14; bar_w = GW - 28;
        spi_ty = bar_y + 26;
        break;
    case PG_HEX: {
        int lines;
        hv_digits = 6;
        hv_cols = hv_digits + 1 + 47 + 1 + 16;
        hv_w = hv_cols * 8 + 12;
        hv_x = ox + win_w / 2 - (hv_w + 18) / 2;
        hv_y = PG_Y + 8;
        hv_rows = (PG_H - 8 - 4 - 6 - 12 - 6) / 8;
        lines = (int)hv_lines();
        {
            struct NewGadget ng;
            int k = hv_unit(), hs = hv_rows * 8 + 4 - 20;
            memset(&ng, 0, sizeof ng);
            ng.ng_LeftEdge = hv_x + hv_w; ng.ng_TopEdge = hv_y; ng.ng_Width = 18; ng.ng_Height = hs;
            ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = G_HSCR; ng.ng_VisualInfo = vi;
            g = gad[G_HSCR] = CreateGadget(SCROLLER_KIND, g, &ng, PGA_Freedom, LORIENT_VERT,
                                           GTSC_Total, (ULONG)((lines + k - 1) / k), GTSC_Visible, (ULONG)(hv_rows / k ? hv_rows / k : 1),
                                           GTSC_Top, (ULONG)(hv_pos / 8 / k), GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
            btn_init(&bt[B_HUP], BTN_ID + B_HUP, hv_x + hv_w, hv_y + hs, 18, 10, "", ICON_UP);
            btn_init(&bt[B_HDN], BTN_ID + B_HDN, hv_x + hv_w, hv_y + hs + 10, 18, 10, "", ICON_DOWN);
        }
        y = hv_y + hv_rows * 8 + 4 + 6;
        x = ox + win_w / 2 - (40 + 72 + 12 + 3 * 68 + 2 * 4) / 2;
        g = mk(STRING_KIND, g, G_GOTO, x + 40, y, 72, 12, NULL, 0, GTST_String, (ULONG)"", GTST_MaxChars, 10);
        x += 40 + 72 + 12;
        btn_page(B_HOPEN, x, y, 68, "Open...");
        btn_page(B_HSAVE, x + 72, y, 68, "Save...");
        btn_page(B_HFILL, x + 144, y, 68, "Fill FF");
        break;
    }
    }
    (void)g;
}

static void tabs_init(void)
{
    int i, w = 112, x = ox + win_w / 2 - (PG_N * w + (PG_N - 1) * 4) / 2;
    for (i = 0; i < PG_N; i++) {
        btn_init(&bt[B_TAB0 + i], BTN_ID + B_TAB0 + i, x + i * (w + 4), TAB_Y, w, 14, pg_lbl[i], ICON_NONE);
        bt[B_TAB0 + i].sel = i == page;
    }
}

static void hv_full(void);

/* ---- drawing of the pages ------------------------------------------------------------------------------------- */

static const char *const fl_lbl[7] = { "Part", "JEDEC ID", "Status", "Write guard", "Main slot", "Free region",
                                       "Erased" };

static void draw_status(void)
{
    int x = ox + 8, w = win_w - 16;
    SetAPen(rp, P_TROUGH); RectFill(rp, x + 2, ST_Y + 1, x + w - 3, ST_Y + 10);
    SetAPen(rp, s_msg_pen); SetDrMd(rp, JAM1);
    Move(rp, x + 6, ST_Y + 2 + rp->TxBaseline);
    Text(rp, (CONST_STRPTR)s_msg, (ULONG)(strlen(s_msg) > (size_t)((w - 12) / 8) ? (w - 12) / 8 : strlen(s_msg)));
    s_msg_new = 0;
}

/* ---- the progress of a job: a bar of pixel resolution that eases toward the bytes done on every display update
 * (an eighth of the distance, at least a sixteenth of a pixel), and under it fixed fields drawn only where characters
 * change: per cent done (tenths), the time since the start (m:ss) and bytes a second (the phase's average since its
 * start, four times a second). */
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
    if (page != PG_FL) return;
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
    if (page != PG_FL || pr_drawn < 0) return;
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
        int k = pr_tn % 5;
        pr_ts[k] = now; pr_bs[k] = pr_done;
        /* the phase's average: the bytes done move in 64 KB requests, one every 0.5 to 1.5 s, which a 1 s window
         * would show as 0 and 65,536 in turn */
        if (now > pr_t0) pr_rate = (ULONG)((unsigned long long)pr_done * efreq / (now - pr_t0));
        pr_tn++;
        pr_text();
    } else if (!(n % 4u) && pr_t0 && !pr_end) pr_text();
}

/* CRC-32 (PKZIP) of the file last loaded into the buffer and of the flash's bytes last read, verified, written or summed */
static ULONG crc_file, crc_file_n, crc_chip, crc_chip_n;
static int   crc_file_ok, crc_chip_ok;
static char  crc_old[2][12];

static void crc_hex(char *t, ULONG v, int ok)
{
    int i;
    for (i = 7; i >= 0; i--) { t[i] = ok ? "0123456789ABCDEF"[v & 15u] : '-'; v >>= 4; }
    t[8] = 0;
}

/* the line "CRC32  file XXXXXXXX  flash XXXXXXXX": the flash's value green when it equals the file's over the same
 * length, red when it differs */
static void crc_line(int all)
{
    char t[12];
    int x = GB_X + GW / 2 - 36 * 4, pen = P_BLACK;
    if (page != PG_FL) return;
    if (all) {
        bold(rp, 1); pen_text(rp, x, spi_ty, P_TXT_BLUE, "CRC32"); bold(rp, 0);
        pen_text(rp, x + 7 * 8, spi_ty, P_BLACK, "file");
        pen_text(rp, x + 22 * 8, spi_ty, P_BLACK, "flash");
        crc_old[0][0] = crc_old[1][0] = 0x7F;
    }
    crc_hex(t, crc_file, crc_file_ok);
    value_r(crc_old[0], x + 20 * 8, spi_ty, 64, crc_file_ok ? P_BLACK : P_GREY, t);
    if (crc_chip_ok && crc_file_ok && crc_chip_n == crc_file_n) pen = crc_chip == crc_file ? P_TXT_GRN : P_TXT_RED;
    crc_hex(t, crc_chip, crc_chip_ok);
    value_r(crc_old[1], x + 36 * 8, spi_ty, 64, crc_chip_ok ? pen : P_GREY, t);
}

static void fl_values(void)
{
    char t[7][40], a[16];
    int i;
    if (page != PG_FL) return;
    for (i = 0; i < 7; i++) t[i][0] = 0;
    if (fi_ok) {
        fmt_thousands(a, fi.pfi_ChipSize >> 10);
        snprintf(t[0], 40, "%s, %s KB", fi.pfi_RDID == 0x202017u ? "M25P64" : fi.pfi_RDID == 0x202018u ? "M25P128" : "unknown", a);
        snprintf(t[1], 40, "%02lX %02lX %02lX", (unsigned long)(fi.pfi_RDID >> 16) & 255u, (unsigned long)(fi.pfi_RDID >> 8) & 255u,
                 (unsigned long)fi.pfi_RDID & 255u);
        snprintf(t[2], 40, "%02lX, %lu KB sectors", (unsigned long)fi.pfi_Status & 255u, (unsigned long)fi.pfi_SectorSize >> 10);
        if (fi.pfi_Guard) snprintf(t[3], 40, "present, %lu refused", (unsigned long)fi.pfi_Refusals);
        else snprintf(t[3], 40, "ABSENT: no writes");
        snprintf(t[4], 40, "%06lX-%06lX", (unsigned long)fi.pfi_Main, (unsigned long)fi.pfi_MainEnd - 1u);
        snprintf(t[5], 40, "%06lX-%06lX", (unsigned long)fi.pfi_Free, (unsigned long)fi.pfi_FreeEnd - 1u);
        fmt_thousands(a, fi.pfi_Erased); snprintf(t[6], 40, "%s sectors", a);
    }
    for (i = 0; i < 7; i++)
        value_r(fl_old[i], GA_X + GW - 12, PG_Y + 8 + i * ROWP, (GW - 24 - 104) & ~7, i == 3 && fi_ok && !fi.pfi_Guard ? P_TXT_RED : P_BLACK, t[i]);
}

static int parse_range(ULONG *a, ULONG *n, int quiet);

/* the map strip: the part's regions to scale, protected dark red, writable dark green, the range of Start and Length
 * lit amber over them */
static void map_draw(void)
{
    int x0 = GA_X + 14, w = GW - 28, y = map_y + 2, k;
    ULONG a, n, size = fi_ok ? fi.pfi_ChipSize : 0;
    if (page != PG_FL) return;
    SetAPen(rp, P_TROUGH); RectFill(rp, x0, y, x0 + w - 1, y + 7);
    if (!size) return;
    for (k = 0; k < 5; k++) {
        static const int pen[5] = { P_RED_D, P_RED_D, P_GRN_D, P_RED_D, P_GRN_D };
        ULONG s = k == 0 ? 0 : k == 1 ? fi.pfi_Golden : k == 2 ? fi.pfi_Main : k == 3 ? fi.pfi_Config : fi.pfi_Free;
        ULONG e = k == 0 ? fi.pfi_Golden : k == 1 ? fi.pfi_Main : k == 2 ? fi.pfi_MainEnd : k == 3 ? fi.pfi_Free : fi.pfi_FreeEnd;
        int px0 = (int)((unsigned long long)s * (ULONG)w / size), px1 = (int)((unsigned long long)e * (ULONG)w / size);
        SetAPen(rp, pen[k]); RectFill(rp, x0 + px0, y, x0 + (px1 > px0 ? px1 - 1 : px0), y + 7);
        SetAPen(rp, P_TROUGH); if (k) RectFill(rp, x0 + px0, y, x0 + px0, y + 7);   /* a gap between regions */
    }
    if (!parse_range(&a, &n, 1)) {
        int px0 = (int)((unsigned long long)a * (ULONG)w / size), px1 = (int)((unsigned long long)(a + n) * (ULONG)w / size);
        SetAPen(rp, P_AMB); RectFill(rp, x0 + px0, y + 2, x0 + (px1 > px0 + 1 ? px1 - 1 : px0 + 1), y + 5);
    }
}

static void map_frame(void)
{
    int x0 = GA_X + 14, w = GW - 28, ly = map_y + 15, lx;
    static const char *const lg[3] = { "protected", "writable", "range" };
    static const int lp[3] = { P_RED_D, P_GRN_D, P_AMB };
    int i, tw = 0;
    DrawBevelBox(rp, x0 - 4, map_y, w + 8, 12, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(rp, P_TROUGH); RectFill(rp, x0 - 2, map_y + 1, x0 + w + 1, map_y + 10);
    for (i = 0; i < 3; i++) tw += 12 + tlen(rp, lg[i]) + (i < 2 ? 16 : 0);
    lx = GA_X + GW / 2 - tw / 2;
    for (i = 0; i < 3; i++) {
        SetAPen(rp, P_TROUGH); RectFill(rp, lx, ly + 1, lx + 7, ly + 6);
        SetAPen(rp, lp[i]); RectFill(rp, lx + 1, ly + 2, lx + 6, ly + 5);
        pen_text(rp, lx + 12, ly, P_TXT_BLUE, lg[i]);
        lx += 12 + tlen(rp, lg[i]) + 16;
    }
    map_draw();
}

static void draw_page(void)
{
    int i;
    SetAPen(rp, P_BG); RectFill(rp, ox, PG_Y - 6, ox + win_w - 1, ST_Y - 2);
    switch (page) {
    case PG_FL:
        group(GA_X, PG_Y, GW, PG_H, "Part and Map");
        group(GB_X, PG_Y, GW, PG_H, "Flash");
        for (i = 0; i < 7; i++) { pen_text(rp, GA_X + 12, PG_Y + 8 + i * ROWP, P_BLACK, fl_lbl[i]); fl_old[i][0] = 0x7F; }
        text_r(rp, GB_X + 58, PG_Y + 10, P_BLACK, "Region");
        text_r(rp, GB_X + 58, PG_Y + 25, P_BLACK, "Start");
        text_r(rp, GB_X + GW - 98, PG_Y + 25, P_BLACK, "Length");
        bar_frame(); bar_draw();
        fl_values();
        map_frame();
        crc_line(1);
        text_c(rp, GB_X + GW / 2, PG_Y + PG_H - 12, P_TXT_BLUE, "Writes only in the green regions");
        break;
    case PG_HEX: {
        char t[64], a[16];
        fmt_thousands(a, hv_len());
        if (hv_len()) snprintf(t, sizeof t, "Buffer, %s Bytes from %06lX", a, (unsigned long)fbase);
        else snprintf(t, sizeof t, "Buffer");
        group(ox + 8, PG_Y, win_w - 16, PG_H, t);
        panel(rp, hv_x, hv_y, hv_w, hv_rows * 8 + 4);
        text_r(rp, ox + win_w / 2 - (40 + 72 + 12 + 3 * 68 + 2 * 4) / 2 + 36, hv_y + hv_rows * 8 + 12, P_BLACK, "Go to");
        if (!hv_len()) {
            static const char *const e[2] = { "The buffer is empty.", "Read a range of the flash, or open a file." };
            int k;
            SetAPen(rp, P_GREY); SetDrMd(rp, JAM1);
            for (k = 0; k < 2; k++) { Move(rp, hv_x + hv_w / 2 - tlen(rp, e[k]) / 2, hv_y + hv_rows * 4 - 6 + k * 12 + rp->TxBaseline);
                                      Text(rp, (CONST_STRPTR)e[k], strlen(e[k])); }
        } else hv_full();
        break;
    }
    }
}

static int job;
static void buttons_state(void);

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
    snprintf(t, sizeof t, "%0*lX", hv_digits, (unsigned long)(fbase + base));
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

/* the 60 Hz update timer (timer.device UNIT_WAITECLOCK), opened in main; the About box's timer uses its unit */
static struct Window      *dwin;                         /* Device Info */
static struct MsgPort     *tport;
static struct timerequest *treq;
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
    if (awin || dwin) return;
    if (scr->Height < 226) { ab_h = 198; ab_py = 42; ab_t0 = 15; ab_t1 = 26; }
    wx = ((scr->Width - AB_W) / 2) & ~15; wy = (scr->Height - AB_H) / 2;
    ab_vbl_n = 0;
    m_sin_init();
    btn_init(&abtn, MBTN_ID + 10, AB_W / 2 - 48, AB_H - (ab_h < 218 ? 21 : 24), 96, 14, "OK", ICON_NONE);
    awin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)wx, WA_Top, (ULONG)wy,
                          WA_Width, AB_W, WA_Height, AB_H, WA_Title, (ULONG)"About Flash Monitor",
                          WA_DragBar, FALSE, WA_DepthGadget, FALSE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_RMBTrap, TRUE,
                          WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!awin) return;
    SetFont(awin->RPort, font);
    ab_x = wx + AB_PX; ab_y = wy + AB_PY;
    ab_text(ab_t0, P_TXT_BLUE, 1, "Flash Monitor");
    ab_text(ab_t1, P_BLACK, 0, "The G2's configuration flash: read, write, bitstreams");
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


/* ---- Device Info: the configuration table ------------------------------------------------------------------
 * The table at fi.pfi_Config (0x6C0000 on the M25P64), read through unit 0: records of a 32-bit tag and a 32-bit value,
 * big-endian, up to the first tag FFFFFFFF.  Tags 0x10000010 and 0x10000011 hold the MAC address (fw/g2.c
 * g2_factory_mac), 0x10000000 the board ID (bits 31:26 the series, fw/bootsh.c `flash id'), 0x10000004 to 0x10000007
 * sixteen characters of text shown as the serial number.  The revision follows from the flash part
 * (M25P64 on revision C, M25P128 on revisions A and B).  A modal window as the About box. */
#define DI_W 432
#define DI_H 140
static btn_t dbtn;
static struct Requester di_req;
static int di_blocked;

static void di_close(void)
{
    if (!dwin) return;
    CloseWindow(dwin); dwin = NULL;
    if (di_blocked) { EndRequest(&di_req, win); di_blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static void di_open(void)
{
    UBYTE t[256];
    char v[6][48];
    static const char *const lb[6] = { "MAC address", "Serial number", "Board ID", "Revision", "FPGA", "Table" };
    ULONG k, nrec = 0, mhi = 0, mlo = 0, bid = 0, have = 0;
    int e, i, wx, wy, y;
    struct RastPort *r;
    if (dwin || awin || job) return;
    if (!fi_ok) { say(P_RED, "No part: Refresh first"); return; }
    if ((e = dev_do(PFL_UNIT_CHIP, CMD_READ, fi.pfi_Config, t, sizeof t, NULL))) {
        say(P_RED, "The configuration table was not read: %s (error %d)", pfl_err(e), e);
        return;
    }
    for (i = 0; i < 6; i++) v[i][0] = 0;
    for (k = 0; k + 8 <= sizeof t; k += 8) {
        ULONG tag = (ULONG)t[k] << 24 | (ULONG)t[k + 1] << 16 | (ULONG)t[k + 2] << 8 | t[k + 3];
        ULONG val = (ULONG)t[k + 4] << 24 | (ULONG)t[k + 5] << 16 | (ULONG)t[k + 6] << 8 | t[k + 7];
        if (tag == 0xFFFFFFFFu) break;
        nrec++;
        if (tag == 0x10000010u) { mhi = val; have |= 1; }
        else if (tag == 0x10000011u) { mlo = val; have |= 2; }
        else if (tag == 0x10000000u) { bid = val; have |= 4; }
        else if (tag >= 0x10000004u && tag <= 0x10000007u) {
            int o = (int)(tag - 0x10000004u) * 4, j;
            for (j = 0; j < 4; j++) { UBYTE c = t[k + 4 + j]; v[1][o + j] = c >= 0x20 && c < 0x7F ? (char)c : '.'; }
            v[1][16] = 0; have |= 8;
        }
    }
    if ((have & 3) == 3 && (mhi & 0xFFFFu) != 0xFFFFu)
        snprintf(v[0], 48, "%02lX:%02lX:%02lX:%02lX:%02lX:%02lX", (mhi >> 8) & 255u, mhi & 255u, mlo >> 24, (mlo >> 16) & 255u,
                 (mlo >> 8) & 255u, mlo & 255u);
    else snprintf(v[0], 48, "not in the table");
    if (!(have & 8)) snprintf(v[1], 48, "not in the table");
    if (have & 4) snprintf(v[2], 48, "%08lX (series %lu)", (unsigned long)bid, (unsigned long)(bid >> 26) & 63u);
    else snprintf(v[2], 48, "not in the table");
    snprintf(v[3], 48, "%s", fi.pfi_RDID == 0x202017u ? "C (M25P64, 8 MB)" : fi.pfi_RDID == 0x202018u ? "A or B (M25P128, 16 MB)" : "unknown part");
    snprintf(v[4], 48, "%s", fi.pfi_RDID == 0x202017u ? "XC6SLX100-FGG484" : fi.pfi_RDID == 0x202018u ? "XC6SLX150-FGG484" : "unknown");
    snprintf(v[5], 48, "0x%06lX, %lu records", (unsigned long)fi.pfi_Config, (unsigned long)nrec);
    wx = ((scr->Width - DI_W) / 2) & ~15; wy = (scr->Height - DI_H) / 2;
    btn_init(&dbtn, MBTN_ID + 20, DI_W / 2 - 48, DI_H - 22, 96, 14, "OK", ICON_NONE);
    dwin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)wx, WA_Top, (ULONG)wy, WA_Width, DI_W, WA_Height, DI_H,
                          WA_Title, (ULONG)"Device Info", WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
                          WA_SmartRefresh, TRUE, WA_RMBTrap, TRUE,
                          WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!dwin) return;
    r = dwin->RPort; SetFont(r, font);
    SetAPen(r, P_BG); RectFill(r, dwin->BorderLeft, dwin->BorderTop, DI_W - dwin->BorderRight - 1, DI_H - dwin->BorderBottom - 1);
    y = dwin->BorderTop + 8;
    { struct RastPort *keep = rp; rp = r; group(12, y + 4, DI_W - 24, 6 * 12 + 14, "Configuration Table"); rp = keep; }
    for (i = 0; i < 6; i++) {
        text_r(r, 132, y + 14 + i * 12, P_BLACK, lb[i]);
        if (i == 0 || i == 1) bold(r, 1);
        pen_text(r, 144, y + 14 + i * 12, !strncmp(v[i], "not", 3) || !strncmp(v[i], "unknown", 7) ? P_TXT_RED : i < 2 ? P_TXT_BLUE : P_BLACK, v[i]);
        if (i == 0 || i == 1) bold(r, 0);
    }
    btn_add(&dbtn, 1, dwin);
    InitRequester(&di_req);
    if (Request(&di_req, win)) di_blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    say(P_GRN, "Configuration table at 0x%06lX: %lu records", (unsigned long)fi.pfi_Config, (unsigned long)nrec);
}

static int di_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_REFRESHWINDOW: BeginRefresh(dwin); EndRefresh(dwin, TRUE); break;
    case IDCMP_VANILLAKEY: if (code == 0x1B || code == '\r' || (code | 0x20) == 'o') return 0; break;
    case IDCMP_GADGETDOWN: btn_draw(&dbtn); break;
    case IDCMP_GADGETUP: btn_draw(&dbtn); if (g == &dbtn.g) return 0; break;
    }
    return 1;
}

/* ---- jobs: a process of their own, a device request at a time ------------------------------------------------- */

enum { J_NONE, J_READ, J_DUMP, J_VERIFY, J_WRITE, J_ERASE, J_CRC };
static struct {
    int            kind, unit;
    ULONG          a, n, uoff, chunk;
    UBYTE         *buf;                                    /* READ, VERIFY, WRITE: the buffer */
    BPTR           fh;                                     /* DUMP: the file */
    volatile ULONG done, total;                            /* bytes of the phase */
    volatile int   phase, state, stop;                     /* state 1: the process ended */
    int            err, fail;
    ULONG          crc, bad, first;
    ULONG          at;                                     /* where a failure happened */
} jc;
static int job_btn, job_phase;
static const char *job_name;

/* CRC32 of unit 0 [a, a + n) continued from crc, by the firmware */
static int w_crc(struct IOStdReq *io, ULONG a, ULONG n, ULONG *crc)
{
    ULONG seed = *crc;
    io->io_Command = PFL_CMD_CRC; io->io_Offset = a; io->io_Length = n; io->io_Data = &seed;
    if (DoIO((struct IORequest *)io)) return io->io_Error;
    *crc = io->io_Actual;
    return 0;
}

static void worker(void)
{
    struct MsgPort *mp = CreateMsgPort();
    struct IOStdReq *u0 = mp ? (struct IOStdReq *)CreateIORequest(mp, sizeof *u0) : NULL;
    struct IOStdReq *uw = mp ? (struct IOStdReq *)CreateIORequest(mp, sizeof *uw) : NULL;
    UBYTE *tmp = (UBYTE *)AllocVec(65536, MEMF_ANY);
    int o0 = 0, ow = 0, e = 0;
    ULONG p, k, i, run = 0xFFFFFFFFu, fc = 0;
    if (!u0 || !uw || !tmp) { e = IOERR_OPENFAIL; goto end; }
    if ((e = OpenDevice((CONST_STRPTR)PANOFLASH_NAME, PFL_UNIT_CHIP, (struct IORequest *)u0, 0))) goto end;
    o0 = 1;
    if (jc.unit > 0) { if ((e = OpenDevice((CONST_STRPTR)PANOFLASH_NAME, (ULONG)jc.unit, (struct IORequest *)uw, 0))) goto end; ow = 1; }
    jc.crc = 0;
    for (p = 0; p < jc.n && !e && !jc.stop; p += k) {
        k = jc.n - p > jc.chunk ? jc.chunk : jc.n - p;
        jc.at = jc.a + p;
        switch (jc.kind) {
        case J_READ: case J_DUMP:
            u0->io_Command = CMD_READ; u0->io_Offset = jc.a + p; u0->io_Length = k;
            u0->io_Data = jc.kind == J_READ ? jc.buf + p : tmp;
            if (DoIO((struct IORequest *)u0)) { e = u0->io_Error; break; }
            run = crc32_run(run, (const UBYTE *)u0->io_Data, k);
            if (jc.kind == J_DUMP && Write(jc.fh, tmp, (LONG)k) != (LONG)k) { e = -100; break; }
            break;
        case J_VERIFY:
            fc = jc.crc;
            if ((e = w_crc(u0, jc.a + p, k, &jc.crc))) break;
            if (crc32_run(fc ^ 0xFFFFFFFFu, jc.buf + p, k) ^ 0xFFFFFFFFu ^ jc.crc) {   /* the piece differs: count */
                u0->io_Command = CMD_READ; u0->io_Offset = jc.a + p; u0->io_Length = k; u0->io_Data = tmp;
                if (DoIO((struct IORequest *)u0)) { e = u0->io_Error; break; }
                for (i = 0; i < k; i++) if (tmp[i] != jc.buf[p + i]) { if (!jc.bad) jc.first = jc.a + p + i; jc.bad++; }
            }
            break;
        case J_WRITE:
            uw->io_Command = CMD_WRITE; uw->io_Offset = jc.uoff + p; uw->io_Length = k; uw->io_Data = jc.buf + p;
            if (DoIO((struct IORequest *)uw)) e = uw->io_Error;
            break;
        case J_ERASE:
            uw->io_Command = PFL_CMD_ERASE; uw->io_Offset = jc.uoff + p; uw->io_Length = k; uw->io_Data = NULL;
            if (DoIO((struct IORequest *)uw)) e = uw->io_Error;
            break;
        case J_CRC:
            e = w_crc(u0, jc.a + p, k, &jc.crc);
            break;
        }
        if (!e) jc.done = p + k;
    }
    if (jc.kind == J_READ || jc.kind == J_DUMP) jc.crc = run ^ 0xFFFFFFFFu;
    if (!e && !jc.stop && jc.kind == J_WRITE) {            /* the second phase: the whole range's CRC32 */
        jc.crc = 0; jc.done = 0; jc.total = jc.n; jc.chunk = 262144u; jc.phase = 2;
        for (p = 0; p < jc.n && !e && !jc.stop; p += k) {
            k = jc.n - p > jc.chunk ? jc.chunk : jc.n - p;
            jc.at = jc.a + p;
            if (!(e = w_crc(u0, jc.a + p, k, &jc.crc))) jc.done = p + k;
        }
    }
end:
    if (ow) CloseDevice((struct IORequest *)uw);
    if (o0) CloseDevice((struct IORequest *)u0);
    if (uw) DeleteIORequest((struct IORequest *)uw);
    if (u0) DeleteIORequest((struct IORequest *)u0);
    if (mp) DeleteMsgPort(mp);
    if (tmp) FreeVec(tmp);
    jc.err = e;
    Forbid();                                              /* held until the process has ended: the code stays loaded */
    jc.state = 1;
}

static void buttons_state(void)
{
    int i;
    for (i = 0; i < PG_N; i++) btn_set(&bt[B_TAB0 + i], i == page, 0);
    if (page == PG_FL)
        for (i = B_RD; i <= B_SAVE; i++) {
            const char *l = i == job_btn && job ? "Stop" : op_lbl[i - B_RD];
            if (bt[i].label != l) { bt[i].label = l; bt[i].icon = i == job_btn && job ? ICON_STOP : ICON_NONE; bt[i].drawn = -1; }
            btn_set(&bt[i], 0, job && i != job_btn);
        }
    if (page == PG_FL) { btn_set(&bt[B_REFRESH], 0, job != 0); btn_set(&bt[B_INFO], 0, job != 0 || !fi_ok); btn_set(&bt[B_BIT], 0, job != 0 || !fbuf_len); }
}

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

static int parse_range(ULONG *a, ULONG *n, int quiet)
{
    int ok1, ok2;
    if (page == PG_FL && gad[G_START]) strncpy(op_start, (const char *)((struct StringInfo *)gad[G_START]->SpecialInfo)->Buffer, sizeof op_start - 1);
    if (page == PG_FL && gad[G_LEN]) strncpy(op_len, (const char *)((struct StringInfo *)gad[G_LEN]->SpecialInfo)->Buffer, sizeof op_len - 1);
    *a = parse_hex(op_start, &ok1);
    *n = parse_hex(op_len, &ok2);
    if (!fi_ok) { if (!quiet) say(P_RED, "No part: %s", "Refresh first"); return -1; }
    if (!ok1 || !ok2) { if (!quiet) say(P_RED, "Start and Length are hex numbers"); return -1; }
    if (*a >= fi.pfi_ChipSize || !*n || *n > fi.pfi_ChipSize - *a) {
        if (!quiet) say(P_RED, "The range passes the end of the part (%lX)", (unsigned long)fi.pfi_ChipSize);
        return -1;
    }
    return 0;
}

static void range_set(ULONG a, ULONG n)
{
    snprintf(op_start, sizeof op_start, "%06lX", (unsigned long)a);
    snprintf(op_len, sizeof op_len, "%lX", (unsigned long)n);
    if (page == PG_FL && gad[G_START]) GT_SetGadgetAttrs(gad[G_START], win, NULL, GTST_String, (ULONG)op_start, TAG_DONE);
    if (page == PG_FL && gad[G_LEN]) GT_SetGadgetAttrs(gad[G_LEN], win, NULL, GTST_String, (ULONG)op_len, TAG_DONE);
    map_draw();
}

static void info_refresh(int quiet)
{
    int e = dev_do(PFL_UNIT_CHIP, PFL_CMD_INFO, 0, &fi, sizeof fi, NULL);
    int i;
    fi_ok = !e && fi.pfi_ChipSize;
    for (i = 0; i < 7; i++) fl_old[i][0] = 0x7F;
    if (e) say(P_RED, "panoflash.device: %s (error %d)", pfl_err(e), e);
    else if (!quiet) say(P_GRN, "%s", fi.pfi_Message);
    fl_values(); map_draw();
}

static int confirm(const char *what)
{
    return EasyRequest(win, &(struct EasyStruct){ sizeof(struct EasyStruct), 0, (UBYTE *)"Flash Monitor", (UBYTE *)"%s",
                                                  (UBYTE *)"Go|Cancel" }, NULL, (ULONG)what) == 1;
}

static int has_sync(const UBYTE *b, ULONG n)
{
    ULONG i;
    for (i = 0; i + 4 <= n && i < 64; i++) if (b[i] == 0xAA && b[i + 1] == 0x99 && b[i + 2] == 0x55 && b[i + 3] == 0x66) return (int)i;
    return -1;
}

static void job_go(int kind, int btn, ULONG a, ULONG n, const char *name)
{
    struct Process *pr;
    memset((void *)&jc, 0, sizeof jc);
    jc.kind = kind; jc.a = a; jc.n = n; jc.total = n; jc.phase = 1; jc.buf = fbuf;
    jc.chunk = kind == J_CRC ? 262144u : kind == J_WRITE || kind == J_ERASE ? fi.pfi_SectorSize : 65536u;
    if (kind == J_WRITE || kind == J_ERASE) jc.unit = wr_unit(a, n, &jc.uoff);
    job = kind; job_btn = btn; job_name = name; job_phase = 1;
    pr_start(n, kind == J_ERASE ? P_AMB : P_GRN);
    say(P_AMB, "%s 0x%06lX-0x%06lX...", name, (unsigned long)a, (unsigned long)(a + n - 1u));
    pr = CreateNewProcTags(NP_Entry, (ULONG)worker, NP_Name, (ULONG)"flashmon job", NP_StackSize, 8192, NP_Priority, 0,
                           NP_WindowPtr, (ULONG)-1, TAG_DONE);   /* no DOS requesters: a full disk is an error */
    if (!pr) { job = J_NONE; say(P_RED, "The job's process did not start"); pr_stop(0); }
    buttons_state();
}

/* one display update while a job runs: progress, a new phase, the end */
static void job_tick(void)
{
    char a1[16];
    ULONG a = jc.a, n = jc.n;
    if (!job) return;
    if (jc.phase != job_phase) { pr_stop(1); job_phase = jc.phase; pr_start(jc.total, P_GRN); job_name = "Check"; }
    pr_set(jc.done);
    if (!jc.state) return;
    jc.fail = jc.err || jc.stop || (job == J_VERIFY && jc.bad);
    pr_stop(!jc.fail);
    fmt_thousands(a1, n);
    if (jc.err == -100) say(P_RED, "%s: the file's write failed at 0x%06lX", job_name, (unsigned long)jc.at);
    else if (jc.err) say(P_RED, "%s failed at 0x%06lX: %s (error %d)", job_name, (unsigned long)jc.at, pfl_err(jc.err), jc.err);
    else if (jc.stop) say(P_AMB, "Stopped at 0x%06lX", (unsigned long)(a + jc.done));
    else switch (job) {
    case J_READ:
        crc_chip = jc.crc; crc_chip_n = n; crc_chip_ok = 1;
        say(P_GRN, "Read %s bytes from 0x%06lX, CRC32 %08lX", a1, (unsigned long)a, (unsigned long)jc.crc);
        break;
    case J_DUMP:
        say(P_GRN, "%s: %s bytes from 0x%06lX, CRC32 %08lX", fname, a1, (unsigned long)a, (unsigned long)jc.crc);
        break;
    case J_VERIFY: {
        ULONG cb = crc32(fbuf, n);
        crc_chip = jc.crc; crc_chip_n = n; crc_chip_ok = 1;
        if (jc.bad) say(P_RED, "Verify: %lu bytes differ, the first at 0x%06lX; CRC32 flash %08lX, buffer %08lX", (unsigned long)jc.bad,
                        (unsigned long)jc.first, (unsigned long)jc.crc, (unsigned long)cb);
        else say(P_GRN, "Verify: 0x%06lX-0x%06lX equal, CRC32 %08lX", (unsigned long)a, (unsigned long)(a + n - 1u), (unsigned long)jc.crc);
        break;
    }
    case J_WRITE: {
        ULONG cb = crc32(fbuf, n);
        crc_chip = jc.crc; crc_chip_n = n; crc_chip_ok = 1;
        if (jc.crc != cb) { jc.fail = 1; say(P_RED, "Written, but the range's CRC32 %08lX differs from the buffer's %08lX", (unsigned long)jc.crc, (unsigned long)cb); }
        else say(P_GRN, "Wrote %s bytes at 0x%06lX, read back; CRC32 %08lX", a1, (unsigned long)a, (unsigned long)jc.crc);
        if (fdirty) memset(fdirty, 0, (fbuf_len + 7u) / 8u);
        break;
    }
    case J_ERASE: say(P_GRN, "Erased 0x%06lX-0x%06lX", (unsigned long)a, (unsigned long)(a + n - 1u)); break;
    case J_CRC:
        crc_chip = jc.crc; crc_chip_n = n; crc_chip_ok = 1;
        say(P_GRN, "CRC32 of 0x%06lX-0x%06lX: %08lX", (unsigned long)a, (unsigned long)(a + n - 1u), (unsigned long)jc.crc);
        break;
    }
    if (job == J_DUMP && jc.fh) { Close(jc.fh); jc.fh = 0; if (jc.fail) DeleteFile((CONST_STRPTR)fname); }
    job = J_NONE;
    crc_line(0);
    buttons_state();
    if (!jc.err) info_refresh(1);                          /* the guard's counts and the sectors erased; the job's line stays */
}

static int file_ask(int save, char *path, int max, const char *title);
static int room_ok(const char *path, ULONG n);

static void fl_op(int which)
{
    ULONG a, n, off;
    char q[256], a1[16];
    int u;
    if (job) {                                             /* the running job's button is Stop */
        if (which == job_btn) { jc.stop = 1; say(P_AMB, "%s: stopping after the current piece", job_name); }
        return;
    }
    if (parse_range(&a, &n, 0)) return;
    switch (which) {
    case B_RD:
        if (buf_alloc(n)) { say(P_RED, "No memory for %lu bytes: Dump reads to a file", (unsigned long)n); return; }
        fbase = a; hv_cur = 0; hv_pos = hv_target = 0;
        job_go(J_READ, B_RD, a, n, "Read");
        break;
    case B_DUMP: {
        BPTR fh;
        snprintf(fname, sizeof fname, "RAM:flash_%06lX.bin", (unsigned long)a);
        if (!file_ask(1, fname, sizeof fname, "Dump the Range to a File")) return;
        if (!room_ok(fname, n)) return;
        if (!(fh = Open((CONST_STRPTR)fname, MODE_NEWFILE))) { say(P_RED, "%s does not open for writing", fname); return; }
        job_go(J_DUMP, B_DUMP, a, n, "Dump");
        jc.fh = fh;
        break;
    }
    case B_CRC: job_go(J_CRC, B_CRC, a, n, "CRC"); break;
    case B_VFY:
        if (!fbuf_len) { say(P_RED, "The buffer is empty: Read or Open first"); return; }
        if (n > fbuf_len) n = fbuf_len;
        fbase = a;
        job_go(J_VERIFY, B_VFY, a, n, "Verify");
        break;
    case B_WR: case B_ER:
        if (which == B_WR) {
            if (!fbuf_len) { say(P_RED, "The buffer is empty: Open a file or Read first"); return; }
            n = fbuf_len;
        }
        if ((u = wr_unit(a, n, &off)) < 0) {
            say(P_RED, "0x%06lX-0x%06lX is not inside the main slot or the free region", (unsigned long)a, (unsigned long)(a + n - 1u));
            return;
        }
        if (a % fi.pfi_SectorSize || (which == B_ER && n % fi.pfi_SectorSize)) {
            say(P_RED, "%s on %lu KB sector boundaries", which == B_WR ? "Start" : "Start and Length", (unsigned long)fi.pfi_SectorSize >> 10);
            return;
        }
        if (which == B_WR && u == PFL_UNIT_MAIN && off == 0 && has_sync(fbuf, fbuf_len) < 0) {
            say(P_RED, "The buffer has no bitstream sync word (AA 99 55 66) in its first 64 bytes: not written at the main slot");
            return;
        }
        fmt_thousands(a1, n);
        if (which == B_WR)
            snprintf(q, sizeof q, "Write %s bytes of the buffer to 0x%06lX-0x%06lX (%s)?\n%s", a1, (unsigned long)a,
                     (unsigned long)(a + n - 1u), u == PFL_UNIT_MAIN ? "main slot" : "free region",
                     u == PFL_UNIT_MAIN ? "The main slot holds the fabric and firmware loaded at power-up." : "");
        else
            snprintf(q, sizeof q, "Erase 0x%06lX-0x%06lX (%s)?\n%s", (unsigned long)a, (unsigned long)(a + n - 1u),
                     u == PFL_UNIT_MAIN ? "main slot" : "free region",
                     u == PFL_UNIT_MAIN && off == 0 ? "Without the main slot the unit starts the golden image at power-up." : "");
        if (!confirm(q)) return;
        fbase = a;
        job_go(which == B_WR ? J_WRITE : J_ERASE, which, a, n, which == B_WR ? "Write" : "Erase");
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
    ULONG a, n;
    int ok;
    if (job) return;
    if (!file_ask(0, fname, sizeof fname, "Load the Buffer")) return;
    if (!(fh = Open((CONST_STRPTR)fname, MODE_OLDFILE))) { say(P_RED, "%s does not open", fname); return; }
    Seek(fh, 0, OFFSET_END); sz = Seek(fh, 0, OFFSET_BEGINNING);
    if (sz <= 0 || buf_alloc((ULONG)sz)) { Close(fh); say(P_RED, sz <= 0 ? "%s is empty" : "No memory for %s", fname); return; }
    if (Read(fh, fbuf, sz) != sz) say(P_RED, "%s: read error", fname);
    else {
        char t[16]; fmt_thousands(t, (ULONG)sz);
        memset(fdirty, 0xFF, (fbuf_len + 7u) / 8u);
        crc_file = crc32(fbuf, (ULONG)sz); crc_file_n = (ULONG)sz; crc_file_ok = 1;
        if (parse_range(&a, &n, 1)) { a = parse_hex(op_start, &ok); if (!ok) a = 0; }
        fbase = a;
        say(P_GRN, "%s: %s bytes, CRC32 %08lX%s", fname, t, (unsigned long)crc_file,
            has_sync(fbuf, fbuf_len) >= 0 ? ", a bitstream" : "");
        range_set(a, (ULONG)sz);
    }
    Close(fh);
    hv_cur = 0; hv_pos = hv_target = 0;
    layout();
}

static void file_save(void)
{
    BPTR fh;
    ULONG n = hv_len();
    if (!n) { say(P_RED, "The buffer is empty"); return; }
    if (!file_ask(1, fname, sizeof fname, "Save the Buffer")) return;
    if (!room_ok(fname, n)) return;
    if (!(fh = Open((CONST_STRPTR)fname, MODE_NEWFILE))) { say(P_RED, "%s does not open for writing", fname); return; }
    if (Write(fh, hv_buf(), (LONG)n) != (LONG)n) { Close(fh); DeleteFile((CONST_STRPTR)fname); say(P_RED, "%s: write error", fname); return; }
    { char a[16]; fmt_thousands(a, n); say(P_GRN, "%s: %s bytes saved, CRC32 %08lX", fname, a, (unsigned long)crc32(hv_buf(), n)); }
    Close(fh);
}

/* ---- the bitstream of the buffer ------------------------------------------------------------------------------
 * Spartan-6 configuration packets (UG380 5): 16-bit words, big-endian.  Type 1: [15:13] 001, [12:11] opcode,
 * [10:5] register, [4:0] word count.  Type 2: [15:13] 010, then a 32-bit word count.  bitgen follows each FDRI
 * type-2 block with a 32-bit auto-CRC.  The stream ends with a write of CMD (register 5) = DESYNC (0x000D) and NO-OPs
 * (0x2000).  bit_walk is tools/g2flash's (g2flash.c bit_walk), with the write of IDCODE (register 0x0E, two words)
 * taken on the way.  Returns the offset after the last NO-OP, or -1. */
static long bit_walk(const UBYTE *b, ULONG n, long sync, ULONG *packets, ULONG *idcode)
{
    ULONG p = (ULONG)sync + 4, np = 0;
    while (p + 2 <= n) {
        unsigned h = ((unsigned)b[p] << 8) | b[p + 1], t = h >> 13;
        np++;
        if (t == 1) {
            unsigned op = (h >> 11) & 3u, reg = (h >> 5) & 63u, wc = h & 31u;
            p += 2;
            if (op == 2 && reg == 5 && wc == 1 && p + 2 <= n && b[p] == 0 && b[p + 1] == 0x0D) {
                p += 2;
                while (p + 2 <= n && b[p] == 0x20 && b[p + 1] == 0) p += 2;
                if (packets) *packets = np;
                return (long)p;
            }
            if (op == 2 && reg == 0x0E && wc == 2 && p + 4 <= n && idcode)
                *idcode = (ULONG)b[p] << 24 | (ULONG)b[p + 1] << 16 | (ULONG)b[p + 2] << 8 | b[p + 3];
            p += 2u * wc;
        } else if (t == 2) {
            ULONG wc;
            if (p + 6 > n) break;
            wc = ((ULONG)b[p + 2] << 24) | ((ULONG)b[p + 3] << 16) | ((ULONG)b[p + 4] << 8) | b[p + 5];
            p += 6 + 2u * wc;
            if (p + 2 <= n && (b[p] >> 5) == 0) p += 4;       /* the auto-CRC after an FDRI block */
        } else break;
    }
    if (packets) *packets = np;
    return -1;
}

/* the part bitgen names in a .bit file for an IDCODE (version bits 31:28 ignored) */
static const char *bit_part(ULONG id)
{
    switch (id & 0x0FFFFFFFu) {
    case 0x04011093u: return "6slx100fgg484";      /* revision C */
    case 0x0401D093u: return "6slx150fgg484";      /* revisions A and B */
    default: return NULL;
    }
}

/* a field of the .bit header: key, 16-bit length (with the NUL), the string */
static ULONG bit_field(UBYTE *h, ULONG o, char key, const char *s)
{
    ULONG l = (ULONG)strlen(s) + 1u;
    h[o] = (UBYTE)key; h[o + 1] = (UBYTE)(l >> 8); h[o + 2] = (UBYTE)l;
    memcpy(h + o + 3, s, l);
    return o + 3 + l;
}

/* the date of a DateStamp: days since 1978-01-01 to year, month, day */
static void civil(LONG days, int *y, int *m, int *d)
{
    static const int ml[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int yy = 1978, mm = 0;
    for (;;) { int yl = (yy % 4 == 0 && (yy % 100 || yy % 400 == 0)) ? 366 : 365; if (days < yl) break; days -= yl; yy++; }
    for (;;) { int l = ml[mm] + (mm == 1 && yy % 4 == 0 && (yy % 100 || yy % 400 == 0)); if (days < l) break; days -= l; mm++; }
    *y = yy; *m = mm + 1; *d = (int)days + 1;
}

/* room for n more bytes at path: RAM: from free memory (its Info() reports no free blocks) less 512 KB for the
 * system, any other volume from Info(); 1 when it fits or the volume does not tell */
static int room_ok(const char *path, ULONG n)
{
    struct InfoData *id;
    char dir[256], *f;
    BPTR l;
    int ok = 1;
    if (!strnicmp(path, "RAM:", 4)) {
        ULONG fr = AvailMem(MEMF_ANY);
        if (fr < n + 524288u) { say(P_RED, "RAM: has %lu KB free, %lu KB are needed: choose another volume", (unsigned long)fr >> 10, (unsigned long)(n + 524288u) >> 10); return 0; }
        return 1;
    }
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    f = (char *)PathPart((STRPTR)dir); *f = 0;
    if (!(id = (struct InfoData *)AllocVec(sizeof *id, MEMF_PUBLIC))) return 1;
    if ((l = Lock((CONST_STRPTR)dir, ACCESS_READ)) != 0) {
        if (Info(l, id) && id->id_NumBlocks) {
            unsigned long long fr = (unsigned long long)(id->id_NumBlocks - id->id_NumBlocksUsed) * (ULONG)id->id_BytesPerBlock;
            if (fr < n) { say(P_RED, "%s has %lu KB free, %lu KB are needed", dir, (unsigned long)(fr >> 10), (unsigned long)n >> 10); ok = 0; }
        }
        UnLock(l);
    }
    FreeVec(id);
    return ok;
}

/* a file of the bytes a then b; a partial file is deleted */
static int save_file(const char *path, const UBYTE *a, ULONG na, const UBYTE *b, ULONG nb)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    int ok;
    if (!fh) return -1;
    ok = (!na || Write(fh, (APTR)a, (LONG)na) == (LONG)na) && Write(fh, (APTR)b, (LONG)nb) == (LONG)nb;
    Close(fh);
    if (!ok) DeleteFile((CONST_STRPTR)path);
    return ok ? 0 : -1;
}

static void bit_extract(void)
{
    long s, end;
    ULONG np = 0, id = 0, o;
    UBYTE h[256];
    char base[256], path[264], t[96], a1[16], *dot;
    const char *part;
    struct DateStamp ds;
    int y, m, d, raw;
    if (!fbuf_len) { say(P_RED, "The buffer is empty: Read a slot (Main slot or Golden image) first"); return; }
    if ((s = has_sync(fbuf, fbuf_len)) < 0) { say(P_RED, "No sync word (AA 99 55 66) in the buffer's first 64 bytes: not a bitstream"); return; }
    if ((end = bit_walk(fbuf, fbuf_len, s, &np, &id)) < 0) {
        say(P_RED, "The packets run past the buffer's end (%lu walked): read the whole slot", (unsigned long)np);
        return;
    }
    part = bit_part(id);
    snprintf(base, sizeof base, "RAM:flash_%06lX.bit", (unsigned long)fbase);
    if (!file_ask(1, base, sizeof base, "Save the Bitstream (.bit, or .bin raw)")) return;
    raw = (dot = strrchr(base, '.')) != NULL && !stricmp(dot, ".bin");
    if (!room_ok(base, (ULONG)end + 512u)) return;
    if (raw) {
        if (save_file(base, NULL, 0, fbuf, (ULONG)end)) { say(P_RED, "%s: write error", base); return; }
        fmt_thousands(a1, (ULONG)end);
        say(P_GRN, "%s: raw bitstream, %s bytes, %lu packets, IDCODE %08lX, CRC32 %08lX", FilePart((STRPTR)base), a1,
            (unsigned long)np, (unsigned long)id, (unsigned long)crc32(fbuf, (ULONG)end));
        return;
    }
    if (dot && !stricmp(dot, ".bit")) *dot = 0;
    DateStamp(&ds); civil(ds.ds_Days, &y, &m, &d);
    snprintf(t, sizeof t, "%s;from flash 0x%06lX", FilePart((STRPTR)base), (unsigned long)fbase);
    o = 13;
    memcpy(h, "\x00\x09\x0f\xf0\x0f\xf0\x0f\xf0\x0f\xf0\x00\x00\x01", 13);
    o = bit_field(h, o, 'a', t);
    o = bit_field(h, o, 'b', part ? part : "unknown");
    snprintf(t, sizeof t, "%04d/%02d/%02d", y, m, d); o = bit_field(h, o, 'c', t);
    snprintf(t, sizeof t, "%02ld:%02ld:%02ld", (long)ds.ds_Minute / 60, (long)ds.ds_Minute % 60, (long)ds.ds_Tick / 50); o = bit_field(h, o, 'd', t);
    h[o] = 'e'; h[o + 1] = (UBYTE)((ULONG)end >> 24); h[o + 2] = (UBYTE)((ULONG)end >> 16); h[o + 3] = (UBYTE)((ULONG)end >> 8); h[o + 4] = (UBYTE)end;
    o += 5;
    snprintf(path, sizeof path, "%s.bit", base);
    if (save_file(path, h, o, fbuf, (ULONG)end)) { say(P_RED, "%s: write error", path); return; }
    fmt_thousands(a1, (ULONG)end);
    if (part) say(P_GRN, "%s.bit: %s bytes, %lu packets, IDCODE %08lX (%s), CRC32 %08lX", FilePart((STRPTR)base), a1,
                  (unsigned long)np, (unsigned long)id, part, (unsigned long)crc32(fbuf, (ULONG)end));
    else say(P_AMB, "%s.bit: %s bytes, %lu packets; IDCODE %08lX is not a G2 part (field b: unknown)", FilePart((STRPTR)base),
             a1, (unsigned long)np, (unsigned long)id);
}

/* ---- menu, events, main ----------------------------------------------------------------------------------------- */

enum { MI_ABOUT = 1, MI_QUIT, MI_INFO, MI_PAGE0 };
static struct NewMenu nm[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Flash", (STRPTR)"1", 0, 0, (APTR)(MI_PAGE0 + 0) },
    { NM_ITEM, (STRPTR)"Hex Editor", (STRPTR)"2", 0, 0, (APTR)(MI_PAGE0 + 1) },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Device Info...", (STRPTR)"I", 0, 0, (APTR)MI_INFO },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)MI_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MI_QUIT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static int quit;

static void page_set(int p)
{
    ULONG a, n;
    if (p == page || p < 0 || p >= PG_N) return;
    if (page == PG_FL) (void)parse_range(&a, &n, 1);       /* Start and Length as typed */
    page = p;
    layout();
}

static void button(int i)
{
    if (i >= B_TAB0 && i < B_TAB0 + PG_N) { page_set(i - B_TAB0); return; }
    switch (i) {
    case B_REFRESH: info_refresh(0); break;
    case B_INFO: di_open(); break;
    case B_RD: case B_VFY: case B_WR: case B_ER: case B_CRC: case B_DUMP: fl_op(i); break;
    case B_OPEN: case B_HOPEN: if (!job) file_open(); break;
    case B_SAVE: case B_HSAVE: if (!job) file_save(); break;
    case B_BIT: if (!job) bit_extract(); break;
    case B_HFILL:
        if (fbuf && !job && confirm("Fill the whole buffer with FF?")) {
            memset(fbuf, 0xFF, fbuf_len); memset(fdirty, 0xFF, (fbuf_len + 7u) / 8u); hv_full();
        }
        break;
    }
}

/* the keys of the Flash page (the Hex Editor's keys edit) */
static void page_key(int c)
{
    static const char keys[] = "rvwecdosbi";
    static const int act[] = { B_RD, B_VFY, B_WR, B_ER, B_CRC, B_DUMP, B_OPEN, B_SAVE, B_BIT, B_INFO };
    const char *k = strchr(keys, c);
    if (page == PG_FL && c && k) button(act[k - keys]);
}

static void gadget_up(struct Gadget *g, UWORD code)
{
    ULONG a, n;
    switch (g->GadgetID) {
    case G_REGION:
        rg_i = code;
        if (!region(rg_i, &a, &n)) range_set(a, n);
        break;
    case G_START: case G_LEN:
        rg_i = R_CUSTOM;
        if (gad[G_REGION]) GT_SetGadgetAttrs(gad[G_REGION], win, NULL, GTCY_Active, (ULONG)rg_i, TAG_DONE);
        (void)parse_range(&a, &n, 0);
        map_draw();
        break;
    case G_HSCR: hv_target = (long)code * hv_unit() * 8; hv_clamp_target(); break;
    case G_GOTO: {
        int ok;
        ULONG v = parse_hex((const char *)((struct StringInfo *)g->SpecialInfo)->Buffer, &ok);
        if (ok && v >= fbase && v - fbase < hv_len()) { ULONG old = hv_cur; hv_cur = v - fbase; hv_nib = 0; hv_line(old); hv_line(hv_cur); hv_show_cursor(); }
        else say(P_RED, "Go to: a hex address from %06lX to %06lX", (unsigned long)fbase, (unsigned long)(fbase + (hv_len() ? hv_len() - 1u : 0)));
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
    static LONG opt[1];
    struct RDArgs *ra = NULL;
    struct Screen *wb;
    ULONG mode, sw, sh, sigs, hn = 0;
    unsigned long long next, period;
    int i, t_open = 0;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR wp_old = me->pr_WindowPtr;
    (void)argv;
    if (argc > 0) {
        if (!(ra = ReadArgs((CONST_STRPTR)"PAGE/N", opt, NULL))) { printf("usage: flashmon [PAGE=0-1]\n"); return 20; }
        if (opt[0] && *(LONG *)opt[0] >= 0 && *(LONG *)opt[0] < PG_N) page = (int)*(LONG *)opt[0];
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (!IntuitionBase || !GfxBase || !GadToolsBase) { printf("flashmon: needs AmigaOS 3.0 or later\n"); goto out; }
    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof *treq) : NULL;
    if (!treq || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_WAITECLOCK, (struct IORequest *)treq, 0)) { printf("flashmon: timer.device\n"); goto out; }
    t_open = 1; TimerBase = treq->tr_node.io_Device;
    { struct EClockVal v; efreq = ReadEClock(&v); }
    AddIntServer(INTB_VERTB, &vbl_is); have_vbl = 1;
    beam_measure();
    period = efreq / TICK_HZ;
    if (!(wb = LockPubScreen((CONST_STRPTR)"Workbench"))) goto out;
    mode = GetVPModeID(&wb->ViewPort); sw = wb->Width; sh = wb->Height;
    if (sw < 640) { ULONG hm = (mode & MONITOR_ID_MASK) | HIRES_KEY | (mode & LORESLACE_KEY); if (!ModeNotAvailable(hm)) { mode = hm; sw = 640; } }
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, sw, SA_Height, sh, SA_Depth, 4, SA_Font, (ULONG)&topaz8,
                         SA_Pens, (ULONG)pens, SA_Colors, (ULONG)colours, SA_Title, (ULONG)"Flash Monitor", SA_ShowTitle, TRUE,
                         SA_Type, CUSTOMSCREEN, SA_Interleaved, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, wb);
    if (!scr) { printf("flashmon: the screen did not open\n"); goto out; }
    font = OpenFont(&topaz8);
    vi = GetVisualInfo(scr, TAG_DONE);
    if (!vi || !font) goto out;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0, WA_Width, (ULONG)scr->Width,
                         WA_Height, (ULONG)scr->Height, WA_Title, (ULONG)"Flash Monitor     G2 configuration flash, panoflash.device",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                         WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBRight, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_ReportMouse, TRUE,
                         WA_IDCMP, BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP | SCROLLERIDCMP |
                                   IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MENUPICK |
                                   IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS, TAG_DONE);
    if (!win) { printf("flashmon: the window did not open\n"); goto out; }
    rp = win->RPort; SetFont(rp, font);
    {
        int bw = win->BorderLeft + win->BorderRight, bh = win->BorderTop + win->BorderBottom;
        int maxw = scr->Width < WIN_W_MAX + bw ? scr->Width : WIN_W_MAX + bw;
        if (scr->Width < WIN_W_MIN + bw || scr->Height < WIN_H_MIN + bh) { printf("flashmon: the screen is too small (640 x 200 is the least)\n"); goto out; }
        WindowLimits(win, (WORD)(WIN_W_MIN + bw), (WORD)(WIN_H_MIN + bh), (UWORD)maxw, (UWORD)scr->Height);
        if (win->Width > maxw) ChangeWindowBox(win, 0, 0, (WORD)maxw, win->Height);
    }
    row_bm = AllocBitMap(1024, 8, 4, BMF_CLEAR, scr->RastPort.BitMap);
    if (row_bm) { InitRastPort(&row_rp); row_rp.BitMap = row_bm; SetFont(&row_rp, font); }
    if ((menu = CreateMenus(nm, TAG_DONE)) != NULL && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    me->pr_WindowPtr = (APTR)-1;                           /* no DOS requesters: a full disk is an error message */
    say(P_GREY, "Reading the part...");
    if (!layout()) goto out;
    info_refresh(0);
    { ULONG a, n; if (fi_ok && !region(rg_i, &a, &n)) range_set(a, n); }

    next = eclock() + period;
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
    SendIO((struct IORequest *)treq);
    while (!quit) {
        struct IntuiMessage *im;
        ULONG msig = awin ? (1ul << awin->UserPort->mp_SigBit) | ab_sigmask | (ab_tport ? 1ul << ab_tport->mp_SigBit : 0) : 0;
        if (dwin) msig |= 1ul << dwin->UserPort->mp_SigBit;
        sigs = Wait((1ul << win->UserPort->mp_SigBit) | (1ul << tport->mp_SigBit) | msig | SIGBREAKF_CTRL_C);
        if (awin && ab_tbusy && ab_tport && (sigs & (1ul << ab_tport->mp_SigBit)) && GetMsg(ab_tport)) { ab_tbusy = 0; ab_chain(); }
        if (awin && (sigs & ab_sigmask)) ab_frame();
        if (sigs & SIGBREAKF_CTRL_C) quit = 1;
        if ((sigs & (1ul << tport->mp_SigBit)) && GetMsg(tport)) {
            unsigned long long now = eclock();
            hn++;
            if (job) job_tick();
            if (!awin) {                                   /* behind the About box nothing is drawn */
                hv_tick();
                if (page == PG_HEX) {                      /* an arrow held: a line every 3 updates after 300 ms */
                    int up = (bt[B_HUP].g.Flags & GFLG_SELECTED) != 0, dn = (bt[B_HDN].g.Flags & GFLG_SELECTED) != 0;
                    if (up || dn) { if (++hv_hold > 18 && !(hv_hold % 3)) hv_arrow(up ? -1 : 1); } else hv_hold = 0;
                }
                pr_tick(hn);
                if (s_msg_new) draw_status();
                for (i = 0; i < B_N; i++) if (bt[i].on) btn_draw(&bt[i]);
            } else btn_draw(&abtn);
            if (dwin) btn_draw(&dbtn);
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
        while (dwin && (im = GT_GetIMsg(dwin->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (!di_event(cls, code, g)) { di_close(); break; }
        }
        while ((im = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code, qual = im->Qualifier;
            WORD mx = im->MouseX, my = im->MouseY;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); continue; }
            if (awin || dwin) continue;                    /* the About box and Device Info are modal */
            switch (cls) {
            case IDCMP_NEWSIZE: if (!layout()) quit = 1; break;
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
                if (page == PG_HEX && code != 0x1B && code != '\t') { if (!job) hv_key(code); break; }
                if (code == '\t' && page == PG_HEX) { hv_ascii ^= 1; hv_nib = 0; hv_line(hv_cur); break; }
                if (code == 0x1B) quit = 1;
                else if (code >= '1' && code <= '2') page_set(code - '1');
                else if ((code | 0x20) == 'q') quit = 1;
                else if (code == '?') ab_open();
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
                    else if (id == MI_INFO) di_open();
                    else if (id >= MI_PAGE0 && id < MI_PAGE0 + PG_N) page_set((int)(id - MI_PAGE0));
                    code = it->NextSelect;
                }
                break;
            }
        }
        if (quit && job) {                                 /* a running job is stopped and waited for first */
            jc.stop = 1;
            say(P_AMB, "Stopping %s before quitting...", job_name); draw_status();
            for (i = 0; i < 3000 && !jc.state; i++) Delay(1);
            if (!jc.state) { quit = 0; say(P_RED, "The job does not end; quit again later"); }
            else job_tick();
        }
    }
    AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq);
out:
    ab_close();
    di_close();
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
    me->pr_WindowPtr = wp_old;
    return 0;
}
