/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* midimon.c -- MIDI on Paula's serial port (31,250 baud, 8N1): a loopback test, port activity meters, a message
 * sender and a monitor, in one window on a screen of its own.
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
 *
 *   midimon [START] [RATE=PERCENT] [SECONDS=N]
 *     START       the loopback test runs from the start
 *     RATE        the test's load: 25, 50, 75 or 100 percent of the wire rate (default 100)
 *
 *   Keys: Space or S starts and stops the test, 1 to 4 set its load, M opens the send panel, C clears the monitor,
 *   P pauses it, T and R show test data and real-time messages in it, Q or Esc quits.  The send panel: Return or S
 *   sends, Esc or C closes it, T steps the message type.
 *     SECONDS     the test stops after N seconds; the program then quits and prints its result
 *
 * 1 Serial hardware.  The program owns Paula's serial port while it runs: MR_SERIALPORT and MR_SERIALBITS of
 *   misc.resource (serial.device is expunged first when nothing has it open), SERPER for the nearest rate at or below
 *   31,250 baud from the colour clock (3,546,895 Hz PAL: SERPER 113, 31,113 baud, -0.44 %; 3,579,545 Hz NTSC:
 *   SERPER 114, 31,126 baud, -0.39 %), so the stream never outruns a receiver's DIN wire, and
 *   its own RBF (level 5) and TBE (level 1) interrupt vectors.  The RBF handler stores each byte with its time in a
 *   4,096-byte ring; the TBE handler takes the next byte from a 4,096-byte ring and writes SERDAT, so bytes leave
 *   back to back.  An empty ring leaves the transmitter idle; a write to the ring sets the TBE request again.
 *
 * 2 Time.  A clock of scan-line steps: the beam's line from VPOSR/VHPOSR read as one longword, plus the frame's
 *   lines for each vertical blank the program's server (first in the chain) has counted; a reading near the top of a
 *   frame whose vertical blank is still pending counts that frame already.  The frame's lines are measured at the
 *   start (the highest line of one frame, plus one): 313 on PAL (64 us a line, 15,625 a second), 263 on NTSC
 *   (63.556 us, 15,734 a second).
 *
 * 3 Loopback test.  Message s of the test (s modulo 2^17) is a system exclusive message when s mod 32 = 31
 *   (F0 7D s[20:14] s[13:7] s[6:0], eight data bytes (5s + 13i) mod 128, F7: 14 bytes), else a 3-byte channel
 *   message: Note On, Poly Pressure and Pitch Bend in turn (s mod 3), channel s[3:0], first data byte s[10:4],
 *   second 40h | s[16:11] (never 0, so no Note On reads as a Note Off).  Every message has its sequence number;
 *   the receiver counts a gap of fewer than 1,024 as lost messages and a malformed or out-of-order message as an
 *   error; two in a row from elsewhere in the sequence set it there.  At the end, lost is every message sent and
 *   not received intact.  A test starts with 128 Active Sensing bytes, everything received until 32 of them are
 *   back and the line is quiet for 100 ms (at most 3 s) discarded, and ends when every message is back or 2 s after
 *   the last was sent, with Active Sensing bytes sent meanwhile: the USB MIDI device passes its last bytes to the
 *   host only as more follow.  The
 *   transmitter's rate is set by a token bucket from the E clock.  Round trip: the time the TBE handler wrote a
 *   message's status byte to the time the RBF handler took it back (up to one byte time, 320 us, early on the
 *   transmit side, one scan line resolution).
 *
 * 4 Display.  60 updates a second from timer.device's E clock (UNIT_WAITECLOCK); the PAL display shows 50 frames
 *   a second.  Meters: bars of a 250 ms window, figures of a 1 s window, peak held 1 s; bit/s is bytes x 10 (start,
 *   8 data bits, stop), full scale 31,250; interrupts per second, full scale 3,125.  The screen copies the
 *   Workbench's display mode with 16 colours and topaz 8; buttons are drawn by the program (section "buttons"). */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <hardware/blit.h>
#include <graphics/sprite.h>
#include <graphics/copper.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <resources/misc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/timer.h>
#include <proto/misc.h>
#include "font8x8.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *GadToolsBase;
struct Device        *TimerBase;
struct Library       *MiscBase;

#define HWREG    ((volatile struct Custom *)0xDFF000)
#define VPOSL     (*(volatile ULONG *)0xDFF004)            /* VPOSR:VHPOSR, line in bits 16:8 */
static ULONG b_lines = 313u, b_hz = 15625u, b_ns = 64000u;   /* the frame's lines, lines a second, ns a line */

#define RING      4096u
#define RMASK     (RING - 1u)
#define STAMPS    1024u
#define SEQ_MOD   (1ul << 17)
#define WIRE_BPS  31250u
#define BYTES_PS  3125u
#define TICK_HZ   60u
#define SEGS      36                                      /* meter segments: 4 pixels, 3 lit and a gap */
#define MON_MAX   200

/* ---- section 1 and 2: the interrupt side ---------------------------------------------------------------- */

static volatile UBYTE  rxq[RING];
static volatile ULONG  rxt[RING];
static volatile UWORD  rx_w, rx_r;
static volatile UBYTE  txq[RING];
static volatile UWORD  tx_w, tx_r, tx_idle = 1;
static volatile ULONG  tx_st[STAMPS], tx_sn;               /* send times of status bytes, in order */
static volatile ULONG  n_tx, n_rx, n_tbe, n_rbf, n_ovr, n_full;
static volatile ULONG  b_frames;                          /* vertical blanks served */

/* the clock: frames served times b_lines plus the beam's line.  A reading near the top of a frame whose vertical
 * blank is still pending (INTREQR) counts that frame already. */
static ULONG beam_sample(void)
{
    UWORD line = (UWORD)((VPOSL >> 8) & 0x1FFu);
    ULONG f = b_frames;
    if (line < 32u && (HWREG->intreqr & INTF_VERTB)) f++;
    return f * b_lines + line;
}

static ULONG beam_now(void)
{
    ULONG t;
    Disable(); t = beam_sample(); Enable();
    return t;
}

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

static void rbf_code(void)
{
    volatile struct Custom *c = HWREG;
    UWORD d = c->serdatr, n;
    c->intreq = INTF_RBF;
    n_rbf++;
    if (d & 0x8000u) n_ovr++;
    n = (rx_w + 1u) & RMASK;
    if (n == rx_r) { n_full++; return; }
    rxt[rx_w] = beam_sample();
    rxq[rx_w] = (UBYTE)d;
    rx_w = n;
    n_rx++;
}

