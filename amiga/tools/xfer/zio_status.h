/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* zio_status.h -- the status block of an lsz or lrz transfer (amiga/tools/xfer), shared with the program that started
 * it (serialmon): that program allocates it (MEMF_PUBLIC), fills `magic` and passes its address as STATUS=0x<hex>;
 * the transfer writes the bytes read and written on the stream, its task (for Ctrl-C), and at the end done = 1 with
 * the exit code.  The block must stay allocated until done is set.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef ZIO_STATUS_H
#define ZIO_STATUS_H

#define ZIO_STATUS_MAGIC 0x5A494F31ul                    /* "ZIO1" */
typedef struct {
    unsigned long magic;
    volatile unsigned long in, out;
    volatile unsigned long done, code;
    void *volatile task;
} zio_status_t;

#endif
