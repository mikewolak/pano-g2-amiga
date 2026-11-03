/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* link.h -- ftdimon's two paths to the FT2232H on the G2's hub: the byte stream of channel A in MPSSE mode (an Exec
 * serial driver whose bytes the firmware passes to the chip; amiga/tools/jtag/mpsse.c on amiga/tools/xfer/zio_amiga.c)
 * and the firmware's console through the command block of HD0, which carries the chip's
 * control requests (console `ftdi').
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef LINK_H
#define LINK_H
#include "xpc.h"

extern xpc_t lk;                     /* the MPSSE engine's state */
extern int   lk_open;                /* the stream is open and the chip answered the sync command */

/* the stream opened on `device' unit `unit' and MPSSE set up with TCK `hz'; 0, or -1 with the reason in why */
int  link_open(const char *device, unsigned long unit, unsigned long hz, char *why, int whylen);
void link_close(void);

/* one console command of the firmware through the command block; its output (NUL-terminated, at most max - 1
 * bytes) in out.  Bytes of output, or -1 with the reason in out */
int  fw_cmd(const char *line, char *out, int max);

/* the bytes after "ftdi: ok" of a `ftdi ctl' answer, parsed as hex; count, or -1 on "ftdi: error" */
int  fw_ftdi_bytes(const char *out, unsigned char *b, int max);
#endif
