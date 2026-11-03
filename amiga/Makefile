# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# amiga/Makefile -- the NanoMig 68000 core as XST-portable Verilog-2001 for the Pano G1.
#
# nanomig/ is upstream untouched; rtl/src/ holds the copies this project edits (amiga/VENDORED.md lists every
# edit); rtl/gen/*.v is what the simulators and XST consume, committed because the build host has no sv2v.
#
#   nanomig.v   module nanomig and every module under it: FX68K, the Minimig chipset (Agnus, Denise, Paula,
#               CIAs, Gary, Gayle and IDE), Amber              sv2v --top, then tools/v2001_fix.py
#
# CORE_DEFS are the defines of the Tang Nano 20K build (src/tang/nano20k/top.sv) that reach the core, with
# INFER_DPRAM for portable block RAM: ENABLE_TG68K, ENABLE_CACHE, ENABLE_AGA, DENISE_EBR off; FX68K only.
# ENABLE_RAM32 is on: the fast-RAM port addresses a 16 MB space, so the 8 MB setting is one 8 MB Zorro II board
# (without it, 4 MB and two boards taken from slow memory); amiga_g2.v caches that space in DDR2 chip A.
# ENABLE_RTC is on: the MSM6242 registers at $DC0000 in minimig.v, set through sysctrl.v command 10 (the firmware
# sends the NTP time; companion.v is generated with the same define).
# ENABLE_AT_MODEM is on: Paula's serial port ends in NanoMig's uart_port.v (rtl/src/uart_port.v, misc/io_fifo.v), which
# turns its bit stream into bytes on the core's own clock; rtl/g2/amiga_serbridge.v carries the bytes to the register
# page.  rtl/src/ide.v takes the sector buffer as
# two ide_dpram instances (upstream's form for vendor RAM) for every build, and rtl/src/ide_dpram.v supplies that
# RAM as an inferable true dual-port: upstream's INFER_DPRAM form writes each array from both sides in one process,
# which XST does not map to block RAM.
SV2V ?= sv2v
CORE_DEFS ?= INFER_DPRAM ENABLE_RTC ENABLE_RAM32 ENABLE_AT_MODEM
N    = nanomig/src
SRC  = rtl/src
GEN  = rtl/gen

