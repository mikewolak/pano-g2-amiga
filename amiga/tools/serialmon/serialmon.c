/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* serialmon.c -- the serial port through an Exec serial driver (serial.device, BaudBandit.device or any other
 * device with the serial.device command set): a loopback test, port activity meters, the modem lines, a monitor of
 * both directions with a send line, and a driver selector that closes one driver and opens another while the program
 * runs, in one window on a screen of its own.
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
 *
 *   serialmon [DEVICE=name] [UNIT=n] [BAUD=n] [FORMAT=8N1|8N2|8E1|8O1|7E1|7O1|7N1] [FLOW=RTSCTS|XONXOFF|NONE]
 *             [RATE=PERCENT] [SECONDS=N] [BUFFER=bytes] [START] [NOWINDOW] [KEEP] [BOOGIE]
 *     DEVICE      the driver opened first (default serial.device); a name not in the list is added to it
 *     UNIT        its unit (default 0)
 *     BAUD        the rate (default 115,200); FORMAT data bits, parity, stop bits (default 8N1); FLOW the handshake
 *                 (default NONE)
 *     RATE        the test's load: 25, 50, 75 or 100 percent of the wire rate (default 100)
 *     SECONDS     the test's length; with START the program quits at the end and prints its result
 *     BUFFER      the driver's read buffer, bytes (io_RBufLen, default 16,384)
 *     START       the loopback test runs from the start
 *     NOWINDOW    the test alone from the CLI, no screen (SECONDS default 10); a PASS or FAIL line and the figures,
 *                 return code 0 on PASS, 5 on FAIL, 20 when the driver or the program cannot start
 *     KEEP        a closed driver is left in the device list (default: removed with RemDevice when nothing has it
 *                 open, which frees the serial port's misc.resource bits for the next driver)
 *     BOOGIE      SERF_RAD_BOOGIE (the driver's high-speed mode) in every SDCMD_SETPARAMS
 *
 *   Keys: Space or S starts and stops the test, 1 to 4 set its load, H switches the monitor and the send line
 *   between ASCII and hex, Return or L goes to the send line, B sends a break, C clears the monitor, P pauses it,
 *   T shows test data in it, D opens the driver panel, F the file transfer panel, A the About box, Q or Esc quits.
 *   Transfer panel: D direction, P protocol, E line end (raw text), F the file field, Return starts, Esc cancels a
 *   running transfer or closes the panel, C closes it.
 *   V (or the Terminal button) opens the terminal (amiga/tools/term): a window on this screen or full screen (Right
 *   Amiga F), Right Amiga U opens the transfer panel to send, Right Amiga Q closes it.  While it is open a ZMODEM
 *   sender's hex ZRQINIT header with a valid CRC starts a ZMODEM receive into the download folder (Settings: "ZMODEM
 *   Download Folder...", empty for the current directory, the default; "Receive ZMODEM Automatically", on by
 *   default; "Terminal Characters UTF-8", off: 8-bit ISO 8859-1 as a VT220; kept as the global variables
 *   serialmon/ZFolder, serialmon/ZAuto and serialmon/TermUTF8 in ENV: and ENVARC:).
 *
 * 1 Driver.  All port access goes through the Exec device interface of the selected driver, unit 0 by default:
 *   OpenDevice with SERF_7WIRE set for RTS/CTS, SDCMD_SETPARAMS (baud, read and write length, stop bits, parity,
 *   xON/xOFF, RTS/CTS, read buffer size), CMD_READ and CMD_WRITE sent with SendIO and collected with CheckIO and
 *   WaitIO, SDCMD_QUERY for the status word (the modem lines) and the bytes waiting in the read buffer, SDCMD_BREAK,
 *   CMD_CLEAR and CMD_FLUSH.  One 1-byte CMD_READ is kept pending; when it returns, SDCMD_QUERY gives the bytes
 *   waiting and one CMD_READ of that many takes them.  Two CMD_WRITE requests alternate, so the driver always has
 *   the next block queued; a block holds at most one update's worth of bytes at the wire rate.  A driver switch, or
 *   a change of a setting, first ends every pending request (CMD_FLUSH, AbortIO, at most 2 s for each to return),
 *   then closes the driver, removes it from the device list when nothing else has it open (as the misc.resource
 *   holder of the serial port it would otherwise keep the port), and opens the next one.
 *
 * 2 Time.  A clock of scan-line steps: the beam's line from VPOSR/VHPOSR read as one longword, plus the frame's
 *   lines for each vertical blank the program's server (first in the chain) has counted; a reading near the top of a
 *   frame whose vertical blank is still pending counts that frame already.  The frame's lines are measured at the
 *   start (the highest line of one frame, plus one): 313 on PAL (64 us a line, 15,625 a second), 263 on NTSC
 *   (63.556 us, 15,734 a second).
 *
 * 3 Loopback test.  The transmit line joined to the receive line.  The test data are midimon's: message s (s
 *   modulo 2^17) is F0 7D s[20:14] s[13:7] s[6:0], eight data bytes (5s + 13i) mod 128, F7 when s mod 32 = 31,
 *   else a 3-byte message: status 90h, A0h or E0h (s mod 3) with channel s[3:0], first data byte s[10:4], second
 *   40h | s[16:11].  The status bytes carry bit 7 and the data bytes include 11h and 13h, so the test runs with 8
 *   data bits and without xON/xOFF.  Every message has its sequence number; the receiver counts a gap of fewer than
 *   1,024 as lost messages and a malformed or out-of-order message as an error; two in a row from elsewhere in the
 *   sequence set it there.  At the end, lost is every message sent and not received intact.  A test starts with 128
 *   filler bytes (FEh), everything received until 32 of them are back and the line is quiet for 100 ms (at most 3 s)
 *   discarded, and ends when every message is back or 2 s after the transmitter has emptied (10 s after the stop at
 *   most, for a driver that does not send), with filler bytes sent meanwhile.  The transmitter's rate is set by a token bucket from the E clock: the load times the wire's byte
 *   rate, the wire rate being the lower of the baud rate and the rate Paula's SERPER gives for it.  Round trip: from
 *   the moment a message's status byte starts on the wire (the start of its CMD_WRITE, or the end of the block
 *   queued before it, plus its offset in the block at the wire rate: an estimate) to the moment the program's
 *   CMD_READ returned it (one scan line resolution).
 *
 * 4 Display.  60 updates a second from timer.device's E clock (UNIT_WAITECLOCK); the PAL display shows 50 frames
 *   a second.  Meters: bars of a 250 ms window, figures of a 1 s window, peak held 1 s; bit/s is bytes times the
 *   frame's bits (start, data, parity, stop), full scale the baud rate.  The screen copies the Workbench's display
 *   mode with 16 colours and topaz 8; buttons are drawn by the program (section "buttons"). */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <devices/timer.h>
#include <devices/serial.h>
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
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/timer.h>
#include <proto/asl.h>
#include <libraries/asl.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include "../xfer/zio_status.h"
#include "../term/term.h"
#include "font8x8.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase       *GfxBase;
struct Library       *GadToolsBase;
struct Device        *TimerBase;

#define HWREG    ((volatile struct Custom *)0xDFF000)
#define VPOSL     (*(volatile ULONG *)0xDFF004)            /* VPOSR:VHPOSR, line in bits 16:8 */
static ULONG b_lines = 313u, b_hz = 15625u, b_ns = 64000u;   /* the frame's lines, lines a second, ns a line */

#define RING      8192u
#define RMASK     (RING - 1u)
#define STAMPS    1024u
#define SEQ_MOD   (1ul << 17)
#define TICK_HZ   60u
#define SEGS      36                                      /* meter segments: 4 pixels, 3 lit and a gap */
#define MON_MAX   200
#define RDBUF     2048u                                    /* one bulk CMD_READ */
#define WRBUF     2048u                                    /* one CMD_WRITE block */

/* ---- section 2: the clock ------------------------------------------------------------------------------------ */

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

/* ---- port settings ---------------------------------------------------------------------------------------- */

#define DRV_MAX   8
static char        drv_name[DRV_MAX][48] = { "serial.device", "BaudBandit.device" };
static LONG        drv_unit[DRV_MAX];
static STRPTR      drv_lbl[DRV_MAX + 1] = { (STRPTR)drv_name[0], (STRPTR)drv_name[1], NULL };
static int         drv_n = 2, s_drv;

#define BAUD_N    12
static ULONG       baud_tab[BAUD_N + 1] = { 110, 300, 1200, 2400, 4800, 9600, 19200, 31250, 38400, 57600, 115200,
                                            230400 };
static char        baud_txt[BAUD_N + 1][8];
static STRPTR      baud_lbl[BAUD_N + 2];
static int         baud_n = BAUD_N, s_bi = 10;             /* 115,200 */

static const char *const fmt_lbl[] = { "8N1", "8N2", "8E1", "8O1", "7E1", "7O1", "7N1", NULL };
static const UBYTE fmt_bits[7] = { 8, 8, 8, 8, 7, 7, 7 }, fmt_par[7] = { 0, 0, 1, 2, 1, 2, 0 },
                   fmt_stop[7] = { 1, 2, 1, 1, 1, 1, 1 };  /* parity 0 none, 1 even, 2 odd */
static int         s_fmt;

enum { FLOW_RTS, FLOW_XON, FLOW_NONE };
static const char *const flow_lbl[] = { "RTS/CTS", "xON/xOFF", "None", NULL };
static int         s_flow = FLOW_NONE;

static ULONG       s_rbuf = 16384, s_baud = 115200;
static int         s_expunge = 1, s_boogie, s_hex;

/* derived from the settings: the frame's bits, Paula's divider and rate for the baud rate (rounded to the nearest
 * the colour clock allows), the wire's byte rate and a byte's time in scan lines x 256 (both at the lower of the
 * baud rate and Paula's rate), one write block (one update's bytes, 16 to WRBUF) */
static ULONG       frame_bits = 10, wire_bps = 11520, bt256 = 347, wr_chunk = 192, paula_per, paula_baud;

static void derive(void)
{
    ULONG clock = (GfxBase->DisplayFlags & PAL) ? 3546895ul : 3579545ul, w;
    s_baud = baud_tab[s_bi];
    frame_bits = 1u + fmt_bits[s_fmt] + (fmt_par[s_fmt] ? 1u : 0u) + fmt_stop[s_fmt];
    paula_per = (clock + s_baud / 2u) / s_baud - 1u;
    if (paula_per > 32767u) paula_per = 32767u;
    paula_baud = clock / (paula_per + 1u);
    w = paula_baud < s_baud ? paula_baud : s_baud;
    wire_bps = w / frame_bits;
    if (!wire_bps) wire_bps = 1;
    bt256 = (frame_bits * b_hz * 256u + w / 2u) / w;
    wr_chunk = wire_bps / TICK_HZ + 1u;
    if (wr_chunk < 16u) wr_chunk = 16u;
    if (wr_chunk > WRBUF) wr_chunk = WRBUF;
}

/* ---- section 1: the driver ---------------------------------------------------------------------------------- */

static struct MsgPort  *sport;
static struct IOExtSer *srd, *swr[2], *sctl, *sbrk;
static int     s_open, s_open_flow, rd_busy, rd_hold, rd_wakes, wr_busy[2], wr_next, brk_busy;
static UBYTE   rd_byte, rd_buf[RDBUF], wr_buf[2][WRBUF];
static UWORD   s_status;                                   /* SDCMD_QUERY's io_Status */
static ULONG   s_inbuf;                                    /* and its io_Actual: bytes in the read buffer */
static ULONG   s_open_us, s_close_us;                      /* the last OpenDevice and close, microseconds */
static BYTE    s_param_err;                                /* the last SDCMD_SETPARAMS's io_Error */
static ULONG   n_tx, n_rx, n_ovr, n_full, n_par, n_brk, n_ioerr;
static BYTE    last_ioerr;

static volatile UBYTE  rxq[RING];
static volatile ULONG  rxt[RING];
static volatile UWORD  rx_w, rx_r;
static volatile UBYTE  txq[RING];
static volatile UWORD  tx_w, tx_r;
static volatile ULONG  tx_st[STAMPS], tx_sn;               /* the wire times of status bytes, in order */
static ULONG           tx_free_at;                         /* the line at which the queued blocks end */

static void notice(int err, const char *fmt, ...);         /* a line in the monitor (section "monitor") */

static const char *ser_errname(BYTE e)
{
    switch (e) {
    case IOERR_OPENFAIL:       return "open failed";
    case IOERR_ABORTED:        return "aborted";
    case IOERR_NOCMD:          return "command not supported";
    case IOERR_BADLENGTH:      return "bad length";
    case IOERR_BADADDRESS:     return "bad address";
    case IOERR_UNITBUSY:       return "unit busy";
    case IOERR_SELFTEST:       return "self test failed";
    case SerErr_DevBusy:       return "device busy";
    case SerErr_BaudMismatch:  return "baud rate not supported";
    case SerErr_BufErr:        return "no memory for the read buffer";
    case SerErr_InvParam:      return "invalid parameter";
    case SerErr_LineErr:       return "overrun";
    case SerErr_ParityErr:     return "parity error";
    case SerErr_TimerErr:      return "timer error";
    case SerErr_BufOverflow:   return "read buffer overflow";
    case SerErr_NoDSR:         return "no DSR";
    case SerErr_DetectedBreak: return "break received";
    default:                   return "unknown error";
    }
}

static ULONG us_since(unsigned long long e0) { return (ULONG)((eclock() - e0) * 1000000ull / efreq); }

/* an error a read or write returned, counted by kind */
static void ser_err(BYTE e)
{
    switch (e) {
    case SerErr_LineErr:       n_ovr++; break;
    case SerErr_BufOverflow:   n_full++; break;
    case SerErr_ParityErr:     n_par++; break;
    case SerErr_DetectedBreak: n_brk++; break;
    case IOERR_ABORTED:        break;
    default:                   n_ioerr++; last_ioerr = e; break;
    }
}

static void rx_store(const UBYTE *b, ULONG n, ULONG t)
{
    ULONG i;
    for (i = 0; i < n; i++) {
        UWORD nx = (UWORD)((rx_w + 1u) & RMASK);
        if (nx == rx_r) { n_full++; continue; }
        rxt[rx_w] = t; rxq[rx_w] = b[i];
        rx_w = nx;
        n_rx++;
    }
}

static unsigned int rx_free(void) { return (rx_r - rx_w - 1u) & RMASK; }

static int ser_query(void)
{
    sctl->IOSer.io_Command = SDCMD_QUERY;
    if (DoIO((struct IORequest *)sctl)) return 0;
    s_status = sctl->io_Status; s_inbuf = sctl->IOSer.io_Actual;
    return 1;
}

static void rd_start(void)
{
    srd->IOSer.io_Command = CMD_READ; srd->IOSer.io_Data = &rd_byte; srd->IOSer.io_Length = 1;
    SendIO((struct IORequest *)srd); rd_busy = 1; rd_hold = 0;
}

/* the next blocks of the transmit ring to the driver, while a write request is free.  The wire time of each test
 * status byte (as midimon's TBE handler stamped them) is estimated here: the block starts now, or where the block
 * still queued before it ends, and each byte takes one frame at the baud rate. */
static void tx_pump(void)
{
    while (s_open && !wr_busy[wr_next] && tx_r != tx_w) {
        struct IOExtSer *w = swr[wr_next];
        UBYTE *d = wr_buf[wr_next];
        ULONG n = 0, now = beam_now(), base = (wr_busy[wr_next ^ 1] && tx_free_at > now) ? tx_free_at : now;
        while (tx_r != tx_w && n < wr_chunk) {
            UBYTE b = txq[tx_r];
            tx_r = (UWORD)((tx_r + 1u) & RMASK);
            if ((b & 0x80u) && b != 0xF7u && b < 0xF8u) { tx_st[tx_sn & (STAMPS - 1u)] = base + ((n * bt256) >> 8); tx_sn++; }
            d[n++] = b;
        }
        tx_free_at = base + ((n * bt256) >> 8);
        w->IOSer.io_Command = CMD_WRITE; w->IOSer.io_Data = d; w->IOSer.io_Length = n;
        SendIO((struct IORequest *)w);
        wr_busy[wr_next] = 1; wr_next ^= 1;
    }
}

static int tx_empty(void) { return tx_r == tx_w && !wr_busy[0] && !wr_busy[1]; }

/* the requests that returned: writes counted and the ring's next blocks sent; a returned read's byte, then what
 * the driver holds (SDCMD_QUERY, one CMD_READ of that many), stamped with the time they were taken.  When a read
 * has returned 4 times in one update and SDCMD_QUERY reported bytes, the next 1-byte read waits for the update (at
 * most 240 wake-ups a second for reading under load); a driver whose SDCMD_QUERY reports no bytes is read a byte a
 * wake-up.  After an error the next read also waits for the update. */
