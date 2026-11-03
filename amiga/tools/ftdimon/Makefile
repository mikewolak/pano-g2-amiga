# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
# SPDX-License-Identifier: GPL-2.0-or-later
# pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
# in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
# ftdimon: the FT2232H on the G2's hub -- device, pins, JTAG, SPI flash, hex editor.  Mike Wolak, EPROM Foundry Inc.,
# pano-g2-amiga.  GPL-2.0-or-later (flashchips_tab.c is derived from flashrom).
# flashchips_tab.c is generated from amiga/third_party/flashrom by flashchips_gen.py (host python3).
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
PYTHON    ?= python3
GIT_HASH  ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
FR        ?= ../../third_party/flashrom
JTAG      ?= ../jtag
XFER      ?= ../xfer
CFLAGS    = -std=gnu99 -noixemul -m68020 -O2 -Wall -I. -I$(JTAG) -I$(XFER) -DGIT_HASH=\"$(GIT_HASH)\"
SRC       = ftdimon.c font8x8.c crc32.c link.c spi.c flashchips.c flashchips_tab.c $(JTAG)/mpsse.c $(JTAG)/jtag.c $(JTAG)/chain.c \
            $(XFER)/zio_amiga.c
ftdimon: $(SRC) font8x8.h crc32.h link.h spi.h flashchips.h $(JTAG)/xpc.h
	$(AMIGA_GCC) $(CFLAGS) -o $@ $(SRC) -lm
flashchips_tab.c: flashchips_gen.py $(FR)/flashchips.c $(FR)/include/flashchips.h
	$(PYTHON) flashchips_gen.py $(FR)/flashchips.c $(FR)/include/flashchips.h > $@
clean:
	rm -f ftdimon flashchips_tab.c
