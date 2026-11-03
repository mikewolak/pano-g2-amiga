# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# The serialmon terminal: term.c (drawing, scrolling, keys) over libvterm 0.3.3 (amiga/third_party/libvterm, MIT),
# and vtplay, which plays a file through it for tests.  libvterm compiles unchanged; '-Dvisibility(x)=' empties its
# visibility("internal") attribute (the Amiga GNU assembler has no .internal).
# Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
V         ?= ../../third_party/libvterm/libvterm-0.3.3
CC        = $(AMIGA_GCC)
CFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -Wall -I$(V)/include -I$(V)/src
VFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -I$(V)/include '-Dvisibility(x)='
VOBJ      = vterm.o encoding.o keyboard.o mouse.o parser.o pen.o screen.o state.o unicode.o
vpath %.c $(V)/src
all: vtplay libterm.a
$(VOBJ): %.o: %.c
	$(CC) $(VFLAGS) -c -o $@ $<
term.o: term.c term.h
	$(CC) $(CFLAGS) -c -o $@ $<
draw4.o: draw4.s
	$(CC) -m68020 -c -o $@ $<
libterm.a: term.o draw4.o $(VOBJ)
	rm -f $@; $(AMIGA_GCC:gcc=ar) rcs $@ $^
vtplay: vtplay.c libterm.a
	$(CC) $(CFLAGS) -o $@ vtplay.c libterm.a
clean:
	rm -f *.o libterm.a vtplay
