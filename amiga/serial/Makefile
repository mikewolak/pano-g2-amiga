# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# amiga/serial/Makefile -- g2serial.device (entry.s, g2serial.c) with m68k-amigaos-gcc 6.5.0b (bebbo amiga-gcc).
# Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  New implementation from a functional analysis of BaudBandit.device 1.4b.
# Install: g2serial.device to DEVS:.  entry.s is linked first: the segment starts with its moveq #-1/rts.
TOOLS   ?= $(HOME)/opt/amiga/bin
CC       = $(TOOLS)/m68k-amigaos-gcc
STRIP    = $(TOOLS)/m68k-amigaos-strip
CFLAGS   = -m68020 -O2 -Wall -noixemul -fomit-frame-pointer -fno-builtin -ffreestanding -fno-strict-aliasing -fno-function-cse
LDFLAGS  = -m68020 -noixemul -nostartfiles -nostdlib

all: g2serial.device

g2serial.device: entry.o g2serial.o
	$(CC) $(LDFLAGS) entry.o g2serial.o -lgcc -o $@.debug
	$(STRIP) $@.debug -o $@

entry.o: entry.s
	$(CC) -m68020 -c $< -o $@

g2serial.o: g2serial.c g2serial.h
	$(CC) $(CFLAGS) -c $< -o $@

g2serial.s: g2serial.c g2serial.h
	$(CC) $(CFLAGS) -S $< -o $@

clean:
	rm -f entry.o g2serial.o g2serial.s g2serial.device g2serial.device.debug
