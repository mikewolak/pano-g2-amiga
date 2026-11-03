/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* chain.h -- IEEE 1149.1 chain discovery: every IDCODE on the chain (BYPASS-
 * only devices show as 0) and the total instruction register length. */
#ifndef CHAIN_H
#define CHAIN_H
#include "jtag.h"
#define CHAIN_MAX 16
typedef struct chain_dev { uint32_t idcode; const char *vendor; const char *part; int irlen; } chain_dev_t;
int chain_scan(jtag_t *j, chain_dev_t *dev, int *ndev, int *ir_total);   /* 0 ok; ends in RTI */
void chain_decode(chain_dev_t *d);                                        /* fill vendor/part/irlen from the idcode */
#endif
