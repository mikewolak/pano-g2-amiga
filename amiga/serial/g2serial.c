/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* g2serial.c -- g2serial.device, a serial.device-compatible Exec device for the G2 NanoMig AGA core (68020).
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  New implementation from the functional specification
 * a functional analysis of BaudBandit.device 1.4b.
 *
 * Data paths (g2serial.h): the G2 serial FIFO board when it is present, Paula otherwise.  The ENV variable
 * G2SERIAL (PAULA or FIFO) or bit 0 of OpenDevice()'s flags argument (G2SER_OPENF_PAULA) selects Paula although the
 * board is present.  The path is fixed from the first open to the last close.
 *
 * Receive: the interrupt stores each byte straight into the active CMD_READ's buffer while the receive ring is empty
 * (rd_direct), otherwise into the ring; a read is completed and replied inside the interrupt, once per request.
 * Bytes already in the ring are copied by the task with interrupts enabled; rd_direct is set under Disable only when
 * the ring is empty, so the order of the bytes is kept.  A full ring is never overwritten: the receive interrupt is
 * switched off and the hardware holds the data (Paula's RBF stays set, which stops uart_port.v; the board's RX FIFO
 * fills, which stops amiga_serbridge.v), until the task has taken bytes out of the ring.
 *
 * Transmit: CMD_WRITE is queued and the transmitter started by enabling its interrupt (Paula: TBE set by software
 * in INTREQ; board: TX interrupt enable, pending while the TX FIFO has room).  Paula takes one SERDAT word per TBE
 * interrupt; the board takes up to 512 bytes per interrupt in word writes.  The write is replied from the interrupt
 * when its last byte has been handed to the hardware.
 *
 * Flow control: SERF_7WIRE negates RTS at 3/4 of the ring and asserts it again at 1/4; CTS is sampled before every
 * Paula byte and every board refill, and while it is negated a 2 ms timer.device UNIT_MICROHZ poll (PA_SOFTINT
 * reply port) restarts the transmitter.  xON/xOFF (unless SERF_XDISABLED) uses the same marks for the receiver and
 * holds the transmitter on a received xOFF.
 *
 * Locking: BeginIO and AbortIO run under Forbid (exclusion of other tasks on the same unit); state shared with the
 * interrupts is changed under Disable for a few instructions; the ring indexes are free-running longwords, each
 * written by one side only. */
#include <stddef.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <hardware/adkbits.h>
#include <resources/misc.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/dos.h>
#include <proto/misc.h>
#include "g2serial.h"

#define CUSTOM ((volatile struct Custom *)0xDFF000)

struct ExecBase      *SysBase;
struct ExpansionBase *ExpansionBase;
struct DosLibrary    *DOSBase;
struct Library       *MiscBase;

static const char dev_name[] = "g2serial.device";
static const char dev_id[]   = "g2serial 1.0 (FIFO board or Paula)\r\n";
const char g2ser_version[]   = "$VER: g2serial.device 1.0";

/* entry points in entry.s */
extern void dev_init_stub(void), dev_open_stub(void), dev_close_stub(void), dev_expunge_stub(void);
extern void dev_null_stub(void), dev_beginio_stub(void), dev_abortio_stub(void);
extern void rbf_stub(void), tbe_stub(void), ports_stub(void), soft_stub(void);

static const APTR dev_vectors[] = {
    (APTR)dev_open_stub, (APTR)dev_close_stub, (APTR)dev_expunge_stub, (APTR)dev_null_stub,
    (APTR)dev_beginio_stub, (APTR)dev_abortio_stub, (APTR)-1
};

static const APTR dev_inittab[4] = {
    (APTR)sizeof(struct G2Ser), (APTR)dev_vectors, NULL, (APTR)dev_init_stub
};

const struct Resident g2ser_romtag __attribute__((used)) = {
    RTC_MATCHWORD, (struct Resident *)&g2ser_romtag, (APTR)(&g2ser_romtag + 1), RTF_AUTOINIT,
    G2SER_VERSION, NT_DEVICE, 0, (char *)dev_name, (char *)dev_id, (APTR)dev_inittab
};

/* ---- small helpers -------------------------------------------------------------------------------------------- */

static inline void rts_negate(void) { __asm volatile ("bset #6,0xbfd000" ::: "memory", "cc"); }
static inline void rts_assert(void) { __asm volatile ("bclr #6,0xbfd000" ::: "memory", "cc"); }
#define CTS_NEGATED() (*CIAB_PRA & CIAF_CTS)

static inline UWORD parity8(UWORD x) { x ^= x >> 4; return (0x6996 >> (x & 15)) & 1; }

static ULONG str_len(const UBYTE *s) { const UBYTE *p = s; while (*p) p++; return (ULONG)(p - s); }

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_Tail = NULL;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static void zero(void *p, ULONG n) { UBYTE *b = p; while (n--) *b++ = 0; }

/* the parity bit for data d (par: 1 even, 2 odd, 3 mark, 4 space) */
static inline UWORD par_bit(const struct G2Ser *g, UWORD d)
{
    switch (g->par) {
    case 1:  return parity8(d);
    case 2:  return parity8(d) ^ 1;
    case 3:  return 1;
    default: return 0;
    }
}

/* the SERDAT word for one character: data bits, the parity bit, the stop bits above them.  On the FIFO path the low
 * eight bits are the byte on the 8N1 wire (7-bit formats carry parity or a stop bit in bit 7). */
static inline UWORD tx_frame(const struct G2Ser *g, UWORD b)
{
    UWORD w = b & g->tx_dmask;
    if (g->par) w |= par_bit(g, w) << g->writelen;
    return w | g->tx_stop;
}

static inline int term_hit(const struct G2Ser *g, UBYTE b) { return g->term_map[b >> 3] & (1 << (b & 7)); }

static inline ULONG wr_length(struct IOExtSer *io)
{
    return io->IOSer.io_Length == (ULONG)-1 ? str_len(io->IOSer.io_Data) : io->IOSer.io_Length;
}

static inline void board_ctrl(struct G2Ser *g) { g->bd[BD_CTRL] = g->ctrl; }

/* ---- completion (Disable held, or interrupt) ------------------------------------------------------------------ */

static void reply(struct IOExtSer *io)
{
    if (!(io->IOSer.io_Flags & IOF_QUICK)) ReplyMsg(&io->IOSer.io_Message);
}

/* the active read ends with err; the next queued read becomes active (rd_direct is kept: the ring is empty when it
 * is set, and it is cleared when no read is left) */
