/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/sock.c -- the socket window of HD0 (sock.h): the Amiga's TCP and UDP sockets on lwIP's
 * raw API, run from the main loop (NO_SYS).  amiga/panotcp's bsdsocket.library sends the calls of its programs here.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 * RINGS.  Each socket has a receive ring (SK_RXN bytes) and a transmit ring (SK_TXN bytes) in the firmware's memory.
 * A stream's received segments are copied into its ring as lwIP delivers them; the window lwIP advertises is given back
 * (tcp_recved) as long as the ring keeps room for a whole TCP_WND beyond the bytes it holds, so the peer can never send
 * more than the ring takes and a segment is never refused (a refused segment would hold the MAC's lent frame,
 * fw/netif_pano.c).  The Amiga's SEND fills the transmit ring; the ring is written into lwIP as its send buffer frees
 * (on each acknowledgement and from sk_poll), so one request moves up to the whole transmit window.  A datagram socket's
 * receive ring holds whole datagrams, each after an 8-byte header (length, port, address).
 *
 * SK_STAT, built at each read:
 *   0 'PNS1'  4 sequence number of the last request  8 address  12 netmask  16 gateway  20 name server 1
 *   24 name server 2  28 the firmware's session identifier (changes at every start)  32 sockets (SK_N)
 *   36 frames of the window served  40 the sockets, 12 bytes each:
 *      0 state (SK_S_)  1 errno of the socket's failure  2 connections waiting to be accepted (listening) or datagrams
 *      waiting (datagram)  4 bytes waiting to be received  8 bytes the transmit ring takes
 * Addresses travel as 32-bit values a.b.c.d = 0xaabbccdd; ports as 16-bit values. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "../timer_ms.h"
#include "sock.h"

#define SK_RXN  65536u                 /* receive ring, a power of two */
#define SK_TXN  32768u                 /* transmit ring, a power of two */
#define SK_ACCQ 8u                     /* connections held for accept() */

/* BSD errno values, as AmiTCP and Roadshow report them */
#define E_BADF          9
#define E_NOMEM        12
#define E_INVAL        22
#define E_MFILE        24
#define E_PIPE         32
#define E_WOULDBLOCK   35
#define E_INPROGRESS   36
#define E_ALREADY      37
#define E_NOTSOCK      38
#define E_DESTADDRREQ  39
#define E_MSGSIZE      40
#define E_PROTONOSUPPORT 43
#define E_OPNOTSUPP    45
#define E_ADDRINUSE    48
#define E_ADDRNOTAVAIL 49
#define E_NETUNREACH   51
#define E_CONNABORTED  53
#define E_CONNRESET    54
#define E_NOBUFS       55
#define E_ISCONN       56
#define E_NOTCONN      57
#define E_TIMEDOUT     60
#define E_CONNREFUSED  61
#define E_HOSTUNREACH  65

#define T_STREAM 1u
#define T_DGRAM  2u

struct sk {
    uint8_t  state, err, type, eof;
    uint8_t  reuse, closing, waiting;   /* SO_REUSEADDR before bind; closed by the Amiga; held for accept() */
    uint8_t  nacc, accq[SK_ACCQ];       /* a listener's connections, oldest first */
    uint16_t dgrams;
    struct tcp_pcb *tp;
    struct udp_pcb *up;
    uint32_t rh, rt, th, tt, owed;      /* ring heads and tails; received bytes whose window is still held */
};

static struct sk s_sk[SK_N];
static uint8_t   s_rxr[SK_N][SK_RXN], s_txr[SK_N][SK_TXN];
static uint8_t   s_txw[SK_WIN], s_rxw[SK_WIN];
static uint8_t   s_req[512], s_res[512];
static uint32_t  s_seq, s_rxoff, s_sess, s_frames, s_nreq, s_rxbytes, s_txbytes;
static int       s_ok;

