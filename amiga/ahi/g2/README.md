# g2.audio: AHI Driver for the Toccata Card of the G2 NanoMig Core

AHI sound driver (audio sub-library `DEVS:AHI/g2.audio` and mode file `DEVS:AudioModes/G2`) for the MacroSystem Toccata Zorro II card as implemented in the G2 NanoMig AGA core (68020). The driver programs the card registers directly and does not use `toccata.library`. Playback only. Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.

Status: runs on a G2 revision C unit (fabric 2b727c2, firmware 983268c, AHI 6.0, A1200 profile with the 68020): AHI Preferences lists the G2 modes and plays its test tone, and Play16 1.10 plays a 44.1 kHz 16-bit mono WAV (104,766 samples) from DH0 through mode 0x00470001 at 44,466 Hz to the end. Items not yet exercised are listed in Section 9.2.

---

## 1 Files

| File | Bytes | Origin | Licence |
|---|---|---|---|
| `common/library.c` | 13731 | AHI 6.0 `Drivers/Common/library.c`, Martin Blom, unmodified | AHI 6.0 source distribution; driver exception of `COPYING.DRIVERS` |
| `common/library.h` | 2765 | AHI 6.0 `Drivers/Common/library.h`, unmodified | as above |
| `common/DriverBase.h` | 1343 | AHI 6.0 `Drivers/Common/DriverBase.h`, unmodified | as above |
| `common/MODEFILE.s` | 1247 | AHI 6.0 `Drivers/Common/MODEFILE.s`, unmodified | as above |
| `include/devices/ahi.h` | 15499 | AHI 6.0 `Include/C/devices/ahi.h`, unmodified | (C) 1994-2005 Martin Blom |
| `include/libraries/ahi_sub.h` | 3948 | AHI 6.0 `Include/C/libraries/ahi_sub.h`, unmodified | (C) 1994-2005 Martin Blom |
| `include/SFD/ahi_sub_lib.sfd` | 1918 | AHI 6.0 `Include/SFD/ahi_sub_lib.sfd`, unmodified | (C) 1994-2005 Martin Blom |
| `COPYING.DRIVERS` | 332 | AHI 6.0, unmodified | driver licence exception |
| `g2-accel.c` | 3238 | AHI 6.0 `Drivers/Void/void-accel.c`, type name `VoidBase` renamed `G2Base` | Void driver: public domain (Martin Blom) |
| `g2-init.c` | 3336 | from `void-init.c`; board search, rate table, board reset added | EPROM Foundry Inc. |
| `g2-main.c` | 12942 | from `void-main.c`; hardware lock, rate selection, ring, card start and stop, output level | EPROM Foundry Inc. |
| `g2-playslave.c` | 4544 | from `void-playslave.c`; mixing into the ring, conversion of the buffer types | EPROM Foundry Inc. |
| `g2-hw.c` | 4745 | new; AD1848 access, FIFO refill, level 6 interrupt server | EPROM Foundry Inc. |
| `toccata.h` | 3854 | register offsets and control/status bits of NetBSD `sys/arch/amiga/dev/toccata.c` 1.22 | BSD-2-Clause, The NetBSD Foundation; notice retained |
| `DriverData.h` | 2446 | from Void `DriverData.h` | EPROM Foundry Inc. |
| `G2.s` | 1233 | mode file source, from Void `VOID.s` | EPROM Foundry Inc. |
| `config.h`, `version.*`, `Makefile` | 131, 13, 2665 | new; `Makefile` follows `Makefile.common.in` without autoconf | EPROM Foundry Inc. |
| `bin/g2.audio` | 4980 | build output, stripped | |
| `bin/G2` | 408 | build output, mode file | |

Byte counts of the hand-written files change with edits; `ls -l` gives the current values.

| Binary | SHA-256 |
|---|---|
| `bin/g2.audio` | `b04a6587c2e48c3c1c01c7688b5ad3accadfe1d7d13cebc23f83c33e3aac40b6` |
| `bin/G2` | `3540f3d5dad67e24c28de85eef0a5f604af741965e6b3b2c506d2d720f6884b4` |

The licence of the EPROM Foundry Inc. files is not yet stated. `COPYING.DRIVERS` permits any licence for an AHI driver.

## 2 Hardware Facts Used

Ground truth is the RTL in this repository: `amiga/third_party/fpga-toccata/toccata.sv`, `toccata_fifo.sv`, `toccata_volume.sv` (Paul Honig, BSD-2-Clause) and `amiga/rtl/src/toccata_playback.sv`.

