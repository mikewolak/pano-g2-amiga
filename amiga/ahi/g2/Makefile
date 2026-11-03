# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
# Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
#
#  Makefile for the g2.audio AHI driver (Toccata Zorro II card, G2 NanoMig core).
#  Rules follow Drivers/Common/Makefile.common.in of AHI 6.0, without autoconf.
#

TOOLS      ?= $(HOME)/opt/amiga/bin
PREFIX_T    = $(TOOLS)/m68k-amigaos-
CC          = $(PREFIX_T)gcc
AS          = $(PREFIX_T)as
OBJCOPY     = $(PREFIX_T)objcopy
STRIP       = $(PREFIX_T)strip
SFDC        = $(TOOLS)/sfdc
HOST        = m68k-amigaos

DRIVER      = g2.audio
MODEFILE    = G2

SFD         = include/SFD/ahi_sub_lib.sfd

CPPFLAGS    = -I. -Icommon -Igen -Iinclude -DDRIVER="\"$(DRIVER)\""
CFLAGS      = -m68020 -Os -Wall -noixemul -fomit-frame-pointer \
              -fno-builtin -ffreestanding -fno-strict-aliasing
LDFLAGS     = -m68020 -noixemul -nostartfiles -nostdlib
LIBS        = -lamiga -lgcc

OBJECTS     = library.o gatestubs.o g2-init.o g2-main.o g2-playslave.o \
              g2-accel.o g2-hw.o
GEN         = gen/version.h gen/gatestubs.c gen/gatestubs.h \
              gen/proto/ahi_sub.h gen/inline/ahi_sub.h gen/clib/ahi_sub_protos.h

VPATH       = common:gen

.PHONY: all clean

all: $(DRIVER) $(MODEFILE)

clean:
	rm -rf $(DRIVER) $(DRIVER).debug $(MODEFILE) *.o gen

$(DRIVER): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@.debug
	$(STRIP) $@.debug -o $@

%.o: %.c $(GEN) DriverData.h toccata.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

gatestubs.o: gen/gatestubs.c $(GEN)
	$(CC) $(CFLAGS) $(CPPFLAGS) -D__CLIB_TYPES__ -c $< -o $@

$(MODEFILE): $(MODEFILE).o
	$(OBJCOPY) --output-target=binary $< $@

$(MODEFILE).o: common/MODEFILE.s $(MODEFILE).s
	cat $^ > gen/modefile-all.s
	$(AS) gen/modefile-all.s -o $@

gen/gatestubs.c: $(SFD)
	@mkdir -p gen
	$(SFDC) --mode=gatestubs --gateprefix=gw --libprefix=_ --libarg=last \
	        --addvectors=library --target=$(HOST) $< -o $@

gen/gatestubs.h: $(SFD)
	@mkdir -p gen
	$(SFDC) --mode=gateproto --gateprefix=gw --libprefix=_ --libarg=last \
	        --addvectors=library --target=$(HOST) $< -o $@

gen/proto/ahi_sub.h: $(SFD)
	@mkdir -p gen/proto
	$(SFDC) --mode=proto --target=$(HOST) $< -o $@

gen/inline/ahi_sub.h: $(SFD)
	@mkdir -p gen/inline
	$(SFDC) --mode=macros --target=$(HOST) $< -o $@

gen/clib/ahi_sub_protos.h: $(SFD)
	@mkdir -p gen/clib
	$(SFDC) --mode=clib --target=$(HOST) $< -o $@

gen/version.h: version.ver version.rev version.date
	@mkdir -p gen
	@ver=`cat version.ver` rev=`cat version.rev` date=`cat version.date` && \
	 echo "#define VERSION $$ver" > $@ && \
	 echo "#define REVISION $$rev" >> $@ && \
	 echo "#define VERS \"$$ver.$$rev ($$date)\"" >> $@