/* host names resolved or failed since they were asked (lwIP's own cache keeps the answers; this keeps the failures) */
#define SK_DNSN 8u
static struct { char name[128]; uint32_t ip, ms; uint8_t state; } s_dns[SK_DNSN];   /* state 1 asked, 2 found, 3 failed */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void be32_put(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t ip_get(const ip_addr_t *a) { return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(a))); }
static void ip_set(ip_addr_t *a, uint32_t v) { ip_addr_set_ip4_u32(a, lwip_htonl(v)); }

static uint32_t rx_used(const struct sk *s) { return (s->rh - s->rt) & (SK_RXN - 1u); }
static uint32_t rx_free(const struct sk *s) { return SK_RXN - 1u - rx_used(s); }
static uint32_t tx_used(const struct sk *s) { return (s->th - s->tt) & (SK_TXN - 1u); }
static uint32_t tx_free(const struct sk *s) { return SK_TXN - 1u - tx_used(s); }

static void rx_put(struct sk *s, const uint8_t *d, uint32_t n)
{
    uint8_t *r = s_rxr[s - s_sk];
    while (n) {
        uint32_t k = SK_RXN - s->rh; if (k > n) k = n;
        memcpy(r + s->rh, d, k); s->rh = (s->rh + k) & (SK_RXN - 1u); d += k; n -= k;
    }
}
/* n bytes from offset `at' past the tail, without consuming them */
static void rx_peek(const struct sk *s, uint32_t at, uint8_t *d, uint32_t n)
{
    const uint8_t *r = s_rxr[s - s_sk];
    uint32_t t = (s->rt + at) & (SK_RXN - 1u);
    while (n) {
        uint32_t k = SK_RXN - t; if (k > n) k = n;
        memcpy(d, r + t, k); t = (t + k) & (SK_RXN - 1u); d += k; n -= k;
    }
}

static int map_err(err_t e)
{
    switch (e) {
    case ERR_OK: return 0;
    case ERR_MEM: case ERR_BUF: return E_NOBUFS;
    case ERR_TIMEOUT: return E_TIMEDOUT;
    case ERR_RTE: return E_HOSTUNREACH;
    case ERR_INPROGRESS: return E_INPROGRESS;
    case ERR_WOULDBLOCK: return E_WOULDBLOCK;
    case ERR_USE: return E_ADDRINUSE;
    case ERR_ALREADY: return E_ALREADY;
    case ERR_ISCONN: return E_ISCONN;
    case ERR_CONN: case ERR_CLSD: return E_NOTCONN;
    case ERR_IF: return E_NETUNREACH;
    case ERR_ABRT: return E_CONNABORTED;
    case ERR_RST: return E_CONNRESET;
    default: return E_INVAL;
    }
}

/* the window lwIP may advertise: TCP_WND - owed bytes, never more than the ring's free space */
static void rx_window(struct sk *s)
{
    uint32_t f = rx_free(s), keep = f >= TCP_WND ? 0u : TCP_WND - f, give;
    if (!s->tp || s->owed <= keep) return;
    give = s->owed - keep;
    while (give) {
        uint32_t k = give > 0xFFFFu ? 0xFFFFu : give;
        tcp_recved(s->tp, (u16_t)k); s->owed -= k; give -= k;
    }
}

/* the transmit ring into lwIP's send buffer */
static void tx_feed(struct sk *s)
{
    struct tcp_pcb *p = s->tp;
    int wrote = 0;
    if (!p || (s->state != SK_S_CONNECTED && s->state != SK_S_EOF && s->state != SK_S_CLOSING)) return;
    while (tx_used(s)) {
        uint32_t n = s->th >= s->tt ? s->th - s->tt : SK_TXN - s->tt, room = tcp_sndbuf(p);
        if (!room || tcp_sndqueuelen(p) >= TCP_SND_QUEUELEN - 1u) break;
        if (n > room) n = room;
        if (n > 0xFFFFu) n = 0xFFFFu;
        if (tcp_write(p, s_txr[s - s_sk] + s->tt, (u16_t)n, TCP_WRITE_FLAG_COPY | (tx_used(s) > n ? TCP_WRITE_FLAG_MORE : 0)) != ERR_OK) break;
        s->tt = (s->tt + n) & (SK_TXN - 1u); wrote = 1;
    }
    if (wrote) tcp_output(p);
}