| Item | Value | Source |
|---|---|---|
| Autoconfig | Zorro II, 64 KB, manufacturer 0x4754 (18260), product 12 | `rtl/src/cpu_wrapper.v`, ENABLE_TOCCATA |
| Board search | `FindConfigDev(NULL, 18260, 12)`, `cd_BoardAddr` | expansion.library V37 |
| Register select | address bits 14, 13, 11 | `toccata.sv` |
| Status read | board + 0x0000; bit 7 interrupt, active low; bit 3 playback half empty; cleared by the read | `toccata.sv` |
| Control write | board + 0x0000 | `toccata.sv` |
| FIFO | board + 0x2000 (0x2000-0x27FF), 1024 bytes, full at 1023 | `toccata_fifo.sv` |
| 16-bit FIFO write | D7:0 stored first, D15:8 second | `toccata.sv` |
| Sample assembly | low byte first, left then right | `toccata_playback.sv` |
| Half-empty interrupt | one edge as the fill falls through 504 after reaching 512 | `toccata_fifo.sv` |
| AD1848 index | board + 0x67FF, low byte lane | `toccata.sv` |
| AD1848 data | board + 0x6801, low byte lane | `toccata.sv` |
| AD1848 bits honoured | index 6/7 bits 5:0 attenuation (x 0.8409 per step) and bit 7 mute; index 8 bit 0 CSS, bits 3:1 CFS, bit 4 stereo, bit 5 companded, bit 6 16-bit; index 9 bits 0, 1, 3 | `toccata.sv`, `toccata_volume.sv` |
| Format change | a change of the stereo or companded bit resets the FIFO | `toccata_playback.sv` |
| Interrupt | level 6, `int6` of Paula, served as INTB_EXTER | `rtl/src/minimig.v` |
| Rate clock | 32-bit phase accumulator in core time, increment rate x 2^32 / f_core (28,636,364 Hz NTSC, 28,375,160 Hz PAL) | `rtl/src/toccata_playback.sv` |
| Core clock | 28.636364 MHz (NTSC, NCO reset value) or 28.375160 MHz (PAL) | `rtl/g2/amiga_g2.v` |

Consequence for byte order: a big-endian 16-bit sample word written to the FIFO as one 16-bit access enters it low byte first, the order the playback side expects. The ring therefore holds AHI's native big-endian samples and no byte exchange is performed.

## 3 Register Sequence

### 3.1 Driver initialisation (`g2-init.c`)

1. `FindConfigDev(NULL, 18260, 12)`; no board: requester, driver init fails, no modes are added.
2. Write 0x02 (reset) to board + 0.
3. Wait 500 ms with `Delay(25)` when the caller is a process.
4. Write 0x01 (active: FIFO reset, interrupt clear, DACs muted).

### 3.2 Start of playback (`g2-main.c`, `_AHIsub_Start`)

1. Allocate the mixing buffer (`ahiac_BuffSize`) and the ring (Section 4).
2. Start the slave process; it mixes until the ring reaches its high-water mark, then signals.
3. Write 0x01 to board + 0.
4. AD1848 index 0x48 (MCE + index 8), data 0x50 | rate bits; index 0x08; poll the index register while it reads 0x80 (bounded).
5. AD1848 index 6 and 7: attenuation of the current output level, mute set.
6. Write 0x11 (active + FIFO fill) to board + 0.
7. `AddIntServer(INTB_EXTER)`.
8. Write 768 bytes (3/4 FIFO) from the ring to board + 0x2000 as 16-bit writes.
9. Write 0x95 (active + codec + fill + playback interrupt) to board + 0; the card clears the DAC mute bits.

The codec format (step 4) precedes the FIFO fill because a format change resets the FIFO in this implementation.

### 3.3 Interrupt (`g2-hw.c`, `G2Interrupt`)

1. Read board + 0. Bit 7 set: return 0 (not this card; the chain continues).
2. Bit 3 set and playback active: write 512 bytes from the ring to the FIFO; when the ring holds less, the remainder is silence and an underrun is counted. The half-empty interrupt is an edge, so a refill is always 512 bytes.
3. Signal the slave when the ring is below its high-water mark. Return 1.
4. Otherwise write 0x01 (interrupts off) and return 1.

An assembly entry passes `is_Data` (A1) to the C handler and sets the Z flag from D0 before returning to exec.

### 3.4 Stop (`_AHIsub_Stop`)

1. Write 0x15 (playback interrupt off).
2. `RemIntServer(INTB_EXTER)`.
3. Write 0x01 (FIFO reset, DACs muted).
4. Signal the slave to exit and wait for it; free the buffers.

## 4 Buffering