static void tbe_code(void)
{
    volatile struct Custom *c = HWREG;
    n_tbe++;
    c->intreq = INTF_TBE;                                  /* before SERDAT: an idle shifter takes it at once */
    if (tx_r != tx_w) {
        UBYTE b = txq[tx_r];
        tx_r = (tx_r + 1u) & RMASK;
        if ((b & 0x80u) && b != 0xF7u && b < 0xF8u) {
            ULONG t;
            c->intena = INTF_INTEN; t = beam_sample(); c->intena = INTF_SETCLR | INTF_INTEN;   /* no VBL between */
            tx_st[tx_sn & (STAMPS - 1u)] = t; tx_sn++;
        }
        c->serdat = 0x100u | b;                            /* one stop bit */
        n_tx++;
    } else tx_idle = 1;
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


static struct Interrupt rbf_is = { { NULL, NULL, NT_INTERRUPT, 0, (char *)"midimon RBF" }, NULL, (void (*)())rbf_code };
static struct Interrupt tbe_is = { { NULL, NULL, NT_INTERRUPT, 0, (char *)"midimon TBE" }, NULL, (void (*)())tbe_code };
static struct Interrupt vbl_is = { { NULL, NULL, NT_INTERRUPT, 127, (char *)"midimon clock" }, NULL, (void (*)())vbl_stub };   /* first in the chain */
static struct Interrupt *old_rbf, *old_tbe;
static int have_port, have_bits, have_ints, have_vbl;

static void tx_kick(void)
{
    Disable();
    if (tx_idle && tx_r != tx_w) { tx_idle = 0; HWREG->intreq = INTF_SETCLR | INTF_TBE; }
    Enable();
}

static unsigned int tx_free(void) { return (tx_r - tx_w - 1u) & RMASK; }

static int tx_put(const UBYTE *b, unsigned int n)
{
    unsigned int i, w = tx_w;
    if (tx_free() < n) return 0;
    for (i = 0; i < n; i++) { txq[w] = b[i]; w = (w + 1u) & RMASK; }
    tx_w = (UWORD)w;
    tx_kick();
    return 1;
}

/* Active Sensing (0xFE) bytes: real-time, ignored by the test.  The USB MIDI device passes the bytes from its MIDI IN
 * to the host only as newer bytes follow them, so the last bytes of a run stay in it until something more is sent:
 * these push them through. */
static void tx_filler(unsigned int n)
{
    static const UBYTE fe[16] = { 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE };
    while (n >= 16u) { if (!tx_put(fe, 16u)) return; n -= 16u; }
    if (n) (void)tx_put(fe, n);
}

static const char *serial_open(void)
{
    static char why[96];
    const char *o;
    ULONG clock;
    MiscBase = (struct Library *)OpenResource((CONST_STRPTR)MISCNAME);
    if (!MiscBase) return "misc.resource not found";
    if ((o = (const char *)AllocMiscResource(MR_SERIALPORT, (CONST_STRPTR)"midimon")) != NULL) {
        struct Library *d;
        Forbid();
        d = (struct Library *)FindName(&SysBase->DeviceList, (CONST_STRPTR)"serial.device");
        if (d && d->lib_OpenCnt == 0) RemDevice((struct Device *)d);
        Permit();
        o = (const char *)AllocMiscResource(MR_SERIALPORT, (CONST_STRPTR)"midimon");
    }
    if (o) { snprintf(why, sizeof why, "The serial port is in use by %s.", o); return why; }
    have_port = 1;
    if ((o = (const char *)AllocMiscResource(MR_SERIALBITS, (CONST_STRPTR)"midimon")) != NULL) {
        snprintf(why, sizeof why, "The serial control bits are in use by %s.", o); return why;
    }
    have_bits = 1;
    clock = (GfxBase->DisplayFlags & PAL) ? 3546895ul : 3579545ul;
    HWREG->intena = INTF_RBF | INTF_TBE;
    HWREG->serper = (UWORD)((clock + WIRE_BPS - 1u) / WIRE_BPS - 1u);    /* rounded up: the rate at or below */
    AddIntServer(INTB_VERTB, &vbl_is); have_vbl = 1;
    beam_measure();
    old_rbf = SetIntVector(INTB_RBF, &rbf_is);
    old_tbe = SetIntVector(INTB_TBE, &tbe_is);
    have_ints = 1;
    (void)HWREG->serdatr;
    HWREG->intreq = INTF_RBF | INTF_TBE;
    HWREG->intena = INTF_SETCLR | INTF_RBF | INTF_TBE;
    return NULL;
}

static void serial_close(void)
{
    if (have_ints) {
        HWREG->intena = INTF_RBF | INTF_TBE;
        SetIntVector(INTB_RBF, old_rbf);
        SetIntVector(INTB_TBE, old_tbe);
    }
    if (have_vbl) RemIntServer(INTB_VERTB, &vbl_is);
    if (have_bits) FreeMiscResource(MR_SERIALBITS);
    if (have_port) FreeMiscResource(MR_SERIALPORT);
}

/* ---- E clock ------------------------------------------------------------------------------------------------ */

static ULONG efreq;

static unsigned long long eclock(void)
{
    struct EClockVal v;
    ReadEClock(&v);
    return ((unsigned long long)v.ev_hi << 32) | v.ev_lo;
}

/* ---- MIDI text -------------------------------------------------------------------------------------------- */

static const char *const note_nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void note_name(char *s, unsigned int n) { sprintf(s, "%s%d", note_nm[n % 12u], (int)(n / 12u) - 1); }

static const char *cc_name(unsigned int c)
{
    switch (c) {
    case 0: return "Bank MSB";  case 1: return "Modulation"; case 2: return "Breath";    case 4: return "Foot";
    case 5: return "Porta time"; case 6: return "Data entry"; case 7: return "Volume";   case 8: return "Balance";
    case 10: return "Pan";       case 11: return "Expression"; case 32: return "Bank LSB"; case 64: return "Sustain";
    case 65: return "Portamento"; case 66: return "Sostenuto"; case 67: return "Soft";   case 120: return "Sound off";
    case 121: return "Reset all"; case 123: return "Notes off";
    default: return "";
    }
}

/* name, channel (0 = none) and data of a message; total: its length for system exclusive */
static void describe(const UBYTE *m, unsigned int n, ULONG total, char *name, int *ch, char *data)
{
    unsigned int st = m[0], hi = st & 0xF0u;
    char nn[8];
    *ch = 0; data[0] = 0;
    if (st < 0xF0u) {
        *ch = (int)(st & 15u) + 1;
        switch (hi) {
        case 0x80: strcpy(name, "Note Off");       note_name(nn, m[1]); sprintf(data, "%-4s vel %u", nn, m[2]); break;
        case 0x90: strcpy(name, "Note On");        note_name(nn, m[1]); sprintf(data, "%-4s vel %u", nn, m[2]); break;
        case 0xA0: strcpy(name, "Poly Pressure");  note_name(nn, m[1]); sprintf(data, "%-4s %u", nn, m[2]); break;
        case 0xB0: strcpy(name, "Control Change"); sprintf(data, "%u %s = %u", m[1], cc_name(m[1]), m[2]); break;
        case 0xC0: strcpy(name, "Program Change"); sprintf(data, "%u", m[1]); break;
        case 0xD0: strcpy(name, "Chan Pressure");  sprintf(data, "%u", m[1]); break;
        default:   strcpy(name, "Pitch Bend");     sprintf(data, "%+d", (int)(m[1] | (m[2] << 7)) - 8192); break;
        }
        return;
    }
    switch (st) {
    case 0xF0: strcpy(name, "SysEx");
               if (n > 1) sprintf(data, "ID %02X, %lu bytes", m[1], (unsigned long)total);
               else sprintf(data, "%lu bytes", (unsigned long)total);
               break;
    case 0xF1: strcpy(name, "MTC Quarter"); sprintf(data, "%u:%u", m[1] >> 4, m[1] & 15u); break;
    case 0xF2: strcpy(name, "Song Position"); sprintf(data, "%u", m[1] | (m[2] << 7)); break;
    case 0xF3: strcpy(name, "Song Select"); sprintf(data, "%u", m[1]); break;
    case 0xF6: strcpy(name, "Tune Request"); break;
    case 0xF7: strcpy(name, "End of SysEx"); break;
    case 0xF8: strcpy(name, "Clock"); break;
    case 0xFA: strcpy(name, "Start"); break;
    case 0xFB: strcpy(name, "Continue"); break;
    case 0xFC: strcpy(name, "Stop"); break;
    case 0xFE: strcpy(name, "Active Sensing"); break;
    case 0xFF: strcpy(name, "Reset"); break;
    default:   strcpy(name, "Undefined"); break;
    }
}

static void hex_bytes(char *s, const UBYTE *m, unsigned int n, unsigned int max)
{
    static const char hx[] = "0123456789ABCDEF";
    unsigned int i;
    for (i = 0; i < n && i < max; i++) {
        if (i) *s++ = ' ';
        *s++ = hx[m[i] >> 4]; *s++ = hx[m[i] & 15u];
    }
    if (n > max) *s++ = '+';
    *s = 0;
}


/* ---- screen, window, layout ------------------------------------------------------------------------------- */

enum { P_BG, P_BLACK, P_WHITE, P_BLUE, P_TROUGH, P_GRN, P_AMB, P_RED, P_GRN_D, P_AMB_D, P_RED_D,
       P_TXT_GRN, P_TXT_RED, P_TXT_BLUE, P_GREY, P_PEAK };
static const struct ColorSpec colours[] = {
    { P_BG, 0xA, 0xA, 0xA }, { P_BLACK, 0, 0, 0 }, { P_WHITE, 0xF, 0xF, 0xF }, { P_BLUE, 0x5, 0x7, 0xA },
    { P_TROUGH, 0x1, 0x1, 0x2 }, { P_GRN, 0x3, 0xE, 0x4 }, { P_AMB, 0xF, 0xC, 0x2 }, { P_RED, 0xF, 0x4, 0x3 },
    { P_GRN_D, 0x1, 0x4, 0x2 }, { P_AMB_D, 0x4, 0x3, 0x1 }, { P_RED_D, 0x4, 0x1, 0x1 },
    { P_TXT_GRN, 0x0, 0x6, 0x1 }, { P_TXT_RED, 0xB, 0x0, 0x0 }, { P_TXT_BLUE, 0x1, 0x2, 0x6 },
    { P_GREY, 0x7, 0x7, 0x7 }, { P_PEAK, 0xF, 0xF, 0xE }, { -1, 0, 0, 0 }
};
/* DrawInfo pens: detail, block, text, shine, shadow, fill, filltext, background, highlight text, bar detail,
 * bar block, bar trim */
static UWORD pens[] = { P_BG, P_BLACK, P_BLACK, P_WHITE, P_BLACK, P_BLUE, P_WHITE, P_BG, P_WHITE, P_BLACK,
                        P_WHITE, P_BLACK, (UWORD)~0 };
static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

static struct Screen   *scr;
static struct Window   *win, *mwin;
static struct RastPort *rp;
static APTR             vi;
static struct TextFont *font;
static struct Gadget   *glist, *mglist;
static struct Menu     *menu;
static struct Requester block_req;
static int              blocked;

enum { G_SCR, G_TEST, G_RT, G_N };                         /* GadTools gadgets of the main window */
static struct Gadget *gad[G_N];
enum { M_TYPE, M_CHAN, M_D1, M_D2, M_BYTES, M_N };        /* of the send panel */
static struct Gadget *mgad[M_N];

/* window coordinates, computed from the window's inner size at open and on every IDCMP_NEWSIZE.  Horizontal: drawn
 * for 640 pixels and scaled to the inner width (win_w): 8-pixel margins, the two groups split the width, their
 * contents at proportional offsets (AX, BX for offsets made for a 308-pixel group), the monitor's row centred.
 * Vertical: the groups' contents are drawn for a group at y 8, 110 high (YG maps those y to the group's place and
 * height); the full layout when its monitor shows 3 rows or more (212 lines inside), else the compact one (down to
 * 172 lines: a 200-line screen).  A group's title is centred on its top edge and stays clear of the border above. */
#define WIN_W     640                                      /* the width the offsets are drawn for */
#define WIN_W_MIN 616                                      /* the narrowest inner width: every control fits */
#define WIN_W_MAX 1280                                     /* the widest: the groups no wider than twice their size */
static int win_w = WIN_W;                                  /* the window's inner width */
static int ox, oy;                                         /* the window's left and top border */
typedef struct {
    int a_y, a_h;                                          /* the loopback test and port activity groups */
    int m_y, m_hd, lv_y, m_by, lv_gap, m_bot;              /* the monitor: head bar, view, button row, margins */
} lay_t;
static const lay_t lay_full    = { 8, 110, 128, 10, 150, 21, 5, 6 };
static const lay_t lay_compact = { 6, 104, 114, 7, 134, 18, 3, 3 };
static const lay_t *ly = &lay_full;
#define LAY_MIN_H(l) ((l)->lv_y + 14 + (l)->lv_gap + (l)->m_by + (l)->m_bot)   /* one monitor row */
#define A_X   (ox + 8)
#define A_Y   (oy + ly->a_y)
#define A_W   ((win_w - 24) / 2)
#define A_H   (ly->a_h)
#define B_X   (ox + 16 + A_W)
#define M_X   (ox + 8)
#define M_Y   (oy + ly->m_y)
#define M_W   (win_w - 16)
#define LV_X  (ox + 16)
#define LV_Y  (oy + ly->lv_y)
#define LV_W  (win_w - 32)
#define AX(x) (A_X + (x) * A_W / 308)
#define BX(x) (B_X + (x) * A_W / 308)
#define YG(y) (A_Y + ((y) - 8) * A_H / 110)
static int m_h, lv_h;                                      /* the monitor group's and the listview's height */
static int st_redraw, pc_redraw;                           /* the result panel, the port counters drawn again */
#define a_cx  (A_X + A_W / 2)
#define b_cx  (B_X + A_W / 2)
static int bar_x;                                          /* x of the meters' first segment */
static const int bar_y0[4] = { 34, 48, 78, 92 };
#define BAR_Y(k) YG(bar_y0[k])
/* the result table of the loopback group, two columns: label x, the right edge of the values, and the width of the
 * value field (the longest label, 8 or 7 characters, and 4 pixels before it) */
static const int col_w[2] = { 72, 76 };
#define COL_L(c) AX((c) ? 164 : 8)
#define COL_R(c) AX((c) ? 300 : 148)
#define ROW_Y(i) YG(71 + (i) * 10)

static void pen_text(struct RastPort *r, int x, int y, int pen, const char *s)
{
    SetAPen(r, pen); SetBPen(r, P_BG); SetDrMd(r, JAM2);
    Move(r, x, y + r->TxBaseline); Text(r, (CONST_STRPTR)s, strlen(s));
}

static int tlen(struct RastPort *r, const char *s) { return TextLength(r, (CONST_STRPTR)s, strlen(s)); }
static void text_c(struct RastPort *r, int cx, int y, int pen, const char *s) { pen_text(r, cx - tlen(r, s) / 2, y, pen, s); }
static void text_r(struct RastPort *r, int rx, int y, int pen, const char *s) { pen_text(r, rx - tlen(r, s), y, pen, s); }

static void bold(struct RastPort *r, int on) { SetSoftStyle(r, on ? FSF_BOLD : FS_NORMAL, AskSoftStyle(r)); }

/* a value right-aligned to rx in a field of w pixels, padded with spaces (topaz 8: 8 pixels a character).  `old`
 * holds the field as last drawn; only the span of characters that differ is drawn, over the old ones.  An `old`
 * of another length (a new field, or a colour change) draws the whole field. */
static void value_r(char *old, int rx, int y, int w, int pen, const char *s)
{
    char f[24];
    int n = w / 8, a, b, l = (int)strlen(s);
    if (n > 23) n = 23;
    if (l > n) l = n;
    memset(f, ' ', (size_t)(n - l)); memcpy(f + n - l, s, (size_t)l); f[n] = 0;
    if ((int)strlen(old) != n || old[0] == 0x7F) { a = 0; b = n; }   /* 0x7F: the field drawn in full */
    else {
        for (a = 0; a < n && f[a] == old[a]; a++) ;
        if (a == n) return;
        for (b = n; b > a && f[b - 1] == old[b - 1]; b--) ;
    }
    SetAPen(rp, pen); SetBPen(rp, P_BG); SetDrMd(rp, JAM2);
    Move(rp, rx - (n - a) * 8, y + rp->TxBaseline); Text(rp, (CONST_STRPTR)f + a, b - a);
    strcpy(old, f);
}

/* a group: a groove frame with its title, bold, centred on the top edge */
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

/* a dark display panel: recessed, filled with the meters' trough colour */
static void panel(struct RastPort *r, int x, int y, int w, int h)
{
    DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(r, P_TROUGH); RectFill(r, x + 2, y + 1, x + w - 3, y + h - 2);
}

static void panel_text(struct RastPort *r, int x, int y, int w, int ty, int pen, const char *s, int b)
{
    SetAPen(r, P_TROUGH); RectFill(r, x + 2, ty, x + w - 3, ty + 7);
    if (b) bold(r, 1);
    SetAPen(r, pen); SetDrMd(r, JAM1);
    Move(r, x + w / 2 - tlen(r, s) / 2, ty + r->TxBaseline); Text(r, (CONST_STRPTR)s, strlen(s));
    if (b) bold(r, 0);
}

/* ---- buttons drawn by the program --------------------------------------------------------------------------
 * Boolean gadgets without Intuition highlighting: the face, bevel and label are drawn here, the label centred
 * with its icon; pressed (Intuition's GFLG_SELECTED while the pointer is on the held button) and latched states
 * are drawn recessed, a latched one in the fill colour; a disabled button keeps its frame and draws its label in
 * grey, and its events are ignored. */

enum { ICON_NONE, ICON_PLAY, ICON_STOP };
typedef struct {
    struct Gadget  g;
    struct Window *w;
    const char    *label;
    int            icon, sel, dis, drawn;
} btn_t;

enum { B_RUN, B_L100, B_L75, B_L50, B_L25, B_SEND, B_CLEAR, B_PAUSE, B_QUIT, B_N };
static btn_t btn[B_N];
enum { MB_SEND, MB_CLOSE, MB_N };
static btn_t mbtn[MB_N];
#define BTN_ID   100                                       /* gadget ID of btn[i]: BTN_ID + i */
#define MBTN_ID  200

static void btn_init(btn_t *b, int id, int x, int y, int w, int h, const char *label, int icon)
{
    memset(b, 0, sizeof *b);
    b->g.LeftEdge = x; b->g.TopEdge = y; b->g.Width = w; b->g.Height = h;
    b->g.Flags = GFLG_GADGHNONE;
    b->g.Activation = GACT_RELVERIFY | GACT_IMMEDIATE;
    b->g.GadgetType = GTYP_BOOLGADGET;
    b->g.GadgetID = id;
    b->label = label; b->icon = icon; b->drawn = -1;
}

static void btn_draw(btn_t *b)
{
    struct RastPort *r = b->w->RPort;
    int x = b->g.LeftEdge, y = b->g.TopEdge, w = b->g.Width, h = b->g.Height;
    int down = (b->g.Flags & GFLG_SELECTED) && !b->dis, st = down | (b->sel << 1) | (b->dis << 2);
    int tw, iw, tx, ty, pen, ip, i;
    if (st == b->drawn) return;
    b->drawn = st;
    SetAPen(r, b->sel ? P_BLUE : P_BG); RectFill(r, x + 2, y + 1, x + w - 3, y + h - 2);
    if (down || b->sel) DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    else DrawBevelBox(r, x, y, w, h, GT_VisualInfo, vi, TAG_DONE);
    pen = b->sel ? (b->dis ? P_BG : P_WHITE) : (b->dis ? P_GREY : P_BLACK);
    tw = tlen(r, b->label);
    iw = b->icon ? 18 : 0;                                 /* a 12-pixel icon and a 6-pixel gap */
    tx = x + (w - iw - tw) / 2 + down;
    ty = y + (h - 8) / 2 + down;
    if (b->icon == ICON_PLAY) {                            /* a triangle, 7 lines high (square pixels: 2:1) */
        ip = b->dis ? P_GREY : P_TXT_GRN;
        SetAPen(r, ip);
        for (i = 0; i < 7; i++) { int ww = 4 * (i < 3 ? i : 6 - i); RectFill(r, tx, ty + i, tx + ww, ty + i); }
    } else if (b->icon == ICON_STOP) {
        ip = b->dis ? P_GREY : P_TXT_RED;
        SetAPen(r, ip); RectFill(r, tx, ty + 1, tx + 10, ty + 5);
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

/* ---- meters ----------------------------------------------------------------------------------------------- */

typedef struct { unsigned long long e; ULONG c[4]; } snap_t;   /* E clock; bytes out, in, TBE, RBF */
static snap_t hist[64];
static unsigned int hn;
static UBYTE seg_on[4][SEGS];
static ULONG peak[4], peak_hold[4], fig_now[4];
static char  fig_old[4][24];

/* counter k per second over the last `ticks` ticks; times 10 (bits) for the two wire meters.  32-bit arithmetic:
 * the E clock and the interval in units of 64 E clock cycles (90 us) */
static ULONG rate(int k, unsigned int ticks)
{
    const snap_t *a, *b;
    ULONG de, r;
    if (hn <= ticks) return 0;
    a = &hist[(hn - 1u) & 63u]; b = &hist[(hn - 1u - ticks) & 63u];
    de = (ULONG)(a->e - b->e) >> 6;
    if (!de) return 0;
    r = (a->c[k] - b->c[k]) * (efreq >> 6) / de;
    return k < 2 ? r * 10u : r;
}

/* v in decimal with a comma every three digits (the readouts run on every update: no sprintf) */
static void fmt_thousands(char *s, ULONG v)
{
    char t[16];
    int n = 0, i, o = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    for (i = n - 1; i >= 0; i--) { s[o++] = t[i]; if (i && i % 3 == 0) s[o++] = ','; }
    s[o] = 0;
}

/* a bar is drawn in runs: the segments of one pen from one RectFill through an area pattern whose every fourth
 * column is clear, so the gaps keep the trough colour.  The pattern is aligned to bitmap x mod 16, and bar_x is a
 * multiple of 4. */
static UWORD seg_pat[2] = { 0xEEEE, 0xEEEE };
#define SEG_GRN 25                                         /* segments 0-24 green (70 %), 25-31 amber, 32-35 red */
#define SEG_AMB 32

static void meter_frame(int k)
{
    int y = BAR_Y(k);
    DrawBevelBox(rp, bar_x - 4, y - 2, SEGS * 4 + 8, 12, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
    SetAPen(rp, P_TROUGH); RectFill(rp, bar_x - 2, y - 1, bar_x + SEGS * 4 + 1, y + 8);
    memset(seg_on[k], 0xFF, SEGS);                         /* every segment drawn on the first update */
    fig_old[k][0] = 0x7F;
}

static int seg_pen(int i, int on)
{
    if (on == 2) return P_PEAK;
    if (i < SEG_GRN) return on ? P_GRN : P_GRN_D;
    if (i < SEG_AMB) return on ? P_AMB : P_AMB_D;
    return on ? P_RED : P_RED_D;
}

static void meter_bar(int k)
{
    ULONG fs = k < 2 ? WIRE_BPS : BYTES_PS;
    ULONG bar = rate(k, 15);
    int lit = (int)((bar * SEGS + fs / 2) / fs), pk, i, j, y = BAR_Y(k), pen;
    UBYTE want[SEGS];
    if (lit > SEGS) lit = SEGS;
    if (bar >= peak[k]) { peak[k] = bar; peak_hold[k] = TICK_HZ; }
    else if (peak_hold[k]) peak_hold[k]--;
    else peak[k] = peak[k] > fs / 90u ? peak[k] - fs / 90u : 0;
    pk = (int)((peak[k] * SEGS + fs / 2) / fs);
    if (pk > SEGS) pk = SEGS;
    for (i = 0; i < SEGS; i++) want[i] = (UBYTE)(i < lit ? 1 : (i == pk - 1 ? 2 : 0));
    if (!memcmp(want, seg_on[k], SEGS)) return;
    SetAfPt(rp, seg_pat, 1); SetDrMd(rp, JAM1);
    for (i = 0; i < SEGS; i = j) {
        if (want[i] == seg_on[k][i]) { j = i + 1; continue; }
        pen = seg_pen(i, want[i]);
        for (j = i + 1; j < SEGS && want[j] != seg_on[k][j] && seg_pen(j, want[j]) == pen; j++) ;
        SetAPen(rp, pen); RectFill(rp, bar_x + i * 4, y, bar_x + j * 4 - 2, y + 7);
        memcpy(&seg_on[k][i], &want[i], (size_t)(j - i));
    }
    SetAfPt(rp, NULL, 0);
}

/* the figure: the 1 s rate */
static void meter_fig(int k)
{
    char s[12];
    fig_now[k] = rate(k, 60);
    fmt_thousands(s, fig_now[k]);
    value_r(fig_old[k], bar_x + SEGS * 4 + 4 + 8 + 48, BAR_Y(k), 48, P_BLACK, s);
}

/* ---- loopback test ---------------------------------------------------------------------------------------- */

enum { T_IDLE, T_RUN, T_DRAIN, T_DONE };
static int    t_state = T_IDLE, t_rate = 3;               /* index of rate_pct */
static const unsigned int rate_pct[4] = { 25, 50, 75, 100 };
static ULONG  t_seq, t_sent, t_exp, t_cand, t_recv, t_lost, t_err, t_ovr0, t_full0;
static ULONG  t_lat_n, t_lat_min, t_lat_max;
static unsigned long long t_lat_sum, t_lat_sq, t_e0, t_e1, t_drain_e;
static ULONG  t_tok, t_rem;                                /* t_tok: bytes x 16 */
static unsigned long long t_last_e;
static int    t_started;

static unsigned int test_msg(ULONG s, UBYTE *m)
{
    if ((s & 31u) == 31u) {
        unsigned int i;
        m[0] = 0xF0; m[1] = 0x7D;
        m[2] = (UBYTE)((s >> 14) & 0x7Fu); m[3] = (UBYTE)((s >> 7) & 0x7Fu); m[4] = (UBYTE)(s & 0x7Fu);
        for (i = 0; i < 8u; i++) m[5 + i] = (UBYTE)((s * 5u + i * 13u) & 0x7Fu);
        m[13] = 0xF7;
        return 14;
    }
    {
        static const UBYTE kind[3] = { 0x90, 0xA0, 0xE0 };
        m[0] = (UBYTE)(kind[s % 3u] | (s & 15u));
        m[1] = (UBYTE)((s >> 4) & 0x7Fu);
        m[2] = (UBYTE)(0x40u | ((s >> 11) & 0x3Fu));
        return 3;
    }
}

/* the sequence number a received message carries, or -1 when it is not a well-formed test message */
static long test_seq(const UBYTE *m, unsigned int n)
{
    static const UBYTE kind[3] = { 0x90, 0xA0, 0xE0 };
    ULONG s;
    unsigned int i;
    if (n == 3) {
        s = (m[0] & 15u) | ((ULONG)m[1] << 4) | ((ULONG)(m[2] & 0x3Fu) << 11);
        if ((s & 31u) == 31u || m[0] != (kind[s % 3u] | (s & 15u)) || !(m[2] & 0x40u)) return -1;
        return (long)s;
    }
    if (n != 14 || m[0] != 0xF0 || m[1] != 0x7D || m[13] != 0xF7) return -1;
    s = ((ULONG)m[2] << 14) | ((ULONG)m[3] << 7) | m[4];
    if (s >= SEQ_MOD || (s & 31u) != 31u) return -1;
    for (i = 0; i < 8u; i++) if (m[5 + i] != (UBYTE)((s * 5u + i * 13u) & 0x7Fu)) return -1;
    return (long)s;
}

static ULONG isqrt64(unsigned long long v)
{
    unsigned long long r = 0, b = 1ull << 62;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (ULONG)r;
}

/* ---- monitor ----------------------------------------------------------------------------------------------
 * A view drawn by the program on a dark panel: the last MON_MAX lines in a ring, IN lines green, OUT lines amber, a
 * line counting messages not shown red.  Those pens and the panel's differ only in bitplanes 0 and 1, so the view
 * is drawn through a write mask of those two.  Measured on this machine: a 72-character Text 5.6 ms, a ScrollRaster
 * of the view 12.2 ms, against 16.7 ms an update; so an update does one of them at most: the view follows the
 * newest lines with a scroll at most twice a second (by every row that arrived since), and draws one row an update
 * until every row shows its line.  A scroller (GadTools) moves through the ring. */

static char         mon_txt[MON_MAX][80];
static UBYTE        mon_pen[MON_MAX];
static int          mon_first, mon_count, mon_top, mon_follow = 1;
#define MON_ROWS_MAX   64
#define MON_COLS_MAX   76                                  /* the view's columns at most (the line buffers) */
static int          mon_drawn_top, mon_row_line[MON_ROWS_MAX];       /* the line each row shows (-1 blank, -2 unknown) */
static ULONG        mon_base, mon_scroll_hn;               /* lines dropped from the ring; the last scroll */
static int          mon_x, mon_y, mon_rows, mon_cols;      /* the text area */
static int          show_test, show_rt, paused;
static ULONG        t_zero;
static int          test_shown_in, test_shown_out, mon_budget;
static ULONG        mon_skipped;
#define MON_PER_UPDATE 3                                   /* lines formatted an update; more are counted */
#define TEST_EVERY     15                                  /* test data: one message each way every 15 updates */
#define MON_PLANES     3                                   /* the write mask of the view */
#define MON_SCROLL_GAP 30                                  /* updates between scrolls */

#define MON_FMT "%8s %-3s  %-20s  %-14s %2s  %s"

/* lines are numbered from the first one since the last clear (mon_base + ring index), so a row's line stays
 * the same line when the ring drops its oldest */
static void mon_push(int pen, const char *t)
{
    int slot, l = (int)strlen(t);
    if (paused) return;
    if (mon_count < MON_MAX) slot = (mon_first + mon_count++) % MON_MAX;
    else {                                                 /* the oldest dropped; a view scrolled back keeps its lines */
        slot = mon_first; mon_first = (mon_first + 1) % MON_MAX; mon_base++;
        if (!mon_follow && mon_top > 0) mon_top--;
    }
    if (l > mon_cols) l = mon_cols;
    memcpy(mon_txt[slot], t, (size_t)l); memset(mon_txt[slot] + l, ' ', sizeof mon_txt[0] - (size_t)l);
    mon_pen[slot] = (UBYTE)pen;
}

static void mon_add(ULONG t, int out, const UBYTE *m, unsigned int n, ULONG total)
{
    char line[96], tm[16], hx[32], name[20], data[40], cs[4];
    ULONG ln = t - t_zero, ms = (ln / 1000u) * b_ns / 1000u + (ln % 1000u) * b_ns / 1000000u;
    int ch;
    if (paused) return;
    sprintf(tm, "%lu.%03lu", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u));
    hex_bytes(hx, m, n, n > 7 ? 6 : 7);
    describe(m, n, total, name, &ch, data);
    if (ch) sprintf(cs, "%d", ch); else strcpy(cs, "-");
    snprintf(line, sizeof line, MON_FMT, tm, out ? "OUT" : "IN", hx, name, cs, data);
    mon_push(out ? P_AMB : P_GRN, line);
}

static void mon_note_skipped(void)
{
    char line[96];
    if (!mon_skipped) return;
    snprintf(line, sizeof line, "%8s %-3s  %lu message%s not shown (more than %d an update)", "", "IN",
             (unsigned long)mon_skipped, mon_skipped == 1 ? "" : "s", MON_PER_UPDATE);
    mon_push(P_RED, line);
    mon_skipped = 0;
}

/* row `row` drawn with line `ln` (absolute number), or blank when that line is not in the ring */
static void mon_row(int row, long ln)
{
    static char blank[80];
    const char *s = blank;
    int pen = P_GRN;
    long i = ln - (long)mon_base;
    if (!blank[0]) memset(blank, ' ', sizeof blank);
    if (i >= 0 && i < mon_count) { int slot = (mon_first + (int)i) % MON_MAX; s = mon_txt[slot]; pen = mon_pen[slot]; }
    SetWrMsk(rp, MON_PLANES);
    SetAPen(rp, pen); SetBPen(rp, P_TROUGH); SetDrMd(rp, JAM2);
    Move(rp, mon_x, mon_y + row * 8 + rp->TxBaseline); Text(rp, (CONST_STRPTR)s, mon_cols);
    SetWrMsk(rp, 0xFF);
    mon_row_line[row] = (i >= 0 && i < mon_count) ? (int)ln : -1;
}

/* one step of bringing the view to its lines: a scroll, or one row; returns 1 when it drew anything */
static int mon_update(void)
{
    long top, r;
    if (mon_follow) mon_top = mon_count > mon_rows ? mon_count - mon_rows : 0;
    top = (long)mon_base + mon_top;
    if (top != mon_drawn_top) {
        long shift = top - mon_drawn_top;
        if (mon_follow && shift > 0 && shift < mon_rows) {
            if (hn - mon_scroll_hn < MON_SCROLL_GAP) goto rows;      /* scrolls at most twice a second */
            SetWrMsk(rp, MON_PLANES); SetBPen(rp, P_TROUGH);
            ScrollRaster(rp, 0, (WORD)(shift * 8), mon_x, mon_y, mon_x + mon_cols * 8 - 1, mon_y + mon_rows * 8 - 1);
            SetWrMsk(rp, 0xFF);
            for (r = 0; r < mon_rows; r++) mon_row_line[r] = r + shift < mon_rows ? mon_row_line[r + shift] : -1;
            mon_drawn_top = (int)top; mon_scroll_hn = hn;
            return 1;
        }
        for (r = 0; r < mon_rows; r++) mon_row_line[r] = -2;
        mon_drawn_top = (int)top;
    }
rows:
    for (r = 0; r < mon_rows; r++) {
        long want = (long)mon_drawn_top + r, i = want - (long)mon_base;
        int shown = (i >= 0 && i < mon_count) ? (int)want : -1;
        if (mon_row_line[r] != shown) { mon_row((int)r, want); return 1; }
    }
    return 0;
}

/* the scroller: the ring's size, the view's top line and height (when they changed) */
static void mon_scroller(void)
{
    static int total_old = -1, top_old = -1;
    if (mon_count == total_old && mon_top == top_old) return;
    total_old = mon_count; top_old = mon_top;
    GT_SetGadgetAttrs(gad[G_SCR], win, NULL, GTSC_Total, (ULONG)mon_count, GTSC_Top, (ULONG)mon_top,
                      GTSC_Visible, (ULONG)mon_rows, TAG_DONE);
}

static void mon_scroll_to(int top)
{
    int last = mon_count > mon_rows ? mon_count - mon_rows : 0;
    if (top > last) top = last;
    if (top < 0) top = 0;
    mon_top = top;
    mon_follow = top == last;
}

static void mon_clear(void)
{
    mon_base += (ULONG)mon_count;
    mon_first = mon_count = mon_top = 0;
    mon_follow = 1;
}

/* ---- the receive side: bytes into messages ----------------------------------------------------------------- */

static UBYTE  p_msg[256];
static ULONG  p_total, p_t;

/* the test's first ELOG_N faults for the CLI summary: kind (M malformed, F far from the expected number, G a gap), the
 * expected number, the number read or the gap, and the last ELOG_B bytes received up to the message's last byte */
#define ELOG_N 16
#define ELOG_B 40
static UBYTE        rx_hist[ELOG_B];
static unsigned int rx_hist_i;
static struct { char k; ULONG exp, v; UBYTE b[ELOG_B]; } elog_e[ELOG_N];
static unsigned int elog_n;
static void elog(char k, ULONG exp, ULONG v)
{
    unsigned int i;
    if (elog_n >= ELOG_N) return;
    elog_e[elog_n].k = k; elog_e[elog_n].exp = exp; elog_e[elog_n].v = v;
    for (i = 0; i < ELOG_B; i++) elog_e[elog_n].b[i] = rx_hist[(rx_hist_i + i) % ELOG_B];
    elog_n++;
}
static unsigned int p_n, p_need, p_sx;
static UBYTE  p_rs;

static void in_message(const UBYTE *m, unsigned int n, ULONG total, ULONG t)
{
    int is_test = 0;
    if (m[0] >= 0xF8u) {
        if (!show_rt) return;
        if (mon_budget > 0) { mon_budget--; mon_add(t, 0, m, n, total); } else mon_skipped++;
        return;
    }
    if (t_state == T_RUN || t_state == T_DRAIN) {
        long s = test_seq(m, n);
        is_test = 1;
        if (s < 0) { elog('M', t_exp, 0); t_err++; }
        else {
            ULONG d = ((ULONG)s - t_exp) & (SEQ_MOD - 1u);
            if (d < STAMPS) {                              /* the expected one, or after a gap of d */
                /* message s was the s-th sent modulo 2^17, so its send time is at s mod 1,024; a stamp of a later
                 * message (1,024 on) is later than t, or later than t by more than a second */
                ULONG ts = tx_st[(ULONG)s & (STAMPS - 1u)];
                if (d) elog('G', t_exp, d);
                t_lost += d; t_recv++;
                t_exp = ((ULONG)s + 1u) & (SEQ_MOD - 1u);
                if (t >= ts && t - ts < b_hz) {                   /* within a second */
                    ULONG lat = t - ts;
                    t_lat_sum += lat; t_lat_sq += lat < 65536u ? lat * lat : 0xFFFFFFFFu; t_lat_n++;
                    if (lat < t_lat_min) t_lat_min = lat;
                    if (lat > t_lat_max) t_lat_max = lat;
                }
            } else {                                       /* far from the expected one: two in a row resync */
                elog('F', t_exp, (ULONG)s);
                t_err++;
                if ((((ULONG)s - t_cand) & (SEQ_MOD - 1u)) == 1u) t_exp = ((ULONG)s + 1u) & (SEQ_MOD - 1u);
                t_cand = (ULONG)s;
            }
        }
    }
    if (is_test) { if (show_test && test_shown_in < 1) { test_shown_in++; mon_add(t, 0, m, n, total); } }
    else if (mon_budget > 0) { mon_budget--; mon_add(t, 0, m, n, total); }
    else mon_skipped++;
}

static void rx_byte(UBYTE b, ULONG t)
{
    rx_hist[rx_hist_i] = b; rx_hist_i = (rx_hist_i + 1u) % ELOG_B;
    if (b >= 0xF8u) { in_message(&b, 1, 1, t); return; }
    if (b & 0x80u) {
        if (p_sx && b != 0xF7u) { in_message(p_msg, p_n, p_total, p_t); p_sx = 0; }   /* cut short */
        if (b == 0xF0u) { p_sx = 1; p_msg[0] = b; p_n = 1; p_total = 1; p_t = t; p_rs = 0; return; }
        if (b == 0xF7u) {
            if (p_sx) {
                if (p_n < sizeof p_msg) p_msg[p_n++] = b;
                p_total++;
                in_message(p_msg, p_n, p_total, p_t); p_sx = 0;
            } else { p_msg[0] = b; in_message(p_msg, 1, 1, t); }
            p_n = 0;
            return;
        }
        p_msg[0] = b; p_n = 1; p_t = t;
        if (b < 0xF0u) { p_rs = b; p_need = ((b & 0xE0u) == 0xC0u) ? 2u : 3u; }
        else {
            p_rs = 0;
            p_need = (b == 0xF2u) ? 3u : (b == 0xF1u || b == 0xF3u) ? 2u : 1u;
            if (p_need == 1u) { in_message(p_msg, 1, 1, t); p_n = 0; }
        }
        return;
    }
    if (p_sx) { if (p_n < sizeof p_msg) p_msg[p_n++] = b; p_total++; return; }
    if (p_n == 0) {                                        /* running status */
        if (!p_rs) return;                                 /* a data byte with no status: dropped */
        p_msg[0] = p_rs; p_n = 1; p_t = t;
    }
    p_msg[p_n++] = b;
    if (p_n == p_need) { in_message(p_msg, p_n, p_n, p_t); p_n = 0; }
}

/* ---- the send panel --------------------------------------------------------------------------------------- */

static const char *const kind_lbl[] = { "Note On", "Note Off", "Poly Pressure", "Control Change", "Program Change",
                                        "Channel Pressure", "Pitch Bend", "System Exclusive", "Raw Bytes", NULL };
static const char *const d1_lbl[] = { "Note", "Note", "Note", "Controller", "Program", "Pressure", "Bend", "", "" };
static const char *const d2_lbl[] = { "Velocity", "Velocity", "Pressure", "Value", "", "", "", "", "" };
static int  s_kind, s_chan = 1, s_d1 = 60, s_d2 = 100, s_bend;
static char s_hex[200] = "7E 7F 06 01";
static UBYTE s_msg[128];
static unsigned int s_n;
static int  m_lx, m_gx, m_row[5], m_pv_y, m_w;
#define M_PV_X 16

/* the message the panel describes; NULL, or why it cannot be sent */
static const char *s_build(void)
{
    static const UBYTE st[7] = { 0x90, 0x80, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 };
    s_n = 0;
    if (s_kind < 7) {
        s_msg[s_n++] = (UBYTE)(st[s_kind] | (s_chan - 1));
        if (s_kind == 6) { int v = s_bend + 8192; s_msg[s_n++] = (UBYTE)(v & 0x7F); s_msg[s_n++] = (UBYTE)(v >> 7); }
        else {
            s_msg[s_n++] = (UBYTE)s_d1;
            if (s_kind < 4) s_msg[s_n++] = (UBYTE)s_d2;
        }
        return NULL;
    } else {
        const char *p = s_hex;
        if (s_kind == 7) s_msg[s_n++] = 0xF0;
        while (*p) {
            unsigned int v = 0, k = 0;
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            while (k < 2 && ((*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f'))) {
                v = v * 16u + (unsigned int)(*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10); p++; k++;
            }
            if (!k || (*p && *p != ' ' && *p != ',')) return "Bytes: two hex digits each, spaces between";
            if (s_n >= sizeof s_msg - 1) return "Bytes: 126 at most";
            if (s_kind == 7 && (v == 0xF0 || v == 0xF7)) continue;   /* the panel adds them */
            if (s_kind == 7 && v > 0x7F) return "SysEx data bytes are 00 to 7F";
            s_msg[s_n++] = (UBYTE)v;
        }
        if (s_kind == 7) s_msg[s_n++] = 0xF7;
        if (s_n == 0) return "Bytes: none given";
        return NULL;
    }
}

/* runs of spaces to one: the monitor's column padding out of a sentence */
static void squeeze(char *s)
{
    char *o = s;
    for (; *s; s++) if (*s != ' ' || o == s || o[-1] != ' ') *o++ = *s;
    *o = 0;
}

/* the panel shows the controls its message type uses: channel messages a slider for the channel and one for each
 * data byte, system exclusive and raw bytes a hex field and a line on how it is read.  The gadgets are made again
 * when the type changes; the window keeps its size. */
static void m_labels(void)
{
    struct RastPort *r = mwin->RPort;
    SetAPen(r, P_BG);
    RectFill(r, mwin->BorderLeft + 2, m_row[1], m_w - mwin->BorderRight - 3, m_row[3] + 12);
    text_r(r, m_lx, m_row[0] + 3, P_BLACK, "Type");
    if (s_kind < 7) {
        text_r(r, m_lx, m_row[1] + 2, P_BLACK, "Channel");
        text_r(r, m_lx, m_row[2] + 2, P_BLACK, d1_lbl[s_kind]);
        if (s_kind < 4) text_r(r, m_lx, m_row[3] + 2, P_BLACK, d2_lbl[s_kind]);
    } else {
        text_r(r, m_lx, m_row[1] + 3, P_BLACK, "Bytes");
        pen_text(r, m_gx, m_row[2] + 6, P_TXT_BLUE,
                 s_kind == 7 ? "Data bytes 00-7F; F0, F7 added" : "Any bytes 00-FF, sent as given");
    }
}

static void m_refresh(void)
{
    struct RastPort *r = mwin->RPort;
    const char *err = s_build();
    char line[64], hx[48], name[20], data[40];
    int ch, w = m_w - 2 * M_PV_X;
    if (err) {
        panel_text(r, M_PV_X, m_pv_y, w, m_pv_y + 4, P_RED, err, 0);
        panel_text(r, M_PV_X, m_pv_y, w, m_pv_y + 14, P_RED, "", 0);
    } else {
        hex_bytes(hx, s_msg, s_n, 12);
        describe(s_msg, s_n, s_n, name, &ch, data);
        if (ch) snprintf(line, sizeof line, "%s, channel %d, %s", name, ch, data);
        else snprintf(line, sizeof line, "%s%s%s", name, data[0] ? ", " : "", data);
        squeeze(line);
        panel_text(r, M_PV_X, m_pv_y, w, m_pv_y + 4, P_AMB, hx, 1);
        panel_text(r, M_PV_X, m_pv_y, w, m_pv_y + 14, P_GRN, line, 0);
    }
    btn_set(&mbtn[MB_SEND], 0, err != NULL || t_state == T_RUN || t_state == T_DRAIN);
}

static void m_hex_read(void)
{
    if (!mgad[M_BYTES]) return;
    strncpy(s_hex, (const char *)((struct StringInfo *)mgad[M_BYTES]->SpecialInfo)->Buffer, sizeof s_hex - 1);
    s_hex[sizeof s_hex - 1] = 0;
}

static void send_now(void)
{
    m_hex_read();
    if (s_build() || t_state == T_RUN || t_state == T_DRAIN) return;
    if (tx_put(s_msg, s_n)) mon_add(beam_now(), 1, s_msg, s_n, s_n);
}

static void m_close(void)
{
    if (!mwin) return;
    CloseWindow(mwin); mwin = NULL;
    FreeGadgets(mglist); mglist = NULL;
    memset(mgad, 0, sizeof mgad);
    if (blocked) { EndRequest(&block_req, win); blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static struct Gadget *m_slider(struct Gadget *g, struct NewGadget *ng, int id, int row, LONG min, LONG max, LONG lv)
{
    ng->ng_TopEdge = m_row[row]; ng->ng_Height = 11; ng->ng_Width = 200; ng->ng_GadgetID = id;
    return mgad[id] = CreateGadget(SLIDER_KIND, g, ng, GTSL_Min, (ULONG)min, GTSL_Max, (ULONG)max, GTSL_Level, (ULONG)lv,
                                   GTSL_LevelFormat, (ULONG)"%5ld", GTSL_MaxLevelLen, 5,
                                   GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
}

static int m_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    memset(mgad, 0, sizeof mgad);
    if (!(g = CreateContext(&mglist))) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi; ng.ng_TextAttr = &topaz8; ng.ng_LeftEdge = m_gx;
    ng.ng_TopEdge = m_row[0]; ng.ng_Width = 200; ng.ng_Height = 14; ng.ng_GadgetID = M_TYPE;
    mgad[M_TYPE] = g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)kind_lbl, GTCY_Active, (ULONG)s_kind,
                                    TAG_DONE);
    if (s_kind < 7) {
        g = m_slider(g, &ng, M_CHAN, 1, 1, 16, s_chan);
        if (s_kind == 6) g = m_slider(g, &ng, M_D1, 2, -8192, 8191, s_bend);
        else g = m_slider(g, &ng, M_D1, 2, 0, 127, s_d1);
        if (s_kind < 4) g = m_slider(g, &ng, M_D2, 3, 0, 127, s_d2);
    } else {
        ng.ng_TopEdge = m_row[1]; ng.ng_Height = 14; ng.ng_Width = 248; ng.ng_GadgetID = M_BYTES;
        mgad[M_BYTES] = g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)s_hex,
                                         GTST_MaxChars, sizeof s_hex - 1, TAG_DONE);
    }
    return g != NULL;
}

/* the gadgets made again for the current type; the drawn buttons follow them in the window's list */
static void m_rebuild(void)
{
    if (mglist) {
        m_hex_read();
        RemoveGList(mwin, mglist, -1);
        FreeGadgets(mglist); mglist = NULL;
    }
    m_labels();
    if (!m_gadgets()) { if (mglist) FreeGadgets(mglist); mglist = NULL; return; }
    AddGList(mwin, mglist, ~0, -1, NULL);
    RefreshGList(mglist, mwin, NULL, -1);
    GT_RefreshWindow(mwin, NULL);
    btn_add(mbtn, MB_N, mwin);
    m_refresh();
}

static int m_open(void)
{
    int bt = scr->WBorTop + scr->Font->ta_YSize + 1, bl = scr->WBorLeft, y, h, bx;
    if (mwin) return 1;
    m_w = 400; m_lx = bl + 100; m_gx = bl + 108;
    y = bt + 10;
    m_row[0] = y; m_row[1] = y + 22; m_row[2] = m_row[1] + 17; m_row[3] = m_row[2] + 17;
    m_pv_y = m_row[3] + 11 + 14;
    y = m_pv_y + 24 + 12;
    bx = m_w / 2;
    btn_init(&mbtn[MB_SEND], MBTN_ID + MB_SEND, bx - 8 - 96, y, 96, 14, "Send", ICON_NONE);
    btn_init(&mbtn[MB_CLOSE], MBTN_ID + MB_CLOSE, bx + 8, y, 96, 14, "Close", ICON_NONE);
    h = y + 14 + 10 + scr->WBorBottom;
    mwin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)((scr->Width - m_w) / 2),
                          WA_Top, (ULONG)((scr->Height - h) / 2), WA_Width, (ULONG)m_w, WA_Height, (ULONG)h,
                          WA_Title, (ULONG)"Send MIDI Message", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                          WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, BUTTONIDCMP | CYCLEIDCMP | SLIDERIDCMP | STRINGIDCMP | IDCMP_CLOSEWINDOW |
                                    IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!mwin) return 0;
    SetFont(mwin->RPort, font);
    panel(mwin->RPort, M_PV_X, m_pv_y, m_w - 2 * M_PV_X, 24);
    m_rebuild();
    InitRequester(&block_req);
    if (Request(&block_req, win)) blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    return 1;
}

/* one event of the panel; 0 when it is to close */
static int m_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_CLOSEWINDOW: return 0;
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(mwin); GT_EndRefresh(mwin, TRUE); break;
    case IDCMP_VANILLAKEY:
        if (code == 0x1B) return 0;                        /* Esc */
        if (code == '\r') { send_now(); break; }
        switch (code | 0x20) {
        case 's': send_now(); break;
        case 'c': return 0;
        case 't':
            s_kind = (s_kind + 1) % 9;
            m_rebuild(); break;
        case 'b': if (mgad[M_BYTES]) ActivateGadget(mgad[M_BYTES], mwin, NULL); break;
        }
        break;
    case IDCMP_GADGETDOWN:
        if (g->GadgetID >= MBTN_ID) btn_draw(&mbtn[g->GadgetID - MBTN_ID]);
        /* fall through: sliders report their level on the press too */
    case IDCMP_GADGETUP: case IDCMP_MOUSEMOVE:
        if (g->GadgetID >= MBTN_ID) {
            btn_t *b = &mbtn[g->GadgetID - MBTN_ID];
            btn_draw(b);
            if (cls != IDCMP_GADGETUP || b->dis) break;
            if (b == &mbtn[MB_SEND]) send_now();
            else return 0;
            break;
        }
        switch (g->GadgetID) {
        case M_TYPE:  s_kind = code; m_rebuild(); break;
        case M_CHAN:  s_chan = (WORD)code; m_refresh(); break;
        case M_D1:    if (s_kind == 6) s_bend = (WORD)code; else s_d1 = (WORD)code; m_refresh(); break;
        case M_D2:    s_d2 = (WORD)code; m_refresh(); break;
        case M_BYTES: m_hex_read(); m_refresh(); break;
        }
        break;
    }
    return 1;
}