static void sk_free(struct sk *s)
{
    memset(s, 0, sizeof *s);
}

static void tcp_detach(struct tcp_pcb *p)
{
    tcp_arg(p, 0); tcp_recv(p, 0); tcp_sent(p, 0); tcp_err(p, 0);
    if (p->state == LISTEN) tcp_accept(p, 0);
}

/* the socket's lwIP side ended now (abort) and the slot freed */
static void sk_drop(struct sk *s)
{
    unsigned i;
    if (s->tp) { tcp_detach(s->tp); tcp_abort(s->tp); }
    if (s->up) udp_remove(s->up);
    for (i = 0; i < s->nacc; i++) sk_drop(&s_sk[s->accq[i]]);
    sk_free(s);
}

static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err)
{
    struct sk *s = &s_sk[(uintptr_t)arg - 1u];
    struct pbuf *q;
    (void)err;
    if (!b) {
        s->eof = 1u;
        if (s->state == SK_S_CONNECTED) s->state = SK_S_EOF;
        return ERR_OK;
    }
    if (b->tot_len > rx_free(s)) return ERR_MEM;     /* not reached while the window rule holds */
    for (q = b; q; q = q->next) rx_put(s, (const uint8_t *)q->payload, q->len);
    s->owed += b->tot_len; s_rxbytes += b->tot_len;
    pbuf_free(b);
    (void)p;
    rx_window(s);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *p, u16_t n)
{
    struct sk *s = &s_sk[(uintptr_t)arg - 1u];
    (void)p; s_txbytes += n;
    tx_feed(s);
    return ERR_OK;
}

static void on_err(void *arg, err_t e)
{
    struct sk *s = &s_sk[(uintptr_t)arg - 1u];
    s->tp = 0;                                       /* lwIP has freed the pcb */
    if (s->closing) { sk_free(s); return; }
    s->err = (uint8_t)(s->state == SK_S_CONNECTING && (e == ERR_RST || e == ERR_ABRT) ? E_CONNREFUSED : map_err(e));
    s->state = SK_S_ERROR;
}

static err_t on_connected(void *arg, struct tcp_pcb *p, err_t e)
{
    struct sk *s = &s_sk[(uintptr_t)arg - 1u];
    (void)p; (void)e;
    s->state = SK_S_CONNECTED;
    tx_feed(s);
    return ERR_OK;
}

static int sk_alloc(void)
{
    unsigned i;
    for (i = 0; i < SK_N; i++) if (s_sk[i].state == SK_S_FREE) { sk_free(&s_sk[i]); return (int)i; }
    return -1;
}

static void tcp_attach(struct sk *s, struct tcp_pcb *p)
{
    s->tp = p;
    tcp_arg(p, (void *)(uintptr_t)(s - s_sk + 1));
    tcp_recv(p, on_recv); tcp_sent(p, on_sent); tcp_err(p, on_err);
}

static err_t on_accept(void *arg, struct tcp_pcb *p, err_t e)
{
    struct sk *l = &s_sk[(uintptr_t)arg - 1u], *s;
    int i;
    if (e != ERR_OK || !p) return ERR_VAL;
    if (l->nacc >= SK_ACCQ || (i = sk_alloc()) < 0) return ERR_MEM;   /* lwIP aborts the connection */
    s = &s_sk[i];
    s->state = SK_S_CONNECTED; s->type = T_STREAM; s->waiting = 1u;
    tcp_attach(s, p);
    l->accq[l->nacc++] = (uint8_t)i;
    return ERR_OK;
}

static void on_udp(void *arg, struct udp_pcb *u, struct pbuf *b, const ip_addr_t *a, u16_t port)
{
    struct sk *s = &s_sk[(uintptr_t)arg - 1u];
    uint8_t h[8];
    struct pbuf *q;
    (void)u;
    if (b->tot_len + 8u <= rx_free(s)) {
        h[0] = (uint8_t)(b->tot_len >> 8); h[1] = (uint8_t)b->tot_len; h[2] = (uint8_t)(port >> 8); h[3] = (uint8_t)port;
        be32_put(h + 4, ip_get(a));
        rx_put(s, h, 8u);
        for (q = b; q; q = q->next) rx_put(s, (const uint8_t *)q->payload, q->len);
        s->dgrams++; s_rxbytes += b->tot_len;
    }
    pbuf_free(b);
}

