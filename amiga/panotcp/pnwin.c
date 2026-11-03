/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* pnwin.c -- the Amiga side of the firmware's socket window on HD0 (pnwin.h, fw/amiga/sock.h).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  The scsi.device channel follows
 * amiga/panoflash/panoflash.c.
 *
 * A request is one write when its payload fits the channel's buffer: the request block and the payload's blocks
 * after it (fw/amiga/sock.h: SK_REQ lies just below SK_TX).  A larger payload is placed so that it ends on a block
 * boundary (pn_place): its whole blocks go first, straight from the caller's buffer, then the request block with the
 * payload's first, partial block after it.  The response is read the same way: the response, the state and a short
 * payload in one read, or the response, the state and the first partial block, then the whole blocks straight into
 * the caller's buffer.  The 68020 moves every byte once, by scsi.device's transfer loop. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <devices/trackdisk.h>
#include <proto/exec.h>
#include "pnwin.h"

#define BUFSZ (512u + PN_SMALL + 1024u + PN_SMALL)

static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static void be32_put(UBYTE *p, ULONG v) { p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v; }
static void mcopy(void *d, const void *s, ULONG n) { CopyMem((APTR)s, d, n); }

static LONG blk_io(struct PnChan *c, UWORD cmd, ULONG block, APTR buf, ULONG blocks)
{
    c->io->io_Command = cmd; c->io->io_Data = buf; c->io->io_Length = blocks * 512u; c->io->io_Offset = block * 512u;
    return DoIO((struct IORequest *)c->io) ? -1 : 0;
}

static void stat_parse(const UBYTE *b, struct PnStat *st)
{
    ULONG i;
    st->seq = be32(b + 4); st->ip = be32(b + 8); st->mask = be32(b + 12); st->gw = be32(b + 16);
    st->dns[0] = be32(b + 20); st->dns[1] = be32(b + 24); st->sess = be32(b + 28); st->frames = be32(b + 36);
    for (i = 0; i < PN_NSOCK; i++) {
        const UBYTE *e = b + 40 + i * 12u;
        st->s[i].state = e[0]; st->s[i].err = e[1]; st->s[i].pending = (UWORD)(e[2] << 8 | e[3]);
        st->s[i].rx = be32(e + 4); st->s[i].tx = be32(e + 8);
    }
}

void pn_close(struct PnChan *c)
{
    if (c->io) { if (c->io->io_Device) CloseDevice((struct IORequest *)c->io); DeleteIORequest((struct IORequest *)c->io); }
    if (c->port) DeleteMsgPort(c->port);
    if (c->buf) FreeMem(c->buf, BUFSZ);
    c->io = NULL; c->port = NULL; c->buf = NULL; c->task = NULL;
}

LONG pn_open(struct PnChan *c)
{
    UBYTE *s;
    c->task = FindTask(NULL);
    c->buf = (UBYTE *)AllocMem(BUFSZ, MEMF_PUBLIC | MEMF_CLEAR);
    c->port = c->buf ? CreateMsgPort() : NULL;
    c->io = c->port ? (struct IOStdReq *)CreateIORequest(c->port, sizeof(struct IOStdReq)) : NULL;
    if (!c->io) { pn_close(c); return -1; }
    if (OpenDevice((CONST_STRPTR)"scsi.device", 0, (struct IORequest *)c->io, 0)) { c->io->io_Device = NULL; pn_close(c); return -1; }
    s = c->buf + 512u + PN_SMALL + 512u;
    if (blk_io(c, CMD_READ, PN_STAT, s, 1) || be32(s) != 0x504E5331u) { pn_close(c); return -1; }   /* 'PNS1' */
    c->seq = be32(s + 4);
    return 0;
}

LONG pn_stat(struct PnChan *c, struct PnStat *st)
{
    UBYTE *s = c->buf + 512u + PN_SMALL + 512u;
    if (blk_io(c, CMD_READ, PN_STAT, s, 1) || be32(s) != 0x504E5331u) return -1;
    stat_parse(s, st);
    return 0;
}

void pn_begin(struct PnChan *c)
{
    ULONG i;
    UBYTE *b = c->buf;
    for (i = 0; i < 16; i++) b[i] = 0;
    c->op = b + 16; c->nops = 0;
}

