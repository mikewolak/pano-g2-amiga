/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* xpc.h -- the JTAG bit engine under jtag.c, chain.c and s3e.c (copied unchanged from Mike Wolak's G1/G2 JTAG flasher
 * tools/panoflash): the same calls as panoflash's Xilinx Platform Cable engine, carried here by an FTDI FT2232H's
 * channel A in MPSSE mode (mpsse.c), reached through an Exec serial device whose bytes the G2 firmware passes to the
 * chip (fw/amiga FTDI=MPSSE).
 *
 * Pins (FTDI AN_108, MPSSE JTAG): ADBUS0 TCK, ADBUS1 TDI, ADBUS2 TDO, ADBUS3 TMS.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef XPC_H
#define XPC_H
#include <stddef.h>
#include <stdint.h>

typedef struct xpc {
    unsigned long tck_hz;            /* the TCK made: 60 MHz / ((1 + divisor) * 2) */
    unsigned long stat_shifts, stat_bits, stat_out, stat_in;   /* shifts, bits, bytes written and read */
    int err;                         /* the first failure: -1 no answer, -2 short answer, -3 sync lost */
} xpc_t;

/* clock `len` TMS bits (TDI held 1), LSB of tms[0] first -- TAP navigation */
int  xpc_tms(xpc_t *x, const uint8_t *tms, int len);
/* shift `len` bits of tdi (LSB of tdi[0] first; NULL: ones) with TMS 0, TMS 1 on the last bit if `last`; TDO into
 * tdo (bit-packed LSB first; NULL: not read) */
int  xpc_shift(xpc_t *x, const uint8_t *tdi, uint8_t *tdo, int len, int last);
/* send what is queued and collect the answers */
int  xpc_flush(xpc_t *x);

/* the engine itself (mpsse.c) */
int  mpsse_open(xpc_t *x, unsigned long tck_hz);              /* sync (bad command 0xAA), clocks, pins; 0 ok */
int  mpsse_loopback(xpc_t *x, int on);                        /* 0x84 TDI to TDO inside the chip, 0x85 off */
/* n bytes out on TDI and n bytes in from TDO (command 0x39), in one exchange of at most 4,096 bytes */
int  mpsse_xfer(xpc_t *x, const uint8_t *out, uint8_t *in, int n);
/* any MPSSE command bytes (n at most 4,096) with `na' answer bytes into ans (na at most 4,096; 0: none), sent at
 * once with what is queued; 0, or -1 with x->err set */
int  mpsse_cmd(xpc_t *x, const uint8_t *cmd, int n, uint8_t *ans, int na);
#endif
