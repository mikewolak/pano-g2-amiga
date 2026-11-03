/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/netbulk.c -- THE BULK FAST PATH.
 *
 * TCP carries the shell; bulk data bypasses lwIP.  A datagram to UDP port 2324 carries
 *     'P' 'B'  id[2]  offset[4]  pad[2]   (little-endian)   then NETBULK_CHUNK bytes (fewer in the last) at that offset
 * and is taken from the MAC's receive ring (fast RAM: single-cycle reads) before the stack ever sees the frame.  The pad
 * puts the data 52 bytes into the frame, and a frame starts on a word in the ring, so the data is read a word at a
 * time and stored a word at a time: one LPDDR store per four bytes, which is the whole cost.  A bit a chunk records
 * what has arrived; netbulk_missing() names what has not, and the host resends only that.
 *
 * THE COPY ENGINE.  With netbulk_use_dma(1), a full chunk that lies whole in the ring is not copied by
 * the CPU at all: rtl/lpddr_dma.v reads it straight out of the MAC's page (0x8000_A000, the window B3 added) in BURST
 * mode into dst + offset.  Both ends are 8-byte aligned by construction: the chunk size is a multiple of 8, and B3's
 * MAC starts every record on an even word (INFO[31]) so the data, 56 bytes into the record, is too.  Byte mode is
 * NOT used: a byte-mode transfer with a fast-RAM source hung the engine's watchdog in simulation --
 * that combination was never built for, and burst mode from the fast RAM is the engine's proven path.  ~5 us of engine against ~136 us of CPU word
 * stores: the difference between draining the ring at wire speed and dropping every burst a USB NIC sends.  The
 * engine writes LPDDR behind the data cache, so the caller must read the result UNCACHED (bootsh's CRC does).  The
 * last short chunk and a frame that wraps the ring end still go by the CPU.  An engine error falls back to the CPU and
 * is counted, never silent.
 *
 * The data is stored through whatever alias the caller passes.  NETSH passes the CACHED one: the cache is
 * write-through, so a line already cached is updated by the store and the final CRC pass reads it back at cache speed.
 *
 * Planted faults (the soc-netbulk-faults gate, rtl/sim rule 8):
 *   NETBULK_FAULT_1  the data is taken from the pad (offset 8, not 10): every word shifted -- the CRC must catch it
 *   NETBULK_FAULT_2  a duplicate is stored and counted as new -- the chunk count overruns, the missing list lies
 *   NETBULK_FAULT_3  the id is not checked: a stale transfer's datagram lands -- the CRC must catch it
 *   NETBULK_FAULT_4  the engine is given the datagram's header as the source (8 bytes early) -- the CRC must catch it
 */
#include <stdint.h>
#include "netbulk.h"
#ifndef PANO_G2
#include "dmatramp.h"
#endif
#include "ethmac.h"
#include "crc32.h"

static uint32_t s_map[(NETBULK_MAXLEN / NETBULK_CHUNK + 32u) / 32u];
static uint8_t *s_dst;
static uint32_t s_len, s_chunks, s_got, s_dup;
static uint16_t s_id;
static uint8_t  s_ip[4];
static int      s_on;
static uint32_t s_cyc, s_nfr;                 /* cycles spent in netbulk_frame on stored datagrams, and how many */
static uint32_t s_ndma, s_dma_err, s_nsplit;  /* chunks by the engine, its refusals, chunks it took in two pieces */
static int      s_dma;                        /* use the engine (netbulk_use_dma; cleared by its first refusal) */
static uint32_t s_dma_last;                   /* the engine's STATUS at the last refusal (0xFFFFFFFF: our timeout) */

static inline uint32_t cyc(void) { uint32_t c; __asm__ volatile ("rdcycle %0" : "=r"(c)); return c; }


/* the MAC's page as the copy engine names it (rtl/lpddr_dma.v f_fast): the ring is read uncached at 0x8000_A000 */
#define MAC_PAGE_LO 0x8000A000u
#define MAC_PAGE_HI 0x8000B000u

#ifdef PANO_G2
/* The G2 has no copy engine (rtl/lpddr_dma.v is the G1's): every chunk goes by the CPU. */
void netbulk_use_dma(int on) { (void)on; s_dma = 0; }
static int dma_bursts(uint32_t dst, uint32_t src, uint32_t n) { (void)dst; (void)src; (void)n; return -1; }
#else
void netbulk_use_dma(int on) { if (on) dmatramp_install(); s_dma = on; }

/* n bytes, both ends 8-byte aligned, n a multiple of 8.  0 done, -1 refused or timed out (the caller copies) */
static int dma_bursts(uint32_t dst, uint32_t src, uint32_t n)
{
    /* through the boot-RAM trampoline: NETSH lives in LPDDR, and an LPDDR access while the engine owns the port
     * kills the board (fw/dmatramp.h) */
    uint32_t st = dmatramp_run(src, dst, n >> 3);
    if (st == 0xFFFFFFFFu || (st & 4u)) { s_dma_last = st; return -1; }
    return 0;
}
#endif

#ifdef NETBULK_FAULT_1
#define DATA_AT 8u
#else
#define DATA_AT NETBULK_HDR
#endif

/* THE FRAME IS TWO RING SEGMENTS (a: na bytes to the ring's end; b: the rest from its start).  Byte i of the frame: */
static const volatile uint8_t *s_a, *s_b;
static unsigned s_na;
static inline uint8_t at(unsigned i) { return i < s_na ? s_a[i] : s_b[i - s_na]; }
static unsigned be16at(unsigned i) { return ((unsigned)at(i) << 8) | at(i + 1u); }
/* where byte i of the frame lies in the ring */
static inline const volatile uint8_t *ptr(unsigned i) { return i < s_na ? s_a + i : s_b + (i - s_na); }

/* n bytes from the ring (word-aligned source when possible) to d, stored a word at a time */
static void cpu_copy(uint8_t *d, const volatile uint8_t *src, unsigned n)
{
    unsigned k = 0;
    uint32_t *w = (uint32_t *)d;
    if (!((uintptr_t)src & 3u) && !((uintptr_t)d & 3u)) {
        const volatile uint32_t *sw = (const volatile uint32_t *)src;
        for (; k + 4u <= n; k += 4u) *w++ = *sw++;
    } else if (!((uintptr_t)d & 3u)) {
        for (; k + 4u <= n; k += 4u) *w++ = (uint32_t)src[k] | ((uint32_t)src[k + 1u] << 8) | ((uint32_t)src[k + 2u] << 16) | ((uint32_t)src[k + 3u] << 24);
    }
    for (; k < n; k++) d[k] = src[k];
}

int netbulk_begin(uint8_t *dst, uint32_t len, uint16_t id, const uint8_t *ip)
{
    uint32_t i;
    if (!len || len > NETBULK_MAXLEN || ((uintptr_t)dst & 3u)) return -1;
    s_dst = dst; s_len = len; s_id = id; s_got = 0; s_dup = 0; s_cyc = 0; s_nfr = 0; s_ndma = 0; s_dma_err = 0; s_nsplit = 0;
    for (i = 0; i < 4u; i++) s_ip[i] = ip[i];
    s_chunks = (len + NETBULK_CHUNK - 1u) / NETBULK_CHUNK;
    for (i = 0; i < sizeof s_map / sizeof s_map[0]; i++) s_map[i] = 0;
    s_on = 1;
    return (int)NETBULK_CHUNK;
}

void netbulk_end(void) { s_on = 0; }

int netbulk_frame(const volatile uint8_t *fa, unsigned na, const volatile uint8_t *fb, unsigned len)
{
    unsigned ihl, ip, ud, pl, ulen, dlen, idx, d0, n1;
    uint32_t off, want, t0 = cyc();
    s_a = fa; s_na = na; s_b = fb;
    if (!s_on || len < 14u + 20u + 8u + NETBULK_HDR) return 0;
    if (at(12) != 0x08 || at(13) != 0x00) return 0;                                    /* IPv4 */
    ip = 14u;
    ihl = (at(ip) & 15u) * 4u;
    if ((at(ip) >> 4) != 4u || ihl < 20u || at(ip + 9u) != 17u) return 0;               /* UDP */
    if (at(ip + 16u) != s_ip[0] || at(ip + 17u) != s_ip[1] || at(ip + 18u) != s_ip[2] || at(ip + 19u) != s_ip[3]) return 0;
    if (be16at(ip + 6u) & 0x3FFFu) return 0;                                             /* fragments: the stack's */
    ud = ip + ihl;
    if (ud + 8u > len || be16at(ud + 2u) != NETBULK_PORT) return 0;
    ulen = be16at(ud + 4u);
    if (ulen < 8u + NETBULK_HDR || ud + ulen > len) return 1;                          /* ours, malformed: dropped */
    pl = ud + 8u;
    if (at(pl) != 'P' || at(pl + 1u) != 'B') return 1;
#ifndef NETBULK_FAULT_3
    if (((unsigned)at(pl + 2u) | ((unsigned)at(pl + 3u) << 8)) != s_id) return 1;       /* a stale transfer's */
#endif
    off = (uint32_t)at(pl + 4u) | ((uint32_t)at(pl + 5u) << 8) | ((uint32_t)at(pl + 6u) << 16) | ((uint32_t)at(pl + 7u) << 24);
    dlen = ulen - 8u - NETBULK_HDR;
    if (off % NETBULK_CHUNK || off >= s_len) return 1;
    want = s_len - off; if (want > NETBULK_CHUNK) want = NETBULK_CHUNK;
    if (dlen != want) return 1;
    idx = off / NETBULK_CHUNK;
#ifndef NETBULK_FAULT_2
    if (s_map[idx >> 5] & (1u << (idx & 31u))) { s_dup++; return 1; }
#endif
    d0 = pl + DATA_AT;                                   /* the data's first byte in the frame */
    n1 = d0 >= na ? 0u : (na - d0 < dlen ? na - d0 : dlen);   /* how much of it lies before the ring's end */
    /* THE ENGINE: one burst-mode transfer per ring segment the data touches.  The ring's end is 8-byte aligned, so if
     * the data starts aligned the split is too; anything misaligned, short, or refused goes by the CPU instead. */
    if (s_dma && dlen == NETBULK_CHUNK && !((uintptr_t)(s_dst + off) & 7u) && !((uintptr_t)ptr(d0) & 7u)
        && !(n1 & 7u) && (uintptr_t)ptr(d0) >= MAC_PAGE_LO && (uintptr_t)ptr(d0) < MAC_PAGE_HI) {
#ifdef NETBULK_FAULT_4
        uint32_t skew = 8u;
#else
        uint32_t skew = 0u;
#endif
        int r = 0;
        if (n1) r = dma_bursts((uint32_t)(uintptr_t)(s_dst + off), (uint32_t)(uintptr_t)ptr(d0) - skew, n1);
        if (!r && dlen > n1) r = dma_bursts((uint32_t)(uintptr_t)(s_dst + off + n1), (uint32_t)(uintptr_t)ptr(d0 + n1) - skew, dlen - n1);
        if (!r) { s_ndma++; if (n1 && dlen > n1) s_nsplit++; goto mark; }
        s_dma_err++;                                     /* refused or timed out: the CPU copies it below, and every chunk
                                                          * after it too -- a pre-B3 bitstream refuses the MAC page (REASON 3)
                                                          * every time, and asking again per chunk would only cost */
        s_dma = 0;
    }
    if (n1) cpu_copy(s_dst + off, ptr(d0), n1);
    if (dlen > n1) cpu_copy(s_dst + off + n1, ptr(d0 + n1), dlen - n1);
mark:
    s_map[idx >> 5] |= 1u << (idx & 31u);
    s_got++;
    s_cyc += cyc() - t0; s_nfr++;
    return 1;
}

/* THE FINAL CRC, THROUGH THE ENGINE.  The engine wrote behind the data cache, so the image must be read
 * uncached -- and an uncached word load is a whole LPDDR round trip: 3.2 s for 4 MB on silicon, more than twice the
 * transfer itself.  So the engine brings the image back 1,536 bytes at a time into the MAC's TRANSMIT buffer (fast RAM,
 * single-cycle reads, ours while nothing is being sent) and the CRC is taken there.  Returns 1 and the CRC, or 0 if
 * the engine is not usable (the caller reads uncached instead).  The video head must still be paused. */
int netbulk_crc(const uint8_t *img, uint32_t len, uint32_t *crc_out)
{
#ifdef PANO_G2
    (void)img; (void)len; (void)crc_out;
    return 0;                                                 /* no engine: the caller reads the image itself */
#else
    const volatile uint32_t *tx = ETHMAC_TXBUF;
    uint32_t crc = 0xFFFFFFFFu, off = 0, whole = len & ~7u, t = 0;
    if (!s_ndma || s_dma_err || ((uintptr_t)img & 7u)) return 0;
    while (ethmac_tx_busy() && ++t < 2000000u) ;             /* a frame still leaving owns the buffer */
    while (off < whole) {
        uint32_t n = whole - off > ETHMAC_TX_MAX ? ETHMAC_TX_MAX : whole - off, st;
        st = dmatramp_run((uint32_t)(uintptr_t)(img + off), (uint32_t)(uintptr_t)tx, n >> 3);
        if (st == 0xFFFFFFFFu || (st & 4u)) { s_dma_last = st; s_dma_err++; return 0; }
        crc = crc32_run_words(crc, ETHMAC_TXBUF, n >> 2);        /* n is a multiple of 8: whole words */
        off += n;
    }
    if (off < len) {                                          /* the last 1..7 bytes: one uncached word or two */
        const volatile uint8_t *u = (const volatile uint8_t *)(((uintptr_t)img & 0x0FFFFFFFu) | 0x90000000u);
        uint8_t b[8]; uint32_t k;
        for (k = 0; off + k < len; k++) b[k] = u[off + k];
        crc = crc32_run(crc, b, k);
    }
    *crc_out = crc ^ 0xFFFFFFFFu;
    return 1;
#endif
}

void netbulk_status(uint32_t *got, uint32_t *chunks, uint32_t *dups) { *got = s_got; *chunks = s_chunks; *dups = s_dup; }

void netbulk_perf(uint32_t *frames, uint32_t *cycles) { *frames = s_nfr; *cycles = s_cyc; }
void netbulk_dma_stat(uint32_t *by_engine, uint32_t *engine_errors) { *by_engine = s_ndma; *engine_errors = s_dma_err; }
uint32_t netbulk_dma_last(void) { return s_dma_last; }
uint32_t netbulk_dma_split(void) { return s_nsplit; }

int netbulk_missing(uint32_t *from, uint32_t *count)
{
    uint32_t i = *from, n = 0;
    while (i < s_chunks && (s_map[i >> 5] & (1u << (i & 31u)))) i++;
    if (i >= s_chunks) return 0;
    while (i + n < s_chunks && !(s_map[(i + n) >> 5] & (1u << ((i + n) & 31u)))) n++;
    *from = i; *count = n;
    return 1;
}