static void ser_poll(void)
{
    int i, k, bulk;
    if (!s_open) return;
    for (i = 0; i < 2; i++) {
        int j = wr_next ^ i;                               /* the older first */
        if (wr_busy[j] && CheckIO((struct IORequest *)swr[j])) {
            WaitIO((struct IORequest *)swr[j]); wr_busy[j] = 0;
            if (swr[j]->IOSer.io_Error) { ser_err(swr[j]->IOSer.io_Error); n_tx += swr[j]->IOSer.io_Actual; }
            else n_tx += swr[j]->IOSer.io_Length;
        }
    }
    tx_pump();
    if (brk_busy && CheckIO((struct IORequest *)sbrk)) {
        WaitIO((struct IORequest *)sbrk); brk_busy = 0;
        if (sbrk->IOSer.io_Error) notice(1, "SDCMD_BREAK: error %d (%s)", sbrk->IOSer.io_Error, ser_errname(sbrk->IOSer.io_Error));
    }
    if (rd_busy && CheckIO((struct IORequest *)srd)) {
        ULONG t = beam_now();
        BYTE e;
        WaitIO((struct IORequest *)srd); rd_busy = 0;
        e = srd->IOSer.io_Error;
        if (e) ser_err(e);
        if (srd->IOSer.io_Actual) rx_store(&rd_byte, 1, t);
        bulk = 0;
        for (k = 0; k < 8 && ser_query(); k++) {
            ULONG n = s_inbuf, f = rx_free();
            if (n > RDBUF) n = RDBUF;
            if (n > f) n = f;
            if (!n) break;
            sctl->IOSer.io_Command = CMD_READ; sctl->IOSer.io_Data = rd_buf; sctl->IOSer.io_Length = n;
            DoIO((struct IORequest *)sctl);
            if (sctl->IOSer.io_Error) ser_err(sctl->IOSer.io_Error);
            rx_store(rd_buf, sctl->IOSer.io_Actual, t);
            bulk = 1;
            if (n < RDBUF) break;
        }
        if (!e && (++rd_wakes < 4 || !bulk)) rd_start(); else rd_hold = 1;
    }
}

/* once an update: the held read sent again, the status word read */
static void ser_tick(void)
{
    rd_wakes = 0;
    if (!s_open) return;
    if (rd_hold && !rd_busy) rd_start();
    ser_poll();
}

/* a request ended: AbortIO when it is still out, then at most 2 s for the driver to return it */
static int io_end(struct IORequest *r)
{
    int i;
    if (!CheckIO(r)) AbortIO(r);
    for (i = 0; i < 100 && !CheckIO(r); i++) Delay(1);
    if (!CheckIO(r)) return 0;
    WaitIO(r);
    return 1;
}

/* every request of the driver back: queued ones by CMD_FLUSH, the active ones aborted.  Bytes still in the
 * transmit ring are dropped.  0 when the driver kept a request (the driver then cannot be closed). */
static int ser_quiesce(void)
{
    int i, ok = 1;
    if (!s_open) return 1;
    sctl->IOSer.io_Command = CMD_FLUSH; DoIO((struct IORequest *)sctl);
    for (i = 0; i < 2; i++) {
        int j = wr_next ^ i;
        if (!wr_busy[j]) continue;
        if (io_end((struct IORequest *)swr[j])) { wr_busy[j] = 0; n_tx += swr[j]->IOSer.io_Actual; } else ok = 0;
    }
    if (rd_busy) { if (io_end((struct IORequest *)srd)) rd_busy = 0; else ok = 0; }
    if (brk_busy) { if (io_end((struct IORequest *)sbrk)) brk_busy = 0; else ok = 0; }
    rd_hold = 0;
    if (tx_r != tx_w) {
        notice(0, "%lu bytes not sent: dropped", (unsigned long)((tx_w - tx_r) & RMASK));
        tx_r = tx_w;
    }
    if (!ok) notice(1, "%s kept a request after AbortIO and 2 s", drv_name[s_drv]);
    return ok;
}

static BYTE ser_params(void)
{
    UBYTE f = 0;
    if (fmt_par[s_fmt]) f |= SERF_PARTY_ON | (fmt_par[s_fmt] == 2 ? SERF_PARTY_ODD : 0);
    if (s_flow == FLOW_RTS) f |= SERF_7WIRE;
    if (s_flow != FLOW_XON) f |= SERF_XDISABLED;
    if (s_boogie) f |= SERF_RAD_BOOGIE;
    sctl->IOSer.io_Command = SDCMD_SETPARAMS;
    sctl->io_CtlChar = SER_DEFAULT_CTLCHAR;
    sctl->io_RBufLen = s_rbuf;
    sctl->io_ExtFlags = 0;
    sctl->io_Baud = s_baud;
    sctl->io_BrkTime = 250000;
    sctl->io_TermArray.TermArray0 = sctl->io_TermArray.TermArray1 = 0;
    sctl->io_ReadLen = sctl->io_WriteLen = fmt_bits[s_fmt];
    sctl->io_StopBits = fmt_stop[s_fmt];
    sctl->io_SerFlags = f;
    DoIO((struct IORequest *)sctl);
    s_param_err = sctl->IOSer.io_Error;
    /* the read and write requests carry the same parameters (a driver may read them from any request) */
    CopyMem(&sctl->io_CtlChar, &srd->io_CtlChar, sizeof(struct IOExtSer) - offsetof(struct IOExtSer, io_CtlChar));
    CopyMem(&sctl->io_CtlChar, &swr[0]->io_CtlChar, sizeof(struct IOExtSer) - offsetof(struct IOExtSer, io_CtlChar));
    CopyMem(&sctl->io_CtlChar, &swr[1]->io_CtlChar, sizeof(struct IOExtSer) - offsetof(struct IOExtSer, io_CtlChar));
    CopyMem(&sctl->io_CtlChar, &sbrk->io_CtlChar, sizeof(struct IOExtSer) - offsetof(struct IOExtSer, io_CtlChar));
    return s_param_err;
}

static void param_notice(void)
{
    char b[16];
    sprintf(b, "%lu", (unsigned long)s_baud);
    if (s_param_err) notice(1, "SDCMD_SETPARAMS %s baud %s %s: error %d (%s)", b, fmt_lbl[s_fmt], flow_lbl[s_flow],
                            s_param_err, ser_errname(s_param_err));
    else notice(0, "SDCMD_SETPARAMS %s baud %s %s, read buffer %lu: done", b, fmt_lbl[s_fmt], flow_lbl[s_flow],
                (unsigned long)s_rbuf);
}

/* driver d opened (unit drv_unit[d]), its parameters set and the first read sent; NULL, or why it did not open */
static const char *ser_open(int d)
{
    static char why[128];
    unsigned long long e0;
    BYTE e;
    int i;
    if (s_open) return NULL;
    memset((UBYTE *)srd + sizeof(struct Message), 0, sizeof(struct IOExtSer) - sizeof(struct Message));
    srd->io_SerFlags = (s_flow == FLOW_RTS ? SERF_7WIRE : 0) | (s_flow != FLOW_XON ? SERF_XDISABLED : 0);
    e0 = eclock();
    e = OpenDevice((CONST_STRPTR)drv_name[d], (ULONG)drv_unit[d], (struct IORequest *)srd, 0);
    s_open_us = us_since(e0);
    s_drv = d;
    if (e) {
        snprintf(why, sizeof why, "%s unit %ld: OpenDevice error %d (%s)", drv_name[d], (long)drv_unit[d], e,
                 ser_errname(e));
        return why;
    }
    s_open = 1; s_open_flow = s_flow;
    for (i = 0; i < 2; i++) CopyMem(srd, swr[i], sizeof(struct IOExtSer));
    CopyMem(srd, sctl, sizeof(struct IOExtSer)); CopyMem(srd, sbrk, sizeof(struct IOExtSer));
    wr_busy[0] = wr_busy[1] = 0; wr_next = 0; rd_busy = brk_busy = 0;
    {
        struct Library *lib = (struct Library *)srd->IOSer.io_Device;
        notice(0, "%s unit %ld opened in %lu us (version %u.%u)", drv_name[d], (long)drv_unit[d],
               (unsigned long)s_open_us, lib->lib_Version, lib->lib_Revision);
    }
    ser_params();
    param_notice();
    ser_query();
    rd_start();
    return NULL;
}

/* the driver closed and, with `expunge` and nothing else having it open, removed from the device list (RemDevice
 * calls its Expunge: a disk-loaded driver is unloaded, the ROM's serial.device is made again from its resident at
 * the next OpenDevice).  0 when a request could not be ended (the driver is then left open). */
static int ser_close(int expunge)
{
    unsigned long long e0;
    int gone = 0;
    if (!s_open) return 1;
    if (!ser_quiesce()) return 0;
    e0 = eclock();
    CloseDevice((struct IORequest *)srd);
    s_open = 0; s_status = 0xFFFFu; s_inbuf = 0;
    if (expunge) {
        struct Library *d;
        Forbid();
        d = (struct Library *)FindName(&SysBase->DeviceList, (CONST_STRPTR)drv_name[s_drv]);
        if (d && d->lib_OpenCnt == 0) RemDevice((struct Device *)d);
        gone = FindName(&SysBase->DeviceList, (CONST_STRPTR)drv_name[s_drv]) == NULL;
        Permit();
    }
    s_close_us = us_since(e0);
    notice(0, "%s closed%s in %lu us", drv_name[s_drv], gone ? " and expunged" : "", (unsigned long)s_close_us);
    return 1;
}

static void tx_kick(void) { tx_pump(); }

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

/* filler bytes (0xFE): ignored by the test's receiver.  A USB serial adapter or a bridge may pass the last bytes of
 * a run on only as newer bytes follow them: these push them through. */
static void tx_filler(unsigned int n)
{
    static const UBYTE fe[16] = { 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE };
    while (n >= 16u) { if (!tx_put(fe, 16u)) return; n -= 16u; }
    if (n) (void)tx_put(fe, n);
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

enum { G_SCR, G_DRV, G_BAUD, G_FMT, G_FLOW, G_LINE, G_N };   /* GadTools gadgets of the main window */
static struct Gadget *gad[G_N];
enum { M_NAME, M_UNIT, M_N };                              /* of the driver panel */
static struct Gadget *mgad[M_N];

/* window coordinates: the port group across the top, the loopback test (A) and port activity (B) groups side by
 * side, the monitor below to the bottom */
/* the horizontal layout: drawn for 640 pixels and scaled to the window's width (win_w, 640 or more): 8-pixel
 * margins, the two groups split the width, their contents at proportional offsets (AX, BX for offsets made for a
 * 308-pixel group), the port row at proportional x (SX), the monitor's send line takes the extra width */
#define WIN_W 640                                          /* the width the offsets are drawn for */
#define WIN_W_MIN 616                                      /* the narrowest inner width: every control fits */
#define WIN_W_MAX 1280                                     /* the widest: the groups no wider than twice their size */
static int win_w = WIN_W;                                  /* the window's inner width */
static int ox, oy;                                         /* the window's left and top border */
#define P_X   (ox + 8)
#define P_W   (win_w - 16)
#define A_X   (ox + 8)
#define A_W   ((win_w - 24) / 2)
#define B_X   (ox + 16 + A_W)
#define M_X   (ox + 8)
#define M_W   (win_w - 16)
#define LV_X  (ox + 16)
#define LV_W  (win_w - 32)
#define SX(x) (ox + (x) * win_w / WIN_W)
#define AX(x) (A_X + (x) * A_W / 308)
#define BX(x) (B_X + (x) * A_W / 308)
#define MON_ROWS_MAX 64
/* the vertical layout, from the top of the window's inner area: the full one (3 monitor rows or more in it, 236
 * lines) and the compact one (down to 179 lines: a 200-line screen), chosen from the inner height; the monitor
 * takes the lines below the groups.  A group's title is centred on its top edge and stays clear of the border above */
typedef struct {
    int p_y, p_h, p_gy;                                    /* the port group, its gadgets' y (height 12) */
    int a_y, a_h;                                          /* the loopback test and port activity groups */
    int btn, load, pan, pan_h, pan_t;                      /* group A: buttons, "Load %", the result panel, its text */
    int head, rule, row, step;                             /* group A's table: heads, rule, first row, row pitch */
    int bhead, bar0, bar1, led, pc;                        /* group B: heading, meters, modem lines, counters */
    int m_y, m_hd, lv_y, m_by, lv_gap, m_bot;              /* the monitor: head bar, view, button row, margins */
} lay_t;
static const lay_t lay_full    = { 8, 24, 15, 39, 105, 10, 13, 27, 14, 30, 45, 54, 58, 10, 10, 22, 36, 53, 67,
                                   152, 10, 174, 21, 5, 6 };
static const lay_t lay_compact = { 5, 20, 11, 30, 86, 7, 10, 23, 12, 25, 37, 46, 48, 9, 7, 18, 31, 43, 54,
                                   121, 7, 141, 18, 3, 3 };
static const lay_t *ly = &lay_full;
#define P_Y   (oy + ly->p_y)
#define P_H   (ly->p_h)
#define P_GY  (oy + ly->p_gy)
#define A_Y   (oy + ly->a_y)
#define A_H   (ly->a_h)
#define M_Y   (oy + ly->m_y)
#define LV_Y  (oy + ly->lv_y)
#define LAY_MIN_H(l) ((l)->lv_y + 14 + (l)->lv_gap + (l)->m_by + (l)->m_bot)   /* one monitor row */
static int m_h, lv_h;
static int st_redraw;                                      /* the result panel drawn again in full */                                      /* the monitor group's and the view's height */
#define a_cx  (A_X + A_W / 2)
#define b_cx  (B_X + A_W / 2)
static int bar_x;                                          /* x of the meters' first segment */
#define BAR_Y(k) (A_Y + ((k) ? ly->bar1 : ly->bar0))
/* the result table of the loopback group, two columns: label x, the right edge of the values, and the width of the
 * value field (the longest label, 8 or 7 characters, and 4 pixels before it) */
static const int col_w[2] = { 72, 76 };
#define COL_L(c) AX((c) ? 164 : 8)
#define COL_R(c) AX((c) ? 300 : 148)
#define ROW_Y(i) (A_Y + ly->row + (i) * ly->step)
/* the port activity group's table: three rows of two columns */
#define PC_L(c) BX((c) ? 160 : 12)
#define PC_R(c) BX((c) ? 296 : 148)
#define PC_Y(i) (A_Y + ly->pc + (i) * ly->step)
#define LED_Y   (A_Y + ly->led)

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


enum { B_RUN, B_L100, B_L75, B_L50, B_L25, B_SEND, B_ASC, B_HEX, B_BRK, B_CLEAR, B_PAUSE, B_XFER, B_QUIT, B_TERM, B_N };
static btn_t btn[B_N];
enum { MB_OPEN, MB_CLOSE, MB_N };
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

typedef struct { unsigned long long e; ULONG c[2]; } snap_t;   /* E clock; bytes out, in */
static snap_t hist[64];
static unsigned int hn;
static UBYTE seg_on[2][SEGS];
static ULONG peak[2], peak_hold[2], fig_now[2];
static char  fig_old[2][24];
#define FIG_W 56                                           /* the figure's field: 7 characters (230,400) */

/* counter k per second over the last `ticks` ticks, times the frame's bits.  32-bit arithmetic: the E clock and the
 * interval in units of 64 E clock cycles (90 us) */
static ULONG rate(int k, unsigned int ticks)
{
    const snap_t *a, *b;
    ULONG de, r;
    if (hn <= ticks) return 0;
    a = &hist[(hn - 1u) & 63u]; b = &hist[(hn - 1u - ticks) & 63u];
    de = (ULONG)(a->e - b->e) >> 6;
    if (!de) return 0;
    r = (a->c[k] - b->c[k]) * (efreq >> 6) / de;
    return r * frame_bits;
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
    ULONG fs = s_baud;
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
    char s[16];
    fig_now[k] = rate(k, 60);
    fmt_thousands(s, fig_now[k]);
    value_r(fig_old[k], bar_x + SEGS * 4 + 4 + 8 + FIG_W, BAR_Y(k), FIG_W, P_BLACK, s);
}

/* ---- the modem lines: LEDs from SDCMD_QUERY's status word.  DTR, RTS (driven by the driver), CTS, DSR, CD (inputs)
 * are active low in bits 7, 6, 4, 3, 5; RI is bit 2, active high (the parallel port's SEL pin as well). ---- */

static const char *const led_lbl[6] = { "DTR", "RTS", "CTS", "DSR", "CD", "RI" };
static const UBYTE led_bit[6] = { 7, 6, 4, 3, 5, 2 };
static int led_x[6], led_old[6];

static int led_on(int i)
{
    if (!s_open) return 0;
    if (led_bit[i] == 2) return (s_status >> 2) & 1u;
    return !((s_status >> led_bit[i]) & 1u);
}

static void leds_frame(void)
{
    int i, w = 0, x;
    for (i = 0; i < 6; i++) w += (int)strlen(led_lbl[i]) * 8 + 4 + 12;
    x = b_cx - (w + 5 * 12) / 2;
    for (i = 0; i < 6; i++) {
        pen_text(rp, x, LED_Y, P_BLACK, led_lbl[i]);
        x += (int)strlen(led_lbl[i]) * 8 + 4;
        DrawBevelBox(rp, x, LED_Y - 1, 12, 10, GT_VisualInfo, vi, GTBB_Recessed, TRUE, TAG_DONE);
        led_x[i] = x; led_old[i] = -1;
        x += 12 + 12;
    }
}

static void leds_draw(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        int on = led_on(i);
        if (on == led_old[i]) continue;
        led_old[i] = on;
        SetAPen(rp, on ? P_GRN : P_GRN_D); RectFill(rp, led_x[i] + 2, LED_Y, led_x[i] + 9, LED_Y + 7);
    }
}


/* ---- loopback test ---------------------------------------------------------------------------------------- */