static void rd_finish(struct G2Ser *g, BYTE err)
{
    struct IOExtSer *io = g->rd, *nx;
    io->IOSer.io_Actual = (ULONG)(g->rd_ptr - (UBYTE *)io->IOSer.io_Data);
    io->IOSer.io_Error = err;
    io->IOSer.io_Flags &= ~G2_IOSERF_ACTIVE;
    nx = (struct IOExtSer *)RemHead((struct List *)&g->rq);
    if (nx) {
        nx->IOSer.io_Flags = (nx->IOSer.io_Flags & ~G2_IOSERF_QUEUED) | G2_IOSERF_ACTIVE;
        g->rd = nx; g->rd_ptr = nx->IOSer.io_Data; g->rd_left = nx->IOSer.io_Length;
    } else {
        g->rd = NULL; g->rd_direct = 0;
    }
    reply(io);
}

static void wr_start(struct G2Ser *g, struct IOExtSer *io)
{
    io->IOSer.io_Flags = (io->IOSer.io_Flags & ~G2_IOSERF_QUEUED) | G2_IOSERF_ACTIVE;
    g->wr = io; g->wr_ptr = io->IOSer.io_Data;
    g->wr_left = io->IOSer.io_Command == CMD_WRITE ? wr_length(io) : 0;
}

static void wr_finish(struct G2Ser *g, BYTE err)
{
    struct IOExtSer *io = g->wr, *nx;
    io->IOSer.io_Actual = io->IOSer.io_Command == CMD_WRITE ? (ULONG)(g->wr_ptr - (UBYTE *)io->IOSer.io_Data) : 0;
    io->IOSer.io_Error = err;
    io->IOSer.io_Flags &= ~G2_IOSERF_ACTIVE;
    nx = (struct IOExtSer *)RemHead((struct List *)&g->wq);
    if (nx) wr_start(g, nx); else g->wr = NULL;
    reply(io);
}

/* the transmitter's interrupt on (Disable held, or interrupt) */
static void tx_kick(struct G2Ser *g)
{
    if (g->tx_on) return;
    g->tx_on = 1;
    if (g->path == PATH_FIFO) {
        g->ctrl |= CTRL_TXIE; board_ctrl(g);
    } else {
        CUSTOM->intreq = INTF_SETCLR | INTF_TBE;
        CUSTOM->intena = INTF_SETCLR | INTF_TBE;
    }
}

/* receive interrupt back on after the task has made room (Disable held) */
static void rx_resume(struct G2Ser *g)
{
    if (!g->rx_hold) return;
    g->rx_hold = 0;
    if (g->path == PATH_FIFO) { g->ctrl |= CTRL_RXIE; board_ctrl(g); }
    else CUSTOM->intena = INTF_SETCLR | INTF_RBF;
}

/* ---- timers (PA_SOFTINT port) --------------------------------------------------------------------------------- */

static void cts_wait(struct G2Ser *g)
{
    g->hold |= HOLD_CTS;
    if (g->cts_busy || !g->timer_open) return;
    g->cts_busy = 1;
    g->cts_tr.tr_node.io_Command = TR_ADDREQUEST;
    g->cts_tr.tr_time.tv_secs = 0;
    g->cts_tr.tr_time.tv_micro = CTS_POLL_US;
    SendIO(&g->cts_tr.tr_node);
}

static void start_break(struct G2Ser *g, struct IOExtSer *io)
{
    ULONG t = g->brktime;
    g->brk_req = io; g->brk_busy = 1; g->brk_abort = 0;
    g->hold |= HOLD_BRK;
    if (g->path == PATH_PAULA) CUSTOM->adkcon = ADKF_SETCLR | ADKF_UARTBRK;
    g->brk_tr.tr_node.io_Command = TR_ADDREQUEST;
    g->brk_tr.tr_time.tv_secs = t / 1000000;
    g->brk_tr.tr_time.tv_micro = t % 1000000;
    SendIO(&g->brk_tr.tr_node);
}

/* software interrupt: a timerequest came back */
void soft_int(struct G2Ser *g)
{
    struct Message *m;
    while ((m = GetMsg(&g->tport)) != NULL) {
        if (m == &g->cts_tr.tr_node.io_Message) {
            Disable();
            g->cts_busy = 0;
            if (g->hold & HOLD_CTS) {
                if ((g->serflags & SERF_7WIRE) && CTS_NEGATED() && g->cts_tr.tr_node.io_Error == 0) {
                    g->hold &= ~HOLD_CTS; cts_wait(g);
                } else {
                    g->hold &= ~HOLD_CTS; tx_kick(g);
                }
            }
            Enable();
        } else if (m == &g->brk_tr.tr_node.io_Message) {
            struct IOExtSer *io;
            BYTE err;
            Disable();
            if (g->path == PATH_PAULA) CUSTOM->adkcon = ADKF_UARTBRK;
            g->brk_busy = 0;
            g->stat |= IO_STATF_WROTEBREAK;
            g->hold &= ~HOLD_BRK;
            io = g->brk_req; g->brk_req = NULL;
            err = g->brk_abort ? IOERR_ABORTED : 0;
            g->brk_abort = 0;
            if (io) {
                if (io == g->wr) wr_finish(g, err);
                else {
                    io->IOSer.io_Error = err;
                    io->IOSer.io_Flags &= ~G2_IOSERF_ACTIVE;
                    reply(io);
                }
            }
            tx_kick(g);
            Enable();
        }
    }
}

/* ---- receive (interrupt) -------------------------------------------------------------------------------------- */

/* the ring has reached 3/4: RTS negated (SERF_7WIRE), xOFF sent (xON/xOFF) */
static void rx_high(struct G2Ser *g)
{
    g->throttled = 1;
    if (g->serflags & SERF_7WIRE) rts_negate();
    if (g->xon_on) { g->ctl_tx = 0x100 | g->xoff; g->stat |= IO_STATF_XOFFREAD; tx_kick(g); }
}

static void rx_error(struct G2Ser *g, BYTE e)
{
    if (g->rd_direct) rd_finish(g, e); else g->rx_err = e;
}

/* one received byte, fast format (8 bits, no parity, no xON/xOFF, no EOF scan) */
static inline void rx_put_fast(struct G2Ser *g, UBYTE b)
{
    if (g->rd_direct) {
        *g->rd_ptr++ = b;
        if (--g->rd_left == 0) rd_finish(g, 0);
    } else {
        ULONG h = g->rhead;
        g->ring[h & g->rmask] = b;
        g->rhead = ++h;
        if (h - g->rtail >= g->rx_hi && !g->throttled) rx_high(g);
    }
}

