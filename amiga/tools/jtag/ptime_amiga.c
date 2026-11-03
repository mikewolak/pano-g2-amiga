/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* ptime_amiga.c -- ptime.h (panoflash) on AmigaOS: the clock from the C library's clock(), the sleep from dos.library
 * Delay() in ticks of 20 ms.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <time.h>
#include <proto/dos.h>
#include "ptime.h"

double pt_now(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }
void pt_sleep_ms(int ms) { Delay(ms > 0 ? (ms + 19) / 20 : 1); }