enum { T_IDLE, T_RUN, T_DRAIN, T_DONE };
static int    t_state = T_IDLE, t_rate = 3;               /* index of rate_pct */
static const unsigned int rate_pct[4] = { 25, 50, 75, 100 };
static ULONG  t_seq, t_sent, t_exp, t_cand, t_recv, t_lost, t_err;
static ULONG  t_ovr0, t_full0, t_par0, t_brk0, t_io0, t_tx0, t_rx0, t_tx1, t_rx1;   /* counters at the start, stop */
static ULONG  t_lat_n, t_lat_min, t_lat_max;
static unsigned long long t_lat_sum, t_lat_sq, t_e0, t_e1, t_drain_e;
static ULONG  t_tok, t_rem;                                /* t_tok: bytes x 16 */
static unsigned long long t_last_e;
static int    t_started, t_drain_set;
static ULONG  t_dur;                                       /* the test's length, seconds; 0 until stopped */

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
 * A view drawn by the program on a dark panel: the last MON_MAX lines in a ring, IN lines green, OUT lines and the
 * driver's events amber, errors and a line counting bytes not shown red.  Those pens and the panel's differ only in
 * bitplanes 0 and 1, so the view is drawn through a write mask of those two.  Measured on this machine: a
 * 72-character Text 5.6 ms, a ScrollRaster of the view 12.2 ms, against 16.7 ms an update; so an update does one of
 * them at most: the view follows the newest lines with a scroll at most twice a second (by every row that arrived
 * since), and draws one row an update until every row shows its line.  A scroller (GadTools) moves through the
 * ring.  Received bytes outside a test are gathered into lines: in ASCII a line ends at CR, LF (CR LF as one end) or
 * at the view's width, in hex at 12 bytes; a line not complete after 100 ms is shown as it is. */

#define MON_COLS_MAX   76                                  /* the view's columns at most (the line buffers) */
static char         mon_txt[MON_MAX][80];
static UBYTE        mon_pen[MON_MAX];
static int          mon_first, mon_count, mon_top, mon_follow = 1;
static int          mon_drawn_top, mon_row_line[MON_ROWS_MAX];   /* the line each row shows (-1 blank, -2 unknown) */
static ULONG        mon_base, mon_scroll_hn;               /* lines dropped from the ring; the last scroll */
static int          mon_x, mon_y, mon_rows, mon_cols;      /* the text area */
static int          show_test, paused;
static ULONG        t_zero;
static int          test_shown_in, test_shown_out, mon_budget;
static ULONG        mon_skipped;                           /* bytes */
#define MON_PER_UPDATE 3                                   /* lines formatted an update; more are counted */
#define TEST_EVERY     15                                  /* test data: one message each way every 15 updates */
#define MON_PLANES     3                                   /* the write mask of the view */
#define MON_SCROLL_GAP 30                                  /* updates between scrolls */
#define TL_HEX         12                                  /* bytes a hex line */
#define MON_PRE        14                                  /* time, direction and gaps before the data */

#define MON_FMT "%8s %-3s  %s"

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


/* the time of a line: seconds since the program started, from the line clock */
static void mon_time(char *tm, ULONG t)
{
    ULONG ln = t - t_zero, ms = (ln / 1000u) * b_ns / 1000u + (ln % 1000u) * b_ns / 1000000u;
    sprintf(tm, "%lu.%03lu", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u));
}

/* a byte as the monitor shows it in ASCII: printable ISO 8859-1, else a dot */
static char shown(UBYTE c) { return (char)(((c >= 0x20u && c < 0x7Fu) || c >= 0xA0u) ? c : '.'); }

/* a test message: its bytes and its sequence number */
static long test_seq(const UBYTE *m, unsigned int n);
static void mon_add(ULONG t, int out, const UBYTE *m, unsigned int n)
{
    char line[112], tm[16], hx[40], d[64];
    long s = test_seq(m, n);
    if (paused) return;
    mon_time(tm, t);
    hex_bytes(hx, m, n, n > 11 ? 10 : 11);
    if (s >= 0) sprintf(d, "%-33s message %ld", hx, s); else sprintf(d, "%-33s malformed", hx);
    snprintf(line, sizeof line, MON_FMT, tm, out ? "OUT" : "IN", d);
    mon_push(out ? P_AMB : (s >= 0 ? P_GRN : P_RED), line);
}

static int cli_mode;

/* an event of the driver (amber) or an error (red) in the monitor; without a window, a line on the CLI */
static void notice(int err, const char *fmt, ...)
{
    char s[96], line[112], tm[16];
    va_list ap;
    va_start(ap, fmt); vsnprintf(s, sizeof s, fmt, ap); va_end(ap);
    if (!win) { if (cli_mode) printf("serialmon: %s\n", s); return; }
    mon_time(tm, beam_now());
    snprintf(line, sizeof line, MON_FMT, tm, err ? "ERR" : "DEV", s);
    mon_push(err ? P_RED : P_AMB, line);
}

static void mon_note_skipped(void)
{
    char line[96];
    if (!mon_skipped) return;
    snprintf(line, sizeof line, "%8s %-3s  %lu byte%s not shown (more than %d lines an update)", "", "IN",
             (unsigned long)mon_skipped, mon_skipped == 1 ? "" : "s", MON_PER_UPDATE);
    mon_push(P_RED, line);
    mon_skipped = 0;
}

/* bytes as one line's data: hex (12 at most, a '+' when there are more) and their ASCII, or ASCII alone */
static void mon_data(char *o, const UBYTE *b, unsigned int n, int hex)
{
    unsigned int i, max = (unsigned int)(mon_cols - MON_PRE);
    if (hex) {
        unsigned int k = n > TL_HEX ? TL_HEX : n;
        hex_bytes(o, b, k, TL_HEX);
        i = (unsigned int)strlen(o);
        if (n > TL_HEX) o[i++] = '+';
        while (i < TL_HEX * 3u + 1u) o[i++] = ' ';
        for (k = 0; k < n && k < TL_HEX; k++) o[i++] = shown(b[k]);
        o[i] = 0;
        return;
    }
    if (max > 70u) max = 70u;
    for (i = 0; i < n && i < max; i++) o[i] = shown(b[i]);
    if (n > max) o[max - 1] = '+';
    o[i] = 0;
}

/* bytes sent from the send line: one amber line */
static void mon_out(const UBYTE *b, unsigned int n)
{
    char line[112], tm[16], d[80];
    if (paused || !win) return;
    mon_time(tm, beam_now());
    mon_data(d, b, n, s_hex);
    snprintf(line, sizeof line, MON_FMT, tm, "OUT", d);
    mon_push(P_AMB, line);
}

/* ---- received bytes outside a test, gathered into lines ---- */
static UBYTE        tl_b[80];
static int          tl_n, tl_cr;
static ULONG        tl_t;
static unsigned int tl_hn;

static void tl_flush(void)
{
    char line[112], tm[16], d[80];
    if (!tl_n) return;
    if (mon_budget > 0 && !paused) {
        mon_budget--;
        mon_time(tm, tl_t);
        mon_data(d, tl_b, (unsigned int)tl_n, s_hex);
        snprintf(line, sizeof line, MON_FMT, tm, "IN", d);
        mon_push(P_GRN, line);
    } else mon_skipped += (ULONG)tl_n;
    tl_n = 0;
}

static void term_in(UBYTE b, ULONG t)
{
    int max = s_hex ? TL_HEX : mon_cols - MON_PRE;
    if (!win) return;
    if (max > (int)sizeof tl_b) max = (int)sizeof tl_b;
    tl_hn = hn;
    if (!s_hex) {
        if (b == '\n' && tl_cr) { tl_cr = 0; return; }   /* CR LF: one line end */
        tl_cr = b == '\r';
        if (b == '\r' || b == '\n') { tl_flush(); return; }
    }
    if (!tl_n) tl_t = t;
    tl_b[tl_n++] = b;
    if (tl_n >= max) tl_flush();
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
    (void)total;
    if (m[0] >= 0xF8u) return;                             /* filler (FEh) and other single bytes from F8h up */
    if (t_state == T_RUN || t_state == T_DRAIN) {
        long s = test_seq(m, n);
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
        if (show_test && test_shown_in < 1) { test_shown_in++; mon_add(t, 0, m, n); }
    }
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


/* ---- section 7: the terminal (amiga/tools/term: libvterm 0.3.3 on a screen of its own).  While it is open the
 * received bytes go to it instead of the monitor, its keys and answers go out through the transmit ring, and the
 * main window is not drawn; Right Amiga Q closes it. ---- */
static term_t *vt;
/* the terminal's window: this screen's pen for each ANSI colour (black, red, green, yellow, blue, magenta, cyan,
 * white, then the bright eight); magenta has no pen of its own here and takes red.  Full screen has the 16 exact. */
static const UBYTE vt_pens[16] = { 1, 12, 11, 6, 13, 7, 3, 0, 14, 7, 5, 6, 3, 7, 3, 2 };

/* ZMODEM's start from a sender in the terminal's input: a hex header ("*", "*", ZDLE (CAN), "B", then 14 hex
 * digits: the frame type, 4 bytes, the CRC-16 (CCITT, polynomial 0x1021, initial 0) of those 5 bytes) whose CRC
 * checks and whose type is ZRQINIT (0, what sz sends first).  zseen is acted on in the update. */
static int zstate, znib, zseen, z_auto = 1, vt_utf8;
static UBYTE zbuf[7];
static char z_folder[256];                                 /* the download folder; empty: the current directory */

static UWORD crc16(const UBYTE *b, int n)
{
    UWORD c = 0;
    int i, k;
    for (i = 0; i < n; i++) {
        c ^= (UWORD)(b[i] << 8);
        for (k = 0; k < 8; k++) c = (UWORD)((c & 0x8000u) ? (c << 1) ^ 0x1021u : c << 1);
    }
    return c;
}

static void zhex_feed(UBYTE b)
{
    int h = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : -1;
    switch (zstate) {
    case 0: zstate = b == '*' ? 1 : 0; break;
    case 1: zstate = b == '*' ? 2 : 0; break;
    case 2: zstate = b == 0x18 ? 3 : b == '*' ? 2 : 0; break;
    case 3: zstate = b == 'B' ? 4 : 0; znib = 0; break;
    default:                                               /* 4: the 14 hex digits (lower case, as ZMODEM sends) */
        if (h < 0) { zstate = b == '*' ? 1 : 0; break; }
        if (znib & 1) zbuf[znib >> 1] |= (UBYTE)h; else zbuf[znib >> 1] = (UBYTE)(h << 4);
        if (++znib == 14) {
            if (zbuf[0] == 0 && crc16(zbuf, 5) == (UWORD)((zbuf[5] << 8) | zbuf[6])) zseen = 1;
            zstate = 0;
        }
        break;
    }
}

static void vt_out(const UBYTE *b, ULONG n, void *u)
{
    (void)u;
    if (s_open && !tx_put(b, (unsigned int)n)) n_full++;
}

/* the received bytes of the ring: to the test's receiver while a test runs, to the terminal while it is open, else
 * to the monitor's lines */
static void rx_take(void)
{
    UWORD r = rx_r, w = rx_w;
    int test = t_state == T_RUN || t_state == T_DRAIN;
    if (vt && !test) {
        while (r != w) {                                   /* contiguous runs of the ring, as many as the terminal takes */
            UWORD e = w > r ? w : 0;
            ULONG n = (ULONG)(((e ? e : RING) - r) & 0xFFFFu), k = term_input(vt, (const UBYTE *)rxq + r, n), i;
            if (z_auto) for (i = 0; i < k; i++) zhex_feed(rxq[(r + i) & RMASK]);   /* the ZMODEM start (zseen) */
            r = (UWORD)((r + k) & RMASK);
            if (k < n) break;
        }
        rx_r = r;
        return;
    }
    for (; r != w; r = (UWORD)((r + 1u) & RMASK)) { if (test) rx_byte(rxq[r], rxt[r]); else term_in(rxq[r], rxt[r]); }
    rx_r = r;
}

/* ---- the send line: in hex mode bytes as two hex digits each, spaces or commas between; in ASCII mode the text,
 * with \r, \n, \t, \\ and \xHH, followed by CR ---- */
static const char *line_bytes(const char *p, UBYTE *b, unsigned int *pn, unsigned int max)
{
    unsigned int n = 0;
    if (s_hex) {
        while (*p) {
            unsigned int v = 0, k = 0;
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            while (k < 2 && ((*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f'))) {
                v = v * 16u + (unsigned int)(*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10); p++; k++;
            }
            if (!k || (*p && *p != ' ' && *p != ',')) return "Send: two hex digits a byte, spaces between";
            if (n >= max) return "Send: too many bytes";
            b[n++] = (UBYTE)v;
        }
    } else {
        while (*p && n < max - 1u) {
            UBYTE c = (UBYTE)*p++;
            if (c == '\\' && *p) {
                char e = *p++;
                if (e == 'r') c = '\r'; else if (e == 'n') c = '\n'; else if (e == 't') c = '\t';
                else if (e == '\\') c = '\\';
                else if (e == 'x' && p[0] && p[1]) {
                    unsigned int v = 0, k;
                    for (k = 0; k < 2; k++, p++) {
                        char h = (char)(*p | 0x20);
                        if (*p >= '0' && *p <= '9') v = v * 16u + (unsigned int)(*p - '0');
                        else if (h >= 'a' && h <= 'f') v = v * 16u + (unsigned int)(h - 'a' + 10);
                        else return "Send: \\x takes two hex digits";
                    }
                    c = (UBYTE)v;
                } else { b[n++] = '\\'; c = (UBYTE)e; }
            }
            b[n++] = c;
        }
        b[n++] = '\r';
    }
    *pn = n;
    return NULL;
}

static void send_line(void)
{
    UBYTE b[256];
    unsigned int n = 0;
    const char *err;
    if (!s_open || t_state == T_RUN || t_state == T_DRAIN) return;
    err = line_bytes((const char *)((struct StringInfo *)gad[G_LINE]->SpecialInfo)->Buffer, b, &n, sizeof b);
    if (err) { notice(1, "%s", err); return; }
    if (!n) return;
    if (tx_put(b, n)) mon_out(b, n); else notice(1, "Send: the transmit ring is full");
}

static void send_break(void)
{
    if (!s_open || brk_busy || t_state == T_RUN || t_state == T_DRAIN) return;
    sbrk->IOSer.io_Command = SDCMD_BREAK; sbrk->io_SerFlags &= (UBYTE)~SERF_QUEUEDBRK;
    SendIO((struct IORequest *)sbrk); brk_busy = 1;
    notice(0, "SDCMD_BREAK sent (250 ms)");
}

static void clear_buffer(void)
{
    if (!s_open) return;
    sctl->IOSer.io_Command = CMD_CLEAR;
    if (DoIO((struct IORequest *)sctl)) notice(1, "CMD_CLEAR: error %d (%s)", sctl->IOSer.io_Error, ser_errname(sctl->IOSer.io_Error));
    else notice(0, "CMD_CLEAR: the driver's read buffer cleared");
}

/* ---- the driver list of the port group's cycle gadget: the label is the name, and the unit when it is not 0 ---- */
static char drv_txt[DRV_MAX][56];

static void drv_labels(void)
{
    int i;
    for (i = 0; i < drv_n; i++) {
        if (drv_unit[i]) snprintf(drv_txt[i], sizeof drv_txt[i], "%s %ld", drv_name[i], (long)drv_unit[i]);
        else strcpy(drv_txt[i], drv_name[i]);
        drv_lbl[i] = (STRPTR)drv_txt[i];
    }
    drv_lbl[drv_n] = NULL;
}

/* the index of driver `name` unit u, added to the list when it is not in it (the last entry is replaced when the
 * list is full) */
static int drv_add(const char *name, LONG u)
{
    int i;
    for (i = 0; i < drv_n; i++) if (!strcmp(drv_name[i], name) && drv_unit[i] == u) return i;
    i = drv_n < DRV_MAX ? drv_n++ : DRV_MAX - 1;
    strncpy(drv_name[i], name, sizeof drv_name[i] - 1); drv_name[i][sizeof drv_name[i] - 1] = 0;
    drv_unit[i] = u;
    drv_labels();
    if (win) GT_SetGadgetAttrs(gad[G_DRV], win, NULL, GTCY_Labels, (ULONG)drv_lbl, TAG_DONE);
    return i;
}

/* ---- the driver panel: another driver by name and unit ---------------------------------------------------- */

static char m_name[48] = "serial.device";
static LONG m_unitv;
static int  m_lx, m_gx, m_row[3], m_w, m_pick = -1;

static void m_close(void)
{
    if (!mwin) return;
    CloseWindow(mwin); mwin = NULL;
    FreeGadgets(mglist); mglist = NULL;
    memset(mgad, 0, sizeof mgad);
    if (blocked) { EndRequest(&block_req, win); blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static int m_open(void)
{
    int bt = scr->WBorTop + scr->Font->ta_YSize + 1, bl = scr->WBorLeft, y, h, bx;
    struct NewGadget ng;
    struct Gadget *g;
    struct RastPort *r;
    if (mwin) return 1;
    m_w = 400; m_lx = bl + 100; m_gx = bl + 108;
    y = bt + 10;
    m_row[0] = y; m_row[1] = y + 20; m_row[2] = m_row[1] + 22;
    y = m_row[2] + 8 + 14;
    bx = m_w / 2;
    btn_init(&mbtn[MB_OPEN], MBTN_ID + MB_OPEN, bx - 8 - 96, y, 96, 14, "Open", ICON_NONE);
    btn_init(&mbtn[MB_CLOSE], MBTN_ID + MB_CLOSE, bx + 8, y, 96, 14, "Close", ICON_NONE);
    h = y + 14 + 10 + scr->WBorBottom;
    memset(mgad, 0, sizeof mgad);
    if (!(g = CreateContext(&mglist))) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi; ng.ng_TextAttr = &topaz8; ng.ng_LeftEdge = m_gx;
    ng.ng_TopEdge = m_row[0]; ng.ng_Width = 248; ng.ng_Height = 14; ng.ng_GadgetID = M_NAME;
    mgad[M_NAME] = g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)m_name, GTST_MaxChars, sizeof m_name - 1,
                                    TAG_DONE);
    ng.ng_TopEdge = m_row[1]; ng.ng_Width = 64; ng.ng_GadgetID = M_UNIT;
    mgad[M_UNIT] = g = CreateGadget(INTEGER_KIND, g, &ng, GTIN_Number, (ULONG)m_unitv, GTIN_MaxChars, 3, TAG_DONE);
    if (!g) { FreeGadgets(mglist); mglist = NULL; return 0; }
    mwin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)((scr->Width - m_w) / 2),
                          WA_Top, (ULONG)((scr->Height - h) / 2), WA_Width, (ULONG)m_w, WA_Height, (ULONG)h,
                          WA_Title, (ULONG)"Open Another Driver", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                          WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_Gadgets, (ULONG)mglist,
                          WA_IDCMP, STRINGIDCMP | INTEGERIDCMP | IDCMP_CLOSEWINDOW | IDCMP_GADGETDOWN |
                                    IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!mwin) { FreeGadgets(mglist); mglist = NULL; return 0; }
    r = mwin->RPort;
    SetFont(r, font);
    GT_RefreshWindow(mwin, NULL);
    text_r(r, m_lx, m_row[0] + 3, P_BLACK, "Name");
    text_r(r, m_lx, m_row[1] + 3, P_BLACK, "Unit");
    pen_text(r, m_gx, m_row[2], P_TXT_BLUE, "In DEVS: or in memory");
    btn_add(mbtn, MB_N, mwin);
    ActivateGadget(mgad[M_NAME], mwin, NULL);
    InitRequester(&block_req);
    if (Request(&block_req, win)) blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    return 1;
}

/* the panel's driver: added to the list and picked (the switch follows when the panel has closed) */
static void m_take(void)
{
    char nm[48];
    int i = 0, j;
    strncpy(nm, (const char *)((struct StringInfo *)mgad[M_NAME]->SpecialInfo)->Buffer, sizeof nm - 1);
    nm[sizeof nm - 1] = 0;
    while (nm[i] == ' ') i++;
    memmove(nm, nm + i, strlen(nm + i) + 1);
    for (j = (int)strlen(nm); j > 0 && nm[j - 1] == ' '; j--) nm[j - 1] = 0;
    if (!nm[0]) return;
    m_unitv = ((struct StringInfo *)mgad[M_UNIT]->SpecialInfo)->LongInt;
    if (m_unitv < 0) m_unitv = 0;
    strcpy(m_name, nm);
    m_pick = drv_add(nm, m_unitv);
}

/* one event of the panel; 0 when it is to close */
static int m_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_CLOSEWINDOW: return 0;
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(mwin); GT_EndRefresh(mwin, TRUE); break;
    case IDCMP_VANILLAKEY:
        if (code == 0x1B) return 0;                        /* Esc */
        if (code == '\r') { m_take(); return m_pick < 0; }
        switch (code | 0x20) {
        case 'o': m_take(); return m_pick < 0;
        case 'c': return 0;
        case 'n': ActivateGadget(mgad[M_NAME], mwin, NULL); break;
        case 'u': ActivateGadget(mgad[M_UNIT], mwin, NULL); break;
        }
        break;
    case IDCMP_GADGETDOWN:
        if (g->GadgetID >= MBTN_ID) btn_draw(&mbtn[g->GadgetID - MBTN_ID]);
        break;
    case IDCMP_GADGETUP:
        if (g->GadgetID >= MBTN_ID) {
            btn_t *b = &mbtn[g->GadgetID - MBTN_ID];
            btn_draw(b);
            if (b->dis) break;
            if (b == &mbtn[MB_OPEN]) { m_take(); return m_pick < 0; }
            return 0;
        }
        if (g->GadgetID == M_NAME) ActivateGadget(mgad[M_UNIT], mwin, NULL);   /* Return in the name: the unit */
        else if (g->GadgetID == M_UNIT) { m_take(); return m_pick < 0; }
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
    if (awin || mwin) return;
    if (scr->Height < 226) { ab_h = 198; ab_py = 42; ab_t0 = 15; ab_t1 = 26; }
    wx = ((scr->Width - AB_W) / 2) & ~15; wy = (scr->Height - AB_H) / 2;
    ab_vbl_n = 0;
    m_sin_init();
    btn_init(&abtn, MBTN_ID + 10, AB_W / 2 - 48, AB_H - (ab_h < 218 ? 21 : 24), 96, 14, "OK", ICON_NONE);
    awin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)wx, WA_Top, (ULONG)wy,
                          WA_Width, AB_W, WA_Height, AB_H, WA_Title, (ULONG)"About Serial Monitor",
                          WA_DragBar, FALSE, WA_DepthGadget, FALSE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_RMBTrap, TRUE,
                          WA_IDCMP, IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!awin) return;
    SetFont(awin->RPort, font);
    ab_x = wx + AB_PX; ab_y = wy + AB_PY;
    ab_text(ab_t0, P_TXT_BLUE, 1, "Serial Monitor");
    ab_text(ab_t1, P_BLACK, 0, "The serial port through its Exec driver");
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