/* the 60 Hz update timer (timer.device UNIT_WAITECLOCK), opened in main */
static struct MsgPort     *tport;
static struct timerequest *treq;
static int                 t_open;

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
    if (scr->Height < 226) { ab_h = 198; ab_py = 42; ab_t0 = 15; ab_t1 = 26; }
    wx = ((scr->Width - AB_W) / 2) & ~15; wy = (scr->Height - AB_H) / 2;
    if (awin || mwin) return;
    ab_vbl_n = 0;
    m_sin_init();
    btn_init(&abtn, MBTN_ID + 10, AB_W / 2 - 48, AB_H - (ab_h < 218 ? 21 : 24), 96, 14, "OK", ICON_NONE);
    awin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)wx, WA_Top, (ULONG)wy,
                          WA_Width, AB_W, WA_Height, AB_H, WA_Title, (ULONG)"About MIDI Monitor",
                          WA_DragBar, FALSE, WA_DepthGadget, FALSE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_RMBTrap, TRUE,
                          WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!awin) return;
    SetFont(awin->RPort, font);
    ab_x = wx + AB_PX; ab_y = wy + AB_PY;
    ab_text(ab_t0, P_TXT_BLUE, 1, "MIDI Monitor");
    ab_text(ab_t1, P_BLACK, 0, "MIDI on Paula's serial port, 31,250 baud");
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