CORE_SV = $(N)/fx68k/fx68k.sv $(N)/fx68k/fx68kAlu.sv $(N)/fx68k/uaddrPla.sv
# CORE_V: the files of src/nanomig.gprj below nanomig
CORE_V  = $(SRC)/nanomig.v $(N)/minimig/Amber.v \
          $(filter-out $(addprefix $(N)/minimig-aga/,cpu_cache_new.v dpram_be_1024x16.v bitplane_ram.v sprite_ram.v \
            cart.v dpram.v paula_floppy_ebr.v paula_floppy_dma_buf.v paula_floppy.v paula_floppy_fifo.v ide.v minimig.v \
            cpu_wrapper.v gary.v denise_colortable_ram_mf.v agnus_beamcounter.v agnus_bitplanedma.v minimig_sram_bridge.v \
            paula.v paula_uart.v), \
            $(wildcard $(N)/minimig-aga/*.v)) \
          $(SRC)/paula_floppy_ebr.v $(SRC)/paula_floppy_dma_buf.v $(SRC)/paula_floppy_fifo.v $(SRC)/ide.v $(SRC)/ide_dpram.v \
          $(SRC)/minimig.v $(SRC)/cpu_wrapper.v $(SRC)/gary.v $(SRC)/denise_colortable_ram_mf.v $(SRC)/agnus_beamcounter.v $(SRC)/agnus_bitplanedma.v $(SRC)/minimig_sram_bridge.v \
          $(SRC)/paula.v $(SRC)/paula_uart.v $(SRC)/uart_port.v $(N)/misc/io_fifo.v

all: $(GEN)/nanomig.v $(GEN)/companion.v

# nanomig_020.v: the core as NanoMig's 68020/AGA top level (src/tang/nano20k/top_020.sv) configures it: TG68K beside
# FX68K (ENABLE_TG68K; TG68KdotC_Kernel stays an instance: XST takes the VHDL of src/tg68k, the simulators
# src/../sim/TG68KdotC_Kernel.v), Denise's bitplane and sprite buffers in block RAM (DENISE_EBR), the TG68K at 14 MHz
# effective (CPU_SLOW14).  The AGA chipset itself is in every build; sysctrl.v's AGA parameter enables it.
# ENABLE_XSURF (the 68020 core only): the X-Surf 100 network board, amiga/third_party/minimig_eth/rtl/ethernet.v in
# rtl/src/minimig.v, its autoconfig entry in rtl/src/cpu_wrapper.v and its decode in rtl/src/gary.v.
# ENABLE_CACHE (the 68020 core only, as top_020.sv): MiSTer's cpu_cache_new.v between the TG68K and the fast-RAM
# port; CHIPRAM_CACHE stays off (rtl/g2/amiga_g2.v passes no turbo chip, so no chip-bus write reaches cached data).
# ENABLE_TOCCATA (the 68020 core only): the Toccata 16-bit sound card, third_party/fpga-toccata (MiSTer's copy of
# ranzbak's fpga-toccata), configured after the X-Surf; its output reaches amiga_g2.v as toccata_left/right.
# ENABLE_G2SER (the 68020 core only): the G2 serial FIFO board, rtl/src/g2ser_fifo.v, configured after the X-Surf; in
# FIFO mode rtl/g2/amiga_g2.v gives it the serial bridge in Paula's place.
CORE020_DEFS = $(CORE_DEFS) ENABLE_TG68K DENISE_EBR CPU_SLOW14 ENABLE_XSURF ENABLE_CACHE ENABLE_TOCCATA ENABLE_G2SER
TOCCATA_SV = $(addprefix third_party/fpga-toccata/,toccata_capture.sv toccata_fifo.sv toccata_volume.sv) $(SRC)/toccata.sv $(SRC)/toccata_playback.sv
CORE020_V = $(CORE_V) $(N)/minimig-aga/bitplane_ram.v $(N)/minimig-aga/sprite_ram.v $(SRC)/ethernet.v \
            $(SRC)/cpu_cache_new.v $(N)/minimig-aga/dpram.v $(N)/minimig-aga/dpram_be_1024x16.v $(SRC)/g2ser_fifo.v
$(GEN)/nanomig_020.v: $(CORE_SV) $(CORE020_V) $(TOCCATA_SV) tools/v2001_fix.py Makefile
	$(SV2V) --top=nanomig $(addprefix -D,$(CORE020_DEFS)) --write=$(@:.v=_raw.v) $(CORE_SV) $(CORE020_V) $(TOCCATA_SV)
	sed '/^[[:space:]]*`default_nettype/d' $(@:.v=_raw.v) > $(@:.v=_nt.v) && rm -f $(@:.v=_raw.v)
	python3 tools/v2001_fix.py $(@:.v=_nt.v) $@ && rm -f $(@:.v=_nt.v)

# companion.v: NanoMig's companion-side modules used by the G2 slot (rtl/g2/amiga_g2.v), unchanged: hid.v with
# amiga_keymap.v, sysctrl.v, osd_u8g2.v; sv2v (sysctrl.v carries SystemVerilog under ENABLE_RTC), then v2001_fix.py
COMP_V = $(N)/misc/hid.v $(N)/misc/amiga_keymap.v $(SRC)/sysctrl.v $(N)/misc/osd_u8g2.v
$(GEN)/companion.v: $(COMP_V) $(N)/misc/amiga_xml.hex tools/v2001_fix.py Makefile
	$(SV2V) -DENABLE_RTC --write=$(@:.v=_raw.v) $(COMP_V)
	sed '/^[[:space:]]*`default_nettype/d' $(@:.v=_raw.v) > $(@:.v=_nt.v) && rm -f $(@:.v=_raw.v)
	python3 tools/v2001_fix.py $(@:.v=_nt.v) $@ && rm -f $(@:.v=_nt.v)
	cp $(N)/misc/amiga_xml.hex $(GEN)/   # sysctrl.v's menu description ($$readmemh)

$(GEN)/nanomig.v: $(CORE_SV) $(CORE_V) tools/v2001_fix.py Makefile
	$(SV2V) --top=nanomig $(addprefix -D,$(CORE_DEFS)) --write=$(@:.v=_raw.v) $(CORE_SV) $(CORE_V)
	# nanomig.v and minimig.v place `default_nettype inside their module, which iverilog and XST refuse; sv2v
	# has already checked the declarations under none, so the directives are removed
	sed '/^[[:space:]]*`default_nettype/d' $(@:.v=_raw.v) > $(@:.v=_nt.v) && rm -f $(@:.v=_raw.v)
	python3 tools/v2001_fix.py $(@:.v=_nt.v) $@ && rm -f $(@:.v=_nt.v)
	cp $(N)/fx68k/microrom.mem $(N)/fx68k/nanorom.mem $(GEN)/

# every generated file must elaborate in both simulators before a build job is spent on it (suppressions as
# lint waivers: FX68K's BLKANDNBLK and INITIALDLY; --no-timing
# because Paula's UART writes its registers with #1 delays, which synthesis ignores)
lint: all
	iverilog -g2005 -o /dev/null -s nanomig $(GEN)/nanomig.v
	verilator --lint-only --no-timing -Wno-fatal -Wno-lint -Wno-style -Wno-BLKANDNBLK -Wno-INITIALDLY --top-module nanomig \
	  $(GEN)/nanomig.v
	python3 tools/xst_scan.py $(GEN)/nanomig.v
	python3 tools/xst_scan.py $(GEN)/companion.v

# ---- chip-memory latency study (sim/, work/sim): the core booting Kickstart from sim/mem_lat.v under Verilator ----
# Kickstart is not in the repository: KICK=<binary ROM image, 256 KB or 512 KB>.  LAT is the clocks of 85.5 MHz from
# the edge that takes the request to the edge that changes the read data (5 = upstream's controller), LAT2 the same for
# the CPU port alone (default LAT).  FRAMES frames.
R = $(or $(LAT),5)$(if $(LAT2),_$(LAT2))
KICK ?= kick.rom
work/sim/kick.hex: $(KICK)
	mkdir -p work/sim
	xxd -p -c2 $< > $@
# rtl/gen is committed: a clone has no sv2v and newer source timestamps, so the bench takes rtl/gen as it is
sim-boot-prep: work/sim/kick.hex
	cp $(GEN)/*.mem $(GEN)/*.hex work/sim/
sim-boot: sim-boot-prep
	cd work/sim && verilator --binary --timing -j 4 -O2 -Wno-fatal -Wno-lint -Wno-style -Wno-BLKANDNBLK -Wno-INITIALDLY \
	  -Wno-UNOPTFLAT --top-module tb_amiga_boot -DLAT=$(or $(LAT),5) $(if $(LAT2),-DLAT2=$(LAT2)) --Mdir obj_lat$(R) -o tb_lat$(R) \
	  ../../sim/tb_amiga_boot.v ../../sim/mem_lat.v ../../sim/sdc_model.v ../../$(GEN)/nanomig.v > build_lat$(R).log 2>&1
	cd work/sim && ./obj_lat$(R)/tb_lat$(R) +kick=kick.hex +frames=$(or $(FRAMES),20) $(if $(DUMP),+dump=$(DUMP)) $(if $(EVERY),+dumpevery=$(EVERY)) \
	  | tee boot_lat$(R).log | grep -v '^- '

clean:
	rm -f $(GEN)/*.v $(GEN)/*.mem

sim-clean:
	rm -rf work

# ---- G2 subsystem bench: rtl/g2/amiga_g2.v (gated core, chip cache) on the MCB port model, against the reference ----
# sim-g2-ref: tb_amiga_boot as the same machine (NTSC, turbo off); sim-g2: tb_amiga_g2; sim-g2-cmp: both, frame CRCs
# compared.  LAT_MAX: MCB model delay per command in clocks (default 12).  ADF=<image> mounts floppy drive 0, HDF=<image>
# (sim-g2 only) IDE drive 0;
# DUMP=<n> writes frame n as a PPM in both benches.
G2SIM = ../../sim/tb_amiga_g2.v ../../sim/bufgce_model.v ../../rtl/g2/amiga_g2.v ../../rtl/g2/amiga_cache.v \
        ../../../rtl/g2/sim/mcb_port_model.v ../../../rtl/g2/g2_uart.v ../../rtl/g2/amiga_serbridge.v ../../$(GEN)/companion.v ../../$(GEN)/nanomig.v
VFLAGS = --binary --timing -j 4 -O2 -Wno-fatal -Wno-lint -Wno-style -Wno-BLKANDNBLK -Wno-INITIALDLY -Wno-UNOPTFLAT
sim-g2-ref: sim-boot-prep
	cd work/sim && verilator $(VFLAGS) --top-module tb_amiga_boot -DLAT=5 -DTB_NTSC "-DTB_TURBO=3'b000" --Mdir obj_g2ref \
	  -o tb_g2ref ../../sim/tb_amiga_boot.v ../../sim/mem_lat.v ../../sim/sdc_model.v ../../$(GEN)/nanomig.v > build_g2ref.log 2>&1
	cd work/sim && ./obj_g2ref/tb_g2ref +kick=kick.hex +frames=$(or $(FRAMES),20) $(if $(ADF),+adf=$(ADF)) $(if $(DUMP),+dump=$(DUMP)) \
	  | grep -v '^- ' > g2ref.log
sim-g2: sim-boot-prep
	cd work/sim && verilator $(VFLAGS) --top-module tb_amiga_g2 -DLAT_MAX=$(or $(LAT_MAX),12) --Mdir obj_g2 -o tb_g2 \
	  $(G2SIM) > build_g2.log 2>&1
	cd work/sim && ./obj_g2/tb_g2 +kick=kick.hex +frames=$(or $(FRAMES),20) $(if $(ADF),+adf=$(ADF)) $(if $(HDF),+hdf=$(HDF)) $(if $(FWSEQ),+fwseq) $(if $(RTC),+rtc) $(if $(FAST),+fast=$(FAST) +fastcheck) $(if $(DUMP),+dump=$(DUMP)) $(if $(FBDUMP),+fbdump) \
	  | grep -v '^- ' > g2.log
sim-g2-cmp:
	$(MAKE) -j2 sim-g2-ref sim-g2
	cd work/sim && grep '^frame' g2ref.log | awk '{print $$1, $$2, $$3, $$4}' > g2ref.frames && \
	  grep '^frame' g2.log > g2.frames && tail -2 g2.log && \
	  if cmp -s g2ref.frames g2.frames; then echo "sim-g2-cmp: $$(wc -l < g2.frames) frames identical"; \
	  else echo "sim-g2-cmp: first difference: $$(cmp g2ref.frames g2.frames | head -1)"; exit 1; fi

# sim-g2-aga: tb_amiga_g2 with the G2_AMIGA_AGA subsystem (the 68020 core of rtl/gen/nanomig_020.v with its CPU cache,
# the X-Surf mailbox), +aga (the A1200 profile) and FAST (default 3, 8 MB).  The TG68K is NanoMig's simulation netlist
# of the VHDL (nanomig/sim/TG68KdotC_Kernel.v, module renamed) behind sim/tg68k_sim_wrap.v: the Verilog port in
# nanomig/src/tg68k/verilog skips the first extension word after a reset fetch, which the netlist and the unit do not.
# KHEX: the ROM (default kick.hex); sim/cachetest_rom.s assembled with m68k-amigaos-as is the cache test.
G2AGASIM = $(filter-out ../../$(GEN)/nanomig.v,$(G2SIM)) ../../$(GEN)/nanomig_020.v tg68k_ghdl.v ../../sim/tg68k_sim_wrap.v \
           ../../rtl/g2/xsurf_mbx.v
work/sim/tg68k_ghdl.v: nanomig/sim/TG68KdotC_Kernel.v
	mkdir -p work/sim && sed 's/^module TG68KdotC_Kernel$$/module TG68KdotC_Kernel_ghdl/' $< > $@
sim-g2-aga: sim-boot-prep work/sim/tg68k_ghdl.v
	cd work/sim && verilator $(VFLAGS) --top-module tb_amiga_g2 -DG2_AMIGA_AGA -DLAT_MAX=$(or $(LAT_MAX),12) --Mdir obj_g2aga \
	  -o tb_g2aga $(G2AGASIM) > build_g2aga.log 2>&1
	cd work/sim && ./obj_g2aga/tb_g2aga +kick=$(or $(KHEX),kick.hex) +aga +fast=$(or $(FAST),3) +fastcheck +frames=$(or $(FRAMES),20) \
	  $(if $(ADF),+adf=$(ADF)) $(if $(HDF),+hdf=$(HDF)) $(if $(DUMP),+dump=$(DUMP)) | grep -v '^- ' > g2aga.log

# sim-ide-reset: sim/ide_reset_rom.s (assembled into work/sim/ir.hex with m68k-amigaos-as) on the AGA bench with a
# 2 MB disk image whose block 0 is a RigidDiskBlock (work/sim/ir1m.hdf, amitools rdbtool create size=2M + init): the
# IDE status before and after the CPU's RESET instruction; PASS when both read 0x40 (ready) without a timeout
sim-ide-reset: sim-boot-prep work/sim/tg68k_ghdl.v
	cd work/sim && verilator $(VFLAGS) --top-module tb_amiga_g2 -DG2_AMIGA_AGA -DLAT_MAX=$(or $(LAT_MAX),12) --Mdir obj_g2aga \
	  -o tb_g2aga $(G2AGASIM) > build_g2aga.log 2>&1
	cd work/sim && ./obj_g2aga/tb_g2aga +kick=ir.hex +aga +fast=3 +hdf=ir1m.hdf +frames=40 +markend | grep -v '^- ' > ide_reset.log
	cd work/sim && grep -q 'mark $$000102 = 0040' ide_reset.log && grep -q 'mark $$000106 = 0040' ide_reset.log && \
	  grep -q 'mark $$000108 = 0000' ide_reset.log && echo "sim-ide-reset: PASS" || { echo "sim-ide-reset: FAIL"; exit 1; }

# LATS: the latencies compared with LAT 5 by sim/lat_sweep.sh, FRAMES frames each
sim-sweep: sim-boot-prep
	bash sim/lat_sweep.sh kick.hex $(or $(FRAMES),20) $(LATS)

.PHONY: all lint clean sim-boot-prep sim-boot sim-sweep sim-clean sim-g2-ref sim-g2 sim-g2-cmp sim-g2-aga sim-ide-reset