/* why the test cannot run now: 0 it can */
static int test_block(void)
{
    if (!s_open) return 1;
    if (fmt_bits[s_fmt] != 8) return 2;
    if (s_flow == FLOW_XON) return 3;
    return 0;
}
static const char *const block_txt[4] = { "READY", "NO DRIVER OPEN", "TEST NEEDS 8 DATA BITS", "TEST NEEDS NO xON/xOFF" };

/* the driver's errors since the test started: overruns, read buffer overflows, parity, breaks, other */
static ULONG test_ioerrs(void)
{
    return (n_ovr - t_ovr0) + (n_full - t_full0) + (n_par - t_par0) + (n_brk - t_brk0) + (n_ioerr - t_io0);
}

static int test_pass(void) { return t_state == T_DONE && !t_lost && !t_err && t_recv == t_sent && !test_ioerrs(); }

/* the result panel, drawn when it changes */
static void test_status(unsigned long long now)
{
    static char head_old[32];
    static int pen_old = -1;
    char s[32];
    ULONG secs = 0;
    int pen = P_GREY, blk = test_block();
    static int state_old = -1, blk_old = -1;
    static ULONG secs_old;
    if (st_redraw) { pen_old = state_old = -1; st_redraw = 0; }
    if (t_state != T_IDLE) secs = ((ULONG)(((t_state == T_RUN ? now : t_e1) - t_e0) >> 4)) / (efreq >> 4);
    if (t_state == state_old && secs == secs_old && blk == blk_old) return;   /* changes once a second at most */
    state_old = t_state; secs_old = secs; blk_old = blk;
    switch (t_state) {
    case T_IDLE:  strcpy(s, block_txt[blk]); break;
    case T_RUN:   sprintf(s, "RUNNING   %lu:%02lu", (unsigned long)(secs / 60u), (unsigned long)(secs % 60u));
                  pen = P_AMB; break;
    case T_DRAIN: strcpy(s, "FINISHING"); pen = P_AMB; break;
    default: {
        int pass = test_pass();
        sprintf(s, "%s   %lu:%02lu", pass ? "PASS" : "FAIL", (unsigned long)(secs / 60u), (unsigned long)(secs % 60u));
        pen = pass ? P_GRN : P_RED;
    }
    }
    if (pen != pen_old || strcmp(s, head_old)) {
        pen_old = pen; strcpy(head_old, s);
        panel_text(rp, A_X + 11, A_Y + ly->pan, A_W - 21, A_Y + ly->pan_t, pen, s, 1);
    }
}

