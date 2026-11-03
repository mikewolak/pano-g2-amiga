/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/stream.c -- see stream.h. */
#include <stdio.h>
#include <string.h>
#include "stream.h"

#define SEQ(x)      ((x) & 0xFFFu)
static int seq_diff(unsigned a, unsigned b) { int d = (int)SEQ(a - b); return d >= 0x800 ? d - 0x1000 : d; }   /* a - b, signed 12-bit */

static int tx_raw(tnp_stream_t *c, unsigned flags, unsigned seq, unsigned ack, const uint8_t *payload, size_t plen)
{
    uint8_t p[8 + TNP_MTU]; tnp_hdr_t h;
    h.flags = flags; h.seq = SEQ(seq); h.ack = SEQ(ack); h.security = 0;
    c->ack_pending = 0; c->ack_sent_ms = plat_ms();   /* this packet carries the ack */
    tnp_hdr_put(p, &h); if (plen) memcpy(p + 8, payload, plen);
    if (c->trace) hexdump("tx", p, 8u + plen);
    c->n_tx++; c->n_tx_bytes += 8u + plen;
    return send(c->s, (const char *)p, (int)(8u + plen), 0) == (int)(8u + plen) ? 0 : -1;
}

static void deliver(tnp_stream_t *c, const uint8_t *p, size_t n)
{
    size_t off = 8; unsigned ep; const uint8_t *m; size_t ml;
    while (tnp_ep_next(p, n, &off, &ep, &m, &ml)) {
        ep0_msg_t e;
        if (ep != 0u) { c->n_other_ep++; if (c->ep_cb) c->ep_cb(c->ep_ctx, ep, m, ml); continue; }
        if (!ep0_parse(m, ml, &e)) continue;
        if (e.op == EP0_INTERRUPT) { c->n_interrupt++; if (c->irq_cb && e.nwords) c->irq_cb(c->irq_ctx, e.w[0]); continue; }
        if (e.op == EP0_ERROR) c->n_error++;
        if (((c->rxt + 1u) % RXQ) == c->rxh) c->rxh = (c->rxh + 1u) % RXQ;        /* full: the oldest goes */
        c->rxq[c->rxt] = e; c->rxt = (c->rxt + 1u) % RXQ;
    }
}

/* take one datagram if there is one within `ms'.  Returns the header's flags, or -1 on none; *acked receives its ack field. */
static int rx_one(tnp_stream_t *c, unsigned ms, unsigned *ack_out, unsigned *seq_out)
{
    uint8_t r[2048]; int n; tnp_hdr_t h;
    if (plat_wait_readable(c->s, ms) <= 0) return -1;
    n = recv(c->s, (char *)r, sizeof r, 0);
    if (n < 8) return -1;
    if (c->trace) hexdump("rx", r, (size_t)n);
    if (!tnp_hdr_get(r, (size_t)n, &h) || (h.flags & TNP_F_DGRAM)) return -1;
    c->n_rx++; c->n_rx_bytes += (unsigned long)n;
    if (ack_out) *ack_out = h.ack;
    if (seq_out) *seq_out = h.seq;
    if (c->up && (h.flags & TNP_F_ACK) && n > 8) {                                  /* it carries a payload: sequence it */
        int d = seq_diff(h.seq, c->rxseq);
        if (d == 1) {
            c->rxseq = SEQ(c->rxseq + 1u);
            deliver(c, r, (size_t)n);
            /* every data packet we send already carries the ack, so a pure one is only needed when we have been quiet:
             * the device answers a batch read with a packet per word, and acknowledging each of them separately was a
             * third of all the traffic */
            if (++c->ack_pending >= 3u || (uint32_t)(plat_ms() - c->ack_sent_ms) >= 2u) {
                tx_raw(c, TNP_F_ACK, c->txseq, c->rxseq, NULL, 0);
                c->ack_pending = 0; c->ack_sent_ms = plat_ms();
            }
        }
        else if (d <= 0) { c->n_dup++; tx_raw(c, TNP_F_ACK, c->txseq, c->rxseq, NULL, 0); }
        else { c->n_nak++; tx_raw(c, TNP_F_NAK, c->txseq, c->rxseq + 1u, NULL, 0); }
    }
    return (int)h.flags;
}