| Quantity | Value |
|---|---|
| Ring contents | 16-bit stereo frames, big-endian words, 4 bytes per frame |
| High-water mark | 2 x `ahiac_BuffSamples` x 4 + 1024 bytes |
| Ring size | smallest power of two of at least 8192 bytes and at least high-water + `ahiac_MaxBuffSamples` x 4 |
| Example | 44.1 kHz mode, player 50 Hz: 882 frames per pass, high-water 8080 bytes (45.8 ms), ring 16384 bytes when `ahiac_MaxBuffSamples` is at most 2062, FIFO 1024 bytes (5.8 ms) |
| Refill | 512 bytes (128 frames) per interrupt; 2.9 ms at 44.1 kHz |
| Slave priority | 5 |

The slave runs `ahiac_PlayerFunc` and `ahiac_MixerFunc` as the Void driver does, converts the buffer into the ring, and sleeps until the interrupt signals that the ring has fallen below the high-water mark.

| `ahiac_BuffType` | Conversion into the ring |
|---|---|
| AHIST_S16S | copied |
| AHIST_M16S | each sample to both channels |
| AHIST_S32S (HiFi) | upper 16 bits of each sample |
| AHIST_M32S (HiFi) | upper 16 bits, to both channels |
| other | silence (not offered by the mode file) |

## 5 Modes and Rates

| Audio ID | Name | Mixing |
|---|---|---|
| 0x00470001 | G2 Toccata:16 bit stereo++ | 16-bit stereo, panning |
| 0x00470002 | G2 Toccata:HiFi 16 bit stereo++ | 32-bit stereo |
| 0x00470003 | G2 Toccata:16 bit mono | 16-bit mono |
| 0x00470004 | G2 Toccata:HiFi 16 bit mono | 32-bit mono |

The card always runs 16-bit stereo. Driver ID 0x0047 is not registered with the AHI author; it is unused by the drivers of the AHI 6.0 source tree.

The rates offered are the 16 rates of the AD1848 data sheet, which `rtl/src/toccata_playback.sv` produces with a 32-bit phase accumulator in core time (exact in the mean, PAL and NTSC). The driver reports each, rounded to a whole Hz, as `AHIDB_Frequency`; `AHIsub_AllocAudio` sets `ahiac_MixFreq` to the nearest one, so AHI mixes at the rate the card plays. Driver and fabric go together: on a fabric before the accumulator (2b727c2 and earlier) the card plays each rate up to 1.0 % fast (44.1 kHz as 44,466 Hz).

| Rate, Hz | CSS, CFS | Source |
|---|---|---|
| 5,512.5 (reported 5,513) | 1, 0 | 16.9344 MHz / 3072 |
| 6,615 | 1, 7 | 16.9344 MHz / 2560 |
| 8,000 | 0, 0 | 24.576 MHz / 3072 |
| 9,600 | 0, 7 | 24.576 MHz / 2560 |
| 11,025 | 1, 1 | 16.9344 MHz / 1536 |
| 16,000 | 0, 1 | 24.576 MHz / 1536 |
| 18,900 | 1, 2 | 16.9344 MHz / 896 |
| 22,050 | 1, 3 | 16.9344 MHz / 768 |
| 27,428.57 (reported 27,429) | 0, 2 | 24.576 MHz / 896 |
| 32,000 | 0, 3 | 24.576 MHz / 768 |
| 33,075 | 1, 6 | 16.9344 MHz / 512 |
| 37,800 | 1, 4 | 16.9344 MHz / 448 |
| 44,100 | 1, 5 | 16.9344 MHz / 384 |
| 48,000 | 0, 6 | 24.576 MHz / 512 |
| 54,857.14 (reported 54,857; beyond the data sheet) | 0, 4 | 24.576 MHz / 448 |
| 64,000 (beyond the data sheet) | 0, 5 | 24.576 MHz / 384 |

The card's output is interpolated linearly between its last two samples (`rtl/src/toccata.sv`) and sampled at 48 kHz by `rtl/g2/amiga_g2.v`; at 48,000 Hz the samples pass unchanged.

Output level: `AHIC_OutputVolume` maps a Fixed level to the nearest of the 64 attenuation steps (0 to -94.5 dB, 1.5 dB per step), written to AD1848 indices 6 and 7. `AHIDB_MinOutputVolume` is 1/65536, `AHIDB_MaxOutputVolume` 1.0.

## 6 Build

Toolchain: m68k-amigaos-gcc 6.5.0b, GNU assembler 2.46.0 (m68k-amigaos), sfdc, in `~/opt/amiga/bin` of the build host. vasm is not installed; the mode file is assembled with the GNU assembler from `common/MODEFILE.s` and `G2.s`, which are GNU assembler source, and extracted with `objcopy --output-target=binary`, as `Makefile.common.in` does.

