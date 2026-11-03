/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netsh.c -- the network half of NETSH: lwIP (raw API, NO_SYS), one TCP console on port 2323, a UDP beacon.
 *
 * WHY A RING ON EACH SIDE.  The shell is a polled loop and a command can run for seconds (a LOAD, a flash erase).  Input
 * lands in a 16 KB ring and is acknowledged to TCP only as the shell consumes it (tcp_recved), so a host pushing a file
 * with `recv' is flow-controlled by the board instead of overrunning it.  Output goes into a 16 KB ring that netsh_poll
 * hands to tcp_write as the send window allows.  Console output never blocks (a shell with nobody listening must never
 * park in a print): when the ring is full the byte is dropped and
 * counted.  Bulk data (`send') uses netsh_write_all, which blocks and polls: file bytes must never be dropped.
 *
 * ONE SESSION.  A second connection gets one line saying so and is closed; two people typing into one shell is not a
 * feature.
 *
 * THE BEACON.  Once an address is up, a UDP broadcast every second on port 48951:
 *     PANO mac=02:50:41:4e:4f:01 ip=10.0.0.18 port=2323 build=2ef8a555 session=0
 * (the G2 build adds " board=g2" before " session")
 * so a host finds the board with no IP typed -- DHCP means nobody knows it in advance.
 */
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/ip4_addr.h"
#include "lwip/timeouts.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "board.h"
#include "timer_ms.h"
#include "netbulk.h"
#include "ethmac.h"
#include "ethphy.h"
#include "netif_pano.h"
#include "netsh.h"
#include "ntptime.h"
#include "crc32.h"
#ifdef PANO_G2
#include "g2.h"
#endif

#ifdef PANO_G2
#define RING 8192u                                /* block RAM: each ring is 3% of the G2's 256 KB */
#else
#define RING 16384u
#endif
static uint8_t  s_rx[RING], s_tx[RING];
static unsigned s_rxh, s_rxt, s_txh, s_txt;       /* head = next write, tail = next read */
static unsigned s_unacked;                        /* bytes consumed from s_rx not yet given back to TCP */
static struct netif    s_nif;
static struct tcp_pcb *s_listen, *s_sess;
static struct udp_pcb *s_beacon;
static int      s_phy = -1, s_up, s_announced, s_dhcp;
static unsigned s_gen;                            /* bumped at every connect and disconnect: see netsh_gen() */
static uint32_t s_next_beacon, s_dropped, s_sessions;

/* TIMING: where a received byte's cycles go.  Read and zeroed by netsh_perf(). */
static inline uint32_t cyc(void) { uint32_t c; __asm__ volatile ("rdcycle %0" : "=r"(c)); return c; }
static uint32_t s_cyc_poll, s_cyc_wait, s_cyc_copy;
/* THE SINK: while `recv' runs, session payload goes straight from the received pbuf -- which, with the
 * zero-copy netif, is the frame in the MAC's fast-RAM ring -- into the destination, the CRC folded in from that
 * single-cycle source.  No session ring on the way: one LPDDR store per word instead of three. */
static uint8_t  *s_sink;
static uint32_t  s_sink_left, *s_sink_crc;

static unsigned rx_used(void) { return (s_rxh - s_rxt) & (RING - 1u); }
static unsigned tx_used(void) { return (s_txh - s_txt) & (RING - 1u); }

/* copy into / out of a ring in at most two contiguous spans: memcpy (fw/pmem.c) stores words, and a byte loop into LPDDR
 * pays ~31 cycles per BYTE -- measured as the difference between 20 KB/s and usable */
static void ring_put(uint8_t *r, unsigned *head, const uint8_t *src, unsigned n)
{
    unsigned h = *head, a = RING - h;
    if (a > n) a = n;
    memcpy(r + h, src, a);
    if (n > a) memcpy(r, src + a, n - a);
    *head = (h + n) & (RING - 1u);
}
static void ring_get(const uint8_t *r, unsigned *tail, uint8_t *dst, unsigned n, uint32_t *crc)
{
    unsigned t = *tail, a = RING - t;
    if (a > n) a = n;
    /* the CRC is taken from the RING (cached LPDDR), not from the destination: a caller's buffer is typically the
     * uncached window, where every byte load is a bus round trip -- measured at ~45 cycles a byte */
    if (crc) { *crc = crc32_run(*crc, r + t, a); if (n > a) *crc = crc32_run(*crc, r, n - a); }
    memcpy(dst, r + t, a);
    if (n > a) memcpy(dst + a, r, n - a);
    *tail = (t + n) & (RING - 1u);
}

