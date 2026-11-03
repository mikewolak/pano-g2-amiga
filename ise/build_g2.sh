#!/bin/bash
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# ise/build_g2.sh -- ISE 14.7 build of the Pano Logic G2 revision C fabric, XC6SLX100-2FGG484: synthesis, map,
# place and route, the timing gate (exit 97 when a constraint fails) and bitgen with the build id as UserID.
# Mike Wolak, EPROM Foundry Inc.
#
# The Amiga fabric of this release is G2_AMIGA_AGA=1: the Amiga subsystem (amiga/rtl/g2/amiga_g2.v) with the 68020
# and the AGA chipset (amiga/rtl/gen/nanomig_020.v and the TG68K's VHDL), ise/pano_g2_amiga_aga.prj and .ucf, XST
# defines G2_AMIGA and G2_AMIGA_AGA, the 68000 microcode memories (amiga/rtl/gen/*.mem) and sysctrl.v's menu
# description (amiga_xml.hex) in the work directory, 32 KB of block RAM holding the loader fw/build/amiga_ldr.bin.
# The release fabric was placed with UCF_EXTRA=floorplan/amiga_chipset_ag.ucf and PAR cost table 7.
#
# The USB host module (g2_usb_host) is not part of this source release; without it the build makes a fabric with no
# USB host (pano_g2_top.v), which does not run the Amiga.
#
#   HASH=<git hash> G2_AMIGA_AGA=1 [UCF_EXTRA=floorplan/<file>.ucf] [MAP_OPT=...] [G2_FW=fw/build/<image>.bin] sh ise/build_g2.sh [map|bit]
#
[ -n "${BASH_VERSION:-}" ] || exec bash "$0" "$@"
: "${ISE_DIR:=/opt/Xilinx/14.7/ISE_DS}"
[ -f "$ISE_DIR/settings64.sh" ] || [ -f "$ISE_DIR/settings32.sh" ] || { echo "build_g2.sh: no ISE at $ISE_DIR"; exit 2; }
# shellcheck disable=SC1090
if [ -f "$ISE_DIR/settings64.sh" ]; then . "$ISE_DIR/settings64.sh" >/dev/null; else . "$ISE_DIR/settings32.sh" >/dev/null; fi
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
# the ISE licence: XILINX_LIC (default ~/.Xilinx/xilinx_ise_14.7_license.lic) when that file exists.  The XC6SLX100 is
# above the WebPack line: map refuses it without a licence.
: "${XILINX_LIC:=$HOME/.Xilinx/xilinx_ise_14.7_license.lic}"
if [ -f "$XILINX_LIC" ]; then XILINXD_LICENSE_FILE="$XILINX_LIC"; export XILINXD_LICENSE_FILE; fi
echo "== XILINXD_LICENSE_FILE=[${XILINXD_LICENSE_FILE:-}] =="
cd "$HERE" || exit 2
DESIGN=pano_g2; TOP=pano_g2_top; PART=xc6slx100-fgg484-2; MODE="${1:-}"
REVB=0; FW_DEFAULT=../../fw/build/netsh_g2.bin; MAIN_SLOT=380000; FLASH_KB=8192; FLASH_PART="8 MB M25P64"
if [ "${G2_REV:-C}" = "B" ]; then
    REVB=1; PART=xc6slx150-fgg484-2; FW_DEFAULT=../../fw/build/netsh_g2_revb.bin; MAIN_SLOT=480000; FLASH_KB=16384
    FLASH_PART="16 MB M25P128"