int tnp_connect(tnp_stream_t *c, uint32_t ip, int trace)
{
    struct sockaddr_in a, l; unsigned k, nak_resets = 0;
    memset(c, 0, sizeof *c); c->ip = ip; c->trace = trace; c->tid = 1u; c->win_size = 1u;
    c->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (c->s == SOCK_BAD) { sprintf(c->err, "cannot open a UDP socket"); return -1; }
    /* always from the same port, so a session we left behind can be cleared with one packet rather than a sweep */
    memset(&l, 0, sizeof l); l.sin_family = AF_INET; l.sin_addr.s_addr = htonl(INADDR_ANY); l.sin_port = htons((uint16_t)TNP_CLIENT_PORT);
    (void)bind(c->s, (struct sockaddr *)&l, sizeof l);          /* taken by another client: an ephemeral port will do */
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip); a.sin_port = htons(TNP_PORT);
    if (connect(c->s, (struct sockaddr *)&a, sizeof a) != 0) { sprintf(c->err, "connect() failed"); sock_close(c->s); return -1; }
    tx_raw(c, TNP_F_RST, 0u, 0u, NULL, 0);                      /* clear whatever we left behind last time */
    { uint32_t t1 = plat_ms(); while ((uint32_t)(plat_ms() - t1) < 150u) rx_one(c, 30u, NULL, NULL); }
    for (k = 0; k < 60u; k++) {
        unsigned ack, seq; int f; uint32_t t0 = plat_ms();
        tx_raw(c, TNP_F_SYN, 0u, 0u, NULL, 0);
        while ((uint32_t)(plat_ms() - t0) < 50u) {
            f = rx_one(c, 50u - (uint32_t)(plat_ms() - t0), &ack, &seq);
            if (f < 0) continue;
            if (f & TNP_F_RST)  {                                   /* the echo of our own reset, or a refusal: try again a few times */
                if (nak_resets < 6u) { nak_resets++; plat_sleep_ms(300u); break; }
                sprintf(c->err, "the device answered RST: it refuses the connection (another host connected?)"); sock_close(c->s); return -2;
            }
            if ((f & (TNP_F_SYN | TNP_F_NAK)) == (TNP_F_SYN | TNP_F_NAK)) {
                /* a stale session from a host that died without RST: reset it and try again (a few times) */
                if (nak_resets < 6u) { uint32_t t1; nak_resets++; tx_raw(c, TNP_F_RST, 0u, 0u, NULL, 0);
                    t1 = plat_ms(); while ((uint32_t)(plat_ms() - t1) < 400u) rx_one(c, 50u, NULL, NULL);      /* swallow the echo */
                    break; }
                sprintf(c->err, "the device answered SYN+NAK: connection refused (a stale session it would not reset)"); sock_close(c->s); return -2;
            }
            if ((f & (TNP_F_SYN | TNP_F_ACK)) == (TNP_F_SYN | TNP_F_ACK)) {
                c->rxseq = seq; c->txseq = 1u; c->up = 1;
                tx_raw(c, TNP_F_ACK, c->txseq, c->rxseq, NULL, 0);
                return 0;
            }
        }
    }
    sprintf(c->err, "no answer to SYN on UDP %u in 3 s", TNP_PORT);
    sock_close(c->s); return -1;
}

void tnp_close(tnp_stream_t *c)
{
    unsigned k;
    if (!c->up) return;
    tnp_drain(c);
    for (k = 0; k < 3u; k++) {
        int f; tx_raw(c, TNP_F_RST, c->txseq, c->rxseq, NULL, 0);
        f = rx_one(c, 300u, NULL, NULL);
        if (f >= 0 && (f & TNP_F_RST)) break;
    }
    c->up = 0; sock_close(c->s);
}

