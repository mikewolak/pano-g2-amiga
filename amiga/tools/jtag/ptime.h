/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* ptime.h -- a monotonic clock and a sleep, portable between the Windows
 * build and the native test build. */
#ifndef PTIME_H
#define PTIME_H
double pt_now(void);              /* seconds, monotonic */
void   pt_sleep_ms(int ms);
#endif
