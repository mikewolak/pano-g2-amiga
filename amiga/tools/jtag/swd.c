/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* swd.c -- ARM Serial Wire Debug on an FTDI FT2232H channel in MPSSE mode (swd.h), through mpsse_cmd of mpsse.c.
 *
 * Wire protocol (ARM IHI 0031, ADIv5, chapter B4): a request of 8 bits (Start 1, APnDP, RnW, A[2], A[3], even parity
 * of those four, Stop 0, Park 1), one turnaround cycle, a 3-bit ACK (OK 001, WAIT 010, FAULT 100, LSB first), then for
 * a read 32 data bits and their even parity from the target and a turnaround, for a write a turnaround and 32 data
 * bits and parity from the host.  Eight idle cycles with SWDIO low follow every transaction.
 *
 * MPSSE commands (FTDI AN_108), all LSB first with SWCLK idle low: 0x19 bytes and 0x1B bits out on the falling edge,
 * 0x28 bytes and 0x2A bits in on the rising edge; 0x80 ADBUS value and direction (ADBUS1 an input while the target
 * drives SWDIO); 0x8F clocks without data for idle cycles.  A bit read lands in the top bits of its answer byte.  The
 * pin scheme (SWDIO driven from ADBUS1 through a resistor, read on ADBUS2, ADBUS1's direction switched for the
 * target's phases) and the sampling edges are those of OpenOCD's ftdi driver (src/jtag/drivers/ftdi.c, GPL-2.0).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  New implementation: no SWD code exists in the
 * repositories this project draws on. */
#include <string.h>
#include "swd.h"

#define PIN_DIR_OUT 0x03u            /* ADBUS0 SWCLK and ADBUS1 SWDIO out; ADBUS2 in */
#define PIN_DIR_IN  0x01u            /* ADBUS1 an input: the target drives SWDIO */
#define PIN_NRST    0x10u            /* ADBUS4: an output (driven low) when NRST is asserted */
#define CSW_32      0x23000012u      /* MasterType debug, HPROT1, auto-increment single, size 32 */
#define CSW_16      0x23000011u      /* the same, size 16 */
#define CB_ROOM     40               /* the longest transaction's command bytes */

static uint8_t s_ans[4096];

static unsigned par32(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1u; }

static void put3(swd_t *s, uint8_t a, uint8_t b, uint8_t c) { s->cb[s->cn++] = a; s->cb[s->cn++] = b; s->cb[s->cn++] = c; }
static void pins(swd_t *s, int out) { put3(s, 0x80, 0x00, (uint8_t)((out ? PIN_DIR_OUT : PIN_DIR_IN) | (s->nrst ? PIN_NRST : 0u))); }
static void idle(swd_t *s)
{
    unsigned n = (s->idle + 7u) / 8u;
    put3(s, 0x1B, 7, 0x00);                          /* 8 cycles, SWDIO low */
    if (n) put3(s, 0x8F, (uint8_t)(n - 1u), (uint8_t)((n - 1u) >> 8));
}

int swd_q(swd_t *s, int ap, int rd, unsigned a, uint32_t wdata, uint32_t *dst)
{
    unsigned req, p;
    if (s->qn >= SWD_Q_MAX || s->cn + CB_ROOM > (int)sizeof s->cb || s->an + 8 > (int)sizeof s_ans) return -1;
    p = ((unsigned)ap ^ (unsigned)rd ^ ((a >> 2) & 1u) ^ ((a >> 3) & 1u)) & 1u;
    req = 0x81u | ((unsigned)ap << 1) | ((unsigned)rd << 2) | ((a & 0xCu) << 1) | (p << 5);
    s->q[s->qn].dst = dst; s->q[s->qn].ans = s->an; s->q[s->qn].rd = rd; s->qn++;
    put3(s, 0x1B, 7, (uint8_t)req);
    pins(s, 0);
    if (rd) {
        s->cb[s->cn++] = 0x2A; s->cb[s->cn++] = 3;   /* turnaround, ACK */
        put3(s, 0x28, 3, 0);                        /* 32 data bits */
        s->cb[s->cn++] = 0x2A; s->cb[s->cn++] = 1;   /* parity, turnaround */
        s->an += 6;
        pins(s, 1);
    } else {
        s->cb[s->cn++] = 0x2A; s->cb[s->cn++] = 4;   /* turnaround, ACK, turnaround */
        s->an += 1;
        pins(s, 1);
        put3(s, 0x19, 3, 0);
        s->cb[s->cn++] = (uint8_t)wdata; s->cb[s->cn++] = (uint8_t)(wdata >> 8);
        s->cb[s->cn++] = (uint8_t)(wdata >> 16); s->cb[s->cn++] = (uint8_t)(wdata >> 24);
        put3(s, 0x1B, 0, (uint8_t)par32(wdata));
    }
    idle(s);
    return 0;
}

int swd_run(swd_t *s)
{
    int i, r;
    s->bad = -1; s->bad_ack = 0;
    if (!s->qn) return 0;
    r = mpsse_cmd(s->x, s->cb, s->cn, s_ans, s->an);
    if (r) { s->bad = 0; s->bad_ack = 0; s->qn = s->cn = s->an = 0; return -1; }
    for (i = 0; i < s->qn; i++) {
        const uint8_t *a = s_ans + s->q[i].ans;
        unsigned ack;
        if (s->q[i].rd) {
            uint32_t v = (uint32_t)a[1] | ((uint32_t)a[2] << 8) | ((uint32_t)a[3] << 16) | ((uint32_t)a[4] << 24);
            ack = ((unsigned)a[0] >> 5) & 7u;        /* 4 bits in the top of the byte: turnaround, ACK */
            if (ack == SWD_ACK_OK && (((unsigned)a[5] >> 6) & 1u) != par32(v)) ack = 8u;
            if (ack == SWD_ACK_OK && s->q[i].dst) *s->q[i].dst = v;
        } else ack = ((unsigned)a[0] >> 4) & 7u;     /* 5 bits: turnaround, ACK, turnaround */
        if (ack != SWD_ACK_OK) { s->bad = i; s->bad_ack = (int)ack; break; }
    }
    s->qn = s->cn = s->an = 0;
    return s->bad < 0 ? 0 : -1;
}

int swd_open(swd_t *s, xpc_t *x)
{
    memset(s, 0, sizeof *s);
    s->x = x; s->select = 0xFFFFFFFFu; s->bad = -1;
    pins(s, 1);
    if (mpsse_cmd(x, s->cb, s->cn, NULL, 0)) return -1;
    s->cn = 0;
    return 0;
}

int swd_nrst(swd_t *s, int low)
{
    s->nrst = low ? 1u : 0u;
    s->cn = 0; s->qn = 0; s->an = 0;
    pins(s, 1);
    if (mpsse_cmd(s->x, s->cb, s->cn, NULL, 0)) return -1;
    s->cn = 0;
    return 0;
}

int swd_line_reset(swd_t *s)
{
    static const uint8_t ones[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint32_t id;
    s->cn = 0; s->qn = 0; s->an = 0;
    pins(s, 1);
    put3(s, 0x19, 6, 0); memcpy(s->cb + s->cn, ones, 7); s->cn += 7;       /* 56 cycles SWDIO high */
    put3(s, 0x19, 1, 0); s->cb[s->cn++] = 0x9E; s->cb[s->cn++] = 0xE7;     /* JTAG-to-SWD 0xE79E, LSB first */
    put3(s, 0x19, 6, 0); memcpy(s->cb + s->cn, ones, 7); s->cn += 7;       /* line reset */
    put3(s, 0x1B, 7, 0x00);                                                /* idle */
    s->select = 0xFFFFFFFFu; s->csw = 0;
    return swd_dp_read(s, DP_DPIDR, &id);
}

int swd_dp_read(swd_t *s, unsigned a, uint32_t *v)  { if (swd_q(s, 0, 1, a, 0, v)) return -1; return swd_run(s); }
int swd_dp_write(swd_t *s, unsigned a, uint32_t v)  { if (swd_q(s, 0, 0, a, v, NULL)) return -1; return swd_run(s); }

int swd_select(swd_t *s, uint32_t sel)
{
    if (s->select == sel) return 0;
    if (swd_dp_write(s, DP_SELECT, sel)) return -1;
    s->select = sel;
    return 0;
}
int swd_ap_read(swd_t *s, unsigned a, uint32_t *v)
{
    if (swd_q(s, 1, 1, a, 0, NULL) || swd_q(s, 0, 1, DP_RDBUFF, 0, v)) return -1;
    return swd_run(s);
}
int swd_ap_write(swd_t *s, unsigned a, uint32_t v) { if (swd_q(s, 1, 0, a, v, NULL)) return -1; return swd_run(s); }

int swd_clear(swd_t *s)
{
    if (swd_line_reset(s)) return -1;
    return swd_dp_write(s, DP_ABORT, 0x1Fu);         /* DAPABORT, STKCMPCLR, STKERRCLR, WDERRCLR, ORUNERRCLR */
}

int swd_connect(swd_t *s, uint32_t *dpidr)
{
    uint32_t v = 0;
    int i;
    if (swd_line_reset(s)) return -1;
    if (swd_dp_read(s, DP_DPIDR, dpidr)) return -2;
    if (swd_dp_write(s, DP_ABORT, 0x1Eu)) return -3;
    if (swd_dp_write(s, DP_CTRLSTAT, 0x50000001u)) return -3;   /* CSYSPWRUPREQ, CDBGPWRUPREQ, ORUNDETECT */
    for (i = 0; i < 20; i++) {
        if (swd_dp_read(s, DP_CTRLSTAT, &v)) return -4;
        if ((v & 0xA0000000u) == 0xA0000000u) break;
    }
    if (i == 20) return -5;                          /* no power-up acknowledge */
    if (swd_select(s, 0)) return -6;
    if (swd_ap_write(s, AP_CSW, CSW_32)) return -7;
    s->csw = CSW_32;
    return 0;
}

static int csw(swd_t *s, uint32_t v)
{
    if (s->csw == v) return 0;
    if (swd_select(s, 0) || swd_ap_write(s, AP_CSW, v)) return -1;
    s->csw = v;
    return 0;
}

/* the sticky error after a block: a MEM-AP bus fault (STICKYERR, bit 5) */
static int sticky(swd_t *s)
{
    uint32_t v = 0;
    if (swd_dp_read(s, DP_CTRLSTAT, &v)) return -1;
    if (v & 0x22u) { (void)swd_dp_write(s, DP_ABORT, 0x1Eu); return -1; }   /* STICKYERR or STICKYORUN */
    return 0;
}

int swd_read32(swd_t *s, uint32_t addr, uint32_t *v) { return swd_read_block(s, addr, v, 1); }
int swd_write32(swd_t *s, uint32_t addr, uint32_t v) { return swd_write_block(s, addr, &v, 1); }

#define CHUNK 120                    /* words per exchange: inside SWD_Q_MAX, 4,096 command and answer bytes */

int swd_read_block(swd_t *s, uint32_t addr, uint32_t *w, int n)
{
    uint32_t dummy;
    while (n > 0) {
        int k = (int)((0x400u - (addr & 0x3FFu)) >> 2), i, tries;     /* TAR increments within 1 KB */
        if (k > n) k = n;
        if (k > CHUNK) k = CHUNK;
        for (tries = 0; ; tries++) {
            if (csw(s, CSW_32) || swd_select(s, 0)) return -1;
            swd_q(s, 1, 0, AP_TAR, addr, NULL);
            for (i = 0; i < k; i++) swd_q(s, 1, 1, AP_DRW, 0, i ? &w[i - 1] : &dummy);
            swd_q(s, 0, 1, DP_RDBUFF, 0, &w[k - 1]);
            if (swd_run(s) == 0) break;
            if (tries == 2 || swd_clear(s)) return -1;
            s->csw = 0;
        }
        addr += (uint32_t)k * 4u; w += k; n -= k;
    }
    return sticky(s);
}

int swd_write_block(swd_t *s, uint32_t addr, const uint32_t *w, int n)
{
    while (n > 0) {
        int k = (int)((0x400u - (addr & 0x3FFu)) >> 2), i;
        if (k > n) k = n;
        if (k > CHUNK) k = CHUNK;
        if (csw(s, CSW_32) || swd_select(s, 0)) return -1;
        swd_q(s, 1, 0, AP_TAR, addr, NULL);
        for (i = 0; i < k; i++) swd_q(s, 1, 0, AP_DRW, w[i], NULL);
        if (swd_run(s)) { (void)swd_clear(s); s->csw = 0; return -1; }
        addr += (uint32_t)k * 4u; w += k; n -= k;
    }
    return sticky(s);
}

int swd_write_half(swd_t *s, uint32_t addr, const uint16_t *h, int n, int *done)
{
    int all = 0;
    if (done) *done = 0;
    while (n > 0) {
        int k = (int)((0x400u - (addr & 0x3FFu)) >> 1), i;
        if (k > n) k = n;
        if (k > CHUNK) k = CHUNK;
        if (csw(s, CSW_16) || swd_select(s, 0)) return -1;
        swd_q(s, 1, 0, AP_TAR, addr, NULL);
        for (i = 0; i < k; i++)                      /* the halfword on its byte lanes: address bit 1 */
            swd_q(s, 1, 0, AP_DRW, (uint32_t)h[i] << (((addr + 2u * (uint32_t)i) & 2u) * 8u), NULL);
        if (swd_run(s)) {
            if (done) *done = all + (s->bad > 0 ? s->bad - 1 : 0);
            (void)swd_clear(s); s->csw = 0;
            return -1;
        }
        addr += (uint32_t)k * 2u; h += k; n -= k; all += k;
        if (done) *done = all;
    }
    return sticky(s);
}
