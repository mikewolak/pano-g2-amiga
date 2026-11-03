/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/xsurf.c -- the firmware side of the X-Surf 100 board of the AGA fabric: the
 * host-mailbox protocol of the Minimig-AGA Ethernet branch's MiSTer daemon (amiga/third_party/minimig_eth/extra:
 * minimig_eth.cpp minimig_eth_poll, transmit_packet, enqueue_shared_rx_packet; offsets of minimig_eth_abi.h), with
 * the G2's MAC (fw/ethmac.c) in place of the daemon's AF_PACKET socket.
 *
 * THE MAILBOX is reached through MBX_ADDR and MBX_DATA (amiga/rtl/g2/xsurf_mbx.v) as the daemon saw its DDR window: a
 * byte array in which byte 2k is the upper byte of the model's word k, and 16- and 32-bit fields are little-endian
 * over those bytes.
 *
 * FRAMES.  The card's station address is the unit's factory MAC with the locally administered bit set (the firmware
 * keeps the factory MAC for its own console).  While the card is present the MAC receives every frame (promiscuous):
 * the receive tap queues frames addressed to the card's current address (the model's ETH_CTRL_MAC, which the Amiga
 * driver may change) and takes them from lwIP; broadcast and multicast frames are queued and also left to lwIP; the
 * rest go to lwIP alone.  Queued frames enter the model's 16-slot receive queue from xsurf_poll. */
#include <stdint.h>
#include <string.h>
#include "amiga_regs.h"
#include "xsurf.h"
#include "../ethmac.h"
#include "../netif_pano.h"
#include "../g2.h"

#define ETH_CTRL_FLAGS        0x1000u
#define ETH_CTRL_MAC          0x104Cu
#define ETH_CTRL_STATUS       0x1052u
#define ETH_HPS_HEARTBEAT     0x1088u
#define ETH_HPS_SIGNATURE     0x108Cu
#define ETH_HPS_MAGIC         0xCAFEBABEu
#define ETH_TX_BUFFER         0x2000u
#define ETH_PKT_MAX           0x0600u
#define ETH_TX_REQUEST_LEN    0x2C02u
#define ETH_RX_QUEUE_HEAD     0x2C04u
#define ETH_RX_QUEUE_TAIL     0x2C06u
#define ETH_TX_REQUEST_SEQ    0x2C08u
#define ETH_TX_COMPLETE_SEQ   0x2C0Au
#define ETH_RX_QUEUE_LEN      0x2C20u
#define ETH_RX_QUEUE_DATA     0x9000u
#define ETH_RX_SLOTS          16u
#define ETH_FLAG_TX_REQ       0x0002u
#define ETH_FLAG_RX_AVAIL     0x0004u
#define ETH_STATUS_TX_OK      0x0001u
#define ETH_STATUS_TX_ERR     0x0002u
#define ETH_STATUS_LINK_UP    0x0004u
#define ETH_STATUS_FULL_DUPLEX 0x0008u

#define HOSTQ 8u
static struct { uint16_t len; uint8_t d[ETH_PKT_MAX]; } s_q[HOSTQ];
static unsigned s_qh, s_qt, s_qn;
static uint8_t s_txb[ETH_PKT_MAX];
static unsigned s_present;
static uint8_t s_mac[6];
static uint32_t s_hb, s_polls;
static uint16_t s_last_seq;
static uint32_t s_tx, s_tx_err, s_rx, s_rx_drop;