/* one value of the table: 0-3 the message counts, 4-7 the round trip */
static void tbl_item(int i)
{
    char s[24];
    ULONG ioe = test_ioerrs();
    switch (i) {
    case 0: fmt_thousands(s, t_sent); tbl_value(0, 0, P_BLACK, s); return;
    case 1: fmt_thousands(s, t_recv); tbl_value(0, 1, P_BLACK, s); return;
    case 2: fmt_thousands(s, t_lost); tbl_value(0, 2, t_lost ? P_TXT_RED : P_BLACK, s); return;
    case 3: fmt_thousands(s, t_err + ioe); tbl_value(0, 3, (t_err || ioe) ? P_TXT_RED : P_BLACK, s);
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

/* the port activity table: bytes out and in, overruns and overflows (the driver's read buffer and the program's
 * ring), parity errors and breaks; the field widths leave 8 pixels after the longest label */
static const char *const pc_lbl[3][2] = { { "Out", "In" }, { "Overruns", "Overflows" }, { "Parity", "Breaks" } };
static const int pc_w[3][2] = { { 104, 112 }, { 64, 56 }, { 80, 80 } };
static char pc_old[3][2][24];

static void pc_row(int row)
{
    static int pen_old[3][2];
    char s[24];
    int c;
    for (c = 0; c < 2; c++) {
        ULONG v = row == 0 ? (c ? n_rx : n_tx) : row == 1 ? (c ? n_full : n_ovr) : (c ? n_brk : n_par);
        int pen = (row && v) ? P_TXT_RED : P_BLACK;
        if (pen_old[row][c] != pen) { pc_old[row][c][0] = 0; pen_old[row][c] = pen; }
        fmt_thousands(s, v);
        value_r(pc_old[row][c], PC_R(c), PC_Y(row), pc_w[row][c], pen, s);
    }
}

/* the readouts in turn, one an update (each about 4 times a second, the rate of a meter's figures): the two meter
 * figures, the eight table values, the three rows of port counters, the monitor's scroller */
#define SLOW_N 14
static void mon_scroller(void);
static void slow_item(int i)
{
    if (i < 2) meter_fig(i);
    else if (i < 10) tbl_item(i - 2);
    else if (i < 13) pc_row(i - 10);
    else mon_scroller();
}

/* the heading of the meters: Paula's rate for the baud rate */
static void b_heading(void)
{
    char s[48], r[16];
    fmt_thousands(r, paula_baud);
    snprintf(s, sizeof s, "Wire bit/s (Paula %s baud)", r);
    SetAPen(rp, P_BG); RectFill(rp, B_X + 6, A_Y + ly->bhead, B_X + A_W - 7, A_Y + ly->bhead + 7);
    text_c(rp, b_cx, A_Y + ly->bhead, P_TXT_BLUE, s);
}

static void controls_state(void)
{
    int run = t_state == T_RUN || t_state == T_DRAIN, i, blk = test_block();
    if (!win) return;                                      /* NOWINDOW */
    btn[B_RUN].label = t_state == T_RUN ? "Stop" : "Start";
    btn[B_RUN].icon = t_state == T_RUN ? ICON_STOP : ICON_PLAY;
    btn[B_RUN].drawn = -1;
    btn_set(&btn[B_RUN], 0, t_state == T_DRAIN || (t_state != T_RUN && blk));
    for (i = 0; i < 4; i++) btn_set(&btn[B_L25 - i], t_rate == i, run);
    btn_set(&btn[B_SEND], 0, run || !s_open);
    btn_set(&btn[B_BRK], 0, run || !s_open);
    btn_set(&btn[B_ASC], !s_hex, 0);
    btn_set(&btn[B_HEX], s_hex, 0);
    btn_set(&btn[B_XFER], 0, run);
}

static void test_start(void)
{
    int n;
    if (t_state == T_RUN || t_state == T_DRAIN || test_block()) return;
    tl_flush();
    for (n = 0; !tx_empty() && n < 150; n++) { Delay(1); ser_tick(); }   /* bytes from the send line leave first */
    {   /* bytes still on their way from an earlier run or program (an adapter or a bridge may hold them until more
         * bytes follow) pushed through by 128 filler bytes; once 32 of them are back and the line is quiet for
         * 100 ms (at most 3 s) what arrived is discarded below */
        UWORD w, r = rx_w; int quiet = 0, fe = 0;
        tx_filler(128u);
        n = 0;
        do {                                               /* the filler back (the bytes before it are in), then quiet */
            UWORD w0 = rx_w;
            Delay(1);
            ser_tick();
            for (w = rx_w; r != w; r = (UWORD)((r + 1u) & RMASK)) if (rxq[r] == 0xFEu) fe++;
            quiet = fe >= 32 && rx_w == w0 && tx_empty() ? quiet + 1 : 0;
        } while (quiet < 5 && ++n < 150);
    }
    rx_r = rx_w; tx_sn = 0;
    p_n = 0; p_sx = 0; p_rs = 0;
    t_seq = t_sent = t_exp = t_recv = t_lost = t_err = t_lat_n = t_lat_max = 0; t_cand = ~0ul; t_lat_min = ~0ul;
    elog_n = 0;
    t_lat_sum = t_lat_sq = 0;
    t_ovr0 = n_ovr; t_full0 = n_full; t_par0 = n_par; t_brk0 = n_brk; t_io0 = n_ioerr; t_tx0 = n_tx; t_rx0 = n_rx;
    t_tok = 0; t_rem = 0; t_drain_set = 0;
    t_e0 = t_last_e = eclock();
    t_state = T_RUN; t_started = 1;
    controls_state();
}

static void test_stop(void)
{
    if (t_state != T_RUN) return;
    t_e1 = eclock();
    t_tx1 = n_tx; t_rx1 = n_rx;
    t_state = T_DRAIN;
    controls_state();
}

static void test_tick(unsigned long long now)
{
    UBYTE m[16];
    unsigned int n;
    if (t_state == T_DRAIN) {
        int stuck = now - t_e1 > (unsigned long long)efreq * 10u;   /* the driver has not sent the rest in 10 s */
        if (tx_empty() || stuck) {
            if (!t_drain_set) { t_drain_set = 1; t_drain_e = stuck ? now : now + efreq * 2u; }   /* 2 s from the end */
            if (t_recv < t_sent && now < t_drain_e) tx_filler(8u);   /* the last messages pushed out of an adapter */
            else {
            t_lost = t_sent > t_recv ? t_sent - t_recv : 0;    /* every message not received intact */
            t_state = T_DONE;
            if (stuck && !tx_empty()) notice(1, "The driver did not send the last bytes within 10 s");
            controls_state();
            }
        }
    }
    if (t_state != T_RUN) return;
    /* the token bucket: bytes x 16, at most 250 ms of bytes in hand, so a late update loses none.  The rate is the
     * wire's byte rate times the load, in sixteenths of a byte a second (r16); time in units of 16 E clock cycles,
     * the quotient and the remainder of r16 by that unit's rate kept apart so every product fits 32 bits */
    {
        ULONG e16 = efreq >> 4, dt = (ULONG)(now - t_last_e) >> 4, r16, num;
        if (dt > e16) dt = e16;
        r16 = wire_bps * 16u * rate_pct[t_rate] / 100u;
        num = dt * (r16 % e16) + t_rem;
        t_tok += dt * (r16 / e16) + num / e16; t_rem = num % e16;
        t_last_e = now;
        if (t_tok > wire_bps * 16u / 4u) t_tok = wire_bps * 16u / 4u;
    }
    /* this update's messages into one buffer, then into the ring at once (one call to the driver) */
    {
        static UBYTE buf[1024];
        unsigned int o = 0, free = tx_free();
        for (;;) {
            n = test_msg(t_seq, m);
            if (t_tok < n * 16u || o + n > free || o + n > sizeof buf) break;
            memcpy(buf + o, m, n); o += n;
            t_tok -= n * 16u;
            if (show_test && test_shown_out < 1) { test_shown_out++; mon_add(beam_now(), 1, m, n); }
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

/* the menus: Project, Settings (check marks), Test (its length, one of four) */
enum { MI_RUN = 1, MI_BREAK, MI_CLEAR, MI_QUIT, MI_ABOUT, MI_CLRBUF, MI_DRIVER, MI_XFER, MI_TERM,
       MI_EXPUNGE = 10, MI_BOOGIE, MI_SHOWTEST, MI_ZAUTO, MI_ZFOLDER, MI_UTF8, MI_DUR = 20 };
static const ULONG dur_tab[4] = { 0, 10, 60, 600 };
static struct NewMenu nm[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Start or Stop Test", (STRPTR)"S", 0, 0, (APTR)MI_RUN },
    { NM_ITEM, (STRPTR)"Send Break", (STRPTR)"B", 0, 0, (APTR)MI_BREAK },
    { NM_ITEM, (STRPTR)"Clear Monitor", (STRPTR)"K", 0, 0, (APTR)MI_CLEAR },
    { NM_ITEM, (STRPTR)"Clear Driver Buffer", NULL, 0, 0, (APTR)MI_CLRBUF },
    { NM_ITEM, (STRPTR)"Other Driver...", (STRPTR)"D", 0, 0, (APTR)MI_DRIVER },
    { NM_ITEM, (STRPTR)"File Transfer...", (STRPTR)"F", 0, 0, (APTR)MI_XFER },
    { NM_ITEM, (STRPTR)"Terminal", (STRPTR)"V", 0, 0, (APTR)MI_TERM },
    { NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)MI_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)MI_QUIT },
    { NM_TITLE, (STRPTR)"Settings", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Expunge Driver on Switch", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)MI_EXPUNGE },
    { NM_ITEM, (STRPTR)"High-Speed Mode (RAD_BOOGIE)", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)MI_BOOGIE },
    { NM_ITEM, (STRPTR)"Show Test Data", (STRPTR)"T", CHECKIT | MENUTOGGLE, 0, (APTR)MI_SHOWTEST },
    { NM_ITEM, (STRPTR)"Receive ZMODEM Automatically", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)MI_ZAUTO },
    { NM_ITEM, (STRPTR)"ZMODEM Download Folder...", NULL, 0, 0, (APTR)MI_ZFOLDER },
    { NM_ITEM, (STRPTR)"Terminal Characters UTF-8", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)MI_UTF8 },
    { NM_TITLE, (STRPTR)"Test", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Until Stopped", NULL, CHECKIT, 14, (APTR)(MI_DUR + 0) },
    { NM_ITEM, (STRPTR)"10 Seconds", NULL, CHECKIT, 13, (APTR)(MI_DUR + 1) },
    { NM_ITEM, (STRPTR)"60 Seconds", NULL, CHECKIT, 11, (APTR)(MI_DUR + 2) },
    { NM_ITEM, (STRPTR)"10 Minutes", NULL, CHECKIT, 7, (APTR)(MI_DUR + 3) },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* the check mark of menu item `id` */
static void menu_check(ULONG id, int on)
{
    struct Menu *m;
    struct MenuItem *it;
    if (!menu || !win) return;
    ClearMenuStrip(win);
    for (m = menu; m; m = m->NextMenu)
        for (it = m->FirstItem; it; it = it->NextItem)
            if ((ULONG)GTMENUITEM_USERDATA(it) == id) { if (on) it->Flags |= CHECKED; else it->Flags &= ~CHECKED; }
    ResetMenuStrip(win, menu);
}

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

/* the port group's row centred: "Driver" 48, 6, cycle 168; 12; "Baud" 32, 6, 72; 12; "Data" 32, 6, 56; 12; "Flow"
 * 32, 6, 92 (592 from x 24).  Group A's controls row centred (Start/Stop 88, 16, "Load %" and 6, four segments of
 * 32).  The monitor's row (592 from x 24 at 640 wide; the send line takes the width above or below that): the send
 * line 100, 4, Send 48; 12; ASCII and Hex 48 each; 12; Break, Clear, Pause 48 each with 4 between; 12; Transfer...
 * 96; 12; Quit 48 */
#define PX_DRV  SX(78)
#define PX_BAUD SX(296)
#define PX_FMT  SX(418)
#define PX_FLOW SX(524)
static int build_main(void)
{
    static const char *const seg[4] = { "25", "50", "75", "100" };
    struct Gadget *g;
    int x, i, by, ex;
    x = a_cx - (88 + 16 + 48 + 6 + 4 * 32) / 2;
    btn_init(&btn[B_RUN], BTN_ID + B_RUN, x, A_Y + ly->btn, 88, 14, "Start", ICON_PLAY);
    for (i = 0; i < 4; i++)
        btn_init(&btn[B_L25 - i], BTN_ID + B_L25 - i, x + 88 + 16 + 48 + 6 + i * 32, A_Y + ly->btn, 32, 14, seg[i], ICON_NONE);
    by = M_Y + m_h - ly->m_by;
    lv_h = by - ly->lv_gap - LV_Y;
    ex = win_w - WIN_W;                                    /* the send line's extra width */
    x = ox + 24;
    btn_init(&btn[B_SEND], BTN_ID + B_SEND, x + ex + 104, by, 48, 14, "Send", ICON_NONE);
    btn_init(&btn[B_ASC], BTN_ID + B_ASC, x + ex + 164, by, 48, 14, "ASCII", ICON_NONE);
    btn_init(&btn[B_HEX], BTN_ID + B_HEX, x + ex + 212, by, 48, 14, "Hex", ICON_NONE);
    btn_init(&btn[B_BRK], BTN_ID + B_BRK, x + ex + 272, by, 48, 14, "Break", ICON_NONE);
    btn_init(&btn[B_CLEAR], BTN_ID + B_CLEAR, x + ex + 324, by, 48, 14, "Clear", ICON_NONE);
    btn_init(&btn[B_PAUSE], BTN_ID + B_PAUSE, x + ex + 376, by, 48, 14, "Pause", ICON_NONE);
    btn_init(&btn[B_XFER], BTN_ID + B_XFER, x + ex + 436, by, 96, 14, "Transfer...", ICON_NONE);
    btn_init(&btn[B_TERM], BTN_ID + B_TERM, LV_X + LV_W - 96, M_Y + ly->m_hd - 1, 96, 14, "Terminal", ICON_NONE);
    btn_init(&btn[B_QUIT], BTN_ID + B_QUIT, x + ex + 544, by, 48, 14, "Quit", ICON_NONE);
    if (!(g = CreateContext(&glist))) return 0;
    mon_x = LV_X + 4; mon_y = LV_Y + 3; mon_rows = (lv_h - 6) / 8; mon_cols = (LV_W - 18 - 8) / 8;
    if (mon_cols > MON_COLS_MAX) mon_cols = MON_COLS_MAX;
    if (mon_rows > MON_ROWS_MAX) mon_rows = MON_ROWS_MAX;
    if (mon_rows < 1) return 0;
    {
        struct NewGadget ng;
        memset(&ng, 0, sizeof ng);
        ng.ng_LeftEdge = LV_X + LV_W - 18; ng.ng_TopEdge = LV_Y; ng.ng_Width = 18; ng.ng_Height = lv_h;
        ng.ng_TextAttr = &topaz8; ng.ng_GadgetID = G_SCR; ng.ng_VisualInfo = vi;
        g = gad[G_SCR] = CreateGadget(SCROLLER_KIND, g, &ng, PGA_Freedom, LORIENT_VERT, GTSC_Arrows, 8,
                                      GTSC_Visible, (ULONG)mon_rows, GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
    }
    g = mk(CYCLE_KIND, g, G_DRV, PX_DRV, P_GY, 168, 12, NULL, 0, GTCY_Labels, (ULONG)drv_lbl, GTCY_Active,
           (ULONG)s_drv);
    g = mk(CYCLE_KIND, g, G_BAUD, PX_BAUD, P_GY, 72, 12, NULL, 0, GTCY_Labels, (ULONG)baud_lbl, GTCY_Active,
           (ULONG)s_bi);
    g = mk(CYCLE_KIND, g, G_FMT, PX_FMT, P_GY, 56, 12, NULL, 0, GTCY_Labels, (ULONG)fmt_lbl, GTCY_Active,
           (ULONG)s_fmt);
    g = mk(CYCLE_KIND, g, G_FLOW, PX_FLOW, P_GY, 92, 12, NULL, 0, GTCY_Labels, (ULONG)flow_lbl, GTCY_Active,
           (ULONG)s_flow);
    g = mk(STRING_KIND, g, G_LINE, x, by, 100 + ex, 14, NULL, 0, GTST_MaxChars, 200, TAG_IGNORE, 0);
    return g != NULL;
}

static void draw_main(void)
{
    char hdr[96];
    int i, c, cx;
    st_redraw = 1;
    group(P_X, P_Y, P_W, P_H, "Port");
    group(A_X, A_Y, A_W, A_H, "Loopback Test");
    group(B_X, A_Y, A_W, A_H, "Port Activity");
    group(M_X, M_Y, M_W, m_h, "Monitor");
    /* the port group: the labels, right-aligned 6 pixels left of their gadgets */
    text_r(rp, PX_DRV - 6, P_GY + 2, P_BLACK, "Driver");
    text_r(rp, PX_BAUD - 6, P_GY + 2, P_BLACK, "Baud");
    text_r(rp, PX_FMT - 6, P_GY + 2, P_BLACK, "Data");
    text_r(rp, PX_FLOW - 6, P_GY + 2, P_BLACK, "Flow");
    /* group A: the load selector's label, the result panel, the two-column table */
    pen_text(rp, btn[B_RUN].g.LeftEdge + 88 + 16, A_Y + ly->load, P_BLACK, "Load %");
    panel(rp, A_X + 11, A_Y + ly->pan, A_W - 21, ly->pan_h);
    for (c = 0; c < 2; c++) {
        cx = (COL_L(c) + COL_R(c)) / 2;
        text_c(rp, cx, A_Y + ly->head, P_TXT_BLUE, c ? "Round trip" : "Messages");
        SetAPen(rp, P_GREY); RectFill(rp, COL_L(c), A_Y + ly->rule, COL_R(c) - 1, A_Y + ly->rule);
        for (i = 0; i < 4; i++) { pen_text(rp, COL_L(c), ROW_Y(i), P_BLACK, tbl_lbl[c][i]); tbl_old[c][i][0] = 0x7F; }
    }
    /* group B: the two meters, the modem lines, the counters */
    bar_x = (b_cx - (24 + 10 + SEGS * 4 + 4 + 8 + FIG_W) / 2 + 24 + 10) & ~3;
    b_heading();
    text_r(rp, bar_x - 10, BAR_Y(0), P_BLACK, "Out");
    text_r(rp, bar_x - 10, BAR_Y(1), P_BLACK, "In");
    for (i = 0; i < 2; i++) meter_frame(i);
    leds_frame();
    for (i = 0; i < 3; i++)
        for (c = 0; c < 2; c++) { pen_text(rp, PC_L(c), PC_Y(i), P_BLACK, pc_lbl[i][c]); pc_old[i][c][0] = 0x7F; }
    /* the monitor: its column heads on a raised bar, on the view's character grid, and the view's panel */
    panel(rp, LV_X, LV_Y, LV_W - 18, lv_h);
    for (i = 0; i < MON_ROWS_MAX; i++) mon_row_line[i] = -1;   /* the panel is blank */
    DrawBevelBox(rp, LV_X, M_Y + ly->m_hd, LV_W - 100, 12, GT_VisualInfo, vi, TAG_DONE);
    sprintf(hdr, MON_FMT, "Time", "Dir", "Data");
    pen_text(rp, LV_X + 4, M_Y + ly->m_hd + 2, P_TXT_BLUE, hdr);
}

/* ---- section 6: file transfers ---------------------------------------------------------------------------------
 * The transfer panel sends or receives a file.  ZMODEM, YMODEM, XMODEM and XMODEM-1K run as C:lsz or C:lrz
 * (amiga/tools/xfer, lrzsz 0.12.20) in a process of their own: the port is closed here first and opened again when
 * the transfer has ended; the program passes the driver, unit, rate, read buffer and RTS/CTS of the port group, and
 * the address of a status block (zio_status.h) in which the transfer counts the bytes it reads and writes and at its
 * end sets done and its exit code.  Cancel sends the transfer Ctrl-C (it sends the cancel string and exits).  Its
 * messages go to T:serialmon-xfer.log; the last lines of it come to the monitor at the end.
 * Raw text is sent by this program through the open port: the file's bytes with its line ends as they are or
 * replaced by CR, LF or CR LF, optionally paced by a delay after each character and after each line. */

enum { XP_Z, XP_Y, XP_X, XP_X1K, XP_RAW };
static const char *const xd_lbl[] = { "Send", "Receive", NULL };
static const char *const xp_lbl_tx[] = { "ZMODEM", "YMODEM", "XMODEM", "XMODEM-1K", "Raw text", NULL };
static const char *const xp_lbl_rx[] = { "ZMODEM", "YMODEM", "XMODEM", NULL };
static const char *const xe_lbl[] = { "As in file", "CR", "LF", "CR LF", NULL };
enum { XG_DIR, XG_PROTO, XG_FILE, XG_CRC, XG_EOL, XG_CDL, XG_LDL, XG_N };
enum { XB_CHOOSE, XB_START, XB_CLOSE, XB_N };
#define XBTN_ID  300
#define X_W      456                                       /* the panel's width */
#define X_LOG    "T:serialmon-xfer.log"

static struct Window *xwin;
static struct Gadget *xglist, *xgad[XG_N];
static btn_t          xbtn[XB_N];
static int            x_dir, x_proto, x_crc = 1, x_eol, x_blocked;
static LONG           x_cdl, x_ldl;                        /* delays, ms */
static char           x_path[2][256] = { "RAM:", "" };  /* the file to send, the folder (or XMODEM file) to receive into;
                                                              * an empty folder: the current directory */
static int            x_gx, x_lx, x_row[6], x_hy, x_by, x_h;
enum { XS_SETUP, XS_RUN, XS_DONE };
static int            x_state;
static int            x_raw;                               /* the running transfer is raw text */
static zio_status_t  *x_st;                                /* the running lsz/lrz's status block */
static unsigned long long x_e0, x_e1, x_cancel_e;
static ULONG          x_size, x_done_b;                    /* bytes of the file; bytes sent (raw) */
static ULONG          x_rx0;                               /* n_rx at the start of a raw send */
static int            x_ok, x_item, x_cancelled;
static char           x_title[96];
/* raw text */
static BPTR           x_fh;
static UBYTE          x_buf[1024];
static LONG           x_bn, x_bi;
static int            x_eof;
static unsigned long long x_due;

static int x_recv_named(void) { return x_dir == 1 && x_proto == XP_X; }   /* XMODEM sends no file name */

static void x_hint(char *s, size_t n)
{
    if (x_dir == 0 && x_proto == XP_RAW) snprintf(s, n, "Delays 0: as fast as the port takes them");
    else if (x_dir == 0) snprintf(s, n, "Sent by C:lsz through %s", drv_txt[s_drv]);
    else if (x_proto == XP_X) snprintf(s, n, "CRC-16 off: checksum, as an HP 48 sends");
    else snprintf(s, n, "Empty folder: the current directory; a file kept");
}

/* the panel's gadgets for the current choice (made again when it changes; same window size) */
static int x_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    int raw = x_dir == 0 && x_proto == XP_RAW;
    if (xglist) { RemoveGList(xwin, xglist, -1); FreeGadgets(xglist); xglist = NULL; }
    memset(xgad, 0, sizeof xgad);
    if (!(g = CreateContext(&xglist))) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi; ng.ng_TextAttr = &topaz8; ng.ng_LeftEdge = x_gx; ng.ng_Height = 12;
    ng.ng_TopEdge = x_row[0]; ng.ng_Width = 120; ng.ng_GadgetID = XG_DIR;
    xgad[XG_DIR] = g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)xd_lbl, GTCY_Active, (ULONG)x_dir, TAG_DONE);
    ng.ng_TopEdge = x_row[1]; ng.ng_Width = 136; ng.ng_GadgetID = XG_PROTO;
    xgad[XG_PROTO] = g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)(x_dir ? xp_lbl_rx : xp_lbl_tx),
                                      GTCY_Active, (ULONG)x_proto, TAG_DONE);
    ng.ng_TopEdge = x_row[2] - 1; ng.ng_Width = 232; ng.ng_Height = 14; ng.ng_GadgetID = XG_FILE;
    xgad[XG_FILE] = g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)x_path[x_dir], GTST_MaxChars,
                                     sizeof x_path[0] - 1, TAG_DONE);
    ng.ng_Height = 12;
    if (x_recv_named()) {
        ng.ng_TopEdge = x_row[3]; ng.ng_Width = 26; ng.ng_Height = 11; ng.ng_GadgetID = XG_CRC;
        ng.ng_GadgetText = (UBYTE *)"CRC-16"; ng.ng_Flags = PLACETEXT_RIGHT;
        xgad[XG_CRC] = g = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)x_crc, TAG_DONE);
        ng.ng_GadgetText = NULL; ng.ng_Flags = 0; ng.ng_Height = 12;
    }
    if (raw) {
        ng.ng_TopEdge = x_row[3]; ng.ng_Width = 120; ng.ng_GadgetID = XG_EOL;
        xgad[XG_EOL] = g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)xe_lbl, GTCY_Active, (ULONG)x_eol, TAG_DONE);
        ng.ng_TopEdge = x_row[4] - 1; ng.ng_Width = 56; ng.ng_Height = 14; ng.ng_GadgetID = XG_CDL;
        xgad[XG_CDL] = g = CreateGadget(INTEGER_KIND, g, &ng, GTIN_Number, (ULONG)x_cdl, GTIN_MaxChars, 4, TAG_DONE);
        ng.ng_TopEdge = x_row[5] - 1; ng.ng_GadgetID = XG_LDL;
        xgad[XG_LDL] = g = CreateGadget(INTEGER_KIND, g, &ng, GTIN_Number, (ULONG)x_ldl, GTIN_MaxChars, 5, TAG_DONE);
    }
    if (!g) { FreeGadgets(xglist); xglist = NULL; return 0; }
    AddGList(xwin, xglist, (UWORD)~0, -1, NULL);
    return 1;
}

/* the panel's buttons out of its window before they are made again (a gadget added twice corrupts the list) */
static void x_btns_off(void)
{
    int i;
    for (i = 0; i < XB_N; i++) if (xwin && xbtn[i].w == xwin) { RemoveGadget(xwin, &xbtn[i].g); xbtn[i].w = NULL; }
}

/* the setup view: the labels drawn here, right-aligned 8 pixels left of the gadgets; the hint; Choose, Start, Close */
static void x_draw_setup(void)
{
    struct RastPort *r = xwin->RPort;
    char h[64];
    int raw = x_dir == 0 && x_proto == XP_RAW;
    x_btns_off();
    SetAPen(r, P_BG);
    RectFill(r, xwin->BorderLeft, xwin->BorderTop, xwin->Width - xwin->BorderRight - 1, xwin->Height - xwin->BorderBottom - 1);
    if (!x_gadgets()) return;
    RefreshGList(xglist, xwin, NULL, -1);
    GT_RefreshWindow(xwin, NULL);
    text_r(r, x_lx, x_row[0] + 2, P_BLACK, "Direction");
    text_r(r, x_lx, x_row[1] + 2, P_BLACK, "Protocol");
    text_r(r, x_lx, x_row[2] + 2, P_BLACK, x_dir == 0 ? "File" : x_recv_named() ? "Save as" : "Folder");
    if (raw) {
        text_r(r, x_lx, x_row[3] + 2, P_BLACK, "Line end");
        text_r(r, x_lx, x_row[4] + 2, P_BLACK, "Delay");
        pen_text(r, x_gx + 64, x_row[4] + 2, P_BLACK, "ms after a character");
        text_r(r, x_lx, x_row[5] + 2, P_BLACK, "Delay");
        pen_text(r, x_gx + 64, x_row[5] + 2, P_BLACK, "ms after a line");
    }
    x_hint(h, sizeof h);
    pen_text(r, x_gx, x_hy, P_TXT_BLUE, h);
    btn_init(&xbtn[XB_CHOOSE], XBTN_ID + XB_CHOOSE, x_gx + 240, x_row[2] - 1, 80, 14, "Choose...", ICON_NONE);
    btn_init(&xbtn[XB_START], XBTN_ID + XB_START, X_W / 2 - 8 - 96, x_by, 96, 14, "Start", ICON_PLAY);
    btn_init(&xbtn[XB_CLOSE], XBTN_ID + XB_CLOSE, X_W / 2 + 8, x_by, 96, 14, "Close", ICON_NONE);
    btn_add(xbtn, XB_N, xwin);
}