static void on_dns(const char *name, const ip_addr_t *a, void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < SK_DNSN; i++)
        if (s_dns[i].state == 1u && !strcmp(s_dns[i].name, name)) {
            s_dns[i].state = a ? 2u : 3u; s_dns[i].ip = a ? ip_get(a) : 0u; s_dns[i].ms = get_millis();
        }
}

/* RESOLVE: 0 and *ip, or an errno */
static int resolve(const char *name, uint32_t *ip)
{
    ip_addr_t a;
    unsigned i, old = 0;
    err_t e;
    if (ipaddr_aton(name, &a)) { *ip = ip_get(&a); return 0; }
    for (i = 0; i < SK_DNSN; i++)
        if (s_dns[i].state && !strcmp(s_dns[i].name, name)) {
            if (s_dns[i].state == 2u) { *ip = s_dns[i].ip; s_dns[i].state = 0; return 0; }
            if (s_dns[i].state == 3u) { s_dns[i].state = 0; return E_HOSTUNREACH; }
            break;                                   /* asked: lwIP answers EINPROGRESS again */
        }
    e = dns_gethostbyname(name, &a, on_dns, 0);
    if (e == ERR_OK) { *ip = ip_get(&a); return 0; }
    if (e != ERR_INPROGRESS) return E_HOSTUNREACH;
    if (i == SK_DNSN) {
        for (i = 0; i < SK_DNSN && s_dns[i].state; i++) if (get_millis() - s_dns[i].ms > get_millis() - s_dns[old].ms) old = i;
        if (i == SK_DNSN) i = old;
        strncpy(s_dns[i].name, name, sizeof s_dns[i].name - 1u); s_dns[i].name[sizeof s_dns[i].name - 1u] = 0;
        s_dns[i].state = 1u; s_dns[i].ms = get_millis();
    }
    return E_INPROGRESS;
}