/* one received character in any format: w holds the data bits, the parity bit and the stop bit as SERDATR does */
static void rx_slow(struct G2Ser *g, UWORD w)
{
    UBYTE d = (UBYTE)(w & g->rx_dmask);
    int perr;
    if (g->brk_detect && !(w & g->rx_stop) && d == 0) {
        g->stat |= IO_STATF_READBREAK;
        rx_error(g, SerErr_DetectedBreak);
        return;
    }
    if (g->xon_on) {
        if (d == g->xon)  { g->hold &= ~HOLD_XOFF; g->stat &= ~IO_STATF_XOFFWRITE; tx_kick(g); return; }
        if (d == g->xoff) { g->hold |= HOLD_XOFF;  g->stat |= IO_STATF_XOFFWRITE; return; }
    }
    perr = g->par && ((w >> g->readlen) & 1) != par_bit(g, d);
    if (g->rd_direct) {
        *g->rd_ptr++ = d;
        if (perr) rd_finish(g, SerErr_ParityErr);
        else if (--g->rd_left == 0 || (g->eof && term_hit(g, d))) rd_finish(g, 0);
    } else {
        ULONG h = g->rhead;
        g->ring[h & g->rmask] = d;
        g->rhead = ++h;
        if (perr) g->rx_err = SerErr_ParityErr;
        if (h - g->rtail >= g->rx_hi && !g->throttled) rx_high(g);
    }
}

/* Paula RBF (level 5, SetIntVector): SERDATR read before RBF is cleared, RBF tested again after */
void rbf_int(struct G2Ser *g)
{
    volatile struct Custom *c = CUSTOM;
    UWORD w;
    do {
        if (!g->rd_direct && g->rhead - g->rtail >= g->rsize) {
            c->intena = INTF_RBF;          /* RBF stays set: uart_port.v sends nothing more */
            g->rx_hold = 1;
            return;
        }
        w = c->serdatr;
        c->intreq = INTF_RBF;
        if (w & 0x8000) { g->stat |= IO_STATF_OVERRUN; rx_error(g, SerErr_LineErr); }
        if (g->fast_rx) rx_put_fast(g, (UBYTE)w); else rx_slow(g, w);
    } while (c->intreqr & INTF_RBF);
}

/* n bytes from the board's RX FIFO to d: longwords (RXDATA2 at 0x10 and 0x12, four bytes a move.l) where d is even,
 * a word and single bytes at the ends */
static void fifo_get(volatile UWORD *bd, UBYTE *d, ULONG n)
{
    volatile ULONG *rl = (volatile ULONG *)&bd[BD_RXDATA2];
    ULONG *l;
    ULONG k;
    if (!n) return;
    if ((ULONG)d & 1) { *d++ = (UBYTE)bd[BD_RXDATA1]; n--; }
    l = (ULONG *)d;
    for (k = n >> 2; k >= 4; k -= 4) { l[0] = *rl; l[1] = *rl; l[2] = *rl; l[3] = *rl; l += 4; }
    while (k--) *l++ = *rl;
    d = (UBYTE *)l;
    if (n & 2) { *(UWORD *)d = bd[BD_RXDATA2]; d += 2; }
    if (n & 1) *d = (UBYTE)bd[BD_RXDATA1];
}

static void fifo_rx_hold(struct G2Ser *g)
{
    g->ctrl &= ~CTRL_RXIE; board_ctrl(g);
    g->rx_hold = 1;
}

static void fifo_rx(struct G2Ser *g)
{
    volatile UWORD *bd = g->bd;
    ULONG n, m;
    int pass;
    for (pass = 0; pass < 4 && (n = bd[BD_RXCOUNT]) != 0; pass++) {
        if (!g->fast_rx) {
            while (n--) {
                if (!g->rd_direct && g->rhead - g->rtail >= g->rsize) { fifo_rx_hold(g); return; }
                rx_slow(g, (UBYTE)bd[BD_RXDATA1] | 0x100);
            }
            continue;
        }
        while (n) {
            if (g->rd_direct) {
                m = n < g->rd_left ? n : g->rd_left;
                fifo_get(bd, g->rd_ptr, m);
                g->rd_ptr += m; g->rd_left -= m; n -= m;
                if (!g->rd_left) rd_finish(g, 0);
            } else {
                ULONG h = g->rhead, fr = g->rsize - (h - g->rtail), i, first;
                if (!fr) { fifo_rx_hold(g); return; }
                m = n < fr ? n : fr;
                i = h & g->rmask;
                first = g->rsize - i;
                if (first > m) first = m;
                fifo_get(bd, g->ring + i, first);
                fifo_get(bd, g->ring, m - first);
                g->rhead = h + m; n -= m;
                if (h + m - g->rtail >= g->rx_hi && !g->throttled) rx_high(g);
            }
        }
    }
}

/* ---- transmit (interrupt) ------------------------------------------------------------------------------------- */

/* Paula TBE (level 1, SetIntVector): one SERDAT word per interrupt */
void tbe_int(struct G2Ser *g)
{
    volatile struct Custom *c = CUSTOM;
    struct IOExtSer *io;
    UWORD b;
    c->intreq = INTF_TBE;
    if (g->ctl_tx) { c->serdat = tx_frame(g, g->ctl_tx & 0xFF); g->ctl_tx = 0; return; }
    io = g->wr;
    if (!io || g->hold) goto off;
    if (io->IOSer.io_Command != CMD_WRITE) { start_break(g, io); goto off; }
    if ((g->serflags & SERF_7WIRE) && CTS_NEGATED()) { cts_wait(g); goto off; }
    b = *g->wr_ptr++;
    c->serdat = g->fast_tx ? (b | 0x100) : tx_frame(g, b);
    if (--g->wr_left == 0) wr_finish(g, 0);
    return;
off:
    c->intena = INTF_TBE;
    g->tx_on = 0;
}

/* n bytes from s to the board's TX FIFO (n <= its free places) */
static void fifo_put(volatile UWORD *bd, const UBYTE *s, ULONG n)
{
    volatile UBYTE *tb = (volatile UBYTE *)&bd[BD_TXDATA];
    volatile UWORD *tw = &bd[BD_TXDATA];
    volatile ULONG *tl = (volatile ULONG *)&bd[BD_TXDATA];       /* TXDATA at 0x14 and 0x16: four bytes a move.l */
    const ULONG *l;
    ULONG k;
    if (!n) return;
    if ((ULONG)s & 1) { *tb = *s++; n--; }
    l = (const ULONG *)s;
    for (k = n >> 2; k >= 4; k -= 4) { *tl = l[0]; *tl = l[1]; *tl = l[2]; *tl = l[3]; l += 4; }
    while (k--) *tl = *l++;
    s = (const UBYTE *)l;
    if (n & 2) { *tw = *(const UWORD *)s; s += 2; }
    if (n & 1) *tb = *s;
}

