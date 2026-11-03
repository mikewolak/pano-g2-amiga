/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* sktest.c -- the firmware's socket window driven directly (pnwin.c), for measurement: the window's state, a name
 * lookup, and a TCP stream from or to a host.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 *   sktest stat
 *   sktest dns  <name>
 *   sktest get  <host> <port> <bytes>    sends "GET <bytes>\n", receives until <bytes> or the end of the stream
 *   sktest put  <host> <port> <bytes>    sends "PUT <bytes>\n" and <bytes> of pattern, then reads the answer line
 * The host side of get and put is amiga/panotcp/tools/sksrv.py. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pnwin.h"

struct Device *TimerBase;
static struct timerequest s_tr;
static struct PnChan s_c;
static struct PnStat s_st;
static UBYTE *s_buf;

static ULONG ms_now(void)
{
    struct EClockVal v;
    ULONG f = ReadEClock(&v);
    return (ULONG)(((unsigned long long)v.ev_hi << 32 | v.ev_lo) * 1000u / f);
}

static void ipstr(ULONG a, char *b) { sprintf(b, "%lu.%lu.%lu.%lu", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255); }

static LONG one(UBYTE code, UWORD s, ULONG a, ULONG b, ULONG c, ULONG d, struct PnData *dt, struct PnRes *r)
{
    pn_begin(&s_c);
    pn_op(&s_c, code, s, a, b, c, d, 0);
    if (pn_run(&s_c, dt, r, &s_st)) { printf("window: no answer\n"); return -2; }
    return r->value;
}

static LONG resolve(const char *name, ULONG *ip)
{
    struct PnData d; struct PnRes r; ULONG t0 = ms_now();
    memset(&d, 0, sizeof d); d.tx = (const UBYTE *)name; d.txlen = strlen(name);
    for (;;) {
        if (one(PN_OP_RESOLVE, 0, 0, d.txlen, 0, 0, &d, &r) == 0) { *ip = r.x; return 0; }
        if (r.err != 36 || ms_now() - t0 > 10000u) { printf("resolve %s: errno %ld\n", name, r.err); return -1; }
        Delay(1);
    }
}

static LONG tcp_open(const char *host, ULONG port)
{
    struct PnRes r; ULONG ip, t0; LONG s;
    char b[20];
    if (resolve(host, &ip)) return -1;
    if ((s = one(PN_OP_SOCKET, 0, 1, 0, 0, 0, NULL, &r)) < 0) { printf("socket: errno %ld\n", r.err); return -1; }
    one(PN_OP_SETOPT, (UWORD)s, 6, 1, 1, 0, NULL, &r);
    one(PN_OP_CONNECT, (UWORD)s, ip, port, 0, 0, NULL, &r);
    t0 = ms_now();
    while (s_st.s[s].state == PN_S_CONNECTING && ms_now() - t0 < 10000u) pn_stat(&s_c, &s_st);
    ipstr(ip, b);
    if (s_st.s[s].state != PN_S_CONNECTED) { printf("connect %s:%lu: state %u errno %u\n", b, port, s_st.s[s].state, s_st.s[s].err); return -1; }
    printf("connected to %s:%lu in %lu ms, socket %ld\n", b, port, ms_now() - t0, s);
    return s;
}

static LONG send_all(LONG s, const UBYTE *p, ULONG n, ULONG *xch)
{
    struct PnData d; struct PnRes r;
    while (n) {
        ULONG k = n, room = s_st.s[s].tx;
        if (k > 32768u) k = 32768u;
        if (k > room) k = room;
        if (!k) { pn_stat(&s_c, &s_st); continue; }
        memset(&d, 0, sizeof d); d.tx = p; d.txlen = k; d.txoff = k > PN_SMALL ? pn_place(k) : 0;
        if (one(PN_OP_SEND, (UWORD)s, d.txoff, k, 0, 0, &d, &r) < 0) {
            if (r.err == 35) { pn_stat(&s_c, &s_st); continue; }
            printf("send: errno %ld\n", r.err); return -1;
        }
        p += r.value; n -= (ULONG)r.value; (*xch)++;
    }
    return 0;
}

