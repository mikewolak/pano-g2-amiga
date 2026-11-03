/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/tnp.c -- see tnp.h. */
#include <stdio.h>
#include <string.h>
#include "tnp.h"

void hexdump(const char *tag, const uint8_t *p, size_t n)
{
    size_t i; fprintf(stderr, "%s [%lu]", tag, (unsigned long)n);
    for (i = 0; i < n && i < 64; i++) fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, n > 64 ? " ...\n" : "\n");
}

void tnp_hdr_put(uint8_t *p, const tnp_hdr_t *h)
{
    p[0] = 1; p[1] = (uint8_t)h->flags;
    p[2] = (uint8_t)((h->seq >> 4) & 0xFFu);
    p[3] = (uint8_t)(((h->seq & 0xFu) << 4) | ((h->ack >> 8) & 0xFu));
    p[4] = (uint8_t)(h->ack & 0xFFu);
    p[5] = (uint8_t)(h->security >> 16); p[6] = (uint8_t)(h->security >> 8); p[7] = (uint8_t)h->security;
}

int tnp_hdr_get(const uint8_t *p, size_t n, tnp_hdr_t *h)
{
    if (n < 8u || p[0] != 1u) return 0;
    h->flags = p[1];
    h->seq = ((unsigned)p[2] << 4) | (p[3] >> 4);
    h->ack = (((unsigned)p[3] & 0xFu) << 8) | p[4];
    h->security = ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
    return 1;
}

size_t tnp_ep_put(uint8_t *pkt, size_t len, size_t cap, unsigned ep, const uint8_t *msg, size_t mlen)
{
    unsigned h;
    if (mlen > 0x7FFu || len + 2u + mlen > cap) return 0;
    h = ((ep & 0x1Fu) << 11) | (unsigned)mlen;
    pkt[len] = (uint8_t)(h >> 8); pkt[len + 1] = (uint8_t)h;
    memcpy(pkt + len + 2, msg, mlen);
    return len + 2u + mlen;
}

int tnp_ep_next(const uint8_t *pkt, size_t n, size_t *off, unsigned *ep, const uint8_t **msg, size_t *mlen)
{
    unsigned h; size_t l;
    if (*off + 2u > n) return 0;
    h = ((unsigned)pkt[*off] << 8) | pkt[*off + 1]; l = h & 0x7FFu;
    if (*off + 2u + l > n) return 0;
    *ep = h >> 11; *msg = pkt + *off + 2; *mlen = l; *off += 2u + l;
    return 1;
}

size_t ep0_build(uint8_t *m, unsigned op, uint32_t addr, unsigned tid, const uint32_t *w, unsigned nw)
{
    unsigned i, cnt = nw ? nw : 1u;                       /* a read request says how many words it WANTS and carries none */
    m[0] = (uint8_t)((op << 4) | ((cnt - 1u) & 0xFu));
    m[1] = (uint8_t)(addr >> 16); m[2] = (uint8_t)(addr >> 8); m[3] = (uint8_t)addr;
    m[4] = (uint8_t)(tid >> 8); m[5] = (uint8_t)tid;
    if (op == EP0_CFGREAD || op == EP0_READ) return 6u;
    for (i = 0; i < nw; i++) { m[6 + 4*i] = (uint8_t)(w[i] >> 24); m[7 + 4*i] = (uint8_t)(w[i] >> 16); m[8 + 4*i] = (uint8_t)(w[i] >> 8); m[9 + 4*i] = (uint8_t)w[i]; }
    return 6u + 4u * nw;
}

int ep0_parse(const uint8_t *m, size_t n, ep0_msg_t *o)
{
    unsigned i;
    if (n < 6u) return 0;
    o->op = m[0] >> 4; o->nwords = (m[0] & 0xFu) + 1u;
    o->addr = ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
    o->tid = ((unsigned)m[4] << 8) | m[5];
    if (o->op == EP0_CFGREAD || o->op == EP0_READ) { o->nwords = 0; return 1; }
    if (n < 6u + 4u * o->nwords) { if (o->op == EP0_ERROR) { o->nwords = (unsigned)((n - 6u) / 4u); } else return 0; }
    for (i = 0; i < o->nwords; i++) o->w[i] = ((uint32_t)m[6+4*i] << 24) | ((uint32_t)m[7+4*i] << 16) | ((uint32_t)m[8+4*i] << 8) | m[9+4*i];
    return 1;
}