static void fifo_tx(struct G2Ser *g)
{
    volatile UWORD *bd = g->bd;
    struct IOExtSer *io;
    ULONG space = bd[BD_TXSPACE], m;
    if (g->tx_cap < FIFO_N) {
        ULONG used = FIFO_N - space;
        space = used >= g->tx_cap ? 0 : g->tx_cap - used;
    }
    if (g->ctl_tx && space) {
        *(volatile UBYTE *)&bd[BD_TXDATA] = (UBYTE)tx_frame(g, g->ctl_tx & 0xFF);
        g->ctl_tx = 0; space--;
    }
    while (space) {
        io = g->wr;
        if (!io || g->hold) goto off;
        if (io->IOSer.io_Command != CMD_WRITE) { start_break(g, io); goto off; }
        if ((g->serflags & SERF_7WIRE) && CTS_NEGATED()) { cts_wait(g); goto off; }
        m = space < g->wr_left ? space : g->wr_left;
        if (g->fast_tx) fifo_put(bd, g->wr_ptr, m);
        else {
            volatile UBYTE *tb = (volatile UBYTE *)&bd[BD_TXDATA];
            const UBYTE *s = g->wr_ptr;
            ULONG k = m;
            while (k--) *tb = (UBYTE)tx_frame(g, *s++);
        }
        g->wr_ptr += m; g->wr_left -= m; space -= m;
        if (!g->wr_left) wr_finish(g, 0);
    }
    return;
off:
    g->ctrl &= ~CTRL_TXIE; board_ctrl(g);
    g->tx_on = 0;
}

/* INT2 server (shared): 1 when the board was the source */
ULONG ports_int(struct G2Ser *g)
{
    UWORD ctrl = g->bd[BD_CTRL];
    if (!(ctrl & (CTRL_RXPEND | CTRL_TXPEND))) return 0;
    if (ctrl & CTRL_RXPEND) fifo_rx(g);
    if (ctrl & CTRL_TXPEND) fifo_tx(g);
    return 1;
}

/* ---- task side: the ring -------------------------------------------------------------------------------------- */

/* RTS and xON again once the ring is down to 1/4 */
static void rx_unthrottle(struct G2Ser *g, int force)
{
    if (!g->throttled) return;
    if (!force && g->rhead - g->rtail > g->rx_lo) return;
    Disable();
    g->throttled = 0;
    rts_assert();
    if (g->stat & IO_STATF_XOFFREAD) {
        g->stat &= ~IO_STATF_XOFFREAD;
        if (g->xon_on) { g->ctl_tx = 0x100 | g->xon; tx_kick(g); }
    }
    Enable();
}

/* up to want bytes from the ring to d; stops after a terminator in EOF mode.  Returns the count, *hit when a
 * terminator ended it. */
static ULONG ring_copy(struct G2Ser *g, UBYTE *d, ULONG want, int *hit)
{
    ULONG t = g->rtail, n = g->rhead - t, i, first;
    *hit = 0;
    if (n > want) n = want;
    if (!n) return 0;
    if (g->eof) {
        ULONG k;
        for (k = 0; k < n; k++) {
            UBYTE b = g->ring[(t + k) & g->rmask];
            d[k] = b;
            if (term_hit(g, b)) { *hit = 1; k++; break; }
        }
        n = k;
    } else {
        i = t & g->rmask;
        first = g->rsize - i;
        if (first > n) first = n;
        CopyMem(g->ring + i, d, first);
        if (n > first) CopyMem(g->ring, d + first, n - first);
    }
    g->rtail = t + n;
    return n;
}

/* the active read served from the ring; when the ring is empty and the read not done, the interrupt takes it over
 * (rd_direct).  Forbid held, Disable not held. */
static void rd_service(struct G2Ser *g)
{
    for (;;) {
        struct IOExtSer *io = g->rd;
        ULONG n;
        int hit;
        if (!io || g->rd_direct) return;
        if (g->hold & HOLD_STOP) { io->IOSer.io_Flags &= ~IOF_QUICK; return; }
        n = ring_copy(g, g->rd_ptr, g->rd_left, &hit);
        if (n) {
            g->rd_ptr += n; g->rd_left -= n;
            rx_unthrottle(g, 0);
            if (g->rx_hold) { Disable(); rx_resume(g); Enable(); }
            if (!g->rd_left || hit) { Disable(); rd_finish(g, 0); Enable(); continue; }
            continue;
        }
        Disable();
        if (g->rx_err) {
            BYTE e = g->rx_err;
            g->rx_err = 0;
            rd_finish(g, e);
            Enable();
            continue;
        }
        if (g->rhead == g->rtail) {
            io->IOSer.io_Flags &= ~IOF_QUICK;
            g->rd_direct = 1;
            rx_resume(g);
            Enable();
            return;
        }
        Enable();
    }
}

/* the ring and the hardware's receive FIFO emptied (Disable held) */
static void rx_discard(struct G2Ser *g)
{
    g->rtail = g->rhead;
    g->rx_err = 0;
    if (g->path == PATH_FIFO) {
        ULONG n = g->bd[BD_RXCOUNT];
        while (n--) (void)g->bd[BD_RXDATA1];
    }
}

/* ---- parameters ----------------------------------------------------------------------------------------------- */

static ULONG pow2_up(ULONG n)
{
    ULONG p = RBUF_MIN;
    if (n > RBUF_MAX) n = RBUF_MAX;
    while (p < n) p <<= 1;
    return p;
}

/* the hardware rate: SERPER = round(colour clock / baud) - 1 (bit 15: 9-bit receive for 8 bits plus parity), or the
 * board's BAUD register */
static void program_rate(struct G2Ser *g)
{
    if (g->path == PATH_FIFO) {
        g->bd[BD_BAUDH] = (UWORD)(g->baud >> 16);
        g->bd[BD_BAUDL] = (UWORD)g->baud;
    } else {
        ULONG p = (g->colour_hz + g->baud / 2) / g->baud - 1;
        if (g->readlen == 8 && g->par) p |= 0x8000;
        CUSTOM->serper = (UWORD)p;
    }
}

