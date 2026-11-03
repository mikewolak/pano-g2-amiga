/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ntptime.c -- see ntptime.h.  RFC 4330 (SNTPv4), client mode, one 48-byte datagram each way. */
#include <string.h>
#include "ntptime.h"
#include "timer_ms.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#define NTP_PORT      123u
#define NTP_UNIX_DIFF 2208988800u                  /* 1900 -> 1970 */
#define TRY_MS        1500u
#define ROUNDS        2u

int ntptime_tz_min = 0;

static struct udp_pcb *s_pcb;
static uint32_t s_srv[5]; static unsigned s_nsrv, s_ix, s_round, s_tries;
static uint32_t s_sent_ms; static int s_busy;
static uint32_t s_unix, s_at_ms, s_who; static unsigned s_rtt; static int s_known;

static void on_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    uint8_t b[48]; uint32_t secs, now = get_millis();
    (void)arg; (void)pcb;
    if (p && s_busy && port == NTP_PORT && p->tot_len >= 48u && pbuf_copy_partial(p, b, 48u, 0) == 48u) {
        unsigned mode = b[0] & 7u, stratum = b[1];
        secs = ((uint32_t)b[40] << 24) | ((uint32_t)b[41] << 16) | ((uint32_t)b[42] << 8) | b[43];   /* transmit timestamp */
        /* a server's answer (mode 4), synchronised (stratum 1..15; 0 is a kiss-of-death), with a time after 2020 */
        if (mode == 4u && stratum >= 1u && stratum <= 15u && secs > NTP_UNIX_DIFF + 1577836800u) {
            s_rtt   = now - s_sent_ms;
            s_unix  = secs - NTP_UNIX_DIFF;
            s_at_ms = now - s_rtt / 2u;             /* the server stamped it about half a round trip ago */
            s_who   = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr)));
            s_known = 1; s_busy = 0;
        }
    }
    if (p) pbuf_free(p);
}

static void ask(void)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 48u, PBUF_RAM);
    ip_addr_t a;
    if (!p) return;
    memset(p->payload, 0, 48u);
    ((uint8_t *)p->payload)[0] = 0x23u;             /* LI 0, VN 4, mode 3 (client) */
    ip_addr_set_ip4_u32(&a, lwip_htonl(s_srv[s_ix]));
    udp_sendto(s_pcb, p, &a, NTP_PORT);
    pbuf_free(p);
    s_sent_ms = get_millis(); s_tries++;
}

void ntptime_start(uint32_t gw, uint32_t override)
{
    static const uint32_t pub[3] = { 0xD8EF2300u /* 216.239.35.0 time.google.com */, 0xA29FC801u /* 162.159.200.1 time.cloudflare.com */,
                                     0x81060F1Cu /* 129.6.15.28 time-a-g.nist.gov */ };
    unsigned k;
    if (!s_pcb) { s_pcb = udp_new(); if (!s_pcb) return; udp_recv(s_pcb, on_recv, 0); }
    s_nsrv = 0;
    if (override) s_srv[s_nsrv++] = override;
    if (gw)       s_srv[s_nsrv++] = gw;
    for (k = 0; k < 3u; k++) s_srv[s_nsrv++] = pub[k];
    s_ix = 0; s_round = 0; s_tries = 0; s_busy = 1;
    ask();
}

void ntptime_poll(void)
{
    if (!s_busy || (uint32_t)(get_millis() - s_sent_ms) < TRY_MS) return;
    if (++s_ix >= s_nsrv) { s_ix = 0; if (++s_round >= ROUNDS) { s_busy = 0; return; } }
    ask();
}

int      ntptime_known(void)  { return s_known; }
int      ntptime_busy(void)   { return s_busy; }
uint32_t ntptime_server(void) { return s_who; }
unsigned ntptime_rtt_ms(void) { return s_rtt; }
unsigned ntptime_tries(void)  { return s_tries; }
uint32_t ntptime_unix(void)   { return s_known ? s_unix + (uint32_t)(get_millis() - s_at_ms) / 1000u : 0u; }

/* days since 1970-01-01 -> civil date (Howard Hinnant's algorithm, unsigned form; valid far past 2100) */
void ntptime_civil(uint32_t t, unsigned *Y, unsigned *M, unsigned *D, unsigned *h, unsigned *m, unsigned *s)
{
    uint32_t days = t / 86400u, rem = t % 86400u, z = days + 719468u, era = z / 146097u, doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u, y = yoe + era * 400u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u), mp = (5u * doy + 2u) / 153u;
    *D = doy - (153u * mp + 2u) / 5u + 1u; *M = mp < 10u ? mp + 3u : mp - 9u; *Y = y + (*M <= 2u);
    *h = rem / 3600u; *m = (rem % 3600u) / 60u; *s = rem % 60u;
}

#if defined(FF_FS_NORTC) && FF_FS_NORTC == 0
/* FatFs asks this for every file it creates or closes after writing: the stick's timestamps, in local time */
unsigned long get_fattime(void)
{
    unsigned Y = 2026u, M = 9u, D = 6u, h = 0, m = 0, s = 0;                       /* unknown: the old fixed date */
    if (s_known) ntptime_civil(ntptime_unix() + (uint32_t)(ntptime_tz_min * 60), &Y, &M, &D, &h, &m, &s);
    if (Y < 1980u) Y = 1980u;
    return ((unsigned long)(Y - 1980u) << 25) | ((unsigned long)M << 21) | ((unsigned long)D << 16) | ((unsigned long)h << 11) | ((unsigned long)m << 5) | (s / 2u);
}
#endif
