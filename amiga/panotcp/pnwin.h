/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* pnwin.h -- the Amiga side of the firmware's socket window on HD0 (fw/amiga/sock.h): one task's channel to
 * scsi.device unit 0 and the exchange of a request and its response.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef PNWIN_H
#define PNWIN_H
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>

#define PN_REQ     511u
#define PN_TX      512u
#define PN_RES     766u
#define PN_STAT    767u
#define PN_RX      768u
#define PN_WIN     65536u
#define PN_OPS     20u
#define PN_NSOCK   32u
#define PN_SMALL   4096u            /* payloads up to this go through the channel's buffer, in the request's write */

#define PN_OP_RESET     1
#define PN_OP_SOCKET    2
#define PN_OP_BIND      3
#define PN_OP_LISTEN    4
#define PN_OP_ACCEPT    5
#define PN_OP_CONNECT   6
#define PN_OP_SEND      7
#define PN_OP_SENDTO    8
#define PN_OP_RECV      9
#define PN_OP_RECVFROM 10
#define PN_OP_SHUTDOWN 11
#define PN_OP_CLOSE    12
#define PN_OP_SETOPT   13
#define PN_OP_NAME     14
#define PN_OP_PEER     15
#define PN_OP_RESOLVE  16

#define PN_S_FREE       0
#define PN_S_OPEN       1
#define PN_S_LISTEN     2
#define PN_S_CONNECTING 3
#define PN_S_CONNECTED  4
#define PN_S_EOF        5
#define PN_S_ERROR      6
#define PN_S_CLOSING    7

struct PnSock { UBYTE state, err; UWORD pending; ULONG rx, tx; };
struct PnStat {
    ULONG seq, ip, mask, gw, dns[2], sess, frames;
    struct PnSock s[PN_NSOCK];
};
struct PnRes { LONG value, err; ULONG x, y, off; };

struct PnChan {
    struct MsgPort  *port;
    struct IOStdReq *io;
    struct Task     *task;
    ULONG            seq;
    UBYTE           *buf;           /* the request block, PN_SMALL of payload; the response, the state, PN_SMALL */
    UBYTE           *op;            /* the next operation in the request block */
    ULONG            nops;
};

/* a payload or a destination of a request: PN_WIN bytes at most, at window offset `off' */
struct PnData { const UBYTE *tx; ULONG txlen, txoff; UBYTE *rx; ULONG rxlen, rxoff; };

LONG pn_open(struct PnChan *c);                  /* 0, or -1 (no scsi.device, no window) */
void pn_close(struct PnChan *c);
LONG pn_stat(struct PnChan *c, struct PnStat *st);   /* 0, or -1 */
void pn_begin(struct PnChan *c);
void pn_op(struct PnChan *c, UBYTE code, UWORD sock, ULONG a, ULONG b, ULONG cc, ULONG d, ULONG e);
/* the request with d's payload, the response's results into res[], the state into st (may be NULL) and d's
 * destination filled when res[0] reports rxlen bytes; 0, or -1 when the window did not answer */
LONG pn_run(struct PnChan *c, const struct PnData *d, struct PnRes *res, struct PnStat *st);
/* the window offset at which a payload of len bytes ends on a block boundary (the large path) */
ULONG pn_place(ULONG len);
#endif