/* parameters from a request: checked, the buffer replaced if its size changes, then applied under Disable */
static BYTE set_params(struct G2Ser *g, struct IOExtSer *io)
{
    ULONG baud = io->io_Baud ? io->io_Baud : g->baud;
    ULONG rlen = io->io_RBufLen ? pow2_up(io->io_RBufLen) : g->rsize;
    UBYTE f = io->io_SerFlags, rl = io->io_ReadLen, wl = io->io_WriteLen, sb = io->io_StopBits, par = 0;
    ULONG ext = io->io_ExtFlags, i;
    UBYTE *nring = NULL, *oring = NULL;
    ULONG osize = 0;

    if (f & SERF_RAD_BOOGIE) { rl = wl = 8; f &= ~(SERF_PARTY_ON | SERF_PARTY_ODD); f |= SERF_XDISABLED; }
    if (rl == 0) rl = g->readlen;
    if (wl == 0) wl = g->writelen;
    if (sb == 0) sb = g->stopbits;
    if ((rl != 7 && rl != 8) || (wl != 7 && wl != 8) || (sb != 1 && sb != 2)) return SerErr_InvParam;
    if (f & SERF_PARTY_ON) {
        if (ext & SEXTF_MSPON) par = (ext & SEXTF_MARK) ? 3 : 4;
        else par = (f & SERF_PARTY_ODD) ? 2 : 1;
    }
    if (g->path == PATH_FIFO) {
        /* the board's wire is 8N1: 7 bits plus parity or stop fit it, 8 bits plus parity do not */
        if (par && (rl == 8 || wl == 8)) return SerErr_InvParam;
        if (baud < FIFO_BAUD_MIN || baud > FIFO_BAUD_MAX) return SerErr_BaudMismatch;
    } else {
        ULONG p;
        if (baud < 110 || baud > g->colour_hz / 2) return SerErr_BaudMismatch;
        p = (g->colour_hz + baud / 2) / baud;
        if (p < 2 || p > 0x8000) return SerErr_BaudMismatch;
    }
    if (rlen != g->rsize) {
        nring = AllocMem(rlen, MEMF_PUBLIC);
        if (!nring) return SerErr_BufErr;
    }

    Disable();
    if (nring) {
        oring = g->ring; osize = g->rsize;
        g->ring = nring; g->rsize = rlen; g->rmask = rlen - 1;
        g->rtail = g->rhead;
        g->rx_hi = rlen - rlen / 4; g->rx_lo = rlen / 4;
    }
    g->baud = baud; g->rbuflen = g->rsize;
    g->readlen = rl; g->writelen = wl; g->stopbits = sb; g->par = par;
    g->serflags = (g->serflags & SERF_SHARED) | (f & ~SERF_SHARED);
    g->extflags = ext;
    g->ctlchar = io->io_CtlChar;
    g->xon = (UBYTE)(g->ctlchar >> 24); g->xoff = (UBYTE)(g->ctlchar >> 16);
    g->xon_on = !(g->serflags & SERF_XDISABLED);
    if (!g->xon_on) { g->hold &= ~HOLD_XOFF; g->stat &= ~IO_STATF_XOFFWRITE; }
    g->brktime = io->io_BrkTime;
    g->term = io->io_TermArray;
    g->eof = (g->serflags & SERF_EOFMODE) != 0;
    for (i = 0; i < 32; i++) g->term_map[i] = 0;
    for (i = 0; i < 8; i++) {
        UBYTE c = (UBYTE)((i < 4 ? g->term.TermArray0 : g->term.TermArray1) >> (24 - 8 * (i & 3)));
        g->term_map[c >> 3] |= 1 << (c & 7);
    }
    g->rx_dmask = (1 << rl) - 1;
    g->rx_stop = 1 << (rl + (par ? 1 : 0));
    g->tx_dmask = (1 << wl) - 1;
    g->tx_stop = (sb == 2 ? 3 : 1) << (wl + (par ? 1 : 0));
    g->fast_rx = rl == 8 && !par && !g->xon_on && !g->eof;
    g->fast_tx = wl == 8 && !par && sb == 1;
    g->brk_detect = g->path == PATH_PAULA;
    if (g->path == PATH_FIFO) {
        g->tx_cap = (g->serflags & SERF_7WIRE) ? FIFO_TXCAP_7WIRE : FIFO_N;
        g->bd[BD_TXTHRESH] = FIFO_N - g->tx_cap / 2;
        g->fast_tx = wl == 8 && !par;    /* the second stop bit is not on the board's 8N1 wire */
    }
    program_rate(g);
    if (!(g->serflags & SERF_7WIRE)) rts_assert();
    if (nring) rx_resume(g);
    if (g->wr || g->ctl_tx) tx_kick(g);   /* a hold this change released (xON/xOFF off) */
    Enable();
    if (oring) FreeMem(oring, osize);
    rx_unthrottle(g, 1);
    io->io_RBufLen = g->rsize;
    return 0;
}

static void defaults(struct G2Ser *g, struct IOExtSer *io, UBYTE flags)
{
    zero(&io->io_CtlChar, sizeof(struct IOExtSer) - offsetof(struct IOExtSer, io_CtlChar));
    io->io_CtlChar = SER_DEFAULT_CTLCHAR;
    io->io_RBufLen = g->rsize ? g->rsize : RBUF_DEFAULT;
    io->io_Baud = 9600;
    io->io_BrkTime = 250000;
    io->io_ReadLen = io->io_WriteLen = 8;
    io->io_StopBits = 1;
    io->io_SerFlags = flags & (SERF_XDISABLED | SERF_7WIRE | SERF_RAD_BOOGIE | SERF_SHARED);
}

static void fill_request(struct G2Ser *g, struct IOExtSer *io)
{
    io->io_CtlChar = g->ctlchar;
    io->io_RBufLen = g->rsize;
    io->io_ExtFlags = g->extflags;
    io->io_Baud = g->baud;
    io->io_BrkTime = g->brktime;
    io->io_TermArray = g->term;
    io->io_ReadLen = g->readlen;
    io->io_WriteLen = g->writelen;
    io->io_StopBits = g->stopbits;
    io->io_SerFlags = (g->serflags & ~SERF_SHARED) | (io->io_SerFlags & SERF_SHARED);
}

/* ---- aborting ------------------------------------------------------------------------------------------------- */

static void abort_queue(struct MinList *l)
{
    struct IOExtSer *io;
    while ((io = (struct IOExtSer *)RemHead((struct List *)l)) != NULL) {
        io->IOSer.io_Error = IOERR_ABORTED;
        io->IOSer.io_Actual = 0;
        io->IOSer.io_Flags = (io->IOSer.io_Flags & ~(G2_IOSERF_QUEUED | G2_IOSERF_ACTIVE)) | G2_IOSERF_ABORT;
        reply(io);
    }
}

/* every request ended: queued ones, the active read and write, a break in progress (Disable not held) */
static void abort_all(struct G2Ser *g)
{
    Disable();
    abort_queue(&g->rq);
    abort_queue(&g->wq);
    if (g->rd) { g->rd->IOSer.io_Flags |= G2_IOSERF_ABORT; rd_finish(g, IOERR_ABORTED); }
    if (g->wr && !(g->brk_busy && g->brk_req == g->wr)) {
        g->wr->IOSer.io_Flags |= G2_IOSERF_ABORT; wr_finish(g, IOERR_ABORTED);
    }
    Enable();
    if (g->brk_busy) {
        g->brk_abort = 1;
        AbortIO(&g->brk_tr.tr_node);
        while (g->brk_busy) ;            /* the soft interrupt runs as soon as AbortIO has replied */
    }
    Disable();
    g->rd_direct = 0;
    g->hold &= ~HOLD_BRK;
    Enable();
}

