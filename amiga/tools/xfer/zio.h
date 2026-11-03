/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* zio.h -- the modem stream of lrzsz 0.12.20 on AmigaOS: included ahead of every lrzsz source (-include zio.h).
 *
 * lrzsz talks to the modem through file descriptors 0 (in) and 1 (out): read() in zreadline.c, putchar(), fwrite()
 * and fflush() on stdout in zm.c and lsz.c, write() on 0 and 1 in canit.c and lrz.c.  The macros below send those
 * to zio_amiga.c, which drives an Exec serial device or the in-memory test pipe; every other descriptor and FILE
 * goes to the C library.  Signals and alarm() do nothing: the read timeout is zio's own, and Ctrl-C (the break
 * signal) cancels the transfer at the next read or write.  exit() flushes and closes the stream first.
 *
 * The system headers come first, so their declarations are made before the macros exist.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  lrzsz: Uwe Ohse, Chuck Forsberg (GPL-2). */
#ifndef ZIO_H
#define ZIO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "zio_status.h"

/* the stream, chosen by the program's leading arguments before lrzsz parses the rest (zio_args) */
typedef struct {
    const char *device;                          /* Exec serial device; NULL: the test pipe */
    unsigned long unit, baud, rbuf;              /* unit, rate, the device's read buffer in bytes */
    int rtscts;                                  /* SERF_7WIRE */
    const char *pipe;                            /* the test pipe's name */
    int side;                                    /* the pipe's side, 0 or 1 */
    const char *trace;                           /* a file for the trace of the stream calls, or NULL */
    zio_status_t *status;                        /* the parent's status block, or NULL */
} zio_cfg_t;

int  zio_args(int *argc, char ***argv, zio_cfg_t *c);   /* takes DEVICE= UNIT= BAUD= BUFFER= RTSCTS PIPE= SIDE= TRACE= LOG= STATUS= */
int  zio_open(const zio_cfg_t *c);               /* 0, or -1 with the reason printed */
void zio_close(void);

int     zio_read_timeout(void *buf, size_t n, unsigned int ds);  /* bytes read, -2 after ds tenths of a second */
ssize_t zio_rdfd(int fd, void *buf, size_t n);
ssize_t zio_wrfd(int fd, const void *buf, size_t n);
int     zio_putc(int c);
size_t  zio_fwrite(const void *p, size_t s, size_t n, FILE *f);
int     zio_fflush(FILE *f);
int     zio_avail(void);                         /* bytes waiting to be read */
void    zio_purge(void);                         /* the input discarded */
void    zio_break(void);                         /* a break on the line */
void    zio_exit(int code) __attribute__((noreturn));
FILE   *zio_fopen(const char *name, const char *mode);   /* fopen, traced */
int     zio_access(const char *p, int m);
int     zio_stat(const char *p, struct stat *b);
int     zio_printf(const char *fmt, ...);
void    zio_trace(const char *fmt, ...);
extern unsigned long zio_baud;                   /* the rate in use */

#ifndef ZIO_IMPL
#undef putchar
#define putchar(c)          zio_putc(c)
#define fwrite(p, s, n, f)  zio_fwrite(p, s, n, f)
#define fflush(f)           zio_fflush(f)
#define read(fd, b, n)      zio_rdfd(fd, b, n)
#define write(fd, b, n)     zio_wrfd(fd, b, n)
#define exit(n)             zio_exit(n)
#define signal(s, h)        ((void (*)(int))0)
#define alarm(n)            (0)
#define setvbuf(f, b, m, s) (0)
#define fopen(n, m)         zio_fopen(n, m)
#define access(p, m)        zio_access(p, m)
#define stat(p, b)          zio_stat(p, b)
#define printf(...)         zio_printf(__VA_ARGS__)
#endif

#endif
