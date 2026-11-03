/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* flashchips.c -- the lookup of flashchips.h over the generated table flashchips_tab.c.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include "flashchips.h"

int fc_lookup(unsigned int mfg, unsigned int model, const fc_part_t **out, int max)
{
    int i, n = 0;
    for (i = 0; i < fc_nparts; i++) {
        const fc_part_t *p = &fc_parts[i];
        if (p->probe != P_RDID && p->probe != P_RDID4) continue;
        if (p->mfg != (mfg & 0xFFu) || p->model != (model & 0xFFFFu)) continue;
        if (n < max) out[n] = p;
        n++;
    }
    return n;
}