/* ---- hardware open and close ---------------------------------------------------------------------------------- */

static int env_says_paula(void)
{
    struct Task *me = FindTask(NULL);
    char buf[8];
    int paula = 0;
    if (me->tc_Node.ln_Type != NT_PROCESS) return 0;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 36);
    if (!DOSBase) return 0;
    if (GetVar("G2SERIAL", buf, sizeof buf, GVF_GLOBAL_ONLY) > 0)
        paula = (buf[0] | 0x20) == 'p';
    CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
    return paula;
}

static void hw_close(struct G2Ser *g)
{
    /* the timers: no more soft interrupts from here */
    if (g->timer_open) {
        Disable();
        g->tport.mp_Flags = PA_IGNORE;
        Enable();
        if (g->cts_busy) {
            AbortIO(&g->cts_tr.tr_node);
            while (!CheckIO(&g->cts_tr.tr_node)) ;
            g->cts_busy = 0;
        }
        if (g->brk_busy) {
            AbortIO(&g->brk_tr.tr_node);
            while (!CheckIO(&g->brk_tr.tr_node)) ;
            g->brk_busy = 0;
        }
        Disable();
        while (GetMsg(&g->tport)) ;
        Enable();
        CloseDevice(&g->cts_tr.tr_node);
        g->timer_open = 0;
    }
    if (g->server_on) {
        Disable();
        g->ctrl = 0;               /* interrupts off, the serial bridge back to Paula */
        board_ctrl(g);
        Enable();
        RemIntServer(INTB_PORTS, &g->ports_is);
        g->server_on = 0;
    }
    if (g->cd) { g->cd->cd_Driver = NULL; g->cd->cd_Flags |= CDF_CONFIGME; g->cd = NULL; }
    if (g->vectors_on) {
        CUSTOM->intena = INTF_RBF | INTF_TBE;
        CUSTOM->intreq = INTF_RBF | INTF_TBE;
        CUSTOM->adkcon = ADKF_UARTBRK;
        SetIntVector(INTB_RBF, g->old_rbf);
        SetIntVector(INTB_TBE, g->old_tbe);
        g->vectors_on = 0;
    }
    if (g->have_misc & 2) {
        *CIAB_PRA = (*CIAB_PRA & ~(CIAF_DTR | CIAF_RTS)) | (g->old_pra & (CIAF_DTR | CIAF_RTS));
        *CIAB_DDRA = (*CIAB_DDRA & ~(CIAF_DTR | CIAF_RTS)) | (g->old_ddra & (CIAF_DTR | CIAF_RTS));
        FreeMiscResource(MR_SERIALBITS);
    }
    if (g->have_misc & 1) FreeMiscResource(MR_SERIALPORT);
    g->have_misc = 0;
    if (g->ring) { FreeMem(g->ring, g->rsize); g->ring = NULL; g->rsize = 0; }
}

/* wait (at most 1 s) for the board's TX FIFO to empty, so the last write is on the wire before the bridge goes
 * back to Paula; a timerequest on a port that signals SIGB_SINGLE */
static void fifo_drain(struct G2Ser *g)
{
    struct MsgPort p;
    struct timerequest t;
    int i;
    if (!g->server_on || !g->timer_open) return;
    zero(&p, sizeof p);
    p.mp_Node.ln_Type = NT_MSGPORT;
    p.mp_Flags = PA_SIGNAL;
    p.mp_SigBit = SIGB_SINGLE;
    p.mp_SigTask = FindTask(NULL);
    new_list(&p.mp_MsgList);
    CopyMem(&g->cts_tr, &t, sizeof t);
    t.tr_node.io_Message.mn_ReplyPort = &p;
    for (i = 0; i < 100 && g->bd[BD_TXSPACE] < FIFO_N; i++) {
        SetSignal(0, SIGF_SINGLE);
        t.tr_node.io_Command = TR_ADDREQUEST;
        t.tr_time.tv_secs = 0;
        t.tr_time.tv_micro = 10000;
        DoIO(&t.tr_node);
    }
    SetSignal(0, SIGF_SINGLE);
}