/* ---- the main window -------------------------------------------------------------------------------------- */

static const char *const tbl_lbl[2][4] = { { "Sent", "Received", "Lost", "Errors" },
                                           { "Mean", "Minimum", "Maximum", "Jitter" } };
static char tbl_old[2][4][24];

static void tbl_value(int c, int i, int pen, const char *s)
{
    static int pen_old[2][4];
    if (pen_old[c][i] != pen) { tbl_old[c][i][0] = 0; pen_old[c][i] = pen; }
    value_r(tbl_old[c][i], COL_R(c), ROW_Y(i), col_w[c], pen, s);
}

static void ms_text(char *s, ULONG lines)
{
    ULONG us = lines * (b_ns / 4u) / 250u;
    fmt_thousands(s, us / 1000u);
    s += strlen(s);
    s[0] = '.'; s[1] = (char)('0' + (us % 1000u) / 100u); strcpy(s + 2, " ms");
}

/* the result panel, drawn when it changes */
static void test_status(unsigned long long now)
{
    static char head_old[32];
    static int pen_old = -1;
    char s[32];
    ULONG secs = 0, ovr = n_ovr - t_ovr0, full = n_full - t_full0;
    int pen = P_GREY;
    static int state_old = -1;
    static ULONG secs_old;
    if (t_state != T_IDLE) secs = ((ULONG)(((t_state == T_RUN ? now : t_e1) - t_e0) >> 4)) / (efreq >> 4);
    if (st_redraw) { pen_old = state_old = -1; st_redraw = 0; }
    if (t_state == state_old && secs == secs_old) return;  /* the text changes once a second at most */
    state_old = t_state; secs_old = secs;
    switch (t_state) {
    case T_IDLE:  strcpy(s, "READY"); break;
    case T_RUN:   sprintf(s, "RUNNING   %lu:%02lu", (unsigned long)(secs / 60u), (unsigned long)(secs % 60u));
                  pen = P_AMB; break;
    case T_DRAIN: strcpy(s, "FINISHING"); pen = P_AMB; break;
    default: {
        int pass = !t_lost && !t_err && t_recv == t_sent && !ovr && !full;
        sprintf(s, "%s   %lu:%02lu", pass ? "PASS" : "FAIL", (unsigned long)(secs / 60u), (unsigned long)(secs % 60u));
        pen = pass ? P_GRN : P_RED;
    }
    }
    if (pen != pen_old || strcmp(s, head_old)) {
        pen_old = pen; strcpy(head_old, s);
        panel_text(rp, A_X + 11, YG(38), A_W - 21, YG(41), pen, s, 1);
    }
}

