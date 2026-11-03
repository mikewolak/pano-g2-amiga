# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
# SPDX-License-Identifier: GPL-2.0-or-later
# pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
# in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
# lsz and lrz: lrzsz 0.12.20 (ZMODEM, YMODEM, XMODEM) for AmigaOS over an Exec serial device or the in-memory test
# pipe.  lrzsz's sources compile unchanged from amiga/third_party/lrzsz except zreadline.c (edited copy here: the read
# with its timeout through zio) and rbsb.c (rbsb_amiga.c in its place: no termios); zio.h is included ahead of each
# lrzsz source and zio_amiga.c carries the modem stream.  main_amiga.c takes the stream's arguments, opens it and
# calls lrzsz's main.  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  GPL-2.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
L         ?= ../../third_party/lrzsz/lrzsz-0.12.20
CC        = $(AMIGA_GCC)
CFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -Wall -Wno-unused -Wno-pointer-sign -DHAVE_CONFIG_H -DNFGVMIN \
            -DLOCALEDIR=\"\" -I. -I$(L)/src -I$(L)/lib
LZ        = $(CFLAGS) -include zio.h
COMMON    = zm.o crctab.o canit.o protname.o zperr.o lsyslog.o timing.o long-options.o getopt.o getopt1.o error.o xstrtoul.o xstrtol.o \
            zreadline.o rbsb_amiga.o zio_amiga.o
vpath %.c $(L)/src $(L)/lib
all: lsz lrz
%.o: %.c zio.h config.h
	$(CC) $(LZ) -c -o $@ $<
zio_amiga.o: zio_amiga.c zio.h
	$(CC) $(CFLAGS) -c -o $@ $<
lsz.o: lsz.c zio.h config.h
	$(CC) $(LZ) -Dmain=lsz_main -c -o $@ $<
lrz.o: lrz.c zio.h config.h
	$(CC) $(LZ) -Dmain=lrz_main -c -o $@ $<
main_lsz.o: main_amiga.c zio.h
	$(CC) $(CFLAGS) -DREAL_MAIN=lsz_main -c -o $@ $<
main_lrz.o: main_amiga.c zio.h
	$(CC) $(CFLAGS) -DREAL_MAIN=lrz_main -c -o $@ $<
lsz: lsz.o main_lsz.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ -lm
lrz: lrz.o main_lrz.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^ -lm
clean:
	rm -f lsz lrz *.o
