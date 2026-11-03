# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# tnpflash/Makefile -- tnpflash for macOS, Linux and Windows.  Mike Wolak, EPROM Foundry Inc.
#   make            build/tnpflash (the host's C compiler)
#   make win64      build/tnpflash.exe (64-bit Windows, MinGW-w64 x86_64-w64-mingw32-gcc)
#   make win32      build/tnpflash32.exe (32-bit Windows, MinGW-w64 i686-w64-mingw32-gcc)
#   make test       build/tnpsim, a stand-in device on 127.0.0.1, and tnpflash discover, info and backup against it
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra
SRC      = main.c tnp.c stream.c flash.c sha256.c plat.c bitfile.c
HDR      = tnp.h stream.h flash.h sha256.h plat.h bitfile.h
MINGW64 ?= x86_64-w64-mingw32-gcc
MINGW32 ?= i686-w64-mingw32-gcc

all: build/tnpflash
build:
	mkdir -p build
build/tnpflash: $(SRC) $(HDR) | build
	$(CC) $(CFLAGS) -o $@ $(SRC)
win64: build/tnpflash.exe
build/tnpflash.exe: $(SRC) $(HDR) | build
	$(MINGW64) -std=c99 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -o $@ $(SRC) -lws2_32 -static
win32: build/tnpflash32.exe
build/tnpflash32.exe: $(SRC) $(HDR) | build
	$(MINGW32) -std=c99 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -o $@ $(SRC) -lws2_32 -static
build/tnpsim: sim.c tnp.c plat.c tnp.h plat.h | build
	$(CC) $(CFLAGS) -o $@ sim.c tnp.c plat.c
test: build/tnpflash build/tnpsim
	CC="$(CC)" sh test.sh
clean:
	rm -rf build
.PHONY: all win64 win32 test clean
