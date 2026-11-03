/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* link.c -- see link.h.  fw_cmd is Mike Wolak's command-block client
 * do_fw, Mike Wolak, EPROM Foundry Inc.), with its output into a buffer and a 20 s limit instead of 600 s.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#define ZIO_IMPL                    /* zio.h without its lrzsz stream macros */
#include "zio.h"
#include "link.h"

xpc_t lk;
int   lk_open;

int link_open(const char *device, unsigned long unit, unsigned long hz, char *why, int whylen)
{
    zio_cfg_t c;
    memset(&c, 0, sizeof c);
    c.device = device; c.unit = unit; c.baud = 115200u; c.rbuf = 16384u;
    link_close();
    if (zio_open(&c)) { snprintf(why, (size_t)whylen, "%s unit %lu did not open", device, unit); return -1; }
    if (mpsse_open(&lk, hz)) {
        snprintf(why, (size_t)whylen, "no MPSSE answer through %s (error %d): FT2232H on the hub, AMIGA.INI FTDI=MPSSE?",
                 device, lk.err);
        zio_close();
        return -1;
    }
    lk_open = 1;
    return 0;
}

void link_close(void)
{
    if (lk_open) zio_close();
    lk_open = 0;
}

/* ---- the firmware's console through the command block  ---- */
#define CMD_OFFSET (3 * 512)
#define CMD_PAGE   492
static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static void be32_put(UBYTE *p, ULONG v) { p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v; }
static int blk_io(struct IOStdReq *io, UWORD cmd, UBYTE *buf)
{
    io->io_Command = cmd; io->io_Data = buf; io->io_Length = 512; io->io_Offset = CMD_OFFSET;
    return DoIO((struct IORequest *)io);
}

int fw_cmd(const char *line, char *out, int max)
{
    static ULONG seq;
    struct MsgPort *port;
    struct IOStdReq *io;
    UBYTE *blk;
    const char *err = NULL;
    ULONG len = 0, page;
    int ticks = 0, done = 0, n = 0;
    out[0] = 0;
    if (strlen(line) > 503) { snprintf(out, (size_t)max, "command longer than 503 characters"); return -1; }
    if (!seq) { struct DateStamp d; DateStamp(&d); seq = (ULONG)d.ds_Days * 4320000u + (ULONG)d.ds_Minute * 3000u + (ULONG)d.ds_Tick; }
    seq++;
    port = CreateMsgPort();
    io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq)) : NULL;
    blk = (UBYTE *)AllocMem(512, MEMF_PUBLIC | MEMF_CLEAR);
    if (!io || !blk || OpenDevice((CONST_STRPTR)"scsi.device", 0, (struct IORequest *)io, 0) != 0) {
        if (blk) FreeMem(blk, 512);
        if (io) DeleteIORequest((struct IORequest *)io);
        if (port) DeleteMsgPort(port);
        snprintf(out, (size_t)max, "cannot open scsi.device unit 0");
        return -1;
    }
    /* the command, written again every 2 s until the firmware reports its sequence number; done within 20 s */
    while (!done && ticks < 20 * 50) {
        if (ticks % 100 == 0) {
            memset(blk, 0, 512); memcpy(blk, "PFC1", 4); be32_put(blk + 4, seq); strcpy((char *)blk + 8, line);
            if (blk_io(io, CMD_WRITE, blk)) { err = "write of the command block failed"; break; }
        }
        Delay(1); ticks += 1;
        if (blk_io(io, CMD_READ, blk)) { err = "read of the command block failed"; break; }
        if (memcmp(blk, "PFR1", 4)) { err = "no command block on HD0 (firmware too old, or the disk's layout has none)"; break; }
        if (be32(blk + 4) == seq && be32(blk + 8) == 2) { done = 1; len = be32(blk + 12); }
    }
    if (!done && !err) err = "the firmware did not finish the command within 20 s";
    for (page = 0; !err && page * CMD_PAGE < len; page++) {
        ULONG k;
        if (page) {                                  /* the page request, written again until the block answers it */
            int tries = 0;
            for (;;) {
                memset(blk, 0, 512); memcpy(blk, "PFP1", 4); be32_put(blk + 4, seq); be32_put(blk + 8, page);
                if (blk_io(io, CMD_WRITE, blk) || blk_io(io, CMD_READ, blk)) { err = "command block i/o failed"; break; }
                if (be32(blk + 4) == seq && be32(blk + 16) == page) break;
                if (++tries > 20) { err = "the firmware did not answer a page request"; break; }
                Delay(1);
            }
            if (err) break;
        }
        k = len - page * CMD_PAGE; if (k > CMD_PAGE) k = CMD_PAGE;
        if (n + (int)k > max - 1) k = (ULONG)(max - 1 - n);
        memcpy(out + n, blk + 20, k); n += (int)k; out[n] = 0;
        if (n >= max - 1) break;
    }
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    FreeMem(blk, 512);
    if (err) { snprintf(out, (size_t)max, "%s", err); return -1; }
    return n;
}

int fw_ftdi_bytes(const char *out, unsigned char *b, int max)
{
    const char *p = strstr(out, "ftdi: ");
    int n = 0;
    if (!p || strncmp(p + 6, "ok", 2)) return -1;
    p += 8;
    for (;;) {
        char *e;
        unsigned long v;
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        v = strtoul(p, &e, 16);
        if (e == p) break;
        if (n < max) b[n] = (unsigned char)v;
        n++; p = e;
    }
    return n;
}