/* one value of the table: 0-3 the message counts, 4-7 the round trip */
static void tbl_item(int i)
{
    char s[24];
    ULONG ovr = n_ovr - t_ovr0, full = n_full - t_full0;
    switch (i) {
    case 0: fmt_thousands(s, t_sent); tbl_value(0, 0, P_BLACK, s); return;
    case 1: fmt_thousands(s, t_recv); tbl_value(0, 1, P_BLACK, s); return;
    case 2: fmt_thousands(s, t_lost); tbl_value(0, 2, t_lost ? P_TXT_RED : P_BLACK, s); return;
    case 3: fmt_thousands(s, t_err + ovr + full); tbl_value(0, 3, (t_err || ovr || full) ? P_TXT_RED : P_BLACK, s);
            return;
    }
    if (!t_lat_n) { tbl_value(1, i - 4, P_BLACK, "-"); return; }
    switch (i) {
    case 4: ms_text(s, (ULONG)(t_lat_sum / t_lat_n)); break;
    case 5: ms_text(s, t_lat_min); break;
    case 6: ms_text(s, t_lat_max); break;
    default: {
        unsigned long long mean = t_lat_sum / t_lat_n, var = t_lat_sq / t_lat_n;
        ms_text(s, isqrt64(var > mean * mean ? var - mean * mean : 0));
    }
    }
    tbl_value(1, i - 4, P_BLACK, s);
}

