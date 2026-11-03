# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
# SPDX-License-Identifier: GPL-2.0-or-later
# pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
# in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
# amiga/panoflash/Makefile -- panoflash.device (entry.s, panoflash.c, the 68020 CRC32 of amiga/tools/ftdimon) with
# m68k-amigaos-gcc 6.5.0b (bebbo amiga-gcc).  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
# Install: panoflash.device to DEVS:.  entry.s is linked first: the segment starts with its moveq #-1/rts.
TOOLS   ?= $(HOME)/opt/amiga/bin
CC       = $(TOOLS)/m68k-amigaos-gcc
STRIP    = $(TOOLS)/m68k-amigaos-strip
FTDIMON ?= ../tools/ftdimon
CFLAGS   = -m68020 -O2 -Wall -noixemul -fomit-frame-pointer -fno-builtin -ffreestanding -fno-strict-aliasing -I$(FTDIMON)
LDFLAGS  = -m68020 -noixemul -nostartfiles -nostdlib

all: panoflash.device

panoflash.device: entry.o panoflash.o crc32.o
	$(CC) $(LDFLAGS) entry.o panoflash.o crc32.o -lgcc -o $@.debug
	$(STRIP) $@.debug -o $@

entry.o: entry.s
	$(CC) -m68020 -c $< -o $@

panoflash.o: panoflash.c panoflash.h $(FTDIMON)/crc32.h
	$(CC) $(CFLAGS) -c $< -o $@

crc32.o: $(FTDIMON)/crc32.c $(FTDIMON)/crc32.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f entry.o panoflash.o crc32.o panoflash.device panoflash.device.debug FlashCLI

# FlashCLI: the shell tool on the device
all: FlashCLI
FlashCLI: flashcli.c panoflash.h $(FTDIMON)/crc32.c $(FTDIMON)/crc32.h
	$(CC) -m68020 -O2 -Wall -noixemul -I$(FTDIMON) -o $@ flashcli.c $(FTDIMON)/crc32.c