/* one operation; the result's value, errno, x, y and receive-window offset */
static void op_run(const uint8_t *o, uint8_t *r)
{
    unsigned code = o[0], si = (unsigned)o[2] << 8 | o[3];
    uint32_t a = be32(o + 4), b = be32(o + 8), c = be32(o + 12), d = be32(o + 16);
    int32_t v = -1; int err = 0; uint32_t x = 0, y = 0, off = s_rxoff;
    struct sk *s = si < SK_N ? &s_sk[si] : 0;
    ip_addr_t ip;
    err_t e;
    if (code != SK_OP_RESET && code != SK_OP_SOCKET && code != SK_OP_RESOLVE && (!s || s->state == SK_S_FREE || s->closing))
        { err = E_BADF; goto done; }
    switch (code) {
    case SK_OP_RESET: {
        unsigned i;
        for (i = 0; i < SK_N; i++) if (s_sk[i].state != SK_S_FREE && !s_sk[i].closing) sk_drop(&s_sk[i]);
        v = (int32_t)s_sess;
        break;
    }
    case SK_OP_SOCKET: {
        int i = sk_alloc();
        if (i < 0) { err = E_MFILE; break; }
        s = &s_sk[i];
        if (a == T_STREAM) { struct tcp_pcb *p = tcp_new(); if (!p) { err = E_NOBUFS; break; } tcp_attach(s, p); }
        else if (a == T_DGRAM) {
            s->up = udp_new(); if (!s->up) { err = E_NOBUFS; break; }
            udp_recv(s->up, on_udp, (void *)(uintptr_t)(i + 1));
        } else { err = E_PROTONOSUPPORT; break; }
        s->type = (uint8_t)a; s->state = SK_S_OPEN;
        v = i;
        break;
    }
    case SK_OP_BIND:
        ip_set(&ip, a);
        if (s->type == T_STREAM) {
            if (!s->tp || s->state != SK_S_OPEN) { err = E_INVAL; break; }
            if (s->reuse) ip_set_option(s->tp, SOF_REUSEADDR);
            e = tcp_bind(s->tp, &ip, (u16_t)b);
        } else e = udp_bind(s->up, &ip, (u16_t)b);
        if (e != ERR_OK) { err = e == ERR_USE ? E_ADDRINUSE : E_ADDRNOTAVAIL; break; }
        v = 0;
        break;
    case SK_OP_LISTEN: {
        struct tcp_pcb *l;
        if (s->type != T_STREAM) { err = E_OPNOTSUPP; break; }
        if (s->state == SK_S_LISTEN) { v = 0; break; }
        if (!s->tp || s->state != SK_S_OPEN) { err = E_INVAL; break; }
        if (s->tp->local_port == 0 && tcp_bind(s->tp, IP_ANY_TYPE, 0) != ERR_OK) { err = E_ADDRINUSE; break; }
        l = tcp_listen_with_backlog(s->tp, (u8_t)(a < 1u ? 1u : a > SK_ACCQ ? SK_ACCQ : a));
        if (!l) { err = E_NOBUFS; break; }
        s->tp = l;
        tcp_arg(l, (void *)(uintptr_t)(si + 1)); tcp_accept(l, on_accept);
        s->state = SK_S_LISTEN; v = 0;
        break;
    }
    case SK_OP_ACCEPT: {
        struct sk *n;
        if (s->state != SK_S_LISTEN) { err = E_INVAL; break; }
        if (!s->nacc) { err = E_WOULDBLOCK; break; }
        v = s->accq[0];
        memmove(s->accq, s->accq + 1, --s->nacc);
        n = &s_sk[v]; n->waiting = 0u;
        if (n->tp) { x = ip_get(&n->tp->remote_ip); y = n->tp->remote_port; tcp_backlog_accepted(n->tp); }
        break;
    }
    case SK_OP_CONNECT:
        ip_set(&ip, a);
        if (s->type == T_DGRAM) { if (udp_connect(s->up, &ip, (u16_t)b) != ERR_OK) { err = E_INVAL; break; } v = 0; break; }
        if (s->state == SK_S_CONNECTING) { err = E_ALREADY; break; }
        if (s->state == SK_S_CONNECTED || s->state == SK_S_EOF) { err = E_ISCONN; break; }
        if (s->state == SK_S_ERROR) { err = s->err; break; }
        if (!s->tp || s->state != SK_S_OPEN) { err = E_INVAL; break; }
        e = tcp_connect(s->tp, &ip, (u16_t)b, on_connected);
        if (e != ERR_OK) { err = map_err(e); break; }
        s->state = SK_S_CONNECTING; err = E_INPROGRESS;
        break;
    case SK_OP_SEND:
        if (a > SK_WIN || b > SK_WIN - a) { err = E_INVAL; break; }
        if (s->type == T_DGRAM) {
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)b, PBUF_RAM);
            if (b > 65507u) { err = E_MSGSIZE; break; }
            if (!p) { err = E_NOBUFS; break; }
            pbuf_take(p, s_txw + a, (u16_t)b);
            e = udp_send(s->up, p); pbuf_free(p);
            if (e != ERR_OK) { err = e == ERR_VAL ? E_DESTADDRREQ : map_err(e); break; }
            v = (int32_t)b; s_txbytes += b;
            break;
        }
        if (s->state == SK_S_ERROR) { err = s->err ? s->err : E_PIPE; break; }
        if (s->state != SK_S_CONNECTED && s->state != SK_S_EOF) { err = s->state == SK_S_CONNECTING ? E_WOULDBLOCK : E_NOTCONN; break; }
        if (!b) { v = 0; break; }
        {
            uint32_t n = tx_free(s), k, done = 0;
            if (n > b) n = b;
            if (!n) { err = E_WOULDBLOCK; break; }
            while (done < n) {
                k = SK_TXN - s->th; if (k > n - done) k = n - done;
                memcpy(s_txr[si] + s->th, s_txw + a + done, k); s->th = (s->th + k) & (SK_TXN - 1u); done += k;
            }
            tx_feed(s);
            v = (int32_t)n;
        }
        break;
    case SK_OP_SENDTO: {
        struct pbuf *p;
        if (s->type != T_DGRAM) { err = E_ISCONN; break; }
        if (a > SK_WIN || b > SK_WIN - a) { err = E_INVAL; break; }
        if (b > 65507u) { err = E_MSGSIZE; break; }
        if (!(p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)b, PBUF_RAM))) { err = E_NOBUFS; break; }
        pbuf_take(p, s_txw + a, (u16_t)b);
        ip_set(&ip, c);
        e = udp_sendto(s->up, p, &ip, (u16_t)d); pbuf_free(p);
        if (e != ERR_OK) { err = map_err(e); break; }
        v = (int32_t)b; s_txbytes += b;
        break;
    }
    case SK_OP_RECV: case SK_OP_RECVFROM:
        if (c != 0xFFFFFFFFu) { if (c > SK_WIN) { err = E_INVAL; break; } s_rxoff = off = c; }
        if (s->type == T_DGRAM) {
            uint8_t h[8]; uint32_t len, n;
            if (!s->dgrams) { err = E_WOULDBLOCK; break; }
            rx_peek(s, 0u, h, 8u);
            len = (uint32_t)h[0] << 8 | h[1]; y = (uint32_t)h[2] << 8 | h[3]; x = be32(h + 4);
            n = len < a ? len : a;
            if (n > SK_WIN - s_rxoff) { err = E_NOBUFS; break; }
            rx_peek(s, 8u, s_rxw + s_rxoff, n);
            if (!(b & 2u)) { s->rt = (s->rt + 8u + len) & (SK_RXN - 1u); s->dgrams--; }
            v = (int32_t)n; s_rxoff += n;
            break;
        }
        {
            uint32_t n = rx_used(s);
            if (n > a) n = a;
            if (n > SK_WIN - s_rxoff) n = SK_WIN - s_rxoff;
            if (!n) {
                if (!a) v = 0;
                else if (s->eof) v = 0;
                else if (s->state == SK_S_ERROR) err = s->err ? s->err : E_CONNRESET;
                else if (s->state == SK_S_LISTEN || s->state == SK_S_OPEN) err = E_NOTCONN;
                else err = E_WOULDBLOCK;
                break;
            }
            rx_peek(s, 0u, s_rxw + s_rxoff, n);
            if (!(b & 2u)) { s->rt = (s->rt + n) & (SK_RXN - 1u); rx_window(s); }
            if (code == SK_OP_RECVFROM && s->tp) { x = ip_get(&s->tp->remote_ip); y = s->tp->remote_port; }
            v = (int32_t)n; s_rxoff += n;
        }
        break;
    case SK_OP_SHUTDOWN:
        if (s->type == T_DGRAM) { v = 0; break; }
        if (!s->tp || (s->state != SK_S_CONNECTED && s->state != SK_S_EOF)) { err = E_NOTCONN; break; }
        if (a == 0u || a == 2u) s->eof = 1u;
        if (a >= 1u) { tx_feed(s); if (!tx_used(s)) tcp_shutdown(s->tp, 0, 1); }
        v = 0;
        break;
    case SK_OP_CLOSE:
        if (s->type == T_STREAM && s->tp && (s->state == SK_S_CONNECTED || s->state == SK_S_EOF) && tx_used(s)) {
            s->closing = 1u; s->state = SK_S_CLOSING;     /* sk_poll closes it once the ring is in lwIP */
            v = 0;
            break;
        }
        if (s->tp) {
            unsigned i;
            for (i = 0; i < s->nacc; i++) sk_drop(&s_sk[s->accq[i]]);
            s->nacc = 0;
            tcp_detach(s->tp);
            if (tcp_close(s->tp) != ERR_OK) tcp_abort(s->tp);
            s->tp = 0;
        }
        if (s->up) { udp_remove(s->up); s->up = 0; }
        sk_free(s); v = 0;
        break;
    case SK_OP_SETOPT:
        if (a == 0xFFFFu && b == 0x0004u) { s->reuse = (uint8_t)(c != 0u); if (s->tp) { if (c) ip_set_option(s->tp, SOF_REUSEADDR); else ip_reset_option(s->tp, SOF_REUSEADDR); } }
        else if (a == 0xFFFFu && b == 0x0008u && s->tp) { if (c) ip_set_option(s->tp, SOF_KEEPALIVE); else ip_reset_option(s->tp, SOF_KEEPALIVE); }
        else if (a == 0xFFFFu && b == 0x0020u && s->up) { if (c) ip_set_option(s->up, SOF_BROADCAST); else ip_reset_option(s->up, SOF_BROADCAST); }
        else if (a == 6u && b == 1u && s->tp) { if (c) tcp_nagle_disable(s->tp); else tcp_nagle_enable(s->tp); }
        v = 0;
        break;
    case SK_OP_NAME: case SK_OP_PEER:
        if (s->tp) {
            if (code == SK_OP_NAME) { x = ip_get(&s->tp->local_ip); y = s->tp->local_port; }
            else if (s->state == SK_S_CONNECTED || s->state == SK_S_EOF || s->state == SK_S_CLOSING) { x = ip_get(&s->tp->remote_ip); y = s->tp->remote_port; }
            else { err = E_NOTCONN; break; }
        } else if (s->up) {
            if (code == SK_OP_NAME) { x = ip_get(&s->up->local_ip); y = s->up->local_port; }
            else if (s->up->flags & UDP_FLAGS_CONNECTED) { x = ip_get(&s->up->remote_ip); y = s->up->remote_port; }
            else { err = E_NOTCONN; break; }
        } else if (s->state == SK_S_ERROR) { err = s->err; break; }
        if (code == SK_OP_NAME && !x && netif_default) x = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(netif_default)));
        v = 0;
        break;
    case SK_OP_RESOLVE: {
        char name[128];
        if (a > SK_WIN || b > SK_WIN - a || b >= sizeof name) { err = E_INVAL; break; }
        memcpy(name, s_txw + a, b); name[b] = 0;
        if ((err = resolve(name, &x)) == 0) v = 0;
        break;
    }
    default:
        err = E_INVAL;
    }
