# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# fw/amiga/Makefile -- the firmware of the G2 Amiga fabric, revision C (fw/amiga/amiga_flash.h).  Outputs in fw/build:
#   amiga_ldr.bin   the block-RAM loader (fw/amiga/amiga_ldr.c, fw/snes/link_snes_ldr.ld, 32 KB): ise/build_g2.sh
#                   G2_AMIGA=1 puts it in the fabric's block RAM
#   amiga_g2.bin    the Amiga firmware, linked in DDR2 chip A at AMIGA_ORIGIN (fw/link_ddr.ld)
#   amiga_g2.img    the loader (-DAMIGA_BOOT, amiga_main.c THE LOADER) with a tools/panoldr header (load = entry =
#                   AMIGA_ORIGIN), for flash 0x680000: it starts \amiga\AMIGA.FW from the stick at every power-up
#   AMIGA.FW        the firmware that runs the Amiga, with the SHARE: volume's server (share.c, -DAMIGA_SHARE), a
#                   tools/panoldr image for \amiga\AMIGA.FW on the stick; not bound by the 256 KB flash region
# Same toolchain and flags as fw/snes/Makefile G2_REV=C (riscv64-unknown-elf-, newlib nano).
PREFIX ?= riscv64-unknown-elf-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size
B       = ../build
ARCH    = -march=rv32im_zicsr_zifencei -mabi=ilp32
LDRMAX  = 0x40000
CFLAGS  = $(ARCH) -Os -g -fno-common -ffunction-sections -fdata-sections -Wall -Wextra -std=gnu99 -DPANO_G2 -DG2_SNES
LDBASE  = $(ARCH) --specs=nano.specs -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
COMMON  = ../start.S ../trap.c ../syscalls.c ../pmem.c

AMIGA_ORIGIN = 0x10000000

LDR_SRC = amiga_ldr.c ../g2.c ../spiflash.c ../crc32.c

.PHONY: all amiga_ldr amiga_g2 amiga_g2_sim amiga_fw
all: amiga_ldr amiga_g2 amiga_fw

# the network console: fw/netsh.c and its drivers, lwIP 2.2.0 (fw/third_party/lwip, BSD-3-Clause) as the G2
# fw/Makefile builds it
LWIP    = ../third_party/lwip
NET_SRC = ../netsh.c ../netif_pano.c ../ethmac.c ../ethphy.c ../netbulk.c ../ntptime.c \
          $(LWIP)/src/core/def.c $(LWIP)/src/core/inet_chksum.c $(LWIP)/src/core/init.c $(LWIP)/src/core/ip.c \
          $(LWIP)/src/core/mem.c $(LWIP)/src/core/memp.c $(LWIP)/src/core/netif.c $(LWIP)/src/core/pbuf.c \
          $(LWIP)/src/core/sys.c $(LWIP)/src/core/tcp.c $(LWIP)/src/core/tcp_in.c $(LWIP)/src/core/tcp_out.c \
          $(LWIP)/src/core/timeouts.c $(LWIP)/src/core/udp.c $(LWIP)/src/core/raw.c $(LWIP)/src/core/stats.c $(LWIP)/src/core/dns.c \
          $(LWIP)/src/core/ipv4/dhcp.c $(LWIP)/src/core/ipv4/etharp.c $(LWIP)/src/core/ipv4/icmp.c \
          $(LWIP)/src/core/ipv4/ip4.c $(LWIP)/src/core/ipv4/ip4_addr.c $(LWIP)/src/core/ipv4/ip4_frag.c \
          $(LWIP)/src/netif/ethernet.c $(LWIP)/port/sys_arch.c
NET_INC = -I$(LWIP)/src/include -I$(LWIP)/port
FW_ID  := 0x$(shell git rev-parse --short=8 HEAD 2>/dev/null || echo 0)
# $(B)/fw_id holds FW_ID and is rewritten only when it changes, so a new commit rebuilds the firmware images
$(shell mkdir -p $(B); [ "$$(cat $(B)/fw_id 2>/dev/null)" = "$(FW_ID)" ] || echo "$(FW_ID)" > $(B)/fw_id)

