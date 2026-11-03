/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* rbsb_amiga.c -- lrzsz 0.12.20's tty functions (rbsb.c) for AmigaOS: the serial device is opened and set by zio
 * (zio_amiga.c) before lrzsz runs, so the mode changes have nothing to do; purge and break go to the device.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  Part of the lrzsz 0.12.20 port (GPL-2). */
#include "zglobal.h"
#include "error.h"

int Fromcu;                                      /* lrz and lsz print their banner when set */
int Twostop;

int from_cu(void)
{
    Fromcu = 0;
    return Fromcu;
}

/* READCHECK_READS (zglobal.h, no FIONREAD here): a byte waiting is read into `checked` */
unsigned char checked;
int rdchk(int fd)
{
    (void)fd;
    if (readline_left > 0) { checked = (unsigned char)*readline_ptr++; readline_left--; return 1; }
    if (zio_avail() > 0 && zio_read_timeout(&checked, 1, 1) == 1) return 1;
    return 0;
}

int io_mode(int fd, int n)
{
    (void)fd;
    if (n == 1 || n == 3) Baudrate = (unsigned)zio_baud;
    return OK;
}

void sendbrk(int fd)
{
    (void)fd;
    zio_break();
}

void purgeline(int fd)
{
    (void)fd;
    readline_purge();
    zio_purge();
}

/* no TCP on this port: lsz and lrz's --tcp options fail */
int tcp_server(char *buf) { (void)buf; error(1, 0, "no TCP in this build"); return -1; }
int tcp_connect(char *buf) { (void)buf; error(1, 0, "no TCP in this build"); return -1; }
int tcp_accept(int d) { (void)d; error(1, 0, "no TCP in this build"); return -1; }
int gethostname(char *name, size_t len) { strncpy(name, "amiga", len); return 0; }

/* lrz runs a sender's command (ZCOMMAND) only with --allow-commands; there is no exec on this port */
int execl(const char *path, const char *arg, ...) { (void)path; (void)arg; errno = ENOSYS; return -1; }
