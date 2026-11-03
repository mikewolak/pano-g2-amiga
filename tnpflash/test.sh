#!/bin/sh
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# tnpflash/test.sh -- `make test': tnpflash against build/tnpsim, a stand-in for a stock G2 revision C on 127.0.0.1
# (TNPSIM_G2C=1).  discover, info, backup, the two refusals of g2write, g2write and verify; then the simulator's flash
# is compared with what g2write must leave: the backup outside the erased sectors, the image inside them.
# Mike Wolak, EPROM Foundry Inc.
set -u
T=build/t; mkdir -p $T; rm -f $T/*
B=build/tnpflash; ok=0; bad=0
pass() { echo "PASS  $1"; ok=$((ok + 1)); }
fail() { echo "FAIL  $1"; bad=$((bad + 1)); }
$CC -std=c99 -O2 -Wall -Wextra -o build/mkimg test/mkimg.c || exit 1
build/mkimg flash $T/flash.bin && build/mkimg slot $T/slot.bin 262144 && build/mkimg slot $T/nosync.bin 4096 || exit 1
printf '\000\000\000\000' | dd of=$T/nosync.bin bs=1 seek=16 conv=notrunc 2>/dev/null
TNPSIM_G2C=1 TNPSIM_IMAGE=$T/flash.bin TNPSIM_DUMP=$T/dump.bin build/tnpsim 2> $T/sim.log &
SIM=$!; sleep 1
trap 'kill $SIM 2>/dev/null' EXIT
$B discover -b 127.0.0.1 -t 2 > $T/discover.txt 2>&1 && grep -q '00-1c-02-63-71-62' $T/discover.txt && pass "discover" || fail "discover"
$B info 127.0.0.1 > $T/info.txt 2>&1 && grep -q 'g2write accepts this unit' $T/info.txt && pass "info: G2 revision C" || fail "info"
$B backup 127.0.0.1 $T/backup.bin > $T/backup.txt 2>&1 && cmp -s $T/backup.bin $T/flash.bin && pass "backup: 8 MB read twice, equal to the device" || fail "backup"
$B g2write 127.0.0.1 $T/backup.bin $T/nosync.bin > $T/w1.txt 2>&1; [ $? -ne 0 ] && grep -q 'no sync word' $T/w1.txt && pass "g2write refuses an image without a sync word" || fail "g2write without sync word"
cp $T/backup.bin $T/other.bin; printf 'X' | dd of=$T/other.bin bs=1 seek=4096 conv=notrunc 2>/dev/null
$B g2write 127.0.0.1 $T/other.bin $T/slot.bin > $T/w2.txt 2>&1; [ $? -ne 0 ] && grep -q 'header sector differs' $T/w2.txt && pass "g2write refuses a backup of another unit" || fail "g2write with a foreign backup"
$B g2write 127.0.0.1 $T/backup.bin $T/slot.bin > $T/w3.txt 2>&1 && grep -q 'written and verified' $T/w3.txt && pass "g2write: 262,144 bytes at 0x380000" || fail "g2write"
$B verify 127.0.0.1 $T/slot.bin@0x380000 > $T/v.txt 2>&1 && grep -q IDENTICAL $T/v.txt && pass "verify" || fail "verify"
sleep 1
dd if=$T/dump.bin of=$T/d_lo.bin bs=65536 count=56 2>/dev/null; dd if=$T/flash.bin of=$T/f_lo.bin bs=65536 count=56 2>/dev/null
dd if=$T/dump.bin of=$T/d_mid.bin bs=65536 skip=56 count=4 2>/dev/null
dd if=$T/dump.bin of=$T/d_hi.bin bs=65536 skip=60 2>/dev/null; dd if=$T/flash.bin of=$T/f_hi.bin bs=65536 skip=60 2>/dev/null
cmp -s $T/d_lo.bin $T/f_lo.bin && cmp -s $T/d_mid.bin $T/slot.bin && cmp -s $T/d_hi.bin $T/f_hi.bin \
    && pass "flash after g2write: header, golden image and configuration table unchanged; slot holds the image" || fail "flash contents after g2write"
echo "$ok passed, $bad failed"
[ $bad -eq 0 ]