fi
SNES=0; RAM_WORDS=65536; AMIGA=0; SUB=snes
[ "${G2_AMIGA:-}" = "1" ] && { AMIGA=1; SUB=amiga; G2_SNES=1; }
# G2_AMIGA_AGA=1: the Amiga fabric with the 68020 and AGA (amiga/rtl/gen/nanomig_020.v and the TG68K's VHDL,
# ise/pano_g2_amiga_aga.prj and .ucf, XST define G2_AMIGA_AGA); implies G2_AMIGA=1
[ "${G2_AMIGA_AGA:-}" = "1" ] && { AMIGA=1; AGA=1; SUB=amiga_aga; G2_SNES=1; }
if [ "${G2_SNES:-}" = "1" ]; then
    SNES=1; RAM_WORDS=8192; FW_DEFAULT=../../fw/build/snes_ldr_revc.bin; MAIN_SLOT=380000
    [ "$REVB" = "1" ] && { FW_DEFAULT=../../fw/build/snes_ldr.bin; MAIN_SLOT=A00000; }
    # the Amiga fabric's block RAM holds fw/amiga/amiga_ldr.c (firmware at flash 0x680000, fw/amiga/amiga_flash.h)
    [ "$AMIGA" = "1" ] && FW_DEFAULT=../../fw/build/amiga_ldr.bin
fi
mkdir -p work_g2/xst/projnav.tmp; cd work_g2 || exit 2
UCF=../$DESIGN.ucf
if [ "$REVB" = "1" ]; then
    # The XC6SLX150 has no SLICE_X11Y191 (columns 10 and 11 end at row 189 on this die; on the XC6SLX100 at 191), so the
    # ULPI output area group, the same top-left corner beside ball A4, ends at row 189.
    sed 's/AREA_GROUP "AG_ulpi_out" RANGE = SLICE_X0Y176:SLICE_X11Y191;/AREA_GROUP "AG_ulpi_out" RANGE = SLICE_X0Y176:SLICE_X11Y189;/' \
        "../$DESIGN.ucf" > "${DESIGN}_revb.ucf"; UCF=${DESIGN}_revb.ucf
    grep -q 'SLICE_X0Y176:SLICE_X11Y189' "$UCF" || { echo "build_g2.sh: the ULPI area group was not rewritten for XC6SLX150"; exit 2; }
fi
if [ "$REVB" = "0" ]; then
    # The video output-stage area groups AG_out1/AG_out2 name the XC6SLX150 slices beside the heads' pins (IO tile columns
    # 58..74, slices X100..X125).  On the XC6SLX100 the same pins (F12..E16 top, Y15..AB21 bottom) are in IO tile columns
    # 41..55, beside slices X66..X89; the rows are the same.
    sed -e 's/AREA_GROUP "AG_out1" RANGE = SLICE_X100Y170:SLICE_X125Y187;/AREA_GROUP "AG_out1" RANGE = SLICE_X66Y170:SLICE_X89Y187;/' \
        -e 's/AREA_GROUP "AG_out2" RANGE = SLICE_X100Y4:SLICE_X125Y21;/AREA_GROUP "AG_out2" RANGE = SLICE_X66Y4:SLICE_X89Y21;/' \
        "$UCF" > "${DESIGN}_revc.ucf"; UCF=${DESIGN}_revc.ucf
    [ "$(grep -c -e 'SLICE_X66Y170:SLICE_X89Y187' -e 'SLICE_X66Y4:SLICE_X89Y21' "$UCF")" = "2" ] || { echo "build_g2.sh: the video output area groups were not rewritten for XC6SLX100"; exit 2; }
fi
if [ "$SNES" = "1" ]; then
    # the Ethernet receive clock has no buffer in the subsystem-slot fabric: its PERIOD, crossings and OFFSET IN are removed
    # u_vid2/olock_n: no net of that name after synthesis of this fabric (ngdbuild ConstraintSystem:59); its TIG is dropped
    # clk_cpu: the subsystem-slot fabric's CPU runs on clk_sys, so the clk_cpu group and its crossings do not exist; AG_cpu and
    # AG_ulpi_link: placement groups of the main fabric that the subsystem-slot fabric does not use; revision C
    # keeps AG_ulpi_link: without it the ULPI OFFSET IN paths into I_LC_TX_CTRL missed by 0.356 ns at 91 % slice use
    DROP_UL="-e AG_ulpi_link"; [ "$REVB" = "0" ] && DROP_UL=""
    grep -v -e '"eth_rxc"' -e 'BEFORE "gmii_rx_clk"' -e '"u_vid2/olock_n"' -e '"clk_cpu"' -e 'AG_cpu' $DROP_UL "$UCF" | cat - ../pano_g2_${SUB}.ucf > "${DESIGN}_${SUB}.ucf"; UCF=${DESIGN}_${SUB}.ucf
    echo "== G2_SNES=1: Ethernet receive-clock constraints removed, pano_g2_${SUB}.ucf appended =="