/* the readouts in turn, one an update (each about 4 times a second, the rate of a meter's figures): the four meter
 * figures, the eight table values, the port counters, the monitor's scroller */
#define SLOW_N 14
static void port_counters(void);
static void slow_item(int i)
{
    if (i < 4) meter_fig(i);
    else if (i < 12) tbl_item(i - 4);
    else if (i == 12) port_counters();
    else mon_scroller();
}

static void port_counters(void)
{
    static ULONG o_ovr = ~0ul, o_full = ~0ul;
    char s[64], a[16], b[16];
    if (pc_redraw) { o_ovr = o_full = ~0ul; pc_redraw = 0; }
    if (o_ovr == n_ovr && o_full == n_full) return;
    o_ovr = n_ovr; o_full = n_full;
    fmt_thousands(a, n_ovr); fmt_thousands(b, n_full);
    sprintf(s, "Overruns %s      Buffer full %s", a, b);
    SetAPen(rp, P_BG); RectFill(rp, B_X + 8, YG(105), B_X + A_W - 9, YG(105) + 7);
    text_c(rp, b_cx, YG(105), (n_ovr || n_full) ? P_TXT_RED : P_BLACK, s);
}

static void controls_state(void)
{
    if (!win) return;                                      /* NOWINDOW */
    int run = t_state == T_RUN || t_state == T_DRAIN, i;
    btn[B_RUN].label = t_state == T_RUN ? "Stop" : "Start";
    btn[B_RUN].icon = t_state == T_RUN ? ICON_STOP : ICON_PLAY;
    btn[B_RUN].drawn = -1;
    btn_set(&btn[B_RUN], 0, t_state == T_DRAIN);
    for (i = 0; i < 4; i++) btn_set(&btn[B_L25 - i], t_rate == i, run);
    btn_set(&btn[B_SEND], 0, run);
}

static void test_start(void)
{
    if (t_state == T_RUN || t_state == T_DRAIN) return;
    while (tx_r != tx_w) Delay(1);                         /* manual messages leave first */
    {   /* bytes still on their way from an earlier run or program (the serial bridge and the USB device hold them
         * until a program reads the port and more bytes follow) pushed through by 128 filler bytes; once 32 of them
         * are back and the line is quiet for 100 ms (at most 3 s) what arrived is discarded below */
        UWORD w, r = rx_w; int quiet = 0, n = 0, fe = 0;
        tx_filler(128u);
        while (tx_r != tx_w) Delay(1);
        do {                                               /* the filler back (the bytes before it are in), then quiet */
            Delay(1);
            for (w = rx_w; r != w; r = (r + 1u) & RMASK) if (rxq[r] == 0xFEu) fe++;
            quiet = fe >= 32 && rx_w == w ? quiet + 1 : 0;
        } while (quiet < 5 && ++n < 150);
    }
    Disable();
    rx_r = rx_w; tx_sn = 0;
    Enable();
    p_n = 0; p_sx = 0; p_rs = 0;
    t_seq = t_sent = t_exp = t_recv = t_lost = t_err = t_lat_n = t_lat_max = 0; t_cand = ~0ul; t_lat_min = ~0ul;
    elog_n = 0;
    t_lat_sum = t_lat_sq = 0;
    t_ovr0 = n_ovr; t_full0 = n_full; t_tok = 0; t_rem = 0;
    t_e0 = t_last_e = eclock();
    t_state = T_RUN; t_started = 1;
    controls_state();
}

static void test_stop(void)
{
    if (t_state != T_RUN) return;
    t_e1 = eclock();
    t_drain_e = t_e1 + efreq * 2u;                         /* at most 2 s for the messages on their way */
    t_state = T_DRAIN;
    controls_state();
}

static void test_tick(unsigned long long now)
{
    UBYTE m[16];
    unsigned int n;
    if (t_state == T_DRAIN && tx_r == tx_w) {
        if (t_recv < t_sent && now < t_drain_e) tx_filler(8u); /* the last messages pushed out of the device */
        else {
        t_lost = t_sent > t_recv ? t_sent - t_recv : 0;    /* every message not received intact */
        t_state = T_DONE;
        controls_state();
        }
    }
    if (t_state != T_RUN) return;
    /* the token bucket: bytes x 16, at most 250 ms of bytes in hand, so a late update loses none.  The rate is
     * 500 x percent sixteenths of a byte a second; time in units of 16 E clock cycles, the remainder kept */
    {
        ULONG dt = (ULONG)(now - t_last_e) >> 4, num;
        if (dt > (efreq >> 4)) dt = efreq >> 4;
        num = dt * 500u * rate_pct[t_rate] + t_rem;
        t_tok += num / (efreq >> 4); t_rem = num % (efreq >> 4);
        t_last_e = now;
        if (t_tok > BYTES_PS * 16u / 4u) t_tok = BYTES_PS * 16u / 4u;
    }
    /* this update's messages into one buffer, then into the ring at once (one kick of the transmitter) */
    {
        static UBYTE buf[1024];
        unsigned int o = 0, free = tx_free();
        for (;;) {
            n = test_msg(t_seq, m);
            if (t_tok < n * 16u || o + n > free || o + n > sizeof buf) break;
            memcpy(buf + o, m, n); o += n;
            t_tok -= n * 16u;
            if (show_test && test_shown_out < 1) { test_shown_out++; mon_add(beam_now(), 1, m, n, n); }
            t_seq = (t_seq + 1u) & (SEQ_MOD - 1u); t_sent++;
        }
        if (o) (void)tx_put(buf, o);
    }
}

