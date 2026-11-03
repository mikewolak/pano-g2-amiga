# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# midimon: MIDI on Paula's serial port, a loopback test, meters, a sender and a monitor (midimon.c).
# AMIGA_GCC: the cross compiler (default ~/opt/amiga/bin/m68k-amigaos-gcc).  GIT_HASH: the version in the About box.
AMIGA_GCC ?= $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
GIT_HASH  ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
midimon: midimon.c font8x8.c font8x8.h
	$(AMIGA_GCC) -std=gnu99 -noixemul -m68020 -O2 -Wall -DGIT_HASH=\"$(GIT_HASH)\" -o $@ midimon.c font8x8.c -lm
clean:
	rm -f midimon
