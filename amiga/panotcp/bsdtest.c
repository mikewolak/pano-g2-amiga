/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* bsdtest.c -- bsdsocket.library through its standard interface (proto/bsdsocket.h), as programs use it: the library's
 * identity, a name lookup, and a TCP stream from or to amiga/panotcp/tools/sksrv.py with recv() or WaitSelect().
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 *   bsdtest <host> <port> get|put|sel <bytes> */
#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest s_tr;
static char s_buf[65536];

static ULONG ms_now(void)
{
    struct EClockVal v;
    ULONG f = ReadEClock(&v);
    return (ULONG)(((unsigned long long)v.ev_hi << 32 | v.ev_lo) * 1000u / f);
}

int main(int argc, char **argv)
{
    struct hostent *h;
    struct sockaddr_in a;
    LONG s, n, mode;
    ULONG bytes, got = 0, t0, ms, i, sum = 0, calls = 0;
    if (argc < 5) { printf("bsdtest <host> <port> get|put|sel <bytes>\n"); return 5; }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&s_tr, 0)) return 20;
    TimerBase = s_tr.tr_node.io_Device;
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4))) { printf("no bsdsocket.library 4\n"); return 20; }
    printf("%s %u.%u\n", (char *)SocketBase->lib_IdString, SocketBase->lib_Version, SocketBase->lib_Revision);
    mode = !strcmp(argv[3], "put") ? 1 : !strcmp(argv[3], "sel") ? 2 : 0;
    bytes = strtoul(argv[4], 0, 10);
    t0 = ms_now();
    if (!(h = gethostbyname(argv[1]))) { printf("%s: not found\n", argv[1]); goto out; }
    printf("%s is %s (%lu ms)\n", argv[1], Inet_NtoA(*(ULONG *)h->h_addr), ms_now() - t0);
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) { printf("socket: errno %ld\n", Errno()); goto out; }
    memset(&a, 0, sizeof a); a.sin_len = sizeof a; a.sin_family = AF_INET; a.sin_port = atoi(argv[2]);
    a.sin_addr.s_addr = *(ULONG *)h->h_addr;
    t0 = ms_now();
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { printf("connect: errno %ld\n", Errno()); CloseSocket(s); goto out; }
    printf("connected in %lu ms\n", ms_now() - t0);
    sprintf(s_buf, "%s %lu\n", mode == 1 ? "PUT" : "GET", bytes);
    send(s, s_buf, strlen(s_buf), 0);
    t0 = ms_now();
    if (mode == 1) {
        for (i = 0; i < sizeof s_buf; i++) s_buf[i] = (char)(i * 7u + 3u);
        while (got < bytes) {
            ULONG k = bytes - got > 32768u ? 32768u : bytes - got;
            if ((n = send(s, s_buf + (got & 65535u), k > 65536u - (got & 65535u) ? 65536u - (got & 65535u) : k, 0)) <= 0) { printf("send: errno %ld\n", Errno()); break; }
            got += n; calls++;
        }
        ms = ms_now() - t0;
        printf("sent %lu bytes in %lu ms: %lu KB/s, %lu calls\n", got, ms, ms ? got / ms * 1000u / 1024u : 0u, calls);
        n = recv(s, s_buf, 100, 0); if (n > 0) { s_buf[n] = 0; printf("host: %s", s_buf); }
    } else {
        if (mode == 2) { LONG nb = 1; IoctlSocket(s, FIONBIO, (char *)&nb); }
        while (got < bytes) {
            if (mode == 2) {
                fd_set rd; struct timeval tv;
                FD_ZERO(&rd); FD_SET(s, &rd); tv.tv_sec = 5; tv.tv_usec = 0;
                if (WaitSelect(s + 1, &rd, NULL, NULL, &tv, NULL) <= 0) { printf("WaitSelect: nothing in 5 s\n"); break; }
            }
            n = recv(s, s_buf, sizeof s_buf, 0);
            if (n <= 0) { if (n < 0 && Errno() == 35) continue; printf("recv: %ld errno %ld\n", n, n < 0 ? Errno() : 0); break; }
            for (i = 0; i < (ULONG)n; i++) sum += (UBYTE)s_buf[i];
            got += n; calls++;
        }
        ms = ms_now() - t0;
        printf("received %lu bytes in %lu ms: %lu KB/s, %lu calls, sum %08lx\n", got, ms, ms ? got / ms * 1000u / 1024u : 0u, calls, sum);
    }
    CloseSocket(s);
out:
    CloseLibrary(SocketBase);
    CloseDevice((struct IORequest *)&s_tr);
    return 0;
}