/* the running view: a dark panel with the transfer's lines (drawn by x_tick), and one button */
static void x_draw_run(void)
{
    struct RastPort *r = xwin->RPort;
    x_btns_off();
    if (xglist) { RemoveGList(xwin, xglist, -1); FreeGadgets(xglist); xglist = NULL; }
    memset(xgad, 0, sizeof xgad);
    SetAPen(r, P_BG);
    RectFill(r, xwin->BorderLeft, xwin->BorderTop, xwin->Width - xwin->BorderRight - 1, xwin->Height - xwin->BorderBottom - 1);
    panel(r, 16, x_row[0] - 2, X_W - 32, x_hy + 10 - x_row[0]);
    btn_init(&xbtn[XB_CLOSE], XBTN_ID + XB_CLOSE, X_W / 2 - 48, x_by, 96, 14, x_state == XS_RUN ? "Cancel" : "Close",
             x_state == XS_RUN ? ICON_STOP : ICON_NONE);
    xbtn[XB_CLOSE].w = xwin; AddGadget(xwin, &xbtn[XB_CLOSE].g, ~0); btn_draw(&xbtn[XB_CLOSE]);
    x_item = -1;
}

/* one line of the running view, padded to the panel's width */
static void x_line(int row, int pen, const char *s)
{
    struct RastPort *r = xwin->RPort;
    char b[56];
    int n = (int)strlen(s), w = (X_W - 48) / 8;            /* the panel's inner width in characters */
    if (n > w) n = w;
    memset(b, ' ', (size_t)w); memcpy(b, s, (size_t)n); b[w] = 0;
    SetAPen(r, pen); SetBPen(r, P_TROUGH); SetDrMd(r, JAM2);
    Move(r, 24, x_row[row] + 1 + r->TxBaseline); Text(r, (CONST_STRPTR)b, w);
}

static void x_close(void)
{
    if (!xwin) return;
    if (x_state == XS_RUN) return;                         /* a transfer runs: the panel stays */
    x_btns_off();
    if (xglist) { RemoveGList(xwin, xglist, -1); FreeGadgets(xglist); xglist = NULL; }
    CloseWindow(xwin); xwin = NULL;
    if (x_blocked) { EndRequest(&block_req, win); x_blocked = 0; }
    SetWindowPointer(win, TAG_DONE);
}

static int x_open(void)
{
    int bt = scr->WBorTop + scr->Font->ta_YSize + 1, i;
    if (xwin || mwin || awin) return 1;
    if (t_state == T_RUN || t_state == T_DRAIN) { notice(1, "No transfer while the test runs"); return 0; }
    x_lx = scr->WBorLeft + 96; x_gx = x_lx + 8;
    for (i = 0; i < 6; i++) x_row[i] = bt + 8 + i * 18;
    x_hy = x_row[5] + 18; x_by = x_hy + 16; x_h = x_by + 14 + 8 + scr->WBorBottom;
    xwin = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, (ULONG)((scr->Width - X_W) / 2),
                          WA_Top, (ULONG)((scr->Height - x_h) / 2), WA_Width, X_W, WA_Height, (ULONG)x_h,
                          WA_Title, (ULONG)"File Transfer", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                          WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, CYCLEIDCMP | STRINGIDCMP | INTEGERIDCMP | CHECKBOXIDCMP | IDCMP_CLOSEWINDOW |
                                    IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_DONE);
    if (!xwin) return 0;
    SetFont(xwin->RPort, font);
    x_state = XS_SETUP;
    x_draw_setup();
    InitRequester(&block_req);
    if (Request(&block_req, win)) x_blocked = 1;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    return 1;
}

/* the panel's fields into the settings */
static void x_take(void)
{
    if (xgad[XG_FILE]) {
        strncpy(x_path[x_dir], (const char *)((struct StringInfo *)xgad[XG_FILE]->SpecialInfo)->Buffer, sizeof x_path[0] - 1);
        x_path[x_dir][sizeof x_path[0] - 1] = 0;
    }
    if (xgad[XG_CDL]) { x_cdl = ((struct StringInfo *)xgad[XG_CDL]->SpecialInfo)->LongInt; if (x_cdl < 0) x_cdl = 0; }
    if (xgad[XG_LDL]) { x_ldl = ((struct StringInfo *)xgad[XG_LDL]->SpecialInfo)->LongInt; if (x_ldl < 0) x_ldl = 0; }
}

/* the ASL requester for the file or folder */
static void x_choose(void)
{
    struct Library *AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 39);
    struct FileRequester *fr;
    char dir[256], *p;
    if (!AslBase) { notice(1, "asl.library 39 did not open"); return; }
    x_take();
    strcpy(dir, x_path[x_dir]);
    p = (char *)FilePart((STRPTR)dir);
    if (x_dir == 0 || x_recv_named()) *p = 0;
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASLFR_Screen, (ULONG)scr,
             ASLFR_TitleText, (ULONG)(x_dir == 0 ? "File to Send" : x_recv_named() ? "Save Received File As" : "Folder for Received Files"),
             ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)(x_dir == 0 || x_recv_named() ? FilePart((STRPTR)x_path[x_dir]) : (STRPTR)""),
             ASLFR_DrawersOnly, (ULONG)(x_dir == 1 && !x_recv_named()), ASLFR_DoSaveMode, (ULONG)x_recv_named(),
             ASLFR_SleepWindow, TRUE, ASLFR_Window, (ULONG)xwin, TAG_DONE);
    if (fr) {
        if (AslRequest(fr, NULL)) {
            strncpy(x_path[x_dir], (const char *)fr->fr_Drawer, sizeof x_path[0] - 1);
            x_path[x_dir][sizeof x_path[0] - 1] = 0;
            if (x_dir == 0 || x_recv_named()) AddPart((STRPTR)x_path[x_dir], fr->fr_File, sizeof x_path[0]);
            GT_SetGadgetAttrs(xgad[XG_FILE], xwin, NULL, GTST_String, (ULONG)x_path[x_dir], TAG_DONE);
        }
        FreeAslRequest(fr);
    }
    CloseLibrary(AslBase);
}

static void x_finish(int ok, const char *why);
static void after_change(void);

/* the last lines of the transfer's log into the monitor */
static void x_log_tail(int err)
{
    BPTR fh = Open((CONST_STRPTR)X_LOG, MODE_OLDFILE);
    static char b[2048];
    LONG n, i, end, k = 0, st[4];
    if (!fh) return;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n > (LONG)sizeof b - 1) Seek(fh, n - (LONG)sizeof b + 1, OFFSET_BEGINNING);
    n = Read(fh, b, sizeof b - 1);
    Close(fh);
    if (n <= 0) return;
    b[n] = 0;
    for (i = 0; i < n; i++) if (b[i] == '\r' || b[i] == '\n') b[i] = 0;
    /* the starts of the last 4 non-empty lines, oldest first */
    for (end = n; end > 0 && k < 4; ) {
        while (end > 0 && !b[end - 1]) end--;
        if (!end) break;
        i = end; while (i > 0 && b[i - 1]) i--;
        st[k++] = i; end = i;
    }
    while (k--) notice(err, "%s: %s", x_dir ? "lrz" : "lsz", b + st[k]);
}

static void x_start(void)
{
    char cmd[640], opt[48];
    const char *prog = x_dir ? "C:lrz" : "C:lsz";
    BPTR lock, in, out, old = 0;
    LONG rc;
    x_take();
    x_raw = x_dir == 0 && x_proto == XP_RAW;
    x_size = 0;
    if (x_dir == 0) {                                      /* the file's size */
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (!(lock = Lock((CONST_STRPTR)x_path[0], ACCESS_READ))) { notice(1, "%s: not found", x_path[0]); return; }
        if (fib && Examine(lock, fib)) x_size = (ULONG)fib->fib_Size;
        if (fib && fib->fib_DirEntryType > 0) { UnLock(lock); FreeDosObject(DOS_FIB, fib); notice(1, "%s is a folder", x_path[0]); return; }
        UnLock(lock);
        if (fib) FreeDosObject(DOS_FIB, fib);
    }
    snprintf(x_title, sizeof x_title, "%s %s  %s", x_dir ? xp_lbl_rx[x_proto] : xp_lbl_tx[x_proto], x_dir ? "receive" : "send",
             x_dir == 1 && !x_recv_named() ? (x_path[1][0] ? "into the folder" : "into the current directory")
                                           : (const char *)FilePart((STRPTR)x_path[x_dir]));
    if (x_raw) {
        if (!s_open) { notice(1, "Raw text needs the port open"); return; }
        if (!(x_fh = Open((CONST_STRPTR)x_path[0], MODE_OLDFILE))) { notice(1, "%s did not open", x_path[0]); return; }
        x_bn = x_bi = 0; x_eof = 0; x_done_b = 0; x_due = 0; x_rx0 = n_rx;
        notice(0, "Raw text send of %s, %lu bytes", x_path[0], (unsigned long)x_size);
    } else {
        if (!(x_st = (zio_status_t *)AllocMem(sizeof *x_st, MEMF_PUBLIC | MEMF_CLEAR))) { notice(1, "out of memory"); return; }
        x_st->magic = ZIO_STATUS_MAGIC;
        switch (x_proto) {
        case XP_Y:   strcpy(opt, x_dir ? "--ymodem -E" : "--ymodem"); break;
        case XP_X:   strcpy(opt, x_dir ? (x_crc ? "-X -c" : "-X") : "-X"); break;
        case XP_X1K: strcpy(opt, "-X -k"); break;
        default:     strcpy(opt, x_dir ? "-E" : ""); break;
        }
        snprintf(cmd, sizeof cmd, "%s DEVICE=%s UNIT=%ld BAUD=%lu BUFFER=%lu%s STATUS=0x%08lx LOG=%s -v %s%s%s%s",
                 prog, drv_name[s_drv], (long)drv_unit[s_drv], (unsigned long)s_baud, (unsigned long)s_rbuf,
                 s_flow == FLOW_RTS ? " RTSCTS" : "", (unsigned long)x_st, X_LOG, opt,
                 x_dir == 1 && !x_recv_named() ? "" : " \"", x_dir == 1 && !x_recv_named() ? "" : x_path[x_dir],
                 x_dir == 1 && !x_recv_named() ? "" : "\"");
        if (s_open && !ser_close(0)) { FreeMem(x_st, sizeof *x_st); x_st = NULL; return; }
        if (x_dir == 1 && !x_recv_named() && x_path[1][0]) {   /* empty: the current directory, which lrz inherits */
            if (!(lock = Lock((CONST_STRPTR)x_path[1], ACCESS_READ))) {
                notice(1, "%s: not found", x_path[1]); FreeMem(x_st, sizeof *x_st); x_st = NULL;
                ser_open(s_drv); after_change(); return;
            }
            old = CurrentDir(lock);
        }
        in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE); out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
        rc = SystemTags((CONST_STRPTR)cmd, SYS_Input, (ULONG)in, SYS_Output, (ULONG)out, SYS_Asynch, TRUE,
                        NP_StackSize, 32768, TAG_DONE);
        if (old) { lock = CurrentDir(old); UnLock(lock); }
        if (rc == -1) {
            Close(in); Close(out); FreeMem(x_st, sizeof *x_st); x_st = NULL;
            notice(1, "%s did not start", prog);
            ser_open(s_drv); after_change(); return;
        }
        notice(0, "%s started: %s", prog, x_title);
    }
    x_e0 = eclock(); x_cancel_e = 0; x_state = XS_RUN;
    x_draw_run();
    after_change();
}

/* Cancel: Ctrl-C to the transfer (raw text: stopped here) */
static void x_cancel(void)
{
    if (x_state != XS_RUN) return;
    if (x_raw) { x_finish(0, "cancelled"); return; }
    Forbid();
    if (x_st && x_st->task && !x_st->done) Signal((struct Task *)x_st->task, SIGBREAKF_CTRL_C);
    Permit();
    if (!x_cancel_e) x_cancel_e = eclock();
}

static void x_finish(int ok, const char *why)
{
    char a[16], c[16];
    ULONG ms;
    x_e1 = eclock();
    ms = (ULONG)((x_e1 - x_e0) * 1000ull / efreq);
    x_ok = ok;
    x_cancelled = !ok && (!strcmp(why, "cancelled") || x_cancel_e);
    if (x_raw) {
        if (x_fh) { Close(x_fh); x_fh = 0; }
        fmt_thousands(a, x_done_b); fmt_thousands(c, n_rx - x_rx0);
        notice(!ok, "Raw text %s: %s bytes in %lu.%lu s, %s bytes received meanwhile", why, a,
               (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u / 100u), c);
    } else {
        ULONG b = x_st ? (x_dir ? x_st->in : x_st->out) : 0;
        fmt_thousands(a, b);
        notice(!ok, "%s: %s (%s bytes on the line in %lu.%lu s)", x_title, why, a,
               (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u / 100u));
        x_log_tail(!ok);
        if (x_st && x_st->done) FreeMem(x_st, sizeof *x_st);   /* else the transfer may still write it: kept */
        x_st = NULL;
        if (!s_open) { const char *w = ser_open(s_drv); if (w) notice(1, "%s", w); }
    }
    x_state = XS_DONE;
    if (xwin) {
        x_draw_run();
        x_line(4, ok ? P_GRN : x_cancelled ? P_AMB : P_RED, ok ? "PASS" : x_cancelled ? "CANCELLED" : "FAIL");
    }
    after_change();
}

/* raw text: as many bytes as the transmit ring takes and the delays allow; without delays a block at a time */
static void x_raw_pump(void)
{
    unsigned long long now = eclock();
    UBYTE o[2];
    static UBYTE blk[512];
    while (x_state == XS_RUN && x_raw && !x_cdl && !x_ldl) {
        unsigned int k = 0, room = tx_free();
        if (x_bi >= x_bn) {
            if (x_eof) { if (tx_empty()) x_finish(1, "done"); return; }
            x_bn = Read(x_fh, x_buf, sizeof x_buf); x_bi = 0;
            if (x_bn <= 0) { x_bn = 0; x_eof = 1; }
            continue;
        }
        if (room > sizeof blk) room = sizeof blk;
        if (room < 4u) return;
        while (x_bi < x_bn && k + 2u <= room) {
            UBYTE c = x_buf[x_bi++];
            x_done_b++;
            if (x_eol && c == '\r') continue;
            if (c == '\n' && x_eol) {
                if (x_eol == 1) blk[k++] = '\r';
                else if (x_eol == 2) blk[k++] = '\n';
                else { blk[k++] = '\r'; blk[k++] = '\n'; }
            } else blk[k++] = c;
        }
        if (k) (void)tx_put(blk, k);
    }
    while (x_state == XS_RUN && x_raw) {
        int n = 0, line = 0;
        UBYTE c;
        if (x_due && now < x_due) return;
        x_due = 0;
        if (x_bi >= x_bn) {
            if (x_eof) { if (tx_empty()) x_finish(1, "done"); return; }
            x_bn = Read(x_fh, x_buf, sizeof x_buf); x_bi = 0;
            if (x_bn <= 0) { x_bn = 0; x_eof = 1; continue; }
        }
        if (tx_free() < 4u) return;
        c = x_buf[x_bi++];
        x_done_b++;
        if (x_eol && c == '\r') continue;                  /* converted line ends: the file's CR dropped */
        if (c == '\n') {
            line = 1;
            switch (x_eol) {
            case 0: o[n++] = '\n'; break;
            case 1: o[n++] = '\r'; break;
            case 2: o[n++] = '\n'; break;
            default: o[n++] = '\r'; o[n++] = '\n'; break;
            }
        } else o[n++] = c;
        (void)tx_put(o, (unsigned int)n);
        if (line && x_ldl) x_due = now + (unsigned long long)x_ldl * efreq / 1000u;
        else if (x_cdl) x_due = now + (unsigned long long)x_cdl * efreq / 1000u;
    }
}

/* once an update: the raw text pump, the transfer's end, and one line of the running view */
static void x_tick(void)
{
    char s[64], a[16], b[16];
    ULONG ms, v;
    if (x_state == XS_RUN && x_raw) x_raw_pump();
    if (x_state == XS_RUN && !x_raw && x_st) {
        if (x_st->done) x_finish(x_st->code == 0, x_st->code == 0 ? "done" : x_st->code == 130 ? "cancelled" : "failed");
        else if (x_cancel_e && (eclock() - x_cancel_e) > 10ull * efreq) x_finish(0, "no answer to Ctrl-C after 10 s");
    }
    if (!xwin || x_state == XS_SETUP) return;
    ms = (ULONG)(((x_state == XS_RUN ? eclock() : x_e1) - x_e0) * 1000ull / efreq);
    v = x_raw ? x_done_b : x_st ? (x_dir ? x_st->in : x_st->out) : 0;
    x_item = (x_item + 1) % 4;
    switch (x_item) {
    case 0: x_line(0, P_AMB, x_title); break;
    case 1:
        fmt_thousands(a, v);
        if (x_size && x_dir == 0) { fmt_thousands(b, x_size); snprintf(s, sizeof s, "Bytes %s of %s", a, b); }
        else snprintf(s, sizeof s, "Bytes %s", a);
        x_line(1, P_GRN, s);
        break;
    case 2:
        fmt_thousands(a, ms ? (ULONG)((unsigned long long)v * 1000u / ms) : 0);
        snprintf(s, sizeof s, "Rate %s bytes/s   Time %lu:%02lu", a, (unsigned long)(ms / 60000u),
                 (unsigned long)(ms / 1000u % 60u));
        x_line(2, P_GRN, s);
        break;
    default:
        if (x_size && x_dir == 0) {                         /* the progress bar: the share of the file on the line */
            struct RastPort *r = xwin->RPort;
            int w = X_W - 48 - 4, f = (int)((unsigned long long)(v > x_size ? x_size : v) * (ULONG)w / x_size);
            if (x_state == XS_DONE && x_ok) f = w;
            SetAPen(r, P_BLUE); if (f > 0) RectFill(r, 24, x_row[3] + 2, 24 + f - 1, x_row[3] + 9);
            SetAPen(r, P_TROUGH); if (f < w) RectFill(r, 24 + f, x_row[3] + 2, 24 + w - 1, x_row[3] + 9);
        }
        if (x_state == XS_DONE)
            x_line(4, x_ok ? P_GRN : x_cancelled ? P_AMB : P_RED, x_ok ? "PASS" : x_cancelled ? "CANCELLED" : "FAIL");
        break;
    }
}

