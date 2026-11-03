/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* crc32.h -- CRC-32 of PKZIP, zlib and Ethernet (reflected polynomial 0xEDB88320, start 0xFFFFFFFF, result inverted)
 * with the loops in 68020 assembler.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef CRC32_H
#define CRC32_H
#include <exec/types.h>

/* running form: start with 0xFFFFFFFF, finish with ^ 0xFFFFFFFF */
ULONG crc32_run(ULONG crc, const UBYTE *p, ULONG n);
/* the CRC of n bytes */
ULONG crc32(const UBYTE *p, ULONG n);
#endif
