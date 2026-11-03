/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
#include "jtag.h"
#include <string.h>
int jtag_reset(jtag_t *j)
{
    uint8_t seq = 0x1F;                       /* 1 1 1 1 1 : TLR from anywhere */
    if (xpc_tms(j->x, &seq, 5)) return -1;
    seq = 0x00;                               /* 0 : TLR -> RTI */
    return xpc_tms(j->x, &seq, 1);
}
int jtag_init(jtag_t *j, xpc_t *x, int irlen) { j->x = x; j->irlen = irlen; return jtag_reset(j); }
int jtag_shift_ir(jtag_t *j, uint32_t ir, uint32_t *ir_capture)
{
    uint8_t seq = 0x03;                       /* 1 1 0 0 : RTI -> SelDR -> SelIR -> CapIR -> ShIR */
    uint8_t tdi[4] = { (uint8_t)ir, (uint8_t)(ir >> 8), (uint8_t)(ir >> 16), (uint8_t)(ir >> 24) };
    uint8_t tdo[4] = {0, 0, 0, 0};
    if (xpc_tms(j->x, &seq, 4)) return -1;
    if (xpc_shift(j->x, tdi, ir_capture ? tdo : NULL, j->irlen, 1)) return -2;   /* last bit exits to Exit1-IR */
    seq = 0x01;                               /* 1 0 : Exit1-IR -> UpdIR -> RTI */
    if (xpc_tms(j->x, &seq, 2)) return -3;
    if (ir_capture) *ir_capture = (uint32_t)tdo[0] | (uint32_t)tdo[1] << 8 | (uint32_t)tdo[2] << 16 | (uint32_t)tdo[3] << 24;
    return 0;
}
int jtag_shift_dr(jtag_t *j, const uint8_t *tdi, uint8_t *tdo, int bits)
{
    uint8_t seq = 0x01;                       /* 1 0 0 : RTI -> SelDR -> CapDR -> ShDR */
    if (xpc_tms(j->x, &seq, 3)) return -1;
    /* long shifts: all but the last chunk keep TMS=0 */
    int done = 0;
    while (done < bits) {
        int n = bits - done; if (n > 8000) n = 8000;          /* multiple of 8, under the 8191-bit cap */
        int last = (done + n == bits);
        const uint8_t *ti = tdi ? tdi + (done >> 3) : NULL;   /* chunks are multiples of 8 except the last */
        uint8_t *to = tdo ? tdo + (done >> 3) : NULL;
        if (xpc_shift(j->x, ti, to, n, last)) return -2;
        done += n;
    }
    seq = 0x01;                               /* 1 0 : Exit1-DR -> UpdDR -> RTI */
    return xpc_tms(j->x, &seq, 2);
}
int jtag_idle(jtag_t *j, int tcks)
{
    uint8_t z[1024]; memset(z, 0, sizeof z);
    while (tcks > 0) { int n = tcks > 8192 ? 8192 : tcks; if (xpc_tms(j->x, z, n)) return -1; tcks -= n; }
    return 0;
}