static void mb_get(uint32_t off, uint8_t *b, unsigned n)
{
    unsigned i = 0;
    AMI_MBX_ADDR = off;
    if (off & 1u) b[i++] = (uint8_t)AMI_MBX_DATA;
    while (i + 1u < n) { uint32_t w = AMI_MBX_DATA; b[i++] = (uint8_t)(w >> 8); b[i++] = (uint8_t)w; }
    if (i < n) b[i] = (uint8_t)(AMI_MBX_DATA >> 8);
}
static void mb_put(uint32_t off, const uint8_t *b, unsigned n)
{
    unsigned i = 0;
    uint32_t w;
    if (off & 1u) { AMI_MBX_ADDR = off; w = AMI_MBX_DATA; AMI_MBX_ADDR = off; AMI_MBX_DATA = (w & 0xFF00u) | b[i++]; }
    else AMI_MBX_ADDR = off;
    while (i + 1u < n) { AMI_MBX_DATA = ((uint32_t)b[i] << 8) | b[i + 1u]; i += 2u; }
    if (i < n) { AMI_MBX_ADDR = off + i; w = AMI_MBX_DATA; AMI_MBX_ADDR = off + i; AMI_MBX_DATA = ((uint32_t)b[i] << 8) | (w & 0xFFu); }
}
static uint32_t mb_r16(uint32_t off) { uint8_t b[2]; mb_get(off, b, 2u); return (uint32_t)b[0] | ((uint32_t)b[1] << 8); }
static uint32_t mb_r32(uint32_t off) { uint8_t b[4]; mb_get(off, b, 4u); return b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static void mb_w16(uint32_t off, uint32_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; mb_put(off, b, 2u); }
static void mb_w32(uint32_t off, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    mb_put(off, b, 4u);
}
static void status_set(uint32_t clear, uint32_t set)
{
    mb_w16(ETH_CTRL_STATUS, ((mb_r16(ETH_CTRL_STATUS) & ~clear) | set | ETH_STATUS_LINK_UP | ETH_STATUS_FULL_DUPLEX) & 0xFFFFu);
}

/* the receive tap (netif_pano_tap): 1 takes the frame from lwIP */
static int xs_tap(const volatile uint8_t *a, unsigned na, const volatile uint8_t *b, unsigned len)
{
    unsigned i, mine = 1, group;
    for (i = 0; i < 6u; i++) if ((i < na ? a[i] : b[i - na]) != s_mac[i]) mine = 0;
    group = ((na ? a[0] : b[0]) & 1u) != 0u;
    if (!mine && !group) return 0;
    if (len > ETH_PKT_MAX || s_qn == HOSTQ) { s_rx_drop++; return mine; }
    for (i = 0; i < len; i++) s_q[s_qt].d[i] = i < na ? a[i] : b[i - na];
    s_q[s_qt].len = (uint16_t)len;
    s_qt = (s_qt + 1u) % HOSTQ; s_qn++;
    return mine;
}

int xsurf_start(void)
{
    uint8_t f[6];
    AMI_XS_MAC_HI = 0xA55Au;                         /* the registers exist in the AGA fabric only */
    s_present = (AMI_XS_MAC_HI & 0xFFFFu) == 0xA55Au;
    if (!s_present) { netif_pano_tap = 0; return 0; }
    if (!g2_factory_mac(f)) { f[0] = 0x00; f[1] = 0x1C; f[2] = 0x02; f[3] = 0x00; f[4] = 0x00; f[5] = 0x01; }
    memcpy(s_mac, f, 6); s_mac[0] = (uint8_t)((s_mac[0] | 0x02u) & ~0x01u);
    AMI_XS_MAC_HI = ((uint32_t)s_mac[0] << 8) | s_mac[1];
    AMI_XS_MAC_LO = ((uint32_t)s_mac[2] << 24) | ((uint32_t)s_mac[3] << 16) | ((uint32_t)s_mac[4] << 8) | s_mac[5];
    s_qh = s_qt = s_qn = 0u; s_last_seq = 0u;
    mb_w16(ETH_RX_QUEUE_TAIL, 0u); mb_w16(ETH_RX_QUEUE_HEAD, 0u); mb_w16(ETH_TX_COMPLETE_SEQ, 0u);
    mb_w32(ETH_HPS_SIGNATURE, ETH_HPS_MAGIC); mb_w32(ETH_HPS_HEARTBEAT, ++s_hb);
    status_set(0xFFFFu, 0u);
    ETHMAC_CTRL |= ETHMAC_CTRL_PROMISC | ETHMAC_CTRL_ALLMULTI;
    netif_pano_tap = xs_tap;
    return 1;
}

void xsurf_poll(void)
{
    uint32_t flags, seq;
    if (!s_present) return;
    flags = mb_r32(ETH_CTRL_FLAGS);
    if ((s_polls++ & 0x3Fu) == 0u) {                 /* the daemon's heartbeat rate: one poll in 64 */
        mb_w32(ETH_HPS_HEARTBEAT, ++s_hb); mb_w32(ETH_HPS_SIGNATURE, ETH_HPS_MAGIC);
        status_set(0u, 0u);
        mb_get(ETH_CTRL_MAC, s_mac, 6u);             /* the address the driver configured (PAR) */
    }
    seq = mb_r16(ETH_TX_REQUEST_SEQ);
    if ((flags & ETH_FLAG_TX_REQ) || (seq != 0u && seq != s_last_seq)) {
        if (seq != 0u && seq != s_last_seq) {
            uint32_t len = mb_r16(ETH_TX_REQUEST_LEN), t;
            int ok = 0;
            status_set(ETH_STATUS_TX_OK | ETH_STATUS_TX_ERR, 0u);
            if (len != 0u && len <= ETH_PKT_MAX) {
                mb_get(ETH_TX_BUFFER, s_txb, len);
                for (t = 0; t < 1000000u && ethmac_send(s_txb, len) == -1; t++) { }
                ok = t < 1000000u;
            }
            if (ok) s_tx++; else s_tx_err++;
            mb_w16(ETH_TX_COMPLETE_SEQ, seq); s_last_seq = (uint16_t)seq;
            status_set(ETH_STATUS_TX_OK | ETH_STATUS_TX_ERR, ok ? ETH_STATUS_TX_OK : ETH_STATUS_TX_ERR);
        } else if (seq == s_last_seq) mb_w16(ETH_TX_COMPLETE_SEQ, seq);
        mb_w32(ETH_CTRL_FLAGS, mb_r32(ETH_CTRL_FLAGS) & ~ETH_FLAG_TX_REQ);
    }
    while (s_qn) {                                    /* queued frames into the model's receive queue */
        uint32_t head = mb_r16(ETH_RX_QUEUE_HEAD) & 0xFFu, tail = mb_r16(ETH_RX_QUEUE_TAIL) & 0xFFu;
        uint32_t next = (tail + 1u) % ETH_RX_SLOTS;
        if (next == head) break;
        mb_put(ETH_RX_QUEUE_DATA + tail * ETH_PKT_MAX, s_q[s_qh].d, s_q[s_qh].len);
        mb_w16(ETH_RX_QUEUE_LEN + tail * 2u, s_q[s_qh].len);
        mb_w16(ETH_RX_QUEUE_TAIL, next);
        mb_w32(ETH_CTRL_FLAGS, mb_r32(ETH_CTRL_FLAGS) | ETH_FLAG_RX_AVAIL);
        s_qh = (s_qh + 1u) % HOSTQ; s_qn--; s_rx++;
    }
}

int xsurf_info(struct xsurf_stats *st)
{
    st->present = s_present; memcpy(st->mac, s_mac, 6);
    st->tx = s_tx; st->tx_err = s_tx_err; st->rx = s_rx; st->rx_drop = s_rx_drop; st->heartbeat = s_hb;
    st->flags = s_present ? mb_r32(ETH_CTRL_FLAGS) : 0u; st->status = s_present ? mb_r16(ETH_CTRL_STATUS) : 0u;
    return (int)s_present;
}