static BYTE hw_open(struct G2Ser *g, struct IOExtSer *io, ULONG oflags)
{
    struct IOExtSer init;
    BYTE e;

    g->misc = OpenResource(MISCNAME);
    MiscBase = g->misc;
    if (!MiscBase) return IOERR_OPENFAIL;
    if (AllocMiscResource(MR_SERIALPORT, dev_name)) return SerErr_DevBusy;
    g->have_misc |= 1;
    if (AllocMiscResource(MR_SERIALBITS, dev_name)) { hw_close(g); return SerErr_DevBusy; }
    g->have_misc |= 2;
    g->old_ddra = *CIAB_DDRA; g->old_pra = *CIAB_PRA;

    /* the path */
    g->path = PATH_PAULA;
    g->bd = NULL;
    ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 33);
    if (ExpansionBase) {
        struct ConfigDev *cd = FindConfigDev(NULL, G2FIFO_MANUF, G2FIFO_PROD);
        /* a configured board without a driver carries CDF_CONFIGME (expansion.library: waiting for a driver); the
         * driver that takes it clears the flag, and sets it again when it lets the board go (hw_close) */
        if (cd && !(oflags & G2SER_OPENF_PAULA) && !env_says_paula() &&
            (cd->cd_Driver == NULL || cd->cd_Driver == (APTR)g)) {
            g->cd = cd; cd->cd_Driver = (APTR)g; cd->cd_Flags &= ~CDF_CONFIGME;
            g->bd = (volatile UWORD *)cd->cd_BoardAddr;
            g->path = PATH_FIFO;
        }
        CloseLibrary((struct Library *)ExpansionBase);
        ExpansionBase = NULL;
    }
    g->colour_hz = SysBase->ex_EClockFrequency ? SysBase->ex_EClockFrequency * 5 :
                   (SysBase->VBlankFrequency == 50 ? 3546895 : 3579545);

    /* the ring */
    g->rsize = RBUF_DEFAULT; g->rmask = RBUF_DEFAULT - 1;
    g->ring = AllocMem(g->rsize, MEMF_PUBLIC);
    if (!g->ring) { g->rsize = 0; hw_close(g); return SerErr_BufErr; }
    g->rhead = g->rtail = 0;
    g->rx_hi = g->rsize - g->rsize / 4; g->rx_lo = g->rsize / 4;
    g->throttled = g->rx_hold = 0; g->rx_err = 0;
    g->rd = g->wr = NULL; g->rd_direct = 0; g->tx_on = 0; g->hold = 0; g->ctl_tx = 0; g->stat = 0;
    new_list((struct List *)&g->rq);
    new_list((struct List *)&g->wq);

    /* timer.device: PA_SOFTINT reply port */
    g->soft_is.is_Node.ln_Type = NT_INTERRUPT;
    g->soft_is.is_Node.ln_Pri = 0;
    g->soft_is.is_Node.ln_Name = (char *)dev_name;
    g->soft_is.is_Data = g;
    g->soft_is.is_Code = soft_stub;
    zero(&g->tport, sizeof g->tport);
    g->tport.mp_Node.ln_Type = NT_MSGPORT;
    g->tport.mp_Flags = PA_SOFTINT;
    g->tport.mp_SigTask = &g->soft_is;
    new_list(&g->tport.mp_MsgList);
    zero(&g->cts_tr, sizeof g->cts_tr);
    g->cts_tr.tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    g->cts_tr.tr_node.io_Message.mn_ReplyPort = &g->tport;
    g->cts_tr.tr_node.io_Message.mn_Length = sizeof(struct timerequest);
    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, &g->cts_tr.tr_node, 0)) { hw_close(g); return SerErr_TimerErr; }
    CopyMem(&g->cts_tr, &g->brk_tr, sizeof g->brk_tr);
    g->timer_open = 1;
    g->cts_busy = g->brk_busy = g->brk_abort = 0;
    g->brk_req = NULL;

    /* RS-232 lines: DTR and RTS outputs, asserted */
    __asm volatile ("bclr #7,0xbfd000\n\tbclr #6,0xbfd000\n\tor.b #0xc0,0xbfd200" ::: "memory", "cc");

    /* the parameters: defaults with the opener's flags */
    g->baud = 9600; g->readlen = g->writelen = 8; g->stopbits = 1; g->serflags = 0;
    defaults(g, &init, io->io_SerFlags);
    init.io_RBufLen = g->rsize;

    if (g->path == PATH_FIFO) {
        Disable();
        g->ctrl = 0; board_ctrl(g);
        g->bd[BD_RXTHRESH] = FIFO_RXTHRESH;
        g->bd[BD_TXTHRESH] = FIFO_N / 2;
        g->tx_cap = FIFO_N;
        Enable();
        e = set_params(g, &init);
        if (e) { hw_close(g); return e; }
        g->ports_is.is_Node.ln_Type = NT_INTERRUPT;
        g->ports_is.is_Node.ln_Pri = 5;
        g->ports_is.is_Node.ln_Name = (char *)dev_name;
        g->ports_is.is_Data = g;
        g->ports_is.is_Code = ports_stub;
        AddIntServer(INTB_PORTS, &g->ports_is);
        g->server_on = 1;
        Disable();
        g->ctrl = CTRL_FIFO; board_ctrl(g);  /* rate first (set_params), then the bridge to the board */
        rx_discard(g);
        g->ctrl = CTRL_FIFO | CTRL_RXIE; board_ctrl(g);
        Enable();
    } else {
        e = set_params(g, &init);
        if (e) { hw_close(g); return e; }
        g->rbf_is.is_Node.ln_Type = NT_INTERRUPT;
        g->rbf_is.is_Node.ln_Name = (char *)dev_name;
        g->rbf_is.is_Data = g;
        g->rbf_is.is_Code = rbf_stub;
        g->tbe_is.is_Node.ln_Type = NT_INTERRUPT;
        g->tbe_is.is_Node.ln_Name = (char *)dev_name;
        g->tbe_is.is_Data = g;
        g->tbe_is.is_Code = tbe_stub;
        Disable();
        CUSTOM->intena = INTF_RBF | INTF_TBE;
        g->old_rbf = SetIntVector(INTB_RBF, &g->rbf_is);
        g->old_tbe = SetIntVector(INTB_TBE, &g->tbe_is);
        g->vectors_on = 1;
        (void)CUSTOM->serdatr;
        CUSTOM->intreq = INTF_RBF | INTF_TBE;
        CUSTOM->intena = INTF_SETCLR | INTF_RBF;
        Enable();
    }
    return 0;
}

/* ---- device vectors ------------------------------------------------------------------------------------------- */

struct G2Ser *dev_init(struct G2Ser *g, BPTR seg, struct ExecBase *sb)
{
    SysBase = sb;
    g->seglist = seg;
    g->dev.dd_Library.lib_Node.ln_Type = NT_DEVICE;
    g->dev.dd_Library.lib_Node.ln_Name = (char *)dev_name;
    g->dev.dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    g->dev.dd_Library.lib_Version = G2SER_VERSION;
    g->dev.dd_Library.lib_Revision = G2SER_REVISION;
    g->dev.dd_Library.lib_IdString = (APTR)dev_id;
    new_list((struct List *)&g->rq);
    new_list((struct List *)&g->wq);
    return g;
}

