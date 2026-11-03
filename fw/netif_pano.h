/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netif_pano.h -- the lwIP netif for the Pano G1's MII MAC (rtl/soc_ethlite.v). */
#ifndef NETIF_PANO_H
#define NETIF_PANO_H
#include <stdint.h>
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"

#ifdef PANO_G2
extern uint8_t netif_pano_mac[6];              /* the factory MAC once netif_pano_up has read it */
#else
extern const uint8_t netif_pano_mac[6];
#endif

/* offered each received frame IN PLACE before lwIP, as two ring segments (a: na bytes up to the ring's end; b: the
 * rest from the ring's start); return 1 to take it (see netif_pano.c) */
typedef int (*netif_fastpath_fn)(const volatile uint8_t *a, unsigned na, const volatile uint8_t *b, unsigned len);
extern netif_fastpath_fn netif_pano_fastpath;
/* offered each received frame before netif_pano_fastpath, the same way; a second user beside the shell's bulk
 * receiver (the Amiga fabric's X-Surf bridge, fw/amiga/xsurf.c) */
extern netif_fastpath_fn netif_pano_tap;

/* PHY up, MAC up, netif added, made default and up.  lwip_init() must already have run (ONCE).  Returns the PHY's
 * MDIO address, or -1 if no PHY answered (the netif is still added: a missing PHY is not fatal to the shell). */
int      netif_pano_up(struct netif *netif);
void     netif_pano_down(struct netif *netif);
/* feed every received frame to lwIP; returns how many.  Call from the main loop. */
unsigned netif_pano_poll(struct netif *netif);
void     pbuf_take_ring(struct pbuf *p, unsigned len);
#endif