static void set_rate(int i)
{
    if (t_state == T_RUN || t_state == T_DRAIN) return;
    t_rate = i;
    controls_state();
}

static struct NewMenu nm[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Start or Stop Test", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Send Message...", (STRPTR)"M", 0, 0, (APTR)2 },
    { NM_ITEM, (STRPTR)"Clear Monitor", (STRPTR)"K", 0, 0, (APTR)3 },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)5 },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)4 },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

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

/* the gadgets: group A's controls row centred (Start/Stop 88, 16, "Load %" and 6, four segments of 32); the
 * monitor's row centred (Send Message... 136, Clear 56, Pause 56 with 8 between; 24; two checkboxes of 106 with 12
 * between; 24; Quit 56) */
static int build_main(void)
{
    static const char *const seg[4] = { "25", "50", "75", "100" };
    struct Gadget *g;
    int x, i, by, cw = 26 + 8 + 9 * 8;
    x = a_cx - (88 + 16 + 48 + 6 + 4 * 32) / 2;
    btn_init(&btn[B_RUN], BTN_ID + B_RUN, x, YG(20), 88, 14, "Start", ICON_PLAY);
    for (i = 0; i < 4; i++)
        btn_init(&btn[B_L25 - i], BTN_ID + B_L25 - i, x + 88 + 16 + 48 + 6 + i * 32, YG(20), 32, 14, seg[i], ICON_NONE);
    by = M_Y + m_h - ly->m_by;
    lv_h = by - ly->lv_gap - LV_Y;
    x = ox + win_w / 2 - (136 + 8 + 56 + 8 + 56 + 24 + cw + 12 + cw + 24 + 56) / 2;
    btn_init(&btn[B_SEND], BTN_ID + B_SEND, x, by, 136, 14, "Send Message...", ICON_NONE);
    btn_init(&btn[B_CLEAR], BTN_ID + B_CLEAR, x + 144, by, 56, 14, "Clear", ICON_NONE);
    btn_init(&btn[B_PAUSE], BTN_ID + B_PAUSE, x + 208, by, 56, 14, "Pause", ICON_NONE);
    btn_init(&btn[B_QUIT], BTN_ID + B_QUIT, x + 264 + 24 + cw + 12 + cw + 24, by, 56, 14, "Quit", ICON_NONE);
    if (!(g = CreateContext(&glist))) return 0;
    mon_x = LV_X + 4; mon_y = LV_Y + 3; mon_rows = (lv_h - 6) / 8; mon_cols = (LV_W - 18 - 8) / 8;
    if (mon_rows > MON_ROWS_MAX) mon_rows = MON_ROWS_MAX;
    if (mon_cols > MON_COLS_MAX) mon_cols = MON_COLS_MAX;
    if (mon_rows < 1) return 0;
    {
        struct NewGadget ng;
        memset(&ng, 0, sizeof ng);
        ng.ng_LeftEdge = LV_X + LV_W - 18; ng.ng_TopEdge = LV_Y; ng.ng_Width = 18; ng.ng_Height = lv_h;
        ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = G_SCR; ng.ng_VisualInfo = vi;
        g = gad[G_SCR] = CreateGadget(SCROLLER_KIND, g, &ng, PGA_Freedom, LORIENT_VERT, GTSC_Arrows, 8,
                                      GTSC_Visible, (ULONG)mon_rows, GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
    }
    g = mk(CHECKBOX_KIND, g, G_TEST, x + 264 + 24, by + 1, 26, 11, "Test data", PLACETEXT_RIGHT, GTCB_Checked, 0,
           TAG_IGNORE, 0);
    g = mk(CHECKBOX_KIND, g, G_RT, x + 264 + 24 + cw + 12, by + 1, 26, 11, "Real-time", PLACETEXT_RIGHT,
           GTCB_Checked, 0, TAG_IGNORE, 0);
    return g != NULL;
}

static void draw_main(void)
{
    char hdr[96];
    int i, c, cx;
    st_redraw = pc_redraw = 1;
    group(A_X, A_Y, A_W, A_H, "Loopback Test");
    group(B_X, A_Y, A_W, A_H, "Port Activity");
    group(M_X, M_Y, M_W, m_h, "Monitor");
    /* group A: the load selector's label, the result panel, the two-column table */
    pen_text(rp, btn[B_RUN].g.LeftEdge + 88 + 16, YG(23), P_BLACK, "Load %");
    panel(rp, A_X + 11, YG(38), A_W - 21, 14);
    for (c = 0; c < 2; c++) {
        cx = (COL_L(c) + COL_R(c)) / 2;
        text_c(rp, cx, YG(57), P_TXT_BLUE, c ? "Round trip" : "Messages");
        SetAPen(rp, P_GREY); RectFill(rp, COL_L(c), YG(66), COL_R(c) - 1, YG(66));
        for (i = 0; i < 4; i++) { pen_text(rp, COL_L(c), ROW_Y(i), P_BLACK, tbl_lbl[c][i]); tbl_old[c][i][0] = 0x7F; }
    }
    /* group B: two meter sets */
    bar_x = (b_cx - (24 + 10 + SEGS * 4 + 4 + 8 + 48) / 2 + 24 + 10) & ~3;
    text_c(rp, b_cx, YG(20), P_TXT_BLUE, "Wire rate, bit/s (31,250 max)");
    text_c(rp, b_cx, YG(64), P_TXT_BLUE, "Interrupts per second (3,125 max)");
    text_r(rp, bar_x - 10, BAR_Y(0), P_BLACK, "Out");
    text_r(rp, bar_x - 10, BAR_Y(1), P_BLACK, "In");
    text_r(rp, bar_x - 10, BAR_Y(2), P_BLACK, "TBE");
    text_r(rp, bar_x - 10, BAR_Y(3), P_BLACK, "RBF");
    for (i = 0; i < 4; i++) meter_frame(i);
    /* the monitor: its column heads on a raised bar, on the view's character grid, and the view's panel */
    panel(rp, LV_X, LV_Y, LV_W - 18, lv_h);
    for (i = 0; i < MON_ROWS_MAX; i++) mon_row_line[i] = -1;         /* the panel is blank */
    DrawBevelBox(rp, LV_X, M_Y + ly->m_hd, LV_W, 12, GT_VisualInfo, vi, TAG_DONE);
    sprintf(hdr, MON_FMT, "Time", "Dir", "Bytes", "Message", "Ch", "Data");
    pen_text(rp, LV_X + 4, M_Y + ly->m_hd + 2, P_TXT_BLUE, hdr);
}

/* the main window's gadgets and drawing for its size now: at open and on every IDCMP_NEWSIZE */
static int layout(void)
{
    struct Gadget *g;
    int wh;
    if (glist) {                                           /* the GadTools gadgets and the drawn buttons after them */
        RemoveGList(win, glist, -1);
        for (g = glist; g; g = g->NextGadget) if (g->NextGadget == &btn[0].g) { g->NextGadget = NULL; break; }
        FreeGadgets(glist); glist = NULL;
    }
    ox = win->BorderLeft; oy = win->BorderTop;
    win_w = win->Width - ox - win->BorderRight;
    wh = win->Height - oy - win->BorderBottom;
    ly = wh >= LAY_MIN_H(&lay_full) + 16 ? &lay_full : &lay_compact;
    m_h = oy + wh - M_Y - ly->m_bot;
    SetAPen(rp, P_BG); RectFill(rp, ox, oy, ox + win_w - 1, oy + wh - 1);
    if (!build_main()) return 0;
    AddGList(win, glist, (UWORD)~0, -1, NULL);
    RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    draw_main();
    btn_add(btn, B_N, win);
    controls_state();
    return 1;
}

/* ---- main ------------------------------------------------------------------------------------------------- */


static void cleanup(void)
{
    ab_close();
    m_close();
    if (win) { ClearMenuStrip(win); CloseWindow(win); }
    if (menu) FreeMenus(menu);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (font) CloseFont(font);
    if (scr) CloseScreen(scr);
    serial_close();
    if (t_open) CloseDevice((struct IORequest *)treq);
    if (treq) DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

static void fail(int cli, const char *why)
{
    if (cli) printf("midimon: %s\n", why);
    else if (IntuitionBase) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"midimon", (UBYTE *)"%s", (UBYTE *)"OK" };
        EasyRequest(NULL, &es, NULL, (ULONG)why);
    }
}

static int quit;

/* the screen title carries the measured number of display updates a second (over the last 60) */
static void title_rate(void)
{
    static char t[96];
    const snap_t *a = &hist[(hn - 1u) & 63u], *b = &hist[(hn - 61u) & 63u];
    ULONG r10 = 0;
    if (hn > 61u && a->e > b->e) r10 = (ULONG)(600ull * efreq / (a->e - b->e));
    sprintf(t, "MIDI Monitor     Paula serial port, 31,250 baud, 8N1     %lu.%lu updates/s",
            (unsigned long)(r10 / 10u), (unsigned long)(r10 % 10u));
    if ((int)strlen(t) * 8 > win->Width - 80)                /* the short form when the long one does not fit */
        sprintf(t, "MIDI Monitor   Paula, 31,250 baud, 8N1   %lu.%lu/s", (unsigned long)(r10 / 10u),
                (unsigned long)(r10 % 10u));
    SetWindowTitles(win, (UBYTE *)t, (UBYTE *)t);
}

static void run_toggle(void) { if (t_state == T_RUN) test_stop(); else test_start(); }

static void main_button(int i)
{
    switch (i) {
    case B_RUN:   run_toggle(); break;
    case B_L25: case B_L50: case B_L75: case B_L100: set_rate(B_L25 - i); break;
    case B_SEND:  if (t_state != T_RUN && t_state != T_DRAIN) m_open(); break;
    case B_CLEAR: mon_clear(); break;
    case B_PAUSE: paused ^= 1; btn_set(&btn[B_PAUSE], paused, 0); break;
    case B_QUIT:  quit = 1; break;
    }
}