1. Copy this directory to the build host, for example `~/build/ahi-g2`.
2. `make`.
3. Outputs: `g2.audio` (stripped), `g2.audio.debug` (with symbols), `G2`.

| Flag | Purpose |
|---|---|
| `-m68020 -Os` | target CPU of the core |
| `-noixemul -nostartfiles -nostdlib`, `-lamiga -lgcc` | library without C start-up code; `_start` returns -1 |
| `-fno-strict-aliasing` | the AHI library-base macros alias pointer types |
| sfdc `--mode=gatestubs --gateprefix=gw --libprefix=_ --libarg=last --addvectors=library` | register gateways, as `Makefile.common.in` |

## 7 Installation

1. Install the AHI user package, 6.x series for m68k-amigaos (Section 9.1).
2. Copy `bin/g2.audio` to `DEVS:AHI/g2.audio`.
3. Copy `bin/G2` to `DEVS:AudioModes/G2`.
4. Reboot, or run `AddAudioModes REFRESH`.
5. In the AHI preferences, select a `G2 Toccata` mode for the units in use, a frequency, and save.

## 8 Deviations

| From | Change | Reason |
|---|---|---|
| Void `void-main.c` | Mixing output goes to a ring; the slave sleeps between passes instead of looping | The card consumes at the sample rate; Void discards |
| Void | Slave priority 5 instead of -1 | The ring holds about two passes; a priority 0 task must not starve the mixer |
| Void | One AudioCtrl at a time; second `AllocAudio` fails | One stream on the card |
| Void | `ahiac_MixFreq` set to the card rate in `AllocAudio` | AHI mixes at the rate the card plays |
| Void | Multichannel mode and `AHISF_KNOWMULTICHANNEL` removed; `AHIDB_Bits` 16 | The card has two channels of 16 bits |
| `INTGW` of `library.h` | Interrupt entry in assembly | exec tests the Z flag on return from a server |
| NetBSD start | Codec format before the FIFO fill, not after | A format change resets the FIFO in this RTL |
| NetBSD start | 16-bit FIFO writes of big-endian words instead of byte writes of little-endian data | Half the bus cycles; byte order follows from Section 2 |
| NetBSD interrupt | A refill shorter than 512 bytes is padded with silence | The half-empty interrupt is an edge; a FIFO left below 504 bytes stops interrupting |
| NetBSD start | AD1848 index 9 (playback enable) not written | The control write 0x95 sets index 9 bit 0 in this RTL |
| NetBSD | Auto-calibration not requested | Index 9 bit 3 is left clear; the RTL only models its timing |

## 9 Open Questions

### 9.1 AHI 4 or AHI 6

The driver is built against the AHI 6.0 driver interface (library version 6, `Drivers/Common/library.c`). The recommended user package is AHI 6.x for m68k-amigaos: the AHI 6 m68k build targets the 68020 by default, the core has a 68020, and AHI 6 is the interface the Void skeleton states (`AHIsub_Disable` and `AHIsub_Enable` need not preserve registers). Under AHI 4.18, `ahi.device` may expect `AHIsub_Disable` and `AHIsub_Enable` to preserve all registers; these call `Forbid()` and `Permit()` and do not preserve D0, D1, A0 and A1. AHI 4.18 is untested; the test unit runs AHI 6.0. The AHI 6 user archive is not in the download set; its exact archive name and source are to be confirmed.

### 9.2 Untested items

| Item | State |
|---|---|
| Driver load, mode registration, AHI preferences | works on a G2 revision C unit (test tone) |
| Level 6 interrupt delivery through Paula `int6` and the INTB_EXTER chain | works on a G2 revision C unit (a 2.4 s file plays to the end through FIFO refills) |
| 16-bit FIFO writes producing one byte per lane through `minimig_m68k_bridge.v` strobes | works on a G2 revision C unit (by ear; not compared sample by sample) |
| Output level | a full-scale Toccata sample reaches a quarter of the 18-bit sum of `rtl/g2/amiga_g2.v` (12 dB below the DAC's range); a centred mono source in a stereo++ mode is a further 6 dB down in AHI |
| Stereo, 32-bit (HiFi) modes, rates other than 44,466 Hz, HippoPlayer | not exercised |
| Underrun count under load | not measured; `G2Data.underruns` is not reported |
| Rates 54.86 kHz and 64 kHz (beyond the AD1848 data sheet, produced by the RTL) | offered, untested |
| Licence of the EPROM Foundry Inc. files | not stated |