fi
if [ -n "${UCF_EXTRA:-}" ]; then
    # a floorplan or other constraint file appended to the UCF (path relative to ise/, e.g. floorplan/revb_cpu_ag.ucf)
    [ -f "../$UCF_EXTRA" ] || { echo "build_g2.sh: UCF_EXTRA ../$UCF_EXTRA is missing"; exit 2; }
    cat "$UCF" "../$UCF_EXTRA" > "${DESIGN}_extra.ucf"; UCF=${DESIGN}_extra.ucf
    echo "== UCF_EXTRA: $UCF_EXTRA appended =="
fi
if [ "$MODE" = "selftest" ]; then
    sed 's/8 ns HIGH 50%/1 ns HIGH 50%/' "$UCF" > "${DESIGN}_selftest.ucf"; UCF=${DESIGN}_selftest.ucf
    echo "== SELFTEST: planted 1 ns PERIOD on osc_clk -- this build MUST fail the timing gate =="
fi
echo "== build_g2.sh $DESIGN part=$PART top=$TOP mode=$MODE $(date '+%Y-%m-%d %H:%M:%S') =="
BUILD_ID=""
case "${HASH:-}" in ""|UNKNOWN) ;; *) BUILD_ID=$(printf '%s' "$HASH" | cut -c1-8) ;; esac
if [ -z "$BUILD_ID" ]; then
    cp "../$DESIGN.xst" "${DESIGN}_build.xst"; cp "../$DESIGN.ut" "${DESIGN}_build.ut"
    echo "== build id: NONE -- no hash from the harness =="
else
    sed "s/-define {/-define {BUILD_ID=32'h$BUILD_ID /" "../$DESIGN.xst" > "${DESIGN}_build.xst"
    sed "s/UserID:0x[0-9A-Fa-f]*/UserID:0x$BUILD_ID/" "../$DESIGN.ut" > "${DESIGN}_build.ut"
    echo "== build id: 0x$BUILD_ID (XST define BUILD_ID, bitgen UserID) =="
