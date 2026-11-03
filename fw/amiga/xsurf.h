/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/xsurf.h -- the firmware side of the X-Surf 100 board (fw/amiga/xsurf.c) */
#ifndef XSURF_H
#define XSURF_H
#include <stdint.h>

struct xsurf_stats {
    unsigned present;
    uint8_t  mac[6];
    uint32_t tx, tx_err, rx, rx_drop, heartbeat, flags, status;
};

/* before the core's reset is released: the station address, the mailbox's host fields, the MAC promiscuous and the
 * receive tap; 0 when the fabric has no X-Surf board (the A500 fabric) */
int  xsurf_start(void);
/* from the main loop: heartbeat, transmit requests, queued frames into the model's receive queue */
void xsurf_poll(void);
int  xsurf_info(struct xsurf_stats *st);
#endif
