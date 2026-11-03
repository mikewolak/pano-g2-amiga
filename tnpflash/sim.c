/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/sim.c -- a TEST-ONLY stand-in for a stock Pano G1's TNP endpoint: enough of the protocol to prove the client
 * against something, on a host, before any real hardware.  It is NOT Pano's code and models only what the spec documents:
 * the connectionless discovery/info reads, the SYN/ACK/RST stream handshake, stop-and-wait data with cumulative ACK, and a
 * register file with the SPI-flash controller (0x5000/0x5001/0x5002, 0xFC) backed by a 1 MB array so `backup' has something
 * to read.  Loss can be injected with TNPSIM_DROP=N (drop 1 in N received data packets) to exercise the retransmit path.
 * TNPSIM_G2B=1 models a G2 revision B instead: config registers 0x0801_0000 / 0x0801_0014 (rev 2049.20), 16 MB of
 * flash, and the M25P128's 256 KB erase (the whole sector holding the latched address), each erase printed.
 * TNPSIM_G2C=1 models a G2 revision C: config registers 0x0801_0000 / 0x0881_0014 (series 2, cfg1 bits 25:23 = 1),
 * 8 MB of flash, and the M25P64's 64 KB erase, each erase printed.
 * TNPSIM_DUMP=FILE writes the whole flash array to FILE every time 0xFC returns to 0 (the end of a write session). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tnp.h"
#include "stream.h"

static uint8_t flash[0x1000000];
static uint32_t flash_size = 0x100000u, erase_bytes = 0x10000u;
static const char *dump_path;
static uint32_t reg_2000[16];
static uint32_t spi_addr_reg, fc_reg;
static uint32_t cfg[64];
static unsigned up, rxseq, txseq, drop, rxcount;

static uint32_t cfg_read(uint32_t a) { return a < 64u ? cfg[a] : 0u; }

static uint32_t sim_reg_read(uint32_t a)
{
    if (a == 0x5000u) { uint32_t off = (spi_addr_reg & 0x7FFFFFFFu) * 4u; return off + 4u <= flash_size ? ((uint32_t)flash[off]<<24)|((uint32_t)flash[off+1]<<16)|((uint32_t)flash[off+2]<<8)|flash[off+3] : 0xFFFFFFFFu; }
    if (a >= 0x2000u && a < 0x2010u) return reg_2000[a - 0x2000u];
    if (a == 0xFCu) return fc_reg;
    return 0u;
}
static void sim_reg_write(uint32_t a, uint32_t v)
{
    if (a == 0x5001u) spi_addr_reg = v;
    else if (a == 0x5000u) { uint32_t off = (spi_addr_reg & 0x7FFFFFFFu) * 4u; if (off + 4u <= flash_size && fc_reg) { flash[off]=(uint8_t)(v>>24); flash[off+1]=(uint8_t)(v>>16); flash[off+2]=(uint8_t)(v>>8); flash[off+3]=(uint8_t)v; } }
    else if (a == 0x5002u) {                                                    /* erase the sector holding the address */
        uint32_t off = ((spi_addr_reg & 0x7FFFFFFFu) * 4u) & ~(erase_bytes - 1u), i;
        if (fc_reg && off < flash_size) { for (i = 0; i < erase_bytes; i++) flash[off+i] = 0xFF; fprintf(stderr, "sim: erase 0x%06lX-0x%06lX\n", (unsigned long)off, (unsigned long)(off + erase_bytes - 1u)); }
    }
    else if (a == 0xFCu) {
        if (fc_reg && !v && dump_path) { FILE *f = fopen(dump_path, "wb"); if (f) { fwrite(flash, 1, flash_size, f); fclose(f); fprintf(stderr, "sim: flash dumped to %s\n", dump_path); } }
        fc_reg = v;
    }
}

/* build one endpoint-0 reply for a request message, append to p at len, return new len (0 if no reply is due) */
static size_t handle_ep0(uint8_t *p, size_t len, size_t cap, const uint8_t *m, size_t ml)
{
    ep0_msg_t e; uint8_t rm[16]; size_t rl; uint32_t v;
    if (!ep0_parse(m, ml, &e)) return len;
    if (e.op == EP0_CFGREAD)  { v = cfg_read(e.addr); rl = ep0_build(rm, EP0_CFGREADRET, e.addr, e.tid, &v, 1u); return tnp_ep_put(p, len, cap, 0u, rm, rl); }
    if (e.op == EP0_READ)     { v = sim_reg_read(e.addr); rl = ep0_build(rm, EP0_READRET,    e.addr, e.tid, &v, 1u); return tnp_ep_put(p, len, cap, 0u, rm, rl); }
    if (e.op == EP0_CFGWRITE) { if (e.addr < 64u) cfg[e.addr] = e.w[0]; return len; }
    if (e.op == EP0_WRITE)    { sim_reg_write(e.addr, e.w[0]); return len; }
    return len;
}

int main(void)
{
    sock_t s; struct sockaddr_in a; const char *dr;
    unsigned mac0 = 0x0000001Cu, mac1 = 0x02637162u;                             /* 00:1c:02:63:71:62, the sample device */
    plat_init();
    memset(flash, 0xFF, sizeof flash);
    { unsigned i; for (i = 0; i < sizeof flash; i++) flash[i] = (uint8_t)(0x40 ^ (i * 7u + (i >> 8))); }   /* a recognisable pattern */
    flash[0] = 0xFF; flash[1] = 0xFE; flash[2] = 0xAA; flash[3] = 0x99;          /* something at the front to eyeball */
    cfg[0x00] = 0x00050000u;                                                     /* G1 cfg reg 0 (RevC per the sample) */
    cfg[0x01] = 0x0004000Eu;                                                     /* rev 4.14 */
    cfg[0x10] = mac0; cfg[0x11] = mac1;
    cfg[0x12] = 0xC0A87BA6u; cfg[0x13] = 0xFFFFFF00u; cfg[0x16] = 0xC0A87B01u;    /* 192.168.123.166/24, gateway .1 */
    { const char *g = getenv("TNPSIM_G2B"); if (g && *g == '1') { flash_size = 0x1000000u; erase_bytes = 0x40000u; cfg[0x00] = 0x08010000u; cfg[0x01] = 0x08010014u; fprintf(stderr, "sim: G2 revision B, 16 MB, 256 KB erase\n"); } }
    { const char *g = getenv("TNPSIM_G2C"); if (g && *g == '1') { flash_size = 0x800000u; erase_bytes = 0x10000u; cfg[0x00] = 0x08010000u; cfg[0x01] = 0x08810014u; fprintf(stderr, "sim: G2 revision C, 8 MB, 64 KB erase\n"); } }
    dump_path = getenv("TNPSIM_DUMP");
    { const char *im = getenv("TNPSIM_IMAGE"); if (im) { FILE *f = fopen(im, "rb"); if (f) { size_t n = fread(flash, 1, flash_size, f); fclose(f); fprintf(stderr, "sim: flash preloaded from %s (%lu bytes)\n", im, (unsigned long)n); } } }
    dr = getenv("TNPSIM_DROP"); drop = dr ? (unsigned)atoi(dr) : 0u;
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(TNP_PORT);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { fprintf(stderr, "sim: cannot bind %u (a real Pano or another sim already there?)\n", TNP_PORT); return 1; }
    fprintf(stderr, "sim: a stock-G1 stand-in listening on UDP %u%s\n", TNP_PORT, drop ? " (dropping 1 in N data packets)" : "");
    for (;;) {
        uint8_t r[2048], p[8 + TNP_MTU]; struct sockaddr_in from; socklen_plat fl = sizeof from; int n; tnp_hdr_t h, oh; size_t off, plen; unsigned ep; const uint8_t *m; size_t ml;
        n = recvfrom(s, (char *)r, sizeof r, 0, (struct sockaddr *)&from, &fl);
        if (n < 8 || !tnp_hdr_get(r, (size_t)n, &h)) continue;
        oh.security = 0;
        if (h.flags & TNP_F_DGRAM) {                                             /* connectionless: reply from the same socket */
            off = 8; plen = 8; oh.flags = TNP_F_DGRAM; oh.seq = h.seq; oh.ack = 0; tnp_hdr_put(p, &oh);
            while (tnp_ep_next(r, (size_t)n, &off, &ep, &m, &ml)) if (ep == 0u) plen = handle_ep0(p, plen, sizeof p, m, ml);
            if (plen > 8u) sendto(s, (const char *)p, (int)plen, 0, (struct sockaddr *)&from, fl);
            continue;
        }
        if (h.flags & TNP_F_RST) { up = 0; oh.flags = TNP_F_RST; oh.seq = txseq; oh.ack = rxseq; tnp_hdr_put(p, &oh); sendto(s, (const char *)p, 8, 0, (struct sockaddr *)&from, fl); continue; }
        if (h.flags & TNP_F_SYN) { up = 1; rxseq = 0; txseq = 0; oh.flags = TNP_F_SYN | TNP_F_ACK; oh.seq = txseq; oh.ack = h.seq; tnp_hdr_put(p, &oh); sendto(s, (const char *)p, 8, 0, (struct sockaddr *)&from, fl); continue; }
        if (!up) continue;
        if (n > 8 && (h.flags & TNP_F_ACK)) {                                    /* a data packet */
            if (drop && (++rxcount % drop) == 0u) continue;                      /* injected loss: no ACK, force a retransmit */
            if (h.seq == ((rxseq + 1u) & 0xFFFu)) {
                rxseq = (rxseq + 1u) & 0xFFFu;
                off = 8; plen = 8; oh.flags = TNP_F_ACK; oh.ack = rxseq;
                while (tnp_ep_next(r, (size_t)n, &off, &ep, &m, &ml)) if (ep == 0u) plen = handle_ep0(p, plen, sizeof p, m, ml);
                if (plen > 8u) { oh.seq = (txseq + 1u) & 0xFFFu; tnp_hdr_put(p, &oh); sendto(s, (const char *)p, (int)plen, 0, (struct sockaddr *)&from, fl); txseq = oh.seq; }
                else { oh.seq = txseq; tnp_hdr_put(p, &oh); sendto(s, (const char *)p, 8, 0, (struct sockaddr *)&from, fl); }
            } else {                                                             /* duplicate or ahead: re-ACK what we have */
                oh.flags = TNP_F_ACK; oh.seq = txseq; oh.ack = rxseq; tnp_hdr_put(p, &oh); sendto(s, (const char *)p, 8, 0, (struct sockaddr *)&from, fl);
            }
        }
    }
}
