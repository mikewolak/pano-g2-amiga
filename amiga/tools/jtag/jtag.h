/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* jtag.h -- IEEE 1149.1 TAP driver over the xpc bit engine (single device chain).
 * Every operation ends in Run-Test/Idle so state is never ambiguous. */
#ifndef JTAG_H
#define JTAG_H
#include "xpc.h"
typedef struct jtag { xpc_t *x; int irlen; } jtag_t;
int jtag_init(jtag_t *j, xpc_t *x, int irlen);         /* TLR then RTI */
int jtag_reset(jtag_t *j);                              /* 5x TMS=1, then RTI */
int jtag_shift_ir(jtag_t *j, uint32_t ir, uint32_t *ir_capture);   /* RTI -> Shift-IR -> RTI */
int jtag_shift_dr(jtag_t *j, const uint8_t *tdi, uint8_t *tdo, int bits);  /* RTI -> Shift-DR -> RTI */
int jtag_idle(jtag_t *j, int tcks);                     /* stay in RTI for `tcks` clocks */
#endif
