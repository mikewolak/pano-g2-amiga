/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netif_pano.c -- the lwIP netif for the Pano G1's MII MAC (rtl/soc_ethlite.v via fw/ethmac.c).
 *
 * OUTPUT linearises the pbuf chain into one buffer and hands it to ethmac_send, WAITING for the transmitter if it is
 * busy: a frame is at most 122 us on the wire, and dropping it instead (the first netif returned ERR_IF) turns every
 * back-to-back pair lwIP emits -- an ARP then the datagram that needed it, two TCP segments -- into a retransmit.
 *
 * INPUT copies each frame straight from the MAC's RX ring (uncached fast RAM, single-cycle reads) into a pool pbuf and
 * passes it up; the ring space is freed as the copy completes.  lwIP's TCP_WND is sized to what the ring holds while
 * the CPU drains it (port/lwipopts.h), so a sender cannot outrun it for long.
 *
 * The MAC is not a bus master and cannot write LPDDR, so nothing here has to be torn down before an exec.
 */
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "ethmac.h"
#include "ethphy.h"
#include "netif_pano.h"

#ifdef PANO_G2
#include "g2.h"
/* The G2's own factory MAC, read from the configuration table in flash (fw/g2.c) at netif_pano_up; this locally
 * administered address ("PANO", 02) stands in when the table has none. */
uint8_t netif_pano_mac[6] = { 0x02, 0x50, 0x41, 0x4E, 0x4F, 0x02 };
#else
/* 02:50:41:4E:4F:01 -- locally administered ("PANO"); per ETHERNET_AND_NETBOOT_PLAN section 2.4 */
const uint8_t netif_pano_mac[6] = { 0x02, 0x50, 0x41, 0x4E, 0x4F, 0x01 };
#endif

static uint8_t s_tx[ETHMAC_TX_MAX];

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    unsigned len = 0, t = 0;
    struct pbuf *q;
    (void)netif;
    for (q = p; q != 0; q = q->next) {
        if (len + q->len > sizeof s_tx) return ERR_BUF;
        memcpy(s_tx + len, q->payload, q->len);
        len += q->len;
    }
    while (ethmac_send(s_tx, len) == -1)
        if (++t > 2000000u) return ERR_IF;           /* tens of ms: the transmitter is wedged, not busy */
    return ERR_OK;
}

static err_t netif_pano_init(struct netif *netif)
{
    netif->name[0] = 'p';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, netif_pano_mac, 6);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

int netif_pano_up(struct netif *netif)
{
    int phy;
    phy_power_up();
    phy = phy_find();
#ifdef PANO_G2
    { uint8_t m[6]; if (g2_factory_mac(m)) memcpy(netif_pano_mac, m, 6); }
#ifdef NETIF_PANO_MII
    /* a fabric whose MAC samples RX_CLK as data (rtl/g2/g2_gmac.v RX_OS) receives MII only: 10/100 advertised */
    if (phy >= 0) phy_g2_advertise((unsigned int)phy, 0);
#else
    if (phy >= 0) phy_g2_setup((unsigned int)phy);
#endif
#endif
    ethmac_init(netif_pano_mac, 0);
    netif_add(netif, 0, 0, 0, 0, netif_pano_init, netif_input);
    netif_set_default(netif);
    netif_set_up(netif);
    return phy;
}

void netif_pano_down(struct netif *netif)
{
    netif_set_down(netif);
    ethmac_stop();
}

/* ZERO-COPY RECEIVE.  The frame is already in fast RAM, where an uncached read costs one cycle; copying it
 * into a pool pbuf in LPDDR cost ~31 cycles per stored word and was most of the stack's time.  So lwIP gets a custom
 * PBUF_REF that points at the frame IN the ring, and the ring slot is given back (ethmac_consume) when lwIP frees it.
 * ONE frame is lent at a time -- the MAC's ring is consumed strictly in order, and a second frame cannot be released
 * before the first -- so while one is out, polling stops.  A frame that wraps the ring end takes the copy path. */
/* THE FAST-PATH HOOK: offered every received frame, in place, BEFORE lwIP.  Returning 1 means it took the
 * frame (it is then released straight back to the ring); 0 hands it to lwIP as usual.  NETSH's bulk receiver uses it:
 * a bulk datagram never touches the stack.  A frame that wraps the ring end is offered as its two ring segments --
 * never copied first: with full frames in a 640-word ring, every other one wraps. */
netif_fastpath_fn netif_pano_fastpath;
netif_fastpath_fn netif_pano_tap;

static struct pbuf_custom s_lent;
static int s_lent_busy;

static void lent_free(struct pbuf *p)
{
    (void)p;
    ethmac_consume();
    s_lent_busy = 0;
}

unsigned netif_pano_poll(struct netif *netif)
{
    unsigned n = 0;
    int len;
    while (!s_lent_busy && (len = ethmac_peek_len()) > 0) {
        const volatile uint8_t *f = ethmac_peek_ptr();
        if (netif_pano_tap) {
            const volatile uint8_t *a, *b;
            unsigned na;
            ethmac_peek_seg(&a, &na, &b);
            if (netif_pano_tap(a, na, b, (unsigned)len)) { ethmac_consume(); n++; continue; }
        }
        if (netif_pano_fastpath) {
            const volatile uint8_t *a, *b;
            unsigned na;
            ethmac_peek_seg(&a, &na, &b);
            if (netif_pano_fastpath(a, na, b, (unsigned)len)) { ethmac_consume(); n++; continue; }
        }
        if (f) {
            struct pbuf *p;
            s_lent.custom_free_function = lent_free;
            p = pbuf_alloced_custom(PBUF_RAW, (u16_t)len, PBUF_REF, &s_lent, (void *)(uintptr_t)f, (u16_t)len);
            if (!p) break;
            s_lent_busy = 1;
            if (netif->input(p, netif) != ERR_OK) pbuf_free(p);
            n++;
            continue;
        }
        struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (p == 0) break;                            /* out of pbufs: leave the frame in the ring for the next poll */
        pbuf_take_ring(p, (unsigned)len);
        if (netif->input(p, netif) != ERR_OK) pbuf_free(p);
        n++;
    }
    return n;
}

/* copy the frame at the ring's read index into the (possibly chained) pbuf, then consume it */
void pbuf_take_ring(struct pbuf *p, unsigned len)
{
    struct pbuf *q;
    unsigned off = 0;
    for (q = p; q != 0 && off < len; q = q->next) {
        unsigned n = q->len;
        if (n > len - off) n = len - off;
        ethmac_copy(q->payload, off, n);
        off += n;
    }
    ethmac_consume();
}
