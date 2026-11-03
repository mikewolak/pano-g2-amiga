/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* zio_amiga.c -- lrzsz's modem stream on AmigaOS (zio.h): an Exec serial device, or the in-memory test pipe.
 *
 * Serial device: one CMD_READ at a time; a read with a timeout queries the bytes waiting (SDCMD_QUERY) and takes
 * them at once, else sends a 1-byte CMD_READ and a timer.device request and waits for the first of the two, then
 * takes what else has arrived.  Output is collected in a 4,096-byte buffer and written by CMD_WRITE when lrzsz
 * flushes stdout or the buffer is full.
 *
 * Test pipe: two programs on one Amiga join a public port of that name; each side writes its own 64 KB ring and
 * reads the other's, and signals the other side (SIGBREAKF_CTRL_F) after it writes or reads.  It carries the
 * protocol between two processes with no serial hardware, for the test of the port.
 *
 * Ctrl-C (SIGBREAKF_CTRL_C) at a wait cancels: the cancel string (canit) is sent and the program exits with 130.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  Part of the lrzsz 0.12.20 port (GPL-2). */
#define ZIO_IMPL
#include "zio.h"
#include <stdarg.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/serial.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>

unsigned long zio_baud;

static struct MsgPort     *s_port, *t_port;
static struct IOExtSer    *s_rd, *s_wr, *s_ctl;
static struct timerequest *t_req;
static int                 s_open, t_open, s_rd_busy, s_wr_busy;
static UBYTE               s_rd_byte;
static UBYTE               o_buf[2][4096];               /* two output buffers: one fills while one is written */
static size_t              o_n;
static int                 o_cur;

/* the test pipe */
#define ZP_RING 65536u
typedef struct {
    struct MsgPort mp;                                   /* a public port: only its name is used */
    char           name[32];
    struct Task   *task[2];
    ULONG          users;
    volatile ULONG head[2], tail[2];                     /* ring k is written by side k */
    UBYTE          ring[2][ZP_RING];
} zpipe_t;
static zpipe_t *zp;
static int      zp_side;
static zio_status_t *st;                                 /* the parent's status block */

static void zio_cancelled(void);

/* the trace (TRACE=file): one line a stream call, appended by DOS directly and the file closed again, so it can
 * be read while the program runs */
static const char *tr_name;
static void tr(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    int n;
    BPTR fh;
    if (!tr_name) return;
    va_start(ap, fmt); n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n <= 0) return;
    if ((fh = Open((CONST_STRPTR)tr_name, MODE_READWRITE)) != 0) {
        Seek(fh, 0, OFFSET_END);
        Write(fh, b, n < (int)sizeof b ? n : (int)sizeof b - 1);
        Close(fh);
    }
}

/* ---- waits ---- */

/* the timer started for ds tenths of a second */
static void t_start(unsigned int ds)
{
    t_req->tr_node.io_Command = TR_ADDREQUEST;
    t_req->tr_time.tv_secs = ds / 10u;
    t_req->tr_time.tv_micro = (ds % 10u) * 100000u;
    SendIO((struct IORequest *)t_req);
}

static void t_stop(void)
{
    if (!CheckIO((struct IORequest *)t_req)) AbortIO((struct IORequest *)t_req);
    WaitIO((struct IORequest *)t_req);
}

/* ---- the serial device ---- */