/* one event of the panel; 0 when it is to close */
static int x_event(ULONG cls, UWORD code, struct Gadget *g)
{
    switch (cls) {
    case IDCMP_CLOSEWINDOW: if (x_state == XS_RUN) { x_cancel(); break; } return 0;
    case IDCMP_REFRESHWINDOW: GT_BeginRefresh(xwin); GT_EndRefresh(xwin, TRUE); break;
    case IDCMP_VANILLAKEY:
        if (code == 0x1B) { if (x_state == XS_RUN) { x_cancel(); break; } return 0; }
        if (x_state == XS_SETUP && code == '\r') { x_start(); break; }
        if (x_state != XS_RUN && (code | 0x20) == 'c') return 0;
        if (x_state != XS_SETUP) break;
        x_take();
        switch (code | 0x20) {                             /* D direction, P protocol, E line end: the next choice */
        case 'd': x_dir ^= 1; x_proto = 0; x_draw_setup(); break;
        case 'p': x_proto = (x_proto + 1) % (x_dir ? 3 : 5); x_draw_setup(); break;
        case 'e': if (xgad[XG_EOL]) { x_eol = (x_eol + 1) % 4; GT_SetGadgetAttrs(xgad[XG_EOL], xwin, NULL, GTCY_Active, (ULONG)x_eol, TAG_DONE); } break;
        case 'f': {                                        /* the file field, the cursor at its end */
            struct StringInfo *si = (struct StringInfo *)xgad[XG_FILE]->SpecialInfo;
            si->BufferPos = (WORD)strlen((const char *)si->Buffer);
            ActivateGadget(xgad[XG_FILE], xwin, NULL);
        } break;
        }
        break;
    case IDCMP_GADGETDOWN:
        if (g->GadgetID >= XBTN_ID) btn_draw(&xbtn[g->GadgetID - XBTN_ID]);
        break;
    case IDCMP_GADGETUP:
        if (g->GadgetID >= XBTN_ID) {
            int id = g->GadgetID - XBTN_ID;
            btn_draw(&xbtn[id]);
            if (id == XB_CHOOSE) x_choose();
            else if (id == XB_START) x_start();
            else if (x_state == XS_RUN) x_cancel();
            else return 0;
            break;
        }
        x_take();
        switch (g->GadgetID) {
        case XG_DIR:   x_dir = code; x_proto = 0; x_draw_setup(); break;
        case XG_PROTO: x_proto = code; x_draw_setup(); break;
        case XG_CRC:   x_crc = (g->Flags & GFLG_SELECTED) != 0; break;
        case XG_EOL:   x_eol = code; break;
        }
        break;
    }
    return 1;
}

/* the terminal opened on a screen of its own in this screen's mode and size, or closed and this screen in front */
static void vt_open(void)
{
    if (vt) { term_front(vt); return; }
    if (x_state == XS_RUN && !x_raw) { notice(1, "The terminal needs the port: a transfer has it"); return; }
    if (!(vt = term_open(scr, vt_pens, 0, vt_out, NULL))) {
        notice(1, "The terminal did not open (chip memory?)"); return;
    }
    term_set_utf8(vt, vt_utf8);
    notice(0, "Terminal opened: Right Amiga F full screen or window, Right Amiga Q closes it");
}

/* the ZMODEM settings, kept as the global variables serialmon/ZFolder and serialmon/ZAuto (ENV: and ENVARC:) */
static void z_load(void)
{
    char b[8];
    if (GetVar((CONST_STRPTR)"serialmon/ZFolder", (STRPTR)z_folder, sizeof z_folder, GVF_GLOBAL_ONLY) < 0) z_folder[0] = 0;
    if (GetVar((CONST_STRPTR)"serialmon/ZAuto", (STRPTR)b, sizeof b, GVF_GLOBAL_ONLY) > 0) z_auto = b[0] != '0';
    if (GetVar((CONST_STRPTR)"serialmon/TermUTF8", (STRPTR)b, sizeof b, GVF_GLOBAL_ONLY) > 0) vt_utf8 = b[0] == '1';
    strcpy(x_path[1], z_folder);
}

static void z_save(void)
{
    SetVar((CONST_STRPTR)"serialmon/ZFolder", (STRPTR)z_folder, (LONG)strlen(z_folder), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
    SetVar((CONST_STRPTR)"serialmon/ZAuto", (STRPTR)(z_auto ? "1" : "0"), 1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
    SetVar((CONST_STRPTR)"serialmon/TermUTF8", (STRPTR)(vt_utf8 ? "1" : "0"), 1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

/* the download folder from an ASL folder requester; Cancel keeps it, an empty choice means the current directory */
static void z_choose(void)
{
    struct Library *AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 39);
    struct FileRequester *fr;
    if (!AslBase) { notice(1, "asl.library 39 did not open"); return; }
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASLFR_Screen, (ULONG)scr,
             ASLFR_TitleText, (ULONG)"ZMODEM Download Folder (none: the current directory)", ASLFR_InitialDrawer,
             (ULONG)z_folder, ASLFR_DrawersOnly, TRUE, ASLFR_SleepWindow, TRUE, ASLFR_Window, (ULONG)win, TAG_DONE);
    if (fr) {
        if (AslRequest(fr, NULL)) {
            strncpy(z_folder, (const char *)fr->fr_Drawer, sizeof z_folder - 1); z_folder[sizeof z_folder - 1] = 0;
            strcpy(x_path[1], z_folder);
            z_save();
            notice(0, "ZMODEM downloads go to %s", z_folder[0] ? z_folder : "the current directory");
        }
        FreeAslRequest(fr);
    }
    CloseLibrary(AslBase);
}

/* the terminal's file transfers, in the transfer panel on this screen: a ZMODEM receive started by the sender's
 * ZRQINIT into the panel's folder, or the panel opened to send (Right Amiga U); the terminal goes to its window */
static void vt_zmodem(void)
{
    if (xwin || mwin || awin || x_state == XS_RUN) return;
    term_window(vt);
    x_dir = 1; x_proto = XP_Z;
    strcpy(x_path[1], z_folder);                           /* the download folder (empty: the current directory) */
    if (!x_open()) return;
    ScreenToFront(scr); WindowToFront(xwin); ActivateWindow(xwin);   /* after the terminal's own WindowToFront */
    notice(0, "Terminal: a ZMODEM header (ZRQINIT) came in; receiving into %s", z_folder[0] ? z_folder : "the current directory");
    x_start();
}

static void vt_send(void)
{
    if (xwin || mwin || awin || x_state == XS_RUN) return;
    term_window(vt);
    x_dir = 0;
    if (x_open()) { ScreenToFront(scr); WindowToFront(xwin); ActivateWindow(xwin); }
}

static void vt_close(void)
{
    term_stats_t st;
    if (!vt) return;
    term_stats(vt, &st);
    term_close(vt); vt = NULL;
    notice(0, "Terminal closed: %lu bytes shown, %lu keys, %lu bytes sent, %lu frames, longest update %lu us",
           (unsigned long)st.bytes, (unsigned long)st.keys, (unsigned long)st.sent, (unsigned long)st.frames,
           (unsigned long)st.max_us);
    ScreenToFront(scr); ActivateWindow(win);
    if (win) { int i; draw_main(); for (i = 0; i < B_N; i++) btn[i].drawn = -1; after_change(); }
}

/* the main window's gadgets and drawing for its size now: at open and on every IDCMP_NEWSIZE.  The layout is
 * the full one when its monitor shows 3 rows or more, else the compact one */
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
    return 1;
}

/* ---- main ------------------------------------------------------------------------------------------------- */

static struct Process *wptr_pr;                            /* pr_WindowPtr as it was before the window opened */
static APTR            wptr_old;

static void cleanup(void)
{
    if (vt) { term_close(vt); vt = NULL; }
    if (wptr_pr) { wptr_pr->pr_WindowPtr = wptr_old; wptr_pr = NULL; }
    if (x_fh) { Close(x_fh); x_fh = 0; }
    x_state = XS_DONE; x_close();
    ab_close();
    m_close();
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (font) CloseFont(font);
    if (scr) CloseScreen(scr);
    if (sctl) ser_close(s_expunge);
    if (s_open) {                                          /* the driver kept a request: it and the port stay */
        if (cli_mode) printf("serialmon: %s kept a request; left open\n", drv_name[s_drv]);
        sbrk = sctl = srd = swr[0] = swr[1] = NULL; sport = NULL;
    }
    if (sbrk) DeleteIORequest((struct IORequest *)sbrk);
    if (sctl) DeleteIORequest((struct IORequest *)sctl);
    if (swr[1]) DeleteIORequest((struct IORequest *)swr[1]);
    if (swr[0]) DeleteIORequest((struct IORequest *)swr[0]);
    if (srd) DeleteIORequest((struct IORequest *)srd);
    if (sport) DeleteMsgPort(sport);
    if (have_vbl) RemIntServer(INTB_VERTB, &vbl_is);
    if (t_open) CloseDevice((struct IORequest *)treq);
    if (treq) DeleteIORequest((struct IORequest *)treq);
    if (tport) DeleteMsgPort(tport);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

static void fail(int cli, const char *why)
{
    if (cli) printf("serialmon: %s\n", why);
    else if (IntuitionBase) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"serialmon", (UBYTE *)"%s", (UBYTE *)"OK" };
        EasyRequest(NULL, &es, NULL, (ULONG)why);
    }
}

static int quit;

/* the screen title: the driver, its settings, and the measured number of display updates a second (over the last
 * 60) */
static void title_rate(void)
{
    static char t[128];
    char b[16];
    const snap_t *a = &hist[(hn - 1u) & 63u], *o = &hist[(hn - 61u) & 63u];
    ULONG r10 = 0;
    if (!win) return;
    if (hn > 61u && a->e > o->e) r10 = (ULONG)(600ull * efreq / (a->e - o->e));
    fmt_thousands(b, s_baud);
    {   /* the long form when it fits the window's title bar (its gadgets about 80 pixels), else the short one */
        int k, len;
        for (k = 0; k < 2; k++) {
            const char *gap = k ? "   " : "     ", *unit = k ? "/s" : " updates/s";
            if (s_open)
                len = snprintf(t, sizeof t, "Serial Monitor%s%s, %s baud, %s, %s%s%lu.%lu%s", gap, drv_txt[s_drv], b,
                               fmt_lbl[s_fmt], flow_lbl[s_flow], gap, (unsigned long)(r10 / 10u),
                               (unsigned long)(r10 % 10u), unit);
            else
                len = snprintf(t, sizeof t, "Serial Monitor%sno driver open%s%lu.%lu%s", gap, gap,
                               (unsigned long)(r10 / 10u), (unsigned long)(r10 % 10u), unit);
            if (len * 8 <= win->Width - 80) break;
        }
    }
    SetWindowTitles(win, (UBYTE *)t, (UBYTE *)t);
}

/* the port group's cycle gadgets set to the settings in use */
static void cycles_show(void)
{
    if (!win) return;
    GT_SetGadgetAttrs(gad[G_DRV], win, NULL, GTCY_Active, (ULONG)s_drv, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_BAUD], win, NULL, GTCY_Active, (ULONG)s_bi, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_FMT], win, NULL, GTCY_Active, (ULONG)s_fmt, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_FLOW], win, NULL, GTCY_Active, (ULONG)s_flow, TAG_DONE);
}

static void after_change(void)
{
    peak[0] = peak[1] = 0;
    if (win) b_heading();
    cycles_show();
    title_rate();
    controls_state();
}

/* section 1: the driver switch.  Every request of the open driver ended and the driver closed (and expunged, see
 * ser_close), then driver d opened with the settings in use.  Not while a test runs. */
static void switch_driver(int d)
{
    const char *why;
    if (t_state == T_RUN || t_state == T_DRAIN) { cycles_show(); notice(1, "The driver stays while the test runs"); return; }
    if (s_open && d == s_drv) return;
    tl_flush();
    if (s_open && !ser_close(s_expunge)) { cycles_show(); return; }
    s_drv = d;
    if ((why = ser_open(d)) != NULL) notice(1, "%s", why);
    after_change();
}

/* a setting changed: the driver's requests ended and SDCMD_SETPARAMS sent; a change to or from RTS/CTS opens the
 * driver again (SERF_7WIRE is read at OpenDevice) */
static void settings_changed(int was_bi, int was_fmt, int was_flow)
{
    if (t_state == T_RUN || t_state == T_DRAIN) {
        s_bi = was_bi; s_fmt = was_fmt; s_flow = was_flow;
        cycles_show(); notice(1, "The settings stay while the test runs");
        return;
    }
    derive();
    tl_flush();
    if (s_open) {
        if ((s_flow == FLOW_RTS) != (s_open_flow == FLOW_RTS)) {
            int d = s_drv;
            const char *why;
            if (ser_close(0) && (why = ser_open(d)) != NULL) notice(1, "%s", why);
        } else if (ser_quiesce()) {
            ser_params(); param_notice(); rd_start();
        }
    }
    after_change();
}

static void run_toggle(void) { if (t_state == T_RUN) test_stop(); else test_start(); }

static void set_hex(int h)
{
    tl_flush();
    s_hex = h;
    controls_state();
}

static void main_button(int i)
{
    switch (i) {
    case B_RUN:   run_toggle(); break;
    case B_L25: case B_L50: case B_L75: case B_L100: set_rate(B_L25 - i); break;
    case B_SEND:  send_line(); break;
    case B_ASC:   set_hex(0); break;
    case B_HEX:   set_hex(1); break;
    case B_BRK:   send_break(); break;
    case B_CLEAR: mon_clear(); break;
    case B_PAUSE: paused ^= 1; btn_set(&btn[B_PAUSE], paused, 0); break;
    case B_XFER:  x_open(); break;
    case B_TERM:  vt_open(); break;
    case B_QUIT:  quit = 1; break;
    }
}

/* the test's result, for a CLI; 1 on PASS */
static int summary(void)
{
    char a[16], b[16], c[16], d[16];
    int pass = test_pass();
    unsigned long long e1 = t_state == T_RUN ? eclock() : t_e1;
    ULONG ms = (ULONG)((e1 - t_e0) * 1000ull / efreq);
    ULONG txb = (t_state == T_RUN ? n_tx : t_tx1) - t_tx0, rxb = (t_state == T_RUN ? n_rx : t_rx1) - t_rx0;
    fmt_thousands(a, s_baud); fmt_thousands(b, paula_baud);
    printf("driver %s unit %ld, opened in %lu us; %s baud (Paula SERPER %lu: %s baud), %s, flow %s, read buffer %lu%s; "
           "SDCMD_SETPARAMS %s\n", drv_name[s_drv], (long)drv_unit[s_drv], (unsigned long)s_open_us, a,
           (unsigned long)paula_per, b, fmt_lbl[s_fmt], flow_lbl[s_flow], (unsigned long)s_rbuf,
           s_boogie ? ", RAD_BOOGIE" : "", s_param_err ? ser_errname(s_param_err) : "done");
    printf("loopback %s: %lu ms at %u %% of %lu bytes/s, sent %lu, received %lu, lost %lu, errors %lu, overruns %lu, "
           "overflows %lu, parity %lu, breaks %lu, other I/O errors %lu\n", pass ? "PASS" : "FAIL", (unsigned long)ms,
           rate_pct[t_rate], (unsigned long)wire_bps, (unsigned long)t_sent, (unsigned long)t_recv,
           (unsigned long)t_lost, (unsigned long)t_err, (unsigned long)(n_ovr - t_ovr0),
           (unsigned long)(n_full - t_full0), (unsigned long)(n_par - t_par0), (unsigned long)(n_brk - t_brk0),
           (unsigned long)(n_ioerr - t_io0));
    if (ms) {
        ULONG ob = (ULONG)((unsigned long long)txb * 1000u / ms), ib = (ULONG)((unsigned long long)rxb * 1000u / ms);
        printf("throughput while sending: out %lu bytes/s (%lu bit/s), in %lu bytes/s (%lu bit/s)\n",
               (unsigned long)ob, (unsigned long)(ob * frame_bits), (unsigned long)ib, (unsigned long)(ib * frame_bits));
    }
    if (t_lat_n) {
        unsigned long long mean = t_lat_sum / t_lat_n, var = t_lat_sq / t_lat_n;
        var = var > mean * mean ? var - mean * mean : 0;
        ms_text(a, (ULONG)mean); ms_text(b, t_lat_min); ms_text(c, t_lat_max); ms_text(d, isqrt64(var));
        printf("round trip: mean %s, min %s, max %s, jitter %s (%lu messages)\n", a, b, c, d,
               (unsigned long)t_lat_n);
    }
    printf("bytes out %lu, in %lu; meters at the end (1 s): out %lu bit/s, in %lu bit/s\n", (unsigned long)n_tx,
           (unsigned long)n_rx, (unsigned long)rate(0, 60), (unsigned long)rate(1, 60));
    if (s_open && ser_query()) {
        int i;
        printf("lines:");
        for (i = 0; i < 6; i++) printf(" %s %s%s", led_lbl[i], led_on(i) ? "on" : "off", i < 5 ? "," : "\n");
    }
    if (n_ioerr) printf("last other I/O error: %d (%s)\n", last_ioerr, ser_errname(last_ioerr));
    {
        unsigned int i, j;
        for (i = 0; i < elog_n; i++) {
            printf("fault %u: %c expected %lu, %s %lu; last bytes:", i + 1u, elog_e[i].k, (unsigned long)elog_e[i].exp,
                   elog_e[i].k == 'G' ? "gap" : "read", (unsigned long)elog_e[i].v);
            for (j = 0; j < ELOG_B; j++) printf(" %02x", elog_e[i].b[j]);
            printf("\n");
        }
    }
    return pass;
}

