/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/tnp.h -- Pano Logic's TNP framing and the endpoint-0 register messages that ride on it.
 *
 * Recovered by static reading of Pano Logic's own host tool; nothing of theirs is linked, run or shipped.
 *
 *   TNP header, 8 bytes, a big-endian bit stream:   ver(8)=1  flags(8)  seq(12)  ack(12)  security(24)
 *     flags: 0x80 SYN   0x40 ACK/data   0x20 NAK   0x10 RST   0x08 connectionless datagram
 *   then endpoint messages:  u16 BE (endpoint & 0x1f) << 11 | (length & 0x7ff), then `length' bytes
 *   an endpoint-0 message:   b0 = opcode << 4 | (nwords - 1);  b1..b3 = 24-bit register address BE;  b4..b5 = transaction
 *                            id BE;  then nwords * 4 bytes, each word BE.  A read REQUEST carries no words (6 bytes).
 *     opcodes: 0 CfgRead 1 CfgWrite 2 CfgReadReturn 3 Read 4 Write 5 ReadReturn 6 Interrupt 7 Error */
#ifndef TNP_H
#define TNP_H
#include "plat.h"

#define TNP_PORT        8321u
#define TNP_DISC_SPORT  8320u
#define TNP_F_SYN  0x80u
#define TNP_F_ACK  0x40u
#define TNP_F_NAK  0x20u
#define TNP_F_RST  0x10u
#define TNP_F_DGRAM 0x08u

enum { EP0_CFGREAD = 0, EP0_CFGWRITE, EP0_CFGREADRET, EP0_READ, EP0_WRITE, EP0_READRET, EP0_INTERRUPT, EP0_ERROR };

typedef struct { unsigned flags, seq, ack; uint32_t security; } tnp_hdr_t;
void tnp_hdr_put(uint8_t *p, const tnp_hdr_t *h);
int  tnp_hdr_get(const uint8_t *p, size_t n, tnp_hdr_t *h);          /* 1 = a version-1 header */
/* append one endpoint message; returns the new length or 0 if it does not fit */
size_t tnp_ep_put(uint8_t *pkt, size_t len, size_t cap, unsigned ep, const uint8_t *msg, size_t mlen);
/* walk the endpoint messages of a payload: *off starts at 8; 1 = one message returned */
int  tnp_ep_next(const uint8_t *pkt, size_t n, size_t *off, unsigned *ep, const uint8_t **msg, size_t *mlen);

size_t ep0_build(uint8_t *m, unsigned op, uint32_t addr, unsigned tid, const uint32_t *words, unsigned nwords);
typedef struct { unsigned op, nwords, tid; uint32_t addr; uint32_t w[16]; } ep0_msg_t;
int  ep0_parse(const uint8_t *m, size_t n, ep0_msg_t *out);          /* 1 = well formed */
const char *ep0_opname(unsigned op);

/* ---- the connectionless ("out of band") path: one datagram, one answer ------------------------------------------------ */
typedef struct { sock_t s; unsigned seq, tid; int trace; } tnp_dgram_t;
int  tnp_dgram_open(tnp_dgram_t *d, uint32_t bind_ip, unsigned bind_port, int broadcast);
void tnp_dgram_close(tnp_dgram_t *d);
/* a register read by datagram: cfg = config space (CfgRead) or not (Read).  0 ok, -1 timeout, -2 error reply, -3 socket */
int  tnp_dgram_read(tnp_dgram_t *d, uint32_t ip, int cfg, uint32_t addr, uint32_t *val, unsigned timeout_ms, unsigned tries);
/* a register write by datagram (no answer exists at this layer: it is followed by nothing).  0 sent */
int  tnp_dgram_write(tnp_dgram_t *d, uint32_t ip, int cfg, uint32_t addr, uint32_t val);

typedef struct { uint32_t ip, mask, gw, rev, cfg0; uint8_t mac[6]; } pano_info_t;
/* DISCOVERY.  A Pano answers a connectionless register read of config 0 sent to a broadcast address, so nobody needs to
 * know its address in advance: probe once a second for `secs', then ask each responder who it is.  `dst' is a list of
 * broadcast addresses in host order (255.255.255.255 and each interface's own directed broadcast). */
typedef void (*pano_found_fn)(const pano_info_t *info, void *ctx);
int pano_discover(const uint32_t *dst, unsigned ndst, uint32_t bind_ip, unsigned secs, pano_found_fn cb, void *ctx);
int  pano_info_read(tnp_dgram_t *d, uint32_t ip, pano_info_t *o);
void hexdump(const char *tag, const uint8_t *p, size_t n);
#endif