static void s_wait_write(void)
{
    if (s_wr_busy) {
        ULONG sig = 1ul << s_port->mp_SigBit;
        while (!CheckIO((struct IORequest *)s_wr)) {
            if (Wait(sig | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
                AbortIO((struct IORequest *)s_wr); WaitIO((struct IORequest *)s_wr); s_wr_busy = 0;
                zio_cancelled();
            }
        }
        WaitIO((struct IORequest *)s_wr);
        s_wr_busy = 0;
    }
}

static int s_query(void)
{
    s_ctl->IOSer.io_Command = SDCMD_QUERY;
    DoIO((struct IORequest *)s_ctl);
    return (int)s_ctl->IOSer.io_Actual;
}

static int s_take(UBYTE *b, size_t n)
{
    int a = s_query();
    if (a <= 0) return 0;
    if ((size_t)a > n) a = (int)n;
    s_ctl->IOSer.io_Command = CMD_READ; s_ctl->IOSer.io_Data = b; s_ctl->IOSer.io_Length = (ULONG)a;
    DoIO((struct IORequest *)s_ctl);
    return (int)s_ctl->IOSer.io_Actual;
}

static int s_read(UBYTE *b, size_t n, unsigned int ds)
{
    ULONG sigs, ps = 1ul << s_port->mp_SigBit, ts = 1ul << t_port->mp_SigBit;
    int got = s_take(b, n);
    if (got > 0) return got;
    if (!s_rd_busy) {
        s_rd->IOSer.io_Command = CMD_READ; s_rd->IOSer.io_Data = &s_rd_byte; s_rd->IOSer.io_Length = 1;
        SendIO((struct IORequest *)s_rd); s_rd_busy = 1;
    }
    t_start(ds ? ds : 1u);
    for (;;) {
        if (CheckIO((struct IORequest *)s_rd)) break;
        if (CheckIO((struct IORequest *)t_req)) break;
        sigs = Wait(ps | ts | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) { t_stop(); zio_cancelled(); }
    }
    t_stop();
    if (!CheckIO((struct IORequest *)s_rd)) return -2;     /* the timeout; the 1-byte read stays queued */
    WaitIO((struct IORequest *)s_rd); s_rd_busy = 0;
    if (s_rd->IOSer.io_Error || s_rd->IOSer.io_Actual != 1) return -2;
    b[0] = s_rd_byte;
    got = 1;
    if (n > 1) { int m = s_take(b + 1, n - 1); if (m > 0) got += m; }
    return got;
}

static void s_write(const UBYTE *b, size_t n)
{
    s_wait_write();
    s_wr->IOSer.io_Command = CMD_WRITE; s_wr->IOSer.io_Data = (APTR)b; s_wr->IOSer.io_Length = (ULONG)n;
    SendIO((struct IORequest *)s_wr); s_wr_busy = 1;
}

/* ---- the test pipe ---- */

static int p_read(UBYTE *b, size_t n, unsigned int ds)
{
    int k = zp_side ^ 1, got = 0, timing = 0;
    ULONG sigs, ts = 1ul << t_port->mp_SigBit;
    for (;;) {
        ULONG h = zp->head[k], t = zp->tail[k];
        while (t != h && (size_t)got < n) { b[got++] = zp->ring[k][t]; t = (t + 1u) & (ZP_RING - 1u); }
        zp->tail[k] = t;
        if (got) {
            Forbid(); if (zp->task[k]) Signal(zp->task[k], SIGBREAKF_CTRL_F); Permit();
            if (timing) t_stop();
            return got;
        }
        if (!timing) { t_start(ds ? ds : 1u); timing = 1; }
        if (CheckIO((struct IORequest *)t_req)) { t_stop(); return -2; }
        sigs = Wait(SIGBREAKF_CTRL_F | ts | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) { t_stop(); zio_cancelled(); }
    }
}

static void p_write(const UBYTE *b, size_t n)
{
    int k = zp_side;
    while (n) {
        ULONG h = zp->head[k], t = zp->tail[k];
        if (((h + 1u) & (ZP_RING - 1u)) == t) {             /* full: wait for the reader */
            if (Wait(SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) zio_cancelled();
            continue;
        }
        while (n && ((h + 1u) & (ZP_RING - 1u)) != t) { zp->ring[k][h] = *b++; h = (h + 1u) & (ZP_RING - 1u); n--; }
        zp->head[k] = h;
        Forbid(); if (zp->task[k ^ 1]) Signal(zp->task[k ^ 1], SIGBREAKF_CTRL_F); Permit();
    }
}

static int p_open(const char *name, int side)
{
    zpipe_t *p;
    Forbid();
    p = (zpipe_t *)FindPort((CONST_STRPTR)name);
    if (!p) {
        if ((p = (zpipe_t *)AllocMem(sizeof *p, MEMF_PUBLIC | MEMF_CLEAR)) == NULL) { Permit(); return -1; }
        strncpy(p->name, name, sizeof p->name - 1);
        p->mp.mp_Node.ln_Name = p->name; p->mp.mp_Node.ln_Type = NT_MSGPORT; p->mp.mp_Flags = PA_IGNORE;
        NewList(&p->mp.mp_MsgList);
        AddPort(&p->mp);
    }
    p->users++;
    p->task[side] = FindTask(NULL);
    Permit();
    zp = p; zp_side = side;
    return 0;
}

static void p_close(void)
{
    if (!zp) return;
    Forbid();
    zp->task[zp_side] = NULL;
    if (--zp->users == 0) { RemPort(&zp->mp); FreeMem(zp, sizeof *zp); }
    Permit();
    zp = NULL;
}

/* ---- the stream ---- */

static void o_flush(void)
{
    if (!o_n) return;
    tr("write %lu\n", (unsigned long)o_n);
    if (st) st->out += o_n;
    if (zp) p_write(o_buf[o_cur], o_n);
    else if (s_open) { s_write(o_buf[o_cur], o_n); o_cur ^= 1; }
    o_n = 0;
}

void zio_trace(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    tr("%s", b);
}

int zio_access(const char *p, int m)
{
    int r;
    tr("access \"%s\"\n", p);
    r = access(p, m);
    tr("  -> %d\n", r);
    return r;
}

int zio_stat(const char *p, struct stat *b)
{
    int r;
    tr("stat \"%s\"\n", p);
    r = stat(p, b);
    tr("  -> %d\n", r);
    return r;
}

int zio_printf(const char *fmt, ...)
{
    va_list ap;
    int r;
    tr("printf \"%s\"\n", fmt);
    va_start(ap, fmt); r = vprintf(fmt, ap); va_end(ap);
    tr("  -> %d\n", r);
    return r;
}

FILE *zio_fopen(const char *name, const char *mode)
{
    FILE *f = fopen(name, mode);
    tr("fopen \"%s\" %s -> %s\n", name, mode, f ? "ok" : "fail");
    return f;
}

int zio_putc(int c)
{
    o_buf[o_cur][o_n++] = (UBYTE)c;
    if (o_n == sizeof o_buf[0]) o_flush();
    return c & 0xFF;
}

static void o_put(const UBYTE *b, size_t n)
{
    while (n) {
        size_t k = sizeof o_buf[0] - o_n;
        if (k > n) k = n;
        memcpy(o_buf[o_cur] + o_n, b, k); o_n += k; b += k; n -= k;
        if (o_n == sizeof o_buf[0]) o_flush();
    }
}

size_t zio_fwrite(const void *p, size_t s, size_t n, FILE *f)
{
    if (f != stdout) return fwrite(p, s, n, f);
    o_put((const UBYTE *)p, s * n);
    return n;
}

int zio_fflush(FILE *f)
{
    if (f != stdout) return fflush(f);
    o_flush();
    return 0;
}

ssize_t zio_wrfd(int fd, const void *buf, size_t n)
{
    if (fd > 1) return write(fd, buf, n);
    o_put((const UBYTE *)buf, n); o_flush();
    return (ssize_t)n;
}

int zio_read_timeout(void *buf, size_t n, unsigned int ds)
{
    int r;
    o_flush();
    tr("read %lu %u\n", (unsigned long)n, ds);
    r = zp ? p_read((UBYTE *)buf, n, ds) : s_read((UBYTE *)buf, n, ds);
    tr("  -> %d\n", r);
    if (st && r > 0) st->in += (unsigned long)r;
    return r;
}

ssize_t zio_rdfd(int fd, void *buf, size_t n)
{
    int r;
    if (fd != 0) return read(fd, buf, n);
    r = zio_read_timeout(buf, n, 600u);
    return r < 0 ? 0 : r;
}

int zio_avail(void)
{
    if (zp) { int k = zp_side ^ 1; return (int)((zp->head[k] - zp->tail[k]) & (ZP_RING - 1u)); }
    return s_open ? s_query() : 0;
}

void zio_purge(void)
{
    if (zp) { int k = zp_side ^ 1; zp->tail[k] = zp->head[k]; return; }
    if (!s_open) return;
    s_ctl->IOSer.io_Command = CMD_CLEAR;
    DoIO((struct IORequest *)s_ctl);
}

void zio_break(void)
{
    if (zp || !s_open) return;
    o_flush(); s_wait_write();
    s_ctl->IOSer.io_Command = SDCMD_BREAK;
    DoIO((struct IORequest *)s_ctl);
}

/* Ctrl-C: the cancel string (8 CAN, 8 backspaces as canit sends) and exit */
static void zio_cancelled(void)
{
    static const UBYTE can[] = { 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 };
    static int in;
    if (!in) { in = 1; o_n = 0; o_put(can, sizeof can); o_flush(); }
    fprintf(stderr, "cancelled\n");
    zio_exit(130);
}

void zio_exit(int code)
{
    static int in;
    if (!in) { in = 1; o_flush(); if (s_open) s_wait_write(); }
    tr("exit %d\n", code);
    zio_close();
    tr("closed\n");
    if (st) { st->code = (unsigned long)code; st->done = 1; st = NULL; }
    exit(code);
}

void zio_close(void)
{
    if (s_open) {
        if (s_wr_busy) { AbortIO((struct IORequest *)s_wr); WaitIO((struct IORequest *)s_wr); s_wr_busy = 0; }
        if (s_rd_busy) { AbortIO((struct IORequest *)s_rd); WaitIO((struct IORequest *)s_rd); s_rd_busy = 0; }
        CloseDevice((struct IORequest *)s_rd); s_open = 0;
    }
    if (s_ctl) { DeleteIORequest((struct IORequest *)s_ctl); s_ctl = NULL; }
    if (s_wr) { DeleteIORequest((struct IORequest *)s_wr); s_wr = NULL; }
    if (s_rd) { DeleteIORequest((struct IORequest *)s_rd); s_rd = NULL; }
    if (s_port) { DeleteMsgPort(s_port); s_port = NULL; }
    p_close();
    if (t_open) { CloseDevice((struct IORequest *)t_req); t_open = 0; }
    if (t_req) { DeleteIORequest((struct IORequest *)t_req); t_req = NULL; }
    if (t_port) { DeleteMsgPort(t_port); t_port = NULL; }
}

int zio_open(const zio_cfg_t *c)
{
    if (!(t_port = CreateMsgPort()) ||
        !(t_req = (struct timerequest *)CreateIORequest(t_port, sizeof *t_req)) ||
        OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)t_req, 0)) {
        fprintf(stderr, "timer.device did not open\n"); zio_close(); return -1;
    }
    t_open = 1;
    if (c->status && c->status->magic == ZIO_STATUS_MAGIC) { st = c->status; st->task = FindTask(NULL); }
    if (c->trace) { BPTR fh = Open((CONST_STRPTR)c->trace, MODE_NEWFILE); if (fh) Close(fh); tr_name = c->trace; }
    tr("open %s\n", c->device ? c->device : "pipe");
    if (!c->device) {
        if (p_open(c->pipe ? c->pipe : "lrzsz.pipe", c->side)) { fprintf(stderr, "no memory for the pipe\n"); zio_close(); return -1; }
        zio_baud = 115200u;
        return 0;
    }
    if (!(s_port = CreateMsgPort()) ||
        !(s_rd = (struct IOExtSer *)CreateIORequest(s_port, sizeof *s_rd)) ||
        !(s_wr = (struct IOExtSer *)CreateIORequest(s_port, sizeof *s_wr)) ||
        !(s_ctl = (struct IOExtSer *)CreateIORequest(s_port, sizeof *s_ctl))) {
        fprintf(stderr, "out of memory\n"); zio_close(); return -1;
    }
    s_rd->io_SerFlags = SERF_XDISABLED | (c->rtscts ? SERF_7WIRE : 0);
    if (OpenDevice((CONST_STRPTR)c->device, c->unit, (struct IORequest *)s_rd, 0)) {
        fprintf(stderr, "%s unit %lu did not open (error %d)\n", c->device, c->unit, s_rd->IOSer.io_Error);
        zio_close(); return -1;
    }
    s_open = 1;
    *s_wr = *s_rd; *s_ctl = *s_rd;
    s_ctl->IOSer.io_Command = SDCMD_SETPARAMS;
    s_ctl->io_Baud = c->baud; s_ctl->io_ReadLen = 8; s_ctl->io_WriteLen = 8; s_ctl->io_StopBits = 1;
    s_ctl->io_RBufLen = c->rbuf; s_ctl->io_SerFlags = SERF_XDISABLED | (c->rtscts ? SERF_7WIRE : 0);
    s_ctl->io_ExtFlags = 0;
    if (DoIO((struct IORequest *)s_ctl)) {
        fprintf(stderr, "%s: SDCMD_SETPARAMS %lu baud refused (error %d)\n", c->device, c->baud, s_ctl->IOSer.io_Error);
        zio_close(); return -1;
    }
    *s_rd = *s_ctl; *s_wr = *s_ctl;
    zio_baud = c->baud;
    return 0;
}