const char *ep0_opname(unsigned op)
{
    static const char *n[8] = { "CfgRead", "CfgWrite", "CfgReadReturn", "Read", "Write", "ReadReturn", "Interrupt", "Error" };
    return n[op & 7u];
}

int tnp_dgram_open(tnp_dgram_t *d, uint32_t bind_ip, unsigned bind_port, int broadcast)
{
    struct sockaddr_in a; int one = 1;
    memset(d, 0, sizeof *d); d->seq = 0x123u; d->tid = 1u;
    d->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (d->s == SOCK_BAD) return -1;
    if (broadcast) setsockopt(d->s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(bind_ip); a.sin_port = htons((uint16_t)bind_port);
    if (bind(d->s, (struct sockaddr *)&a, sizeof a) != 0) { sock_close(d->s); d->s = SOCK_BAD; return -2; }
    return 0;
}

void tnp_dgram_close(tnp_dgram_t *d) { if (d->s != SOCK_BAD) sock_close(d->s); d->s = SOCK_BAD; }

static int dgram_send(tnp_dgram_t *d, uint32_t ip, const uint8_t *msg, size_t mlen)
{
    uint8_t p[64]; tnp_hdr_t h; size_t n; struct sockaddr_in a;
    h.flags = TNP_F_DGRAM; h.seq = d->seq; h.ack = 0; h.security = 0; d->seq = (d->seq + 1u) & 0xFFFu;
    tnp_hdr_put(p, &h); n = tnp_ep_put(p, 8u, sizeof p, 0u, msg, mlen);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip); a.sin_port = htons(TNP_PORT);
    if (d->trace) hexdump("tx", p, n);
    return sendto(d->s, (const char *)p, (int)n, 0, (struct sockaddr *)&a, sizeof a) == (int)n ? 0 : -3;
}

int tnp_dgram_read(tnp_dgram_t *d, uint32_t ip, int cfg, uint32_t addr, uint32_t *val, unsigned timeout_ms, unsigned tries)
{
    unsigned t;
    for (t = 0; t < tries; t++) {
        uint8_t m[16]; unsigned tid = d->tid; uint32_t t0;
        d->tid = (d->tid + 1u) & 0xFFFFu; if (!d->tid) d->tid = 1u;
        if (dgram_send(d, ip, m, ep0_build(m, cfg ? EP0_CFGREAD : EP0_READ, addr, tid, NULL, 1u))) return -3;
        t0 = plat_ms();
        while ((uint32_t)(plat_ms() - t0) < timeout_ms) {
            uint8_t r[1600]; struct sockaddr_in from; socklen_plat fl = sizeof from; int n; tnp_hdr_t h; size_t off = 8; unsigned ep; const uint8_t *msg; size_t ml;
            if (plat_wait_readable(d->s, timeout_ms - (uint32_t)(plat_ms() - t0)) <= 0) break;
            n = recvfrom(d->s, (char *)r, sizeof r, 0, (struct sockaddr *)&from, &fl);
            if (n <= 0) continue;
            if (d->trace) hexdump("rx", r, (size_t)n);
            if (ntohl(from.sin_addr.s_addr) != ip || !tnp_hdr_get(r, (size_t)n, &h)) continue;
            while (tnp_ep_next(r, (size_t)n, &off, &ep, &msg, &ml)) {
                ep0_msg_t e;
                if (ep != 0u || !ep0_parse(msg, ml, &e) || e.tid != tid) continue;
                if (e.op == EP0_ERROR) return -2;
                if ((e.op == EP0_CFGREADRET || e.op == EP0_READRET) && e.nwords >= 1u) { *val = e.w[0]; return 0; }
            }
        }
    }
    return -1;
}