/* the test's result, for a CLI */
static void summary(void)
{
    char a[16], b[16], c[16], d[16];
    int pass = t_state == T_DONE && !t_lost && !t_err && t_recv == t_sent && n_ovr == t_ovr0 && n_full == t_full0;
    unsigned long long e1 = t_state == T_RUN ? eclock() : t_e1;
    ULONG ms = (ULONG)((e1 - t_e0) * 1000ull / efreq);
    printf("loopback %s: %lu ms at %u %%, sent %lu, received %lu, lost %lu, errors %lu, overruns %lu, "
           "buffer full %lu\n", pass ? "PASS" : "FAIL", (unsigned long)ms, rate_pct[t_rate],
           (unsigned long)t_sent, (unsigned long)t_recv, (unsigned long)t_lost, (unsigned long)t_err,
           (unsigned long)(n_ovr - t_ovr0), (unsigned long)(n_full - t_full0));
    if (t_lat_n) {
        unsigned long long mean = t_lat_sum / t_lat_n, var = t_lat_sq / t_lat_n;
        var = var > mean * mean ? var - mean * mean : 0;
        ms_text(a, (ULONG)mean); ms_text(b, t_lat_min); ms_text(c, t_lat_max); ms_text(d, isqrt64(var));
        printf("round trip: mean %s, min %s, max %s, jitter %s (%lu messages)\n", a, b, c, d,
               (unsigned long)t_lat_n);
    }
    printf("bytes out %lu, in %lu; TBE interrupts %lu, RBF interrupts %lu\n", (unsigned long)n_tx,
           (unsigned long)n_rx, (unsigned long)n_tbe, (unsigned long)n_rbf);
    printf("meters at the end (1 s): out %lu bit/s, in %lu bit/s, TBE %lu/s, RBF %lu/s\n",
           (unsigned long)fig_now[0], (unsigned long)fig_now[1], (unsigned long)fig_now[2],
           (unsigned long)fig_now[3]);
    {
        unsigned int i, j;
        for (i = 0; i < elog_n; i++) {
            printf("fault %u: %c expected %lu, %s %lu; last bytes:", i + 1u, elog_e[i].k, (unsigned long)elog_e[i].exp,
                   elog_e[i].k == 'G' ? "gap" : "read", (unsigned long)elog_e[i].v);
            for (j = 0; j < ELOG_B; j++) printf(" %02x", elog_e[i].b[j]);
            printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    static LONG opt[4];
    struct RDArgs *ra = NULL;
    struct Screen *wb;
    ULONG mode, sw, sh, sigs, autoquit = 0;
    unsigned long long next, period, start_e = 0;
    const char *why;
    int i, cli = argc > 0;
    (void)argv;

    if (cli) {
        if (!(ra = ReadArgs((CONST_STRPTR)"START/S,RATE/N,SECONDS/N,NOWINDOW/S", opt, NULL))) {
            printf("usage: midimon [START] [RATE=25|50|75|100] [SECONDS=N] [NOWINDOW]\n"); return 10;
        }
        if (opt[1]) for (i = 0; i < 4; i++) if ((unsigned int)*(LONG *)opt[1] == rate_pct[i]) t_rate = i;
        if (opt[2]) autoquit = (ULONG)*(LONG *)opt[2];
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 39);
    if (!IntuitionBase || !GfxBase || !GadToolsBase) { fail(cli, "needs AmigaOS 3.0 or later"); cleanup(); return 20; }
    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof *treq) : NULL;
    if (!treq || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_WAITECLOCK, (struct IORequest *)treq, 0)) {
        fail(cli, "timer.device did not open"); cleanup(); return 20;
    }
    t_open = 1; TimerBase = treq->tr_node.io_Device;
    { struct EClockVal v; efreq = ReadEClock(&v); }
    if ((why = serial_open()) != NULL) { fail(cli, why); cleanup(); return 20; }
    if (opt[3]) {                                          /* NOWINDOW: the test alone, no screen, nothing drawn */
        unsigned long long next, period = efreq / TICK_HZ, now;
        if (!autoquit) autoquit = 10;
        test_start(); start_e = t_e0;
        next = eclock() + period;
        treq->tr_node.io_Command = TR_ADDREQUEST;
        while (t_state != T_DONE && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C)) {
            treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
            DoIO((struct IORequest *)treq);
            now = eclock();
            test_tick(now);
            {
                UWORD r = rx_r, w = rx_w;
                for (; r != w; r = (r + 1u) & RMASK) rx_byte(rxq[r], rxt[r]);
                rx_r = r;
            }
            if (t_state == T_RUN && now - start_e >= (unsigned long long)autoquit * efreq) test_stop();
            next += period;
            if (next < now) next = now + period;
        }
        if (cli) summary();
        cleanup();
        if (ra) FreeArgs(ra);
        return 0;
    }


    if (!(wb = LockPubScreen((CONST_STRPTR)"Workbench"))) { fail(cli, "no Workbench screen"); cleanup(); return 20; }
    mode = GetVPModeID(&wb->ViewPort); sw = wb->Width; sh = wb->Height;
    if (sw < WIN_W) {                                      /* a lores Workbench: hires on the same monitor, 640 wide */
        ULONG hm = (mode & MONITOR_ID_MASK) | HIRES_KEY | (mode & LORESLACE_KEY);
        if (!ModeNotAvailable(hm)) { mode = hm; sw = WIN_W; }
    }
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, (ULONG)sw, SA_Height, (ULONG)sh,
                         SA_Depth, 4, SA_Font, (ULONG)&topaz8, SA_Pens, (ULONG)pens, SA_Colors, (ULONG)colours,
                         SA_Title, (ULONG)"MIDI Monitor     Paula serial port, 31,250 baud, 8N1", SA_ShowTitle, TRUE,
                         SA_Type, CUSTOMSCREEN, SA_Interleaved, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, wb);
    if (!scr) { fail(cli, "the screen did not open"); cleanup(); return 20; }
    font = OpenFont(&topaz8);
    vi = GetVisualInfo(scr, TAG_DONE);
    if (!vi || !font) { fail(cli, "the gadgets could not be made"); cleanup(); return 20; }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, (ULONG)scr->Width, WA_Height, (ULONG)scr->Height, WA_Title, (ULONG)"MIDI Monitor",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_SizeBRight, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, BUTTONIDCMP | CHECKBOXIDCMP | SCROLLERIDCMP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY |
                                   IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW, TAG_DONE);
    if (!win) { fail(cli, "the window did not open"); cleanup(); return 20; }
    rp = win->RPort; SetFont(rp, font);
    {   /* the smallest window shows every control (the compact layout, one monitor row); the largest is the screen,
         * at most WIN_W_MAX wide inside */
        int bw = win->BorderLeft + win->BorderRight, bh = win->BorderTop + win->BorderBottom;
        int maxw = scr->Width < WIN_W_MAX + bw ? scr->Width : WIN_W_MAX + bw;
        if (scr->Width < WIN_W_MIN + bw || scr->Height < LAY_MIN_H(&lay_compact) + bh) {
            fail(cli, "the screen is too small (640 x 200 is the least)"); cleanup(); return 20;
        }
        WindowLimits(win, (WORD)(WIN_W_MIN + bw), (WORD)(LAY_MIN_H(&lay_compact) + bh), (UWORD)maxw, (UWORD)scr->Height);
        if (win->Width > maxw) ChangeWindowBox(win, 0, 0, (WORD)maxw, win->Height);
    }
    if ((menu = CreateMenus(nm, TAG_DONE)) != NULL && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE))
        SetMenuStrip(win, menu);
    if (!layout()) { fail(cli, "the gadgets could not be made"); cleanup(); return 20; }
    t_zero = beam_now();

    period = efreq / TICK_HZ;
    next = eclock() + period;
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
    SendIO((struct IORequest *)treq);
    if (opt[0]) { test_start(); start_e = t_e0; }

    while (!quit) {
        struct IntuiMessage *im;
        ULONG msig = mwin ? 1ul << mwin->UserPort->mp_SigBit : 0;
        if (awin) msig |= (1ul << awin->UserPort->mp_SigBit) | ab_sigmask | (1ul << ab_tport->mp_SigBit);
        sigs = Wait((1ul << win->UserPort->mp_SigBit) | msig | (1ul << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (awin && ab_tbusy && (sigs & (1ul << ab_tport->mp_SigBit)) && GetMsg(ab_tport)) { ab_tbusy = 0; ab_chain(); }
        if (awin && (sigs & ab_sigmask)) ab_frame();
        if (sigs & SIGBREAKF_CTRL_C) quit = 1;
        if ((sigs & (1ul << tport->mp_SigBit)) && GetMsg(tport)) {
            unsigned long long now = eclock();
            /* the tick: the test's transmitter, the received bytes, the meters, the table, the buttons */
            /* the monitor's budget: test data on one update in three, other lines three an update; a count of
             * the lines not shown goes in as a line of its own */
            test_shown_in = test_shown_out = hn % TEST_EVERY ? 1 : 0;
            mon_budget = MON_PER_UPDATE;
            test_tick(now);
            {
                UWORD r = rx_r, w = rx_w;
                for (; r != w; r = (r + 1u) & RMASK) rx_byte(rxq[r], rxt[r]);
                rx_r = r;
            }
            mon_note_skipped();
            {
                snap_t *s = &hist[hn & 63u];
                s->e = now; s->c[0] = n_tx; s->c[1] = n_rx; s->c[2] = n_tbe; s->c[3] = n_rbf; hn++;
            }
            /* the Out and In bars every update, the TBE and RBF bars on alternate updates; the result panel every
             * update; the readouts one an update in turn */
            if (!awin) {                                   /* behind the About box nothing is drawn */
                meter_bar(0); meter_bar(1); meter_bar(2 + (int)(hn & 1u));
                test_status(now);
                if (!mon_update()) slow_item((int)(hn % SLOW_N));   /* the view or a readout: one an update */
            }
            if (hn % (3u * TICK_HZ) == 0) title_rate();
            if (!awin) for (i = 0; i < B_N; i++) btn_draw(&btn[i]);
            if (mwin) for (i = 0; i < MB_N; i++) btn_draw(&mbtn[i]);
            if (awin) btn_draw(&abtn);
            if (autoquit && t_state == T_RUN && now - start_e >= (unsigned long long)autoquit * efreq) test_stop();
            if (autoquit && t_state == T_DONE) quit = 1;
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
            if (!m_event(cls, code, g)) { m_close(); break; }
        }
        while ((im = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); continue; }
            if (cls == IDCMP_NEWSIZE && !mwin && !awin) { if (!layout()) quit = 1; continue; }
            if (cls == IDCMP_CLOSEWINDOW && !mwin && !awin) { quit = 1; continue; }
            if (mwin || awin) continue;                    /* the panel and the About box are modal */
            switch (cls) {
            case IDCMP_MENUPICK:
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: run_toggle(); break;
                    case 2: main_button(B_SEND); break;
                    case 3: mon_clear(); break;
                    case 5: ab_open(); break;
                    case 4: quit = 1; break;
                    }
                    code = it->NextSelect;
                }
                break;
            case IDCMP_VANILLAKEY:
                switch (code) {
                case ' ': run_toggle(); break;
                case '1': case '2': case '3': case '4': set_rate(code - '1'); break;
                case 0x1B: quit = 1; break;
                default:
                    switch (code | 0x20) {
                    case 's': run_toggle(); break;
                    case 'm': main_button(B_SEND); break;
                    case 'c': mon_clear(); break;
                    case 'p': main_button(B_PAUSE); break;
                    case 't': show_test ^= 1;
                              GT_SetGadgetAttrs(gad[G_TEST], win, NULL, GTCB_Checked, (ULONG)show_test, TAG_DONE); break;
                    case 'r': show_rt ^= 1;
                              GT_SetGadgetAttrs(gad[G_RT], win, NULL, GTCB_Checked, (ULONG)show_rt, TAG_DONE); break;
                    case 'q': quit = 1; break;
                    case 'a': ab_open(); break;
                    }
                }
                break;
            case IDCMP_GADGETDOWN: case IDCMP_MOUSEMOVE:
                if (g->GadgetID >= BTN_ID && g->GadgetID < BTN_ID + B_N) btn_draw(&btn[g->GadgetID - BTN_ID]);
                else if (g->GadgetID == G_SCR) mon_scroll_to(code);
                break;
            case IDCMP_GADGETUP:
                if (g->GadgetID >= BTN_ID && g->GadgetID < BTN_ID + B_N) {
                    btn_t *b = &btn[g->GadgetID - BTN_ID];
                    btn_draw(b);
                    if (!b->dis) main_button(g->GadgetID - BTN_ID);
                    break;
                }
                switch (g->GadgetID) {
                case G_SCR:  mon_scroll_to(code); break;
                case G_TEST: show_test = (g->Flags & GFLG_SELECTED) != 0; break;
                case G_RT:   show_rt = (g->Flags & GFLG_SELECTED) != 0; break;
                }
                break;
            }
        }
    }
    AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq);
    if (cli && t_started) summary();
    cleanup();
    if (ra) FreeArgs(ra);
    return 0;
}