/* the window: acknowledged packets leave from the front; a timeout or a NAK resends everything from the oldest */
static void win_ack(tnp_stream_t *c, unsigned ack)
{
    while (c->win_n && seq_diff(ack, c->win[0].seq) >= 0) {
        uint32_t rtt = plat_ms() - c->win[0].sent_ms;
        if (rtt < 10000u) {                                  /* a retransmitted packet's stamp is the latest send */
            c->rtt_ms = rtt;
            c->srtt_ms = c->srtt_ms ? (unsigned)((c->srtt_ms * 7u + rtt) / 8u) : rtt;
        }
        memmove(&c->win[0], &c->win[1], (c->win_n - 1u) * sizeof c->win[0]); c->win_n--;
    }
}
static int win_resend(tnp_stream_t *c)
{
    unsigned i;
    for (i = 0; i < c->win_n; i++) {
        c->n_retx++; c->n_tx++; c->n_tx_bytes += c->win[i].len; c->win[i].sent_ms = plat_ms();
        if (c->trace) hexdump("re", c->win[i].buf, c->win[i].len);
        if (send(c->s, (const char *)c->win[i].buf, (int)c->win[i].len, 0) != (int)c->win[i].len) return -1;
    }
    return 0;
}
/* service the socket once: take ACKs/NAKs/data, resend on timeout.  ms = how long to wait for something.  <0 = connection gone */
static int win_service(tnp_stream_t *c, unsigned ms)
{
    unsigned ack = 0; int f = rx_one(c, ms, &ack, NULL);
    if (f >= 0) {
        if (f & TNP_F_RST) { c->up = 0; sprintf(c->err, "the device reset the connection"); return -3; }
        if (f & TNP_F_ACK) win_ack(c, ack);
        if ((f & TNP_F_NAK) && c->win_n && seq_diff(ack, c->win[0].seq) >= 0 && seq_diff(ack, c->win[c->win_n - 1u].seq) <= 0) {
            win_ack(c, ack - 1u); if (win_resend(c)) return -1;
        }
    }
    if (c->win_n && (uint32_t)(plat_ms() - c->win[0].sent_ms) >= 40u) {
        if (++c->win[0].sent_ms == 0u) { }                              /* (keeps the compiler quiet about the field) */
        if (c->n_retx > 4000u && c->n_retx > c->n_tx / 2u) { sprintf(c->err, "the device stopped acknowledging"); return -1; }
        if (win_resend(c)) return -1;
    }
    return 0;
}

int tnp_poll(tnp_stream_t *c, unsigned ms) { return win_service(c, ms); }

void tnp_set_window(tnp_stream_t *c, unsigned n) { c->win_size = n < 1u ? 1u : n > 32u ? 32u : n; }

int tnp_drain(tnp_stream_t *c)
{
    uint32_t t0 = plat_ms();
    while (c->win_n) {
        int rc = win_service(c, 20u);
        if (rc) return rc;
        if ((uint32_t)(plat_ms() - t0) > 8000u) { sprintf(c->err, "packet seq %u was never acknowledged", c->win[0].seq); return -1; }
    }
    return 0;
}

static int win_push(tnp_stream_t *c, const uint8_t *payload, size_t plen)
{
    uint32_t t0 = plat_ms(); unsigned seq; tnp_hdr_t h;
    if (!c->up) return -1;
    if (!c->win_size) c->win_size = 1u;
    while (c->win_n >= c->win_size) {                                    /* no room: service until there is */
        int rc = win_service(c, 20u);
        if (rc) return rc;
        if ((uint32_t)(plat_ms() - t0) > 8000u) { sprintf(c->err, "packet seq %u was never acknowledged", c->win[0].seq); return -1; }
    }
    seq = c->txseq; c->txseq = SEQ(seq + 1u);
    h.flags = TNP_F_ACK; h.seq = seq; h.ack = c->rxseq; h.security = 0;
    tnp_hdr_put(c->win[c->win_n].buf, &h); memcpy(c->win[c->win_n].buf + 8, payload, plen);
    c->win[c->win_n].seq = seq; c->win[c->win_n].len = 8u + plen; c->win[c->win_n].sent_ms = plat_ms(); c->win_n++;
    c->n_tx++; c->n_tx_bytes += 8u + plen;
    if (c->trace) hexdump("tx", c->win[c->win_n - 1u].buf, 8u + plen);
    if (send(c->s, (const char *)c->win[c->win_n - 1u].buf, (int)(8u + plen), 0) != (int)(8u + plen)) { sprintf(c->err, "send failed"); return -1; }
    if (c->win_size == 1u) return tnp_drain(c);                          /* stop-and-wait: as before */
    return win_service(c, 0u);                                           /* take whatever ACKs are waiting, no blocking */
}

