#!/bin/sh
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# amiga/g2tools/mktar.sh -- G2TOOLS.TAR, the contents of the G2Tools floppy image: Install, ReadMe and the built
# programs in their AmigaOS directories, as a ustar archive for the firmware's `afs untar'.  The firmware makes the
# image on the unit:  adf new G2TOOLS.adf G2Tools;  df 3 G2TOOLS.adf;  afs untar G2TOOLS.TAR DF3:;  df 3 eject
#   sh amiga/g2tools/mktar.sh <directory holding C/ DEVS/ L/ of a build> <out.tar>
set -e
B=$1; OUT=$2; H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d)
mkdir -p "$T/C" "$T/L" "$T/DEVS/AHI" "$T/DEVS/AudioModes" "$T/DEVS/DOSDrivers"
for p in serialmon midimon ftdimon flashmon PanoTCP lsz lrz; do cp "$B/C/$p" "$T/C/"; done
cp "$H/../panofs/build/PANOFS" "$T/C/"
cp "$B/L/share-handler" "$T/L/"
cp "$B/DEVS/g2serial.device" "$B/DEVS/panoflash.device" "$T/DEVS/"
cp "$B/DEVS/AHI/g2.audio" "$T/DEVS/AHI/"; cp "$B/DEVS/AudioModes/G2" "$T/DEVS/AudioModes/"
cp "$H/../share/SHARE" "$T/DEVS/DOSDrivers/"
cp "$H/Install" "$H/ReadMe" "$T/"
(cd "$T" && COPYFILE_DISABLE=1 tar --format ustar -cf "$OUT" Install ReadMe C L DEVS)
rm -rf "$T"
