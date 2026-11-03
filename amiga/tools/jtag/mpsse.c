/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* mpsse.c -- the JTAG bit engine of xpc.h on an FTDI FT2232H channel in MPSSE mode, through the serial stream of
 * amiga/tools/xfer/zio_amiga.c (an Exec serial device; on the G2 the firmware passes its bytes to the chip).
 *
 * Commands (FTDI AN_108): 0x19 bytes out on TDI, 0x39 bytes out and in, 0x1B/0x3B 1-8 bits out (and in), all LSB first,
 * TDI changed on the falling and TDO sampled on the rising edge of TCK; 0x4B/0x6B 1-7 TMS bits with TDI held at bit 7
 * of the data byte (and TDO read); 0x80 ADBUS value and direction; 0x84/0x85 loopback on/off; 0x86 the divisor;
 * 0x87 send the answers now; 0x8A clock divide by 5 off (60 MHz base); 0x8D three-phase clocking off; 0x97 adaptive
 * clocking off.  A bit read lands in the top bits of its answer byte (AN_108 3.3).  The bad command 0xAA is answered
 * 0xFA 0xAA: the sync check.
 *
 * Commands are queued in one buffer and the bits to read are remembered; xpc_flush sends the buffer with 0x87 and
 * reads the answers back into place.  A shift is flushed before the buffer or the answer count would pass its limit.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  New implementation: no MPSSE engine exists in
 * the repositories this project draws on (panoflash drives a Xilinx Platform Cable). */
#include <stdio.h>
#include <string.h>
#define ZIO_IMPL                    /* zio.h without its lrzsz stream macros */
#include "zio.h"
#include "xpc.h"

#define OB_MAX  8192                 /* command bytes queued */
#define RX_MAX  4096                 /* answer bytes expected per flush */
#define RD_MAX  1024                 /* reads queued */
#define TMO_DS  20u                  /* tenths of a second to wait for an answer */

static uint8_t ob[OB_MAX];
static int ob_n, rx_n;
static struct { uint8_t *dst; int pos, nbits, bytes; } rd[RD_MAX];
static int rd_n;
static uint8_t rx[RX_MAX];

static void setbit(uint8_t *d, int pos, int v)
{
    if (v) d[pos >> 3] |= (uint8_t)(1u << (pos & 7)); else d[pos >> 3] &= (uint8_t)~(1u << (pos & 7));
}
static int getbit(const uint8_t *d, int pos) { return d ? (d[pos >> 3] >> (pos & 7)) & 1 : 1; }

/* the answers of n bytes, waited for TMO_DS at most per piece */
static int read_n(xpc_t *x, uint8_t *b, int n)
{
    int got = 0, r;
    while (got < n) {
        r = zio_read_timeout(b + got, (size_t)(n - got), TMO_DS);
        if (r <= 0) { if (!x->err) x->err = got ? -2 : -1; return -1; }
        got += r;
    }
    x->stat_in += (unsigned long)n;
    return 0;
}

int xpc_flush(xpc_t *x)
{
    int i, k, off = 0;
    if (x->err) return -1;
    if (!ob_n) return 0;
    if (rx_n) ob[ob_n++] = 0x87;
    zio_wrfd(1, ob, (size_t)ob_n);
    x->stat_out += (unsigned long)ob_n;
    ob_n = 0;
    if (rx_n) {
        if (read_n(x, rx, rx_n)) { rx_n = rd_n = 0; return -1; }
        for (i = 0; i < rd_n; i++) {
            if (rd[i].bytes) {                       /* whole bytes: bit k of byte j is bit 8j + k */
                for (k = 0; k < rd[i].nbits; k++) setbit(rd[i].dst, rd[i].pos + k, (rx[off + (k >> 3)] >> (k & 7)) & 1);
                off += rd[i].nbits >> 3;
            } else {                                 /* 1-8 bits in the top of one byte */
                unsigned v = rx[off++] >> (8 - rd[i].nbits);
                for (k = 0; k < rd[i].nbits; k++) setbit(rd[i].dst, rd[i].pos + k, (v >> k) & 1);
            }
        }
    }
    rx_n = rd_n = 0;
    return 0;
}

/* room for a command of `c` bytes with `a` answer bytes, flushing first if needed */
static int room(xpc_t *x, int c, int a)
{
    if (ob_n + c + 1 > OB_MAX || rx_n + a > RX_MAX || (a && rd_n >= RD_MAX)) return xpc_flush(x);
    return x->err ? -1 : 0;
}
static void want(uint8_t *dst, int pos, int nbits, int bytes)
{
    rd[rd_n].dst = dst; rd[rd_n].pos = pos; rd[rd_n].nbits = nbits; rd[rd_n].bytes = bytes; rd_n++;
    rx_n += bytes ? nbits >> 3 : 1;
}

int xpc_tms(xpc_t *x, const uint8_t *tms, int len)
{
    int done = 0, n, k;
    while (done < len) {
        uint8_t b = 0x80;                            /* TDI held 1 */
        n = len - done; if (n > 7) n = 7;
        for (k = 0; k < n; k++) b |= (uint8_t)(getbit(tms, done + k) << k);
        if (room(x, 3, 0)) return -1;
        ob[ob_n++] = 0x4B; ob[ob_n++] = (uint8_t)(n - 1); ob[ob_n++] = b;
        done += n;
    }
    x->stat_bits += (unsigned long)len;
    return 0;
}