static BPTR expunge(struct G2Ser *g)
{
    BPTR seg;
    if (g->dev.dd_Library.lib_OpenCnt || g->opening) {
        g->dev.dd_Library.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seg = g->seglist;
    Remove(&g->dev.dd_Library.lib_Node);
    FreeMem((UBYTE *)g - g->dev.dd_Library.lib_NegSize,
            g->dev.dd_Library.lib_NegSize + g->dev.dd_Library.lib_PosSize);
    return seg;
}

BPTR dev_expunge(struct G2Ser *g) { return expunge(g); }

void dev_open(struct IOExtSer *io, ULONG unit, ULONG oflags, struct G2Ser *g)
{
    BYTE e = 0;
    g->dev.dd_Library.lib_OpenCnt++;
    if (unit != 0) e = IOERR_OPENFAIL;
    else if (g->opening) e = SerErr_DevBusy;
    else if (g->dev.dd_Library.lib_OpenCnt > 1) {
        if (!g->shared || !(io->io_SerFlags & SERF_SHARED)) e = SerErr_DevBusy;
    } else {
        g->opening = 1;
        e = hw_open(g, io, oflags);
        g->opening = 0;
        if (!e) g->shared = (io->io_SerFlags & SERF_SHARED) != 0;
    }
    if (e) {
        g->dev.dd_Library.lib_OpenCnt--;
        io->IOSer.io_Error = e;
        io->IOSer.io_Device = NULL;
        return;
    }
    g->dev.dd_Library.lib_Flags &= ~LIBF_DELEXP;
    g->unit.unit_OpenCnt++;
    io->IOSer.io_Error = 0;
    io->IOSer.io_Device = &g->dev;
    io->IOSer.io_Unit = &g->unit;
    io->IOSer.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    fill_request(g, io);
}

BPTR dev_close(struct IOExtSer *io, struct G2Ser *g)
{
    io->IOSer.io_Device = (APTR)-1;
    io->IOSer.io_Unit = (APTR)-1;
    g->unit.unit_OpenCnt--;
    if (g->dev.dd_Library.lib_OpenCnt == 1) {
        g->opening = 1;
        abort_all(g);
        fifo_drain(g);
        hw_close(g);
        g->opening = 0;
    }
    if (--g->dev.dd_Library.lib_OpenCnt == 0 && (g->dev.dd_Library.lib_Flags & LIBF_DELEXP)) return expunge(g);
    return 0;
}

/* returns 1 when the request is complete (to be replied unless IOF_QUICK) */
static int cmd(struct G2Ser *g, struct IOExtSer *io)
{
    switch (io->IOSer.io_Command) {
    case CMD_READ:
        io->IOSer.io_Actual = 0;
        if (!io->IOSer.io_Length) return 1;
        Disable();
        if (g->rd) {
            io->IOSer.io_Flags = (io->IOSer.io_Flags & ~IOF_QUICK) | G2_IOSERF_QUEUED;
            AddTail((struct List *)&g->rq, &io->IOSer.io_Message.mn_Node);
            Enable();
            return 0;
        }
        io->IOSer.io_Flags |= G2_IOSERF_ACTIVE;
        g->rd = io; g->rd_ptr = io->IOSer.io_Data; g->rd_left = io->IOSer.io_Length; g->rd_direct = 0;
        Enable();
        rd_service(g);                      /* completes and replies it, or hands it to the interrupt */
        return 0;

    case CMD_WRITE:
        io->IOSer.io_Actual = 0;
        if (!io->IOSer.io_Length || (io->IOSer.io_Length == (ULONG)-1 && !*(UBYTE *)io->IOSer.io_Data)) return 1;
        /* fall through */
    queue_tx:
        Disable();
        io->IOSer.io_Flags &= ~IOF_QUICK;
        if (g->wr) {
            io->IOSer.io_Flags |= G2_IOSERF_QUEUED;
            AddTail((struct List *)&g->wq, &io->IOSer.io_Message.mn_Node);
        } else wr_start(g, io);
        tx_kick(g);
        Enable();
        return 0;

    case SDCMD_BREAK:
        io->IOSer.io_Actual = 0;
        if ((io->io_SerFlags & SERF_QUEUEDBRK) || g->brk_busy) goto queue_tx;
        Disable();
        io->IOSer.io_Flags = (io->IOSer.io_Flags & ~IOF_QUICK) | G2_IOSERF_ACTIVE;
        start_break(g, io);
        Enable();
        return 0;

    case CMD_CLEAR:
        Disable();
        rx_discard(g);
        g->stat &= ~(IO_STATF_OVERRUN | IO_STATF_READBREAK);
        rx_resume(g);
        Enable();
        rx_unthrottle(g, 1);
        return 1;

    case CMD_RESET: {
        struct IOExtSer d;
        BYTE e;
        abort_all(g);
        defaults(g, &d, g->serflags);
        d.io_RBufLen = RBUF_DEFAULT;
        e = set_params(g, &d);
        Disable();
        rx_discard(g);
        g->hold &= HOLD_CTS;
        g->stat = 0;
        rx_resume(g);
        tx_kick(g);
        Enable();
        rx_unthrottle(g, 1);
        io->IOSer.io_Error = e;
        return 1;
    }

    case CMD_FLUSH:
        Disable();
        abort_queue(&g->rq);
        abort_queue(&g->wq);
        Enable();
        return 1;

    case CMD_STOP:
        Disable();
        g->hold |= HOLD_STOP;
        g->rd_direct = 0;
        Enable();
        return 1;

    case CMD_START:
        Disable();
        g->hold &= ~HOLD_STOP;
        tx_kick(g);
        Enable();
        rd_service(g);
        return 1;

    case CMD_UPDATE:
        return 1;

    case SDCMD_QUERY: {
        ULONG n;
        Disable();
        io->io_Status = (*CIAB_PRA & 0xFC) | g->stat;
        g->stat &= ~(IO_STATF_OVERRUN | IO_STATF_READBREAK | IO_STATF_WROTEBREAK);
        n = g->rhead - g->rtail;
        if (g->path == PATH_FIFO) n += g->bd[BD_RXCOUNT];
        Enable();
        io->IOSer.io_Actual = n;
        return 1;
    }

    case SDCMD_SETPARAMS:
        io->IOSer.io_Error = set_params(g, io);
        return 1;

    default:
        io->IOSer.io_Error = IOERR_NOCMD;
        return 1;
    }
}

void dev_beginio(struct IOExtSer *io, struct G2Ser *g)
{
    int done;
    io->IOSer.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->IOSer.io_Flags &= ~(G2_IOSERF_ACTIVE | G2_IOSERF_ABORT | G2_IOSERF_QUEUED);
    io->IOSer.io_Error = 0;
    Forbid();
    done = cmd(g, io);
    Permit();
    if (done && !(io->IOSer.io_Flags & IOF_QUICK)) ReplyMsg(&io->IOSer.io_Message);
}

LONG dev_abortio(struct IOExtSer *io, struct G2Ser *g)
{
    Forbid();
    Disable();
    if (io == g->rd) {
        io->IOSer.io_Flags |= G2_IOSERF_ABORT;
        rd_finish(g, IOERR_ABORTED);
        Enable();
        rd_service(g);
    } else if (io == g->brk_req && g->brk_busy) {
        Enable();
        g->brk_abort = 1;
        io->IOSer.io_Flags |= G2_IOSERF_ABORT;
        AbortIO(&g->brk_tr.tr_node);
    } else if (io == g->wr) {
        io->IOSer.io_Flags |= G2_IOSERF_ABORT;
        wr_finish(g, IOERR_ABORTED);
        tx_kick(g);
        Enable();
    } else if (io->IOSer.io_Flags & G2_IOSERF_QUEUED) {
        Remove(&io->IOSer.io_Message.mn_Node);
        io->IOSer.io_Error = IOERR_ABORTED;
        io->IOSer.io_Actual = 0;
        io->IOSer.io_Flags = (io->IOSer.io_Flags & ~(G2_IOSERF_QUEUED | G2_IOSERF_ACTIVE)) | G2_IOSERF_ABORT;
        reply(io);
        Enable();
    } else Enable();
    Permit();
    return 0;
}
