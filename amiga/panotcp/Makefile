# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# amiga/panotcp/Makefile -- the Amiga side of the firmware's socket window (fw/amiga/sock.h) with m68k-amigaos-gcc
# 6.5.0b (bebbo amiga-gcc).  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
TOOLS   ?= $(HOME)/opt/amiga/bin
CC       = $(TOOLS)/m68k-amigaos-gcc
CFLAGS   = -m68020 -O2 -Wall -noixemul -fomit-frame-pointer -fno-strict-aliasing

all: PanoTCP sktest bsdtest

bsdtest: bsdtest.c
	$(CC) $(CFLAGS) -Wno-format -o $@ bsdtest.c

PanoTCP: panotcp.c pnwin.c pnwin.h tstub.s
	$(CC) $(CFLAGS) -Wno-format -o $@ panotcp.c pnwin.c tstub.s

sktest: sktest.c pnwin.c pnwin.h
	$(CC) $(CFLAGS) -Wno-format -o $@ sktest.c pnwin.c

clean:
	rm -f PanoTCP sktest bsdtest