static void sess_gone(void) { s_sess = 0; s_rxh = s_rxt = s_txh = s_txt = 0; s_unacked = 0; s_sink_left = 0; s_gen++; }

static void give_back(int force)
{
    if (s_sess && s_unacked && (force || s_unacked >= 1024u)) { tcp_recved(s_sess, (u16_t)s_unacked); s_unacked = 0; }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;
    (void)arg; (void)err;
    if (!p) {                                        /* the host closed its side */
        tcp_arg(pcb, 0); tcp_recv(pcb, 0); tcp_err(pcb, 0);
        tcp_close(pcb);
        if (pcb == s_sess) sess_gone();
        return ERR_OK;
    }
    if (pcb != s_sess) { pbuf_free(p); return ERR_OK; }
    if (s_sink_left) {
        uint32_t taken = 0, rest = p->tot_len;
        if (rest > s_sink_left && (rest - s_sink_left) > RING - 1u - rx_used()) return ERR_MEM;   /* the tail must fit */
        for (q = p; q; q = q->next) {
            const uint8_t *src = (const uint8_t *)q->payload;
            uint32_t n = q->len, k = n < s_sink_left ? n : s_sink_left;
            if (k) {
                *s_sink_crc = crc32_run(*s_sink_crc, src, k);
                memcpy(s_sink, src, k);
                s_sink += k; s_sink_left -= k; taken += k;
            }
            if (n > k) ring_put(s_rx, &s_rxh, src + k, n - k);   /* past the transfer: ordinary session input */
        }
        pbuf_free(p);
        tcp_recved(pcb, (u16_t)taken);               /* consumed already: give the window back now */
        return ERR_OK;
    }
    if (p->tot_len > RING - 1u - rx_used()) return ERR_MEM;   /* no room: lwIP keeps it and offers it again later */
    for (q = p; q; q = q->next) ring_put(s_rx, &s_rxh, (const uint8_t *)q->payload, q->len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) { (void)arg; (void)err; sess_gone(); }   /* lwIP has freed the pcb */

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    static const char busy[] = "netsh: another session is open -- one at a time\r\n";
    static const char hello[] = "\r\nPano netsh -- the boot shell over the network.  `help' for commands.\r\n> ";
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    if (s_sess) {
        tcp_write(pcb, busy, sizeof busy - 1u, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        tcp_close(pcb);
        return ERR_OK;
    }
    sess_gone();
    s_sess = pcb;
    s_sessions++;
    tcp_nagle_disable(pcb);                          /* a shell: every keystroke's echo goes now */
    tcp_arg(pcb, 0); tcp_recv(pcb, on_recv); tcp_err(pcb, on_err);
    netsh_puts(hello);
    return ERR_OK;
}

void netsh_flush(void)
{
    if (!s_sess) { s_txt = s_txh; return; }
    while (tx_used()) {
        unsigned n = (s_txh >= s_txt) ? s_txh - s_txt : RING - s_txt;   /* contiguous run */
        unsigned room = tcp_sndbuf(s_sess);
        if (!room) break;
        if (n > room) n = room;
        if (tcp_write(s_sess, &s_tx[s_txt], (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        s_txt = (s_txt + n) & (RING - 1u);
    }
    tcp_output(s_sess);
}

/* the session's output sent and acknowledged by the peer, or ms milliseconds (a CPU cycle count) spent trying */
void netsh_drain(unsigned ms)
{
    uint32_t t0 = cyc();
    while (s_sess && (tx_used() || s_sess->unsent || s_sess->unacked)) {
        netsh_poll();
        if (cyc() - t0 >= ms * PANO_CYC_PER_MS) break;
    }
}

void netsh_init(void)
{
    if (s_up) return;
    lwip_init();                                     /* once, before netif_add (nettest learned this the hard way) */
    s_phy = netif_pano_up(&s_nif);
    dhcp_start(&s_nif); s_dhcp = 1;
    s_listen = tcp_new();
    if (s_listen && tcp_bind(s_listen, IP_ADDR_ANY, NETSH_PORT) == ERR_OK) {
        s_listen = tcp_listen(s_listen);
        tcp_accept(s_listen, on_accept);
    }
    s_beacon = udp_new();
    s_next_beacon = get_millis() + 1000u;
    s_up = 1;
}

static void beacon(void)
{
    char b[128];
    unsigned n = 0, k;
    const uint8_t *m = netif_pano_mac;
    static const char hx[] = "0123456789abcdef";
    struct pbuf *p;
    const char *ip = ip4addr_ntoa(netif_ip4_addr(&s_nif));
    uint32_t id = SOC_BUILD_ID;
#define PUT(s) do { const char *_s = (s); while (*_s && n < sizeof b - 1u) b[n++] = *_s++; } while (0)
    PUT("PANO mac=");
    for (k = 0; k < 6; k++) { b[n++] = hx[m[k] >> 4]; b[n++] = hx[m[k] & 15u]; if (k < 5) b[n++] = ':'; }
    PUT(" ip="); PUT(ip); PUT(" port=2323 build=");
    for (k = 0; k < 8; k++) b[n++] = hx[(id >> (28u - 4u * k)) & 15u];
#ifdef PANO_G2
    PUT(" board=g2");                                /* the board type, for discovery */
#endif
    PUT(s_sess ? " session=1\n" : " session=0\n");
#undef PUT
    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, b, n);
    udp_sendto(s_beacon, p, IP_ADDR_BROADCAST, NETSH_BEACON_PORT);
    pbuf_free(p);
}

int netsh_poll(void)
{
    int ev = 0;
    uint32_t t0 = cyc();
#ifdef PANO_G2
    g2_wdt_kick();                                   /* every long network operation polls here */
#endif
    if (!s_up) return 0;
    netif_pano_poll(&s_nif);                         /* MAC ring -> pbuf -> lwIP -> on_recv -> s_rx: the stack's cost */
    sys_check_timeouts();
    ntptime_poll();
    netsh_flush();
    s_cyc_poll += cyc() - t0;
    give_back(rx_used() == 0);
    if (!ip4_addr_isany(netif_ip4_addr(&s_nif))) {
        if (!s_announced) { s_announced = 1; ev = 1; ntptime_start(netsh_gw_host(), 0u); }   /* the address just arrived: ask the time once */
        if (s_beacon && (int32_t)(get_millis() - s_next_beacon) >= 0) { s_next_beacon += 1000u; beacon(); }
    }
    return ev;
}

uint32_t netsh_gw_host(void) { return lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(&s_nif))); }   /* host order, 0 if none */
int netsh_session(void) { return s_sess != 0; }
unsigned netsh_gen(void) { return s_gen; }

int netsh_getc(void)
{
    int c;
    if (!s_sess || !rx_used()) return -1;
    c = s_rx[s_rxt];
    s_rxt = (s_rxt + 1u) & (RING - 1u);
    s_unacked++;
    give_back(0);
    return c;
}

int netsh_read(void *buf, unsigned n, unsigned timeout_ms) { return netsh_read_crc(buf, n, timeout_ms, 0); }

int netsh_read_crc(void *buf, unsigned n, unsigned timeout_ms, uint32_t *crc)
{
    uint8_t *o = (uint8_t *)buf;
    unsigned got = 0, t0 = get_millis();
    uint32_t c0 = cyc();
    while (!rx_used()) {
        if (!s_sess) return -1;
        netsh_poll();
        if (get_millis() - t0 > timeout_ms) return 0;
    }
    s_cyc_wait += cyc() - c0;
    got = rx_used();
    if (got > n) got = n;
    c0 = cyc();
    ring_get(s_rx, &s_rxt, o, got, crc);
    s_cyc_copy += cyc() - c0;
    s_unacked += got;
    give_back(0);
    return (int)got;
}

/* ---- A FULL TX RING MUST DRAIN, NOT DROP ---------------------------------------------------
 * This used to `s_dropped++; return;' and that silently ate the END of any command whose output exceeds the
 * 16 KB ring.  A listing of about 38 KB stopped at 16 KB, and the bytes that went missing included the summary
 * line AND THE PROMPT, which is what a client waits for: a dropped prompt looks exactly like a hang.
 *
 * netsh_write_all() has always done the right thing here (poll until there is room), so this is only the
 * character path catching up with the block path.  Two guards make it safe to call the stack from inside a
 * character write:
 *   - in_drain: netsh_poll() runs lwIP callbacks, and a callback that says something would re-enter.  While
 *     draining we fall back to the old drop, which is strictly better than unbounded recursion.
 *   - a 500 ms bound: a peer that has stopped reading must not be able to wedge the shell forever.  Past the
 *     bound we drop exactly as before, and s_dropped still counts it, so `net stat' keeps telling the truth. */
#define TX_DRAIN_MS 500u

void netsh_putc(char c)
{
    static int in_drain;

    if (!s_sess) return;
    if (tx_used() == RING - 1u) {
        if (!in_drain) {
            uint32_t t0 = get_millis();
            in_drain = 1;
            while (s_sess && tx_used() == RING - 1u && (get_millis() - t0) < TX_DRAIN_MS)
                netsh_poll();
            in_drain = 0;
        }
        if (!s_sess) return;
        if (tx_used() == RING - 1u) { s_dropped++; return; }
    }
    s_tx[s_txh] = (uint8_t)c;
    s_txh = (s_txh + 1u) & (RING - 1u);
}

void netsh_puts(const char *s) { while (*s) netsh_putc(*s++); }

int netsh_write_all(const void *buf, unsigned n)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (n) {
        unsigned room;
        if (!s_sess) return -1;
        room = RING - 1u - tx_used();
        if (!room) { netsh_poll(); continue; }
        if (room > n) room = n;
        ring_put(s_tx, &s_txh, p, room);
        p += room; n -= room;
    }
    while (s_sess && tx_used()) netsh_poll();        /* drain, so the caller's trailer line comes after the data */
    return s_sess ? 0 : -1;
}