static void transfer(int put, const char *host, ULONG port, ULONG bytes)
{
    struct PnData d; struct PnRes r;
    char line[64];
    ULONG got = 0, t0, ms, xch = 0, polls = 0, crc = 0;
    LONG s = tcp_open(host, port);
    if (s < 0) return;
    sprintf(line, "%s %lu\n", put ? "PUT" : "GET", bytes);
    if (send_all(s, (const UBYTE *)line, strlen(line), &xch)) return;
    t0 = ms_now();
    if (put) {
        ULONG i, left = bytes;
        for (i = 0; i < 65536u; i++) s_buf[i] = (UBYTE)(i * 7u + 3u);
        while (left) { ULONG k = left > 65536u ? 65536u : left; if (send_all(s, s_buf, k, &xch)) return; left -= k; }
        ms = ms_now() - t0;
        printf("sent %lu bytes in %lu ms: %lu KB/s, %lu exchanges\n", bytes, ms, ms ? bytes / ms * 1000u / 1024u : 0u, xch);
    }
    for (;;) {                                           /* get: the data; put: the answer line */
        ULONG want = put ? 63u - got : bytes - got, av = s_st.s[s].rx;
        if (!want) break;
        if (!av) {
            if (s_st.s[s].state == PN_S_EOF || s_st.s[s].state == PN_S_ERROR) break;
            pn_stat(&s_c, &s_st); polls++;
            continue;
        }
        if (want > av) want = av;
        if (want > PN_WIN - 512u) want = PN_WIN - 512u;
        memset(&d, 0, sizeof d);
        d.rx = put ? (UBYTE *)line + got : s_buf; d.rxlen = want; d.rxoff = want > PN_SMALL ? pn_place(want) : 0;
        if (one(PN_OP_RECV, (UWORD)s, want, 0, d.rxoff, 0, &d, &r) <= 0) { printf("recv: %ld errno %ld\n", r.value, r.err); break; }
        if (!put) { ULONG i; for (i = 0; i < (ULONG)r.value; i++) crc += s_buf[i]; }
        got += (ULONG)r.value; xch++;
        if (put && memchr(line, '\n', got)) break;
    }
    ms = ms_now() - t0;
    if (put) { line[got < 63 ? got : 63] = 0; printf("host: %s", line); }
    else printf("received %lu bytes in %lu ms: %lu KB/s, %lu exchanges, %lu polls, sum %08lx\n", got, ms,
                ms ? got / ms * 1000u / 1024u : 0u, xch, polls, crc);
    one(PN_OP_CLOSE, (UWORD)s, 0, 0, 0, 0, NULL, &r);
}

int main(int argc, char **argv)
{
    char a[20], b[20], g[20], n1[20], n2[20];
    struct PnRes r;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&s_tr, 0)) return 20;
    TimerBase = s_tr.tr_node.io_Device;
    if (pn_open(&s_c)) { printf("no socket window (AMIGA.FW with -DAMIGA_SOCK, HD0 laid out by hd_create)\n"); return 20; }
    s_buf = AllocMem(65536u, MEMF_PUBLIC);
    pn_stat(&s_c, &s_st);
    if (argc >= 2 && !strcmp(argv[1], "stat")) {
        ULONG i;
        ipstr(s_st.ip, a); ipstr(s_st.mask, b); ipstr(s_st.gw, g); ipstr(s_st.dns[0], n1); ipstr(s_st.dns[1], n2);
        printf("address %s mask %s gateway %s dns %s %s session %08lx frames %lu last %lu\n", a, b, g, n1, n2, s_st.sess, s_st.frames, s_st.seq);
        for (i = 0; i < PN_NSOCK; i++)
            if (s_st.s[i].state) printf("socket %2lu: state %u errno %u pending %u rx %lu tx %lu\n", i, s_st.s[i].state, s_st.s[i].err, s_st.s[i].pending, s_st.s[i].rx, s_st.s[i].tx);
    } else if (argc >= 2 && !strcmp(argv[1], "reset")) {
        printf("reset: %ld\n", one(PN_OP_RESET, 0, 0, 0, 0, 0, NULL, &r));
    } else if (argc >= 3 && !strcmp(argv[1], "dns")) {
        ULONG ip, t0 = ms_now();
        if (!resolve(argv[2], &ip)) { ipstr(ip, a); printf("%s: %s (%lu ms)\n", argv[2], a, ms_now() - t0); }
    } else if (argc >= 5 && (!strcmp(argv[1], "get") || !strcmp(argv[1], "put"))) {
        transfer(argv[1][0] == 'p', argv[2], strtoul(argv[3], 0, 10), strtoul(argv[4], 0, 10));
    } else printf("sktest stat | reset | dns <name> | get|put <host> <port> <bytes>\n");
    FreeMem(s_buf, 65536u);
    pn_close(&s_c);
    CloseDevice((struct IORequest *)&s_tr);
    return 0;
}