done:
    if (err) v = -1;
    be32_put(r, (uint32_t)v); be32_put(r + 4, (uint32_t)err); be32_put(r + 8, x); be32_put(r + 12, y); be32_put(r + 16, off);
}

static void req_run(void)
{
    uint32_t n = be32(s_req + 8), i;
    if (n > SK_OPS) n = SK_OPS;
    memset(s_res, 0, sizeof s_res);
    s_rxoff = 0;
    for (i = 0; i < n; i++) op_run(s_req + 16u + i * SK_OPLEN, s_res + 16u + i * SK_RESLEN);
    s_seq = be32(s_req + 4);
    memcpy(s_res, "PNR1", 4); be32_put(s_res + 4, s_seq); be32_put(s_res + 8, n); be32_put(s_res + 12, s_rxoff);
    s_nreq++;
}

void sk_init(void)
{
    if (s_sess) return;                          /* once: a reload keeps the sockets */
    memset(s_sk, 0, sizeof s_sk);
    s_sess = (get_millis() * 2654435761u ^ 0x504E0000u) | 1u;
}

void sk_setup(int ok) { s_ok = ok; }

int sk_block(uint32_t sec) { return s_ok && sec >= SK_REQ && sec < SK_END; }

void sk_write(uint32_t sec, const uint8_t *b)
{
    s_frames++;
    if (sec >= SK_TX && sec < SK_TX + SK_WIN / 512u) {
        memcpy(s_txw + (sec - SK_TX) * 512u, b, 512u);
        if (!memcmp(s_req, "PNQ1", 4) && be32(s_req + 4) != s_seq && be32(s_req + 12) && sec == SK_REQ + be32(s_req + 12)) req_run();
        return;
    }
    if (sec != SK_REQ) return;
    memcpy(s_req, b, 512u);
    if (!memcmp(s_req, "PNQ1", 4) && be32(s_req + 4) != s_seq && !be32(s_req + 12)) req_run();
}

