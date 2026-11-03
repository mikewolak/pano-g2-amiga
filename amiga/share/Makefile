# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the block transport and the changes; the a314fs code
# it is built on (Niklas Ekstrom, a314 repository) is CC0-1.0.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 AND CC0-1.0
# amiga/share/Makefile -- the SHARE: volume's handler (share-handler.c, a314fs.c with the HD0 block transport) with
# m68k-amigaos-gcc (bebbo amiga-gcc 6.5.0b).  Install: share-handler to L:, SHARE as DEVS:DOSDrivers/SHARE.
PREFIX ?= m68k-amigaos-
CC = $(PREFIX)gcc
share-handler: entry.s share-handler.c messages.h
	$(CC) -O2 -m68000 -Wall -nostartfiles -noixemul -o $@ entry.s share-handler.c
clean:
	rm -f share-handler