# Amiga file systems inside the images: fw/amiga/afs.c on ADFlib 0.10.7 (adflib/, LGPL-2.1)
ADF     = ../../adflib/src
ADF_SRC = afs.c xsurf.c $(filter-out $(ADF)/adf_dev_driver_nativ.c,$(wildcard $(ADF)/*.c))

FW_SRC  = amiga_main.c setup.c ../snes/snes_audio.c $(ADF_SRC) \
          ../usb.c ../usb_ehci.c ../usbmsd.c ../diskio_msd_g2.c ../usbhid.c ../usbmidi.c ../usbftdi.c ../usbpad.c \
          ../vga.c ../vgacon.c ../ch7301.c ../codec_g2.c ../g2.c ../spiflash.c ../crc32.c ../timer_ms.c \
          ../../fatfs/source/ff.c ../../fatfs/source/ffunicode.c $(NET_SRC)
FW_DEF  = -DPANO_G2_DDR -DNETIF_PANO_MII -DUSB_EHCI -DAUD_WIDE -DAUD_IN_HZ=48000u -DAUD_COEF_TABLE=\"aud_coef_48000.h\" -DFW_ID=$(FW_ID)u -I.. -I../../fatfs/source -I$(ADF) $(NET_INC)

amiga_g2: $(B)/amiga_g2.img
$(B)/amiga_g2.elf: $(FW_SRC) amiga_regs.h amiga_flash.h ../snes/snes_audio.h ../snes/aud_coef_48000.h ../snes/snes_regs.h ../link_ddr.ld $(COMMON) $(B)/fw_id | $(B)
	$(CC) $(CFLAGS) $(FW_DEF) -DAMIGA_BOOT $(LDBASE) -Wl,-T,../link_ddr.ld -Wl,--defsym=__ddr_origin=$(AMIGA_ORIGIN) \
	      -Wl,-Map,$(B)/amiga_g2.map -o $@ $(COMMON) $(FW_SRC) -lm
	$(SIZE) $@
$(B)/amiga_g2.bin: $(B)/amiga_g2.elf
	$(OBJCOPY) -O binary $< $@
$(B)/amiga_g2.img: $(B)/amiga_g2.bin ../../tools/panoldr/build/mkpanoldr
	../../tools/panoldr/build/mkpanoldr -i $< -o $@ -l $(AMIGA_ORIGIN) -e $(AMIGA_ORIGIN) -m $(LDRMAX)
../../tools/panoldr/build/mkpanoldr: ../../tools/panoldr/mkpanoldr.c ../../tools/panoldr/panoldr.h
	$(MAKE) -C ../../tools/panoldr

# the stick firmware: no flash-region limit (FW_STAGE_MAX in amiga_main.c, 8 MB)
amiga_fw: $(B)/AMIGA.FW
$(B)/amiga_fw.elf: $(FW_SRC) share.c share.h pflash.c pflash.h sock.c sock.h ../third_party/lwip/port/lwipopts.h amiga_regs.h amiga_flash.h ../snes/snes_audio.h ../snes/aud_coef_48000.h ../snes/snes_regs.h ../link_ddr.ld $(COMMON) $(B)/fw_id | $(B)
	$(CC) $(CFLAGS) $(FW_DEF) -DAMIGA_SHARE -DAMIGA_PFLASH -DAMIGA_SOCK $(LDBASE) -Wl,-T,../link_ddr.ld -Wl,--defsym=__ddr_origin=$(AMIGA_ORIGIN) \
	      -Wl,-Map,$(B)/amiga_fw.map -o $@ $(COMMON) $(FW_SRC) share.c pflash.c sock.c -lm
	$(SIZE) $@
$(B)/amiga_fw.bin: $(B)/amiga_fw.elf
	$(OBJCOPY) -O binary $< $@
$(B)/AMIGA.FW: $(B)/amiga_fw.bin ../../tools/panoldr/build/mkpanoldr
	../../tools/panoldr/build/mkpanoldr -i $< -o $@ -l $(AMIGA_ORIGIN) -e $(AMIGA_ORIGIN) -m 0x800000

amiga_ldr: $(B)/amiga_ldr.bin
$(B)/amiga_ldr.elf: $(LDR_SRC) amiga_flash.h ../snes/link_snes_ldr.ld $(COMMON) | $(B)
	$(CC) $(CFLAGS) $(LDBASE) -Wl,-T,../snes/link_snes_ldr.ld -Wl,-Map,$(B)/amiga_ldr.map -o $@ $(COMMON) $(LDR_SRC)
	$(SIZE) $@
$(B)/amiga_ldr.bin: $(B)/amiga_ldr.elf
	$(OBJCOPY) -O binary $< $@
$(B):
	mkdir -p $(B)

# amiga_g2_sim: the firmware for the whole-SoC simulation (rtl/sim g2-soc-amiga): no video encoder, codec or USB;
# FatFs on a 4 MB RAM disk in DDR2 chip A (fw/snes/diskio_ram.c).  Image amiga_g2_sim.img (PANOLDR, for flash
# AMIGA_FLASH_FW 0x680000 in the model).
SIM_SRC = amiga_main.c ../snes/snes_audio.c ../snes/diskio_ram.c $(ADF_SRC) \
          ../usb.c ../usb_ehci.c ../usbmsd.c ../usbhid.c ../usbmidi.c ../usbftdi.c ../usbpad.c \
          ../vga.c ../vgacon.c ../ch7301.c ../codec_g2.c ../g2.c ../spiflash.c ../crc32.c ../timer_ms.c \
          ../../fatfs/source/ff.c ../../fatfs/source/ffunicode.c $(NET_SRC)
amiga_g2_sim: $(B)/amiga_g2_sim.img
$(B)/amiga_g2_sim.elf: $(SIM_SRC) amiga_regs.h amiga_flash.h ../snes/snes_audio.h ../snes/aud_coef_48000.h ../snes/snes_regs.h ../link_ddr.ld $(COMMON) $(B)/fw_id | $(B)
	$(CC) $(CFLAGS) $(FW_DEF) -DAMIGA_SIM -DRAMDISK_BYTES=0x00400000u $(LDBASE) -Wl,-T,../link_ddr.ld -Wl,--defsym=__ddr_origin=$(AMIGA_ORIGIN) \
	      -Wl,-Map,$(B)/amiga_g2_sim.map -o $@ $(COMMON) $(SIM_SRC) -lm
	$(SIZE) $@
$(B)/amiga_g2_sim.bin: $(B)/amiga_g2_sim.elf
	$(OBJCOPY) -O binary $< $@
$(B)/amiga_g2_sim.img: $(B)/amiga_g2_sim.bin ../../tools/panoldr/build/mkpanoldr
	../../tools/panoldr/build/mkpanoldr -i $< -o $@ -l $(AMIGA_ORIGIN) -e $(AMIGA_ORIGIN) -m $(LDRMAX)
