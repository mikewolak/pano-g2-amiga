# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# serialmon: the serial port through an Exec serial driver: a loopback test, meters, the modem lines, a monitor with a
# send line, and a driver selector (serialmon.c).  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
# AMIGA_GCC: the cross compiler (default ~/opt/amiga/bin/m68k-amigaos-gcc).  GIT_HASH: the version in the About box.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
GIT_HASH  ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
# the terminal: amiga/tools/term (term.c and libvterm), built there as libterm.a
TERM      ?= ../term
serialmon: serialmon.c font8x8.c font8x8.h $(TERM)/libterm.a $(TERM)/term.h
	$(AMIGA_GCC) -std=gnu99 -noixemul -m68020 -O2 -Wall -DGIT_HASH=\"$(GIT_HASH)\" -o $@ serialmon.c font8x8.c $(TERM)/libterm.a -lm
$(TERM)/libterm.a:
	$(MAKE) -C $(TERM) libterm.a
clean:
	rm -f serialmon