fi
if [ "$REVB" = "1" ]; then sed -i.bak "s/-define {/-define {G2_REVB /; s/^-p xc6slx100-2-fgg484/-p xc6slx150-2-fgg484/" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"; echo "== G2_REV=B: XC6SLX150, G2_REVB defined =="; fi
if [ "$SNES" = "1" ]; then
    cat ../pano_g2.prj ../pano_g2_${SUB}.prj > "${DESIGN}_build.prj"
    sed -i.bak "s/-define {/-define {G2_SNES /; s#^-ifn ../pano_g2.prj#-ifn ${DESIGN}_build.prj#" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"
    echo "== G2_SNES=1: G2_SNES defined, pano_g2_${SUB}.prj appended =="
    if [ "$AMIGA" = "1" ]; then
        sed -i.bak "s/-define {/-define {G2_AMIGA G2_DDRA_P1_EXT /" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"
        if [ "${AGA:-0}" = "1" ]; then sed -i.bak "s/-define {/-define {G2_AMIGA_AGA /" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"; echo "== G2_AMIGA_AGA=1: G2_AMIGA_AGA defined, pano_g2_amiga_aga.prj =="; fi
        cp ../../amiga/rtl/gen/*.mem ../../amiga/rtl/gen/*.hex . || { echo "== build_g2.sh FAILED at the amiga .mem/.hex copy =="; exit 1; }
        echo "== G2_AMIGA=1: G2_AMIGA and G2_DDRA_P1_EXT (chip A P1: the fast-RAM cache) defined, amiga/rtl/gen/*.mem and *.hex copied =="
    fi
fi
if [ "${G2_CPU:-}" = "nofd" ]; then
    # the VexiiRiscv core without F and D (rtl/vexiiriscv/VexiiRiscv_PanoG2_core_nofd.v, RV32IMAC, LSU ports 32 bits)
    # and the wrapper's G2CPU_LSU32 connection; the firmware of the subsystem-slot fabrics uses no floating point
    if ! grep -q -- "-ifn ${DESIGN}_build.prj" "${DESIGN}_build.xst"; then
        cp ../pano_g2.prj "${DESIGN}_build.prj"
        sed -i.bak "s#^-ifn ../pano_g2.prj#-ifn ${DESIGN}_build.prj#" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"
    fi
    sed -i.bak "s#rtl/vexiiriscv/VexiiRiscv_PanoG2_core.v#rtl/vexiiriscv/VexiiRiscv_PanoG2_core_nofd.v#" "${DESIGN}_build.prj" && rm -f "${DESIGN}_build.prj.bak"
    sed -i.bak "s/-define {/-define {G2CPU_LSU32 /" "${DESIGN}_build.xst" && rm -f "${DESIGN}_build.xst.bak"
    grep -q VexiiRiscv_PanoG2_core_nofd.v "${DESIGN}_build.prj" || { echo "== build_g2.sh FAILED: the nofd core is not in the project =="; exit 1; }
    echo "== G2_CPU=nofd: VexiiRiscv_PanoG2_core_nofd.v, G2CPU_LSU32 defined =="
fi
grep -- "-define" "${DESIGN}_build.xst"; grep -- "^-p " "${DESIGN}_build.xst"; grep "UserID" "${DESIGN}_build.ut"
# THE UNUSED-PIN OPTION IS FIXED AT PullDown.  Any other value takes a G2 off the network (the
# 88E1119R's strap balls read the pull state at reset and the PHY gives no CLK125).
grep -q '^-g UnusedPin:PullDown$' "${DESIGN}_build.ut" || { echo "== build_g2.sh REFUSED: ${DESIGN}_build.ut must carry exactly '-g UnusedPin:PullDown' =="; exit 2; }
fail() { echo "== build_g2.sh FAILED at $1 =="; exit 1; }

if [ "$MODE" != "bit" ]; then
    FW=${G2_FW:-$FW_DEFAULT}
    MK=../../tools/mkraminit/build/mkraminit
    [ -x "$MK" ] || make -s -C ../../tools/mkraminit >/dev/null 2>&1
    [ -x "$MK" ] || { echo "build_g2.sh: tools/mkraminit is not built and will not build"; exit 2; }
    [ -f "$FW" ] || { echo "build_g2.sh: $FW is missing"; exit 2; }
    [ "$(wc -c < "$FW")" -le $((RAM_WORDS * 4)) ] || { echo "build_g2.sh: $FW does not fit $((RAM_WORDS * 4)) bytes"; exit 2; }
    "$MK" -i "$FW" -o g2fw_ -d "$RAM_WORDS" || exit 2
    echo "firmware: $FW $(wc -c < "$FW") bytes sha256 $(sha256sum "$FW" | cut -c1-64) -> g2fw_{0,1,2,3}.hex, $RAM_WORDS words each"
    echo "== xst =="
    xst -intstyle xflow -ifn "${DESIGN}_build.xst" -ofn "$DESIGN.syr" || fail xst
    echo "== ngdbuild =="
    ngdbuild -intstyle xflow -dd _ngo -nt timestamp -uc "$UCF" -p "$PART" "$DESIGN.ngc" "$DESIGN.ngd" || fail ngdbuild
    # PAR_CT_LIST: cost tables, comma-separated, tried in turn on this synthesis (map places on Spartan-6), the first that meets timing
    # kept; without it PAR_CT alone (default 1)
    : "${PAR_CT:=1}"; : "${PAR_CT_LIST:=$PAR_CT}"
    # MAP_OPT overrides the optimisation switches (default "-logic_opt off -register_duplication off -global_opt off")
    : "${MAP_OPT:=-logic_opt off -register_duplication off -global_opt off}"; echo "== map options: $MAP_OPT =="
    for PAR_CT in ${PAR_CT_LIST//,/ }; do
    echo "== map, placement cost table $PAR_CT =="
    # shellcheck disable=SC2086
    map -intstyle xflow -p "$PART" -w $MAP_OPT -ol high -xe n -t "$PAR_CT" -xt 0 -r 4 \
        -mt 2 -ir off -pr b -lc off -power off -detail -o "${DESIGN}_map.ncd" "$DESIGN.ngd" "$DESIGN.pcf" || fail map
    echo "== map warnings =="; grep "WARNING:" "${DESIGN}_map.mrp"
    echo "== utilisation (map) =="
    grep -E "Number of Slice Registers|Number of Slice LUTs|Number of occupied Slices|Number of RAMB16BWERs|Number of RAMB8BWERs|Number of DSP48A1s|Number of BUFG/BUFGMUXs|Number of PLL_ADVs|Number of bonded IOBs|Number of ILOGIC2/ISERDES2s|Number of OLOGIC2/OSERDES2s" "${DESIGN}_map.mrp"
    [ "$MODE" = "map" ] && { echo "== build_g2.sh OK (map only) =="; exit 0; }
    echo "== par =="
    par -w -intstyle xflow -ol high -xe n -mt 4 "${DESIGN}_map.ncd" "$DESIGN.ncd" "$DESIGN.pcf" || fail par
    echo "== trce =="
    trce -intstyle xflow -v 3 -s 2 -n 3 -fastpaths -xml "$DESIGN.twx" "$DESIGN.ncd" -o "$DESIGN.twr" "$DESIGN.pcf" || fail trce
    echo "== timing: every constraint, worst slack =="
    grep -E "^ +TS_|^ *\* TS_|^ +\| |Autotimespec" "$DESIGN.par"
    grep -H -E "All constraints were met|constraints not met|Timing errors:|Score:" "$DESIGN.par" "$DESIGN.twr"
    grep -q "Timing errors: 0" "$DESIGN.twr" && break
    cp "$DESIGN.twr" "$DESIGN.ct$PAR_CT.twr"
    echo "== cost table $PAR_CT: timing not met =="
    done
    if ! grep -q "Timing errors: 0" "$DESIGN.twr"; then
        echo '== TIMING GATE: .twr does not say "Timing errors: 0" -- FAILED, no bitgen =='; exit 97
    fi
    if ! grep -qi "clk_sys_pll" "$DESIGN.par"; then
        echo '== CLOCK GATE: no derived PERIOD for the PLL output clk_sys -- FAILED, no bitgen =='; exit 97
    fi
    echo "== cost table $PAR_CT met timing =="
    echo "== TIMING GATE: passed =="
    [ "$MODE" = "selftest" ] && { echo "== SELFTEST BROKEN: planted failure passed the timing gate =="; exit 96; }
fi
echo "== bitgen =="
bitgen -intstyle xflow -f "${DESIGN}_build.ut" "$DESIGN.ncd" || fail bitgen
SYNC=$(od -An -tx1 -N64 -v "$DESIGN.bin" | tr -d ' \n' | grep -bo 'aa995566' | head -1 | cut -d: -f1)
SIZE=$(wc -c < "$DESIGN.bin")
echo "raw image $DESIGN.bin: $SIZE bytes, sync word at offset $(( ${SYNC:-(-2)} / 2 )), sha256 $(sha256sum "$DESIGN.bin" | cut -c1-64)"
[ -n "$SYNC" ] || { echo "raw image: NO SYNC WORD"; fail bin; }
REGION=$((0x340000)); [ "$REVB" = "1" ] && REGION=$((0x440000))   # revision B subsystem slot 0xA00000-0xE3FFFF: also 0x440000
[ "$SIZE" -le "$REGION" ] || { echo "raw image: LARGER THAN THE MULTIBOOT REGION $(printf 0x%X "$REGION")"; fail bin; }
echo "== promgen (SPI .mcs, the image at 0x$MAIN_SLOT of the $FLASH_PART) =="
promgen -w -p mcs -c FF -s "$FLASH_KB" -o "$DESIGN.mcs" -u "$MAIN_SLOT" "$DESIGN.bit" -spi || fail promgen
ls -l "$DESIGN.bit" "$DESIGN.bin" "$DESIGN.mcs"
echo "== build_g2.sh OK =="
