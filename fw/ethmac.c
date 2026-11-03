/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ethmac.c -- the driver for rtl/soc_ethlite.v.  See ethmac.h for the memory map.
 *
 * Everything is word access to uncached single-cycle memory.  The RX side walks records: a header word ([10:0] length),
 * then ceil(len/4) payload words, wrapping at the end of the ring (2,560 bytes, or 16,384 on the G2 large-ring MAC).  RX_RD is written back after each record,
 * which is what frees its space -- the hardware never writes a word whose successor is RX_RD, so it cannot overrun us.
 */
#include "ethmac.h"

/* THE RING IN USE, taken from the MAC at ethmac_init: its base from RING_BASE (register 11; 0 on MACs before the large
 * ring, which have the 2,560-byte ring at 0x8000_A000) and its size from INFO[15:0].  The large ring (16 KB at
 * 0x8200_0000, rtl/g2/g2_gmac.v) is requested with CTRL[8]; a MAC without it ignores the bit and keeps its own ring. */
static volatile uint32_t *s_ring = ETHMAC_RING;
static unsigned s_ring_words = ETHMAC_RING_BYTES / 4u;
#define RING_WORDS s_ring_words
#undef  ETHMAC_RING
#define ETHMAC_RING s_ring

static unsigned s_rd;                       /* our read index, in words */
static unsigned s_even;                     /* INFO[31]: the MAC starts every record on an even word (B3 and later) */

void ethmac_init(const uint8_t mac[6], uint32_t flags)
{
    ETHMAC_CTRL = 0;                        /* disabled: the ring is empty wherever RX_RD says */
    ETHMAC_MAC_LO = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    ETHMAC_MAC_HI = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8);
    ETHMAC_CTRL = ETHMAC_CTRL_RX_BIG;       /* RX off: the MAC takes the ring mode now */
    {
        uint32_t base = ETHMAC_RING_BASE, info = ETHMAC_INFO;
        unsigned bytes = info & 0xFFFFu;
        if (base && bytes >= 2560u && !(bytes & 7u)) { s_ring = (volatile uint32_t *)(uintptr_t)base; s_ring_words = bytes / 4u; }
        else { s_ring = (volatile uint32_t *)0x8000A000u; s_ring_words = ETHMAC_RING_BYTES / 4u; }
        s_even = info >> 31;
    }
    s_rd = 0;
    ETHMAC_RX_RD = 0;
    ETHMAC_CTRL = ETHMAC_CTRL_RX_BIG | ETHMAC_CTRL_RX_EN | ETHMAC_CTRL_TX_EN | (flags & (ETHMAC_CTRL_PROMISC | ETHMAC_CTRL_ALLMULTI));
}

void ethmac_stop(void)
{
    unsigned n = 0;
    while ((ETHMAC_STATUS & ETHMAC_S_TX_BUSY) && ++n < 1000000u) ;   /* let a frame in flight finish */
    ETHMAC_CTRL = 0;
}

int ethmac_tx_busy(void) { return (ETHMAC_STATUS & ETHMAC_S_TX_BUSY) != 0; }

int ethmac_send(const void *frame, unsigned len)
{
    const uint8_t *p = (const uint8_t *)frame;
    unsigned w, n;
    if (len > ETHMAC_TX_MAX) return -2;
    if (ETHMAC_STATUS & ETHMAC_S_TX_BUSY) return -1;
    n = (len + 3u) / 4u;
    if (!((uintptr_t)p & 3u)) {             /* aligned: whole words (the tail word may carry bytes past len: harmless) */
        const uint32_t *pw = (const uint32_t *)p;
        for (w = 0; w < n; w++) ETHMAC_TXBUF[w] = pw[w];
    } else
    for (w = 0; w < n; w++, p += 4)         /* bytes, not a word cast: the caller's buffer need not be aligned */
        ETHMAC_TXBUF[w] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    ETHMAC_TX_LEN = 0x80000000u | len;
    return 0;
}

int ethmac_recv(void *out, unsigned max)
{
    uint8_t *o = (uint8_t *)out;
    unsigned wr = (ETHMAC_RX_WR >> 2), idx, len, k;
    uint32_t w;
    if (wr == s_rd) return 0;
    len = ETHMAC_RING[s_rd] & 0x7FFu;
    idx = s_rd + 1u; if (idx == RING_WORDS) idx = 0;
    for (k = 0; k < len; k += 4) {
        w = ETHMAC_RING[idx];
        if (k + 0 < max) o[k + 0] = (uint8_t)w;
        if (k + 1 < max && k + 1 < len) o[k + 1] = (uint8_t)(w >> 8);
        if (k + 2 < max && k + 2 < len) o[k + 2] = (uint8_t)(w >> 16);
        if (k + 3 < max && k + 3 < len) o[k + 3] = (uint8_t)(w >> 24);
        if (++idx == RING_WORDS) idx = 0;
    }
    if (s_even) idx += idx & 1u;                    /* the pad word the MAC skipped (rtl/soc_ethlite.v) */
    if (idx >= RING_WORDS) idx -= RING_WORDS;
    s_rd = idx;
    ETHMAC_RX_RD = s_rd * 4u;
    return (int)len;
}

