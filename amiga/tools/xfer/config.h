/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* config.h -- lrzsz 0.12.20's configuration for AmigaOS (m68k-amigaos-gcc, libnix), written by hand in place of
 * the configure output: the C library's headers and functions, no NLS, no syslog, no termios (rbsb_amiga.c).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  Part of the lrzsz 0.12.20 port (GPL-2). */
#define PACKAGE "lrzsz"
#define VERSION "0.12.20"
#define STDC_HEADERS 1
#define PROTOTYPES 1
#define RETSIGTYPE void
#define HAVE_STRING_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_TIME_H 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_UTIME_H 1
#define HAVE_UTIME 1
#define HAVE_STRUCT_UTIMBUF 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_STRERROR 1
#define HAVE_ERRNO_DECLARATION 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRPBRK 1
#define HAVE_MEMCPY 1
#define HAVE_VPRINTF 1
#define HAVE_MKDIR 1
#define HAVE_MKTIME 1
#define ENABLE_MKDIR 1
#define HAVE_TERMIOS_H 1