void pn_op(struct PnChan *c, UBYTE code, UWORD sock, ULONG a, ULONG b, ULONG cc, ULONG d, ULONG e)
{
    UBYTE *o = c->op;
    if (c->nops >= PN_OPS) return;
    o[0] = code; o[1] = 0; o[2] = (UBYTE)(sock >> 8); o[3] = (UBYTE)sock;
    be32_put(o + 4, a); be32_put(o + 8, b); be32_put(o + 12, cc); be32_put(o + 16, d); be32_put(o + 20, e);
    c->op += 24; c->nops++;
}

ULONG pn_place(ULONG len) { return (512u - (len & 511u)) & 511u; }

LONG pn_run(struct PnChan *c, const struct PnData *d, struct PnRes *res, struct PnStat *st)
{
    UBYTE *rq = c->buf, *tx = c->buf + 512u, *rs = c->buf + 512u + PN_SMALL, *rx = rs + 1024u;
    ULONG follow = 0, i, rxblk, head = 0, got;
    if (++c->seq == 0) c->seq = 1;
    be32_put(rq, 0x504E5131u);                                   /* 'PNQ1' */
    be32_put(rq + 4, c->seq); be32_put(rq + 8, c->nops);
    if (d && d->txlen) {
        ULONG end = d->txoff + d->txlen;
        if (end <= PN_SMALL) {
            mcopy(tx + d->txoff, d->tx, d->txlen);
            follow = (end + 511u) >> 9;
        } else {                                                 /* the large path: end on a block boundary */
            head = d->txoff ? 512u - d->txoff : 0u;
            if (blk_io(c, CMD_WRITE, PN_TX + (head ? 1u : 0u), (APTR)(d->tx + head), (d->txlen - head) >> 9)) return -1;
            if (head) { mcopy(tx + d->txoff, d->tx, head); follow = 1; }
        }
    }
    be32_put(rq + 12, follow);
    if (blk_io(c, CMD_WRITE, PN_REQ, rq, 1u + follow)) return -1;
    head = 0;
    if (d && d->rxlen) {
        ULONG end = d->rxoff + d->rxlen;
        if (end <= PN_SMALL) rxblk = (end + 511u) >> 9;
        else { head = d->rxoff ? 512u - d->rxoff : 0u; rxblk = head ? 1u : 0u; }
    } else rxblk = 0;
    if (blk_io(c, CMD_READ, PN_RES, rs, 2u + rxblk)) return -1;
    if (be32(rs) != 0x504E5231u || be32(rs + 4) != c->seq) return -1;   /* 'PNR1' and this request */
    for (i = 0; i < c->nops; i++) {
        const UBYTE *r = rs + 16u + i * 20u;
        res[i].value = (LONG)be32(r); res[i].err = (LONG)be32(r + 4); res[i].x = be32(r + 8); res[i].y = be32(r + 12);
        res[i].off = be32(r + 16);
    }
    if (st) stat_parse(rs + 512u, st);
    if (!d || !d->rxlen || c->nops == 0 || res[0].value <= 0) return 0;
    got = (ULONG)res[0].value;
    if (got > d->rxlen) got = d->rxlen;
    if (d->rxoff + d->rxlen <= PN_SMALL) { mcopy(d->rx, rx + d->rxoff, got); return 0; }
    if (head) mcopy(d->rx, rx + d->rxoff, got < head ? got : head);
    if (got == d->rxlen) {                                       /* the whole blocks straight into the caller's buffer */
        if (got > head && blk_io(c, CMD_READ, PN_RX + (head ? 1u : 0u), d->rx + head, (got - head) >> 9)) return -1;
        return 0;
    }
    for (i = head; i < got; ) {                                  /* a shorter answer: through the buffer */
        ULONG o = d->rxoff + i, b0 = o >> 9, n = got - i, k;
        if (n > PN_SMALL - (o & 511u)) n = PN_SMALL - (o & 511u);
        k = ((o & 511u) + n + 511u) >> 9;
        if (blk_io(c, CMD_READ, PN_RX + b0, rx, k)) return -1;
        mcopy(d->rx + i, rx + (o & 511u), n);
        i += n;
    }
    return 0;
}