int ethmac_peek_len(void)
{
    if ((ETHMAC_RX_WR >> 2) == s_rd) return 0;
    return (int)(ETHMAC_RING[s_rd] & 0x7FFu);
}

/* bytes [off, off+n) of the frame at the read index; the payload starts one word after the header and may wrap */
void ethmac_copy(void *dst, unsigned off, unsigned n)
{
    uint8_t *o = (uint8_t *)dst;
    /* WORD PATH: a pbuf payload and the ring are both word-aligned, and a store to LPDDR costs ~31 cycles whatever its
     * width -- so copying words is four times fewer of them than copying bytes (the byte loop cost ~0.9 ms a frame). */
    if (!(off & 3u) && !((uintptr_t)dst & 3u)) {
        uint32_t *ow = (uint32_t *)dst;
        unsigned idx = s_rd + 1u + (off >> 2);
        if (idx >= RING_WORDS) idx -= RING_WORDS;
        while (n >= 4u) {
            *ow++ = ETHMAC_RING[idx];
            if (++idx == RING_WORDS) idx = 0;
            n -= 4u; off += 4u;
        }
        o = (uint8_t *)ow;
        if (!n) return;
    }
    unsigned idx = s_rd + 1u + (off >> 2), sh = (off & 3u) * 8u;
    uint32_t w;
    if (idx >= RING_WORDS) idx -= RING_WORDS;
    w = ETHMAC_RING[idx] >> sh;
    while (n--) {
        *o++ = (uint8_t)w;
        w >>= 8; sh += 8;
        if (sh == 32 && n) { sh = 0; if (++idx == RING_WORDS) idx = 0; w = ETHMAC_RING[idx]; }
    }
}

const volatile uint8_t *ethmac_peek_ptr(void)
{
    unsigned len, words;
    if ((ETHMAC_RX_WR >> 2) == s_rd) return 0;
    len = ETHMAC_RING[s_rd] & 0x7FFu;
    words = (len + 3u) / 4u;
    if (s_rd + 1u + words > RING_WORDS) return 0;             /* it wraps the ring end: the caller copies */
    /* In place only from the fast-RAM ring at 0x8000_A000.  The large ring at 0x8200_0000 is read only to the CPU
     * (rtl/g2/pano_soc_g2.v: a write is acknowledged and ignored), and lwIP converts the TCP header's byte order in
     * place in the buffer it is given: lent from that ring, the conversion is lost and every SYN is answered with a
     * reset carrying byte-swapped ports.  A frame in the large ring takes the caller's copy path. */
    if (s_ring != (volatile uint32_t *)0x8000A000u) return 0;
    return (const volatile uint8_t *)&ETHMAC_RING[s_rd + 1u];
}

/* the frame at the read index as TWO ring segments: *a holds the first *na bytes (up to the ring's end), *b (the
 * ring's start) the rest.  Returns the length, 0 if nothing is waiting.  For a caller that wants the frame in place
 * even when it wraps -- the bulk path hands the pieces to the copy engine. */
int ethmac_peek_seg(const volatile uint8_t **a, unsigned *na, const volatile uint8_t **b)
{
    unsigned len, idx, room;
    if ((ETHMAC_RX_WR >> 2) == s_rd) return 0;
    len = ETHMAC_RING[s_rd] & 0x7FFu;
    idx = s_rd + 1u; if (idx == RING_WORDS) idx = 0;
    room = (RING_WORDS - idx) * 4u;
    *a = (const volatile uint8_t *)&ETHMAC_RING[idx];
    *na = len < room ? len : room;
    *b = (const volatile uint8_t *)&ETHMAC_RING[0];
    return (int)len;
}

void ethmac_consume(void)
{
    unsigned len;
    if ((ETHMAC_RX_WR >> 2) == s_rd) return;
    len = ETHMAC_RING[s_rd] & 0x7FFu;
    s_rd += 1u + (len + 3u) / 4u;
    if (s_even) s_rd += s_rd & 1u;                  /* the pad word the MAC skipped (rtl/soc_ethlite.v) */
    if (s_rd >= RING_WORDS) s_rd -= RING_WORDS;
    ETHMAC_RX_RD = s_rd * 4u;
}

uint32_t ethmac_stat(unsigned which)
{
    ETHMAC_STAT_SEL = which;
    return ETHMAC_STAT;
}