int xpc_shift(xpc_t *x, const uint8_t *tdi, uint8_t *tdo, int len, int last)
{
    int body = last ? len - 1 : len, pos = 0, n, k;
    if (len <= 0) return 0;
    x->stat_shifts++; x->stat_bits += (unsigned long)len;
    while (body - pos >= 8) {                        /* whole bytes */
        n = (body - pos) >> 3;
        if (tdo) { if (n > RX_MAX) n = RX_MAX; }
        else if (n > OB_MAX - 8) n = OB_MAX - 8;
        if (room(x, 3 + n, tdo ? n : 0)) return -1;
        ob[ob_n++] = tdo ? 0x39 : 0x19; ob[ob_n++] = (uint8_t)(n - 1); ob[ob_n++] = (uint8_t)((n - 1) >> 8);
        if (tdi) memcpy(ob + ob_n, tdi + (pos >> 3), (size_t)n); else memset(ob + ob_n, 0xFF, (size_t)n);
        ob_n += n;
        if (tdo) want(tdo, pos, n * 8, 1);
        pos += n * 8;
    }
    if (body - pos > 0) {                            /* 1-7 bits */
        uint8_t b = 0;
        n = body - pos;
        for (k = 0; k < n; k++) b |= (uint8_t)(getbit(tdi, pos + k) << k);
        if (room(x, 3, tdo ? 1 : 0)) return -1;
        ob[ob_n++] = tdo ? 0x3B : 0x1B; ob[ob_n++] = (uint8_t)(n - 1); ob[ob_n++] = b;
        if (tdo) want(tdo, pos, n, 0);
        pos += n;
    }
    if (last) {                                      /* the last bit with TMS 1: to Exit1 */
        if (room(x, 3, tdo ? 1 : 0)) return -1;
        ob[ob_n++] = tdo ? 0x6B : 0x4B; ob[ob_n++] = 0; ob[ob_n++] = (uint8_t)((getbit(tdi, pos) << 7) | 1);
        if (tdo) want(tdo, pos, 1, 0);
    }
    /* a read is collected before the caller looks at it */
    return tdo ? xpc_flush(x) : 0;
}

int mpsse_loopback(xpc_t *x, int on)
{
    if (room(x, 1, 0)) return -1;
    ob[ob_n++] = on ? 0x84 : 0x85;
    return xpc_flush(x);
}

int mpsse_xfer(xpc_t *x, const uint8_t *out, uint8_t *in, int n)
{
    if (n <= 0 || n > RX_MAX) return -1;
    if (xpc_flush(x)) return -1;
    if (room(x, 3 + n, n)) return -1;
    ob[ob_n++] = 0x39; ob[ob_n++] = (uint8_t)(n - 1); ob[ob_n++] = (uint8_t)((n - 1) >> 8);
    memcpy(ob + ob_n, out, (size_t)n); ob_n += n;
    want(in, 0, n * 8, 1);
    return xpc_flush(x);
}

int mpsse_cmd(xpc_t *x, const uint8_t *cmd, int n, uint8_t *ans, int na)
{
    if (n <= 0 || n > RX_MAX || na < 0 || na > RX_MAX) return -1;
    if (room(x, n, na)) return -1;
    memcpy(ob + ob_n, cmd, (size_t)n); ob_n += n;
    if (na) want(ans, 0, na * 8, 1);
    return xpc_flush(x);
}

int mpsse_open(xpc_t *x, unsigned long tck_hz)
{
    uint8_t a[2];
    unsigned long div;
    int i;
    memset(x, 0, sizeof *x);
    ob_n = rx_n = rd_n = 0;
    zio_purge();
    for (i = 0; i < 2; i++) {                        /* sync: the bad command 0xAA is answered 0xFA 0xAA */
        x->err = 0;
        ob[ob_n++] = 0xAA; ob[ob_n++] = 0x87;
        zio_wrfd(1, ob, (size_t)ob_n); ob_n = 0;
        if (read_n(x, a, 2) == 0 && a[0] == 0xFA && a[1] == 0xAA) break;
        zio_purge();
    }
    if (i == 2) { if (!x->err) x->err = -3; return -1; }
    if (tck_hz < 458u) tck_hz = 458u;                 /* divisor 65535 */
    if (tck_hz > 30000000u) tck_hz = 30000000u;
    div = 30000000u / tck_hz; div = div ? div - 1u : 0u;
    x->tck_hz = 30000000u / (div + 1u);
    {   static const uint8_t setup[] = { 0x8A, 0x97, 0x8D,
                                         0x80, 0x08, 0x0B,       /* ADBUS: TMS 1, TCK 0, TDI 0; TCK, TDI, TMS outputs */
                                         0x82, 0x00, 0x00,       /* ACBUS: all inputs */
                                         0x85 };                 /* loopback off */
        memcpy(ob, setup, sizeof setup); ob_n = (int)sizeof setup;
        ob[ob_n++] = 0x86; ob[ob_n++] = (uint8_t)div; ob[ob_n++] = (uint8_t)(div >> 8);
    }
    return xpc_flush(x);
}
