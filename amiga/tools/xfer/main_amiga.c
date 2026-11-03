/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* main_amiga.c -- the entry of lsz and lrz on AmigaOS: the stream's arguments (zio_args: DEVICE= UNIT= BAUD= BUFFER=
 * RTSCTS, or PIPE= SIDE= for the test pipe) come first and are taken off; the stream is opened (zio_open) and
 * lrzsz's own main runs with the rest of the command line.
 *
 *   lsz DEVICE=serial.device BAUD=115200 -vv FILE          lrz PIPE=t SIDE=1 -vv
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  Part of the lrzsz 0.12.20 port (GPL-2). */
#include "zio.h"
#include <dos/dosextens.h>
#include <proto/exec.h>

int REAL_MAIN(int argc, char **argv);

static void at_end(void) { zio_trace("atexit\n"); zio_close(); zio_trace("atexit done\n"); }

int main(int argc, char **argv)
{
    zio_cfg_t c;
    int rc;
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;   /* no DOS requesters: a bad path is an error */
    zio_args(&argc, &argv, &c);
    {   /* lrzsz chooses its protocol from the program's name and splits that at '/' only: a volume or directory
         * ("RAM:lrz") would read as "rz" with more after it, which means "pipe the file to a command" */
        char *p = argv[0], *b = p;
        for (; *p; p++) if (*p == ':' || *p == '/') b = p + 1;
        argv[0] = b;
    }
    if (zio_open(&c)) {
        if (c.status && c.status->magic == ZIO_STATUS_MAGIC) { c.status->code = 20; c.status->done = 1; }
        return 20;
    }
    atexit(at_end);
    rc = REAL_MAIN(argc, argv);
    zio_exit(rc);
}