int tnp_dgram_write(tnp_dgram_t *d, uint32_t ip, int cfg, uint32_t addr, uint32_t val)
{
    uint8_t m[16]; unsigned tid = d->tid;
    d->tid = (d->tid + 1u) & 0xFFFFu; if (!d->tid) d->tid = 1u;
    return dgram_send(d, ip, m, ep0_build(m, cfg ? EP0_CFGWRITE : EP0_WRITE, addr, tid, &val, 1u));
}

int pano_info_read(tnp_dgram_t *d, uint32_t ip, pano_info_t *o)
{
    uint32_t m0, m1; int rc;
    memset(o, 0, sizeof *o);
    if ((rc = tnp_dgram_read(d, ip, 1, 0x00u, &o->cfg0, 1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x01u, &o->rev,  1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x10u, &m0,      1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x11u, &m1,      1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x12u, &o->ip,   1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x13u, &o->mask, 1000u, 3u))) return rc;
    if ((rc = tnp_dgram_read(d, ip, 1, 0x16u, &o->gw,   1000u, 3u))) return rc;
    o->mac[0] = (uint8_t)(m0 >> 8); o->mac[1] = (uint8_t)m0;
    o->mac[2] = (uint8_t)(m1 >> 24); o->mac[3] = (uint8_t)(m1 >> 16); o->mac[4] = (uint8_t)(m1 >> 8); o->mac[5] = (uint8_t)m1;
    return 0;
}


int pano_discover(const uint32_t *dst, unsigned ndst, uint32_t bind_ip, unsigned secs, pano_found_fn cb, void *ctx)
{
    tnp_dgram_t d; uint32_t seen[64]; unsigned nseen = 0, i; uint32_t t0, last = 0; int first = 1;
    if (tnp_dgram_open(&d, bind_ip, TNP_DISC_SPORT, 1) && tnp_dgram_open(&d, bind_ip, 0, 1)) return -1;
    t0 = plat_ms();
    while ((uint32_t)(plat_ms() - t0) < secs * 1000u) {
        uint8_t r[1600]; struct sockaddr_in from; socklen_plat fl = sizeof from; int n;
        if (first || (uint32_t)(plat_ms() - last) >= 1000u) {
            uint8_t p[32], m[16]; tnp_hdr_t h; size_t len; struct sockaddr_in a;
            first = 0; last = plat_ms();
            h.flags = TNP_F_DGRAM; h.seq = d.seq; h.ack = 0; h.security = 0; d.seq = (d.seq + 1u) & 0xFFFu;
            tnp_hdr_put(p, &h);
            len = tnp_ep_put(p, 8u, sizeof p, 0u, m, ep0_build(m, EP0_CFGREAD, 0u, 1u, NULL, 1u));
            for (i = 0; i < ndst; i++) {
                memset(&a, 0, sizeof a); a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(dst[i]); a.sin_port = htons(TNP_PORT);
                sendto(d.s, (const char *)p, (int)len, 0, (struct sockaddr *)&a, sizeof a);
            }
        }
        if (plat_wait_readable(d.s, 200u) <= 0) continue;
        n = recvfrom(d.s, (char *)r, sizeof r, 0, (struct sockaddr *)&from, &fl);
        if (n < 8) continue;
        { tnp_hdr_t h; uint32_t ip = ntohl(from.sin_addr.s_addr); int dup = 0;
          if (!tnp_hdr_get(r, (size_t)n, &h)) continue;
          for (i = 0; i < nseen; i++) if (seen[i] == ip) dup = 1;
          if (dup || nseen >= 64u) continue;
          seen[nseen++] = ip; }
    }
    tnp_dgram_close(&d);
    for (i = 0; i < nseen; i++) {                      /* now ask each responder who it is, unicast */
        tnp_dgram_t u; pano_info_t inf;
        if (tnp_dgram_open(&u, bind_ip, 0, 0)) continue;
        if (!pano_info_read(&u, seen[i], &inf)) { if (!inf.ip) inf.ip = seen[i]; cb(&inf, ctx); }
        tnp_dgram_close(&u);
    }
    return (int)nseen;
}