void netsh_info(netsh_info_t *i)
{
    memset(i, 0, sizeof *i);
    i->phy = s_phy;
    i->link = (s_phy >= 0) && (phy_read((unsigned)s_phy, PHY_BMSR) & BMSR_LINK);
    i->bound = !ip4_addr_isany(netif_ip4_addr(&s_nif));
    i->dhcp = s_dhcp;
    i->ip = ip4_addr_get_u32(netif_ip4_addr(&s_nif));
    i->mask = ip4_addr_get_u32(netif_ip4_netmask(&s_nif));
    i->gw = ip4_addr_get_u32(netif_ip4_gw(&s_nif));
    i->session = s_sess != 0;
    i->tx_dropped = s_dropped;
    i->sessions = s_sessions;
}

void netsh_dhcp(void) { s_announced = 0; dhcp_start(&s_nif); s_dhcp = 1; }

void netsh_static(uint32_t ip, uint32_t mask, uint32_t gw)
{
    ip4_addr_t a, m, g;
    dhcp_stop(&s_nif); s_dhcp = 0;
    IP4_ADDR(&a, ip >> 24, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
    IP4_ADDR(&m, mask >> 24, (mask >> 16) & 255u, (mask >> 8) & 255u, mask & 255u);
    IP4_ADDR(&g, gw >> 24, (gw >> 16) & 255u, (gw >> 8) & 255u, gw & 255u);
    netif_set_addr(&s_nif, &a, &m, &g);
    s_announced = 0;
}

int netsh_sink(uint8_t *dst, uint32_t n, uint32_t *crc, unsigned idle_ms)
{
    uint32_t have = rx_used(), t0, last;
    if (have > n) have = n;
    if (have) {                                      /* bytes that arrived before the sink was set: in order, first */
        ring_get(s_rx, &s_rxt, dst, have, crc);
        s_unacked += have; give_back(1);
    }
    s_sink = dst + have; s_sink_left = n - have; s_sink_crc = crc;
    t0 = get_millis(); last = s_sink_left;
    while (s_sink_left) {
        if (!s_sess) { s_sink_left = 0; return -1; }
        netsh_poll();
        if (s_sink_left != last) { last = s_sink_left; t0 = get_millis(); }
        else if (get_millis() - t0 > idle_ms) { uint32_t left = s_sink_left; s_sink_left = 0; return (int)(n - left); }
    }
    return (int)n;
}

/* ---- THE BULK FAST PATH: fw/netbulk.c does the work; NETSH only supplies the address and hooks the netif ---- */
static int bulk_hook(const volatile uint8_t *a, unsigned na, const volatile uint8_t *b, unsigned len) { return netbulk_frame(a, na, b, len); }

int netsh_bulk_begin(uint8_t *dst, uint32_t len, uint16_t id)
{
    int c = netbulk_begin(dst, len, id, (const uint8_t *)netif_ip4_addr(&s_nif));
    if (c > 0) netif_pano_fastpath = bulk_hook;
    return c;
}
void netsh_bulk_end(void) { netif_pano_fastpath = 0; netbulk_end(); }
void netsh_bulk_status(uint32_t *got, uint32_t *chunks, uint32_t *dups) { netbulk_status(got, chunks, dups); }
int netsh_bulk_missing(uint32_t *from, uint32_t *count) { return netbulk_missing(from, count); }
void netsh_bulk_perf(uint32_t *frames, uint32_t *cycles) { netbulk_perf(frames, cycles); }
void netsh_bulk_dma(int on) { netbulk_use_dma(on); }
int netsh_bulk_crc(const uint8_t *img, uint32_t len, uint32_t *crc) { return netbulk_crc(img, len, crc); }
void netsh_bulk_dma_stat(uint32_t *n, uint32_t *err) { netbulk_dma_stat(n, err); }
/* the MAC ring only: bulk datagrams in the hook, anything else to lwIP -- no timers, no TCP flush (brecv's inner loop) */
void netsh_poll_rx(void) { if (s_up) netif_pano_poll(&s_nif); }

void netsh_perf(uint32_t *poll, uint32_t *wait, uint32_t *copy)
{
    *poll = s_cyc_poll; *wait = s_cyc_wait; *copy = s_cyc_copy;
    s_cyc_poll = s_cyc_wait = s_cyc_copy = 0;
}

void netsh_down(void)
{
    if (s_sess) {
        netsh_puts("\r\nnetsh: closing -- exec\r\n");
        netsh_flush();
        tcp_arg(s_sess, 0); tcp_recv(s_sess, 0); tcp_err(s_sess, 0);
        tcp_close(s_sess);
        sess_gone();
    }
    { unsigned t0 = get_millis(); while (get_millis() - t0 < 50u) { netif_pano_poll(&s_nif); sys_check_timeouts(); } }
    ethmac_stop();
}

void netsh_resume(void)
{
    if (s_up) ethmac_init(netif_pano_mac, 0);
}

/* ---- ping: one ICMP echo request, and its reply ---------------------------------------------------------------
 * A raw ICMP PCB receives every ICMP packet addressed to us; the reply that matches this request's identifier and
 * sequence number is taken, and lwIP's own ICMP input still answers other hosts' requests (the callback returns 0 for
 * everything it does not consume). */
#define PING_ID 0x5047u                                     /* "PG" */
static struct raw_pcb *s_ping_pcb;
static volatile int      s_ping_got;
static volatile unsigned s_ping_seq, s_ping_ttl, s_ping_len;

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    struct icmp_echo_hdr eh;
    const struct ip_hdr *iph = (const struct ip_hdr *)p->payload;
    u16_t hl;
    (void)arg; (void)pcb; (void)addr;
    if (p->tot_len < sizeof(struct ip_hdr)) return 0;
    hl = (u16_t)(IPH_HL(iph) * 4u);
    if (p->tot_len < hl + sizeof eh) return 0;
    pbuf_copy_partial(p, &eh, sizeof eh, hl);
    if (eh.type != ICMP_ER || eh.id != lwip_htons(PING_ID) || eh.seqno != lwip_htons((u16_t)s_ping_seq)) return 0;
    s_ping_ttl = IPH_TTL(iph);
    s_ping_len = (unsigned)(p->tot_len - hl - sizeof eh);
    s_ping_got = 1;
    pbuf_free(p);
    return 1;
}