/* the leading NAME=value arguments of the stream; the rest stays for lrzsz's own options */
int zio_args(int *argc, char ***argv, zio_cfg_t *c)
{
    char **v = *argv;
    int n = *argc, i = 1;
    c->device = "serial.device"; c->unit = 0; c->baud = 115200u; c->rbuf = 16384u; c->rtscts = 0;
    c->pipe = NULL; c->side = 0; c->trace = NULL; c->status = NULL;
    for (; i < n; i++) {
        const char *a = v[i];
        if (!strncmp(a, "DEVICE=", 7)) c->device = a + 7;
        else if (!strncmp(a, "UNIT=", 5)) c->unit = strtoul(a + 5, NULL, 10);
        else if (!strncmp(a, "BAUD=", 5)) c->baud = strtoul(a + 5, NULL, 10);
        else if (!strncmp(a, "BUFFER=", 7)) c->rbuf = strtoul(a + 7, NULL, 10);
        else if (!strcmp(a, "RTSCTS")) c->rtscts = 1;
        else if (!strncmp(a, "PIPE=", 5)) { c->pipe = a + 5; c->device = NULL; }
        else if (!strncmp(a, "SIDE=", 5)) c->side = (int)strtoul(a + 5, NULL, 10) & 1;
        else if (!strncmp(a, "TRACE=", 6)) c->trace = a + 6;
        else if (!strncmp(a, "STATUS=", 7)) c->status = (zio_status_t *)strtoul(a + 7, NULL, 16);
        else if (!strncmp(a, "LOG=", 4)) freopen(a + 4, "w", stderr);
        else break;
    }
    v[i - 1] = v[0];
    *argv = v + i - 1; *argc = n - i + 1;
    return 0;
}