/* one 60 Hz update's work that needs no window: the driver, the test, the received bytes, the meters' history */
static void tick_core(unsigned long long now)
{
    ser_tick();
    if (s_open && !(hn & 3u)) ser_query();                /* the modem lines, 15 times a second */
    test_tick(now);
    rx_take();
    {
        snap_t *s = &hist[hn & 63u];
        s->e = now; s->c[0] = n_tx; s->c[1] = n_rx; hn++;
    }
    if (t_dur && t_state == T_RUN && now - t_e0 >= (unsigned long long)t_dur * efreq) test_stop();
}

static int str_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if ((*a | 0x20) != (*b | 0x20)) return 0;
    return *a == *b;
}

int main(int argc, char **argv)
{
    static LONG opt[12];
    struct RDArgs *ra = NULL;
    struct Screen *wb;
    ULONG mode, sw, sh, sigs, quit_at_end = 0, ioerr_seen = 0;
    unsigned long long next, period;
    const char *why;
    int i, cli = argc > 0, rc = 0;
    (void)argv;

    cli_mode = cli;
    for (i = 0; i < BAUD_N; i++) { sprintf(baud_txt[i], "%lu", (unsigned long)baud_tab[i]); baud_lbl[i] = (STRPTR)baud_txt[i]; }
    baud_lbl[BAUD_N] = NULL;
    drv_labels();
    if (cli) {
        if (!(ra = ReadArgs((CONST_STRPTR)"DEVICE/K,UNIT/N,BAUD/N,FORMAT/K,FLOW/K,RATE/N,SECONDS/N,BUFFER/N,START/S,"
                                          "NOWINDOW/S,KEEP/S,BOOGIE/S", opt, NULL))) {
            printf("usage: serialmon [DEVICE=name] [UNIT=n] [BAUD=n] [FORMAT=8N1|8N2|8E1|8O1|7E1|7O1|7N1] "
                   "[FLOW=RTSCTS|XONXOFF|NONE] [RATE=25|50|75|100] [SECONDS=N] [BUFFER=bytes] [START] [NOWINDOW] "
                   "[KEEP] [BOOGIE]\n");
            return 20;
        }
        if (opt[0] || opt[1]) s_drv = drv_add(opt[0] ? (const char *)opt[0] : "serial.device", opt[1] ? *(LONG *)opt[1] : 0);
        if (opt[2]) {
            ULONG v = (ULONG)*(LONG *)opt[2];
            for (i = 0; i < BAUD_N && baud_tab[i] != v; i++) ;
            if (i == BAUD_N && v >= 50u) {                 /* a rate not in the list: a 13th entry */
                baud_tab[BAUD_N] = v; sprintf(baud_txt[BAUD_N], "%lu", (unsigned long)v);
                baud_lbl[BAUD_N] = (STRPTR)baud_txt[BAUD_N]; baud_lbl[BAUD_N + 1] = NULL; baud_n = BAUD_N + 1;
            }
            if (i < baud_n) s_bi = i;
        }
        if (opt[3]) for (i = 0; fmt_lbl[i]; i++) if (str_ieq((const char *)opt[3], fmt_lbl[i])) s_fmt = i;
        if (opt[4]) {
            const char *f = (const char *)opt[4];
            if (str_ieq(f, "RTSCTS") || str_ieq(f, "RTS/CTS") || str_ieq(f, "7WIRE")) s_flow = FLOW_RTS;
            else if (str_ieq(f, "XONXOFF") || str_ieq(f, "xON/xOFF")) s_flow = FLOW_XON;
            else s_flow = FLOW_NONE;
        }
        if (opt[5]) for (i = 0; i < 4; i++) if ((unsigned int)*(LONG *)opt[5] == rate_pct[i]) t_rate = i;
        if (opt[6]) t_dur = (ULONG)*(LONG *)opt[6];
        if (opt[7]) { s_rbuf = (ULONG)*(LONG *)opt[7]; if (s_rbuf < 64u) s_rbuf = 64u; }
        if (opt[10]) s_expunge = 0;
        if (opt[11]) s_boogie = 1;
        quit_at_end = opt[6] && opt[8];
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
    AddIntServer(INTB_VERTB, &vbl_is); have_vbl = 1;
    sport = CreateMsgPort();
    if (sport) {
        srd = (struct IOExtSer *)CreateIORequest(sport, sizeof(struct IOExtSer));
        swr[0] = (struct IOExtSer *)CreateIORequest(sport, sizeof(struct IOExtSer));
        swr[1] = (struct IOExtSer *)CreateIORequest(sport, sizeof(struct IOExtSer));
        sctl = (struct IOExtSer *)CreateIORequest(sport, sizeof(struct IOExtSer));
        sbrk = (struct IOExtSer *)CreateIORequest(sport, sizeof(struct IOExtSer));
    }
    if (!srd || !swr[0] || !swr[1] || !sctl || !sbrk) { fail(cli, "out of memory"); cleanup(); return 20; }
    beam_measure();
    derive();
    t_zero = beam_now();
    period = efreq / TICK_HZ;

    if (opt[9]) {                                          /* NOWINDOW: the test alone, no screen, nothing drawn */
        unsigned long long now;
        if (!t_dur) t_dur = 10;
        if ((why = ser_open(s_drv)) != NULL) { printf("serialmon: %s\n", why); cleanup(); if (ra) FreeArgs(ra); return 20; }
        if (test_block()) {
            printf("serialmon: the loopback test needs 8 data bits and no xON/xOFF\n");
            cleanup(); if (ra) FreeArgs(ra); return 20;
        }
        test_start();
        next = eclock() + period;
        treq->tr_node.io_Command = TR_ADDREQUEST;
        treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
        SendIO((struct IORequest *)treq);
        while (t_state != T_DONE) {
            sigs = Wait((1ul << tport->mp_SigBit) | (1ul << sport->mp_SigBit) | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
            if (sigs & (1ul << sport->mp_SigBit)) ser_poll();
            if ((sigs & (1ul << tport->mp_SigBit)) && GetMsg(tport)) {
                now = eclock();
                tick_core(now);
                next += period;
                if (next < now) next = now + period;
                treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
                SendIO((struct IORequest *)treq);
            }
        }
        AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq);
        rc = summary() ? 0 : 5;
        cleanup();
        if (ra) FreeArgs(ra);
        return rc;
    }

    if (!(wb = LockPubScreen((CONST_STRPTR)"Workbench"))) { fail(cli, "no Workbench screen"); cleanup(); return 20; }
    mode = GetVPModeID(&wb->ViewPort); sw = wb->Width; sh = wb->Height;
    if (sw < WIN_W) {                                      /* a lores Workbench: hires on the same monitor, 640 wide */
        ULONG hm = (mode & MONITOR_ID_MASK) | HIRES_KEY | (mode & LORESLACE_KEY);
        if (!ModeNotAvailable(hm)) { mode = hm; sw = WIN_W; }
    }
    scr = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, (ULONG)sw, SA_Height, (ULONG)sh,
                         SA_Depth, 4, SA_Font, (ULONG)&topaz8, SA_Pens, (ULONG)pens, SA_Colors, (ULONG)colours,
                         SA_Title, (ULONG)"Serial Monitor", SA_ShowTitle, TRUE,
                         SA_Type, CUSTOMSCREEN, SA_Interleaved, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, wb);
    if (!scr) { fail(cli, "the screen did not open"); cleanup(); return 20; }
    font = OpenFont(&topaz8);
    vi = GetVisualInfo(scr, TAG_DONE);
    if (!vi || !font) { fail(cli, "the gadgets could not be made"); cleanup(); return 20; }
    z_load();
    for (i = 0; nm[i].nm_Type != NM_END; i++) {           /* the check marks from the settings */
        ULONG id = (ULONG)nm[i].nm_UserData;
        if (id == MI_EXPUNGE && !s_expunge) nm[i].nm_Flags &= ~CHECKED;
        if (id == MI_BOOGIE && s_boogie) nm[i].nm_Flags |= CHECKED;
        if (id == MI_ZAUTO && !z_auto) nm[i].nm_Flags &= ~CHECKED;
        if (id == MI_UTF8 && vt_utf8) nm[i].nm_Flags |= CHECKED;
        if (id >= MI_DUR && id < MI_DUR + 4u && dur_tab[id - MI_DUR] == t_dur) nm[i].nm_Flags |= CHECKED;
    }
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, (ULONG)scr->Width, WA_Height, (ULONG)scr->Height, WA_Title, (ULONG)"Serial Monitor",
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_SizeBRight, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP | SCROLLERIDCMP | IDCMP_GADGETDOWN |
                                   IDCMP_VANILLAKEY | IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE |
                                   IDCMP_CLOSEWINDOW, TAG_DONE);
    if (!win) { fail(cli, "the window did not open"); cleanup(); return 20; }
    rp = win->RPort; SetFont(rp, font);
    wptr_pr = (struct Process *)FindTask(NULL); wptr_old = wptr_pr->pr_WindowPtr;
    wptr_pr->pr_WindowPtr = win;                           /* DOS requesters on this screen (cleanup restores it) */
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
    if ((why = ser_open(s_drv)) != NULL) notice(1, "%s", why);
    after_change();

    next = eclock() + period;
    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = (ULONG)(next >> 32); treq->tr_time.tv_micro = (ULONG)next;
    SendIO((struct IORequest *)treq);
    if (opt[8]) test_start();

    while (!quit) {
        struct IntuiMessage *im;
        ULONG msig = mwin ? 1ul << mwin->UserPort->mp_SigBit : 0;
        if (awin) msig |= (1ul << awin->UserPort->mp_SigBit) | ab_sigmask | (1ul << ab_tport->mp_SigBit);
        if (xwin) msig |= 1ul << xwin->UserPort->mp_SigBit;
        if (vt) msig |= term_sigmask(vt);
        sigs = Wait((1ul << win->UserPort->mp_SigBit) | msig | (1ul << tport->mp_SigBit) | (1ul << sport->mp_SigBit) |
                    SIGBREAKF_CTRL_C);
        if (awin && ab_tbusy && (sigs & (1ul << ab_tport->mp_SigBit)) && GetMsg(ab_tport)) { ab_tbusy = 0; ab_chain(); }
        if (awin && (sigs & ab_sigmask)) ab_frame();
        if (sigs & SIGBREAKF_CTRL_C) quit = 1;
        if (sigs & (1ul << sport->mp_SigBit)) ser_poll();
        if ((sigs & (1ul << tport->mp_SigBit)) && GetMsg(tport)) {
            unsigned long long now = eclock();
            /* the tick: the driver, the test's transmitter, the received bytes, the meters, the table, the buttons */
            /* the monitor's budget: test data on one update in fifteen, other lines three an update; a count of
             * the bytes not shown goes in as a line of its own */
            test_shown_in = test_shown_out = hn % TEST_EVERY ? 1 : 0;
            mon_budget = MON_PER_UPDATE;
            tick_core(now);
            if (tl_n && hn - tl_hn > 6u) tl_flush();       /* a line not complete after 100 ms */
            mon_note_skipped();
            if (n_ioerr != ioerr_seen && hn % TICK_HZ == 0) {
                notice(1, "I/O error %d (%s), %lu so far", last_ioerr, ser_errname(last_ioerr), (unsigned long)n_ioerr);
                ioerr_seen = n_ioerr;
            }
            /* the Out and In bars and the modem lines every update; the result panel every update; the readouts
             * one an update in turn */
            if (vt) term_tick(vt, 9000u);                  /* the terminal: input, drawing, scroll (about 9 ms) */
            if (vt && zseen) { zseen = 0; vt_zmodem(); }
            if (!awin && !vt) {                            /* behind the About box or the terminal nothing is drawn */
                meter_bar(0); meter_bar(1); leds_draw();
                test_status(now);
                if (!mon_update()) slow_item((int)(hn % SLOW_N));   /* the view or a readout: one an update */
            }
            x_tick();
            if (hn % (3u * TICK_HZ) == 0) title_rate();
            if (!awin && !vt) for (i = 0; i < B_N; i++) btn_draw(&btn[i]);
            if (xwin) for (i = 0; i < XB_N; i++) if (xbtn[i].w == xwin) btn_draw(&xbtn[i]);
            if (mwin) for (i = 0; i < MB_N; i++) btn_draw(&mbtn[i]);
            if (awin) btn_draw(&abtn);
            if (quit_at_end && t_state == T_DONE) quit = 1;
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
        if (vt && (sigs & term_sigmask(vt))) {
            if (!term_events(vt)) vt_close();
            else if (term_requests(vt) & TERM_REQ_SEND) vt_send();
        }
        while (xwin && (im = GT_GetIMsg(xwin->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (!x_event(cls, code, g)) { x_close(); break; }
        }
        while (mwin && (im = GT_GetIMsg(mwin->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (!m_event(cls, code, g)) {
                m_close();
                if (m_pick >= 0) { int d = m_pick; m_pick = -1; switch_driver(d); }
                break;
            }
        }
        while ((im = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            struct Gadget *g = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); continue; }
            if (mwin || awin || xwin) continue;            /* the panels and the About box are modal */
            switch (cls) {
            case IDCMP_NEWSIZE:
                if (!layout()) quit = 1;
                after_change();
                break;
            case IDCMP_CLOSEWINDOW: quit = 1; break;
            case IDCMP_MENUPICK:
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    ULONG id;
                    if (!it) break;
                    id = (ULONG)GTMENUITEM_USERDATA(it);
                    switch (id) {
                    case MI_RUN: run_toggle(); break;
                    case MI_BREAK: send_break(); break;
                    case MI_CLEAR: mon_clear(); break;
                    case MI_CLRBUF: clear_buffer(); break;
                    case MI_DRIVER: m_open(); break;
                    case MI_XFER: x_open(); break;
                    case MI_TERM: vt_open(); break;
                    case MI_ABOUT: ab_open(); break;
                    case MI_QUIT: quit = 1; break;
                    case MI_EXPUNGE: s_expunge = (it->Flags & CHECKED) != 0; break;
                    case MI_BOOGIE:
                        s_boogie = (it->Flags & CHECKED) != 0;
                        settings_changed(s_bi, s_fmt, s_flow);
                        if (t_state == T_RUN || t_state == T_DRAIN) { s_boogie ^= 1; menu_check(MI_BOOGIE, s_boogie); }
                        break;
                    case MI_SHOWTEST: show_test = (it->Flags & CHECKED) != 0; break;
                    case MI_ZAUTO: z_auto = (it->Flags & CHECKED) != 0; z_save(); break;
                    case MI_ZFOLDER: z_choose(); break;
                    case MI_UTF8: vt_utf8 = (it->Flags & CHECKED) != 0; if (vt) term_set_utf8(vt, vt_utf8); z_save(); break;
                    default:
                        if (id >= MI_DUR && id < MI_DUR + 4u) t_dur = dur_tab[id - MI_DUR];
                        break;
                    }
                    code = it->NextSelect;
                }
                break;
            case IDCMP_VANILLAKEY:
                switch (code) {
                case ' ': run_toggle(); break;
                case '1': case '2': case '3': case '4': set_rate(code - '1'); break;
                case 0x1B: quit = 1; break;
                case '\r': ActivateGadget(gad[G_LINE], win, NULL); break;
                default:
                    switch (code | 0x20) {
                    case 's': run_toggle(); break;
                    case 'h': set_hex(!s_hex); break;
                    case 'l': ActivateGadget(gad[G_LINE], win, NULL); break;
                    case 'b': send_break(); break;
                    case 'c': mon_clear(); break;
                    case 'p': main_button(B_PAUSE); break;
                    case 't': show_test ^= 1; menu_check(MI_SHOWTEST, show_test); break;
                    case 'd': m_open(); break;
                    case 'f': x_open(); break;
                    case 'v': vt_open(); break;
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
                case G_DRV:  switch_driver(code); break;
                case G_BAUD: { int o = s_bi; s_bi = code; settings_changed(o, s_fmt, s_flow); } break;
                case G_FMT:  { int o = s_fmt; s_fmt = code; settings_changed(s_bi, o, s_flow); } break;
                case G_FLOW: { int o = s_flow; s_flow = code; settings_changed(s_bi, s_fmt, o); } break;
                case G_LINE: if (code != 0x09) { send_line(); ActivateGadget(gad[G_LINE], win, NULL); } break;
                }
                break;
            }
        }
        if (quit && x_state == XS_RUN) {                   /* a transfer runs: it is cancelled first */
            quit = 0; x_cancel();
            notice(1, "A transfer runs: it is being cancelled; quit again when it has ended");
        }
    }
    AbortIO((struct IORequest *)treq); WaitIO((struct IORequest *)treq);
    if (cli && t_started) summary();
    cleanup();
    if (ra) FreeArgs(ra);
    return 0;
}
