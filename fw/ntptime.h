/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ntptime.h -- the time of day in the shell: one SNTP exchange once the network is up.
 *
 * No RTC on this board: the time is a Unix second count pinned to a value of get_millis(), and it lives exactly as long as
 * the shell does.  Polled, lwIP raw UDP, no DNS (LWIP_DNS is 0): the servers are addresses -- one given by hand, then the
 * DHCP gateway (a router usually answers NTP), then three public anycast/stratum-1 addresses, which
 * need whatever gave us DHCP to forward and NAT for us.  The first valid answer wins and says who it was. */
#ifndef NTPTIME_H
#define NTPTIME_H
#include <stdint.h>

void     ntptime_start(uint32_t gw_host_order, uint32_t override_host_order);   /* begin (or begin again); 0 = none */
void     ntptime_poll(void);                       /* from the network poll: time-outs and the walk down the list */
int      ntptime_known(void);
uint32_t ntptime_unix(void);                       /* seconds since 1970 UTC, 0 if unknown */
uint32_t ntptime_server(void);                     /* host order; who answered */
unsigned ntptime_rtt_ms(void);
unsigned ntptime_tries(void);
int      ntptime_busy(void);
extern int ntptime_tz_min;                         /* minutes east of UTC for what `date' and the stick's timestamps show */
void     ntptime_civil(uint32_t unix_s, unsigned *Y, unsigned *M, unsigned *D, unsigned *h, unsigned *m, unsigned *s);
#endif
