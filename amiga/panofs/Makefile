# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# amiga/panofs/Makefile -- PANOFS (panofs.s), the Amiga side of the firmware's volume handshake.
# vasm 2 (vasmm68k_mot) writes the AmigaDOS hunk executable directly.  The include files are those of the AmigaOS
# 3.2 NDK (Hyperion Entertainment; Aminet dev/misc/NDK3.2.lha), not in this repository: NDK=<its directory>.
#   make NDK=$HOME/Amiga/NDK3.2        build/PANOFS
VASM ?= vasmm68k_mot
NDK  ?= $(HOME)/Amiga/NDK3.2
B     = build

$(B)/PANOFS: panofs.s Makefile | $(B)
	$(VASM) -Fhunkexe -nosym -quiet -I$(NDK)/Include_I -o $@ panofs.s
	@ls -l $@
$(B):
	mkdir -p $(B)
.PHONY: clean
clean:
	rm -rf $(B)