void sk_read(uint32_t sec, uint8_t *b)
{
    unsigned i;
    s_frames++;
    if (sec >= SK_RX) { memcpy(b, s_rxw + (sec - SK_RX) * 512u, 512u); return; }
    memset(b, 0, 512u);
    if (sec == SK_RES) { memcpy(b, s_res, 512u); if (memcmp(b, "PNR1", 4)) memcpy(b, "PNR1", 4); return; }
    if (sec != SK_STAT) return;
    memcpy(b, "PNS1", 4); be32_put(b + 4, s_seq);
    if (netif_default) {
        be32_put(b + 8, lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(netif_default))));
        be32_put(b + 12, lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(netif_default))));
        be32_put(b + 16, lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(netif_default))));
    }
    be32_put(b + 20, ip_get(dns_getserver(0))); be32_put(b + 24, ip_get(dns_getserver(1)));
    be32_put(b + 28, s_sess); be32_put(b + 32, SK_N); be32_put(b + 36, s_frames);
    for (i = 0; i < SK_N; i++) {
        const struct sk *s = &s_sk[i];
        uint8_t *e = b + 40u + i * 12u;
        uint32_t pend = s->state == SK_S_LISTEN ? s->nacc : s->dgrams;
        e[0] = s->waiting ? SK_S_FREE : s->state; e[1] = s->err; e[2] = (uint8_t)(pend >> 8); e[3] = (uint8_t)pend;
        be32_put(e + 4, s->type == T_DGRAM ? (s->dgrams ? rx_used(s) : 0u) : rx_used(s));
        be32_put(e + 8, s->type == T_DGRAM ? SK_WIN : tx_free(s));
    }
}