int tnp_send_ep0(tnp_stream_t *c, const uint8_t *msgs, size_t len) { return win_push(c, msgs, len); }

int tnp_send_ep(tnp_stream_t *c, unsigned ep, const uint8_t *msg, size_t mlen)
{
    uint8_t p[TNP_MTU]; size_t n = tnp_ep_put(p, 0, sizeof p, ep, msg, mlen);
    if (!n) { sprintf(c->err, "message too long for a packet"); return -1; }
    return win_push(c, p, n);
}

int tnp_wait_reply(tnp_stream_t *c, unsigned tid, ep0_msg_t *out, unsigned timeout_ms)
{
    uint32_t t0 = plat_ms();
    for (;;) {
        unsigned i;
        for (i = c->rxh; i != c->rxt; i = (i + 1u) % RXQ) if (c->rxq[i].tid == tid) {
            *out = c->rxq[i];
            for (; ((i + 1u) % RXQ) != c->rxt; i = (i + 1u) % RXQ) c->rxq[i] = c->rxq[(i + 1u) % RXQ];       /* close the gap */
            c->rxt = (c->rxt + RXQ - 1u) % RXQ;
            return out->op == EP0_ERROR ? -2 : 0;
        }
        if ((uint32_t)(plat_ms() - t0) >= timeout_ms) { sprintf(c->err, "no reply to transaction %u in %u ms", tid, timeout_ms); return -1; }
        { int rc = win_service(c, 20u); if (rc) return rc; }
    }
}

unsigned tnp_next_tid(tnp_stream_t *c) { unsigned t = c->tid; c->tid = (c->tid + 1u) & 0xFFFFu; if (!c->tid) c->tid = 1u; return t; }

int reg_write(tnp_stream_t *c, int cfg, uint32_t addr, uint32_t val)
{
    uint8_t p[32], m[16]; size_t n = tnp_ep_put(p, 0, sizeof p, 0u, m, ep0_build(m, cfg ? EP0_CFGWRITE : EP0_WRITE, addr, tnp_next_tid(c), &val, 1u));
    return tnp_send_ep0(c, p, n);
}

int reg_read(tnp_stream_t *c, int cfg, uint32_t addr, uint32_t *val)
{
    uint8_t p[32], m[16]; unsigned tid = tnp_next_tid(c), k; ep0_msg_t e; int rc;
    size_t n = tnp_ep_put(p, 0, sizeof p, 0u, m, ep0_build(m, cfg ? EP0_CFGREAD : EP0_READ, addr, tid, NULL, 1u));
    for (k = 0; k < 3u; k++) {                                   /* the transport is reliable; a lost REPLY would be the device's bug */
        if ((rc = tnp_send_ep0(c, p, n))) return rc;
        rc = tnp_wait_reply(c, tid, &e, 2000u);
        if (rc == 0) { *val = e.w[0]; return 0; }
        if (rc != -1) return rc;
    }
    return -1;
}


int tnp_force_reset(uint32_t ip)
{
    struct sockaddr_in a; uint8_t p[8]; tnp_hdr_t h; unsigned port, sent = 0;
    h.flags = TNP_F_RST; h.seq = 0; h.ack = 0; h.security = 0;
    tnp_hdr_put(p, &h);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip); a.sin_port = htons(TNP_PORT);
    for (port = 1024u; port < 65536u; port++) {
        sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); struct sockaddr_in l;
        if (s == SOCK_BAD) continue;
        memset(&l, 0, sizeof l); l.sin_family = AF_INET; l.sin_addr.s_addr = htonl(INADDR_ANY); l.sin_port = htons((uint16_t)port);
        if (bind(s, (struct sockaddr *)&l, sizeof l) == 0 && sendto(s, (const char *)p, 8, 0, (struct sockaddr *)&a, sizeof a) == 8) sent++;
        sock_close(s);
        if ((port & 4095u) == 0u) plat_sleep_ms(20u);
    }
    { tnp_stream_t c; int rc = tnp_connect(&c, ip, 0); if (rc == 0) { tnp_close(&c); return 0; } }
    return sent ? 1 : -1;
}
