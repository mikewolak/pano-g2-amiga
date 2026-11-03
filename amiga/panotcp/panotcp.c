/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* panotcp.c -- PanoTCP: bsdsocket.library (the AmiTCP/IP 4 and Roadshow interface, version 4) whose TCP/IP stack is
 * the firmware's lwIP, reached through the socket window of HD0 (pnwin.c, fw/amiga/sock.h).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  Function offsets and register use are those of
 * the Roadshow SDK's pragmas/bsdsocket_pragmas.h (Olaf Barthel, freely distributable), which extend AmiTCP's.
 *
 * START.  `run >NIL: PanoTCP' makes the library and adds it to exec's list at priority 10, ahead of any other
 * bsdsocket.library already there (an AmiTCP started before it keeps its own openers), resets the firmware's sockets
 * and waits; CTRL-C removes the library once no program has it open.  The process keeps the code loaded.
 *
 * BASES.  Every OpenLibrary() gets a base of its own (errno, signal masks, descriptor table, the scsi.device channel),
 * as AmiTCP gives; the firmware's sockets are shared by all bases (a descriptor names one, ReleaseSocket and
 * ObtainSocket hand one over).  One request at a time uses the window (g_lock).
 *
 * BLOCKING.  A call that must wait (accept, connect, recv, send with a full ring, WaitSelect) reads the state block
 * until its condition holds: at once for three reads, then after 1, 2, 5, 10 and 20 ms waits on timer.device, and
 * returns EINTR on the base's break signals (CTRL-C by default).  Nothing polls while no call waits. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include "pnwin.h"

#define LIB_VERSION   4
#define LIB_REVISION  1
#define DT_MAX        256        /* descriptors a base may have */
#define DT_DEFAULT    64

/* BSD numbers, as AmiTCP's sys/errno.h */
#define EINTR          4
#define EBADF          9
#define ENOMEM        12
#define EFAULT        14
#define EINVAL        22
#define EMFILE        24
#define EPIPE         32
#define EWOULDBLOCK   35
#define EINPROGRESS   36
#define ENOTSOCK      38
#define EMSGSIZE      40
#define ENOPROTOOPT   42
#define EPROTONOSUPPORT 43
#define EOPNOTSUPP    45
#define EAFNOSUPPORT  47
#define ENOTCONN      57
#define ENOSYS        78

#define AF_INET        2
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define MSG_PEEK       0x02
#define MSG_WAITALL    0x40
#define MSG_DONTWAIT   0x80
#define SOL_SOCKET     0xFFFF
#define SO_REUSEADDR   0x0004
#define SO_KEEPALIVE   0x0008
#define SO_BROADCAST   0x0020
#define SO_SNDBUF      0x1001
#define SO_RCVBUF      0x1002
#define SO_ERROR       0x1007
#define SO_TYPE        0x1008
#define IPPROTO_TCP    6
#define TCP_NODELAY    1
#define FIONBIO        0x8004667EUL
#define FIONREAD       0x4004667FUL
#define FIOASYNC       0x8004667DUL
#define SIOCATMARK     0x40047307UL
#define HOST_NOT_FOUND 1
#define UNIQUE_ID      (-1)

/* descriptor flags */
#define F_NB    0x01             /* non-blocking (FIONBIO) */
#define F_DGRAM 0x02
#define F_KEEP  0x04
#define F_REUSE 0x08
#define F_NODLY 0x10
#define F_RSVD  0x20             /* reserved by Dup2Socket(-1, fd) */

struct sockaddr_in { UBYTE sin_len, sin_family; UWORD sin_port; ULONG sin_addr; UBYTE sin_zero[8]; };
struct hostent { char *h_name; char **h_aliases; LONG h_addrtype, h_length; char **h_addr_list; };
struct servent { char *s_name; char **s_aliases; LONG s_port; char *s_proto; };
struct protoent { char *p_name; char **p_aliases; LONG p_proto; };
struct tval { ULONG tv_sec, tv_usec; };
struct iovec { APTR iov_base; ULONG iov_len; };
struct msghdr { APTR msg_name; ULONG msg_namelen; struct iovec *msg_iov; ULONG msg_iovlen; APTR msg_control; ULONG msg_controllen; LONG msg_flags; };
struct addrinfo { LONG ai_flags, ai_family, ai_socktype, ai_protocol; ULONG ai_addrlen; struct sockaddr_in *ai_addr; char *ai_canonname; struct addrinfo *ai_next; };

struct SB {
    struct Library     lib;
    struct Task       *task;
    struct PnChan      ch;
    struct MsgPort    *tport;
    struct timerequest *tr;
    LONG               err, herr;
    APTR               eptr;
    ULONG              esize;
    ULONG              sig_break, sig_io, sig_urg, sig_event;
    ULONG              dtsize;
    WORD               fd[DT_MAX];
    UBYTE              ff[DT_MAX];
    char               ntoa[20];
    struct hostent     he;
    char               hname[132];
    ULONG              haddr;
    char              *hlist[2], *hnull[1];
    struct servent     se;
    struct protoent    pe;
    char              *logtag;
    ULONG              logmask, logfacility;
    APTR               fdcallback;
};

extern struct ExecBase *SysBase;                 /* libnix's startup sets both */
extern struct DosLibrary *DOSBase;
struct Device *TimerBase;
static struct timerequest g_tr;
static struct SignalSemaphore g_lock;
static struct PnStat g_st;
static ULONG g_st_ms;
static UBYTE g_ref[PN_NSOCK];
static struct { LONG id; WORD sid; UBYTE ff, used; } g_rel[16];
static LONG g_relid = 0x10000;
static struct SB *g_master;
static char g_hostname[64] = "pano";
static const char g_name[] = "bsdsocket.library";
static const char g_idstr[] = "PanoTCP 4.1 (bsdsocket.library on the G2 firmware's lwIP)";
const char g_ver[] = "$VER: PanoTCP 4.1";

/* ---- small helpers (no C library in calls made from other tasks) ------------------------------------------------- */
static void bzero_(void *p, ULONG n) { UBYTE *b = p; while (n--) *b++ = 0; }
static ULONG slen(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (UBYTE)*a - (UBYTE)*b; }
static int sicmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y || !x) return x - y;
    }
}
static void scpy(char *d, const char *s, ULONG n) { if (!n) return; while (--n && *s) *d++ = *s++; *d = 0; }
static char *udec(char *p, ULONG v) { char t[12]; int i = 0; do { t[i++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (i) *p++ = t[--i]; *p = 0; return p; }
static void ip_text(ULONG a, char *b) { char *p = b; int k; for (k = 24; k >= 0; k -= 8) { p = udec(p, (a >> k) & 255u); if (k) *p++ = '.'; } *p = 0; }

static ULONG ms_now(void)
{
    struct EClockVal v;
    ULONG f = ReadEClock(&v);
    return (ULONG)(((unsigned long long)v.ev_hi << 32 | v.ev_lo) * 1000u / f);
}

static LONG fail(struct SB *sb, LONG e)
{
    sb->err = e;
    if (sb->eptr) {
        if (sb->esize == 1) *(UBYTE *)sb->eptr = (UBYTE)e;
        else if (sb->esize == 2) *(UWORD *)sb->eptr = (UWORD)e;
        else *(LONG *)sb->eptr = e;
    }
    return -1;
}

/* the base's channel and timer, in the calling task */
static LONG chan(struct SB *sb)
{
    struct Task *t = FindTask(NULL);
    if (sb->ch.io && sb->task == t) return 0;
    if (sb->ch.io) pn_close(&sb->ch);
    if (sb->tr) { CloseDevice((struct IORequest *)sb->tr); DeleteIORequest((struct IORequest *)sb->tr); sb->tr = NULL; }
    if (sb->tport) { DeleteMsgPort(sb->tport); sb->tport = NULL; }
    sb->task = t;
    if (pn_open(&sb->ch)) return -1;
    sb->tport = CreateMsgPort();
    sb->tr = sb->tport ? (struct timerequest *)CreateIORequest(sb->tport, sizeof(struct timerequest)) : NULL;
    if (!sb->tr || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)sb->tr, 0)) {
        if (sb->tr) { DeleteIORequest((struct IORequest *)sb->tr); sb->tr = NULL; }
        pn_close(&sb->ch);
        return -1;
    }
    return 0;
}

/* one request of n operations already built in the channel; the shared state updated */
static LONG xchg(struct SB *sb, struct PnData *d, struct PnRes *r)
{
    LONG e;
    ObtainSemaphore(&g_lock);
    e = pn_run(&sb->ch, d, r, &g_st);
    if (!e) g_st_ms = ms_now();
    ReleaseSemaphore(&g_lock);
    return e;
}

/* one operation: its value, or -1 with errno (EIO-like ENOSYS when the window does not answer) */
static LONG op1(struct SB *sb, UBYTE code, LONG sid, ULONG a, ULONG b, ULONG c, ULONG d, struct PnData *dt, struct PnRes *r)
{
    if (chan(sb)) { r->err = ENOSYS; return fail(sb, ENOSYS); }
    pn_begin(&sb->ch);
    pn_op(&sb->ch, code, (UWORD)sid, a, b, c, d, 0);
    if (xchg(sb, dt, r)) { r->err = ENOSYS; return fail(sb, ENOSYS); }
    if (r->value < 0) return fail(sb, r->err);
    return r->value;
}

/* the state, read again unless another call read it within the last millisecond */
static void refresh(struct SB *sb)
{
    if (chan(sb)) return;
    ObtainSemaphore(&g_lock);
    if (ms_now() - g_st_ms >= 1u && !pn_stat(&sb->ch, &g_st)) g_st_ms = ms_now();
    ReleaseSemaphore(&g_lock);
}

/* one step of a wait: 0 to look again, EINTR on a break signal, or the extra signals received */
static LONG pause(struct SB *sb, ULONG *step, ULONG extra, ULONG maxus)
{
    static const ULONG us[] = { 0, 0, 0, 1000, 2000, 5000, 10000, 20000 };
    ULONG t = us[*step < 7u ? *step : 7u], sigs, got;
    if (*step < 7u) (*step)++;
    if (t > maxus) t = maxus;
    sigs = SetSignal(0, 0) & (sb->sig_break | extra);
    if (!sigs && t && sb->tr) {
        sb->tr->tr_node.io_Command = TR_ADDREQUEST;
        sb->tr->tr_time.tv_secs = 0; sb->tr->tr_time.tv_micro = t;
        SendIO((struct IORequest *)sb->tr);
        got = Wait((1UL << sb->tport->mp_SigBit) | sb->sig_break | extra);
        if (!CheckIO((struct IORequest *)sb->tr)) AbortIO((struct IORequest *)sb->tr);
        WaitIO((struct IORequest *)sb->tr);
        sigs = got & (sb->sig_break | extra);
    } else if (sigs) SetSignal(0, sigs);
    if (sigs & sb->sig_break) { SetSignal(0, sigs & sb->sig_break); return EINTR; }
    return (LONG)(sigs & extra);
}

static LONG fd_sid(struct SB *sb, LONG fd)
{
    if (fd < 0 || (ULONG)fd >= sb->dtsize || sb->fd[fd] < 0) { fail(sb, EBADF); return -1; }
    return sb->fd[fd];
}
static LONG fd_new(struct SB *sb, LONG sid, UBYTE ff)
{
    ULONG i;
    for (i = 0; i < sb->dtsize; i++) if (sb->fd[i] == -1 && !(sb->ff[i] & F_RSVD)) { sb->fd[i] = (WORD)sid; sb->ff[i] = ff; return (LONG)i; }
    return -1;
}
static void sid_drop(struct SB *sb, LONG sid)
{
    struct PnRes r;
    if (sid < 0 || sid >= (LONG)PN_NSOCK) return;
    if (g_ref[sid] && --g_ref[sid]) return;
    op1(sb, PN_OP_CLOSE, sid, 0, 0, 0, 0, NULL, &r);
}

static void sa_put(APTR sa, LONG *len, ULONG ip, ULONG port)
{
    struct sockaddr_in a;
    ULONG n;
    if (!sa || !len) return;
    bzero_(&a, sizeof a); a.sin_len = sizeof a; a.sin_family = AF_INET; a.sin_port = (UWORD)port; a.sin_addr = ip;
    n = (ULONG)*len < sizeof a ? (ULONG)*len : sizeof a;
    CopyMem(&a, sa, n); *len = sizeof a;
}

/* ---- library vectors ---------------------------------------------------------------------------------------------- */
#define REG(r) __asm(#r)
#define SBP struct SB *sb REG(a6)

static struct SB *L_Open(ULONG ver REG(d0), SBP)
{
    UWORD neg = sb->lib.lib_NegSize, pos = sb->lib.lib_PosSize;
    UBYTE *m;
    struct SB *n;
    ULONG i;
    (void)ver;
    m = (UBYTE *)AllocMem(neg + pos, MEMF_PUBLIC | MEMF_CLEAR);
    if (!m) return NULL;
    CopyMem((UBYTE *)sb - neg, m, neg + sizeof(struct Library));
    CacheClearU();
    n = (struct SB *)(m + neg);
    n->lib.lib_OpenCnt = 1;
    n->sig_break = SIGBREAKF_CTRL_C;
    n->dtsize = DT_DEFAULT;
    for (i = 0; i < DT_MAX; i++) n->fd[i] = -1;
    sb->lib.lib_OpenCnt++;
    sb->lib.lib_Flags &= ~LIBF_DELEXP;
    return n;
}

static BPTR L_Close(SBP)
{
    ULONG i;
    UWORD neg = sb->lib.lib_NegSize, pos = sb->lib.lib_PosSize;
    if (sb == g_master) return 0;
    for (i = 0; i < DT_MAX; i++) if (sb->fd[i] >= 0) { LONG s = sb->fd[i]; sb->fd[i] = -1; if (sb->ch.io) sid_drop(sb, s); }
    if (sb->ch.io) pn_close(&sb->ch);
    if (sb->tr) { CloseDevice((struct IORequest *)sb->tr); DeleteIORequest((struct IORequest *)sb->tr); }
    if (sb->tport) DeleteMsgPort(sb->tport);
    g_master->lib.lib_OpenCnt--;
    FreeMem((UBYTE *)sb - neg, neg + pos);
    return 0;
}

static BPTR L_Expunge(SBP) { (void)sb; return 0; }   /* the PanoTCP process removes the library */
static ULONG L_Null(void) { return 0; }

static LONG L_socket(LONG domain REG(d0), LONG type REG(d1), LONG proto REG(d2), SBP)
{
    struct PnRes r;
    LONG sid, fd;
    (void)proto;
    if (domain != AF_INET) return fail(sb, EAFNOSUPPORT);
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return fail(sb, EPROTONOSUPPORT);
    if ((fd = fd_new(sb, 0x7FFF, 0)) < 0) return fail(sb, EMFILE);
    if ((sid = op1(sb, PN_OP_SOCKET, 0, (ULONG)type, 0, 0, 0, NULL, &r)) < 0) { sb->fd[fd] = -1; return -1; }
    sb->fd[fd] = (WORD)sid; sb->ff[fd] = type == SOCK_DGRAM ? F_DGRAM : 0; g_ref[sid] = 1;
    return fd;
}

static LONG L_bind(LONG fd REG(d0), struct sockaddr_in *a REG(a0), LONG len REG(d1), SBP)
{
    struct PnRes r;
    LONG sid = fd_sid(sb, fd);
    (void)len;
    if (sid < 0) return -1;
    if (!a) return fail(sb, EFAULT);
    return op1(sb, PN_OP_BIND, sid, a->sin_addr, a->sin_port, 0, 0, NULL, &r) < 0 ? -1 : 0;
}

static LONG L_listen(LONG fd REG(d0), LONG backlog REG(d1), SBP)
{
    struct PnRes r;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    return op1(sb, PN_OP_LISTEN, sid, (ULONG)backlog, 0, 0, 0, NULL, &r) < 0 ? -1 : 0;
}

static LONG L_accept(LONG fd REG(d0), APTR addr REG(a0), LONG *len REG(a1), SBP)
{
    struct PnRes r;
    ULONG step = 0;
    LONG sid = fd_sid(sb, fd), n, e;
    if (sid < 0) return -1;
    for (;;) {
        refresh(sb);
        if (g_st.s[sid].state != PN_S_LISTEN) return fail(sb, EINVAL);
        if (g_st.s[sid].pending) {
            if ((n = op1(sb, PN_OP_ACCEPT, sid, 0, 0, 0, 0, NULL, &r)) >= 0) {
                LONG nfd = fd_new(sb, n, 0);
                if (nfd < 0) { g_ref[n] = 1; sid_drop(sb, n); return fail(sb, EMFILE); }
                g_ref[n] = 1;
                sa_put(addr, len, r.x, r.y);
                return nfd;
            }
            if (r.err != EWOULDBLOCK) return -1;
        }
        if (sb->ff[fd] & F_NB) return fail(sb, EWOULDBLOCK);
        if ((e = pause(sb, &step, 0, ~0UL)) == EINTR) return fail(sb, EINTR);
    }
}

static LONG L_connect(LONG fd REG(d0), struct sockaddr_in *a REG(a0), LONG len REG(d1), SBP)
{
    struct PnRes r;
    ULONG step = 0;
    LONG sid = fd_sid(sb, fd);
    (void)len;
    if (sid < 0) return -1;
    if (!a) return fail(sb, EFAULT);
    if (op1(sb, PN_OP_CONNECT, sid, a->sin_addr, a->sin_port, 0, 0, NULL, &r) >= 0) return 0;
    if (r.err != EINPROGRESS) return -1;
    if (sb->ff[fd] & F_NB) return -1;
    for (;;) {
        UBYTE s = g_st.s[sid].state;
        if (s == PN_S_CONNECTED || s == PN_S_EOF) return 0;
        if (s == PN_S_ERROR) return fail(sb, g_st.s[sid].err);
        if (pause(sb, &step, 0, ~0UL) == EINTR) return fail(sb, EINTR);
        refresh(sb);
    }
}

static LONG do_sendto(struct SB *sb, LONG fd, const UBYTE *buf, LONG len, LONG flags, struct sockaddr_in *to)
{
    struct PnRes r;
    struct PnData d;
    ULONG step = 0, done = 0;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    if (len < 0) return fail(sb, EINVAL);
    if (sb->ff[fd] & F_DGRAM) {
        if ((ULONG)len > PN_WIN - 512u) return fail(sb, EMSGSIZE);
        bzero_(&d, sizeof d); d.tx = buf; d.txlen = (ULONG)len; d.txoff = (ULONG)len > PN_SMALL ? pn_place((ULONG)len) : 0;
        if (to) return op1(sb, PN_OP_SENDTO, sid, d.txoff, (ULONG)len, to->sin_addr, to->sin_port, &d, &r);
        return op1(sb, PN_OP_SEND, sid, d.txoff, (ULONG)len, 0, 0, &d, &r);
    }
    while (done < (ULONG)len) {
        ULONG k = (ULONG)len - done, room = g_st.s[sid].tx;
        UBYTE s = g_st.s[sid].state;
        if (s == PN_S_ERROR) return done ? (LONG)done : fail(sb, g_st.s[sid].err ? g_st.s[sid].err : EPIPE);
        if (s != PN_S_CONNECTED && s != PN_S_EOF && s != PN_S_CONNECTING) return done ? (LONG)done : fail(sb, s == PN_S_FREE ? EPIPE : ENOTCONN);
        if (k > 32768u) k = 32768u;
        if (k > room) k = room;
        if (!k || s == PN_S_CONNECTING) {
            if ((sb->ff[fd] & F_NB) || (flags & MSG_DONTWAIT)) return done ? (LONG)done : fail(sb, EWOULDBLOCK);
            if (pause(sb, &step, 0, ~0UL) == EINTR) return done ? (LONG)done : fail(sb, EINTR);
            refresh(sb);
            continue;
        }
        bzero_(&d, sizeof d); d.tx = buf + done; d.txlen = k; d.txoff = k > PN_SMALL ? pn_place(k) : 0;
        if (op1(sb, PN_OP_SEND, sid, d.txoff, k, 0, 0, &d, &r) < 0) {
            if (r.err == EWOULDBLOCK) continue;
            return done ? (LONG)done : -1;
        }
        done += (ULONG)r.value; step = 0;
    }
    return (LONG)done;
}

static LONG do_recvfrom(struct SB *sb, LONG fd, UBYTE *buf, LONG len, LONG flags, APTR from, LONG *fromlen)
{
    struct PnRes r;
    struct PnData d;
    ULONG step = 0, got = 0;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    if (len < 0) return fail(sb, EINVAL);
    if (!len) return 0;
    for (;;) {
        struct PnSock *s = &g_st.s[sid];
        ULONG av = s->rx, want = (ULONG)len - got;
        int dg = (sb->ff[fd] & F_DGRAM) != 0;
        if (dg ? s->pending : av) {
            if (want > PN_WIN - 512u) want = PN_WIN - 512u;
            if (!dg && want > av) want = av;
            bzero_(&d, sizeof d); d.rx = buf + got; d.rxlen = want; d.rxoff = want > PN_SMALL ? pn_place(want) : 0;
            if (op1(sb, dg ? PN_OP_RECVFROM : PN_OP_RECV, sid, want, (ULONG)(flags & MSG_PEEK), d.rxoff, 0, &d, &r) < 0) {
                if (r.err != EWOULDBLOCK) return got ? (LONG)got : -1;
            } else {
                if (dg) { sa_put(from, fromlen, r.x, r.y); return r.value; }
                got += (ULONG)r.value; step = 0;
                if (!(flags & MSG_WAITALL) || got >= (ULONG)len || (flags & MSG_PEEK) || !r.value) break;
                continue;
            }
        } else if (!dg) {
            if (s->state == PN_S_EOF || s->state == PN_S_FREE || s->state == PN_S_CLOSING) break;
            if (s->state == PN_S_ERROR) { if (got) break; return fail(sb, s->err); }
            if (s->state == PN_S_OPEN || s->state == PN_S_LISTEN) return fail(sb, ENOTCONN);
        }
        if (got && !(flags & MSG_WAITALL)) break;
        if ((sb->ff[fd] & F_NB) || (flags & MSG_DONTWAIT)) return got ? (LONG)got : fail(sb, EWOULDBLOCK);
        if (pause(sb, &step, 0, ~0UL) == EINTR) return got ? (LONG)got : fail(sb, EINTR);
        refresh(sb);
    }
    if (from && fromlen && op1(sb, PN_OP_PEER, sid, 0, 0, 0, 0, NULL, &r) >= 0) sa_put(from, fromlen, r.x, r.y);
    return (LONG)got;
}

static LONG L_sendto(LONG fd REG(d0), const UBYTE *buf REG(a0), LONG len REG(d1), LONG flags REG(d2), struct sockaddr_in *to REG(a1), LONG tolen REG(d3), SBP)
{ (void)tolen; return do_sendto(sb, fd, buf, len, flags, to); }
static LONG L_send(LONG fd REG(d0), const UBYTE *buf REG(a0), LONG len REG(d1), LONG flags REG(d2), SBP)
{ return do_sendto(sb, fd, buf, len, flags, NULL); }
static LONG L_recvfrom(LONG fd REG(d0), UBYTE *buf REG(a0), LONG len REG(d1), LONG flags REG(d2), APTR from REG(a1), LONG *fromlen REG(a2), SBP)
{ return do_recvfrom(sb, fd, buf, len, flags, from, fromlen); }
static LONG L_recv(LONG fd REG(d0), UBYTE *buf REG(a0), LONG len REG(d1), LONG flags REG(d2), SBP)
{ return do_recvfrom(sb, fd, buf, len, flags, NULL, NULL); }

static LONG L_shutdown(LONG fd REG(d0), LONG how REG(d1), SBP)
{
    struct PnRes r;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    return op1(sb, PN_OP_SHUTDOWN, sid, (ULONG)how, 0, 0, 0, NULL, &r) < 0 ? -1 : 0;
}

static LONG L_setsockopt(LONG fd REG(d0), LONG level REG(d1), LONG name REG(d2), APTR val REG(a0), LONG vlen REG(d3), SBP)
{
    struct PnRes r;
    LONG sid = fd_sid(sb, fd), v = 0;
    UBYTE bit = 0;
    if (sid < 0) return -1;
    if (val && vlen >= 4) v = *(LONG *)val; else if (val && vlen >= 1) v = *(UBYTE *)val;
    if (level == SOL_SOCKET && (name == SO_REUSEADDR || name == SO_KEEPALIVE || name == SO_BROADCAST)) bit = name == SO_REUSEADDR ? F_REUSE : name == SO_KEEPALIVE ? F_KEEP : 0;
    else if (level == IPPROTO_TCP && name == TCP_NODELAY) bit = F_NODLY;
    else return 0;                                  /* buffer sizes, timeouts, linger, IP options: accepted, not applied */
    if (bit) { if (v) sb->ff[fd] |= bit; else sb->ff[fd] &= (UBYTE)~bit; }
    return op1(sb, PN_OP_SETOPT, sid, (ULONG)level, (ULONG)name, v != 0, 0, NULL, &r) < 0 ? -1 : 0;
}

static LONG L_getsockopt(LONG fd REG(d0), LONG level REG(d1), LONG name REG(d2), APTR val REG(a0), LONG *vlen REG(a1), SBP)
{
    LONG sid = fd_sid(sb, fd), v = 0;
    if (sid < 0) return -1;
    if (!val || !vlen || *vlen < 4) return fail(sb, EINVAL);
    if (level == SOL_SOCKET) {
        switch (name) {
        case SO_ERROR: refresh(sb); v = g_st.s[sid].state == PN_S_ERROR ? g_st.s[sid].err : 0; break;
        case SO_TYPE: v = (sb->ff[fd] & F_DGRAM) ? SOCK_DGRAM : SOCK_STREAM; break;
        case SO_RCVBUF: v = 65535; break;
        case SO_SNDBUF: v = 32767; break;
        case SO_REUSEADDR: v = (sb->ff[fd] & F_REUSE) ? 1 : 0; break;
        case SO_KEEPALIVE: v = (sb->ff[fd] & F_KEEP) ? 1 : 0; break;
        default: v = 0;
        }
    } else if (level == IPPROTO_TCP && name == TCP_NODELAY) v = (sb->ff[fd] & F_NODLY) ? 1 : 0;
    else return fail(sb, ENOPROTOOPT);
    *(LONG *)val = v; *vlen = 4;
    return 0;
}

static LONG names(struct SB *sb, LONG fd, APTR a, LONG *len, UBYTE code)
{
    struct PnRes r;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    if (op1(sb, code, sid, 0, 0, 0, 0, NULL, &r) < 0) return -1;
    sa_put(a, len, r.x, r.y);
    return 0;
}
static LONG L_getsockname(LONG fd REG(d0), APTR a REG(a0), LONG *len REG(a1), SBP) { return names(sb, fd, a, len, PN_OP_NAME); }
static LONG L_getpeername(LONG fd REG(d0), APTR a REG(a0), LONG *len REG(a1), SBP) { return names(sb, fd, a, len, PN_OP_PEER); }

static LONG L_IoctlSocket(LONG fd REG(d0), ULONG req REG(d1), APTR arg REG(a0), SBP)
{
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    switch (req) {
    case FIONBIO: if (arg && *(LONG *)arg) sb->ff[fd] |= F_NB; else sb->ff[fd] &= (UBYTE)~F_NB; return 0;
    case FIONREAD: refresh(sb); if (arg) *(LONG *)arg = (LONG)g_st.s[sid].rx; return 0;
    case FIOASYNC: return 0;
    case SIOCATMARK: if (arg) *(LONG *)arg = 0; return 0;
    }
    return fail(sb, EINVAL);
}

static LONG L_CloseSocket(LONG fd REG(d0), SBP)
{
    LONG sid;
    if (fd >= 0 && (ULONG)fd < sb->dtsize && (sb->ff[fd] & F_RSVD) && sb->fd[fd] < 0) { sb->ff[fd] = 0; return 0; }
    if ((sid = fd_sid(sb, fd)) < 0) return -1;
    sb->fd[fd] = -1; sb->ff[fd] = 0;
    sid_drop(sb, sid);
    return 0;
}

/* ready conditions of a socket for WaitSelect */
static int can_read(struct SB *sb, LONG fd, LONG sid)
{
    struct PnSock *s = &g_st.s[sid];
    if (sb->ff[fd] & F_DGRAM) return s->pending != 0;
    if (s->state == PN_S_LISTEN) return s->pending != 0;
    return s->rx || s->state == PN_S_EOF || s->state == PN_S_ERROR || s->state == PN_S_FREE;
}
static int can_write(struct SB *sb, LONG fd, LONG sid)
{
    struct PnSock *s = &g_st.s[sid];
    if (sb->ff[fd] & F_DGRAM) return 1;
    return ((s->state == PN_S_CONNECTED || s->state == PN_S_EOF) && s->tx) || s->state == PN_S_ERROR || s->state == PN_S_FREE;
}

static LONG L_WaitSelect(LONG nfds REG(d0), ULONG *rd REG(a0), ULONG *wr REG(a1), ULONG *ex REG(a2), struct tval *tv REG(a3), ULONG *sigs REG(d1), SBP)
{
    ULONG step = 0, extra = sigs ? *sigs : 0, t0 = ms_now(), limit = 0, words, i;
    ULONG ord[DT_MAX / 32], owr[DT_MAX / 32];
    LONG n, e;
    if (nfds < 0) return fail(sb, EINVAL);
    if ((ULONG)nfds > sb->dtsize) nfds = (LONG)sb->dtsize;
    words = ((ULONG)nfds + 31u) >> 5;
    if (tv) limit = tv->tv_sec * 1000u + tv->tv_usec / 1000u;
    if (sigs) *sigs = 0;
    for (;;) {
        LONG fd;
        refresh(sb);
        n = 0;
        for (i = 0; i < words; i++) ord[i] = owr[i] = 0;
        for (fd = 0; fd < nfds; fd++) {
            ULONG w = (ULONG)fd >> 5, b = 1UL << (fd & 31);
            LONG sid = sb->fd[fd];
            if (sid < 0) continue;
            if (rd && (rd[w] & b) && can_read(sb, fd, sid)) { ord[w] |= b; n++; }
            if (wr && (wr[w] & b) && can_write(sb, fd, sid)) { owr[w] |= b; n++; }
        }
        if (n || (tv && ms_now() - t0 >= limit)) break;
        e = pause(sb, &step, extra, tv ? (limit - (ms_now() - t0)) * 1000u + 1000u : ~0UL);
        if (e == EINTR) return fail(sb, EINTR);
        if (e) { if (sigs) *sigs = (ULONG)e; n = 0; break; }
    }
    for (i = 0; i < words; i++) { if (rd) rd[i] = ord[i]; if (wr) wr[i] = owr[i]; if (ex) ex[i] = 0; }
    return n;
}

static LONG L_SetSocketSignals(ULONG intr REG(d0), ULONG io REG(d1), ULONG urg REG(d2), SBP)
{ sb->sig_break = intr; sb->sig_io = io; sb->sig_urg = urg; return 0; }
static LONG L_getdtablesize(SBP) { return (LONG)sb->dtsize; }

static LONG L_ObtainSocket(LONG id REG(d0), LONG dom REG(d1), LONG type REG(d2), LONG proto REG(d3), SBP)
{
    ULONG i;
    LONG fd;
    (void)dom; (void)type; (void)proto;
    Forbid();
    for (i = 0; i < 16; i++) if (g_rel[i].used && g_rel[i].id == id) break;
    if (i == 16) { Permit(); return fail(sb, EINVAL); }
    fd = fd_new(sb, g_rel[i].sid, g_rel[i].ff);
    if (fd >= 0) g_rel[i].used = 0;
    Permit();
    return fd < 0 ? fail(sb, EMFILE) : fd;
}
static LONG release(struct SB *sb, LONG fd, LONG id, int copy)
{
    ULONG i;
    LONG sid = fd_sid(sb, fd);
    if (sid < 0) return -1;
    Forbid();
    for (i = 0; i < 16 && g_rel[i].used; i++) { }
    if (i == 16) { Permit(); return fail(sb, ENOMEM); }
    if (id == UNIQUE_ID) id = g_relid++;
    g_rel[i].used = 1; g_rel[i].id = id; g_rel[i].sid = (WORD)sid; g_rel[i].ff = sb->ff[fd];
    if (copy) g_ref[sid]++; else { sb->fd[fd] = -1; sb->ff[fd] = 0; }
    Permit();
    return id;
}
static LONG L_ReleaseSocket(LONG fd REG(d0), LONG id REG(d1), SBP) { return release(sb, fd, id, 0); }
static LONG L_ReleaseCopyOfSocket(LONG fd REG(d0), LONG id REG(d1), SBP) { return release(sb, fd, id, 1); }
static LONG L_Errno(SBP) { return sb->err; }
static LONG L_SetErrnoPtr(APTR p REG(a0), LONG size REG(d0), SBP) { sb->eptr = p; sb->esize = (ULONG)size; return 0; }

static char *L_Inet_NtoA(ULONG a REG(d0), SBP) { ip_text(a, sb->ntoa); return sb->ntoa; }

/* dotted forms a, a.b, a.b.c, a.b.c.d, each part decimal, octal (0) or hex (0x); 0 on success */
static int parse_ip(const char *s, ULONG *out)
{
    ULONG p[4], n = 0, v;
    for (;;) {
        int base = 10, digits = 0;
        v = 0;
        if (*s == '0') { base = 8; if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; } }
        for (;; s++) {
            int c = *s, d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (d >= base) return -1;
            v = v * (ULONG)base + (ULONG)d; digits++;
        }
        if (!digits && base != 8) return -1;
        if (n >= 4) return -1;
        p[n++] = v;
        if (*s == '.') { s++; continue; }
        if (*s && *s != ' ' && *s != '\t' && *s != '\n') return -1;
        break;
    }
    switch (n) {
    case 1: *out = p[0]; break;
    case 2: if (p[0] > 255 || p[1] > 0xFFFFFF) return -1; *out = p[0] << 24 | p[1]; break;
    case 3: if (p[0] > 255 || p[1] > 255 || p[2] > 0xFFFF) return -1; *out = p[0] << 24 | p[1] << 16 | p[2]; break;
    default: if (p[0] > 255 || p[1] > 255 || p[2] > 255 || p[3] > 255) return -1; *out = p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    }
    return 0;
}
static ULONG L_inet_addr(const char *s REG(a0), SBP) { ULONG a; (void)sb; return s && !parse_ip(s, &a) ? a : 0xFFFFFFFFUL; }
static ULONG L_Inet_LnaOf(ULONG a REG(d0), SBP)
{ (void)sb; return (a & 0x80000000UL) == 0 ? a & 0xFFFFFF : (a & 0xC0000000UL) == 0x80000000UL ? a & 0xFFFF : a & 0xFF; }
static ULONG L_Inet_NetOf(ULONG a REG(d0), SBP)
{ (void)sb; return (a & 0x80000000UL) == 0 ? a >> 24 : (a & 0xC0000000UL) == 0x80000000UL ? a >> 16 : a >> 8; }
static ULONG L_Inet_MakeAddr(ULONG net REG(d0), ULONG host REG(d1), SBP)
{
    (void)sb;
    if (net < 128) return net << 24 | (host & 0xFFFFFF);
    if (net < 65536) return net << 16 | (host & 0xFFFF);
    return net << 8 | (host & 0xFF);
}
static ULONG L_inet_network(const char *s REG(a0), SBP)
{
    ULONG a, p[4], n = 0;
    (void)sb;
    if (!s || parse_ip(s, &a)) return 0xFFFFFFFFUL;
    for (n = 0; *s; s++) if (*s == '.') n++;
    if (n == 0) return a;
    p[0] = a >> 24; p[1] = (a >> 16) & 255; p[2] = (a >> 8) & 255; p[3] = a & 255;
    return n == 1 ? (p[0] << 8 | p[3]) : n == 2 ? (p[0] << 16 | p[1] << 8 | p[3]) : a;
}

/* a host name to an address through the firmware's resolver; 0, or -1 with h_errno */
static LONG resolve(struct SB *sb, const char *name, ULONG *ip)
{
    struct PnRes r;
    struct PnData d;
    ULONG t0 = ms_now(), step = 3, n = name ? slen(name) : 0;
    if (!n || n > 127) { sb->herr = HOST_NOT_FOUND; return -1; }
    if (!parse_ip(name, ip) && *name >= '0' && *name <= '9') return 0;
    if (!sicmp(name, "localhost")) { *ip = 0x7F000001UL; return 0; }
    if (!sicmp(name, g_hostname)) { refresh(sb); *ip = g_st.ip; return 0; }
    for (;;) {
        bzero_(&d, sizeof d); d.tx = (const UBYTE *)name; d.txlen = n;
        if (op1(sb, PN_OP_RESOLVE, 0, 0, n, 0, 0, &d, &r) >= 0) { *ip = r.x; return 0; }
        if (r.err != EINPROGRESS || ms_now() - t0 > 15000u) { sb->herr = HOST_NOT_FOUND; return -1; }
        if (pause(sb, &step, 0, 20000u) == EINTR) { sb->herr = HOST_NOT_FOUND; return -1; }
    }
}

static struct hostent *host_fill(struct SB *sb, const char *name, ULONG ip)
{
    scpy(sb->hname, name, sizeof sb->hname);
    sb->haddr = ip; sb->hlist[0] = (char *)&sb->haddr; sb->hlist[1] = NULL; sb->hnull[0] = NULL;
    sb->he.h_name = sb->hname; sb->he.h_aliases = sb->hnull; sb->he.h_addrtype = AF_INET; sb->he.h_length = 4;
    sb->he.h_addr_list = sb->hlist;
    return &sb->he;
}
static struct hostent *L_gethostbyname(const char *name REG(a0), SBP)
{
    ULONG ip;
    if (resolve(sb, name, &ip)) return NULL;
    return host_fill(sb, name, ip);
}
static struct hostent *L_gethostbyaddr(const ULONG *a REG(a0), LONG len REG(d0), LONG type REG(d1), SBP)
{
    char t[20];
    if (!a || len < 4 || type != AF_INET) { sb->herr = HOST_NOT_FOUND; return NULL; }
    ip_text(*a, t);
    return host_fill(sb, t, *a);
}
static APTR L_null(SBP) { (void)sb; return NULL; }

static const struct { const char *name; UWORD port; UBYTE tcp, udp; } g_serv[] = {
    { "ftp-data", 20, 1, 0 }, { "ftp", 21, 1, 0 }, { "ssh", 22, 1, 1 }, { "telnet", 23, 1, 0 }, { "smtp", 25, 1, 0 },
    { "time", 37, 1, 1 }, { "domain", 53, 1, 1 }, { "tftp", 69, 0, 1 }, { "gopher", 70, 1, 0 }, { "finger", 79, 1, 0 },
    { "http", 80, 1, 0 }, { "www", 80, 1, 0 }, { "pop3", 110, 1, 0 }, { "auth", 113, 1, 0 }, { "nntp", 119, 1, 0 },
    { "ntp", 123, 0, 1 }, { "imap", 143, 1, 0 }, { "irc", 194, 1, 0 }, { "https", 443, 1, 0 }, { "smtps", 465, 1, 0 },
    { "submission", 587, 1, 0 }, { "imaps", 993, 1, 0 }, { "pop3s", 995, 1, 0 }, { "ircd", 6667, 1, 0 }
};
static const char *g_ali[1];
static struct servent *serv_fill(struct SB *sb, ULONG i, const char *proto)
{
    int udp = proto && !sicmp(proto, "udp");
    if (udp ? !g_serv[i].udp : !g_serv[i].tcp) return NULL;
    sb->se.s_name = (char *)g_serv[i].name; sb->se.s_aliases = (char **)g_ali; sb->se.s_port = g_serv[i].port;
    sb->se.s_proto = udp ? (char *)"udp" : (char *)"tcp";
    return &sb->se;
}
static struct servent *L_getservbyname(const char *name REG(a0), const char *proto REG(a1), SBP)
{
    ULONG i;
    for (i = 0; name && i < sizeof g_serv / sizeof g_serv[0]; i++) if (!sicmp(name, g_serv[i].name)) { struct servent *s = serv_fill(sb, i, proto); if (s) return s; }
    return NULL;
}
static struct servent *L_getservbyport(LONG port REG(d0), const char *proto REG(a0), SBP)
{
    ULONG i;
    for (i = 0; i < sizeof g_serv / sizeof g_serv[0]; i++) if (g_serv[i].port == (UWORD)port) { struct servent *s = serv_fill(sb, i, proto); if (s) return s; }
    return NULL;
}
static const struct { const char *name; UBYTE num; } g_prot[] = { { "ip", 0 }, { "icmp", 1 }, { "tcp", 6 }, { "udp", 17 } };
static struct protoent *L_getprotobyname(const char *name REG(a0), SBP)
{
    ULONG i;
    for (i = 0; name && i < 4; i++) if (!sicmp(name, g_prot[i].name)) { sb->pe.p_name = (char *)g_prot[i].name; sb->pe.p_aliases = (char **)g_ali; sb->pe.p_proto = g_prot[i].num; return &sb->pe; }
    return NULL;
}
static struct protoent *L_getprotobynumber(LONG n REG(d0), SBP)
{
    ULONG i;
    for (i = 0; i < 4; i++) if (g_prot[i].num == n) { sb->pe.p_name = (char *)g_prot[i].name; sb->pe.p_aliases = (char **)g_ali; sb->pe.p_proto = n; return &sb->pe; }
    return NULL;
}
static void L_vsyslog(ULONG lvl REG(d0), const char *fmt REG(a0), APTR args REG(a1), SBP) { (void)lvl; (void)fmt; (void)args; (void)sb; }

static LONG L_Dup2Socket(LONG a REG(d0), LONG b REG(d1), SBP)
{
    LONG sid;
    if (a == -1) {                                  /* reserve b, or the first free descriptor */
        if (b == -1) { ULONG i; for (i = 0; i < sb->dtsize && (sb->fd[i] >= 0 || (sb->ff[i] & F_RSVD)); i++) { } if (i == sb->dtsize) return fail(sb, EMFILE); b = (LONG)i; }
        if (b < 0 || (ULONG)b >= sb->dtsize) return fail(sb, EBADF);
        if (sb->fd[b] >= 0) L_CloseSocket(b, sb);
        sb->ff[b] = F_RSVD;
        return b;
    }
    if ((sid = fd_sid(sb, a)) < 0) return -1;
    if (b == -1) { if ((b = fd_new(sb, sid, sb->ff[a])) < 0) return fail(sb, EMFILE); g_ref[sid]++; return b; }
    if (b < 0 || (ULONG)b >= sb->dtsize) return fail(sb, EBADF);
    if (b == a) return b;
    if (sb->fd[b] >= 0) L_CloseSocket(b, sb);
    sb->fd[b] = (WORD)sid; sb->ff[b] = (UBYTE)(sb->ff[a] & (UBYTE)~F_RSVD); g_ref[sid]++;
    return b;
}

/* sendmsg and recvmsg through a buffer of the whole message */
static LONG L_sendmsg(LONG fd REG(d0), struct msghdr *m REG(a0), LONG flags REG(d1), SBP)
{
    ULONG i, n = 0, o = 0;
    UBYTE *b;
    LONG r;
    if (!m) return fail(sb, EFAULT);
    for (i = 0; i < m->msg_iovlen; i++) n += m->msg_iov[i].iov_len;
    if (!(b = AllocMem(n ? n : 1, MEMF_PUBLIC))) return fail(sb, ENOMEM);
    for (i = 0; i < m->msg_iovlen; i++) { CopyMem(m->msg_iov[i].iov_base, b + o, m->msg_iov[i].iov_len); o += m->msg_iov[i].iov_len; }
    r = do_sendto(sb, fd, b, (LONG)n, flags, (struct sockaddr_in *)m->msg_name);
    FreeMem(b, n ? n : 1);
    return r;
}
static LONG L_recvmsg(LONG fd REG(d0), struct msghdr *m REG(a0), LONG flags REG(d1), SBP)
{
    ULONG i, n = 0, o = 0;
    UBYTE *b;
    LONG r, nl;
    if (!m) return fail(sb, EFAULT);
    for (i = 0; i < m->msg_iovlen; i++) n += m->msg_iov[i].iov_len;
    if (!(b = AllocMem(n ? n : 1, MEMF_PUBLIC))) return fail(sb, ENOMEM);
    nl = (LONG)m->msg_namelen;
    r = do_recvfrom(sb, fd, b, (LONG)n, flags, m->msg_name, m->msg_name ? &nl : NULL);
    for (i = 0; r > 0 && i < m->msg_iovlen && o < (ULONG)r; i++) {
        ULONG k = m->msg_iov[i].iov_len < (ULONG)r - o ? m->msg_iov[i].iov_len : (ULONG)r - o;
        CopyMem(b + o, m->msg_iov[i].iov_base, k); o += k;
    }
    if (m->msg_name) m->msg_namelen = (ULONG)nl;
    m->msg_controllen = 0; m->msg_flags = 0;
    FreeMem(b, n ? n : 1);
    return r;
}

static LONG L_gethostname(char *b REG(a0), LONG n REG(d0), SBP) { (void)sb; if (!b || n <= 0) return -1; scpy(b, g_hostname, (ULONG)n); return 0; }
static ULONG L_gethostid(SBP) { refresh(sb); return g_st.ip; }

static const char *const g_errstr[] = {
    "Undefined error: 0", "Operation not permitted", "No such file or directory", "No such process", "Interrupted system call",
    "Input/output error", "Device not configured", "Argument list too long", "Exec format error", "Bad file descriptor",
    "No child processes", "Resource deadlock avoided", "Cannot allocate memory", "Permission denied", "Bad address",
    "Block device required", "Device busy", "File exists", "Cross-device link", "Operation not supported by device",
    "Not a directory", "Is a directory", "Invalid argument", "Too many open files in system", "Too many open files",
    "Inappropriate ioctl for device", "Text file busy", "File too large", "No space left on device", "Illegal seek",
    "Read-only file system", "Too many links", "Broken pipe", "Numerical argument out of domain", "Result too large",
    "Operation would block", "Operation now in progress", "Operation already in progress", "Socket operation on non-socket",
    "Destination address required", "Message too long", "Protocol wrong type for socket", "Protocol not available",
    "Protocol not supported", "Socket type not supported", "Operation not supported", "Protocol family not supported",
    "Address family not supported by protocol family", "Address already in use", "Can't assign requested address",
    "Network is down", "Network is unreachable", "Network dropped connection on reset", "Software caused connection abort",
    "Connection reset by peer", "No buffer space available", "Socket is already connected", "Socket is not connected",
    "Can't send after socket shutdown", "Too many references: can't splice", "Operation timed out", "Connection refused",
    "Too many levels of symbolic links", "File name too long", "Host is down", "No route to host"
};
static const char *const g_herrstr[] = { "Resolver Error 0 (no error)", "Unknown host", "Host name lookup failure", "Unknown server error", "No address associated with name" };

/* SocketBaseTagList: 0, or the number of the first tag not understood */
static LONG L_SocketBaseTagList(struct TagItem *tags REG(a0), SBP)
{
    LONG idx = 0;
    struct TagItem *t;
    if (!tags) return 0;
    for (t = tags; ; t++) {
        ULONG tag = t->ti_Tag, code, ref, set;
        ULONG *vp, v;
        if (tag == TAG_DONE) break;
        if (tag == TAG_IGNORE) { idx++; continue; }
        if (tag == TAG_MORE) { t = (struct TagItem *)t->ti_Data - 1; continue; }
        if (tag == TAG_SKIP) { t += t->ti_Data; idx++; continue; }
        idx++;
        if (!(tag & TAG_USER)) return idx;
        code = (tag >> 1) & 0x3FFF; ref = tag & 0x8000; set = tag & 1;
        vp = ref ? (ULONG *)t->ti_Data : &t->ti_Data;
        v = set ? *vp : 0;
        switch (code) {
        case 1:  if (set) sb->sig_break = v; else *vp = sb->sig_break; break;            /* BREAKMASK */
        case 2:  if (set) sb->sig_io = v; else *vp = sb->sig_io; break;                  /* SIGIOMASK */
        case 3:  if (set) sb->sig_urg = v; else *vp = sb->sig_urg; break;                /* SIGURGMASK */
        case 4:  if (set) sb->sig_event = v; else *vp = sb->sig_event; break;            /* SIGEVENTMASK */
        case 6:  if (set) sb->err = (LONG)v; else *vp = (ULONG)sb->err; break;           /* ERRNO */
        case 7:  if (set) sb->herr = (LONG)v; else *vp = (ULONG)sb->herr; break;         /* HERRNO */
        case 8:  if (set) { if (v > DT_MAX) return idx; if (v > sb->dtsize) { ULONG i; for (i = sb->dtsize; i < v; i++) { sb->fd[i] = -1; sb->ff[i] = 0; } } sb->dtsize = v; }
                 else *vp = sb->dtsize; break;                                           /* DTABLESIZE */
        case 9:  if (set) sb->fdcallback = (APTR)v; else *vp = (ULONG)sb->fdcallback; break;   /* FDCALLBACK */
        case 10: if (!set) *vp = 0; break;                                               /* LOGSTAT */
        case 11: if (set) sb->logtag = (char *)v; else *vp = (ULONG)sb->logtag; break;   /* LOGTAGPTR */
        case 12: if (set) sb->logfacility = v; else *vp = sb->logfacility; break;
        case 13: if (set) sb->logmask = v; else *vp = sb->logmask; break;
        case 14: if (!set) { ULONG e = ref ? *vp : t->ti_Data; *vp = (ULONG)(e < sizeof g_errstr / sizeof g_errstr[0] ? g_errstr[e] : "Unknown error"); } break;
        case 15: if (!set) { ULONG e = ref ? *vp : t->ti_Data; *vp = (ULONG)(e < 5 ? g_herrstr[e] : "Unknown resolver error"); } break;
        case 16: case 17: case 18: if (!set) *vp = (ULONG)"I/O error"; break;            /* IO, S2, S2W errno strings */
        case 21: case 22: case 24:                                                      /* ERRNOBYTEPTR, WORD, LONG */
            if (set) { sb->eptr = (APTR)v; sb->esize = code == 21 ? 1 : code == 22 ? 2 : 4; } else *vp = (ULONG)sb->eptr; break;
        case 25: if (!set) *vp = 0; break;                                               /* HERRNOLONGPTR */
        case 29: if (!set) *vp = (ULONG)g_idstr; break;                                  /* RELEASESTRPTR */
        case 51: if (!set) *vp = 0; break;                                               /* CAN_SHARE_LIBRARY_BASES */
        case 41: case 47: case 50: case 53: case 54: case 59: case 60: case 61: case 63: case 67: case 69:
            if (!set) *vp = code == 60 ? 1 : 0; break;                                   /* the HAVE_*_API questions */
        case 64: case 65: if (!set) *vp = 0; break;                                      /* bytes received, sent */
        default: return idx;
        }
    }
    return 0;
}
static LONG L_GetSocketEvents(ULONG *mask REG(a0), SBP) { (void)sb; if (mask) *mask = 0; return -1; }

static LONG L_inet_aton(const char *s REG(a0), ULONG *a REG(a1), SBP) { ULONG v; (void)sb; if (!s || parse_ip(s, &v)) return 0; if (a) *a = v; return 1; }
static char *L_inet_ntop(LONG af REG(d0), const ULONG *src REG(a0), char *dst REG(a1), LONG size REG(d1), SBP)
{
    char t[20];
    if (af != AF_INET || !src || !dst) { fail(sb, EAFNOSUPPORT); return NULL; }
    ip_text(*src, t);
    if ((LONG)slen(t) >= size) { fail(sb, 28); return NULL; }
    scpy(dst, t, (ULONG)size);
    return dst;
}
static LONG L_inet_pton(LONG af REG(d0), const char *src REG(a0), ULONG *dst REG(a1), SBP)
{
    ULONG v, dots = 0;
    const char *p;
    if (af != AF_INET) { fail(sb, EAFNOSUPPORT); return -1; }
    for (p = src; p && *p; p++) { if (*p == '.') dots++; else if (*p < '0' || *p > '9') return 0; }
    if (!src || dots != 3 || parse_ip(src, &v)) return 0;
    *dst = v;
    return 1;
}
static LONG L_zero(SBP) { (void)sb; return 0; }

static struct hostent *L_gethostbyname_r(const char *name REG(a0), struct hostent *hp REG(a1), char *buf REG(a2), LONG len REG(d0), LONG *herr REG(a3), SBP)
{
    ULONG ip, n;
    char **l;
    if (resolve(sb, name, &ip)) { if (herr) *herr = HOST_NOT_FOUND; return NULL; }
    n = slen(name) + 1;
    if (!hp || !buf || (ULONG)len < 16 + n) { if (herr) *herr = 3; fail(sb, 34); return NULL; }
    l = (char **)buf; *(ULONG *)(buf + 12) = ip; l[0] = buf + 12; l[1] = NULL; l[2] = NULL;
    scpy(buf + 16, name, n);
    hp->h_name = buf + 16; hp->h_aliases = &l[2]; hp->h_addrtype = AF_INET; hp->h_length = 4; hp->h_addr_list = l;
    return hp;
}
static struct hostent *L_gethostbyaddr_r(const ULONG *a REG(a0), LONG alen REG(d0), LONG type REG(d1), struct hostent *hp REG(a1), char *buf REG(a2), LONG len REG(d2), LONG *herr REG(a3), SBP)
{
    char **l;
    if (!a || alen < 4 || type != AF_INET || !hp || !buf || len < 40) { if (herr) *herr = HOST_NOT_FOUND; return NULL; }
    l = (char **)buf; *(ULONG *)(buf + 12) = *a; l[0] = buf + 12; l[1] = NULL; l[2] = NULL;
    ip_text(*a, buf + 16);
    hp->h_name = buf + 16; hp->h_aliases = &l[2]; hp->h_addrtype = AF_INET; hp->h_length = 4; hp->h_addr_list = l;
    (void)sb;
    return hp;
}

/* getaddrinfo for IPv4: one result per socket type asked (or stream and datagram) */
#define EAI_FAMILY 5
#define EAI_MEMORY 6
#define EAI_NONAME 8
#define EAI_SERVICE 9
static void L_freeaddrinfo(struct addrinfo *ai REG(a0), SBP)
{
    (void)sb;
    while (ai) { struct addrinfo *n = ai->ai_next; FreeVec(ai); ai = n; }
}
static LONG L_getaddrinfo(const char *node REG(a0), const char *serv REG(a1), const struct addrinfo *hints REG(a2), struct addrinfo **res REG(a3), SBP)
{
    ULONG ip = 0, port = 0, k, nt = 0;
    LONG types[2], fam = hints ? hints->ai_family : 0, st = hints ? hints->ai_socktype : 0, flags = hints ? hints->ai_flags : 0;
    struct addrinfo *head = NULL, **tail = &head;
    if (!res) return EAI_NONAME;
    *res = NULL;
    if (fam != 0 && fam != AF_INET) return EAI_FAMILY;
    if (!node && !serv) return EAI_NONAME;
    if (serv) {
        const char *p = serv;
        while (*p >= '0' && *p <= '9') port = port * 10u + (ULONG)(*p++ - '0');
        if (*p || p == serv) {
            struct servent *s = L_getservbyname(serv, st == SOCK_DGRAM ? "udp" : "tcp", sb);
            if (!s) return EAI_SERVICE;
            port = (ULONG)s->s_port;
        }
    }
    if (node) { if (resolve(sb, node, &ip)) return EAI_NONAME; }
    else ip = (flags & 1) ? 0 : 0x7F000001UL;               /* AI_PASSIVE: any address; else the loopback */
    if (st == 0) { types[0] = SOCK_STREAM; types[1] = SOCK_DGRAM; nt = 2; } else { types[0] = st; nt = 1; }
    for (k = 0; k < nt; k++) {
        ULONG cn = (flags & 2) && node ? slen(node) + 1 : 0;   /* AI_CANONNAME */
        struct addrinfo *a = AllocVec(sizeof *a + sizeof(struct sockaddr_in) + cn, MEMF_PUBLIC | MEMF_CLEAR);
        struct sockaddr_in *sa;
        if (!a) { L_freeaddrinfo(head, sb); return EAI_MEMORY; }
        sa = (struct sockaddr_in *)(a + 1);
        sa->sin_len = sizeof *sa; sa->sin_family = AF_INET; sa->sin_port = (UWORD)port; sa->sin_addr = ip;
        a->ai_family = AF_INET; a->ai_socktype = types[k]; a->ai_protocol = types[k] == SOCK_STREAM ? 6 : 17;
        a->ai_addrlen = sizeof *sa; a->ai_addr = sa;
        if (cn) { a->ai_canonname = (char *)(sa + 1); scpy(a->ai_canonname, node, cn); }
        *tail = a; tail = &a->ai_next;
    }
    *res = head;
    return 0;
}
static const char *L_gai_strerror(LONG e REG(a0), SBP)
{
    (void)sb;
    switch (e) {
    case 0: return "Success";
    case EAI_FAMILY: return "ai_family not supported";
    case EAI_MEMORY: return "Memory allocation failure";
    case EAI_NONAME: return "No address associated with hostname";
    case EAI_SERVICE: return "Service not supported for ai_socktype";
    }
    return "Unknown error";
}
static LONG L_GetDefaultDomainName(char *b REG(a0), LONG n REG(d0), SBP) { (void)sb; if (b && n > 0) b[0] = 0; return 1; }
static LONG L_nosys(SBP) { return fail(sb, ENOSYS); }

/* ---- the call trace (PanoTCP TRACE, tstub.s; PanoTCP DUMP prints it) ---------------------------------------------- */
#define TR_N 1024
struct TrEnt { ULONG task, k, d0, d1, d2, a0, a1, err, ms; char name[8], arg[32]; };
struct Trace { ULONG magic, n; struct TrEnt e[TR_N]; };
static struct Trace *g_trc;
static void say(const char *s);
extern UBYTE trace_stubs[];

ULONG trace_in(ULONG *r);
void trace_out(ULONG *r);
static APTR g_real[137];
ULONG trace_in(ULONG *r)
{
    ULONG k = r[15];
    if (g_trc) {
        struct Task *t = FindTask(NULL);
        struct TrEnt *e;
        const char *nm = t->tc_Node.ln_Name;
        ULONG i;
        Disable();
        e = &g_trc->e[g_trc->n++ % TR_N];
        Enable();
        e->ms = ms_now(); e->task = (ULONG)t; e->k = k; e->d0 = r[0]; e->d1 = r[1]; e->d2 = r[2]; e->a0 = r[8]; e->a1 = r[9]; e->err = 0;
        for (i = 0; i < 8; i++) { e->name[i] = nm ? nm[i] : 0; if (nm && !nm[i]) nm = NULL; }
        e->arg[0] = 0;
        if ((k == 35 || k == 135) && r[8]) { const char *q = (const char *)r[8]; for (i = 0; i < 31 && q[i]; i++) e->arg[i] = q[i]; e->arg[i] = 0; }
    }
    return (ULONG)g_real[k];
}
void trace_out(ULONG *r)
{
    ULONG k = r[15];
    struct SB *sb = (struct SB *)r[14];
    if (g_trc && k > 4) {
        struct TrEnt *e;
        Disable();
        e = &g_trc->e[g_trc->n++ % TR_N];
        Enable();
        e->ms = ms_now(); e->task = (ULONG)FindTask(NULL); e->k = k | 0x8000; e->d0 = r[0]; e->d1 = r[1]; e->err = (ULONG)sb->err;
        e->d2 = e->a0 = e->a1 = 0; e->name[0] = 0; e->arg[0] = 0;
    }
}

/* ---- the vector table, by offset ---------------------------------------------------------------------------------- */
#define NVEC 136                                     /* the last offset, 0x330 (gai_strerror), is vector 136 */
static APTR g_vec[NVEC + 1];
static void vset(ULONG off, APTR f) { g_vec[off / 6u - 1u] = f; }

static void vec_build(void)
{
    ULONG i;
    for (i = 0; i < NVEC; i++) g_vec[i] = (APTR)L_nosys;
    g_vec[NVEC] = (APTR)-1;
    vset(0x006, (APTR)L_Open); vset(0x00C, (APTR)L_Close); vset(0x012, (APTR)L_Expunge); vset(0x018, (APTR)L_Null);
    vset(0x01E, (APTR)L_socket); vset(0x024, (APTR)L_bind); vset(0x02A, (APTR)L_listen); vset(0x030, (APTR)L_accept);
    vset(0x036, (APTR)L_connect); vset(0x03C, (APTR)L_sendto); vset(0x042, (APTR)L_send); vset(0x048, (APTR)L_recvfrom);
    vset(0x04E, (APTR)L_recv); vset(0x054, (APTR)L_shutdown); vset(0x05A, (APTR)L_setsockopt); vset(0x060, (APTR)L_getsockopt);
    vset(0x066, (APTR)L_getsockname); vset(0x06C, (APTR)L_getpeername); vset(0x072, (APTR)L_IoctlSocket);
    vset(0x078, (APTR)L_CloseSocket); vset(0x07E, (APTR)L_WaitSelect); vset(0x084, (APTR)L_SetSocketSignals);
    vset(0x08A, (APTR)L_getdtablesize); vset(0x090, (APTR)L_ObtainSocket); vset(0x096, (APTR)L_ReleaseSocket);
    vset(0x09C, (APTR)L_ReleaseCopyOfSocket); vset(0x0A2, (APTR)L_Errno); vset(0x0A8, (APTR)L_SetErrnoPtr);
    vset(0x0AE, (APTR)L_Inet_NtoA); vset(0x0B4, (APTR)L_inet_addr); vset(0x0BA, (APTR)L_Inet_LnaOf);
    vset(0x0C0, (APTR)L_Inet_NetOf); vset(0x0C6, (APTR)L_Inet_MakeAddr); vset(0x0CC, (APTR)L_inet_network);
    vset(0x0D2, (APTR)L_gethostbyname); vset(0x0D8, (APTR)L_gethostbyaddr); vset(0x0DE, (APTR)L_null);
    vset(0x0E4, (APTR)L_null); vset(0x0EA, (APTR)L_getservbyname); vset(0x0F0, (APTR)L_getservbyport);
    vset(0x0F6, (APTR)L_getprotobyname); vset(0x0FC, (APTR)L_getprotobynumber); vset(0x102, (APTR)L_vsyslog);
    vset(0x108, (APTR)L_Dup2Socket); vset(0x10E, (APTR)L_sendmsg); vset(0x114, (APTR)L_recvmsg);
    vset(0x11A, (APTR)L_gethostname); vset(0x120, (APTR)L_gethostid); vset(0x126, (APTR)L_SocketBaseTagList);
    vset(0x12C, (APTR)L_GetSocketEvents);
    vset(0x1CE, (APTR)L_null);                      /* ObtainInterfaceList */
    vset(0x1C8, (APTR)L_zero);                      /* ReleaseInterfaceList */
    vset(0x210, (APTR)L_zero);                      /* ReleaseDomainNameServerList */
    vset(0x216, (APTR)L_null);                      /* ObtainDomainNameServerList */
    vset(0x21C, (APTR)L_zero); vset(0x222, (APTR)L_zero); vset(0x228, (APTR)L_null);   /* setnetent, endnetent, getnetent */
    vset(0x22E, (APTR)L_zero); vset(0x234, (APTR)L_zero); vset(0x23A, (APTR)L_null);   /* protoent */
    vset(0x240, (APTR)L_zero); vset(0x246, (APTR)L_zero); vset(0x24C, (APTR)L_null);   /* servent */
    vset(0x252, (APTR)L_inet_aton); vset(0x258, (APTR)L_inet_ntop); vset(0x25E, (APTR)L_inet_pton);
    vset(0x264, (APTR)L_zero); vset(0x26A, (APTR)L_zero);                              /* In_LocalAddr, In_CanForward */
    vset(0x2B2, (APTR)L_zero);                      /* ProcessIsServer */
    vset(0x2BE, (APTR)L_GetDefaultDomainName);
    vset(0x2CA, (APTR)L_null);                      /* ObtainRoadshowData */
    vset(0x2E2, (APTR)L_gethostbyname_r); vset(0x2E8, (APTR)L_gethostbyaddr_r);
    vset(0x324, (APTR)L_freeaddrinfo); vset(0x32A, (APTR)L_getaddrinfo); vset(0x330, (APTR)L_gai_strerror);
}

/* every vector through its trace stub; the functions kept in g_real */
static void vec_trace(void)
{
    ULONG i;
    for (i = 0; i < NVEC; i++) { g_real[i + 1] = g_vec[i]; g_vec[i] = (APTR)(trace_stubs + 10u * i); }
}

static void dump(void)
{
    static const struct { UWORD off; const char *n; } nm[] = {
        {0x006,"Open"},{0x00C,"Close"},{0x01E,"socket"},{0x024,"bind"},{0x02A,"listen"},{0x030,"accept"},{0x036,"connect"},
        {0x03C,"sendto"},{0x042,"send"},{0x048,"recvfrom"},{0x04E,"recv"},{0x054,"shutdown"},{0x05A,"setsockopt"},
        {0x060,"getsockopt"},{0x066,"getsockname"},{0x06C,"getpeername"},{0x072,"IoctlSocket"},{0x078,"CloseSocket"},
        {0x07E,"WaitSelect"},{0x084,"SetSocketSignals"},{0x08A,"getdtablesize"},{0x090,"ObtainSocket"},{0x096,"ReleaseSocket"},
        {0x09C,"ReleaseCopyOfSocket"},{0x0A2,"Errno"},{0x0A8,"SetErrnoPtr"},{0x0AE,"Inet_NtoA"},{0x0B4,"inet_addr"},
        {0x0D2,"gethostbyname"},{0x0D8,"gethostbyaddr"},{0x0EA,"getservbyname"},{0x0F0,"getservbyport"},
        {0x0F6,"getprotobyname"},{0x108,"Dup2Socket"},{0x10E,"sendmsg"},{0x114,"recvmsg"},{0x11A,"gethostname"},
        {0x120,"gethostid"},{0x126,"SocketBaseTagList"},{0x12C,"GetSocketEvents"},{0x252,"inet_aton"},{0x258,"inet_ntop"},
        {0x25E,"inet_pton"},{0x2E2,"gethostbyname_r"},{0x324,"freeaddrinfo"},{0x32A,"getaddrinfo"},{0x330,"gai_strerror"}
    };
    struct SB *m;
    struct Trace *t;
    ULONG i, from;
    Forbid();
    m = (struct SB *)FindName(&SysBase->LibList, (CONST_STRPTR)g_name);
    Permit();
    if (!m || !m->lib.lib_IdString || scmp((const char *)m->lib.lib_IdString, g_idstr) || !(t = (struct Trace *)m->fdcallback) || t->magic != 0x54524331UL) {
        say("PanoTCP DUMP: no PanoTCP started with TRACE\n"); return;
    }
    from = t->n > TR_N ? t->n - TR_N : 0;
    for (i = from; i < t->n; i++) {
        struct TrEnt *e = &t->e[i % TR_N];
        ULONG k = e->k & 0x7FFF, j;
        char b[200], *p = b;
        const char *fn = "?";
        for (j = 0; j < sizeof nm / sizeof nm[0]; j++) if (nm[j].off == k * 6u) fn = nm[j].n;
        p = udec(p, i); *p++ = ' '; p = udec(p, e->ms); *p++ = ' ';
        { char h[12]; ULONG v = e->task, q; for (q = 0; q < 8; q++) h[q] = "0123456789abcdef"[(v >> (28 - 4 * q)) & 15]; h[8] = 0; scpy(p, h, 9); p += 8; }
        *p++ = ' ';
        if (e->k & 0x8000) { scpy(p, "  -> ", 6); p += 5; scpy(p, fn, 30); p += slen(p); scpy(p, " = ", 4); p += 3;
            if ((LONG)e->d0 < 0) { *p++ = '-'; p = udec(p, (ULONG)-(LONG)e->d0); } else p = udec(p, e->d0);
            scpy(p, " (0x", 5); p += 4; { ULONG q; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->d0 >> (28 - 4 * q)) & 15]; }
            scpy(p, ") errno ", 9); p += 8; p = udec(p, e->err); }
        else {
            ULONG q;
            for (q = 0; q < 8 && e->name[q]; q++) *p++ = e->name[q];
            *p++ = ' '; scpy(p, fn, 30); p += slen(p);
            if (*fn == '?') { scpy(p, " offset ", 9); p += 8; p = udec(p, k * 6u); }
            if (e->arg[0]) { *p++ = ' '; *p++ = '"'; scpy(p, e->arg, 32); p += slen(p); *p++ = '"'; }
            scpy(p, " d0=", 5); p += 4; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->d0 >> (28 - 4 * q)) & 15];
            scpy(p, " d1=", 5); p += 4; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->d1 >> (28 - 4 * q)) & 15];
            scpy(p, " d2=", 5); p += 4; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->d2 >> (28 - 4 * q)) & 15];
            scpy(p, " a0=", 5); p += 4; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->a0 >> (28 - 4 * q)) & 15];
            scpy(p, " a1=", 5); p += 4; for (q = 0; q < 8; q++) *p++ = "0123456789abcdef"[(e->a1 >> (28 - 4 * q)) & 15];
        }
        *p++ = '\n'; *p = 0;
        say(b);
    }
}

/* ---- the process -------------------------------------------------------------------------------------------------- */
static void say(const char *s) { Write(Output(), (APTR)s, (LONG)slen(s)); }

int main(int argc, char **argv)
{
    int trace = argc >= 2 && !sicmp(argv[1], "TRACE");
    struct PnChan c;
    struct PnStat st;
    struct PnRes r;
    char b[160], ip[20];
    BPTR f;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&g_tr, 0)) return 20;
    TimerBase = g_tr.tr_node.io_Device;
    if (argc >= 2 && !sicmp(argv[1], "DUMP")) { dump(); CloseDevice((struct IORequest *)&g_tr); return 0; }
    Forbid();
    {
        struct Library *l = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)g_name);
        if (l && l->lib_IdString && !scmp((const char *)l->lib_IdString, g_idstr)) { Permit(); say("PanoTCP is already running\n"); return 5; }
    }
    Permit();
    bzero_(&c, sizeof c);
    if (pn_open(&c)) { say("PanoTCP: no socket window on HD0 (AMIGA.FW with the socket service, HD0 laid out by the firmware)\n"); return 20; }
    pn_begin(&c);
    pn_op(&c, PN_OP_RESET, 0, 0, 0, 0, 0, 0);
    if (pn_run(&c, NULL, &r, &st)) { pn_close(&c); say("PanoTCP: the socket window does not answer\n"); return 20; }
    pn_close(&c);
    g_st = st;
    if ((f = Open((CONST_STRPTR)"ENV:HOSTNAME", MODE_OLDFILE))) {
        LONG n = Read(f, g_hostname, sizeof g_hostname - 1);
        Close(f);
        if (n > 0) { g_hostname[n] = 0; while (n && (g_hostname[n - 1] == '\n' || g_hostname[n - 1] == ' ')) g_hostname[--n] = 0; }
        if (!g_hostname[0]) scpy(g_hostname, "pano", sizeof g_hostname);
    }
    InitSemaphore(&g_lock);
    vec_build();
    if (trace && (g_trc = (struct Trace *)AllocMem(sizeof(struct Trace), MEMF_PUBLIC | MEMF_CLEAR))) { g_trc->magic = 0x54524331UL; vec_trace(); }
    g_master = (struct SB *)MakeLibrary(g_vec, NULL, NULL, sizeof(struct SB), 0);
    if (!g_master) { say("PanoTCP: no memory\n"); return 20; }
    g_master->lib.lib_Node.ln_Type = NT_LIBRARY;
    g_master->lib.lib_Node.ln_Name = (char *)g_name;
    g_master->lib.lib_Node.ln_Pri = 10;
    g_master->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    g_master->lib.lib_Version = LIB_VERSION;
    g_master->lib.lib_Revision = LIB_REVISION;
    g_master->lib.lib_IdString = (APTR)g_idstr;
    g_master->fdcallback = g_trc;                    /* the master's field, never a caller's: where DUMP finds the trace */
    AddLibrary(&g_master->lib);
    ip_text(st.ip, ip);
    scpy(b, "PanoTCP 4.1: bsdsocket.library 4.1 on the firmware's stack, address ", sizeof b);
    scpy(b + slen(b), ip, sizeof b - slen(b));
    scpy(b + slen(b), "\n", sizeof b - slen(b));
    say(b);
    for (;;) {
        Wait(SIGBREAKF_CTRL_C);
        Forbid();
        if (g_master->lib.lib_OpenCnt == 0) {
            Remove(&g_master->lib.lib_Node);
            Permit();
            FreeMem((UBYTE *)g_master - g_master->lib.lib_NegSize, g_master->lib.lib_NegSize + g_master->lib.lib_PosSize);
            say("PanoTCP: removed\n");
            break;
        }
        Permit();
        say("PanoTCP: programs still have the library open\n");
    }
    CloseDevice((struct IORequest *)&g_tr);
    return 0;
}
