# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
# SPDX-License-Identifier: GPL-2.0-or-later
# pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
# in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
# flashmon: the G2's configuration flash through panoflash.device -- part, map, read, write, erase, CRC32, backups,
# bitstream extraction (.bit, .bin), hex editor.  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
GIT_HASH  ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
FTDIMON   ?= ../ftdimon
PANOFLASH ?= ../../panoflash
CFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -Wall -I. -I$(FTDIMON) -I$(PANOFLASH) -DGIT_HASH=\"$(GIT_HASH)\"
SRC       = flashmon.c $(FTDIMON)/font8x8.c $(FTDIMON)/crc32.c
flashmon: $(SRC) $(FTDIMON)/font8x8.h $(FTDIMON)/crc32.h $(PANOFLASH)/panoflash.h
	$(AMIGA_GCC) $(CFLAGS) -o $@ $(SRC) -lm
clean:
	rm -f flashmon
