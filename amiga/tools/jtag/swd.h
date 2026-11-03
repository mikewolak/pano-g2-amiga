/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* swd.h -- ARM Serial Wire Debug (ADIv5 SW-DP, MEM-AP) on an FTDI FT2232H channel in MPSSE mode, through the
 * stream of mpsse.c.
 *
 * Wiring (channel A): ADBUS0 SWCLK; ADBUS1 to SWDIO through a 470 ohm resistor; ADBUS2 to SWDIO directly; ADBUS4
 * NRST (optional: driven low to reset, an input otherwise); GND.  ADBUS1 is made an input while the target drives
 * SWDIO and ADBUS2 reads the line throughout.  3.3 V targets only (the FT2232H's I/O).
 *
 * Transactions are queued and sent as one MPSSE exchange (swd_run).  The DP's overrun detection (CTRL/STAT
 * ORUNDETECT) is set by swd_connect, so a WAIT or FAULT answer keeps its data phase and the stream stays aligned;
 * swd_run reports the first transaction that was not answered OK.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef SWD_H
#define SWD_H
#include <stdint.h>
#include "xpc.h"

#define SWD_ACK_OK    1
#define SWD_ACK_WAIT  2
#define SWD_ACK_FAULT 4

/* DP registers (A[3:2] << 2) */
#define DP_DPIDR   0x0           /* read */
#define DP_ABORT   0x0           /* write */
#define DP_CTRLSTAT 0x4
#define DP_SELECT  0x8
#define DP_RDBUFF  0xC
/* MEM-AP registers, bank 0, and the IDR in bank 0xF */
#define AP_CSW     0x0
#define AP_TAR     0x4
#define AP_DRW     0xC
#define AP_IDR_BANK 0xF0u
#define AP_IDR     0xC

#define SWD_Q_MAX  256           /* transactions per exchange */

typedef struct swd {
    xpc_t *x;
    unsigned idle;               /* extra SWCLK cycles after each transaction (beyond the 8 every one has) */
    unsigned nrst;               /* 1: ADBUS4 driven low */
    uint8_t cb[4096]; int cn;    /* MPSSE commands queued */
    int an;                      /* answer bytes expected */
    struct { uint32_t *dst; int ans, rd; } q[SWD_Q_MAX];
    int qn;
    int bad;                     /* after swd_run: index of the first transaction not answered OK, -1 none */
    int bad_ack;                 /* its ACK bits (or 8: a read parity error) */
    uint32_t select;             /* SELECT as last written; 0xFFFFFFFF unknown */
    uint32_t csw;                /* CSW as last written; 0 unknown */
} swd_t;

int  swd_open(swd_t *s, xpc_t *x);                         /* pins for SWD; 0 ok */
int  swd_line_reset(swd_t *s);                              /* JTAG-to-SWD sequence and line reset, then DPIDR */
int  swd_connect(swd_t *s, uint32_t *dpidr);               /* line reset, DPIDR, ABORT, power-up, ORUNDETECT, SELECT 0 */
int  swd_nrst(swd_t *s, int low);                           /* ADBUS4 low (1) or released (0), at once */

/* queue a transaction (-1 when the queue is full: callers keep an exchange under SWD_Q_MAX and 4,096 command
 * bytes); reads land in *dst after swd_run (an AP read returns the previous AP read's value: the
 * caller reads RDBUFF last) */
int  swd_q(swd_t *s, int ap, int rd, unsigned a, uint32_t wdata, uint32_t *dst);
/* send the queue; 0 when every transaction was answered OK, else -1 with bad and bad_ack set */
int  swd_run(swd_t *s);
/* after a failed run: ABORT with every sticky flag cleared; 0 ok */
int  swd_clear(swd_t *s);

/* single register access, sent at once */
int  swd_dp_read(swd_t *s, unsigned a, uint32_t *v);
int  swd_dp_write(swd_t *s, unsigned a, uint32_t v);
int  swd_ap_read(swd_t *s, unsigned a, uint32_t *v);      /* the AP register, then RDBUFF */
int  swd_ap_write(swd_t *s, unsigned a, uint32_t v);
int  swd_select(swd_t *s, uint32_t sel);                   /* SELECT, written only when it changes */

/* memory through MEM-AP 0 (32-bit words; TAR auto-increment within 1 KB) */
int  swd_read32(swd_t *s, uint32_t addr, uint32_t *v);
int  swd_write32(swd_t *s, uint32_t addr, uint32_t v);
int  swd_read_block(swd_t *s, uint32_t addr, uint32_t *w, int n);        /* n words */
int  swd_write_block(swd_t *s, uint32_t addr, const uint32_t *w, int n);
/* halfwords (CSW size 16), each followed by s->idle extra cycles: flash programming of the STM32F0/F1/F3 */
int  swd_write_half(swd_t *s, uint32_t addr, const uint16_t *h, int n, int *done);
#endif
