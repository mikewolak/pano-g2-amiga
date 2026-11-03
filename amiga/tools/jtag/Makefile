# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
# SPDX-License-Identifier: GPL-2.0-or-later
# pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
# in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
# jtagtool: JTAG, swdtool: ARM Serial Wire Debug (STM32), through an FTDI FT2232H (MPSSE) on the G2's USB hub.
# jtag.c, chain.c, s3e.c, bitfile.c and the headers jtag.h, chain.h, bitfile.h, ptime.h are Mike Wolak's G1/G2 JTAG flasher's,
# tools/panoflash's, unchanged (commit 00afbbf5; s3e.h of commit 4b5725e3); xpc.h, mpsse.c, swd.c, swd.h,
# ptime_amiga.c, jtagtool.c and swdtool.c are this directory's; the serial stream is ../xfer/zio_amiga.c.
# Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
XFER      ?= ../xfer
CFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -Wall -I. -I$(XFER)
SRC       = jtagtool.c mpsse.c ptime_amiga.c jtag.c chain.c s3e.c bitfile.c $(XFER)/zio_amiga.c
SWDSRC    = swdtool.c swd.c mpsse.c ptime_amiga.c $(XFER)/zio_amiga.c
all: jtagtool swdtool
jtagtool: $(SRC) xpc.h jtag.h chain.h s3e.h bitfile.h ptime.h $(XFER)/zio.h
	$(AMIGA_GCC) $(CFLAGS) -o $@ $(SRC) -lm
swdtool: $(SWDSRC) xpc.h swd.h ptime.h $(XFER)/zio.h
	$(AMIGA_GCC) $(CFLAGS) -o $@ $(SWDSRC) -lm
clean:
	rm -f jtagtool swdtool
