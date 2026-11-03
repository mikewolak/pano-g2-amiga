# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
CC      ?= cc
CFLAGS  ?= -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -pedantic
B        = build

all: $(B)/mkpanoldr

$(B):
	mkdir -p $(B)

$(B)/mkpanoldr: mkpanoldr.c panoldr.h | $(B)
	$(CC) $(CFLAGS) -o $@ mkpanoldr.c

clean:
	rm -rf $(B)

.PHONY: all clean