int netsh_ping(uint32_t ip_host, unsigned seq, unsigned size, unsigned timeout_ms,
               unsigned *rtt_ms, unsigned *ttl, unsigned *len, int (*stop)(void))
{
    struct pbuf *p;
    struct icmp_echo_hdr *eh;
    ip_addr_t dst;
    uint32_t t0;
    unsigned k;
    if (!s_up) return -1;
    if (!s_ping_pcb) {
        s_ping_pcb = raw_new(IP_PROTO_ICMP);
        if (!s_ping_pcb) return -2;
        raw_recv(s_ping_pcb, ping_recv, 0);
        raw_bind(s_ping_pcb, IP_ADDR_ANY);
    }
    p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof *eh + size), PBUF_RAM);
    if (!p) return -3;
    eh = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(eh, ICMP_ECHO);
    ICMPH_CODE_SET(eh, 0);
    eh->id = lwip_htons(PING_ID);
    eh->seqno = lwip_htons((u16_t)seq);
    for (k = 0; k < size; k++) ((u8_t *)(eh + 1))[k] = (u8_t)('a' + k % 23u);
    eh->chksum = 0;
    eh->chksum = inet_chksum(eh, (u16_t)(sizeof *eh + size));
    IP4_ADDR(ip_2_ip4(&dst), (u8_t)(ip_host >> 24), (u8_t)(ip_host >> 16), (u8_t)(ip_host >> 8), (u8_t)ip_host);
    s_ping_seq = seq; s_ping_got = 0;
    t0 = get_millis();
    if (raw_sendto(s_ping_pcb, p, &dst) != ERR_OK) { pbuf_free(p); return -4; }
    pbuf_free(p);
    for (;;) {
        netsh_poll();
        if (s_ping_got) { *rtt_ms = (unsigned)(get_millis() - t0); *ttl = s_ping_ttl; *len = s_ping_len; return 0; }
        if (stop && stop()) return 2;
        if ((unsigned)(get_millis() - t0) >= timeout_ms) return 1;
    }
}
