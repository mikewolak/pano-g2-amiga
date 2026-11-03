/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netbulk.h -- the bulk receive fast path: UDP datagrams to port 2324 taken straight from the MAC's receive ring,
 * no lwIP.  See netbulk.c for the datagram format.  Used by NETSH's `brecv' and by the soc-netbulk gate (bulksim.c). */
#ifndef NETBULK_H
#define NETBULK_H
#include <stdint.h>

#define NETBULK_PORT   2324u
#define NETBULK_CHUNK  1408u                           /* data bytes a datagram; a multiple of 4 */
#define NETBULK_HDR    10u                             /* 'P' 'B' id16 off32 pad16 */
#ifndef NETBULK_MAXLEN
#define NETBULK_MAXLEN 0x01000000u
#endif

/* arm a transfer of len bytes to dst (word-aligned), datagrams tagged id, for the IPv4 address ip[4] (network order,
 * as lwIP holds it).  Returns the chunk size, or -1 for a bad length or alignment. */
int  netbulk_begin(uint8_t *dst, uint32_t len, uint16_t id, const uint8_t *ip);
void netbulk_end(void);
/* one received frame, IN PLACE in the MAC's ring as two segments (a: na bytes up to the ring's end; b: the rest from
 * its start; fw/ethmac.c ethmac_peek_seg).  1 = a bulk datagram, taken (stored, a duplicate, stale or malformed: either
 * way the ring slot is free); 0 = not ours, give it to the stack. */
int  netbulk_frame(const volatile uint8_t *a, unsigned na, const volatile uint8_t *b, unsigned len);
void netbulk_status(uint32_t *got, uint32_t *chunks, uint32_t *dups);
/* the next run of missing chunks at or after *from: its first chunk and count; 0 when nothing is missing */
int  netbulk_missing(uint32_t *from, uint32_t *count);
/* stored datagrams and the cycles netbulk_frame spent on them (since begin) */
void netbulk_perf(uint32_t *frames, uint32_t *cycles);
/* B3: full in-ring chunks by the copy engine (rtl/lpddr_dma.v's MAC-page window).  The caller must then read dst
 * UNCACHED, and must not let the video head scan meanwhile unless that combination has been proven (fw/dma.c). */
void netbulk_use_dma(int on);
/* the image's CRC-32 taken through the engine and the MAC's transmit buffer (fast); 0 = not available, read uncached */
int  netbulk_crc(const uint8_t *img, uint32_t len, uint32_t *crc_out);
void netbulk_dma_stat(uint32_t *by_engine, uint32_t *engine_errors);
/* the engine's STATUS at the last refusal: REASON in [7:4] (rtl/lpddr_dma.v); 0xFFFFFFFF = no answer in time */
uint32_t netbulk_dma_last(void);
/* chunks the engine took in TWO transfers because the frame wrapped the ring's end */
uint32_t netbulk_dma_split(void);
#endif
