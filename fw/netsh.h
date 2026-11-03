/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netsh.h -- the network half of NETSH (fw/bootsh.c built with -DNET): lwIP, one TCP console session on port 2323,
 * and the discovery beacon.  bootsh treats the session as a third console beside the JTAG link and the HID keyboard:
 * its output is one more sink of say(), its input one more source of key_get().  One parser, one command table. */
#ifndef NETSH_H
#define NETSH_H
#include <stdint.h>

#define NETSH_PORT        2323u
#define NETSH_BEACON_PORT 48951u
#define NETSH_BULK_PORT   2324u

typedef struct {
    int      phy;                  /* MDIO address of the PHY, -1 if none answered */
    int      link;                 /* MDIO BMSR link bit */
    int      bound;                /* an IPv4 address is set (DHCP or static) */
    int      dhcp;                 /* 1 while the DHCP client is running */
    uint32_t ip, mask, gw;         /* network byte order as lwIP holds them */
    int      session;              /* a TCP console is connected */
    uint32_t tx_dropped;           /* console bytes dropped because the session's ring was full */
    uint32_t sessions;             /* connections accepted since netsh_init */
} netsh_info_t;

void     netsh_init(void);                     /* PHY, MAC, lwIP (once), DHCP, listener, beacon */
/* service the MAC, lwIP's timers, the session's output, the beacon.  Returns 1 exactly once, the first time an address
 * becomes usable, so the shell can announce it. */
int      netsh_poll(void);
uint32_t netsh_gw_host(void);              /* the gateway, host byte order, 0 if none */
int      netsh_session(void);
/* THE SESSION GENERATION: changes at every connect and every disconnect.  A command typed in one session must never
 * run in, or write into, another -- a host that gave up and reconnected once received a stale `send' into its stream
 *.  bootsh records it with each line from the network and checks it before touching the session. */
unsigned netsh_gen(void);
/* One ICMP echo to ip_host (host byte order) with `size' data bytes, waited for up to timeout_ms or until stop() returns
 * nonzero.  0 = reply (rtt_ms, ttl, len filled), 1 = timed out, 2 = stopped, negative = could not send. */
int netsh_ping(uint32_t ip_host, unsigned seq, unsigned size, unsigned timeout_ms,
               unsigned *rtt_ms, unsigned *ttl, unsigned *len, int (*stop)(void));
int      netsh_getc(void);                     /* -1 when nothing is waiting */
void     netsh_putc(char c);                   /* console output: best-effort, never blocks, counts drops */
void     netsh_puts(const char *s);
/* bulk data (send): blocks, polling the stack, until every byte is queued.  0 ok, -1 the session went away. */
int      netsh_write_all(const void *buf, unsigned n);
/* up to n bytes of session input; waits at most timeout_ms for the first byte.  Returns the count, 0 on timeout,
 * -1 if the session went away. */
int      netsh_read(void *buf, unsigned n, unsigned timeout_ms);
/* the same, folding the bytes into *crc (running CRC-32, fw/crc32.h) as they leave the ring -- from cached memory */
int      netsh_read_crc(void *buf, unsigned n, unsigned timeout_ms, uint32_t *crc);
/* receive exactly n bytes of session payload straight into dst (no session ring), CRC folded into *crc from the
 * received frame.  Returns n, fewer if the sender went idle for idle_ms, or -1 if the session went away. */
int      netsh_sink(uint8_t *dst, uint32_t n, uint32_t *crc, unsigned idle_ms);
void     netsh_flush(void);                    /* push queued output now (before a long command) */
void     netsh_drain(unsigned ms);             /* output sent and acknowledged, or ms spent trying */
void     netsh_info(netsh_info_t *i);
void     netsh_dhcp(void);
void     netsh_static(uint32_t ip, uint32_t mask, uint32_t gw);   /* host byte order, a.b.c.d = 0xaabbccdd */
/* THE BULK FAST PATH: UDP 2324 datagrams {'P','B',id16,off32 LE,pad16,data} go straight to dst in the netif, bypassing lwIP.
 * begin returns the chunk size (or -1); status and missing drive `brecv' (fw/bootsh.c). */
int      netsh_bulk_begin(uint8_t *dst, uint32_t len, uint16_t id);
void     netsh_bulk_end(void);
void     netsh_bulk_status(uint32_t *got, uint32_t *chunks, uint32_t *dups);
int      netsh_bulk_missing(uint32_t *from, uint32_t *count);
void     netsh_bulk_perf(uint32_t *frames, uint32_t *cycles);
void     netsh_bulk_dma(int on);                 /* full chunks by the copy engine (B3): read dst uncached after */
void     netsh_bulk_dma_stat(uint32_t *n, uint32_t *err);
int      netsh_bulk_crc(const uint8_t *img, uint32_t len, uint32_t *crc);   /* 1 = taken through the engine */
void     netsh_poll_rx(void);                  /* the MAC ring only, no lwIP timers or output */
void     netsh_down(void);                     /* close the session, stop the MAC: before an exec */
void     netsh_resume(void);                   /* after netsh_down: the MAC enabled again, the lwIP state (address, lease) kept */
/* cycles since the last call: in netsh_poll (the MAC ring, lwIP, the session ring), waiting in netsh_read for data
 * (includes the polls that brought it), and copying out of the session ring; all three are zeroed */
void     netsh_perf(uint32_t *poll, uint32_t *wait, uint32_t *copy);
#endif