void sk_poll(void)
{
    static uint32_t ms;
    unsigned i;
    if (get_millis() == ms) return;               /* once a millisecond: acknowledgements and requests feed the rings too */
    ms = get_millis();
    for (i = 0; i < SK_N; i++) {
        struct sk *s = &s_sk[i];
        if (s->state == SK_S_FREE || !s->tp || s->type != T_STREAM) continue;
        if (tx_used(s)) tx_feed(s);
        rx_window(s);
        if (s->closing && !tx_used(s)) {
            tcp_detach(s->tp);
            if (tcp_close(s->tp) == ERR_OK) { s->tp = 0; sk_free(s); }
            else tcp_arg(s->tp, (void *)(uintptr_t)(i + 1)), tcp_err(s->tp, on_err);
        }
    }
}

void sk_counts(uint32_t *req, uint32_t *open, uint32_t *rx, uint32_t *tx)
{
    unsigned i, n = 0;
    for (i = 0; i < SK_N; i++) if (s_sk[i].state != SK_S_FREE) n++;
    *req = s_nreq; *open = n; *rx = s_rxbytes; *tx = s_txbytes;
}

/* one line for socket i into b (n bytes), 0 when the socket is free */
int sk_line(unsigned i, char *b, unsigned n)
{
    const struct sk *s = &s_sk[i];
    if (i >= SK_N || s->state == SK_S_FREE) return 0;
    if (s->tp)
        snprintf(b, n, "%2u: %s state %u err %u rx %lu tx %lu owed %lu  tcp %d snd_nxt %lu lastack %lu sndbuf %u queue %u rcv_wnd %u unsent %d unacked %d\n",
                 i, s->type == T_STREAM ? "tcp" : "udp", s->state, s->err, (unsigned long)rx_used(s), (unsigned long)tx_used(s),
                 (unsigned long)s->owed, s->tp->state, (unsigned long)s->tp->snd_nxt, (unsigned long)s->tp->lastack,
                 (unsigned)tcp_sndbuf(s->tp), (unsigned)s->tp->snd_queuelen, (unsigned)s->tp->rcv_wnd, s->tp->unsent != 0, s->tp->unacked != 0);
    else snprintf(b, n, "%2u: %s state %u err %u rx %lu tx %lu dgrams %u\n", i, s->type == T_STREAM ? "tcp" : "udp", s->state,
                  s->err, (unsigned long)rx_used(s), (unsigned long)tx_used(s), s->dgrams);
    return 1;
}
