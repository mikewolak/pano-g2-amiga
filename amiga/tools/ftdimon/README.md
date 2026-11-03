# ftdimon: the FT2232H on the G2's USB hub

An AmigaOS program for the G2 NanoMig core that drives an FTDI FT2232H on the G2's USB hub. In one window it shows
the chip and its MPSSE settings, sets and latches every pin of channel A, scans a JTAG chain, programs SPI NOR flash,
and edits the flash and EEPROM images in a hex editor. Specification: the G2 FT2232H port data sheet (`docs/G2_FT2232H_PORT.pdf`). Mike Wolak, EPROM
Foundry Inc., pano-g2-amiga. Licence: GPL-2.0-or-later (the part table is derived from flashrom).

## 1 Paths to the chip

| Path | Carries | Code |
|---|---|---|
| MPSSE stream | Channel A in MPSSE mode (`\amiga\AMIGA.INI` `FTDI=MPSSE`); command bytes and answers through `g2serial.device` and the serial FIFO board, passed to the chip by the firmware | `link.c`, `amiga/tools/jtag/mpsse.c`, `amiga/tools/xfer/zio_amiga.c` |
| Command block | The firmware's console command `ftdi` (latency timer, pins, raw control requests, EEPROM, the channel's mode) through block 3 of HD0 | `link.c` `fw_cmd` |

## 2 Pages

| Page | Key | Content |
|---|---|---|
| Device | 1 | The chip as the firmware reports it (USB port, IDs, channel, mode, packet size, latency, pins, bytes, errors); clock (30 MHz to 100 kHz), divide by 5, adaptive and three-phase clocking, internal loopback, latency timer, channel mode; EEPROM Read (into the Hex Editor), Write (changed words), Erase. Keys: R refresh, A apply, C reconnect, E read the EEPROM |
| Pins | 2 | ADBUS0-7 and ACBUS0-7: each pin an input or an output (In/Out) with its latched level (Lo/Hi), sent with MPSSE 0x80 and 0x82; levels read with 0x81 and 0x83 ten times a second (Auto Read). On entry the page sets the pins as it shows them. Keys: I all inputs, P read, U auto read |
| JTAG | 3 | TCK; Scan Chain (IDCODE, vendor, part and IR length of each device, panoflash's `chain.c`); Loop Inside (0x84) and Loop Wire (AD1 to AD2): 20 rounds of 1,024 bytes, bit errors, bytes/s. Keys: S scan, I inside, W wire |
| SPI Flash | 4 | Detect (0x9F; the part from flashrom's table, several parts sharing an ID chosen in Match), Unprotect (status register 0); Start and Length (hex), the erase block size from the part's erasers; Read, Verify, Erase, Program, Write All (erase, program, verify), Open and Save of the buffer; CRC32 of the file loaded and of the chip's bytes last read or verified (green when they agree over the same length, red when not); the bus settings in use; progress (section 3). Keys: D, R, V, E, P, W, O, S |
| Hex Editor | 5 | The flash buffer or the EEPROM's 512 bytes: offset, 16 bytes in hex, the same as text; cursor keys (Shift: a page, the line's ends), hex digits edit, Tab between hex and text, a click places the cursor; changed bytes red; Go to (hex), Open, Save, Fill FF. The view scrolls by pixels toward its target line. The scroller counts in units of 1, 2, 4 ... lines so that its total stays below 32,768 (GTSC_Total is 16 bits; a 1 MB part has 65,536 lines); its arrow buttons move the cursor a line as the cursor keys do (a cursor outside the view goes to the view's top or bottom line first), repeating every 3 updates after 300 ms held |

Q or Esc quits (Esc on the Hex Editor as well); `,` or Project, Preferences... opens the Preferences (section 3); ? or Project, About... opens the About box (serialmon's, unchanged but for its two text lines: the turning EPROMFOUNDRY logo, the cube and the starfield, `font8x8.c`).

## 3 Preferences and progress

The Preferences panel (Save: ENV: and ENVARC:; Use: ENV:; Cancel) holds the global variables `ftdimon/<name>`:

| Variable | Values | Default | Effect |
|---|---|---|---|
| SCKIdle | 0 low, 1 high | 0 | SCK level between transfers (CPOL) |
| SampleEdge | 0 leading, 1 trailing | 0 | Edge on which MISO is sampled (CPHA); MOSI changes on the other. Modes 0 and 3 use MPSSE 0x11/0x20/0x31, modes 1 and 2 0x10/0x24/0x34 |
| LSBFirst | 0, 1 | 0 | Bit order (MPSSE bit 3) |
| CSPin | 3 to 7 | 4 | ADBUS bit of chip select |
| CSHigh | 0, 1 | 0 | Chip select active level |
| CReset | 0 held low, 1 held high, 2 not driven | 0 | ADBUS7 when it is not chip select (ftdiflash holds it low for an iCE40) |
| Clock | 0 to 7 (30 MHz to 100 kHz) | 5 (1 MHz) | MPSSE divisor (TCK and SCK) |
| Divide5, Adaptive, ThreePhase | 0, 1 | 0 | MPSSE 0x8B/0x8A, 0x96/0x97, 0x8C/0x8D |
| Latency | 1 to 255 ms | 1 | Latency timer, set at start when the chip's differs |
| StartPage | 0 to 4 | 0 | Page shown at start (`PAGE=` overrides) |

Progress of a job: a bar of pixel resolution that moves an eighth of the way to the bytes done on every display
update; under it fixed fields, drawn only where characters change: per cent done (tenths), elapsed time (m:ss) and
bytes/s (1 s window while running; the phase's average at the end).  Each phase (Read, Erase, Program, Verify) fills
the bar once; Erase is amber.  A job step is a 512-byte read (37 ms), the pages that fit in 12 ms, or, for an erase,
the command and then one status read per update until WIP clears (no blocking wait).

## 4 Pins of channel A

| Pin | JTAG | SPI (defaults) |
|---|---|---|
| AD0 | TCK | SCK |
| AD1 | TDI | MOSI |
| AD2 | TDO | MISO |
| AD3 | TMS | CS when CSPin is 3 |
| AD4 | | CS (low active) |
| AD7 | | CRESET (held low between commands, as ftdiflash does), or CS when CSPin is 7 |

## 5 Files and origin

| File | Content | Origin |
|---|---|---|
| `ftdimon.c` | The program: screen, pages, jobs, hex view | This directory; drawing, buttons, palette, the clock and vertical blank server and the About box from `amiga/tools/serialmon` |
| `font8x8.c`, `font8x8.h` | The About box's 8x8 font | `amiga/tools/serialmon` (midimon's) |
| `crc32.c`, `crc32.h` | CRC-32 of PKZIP (reflected 0xEDB88320, start and result inverted): byte loop and slicing-by-four loop in 68020 assembler | This directory; the table as `fw/crc32.c` builds it |
| `link.c`, `link.h` | Stream and command block | This directory; `fw_cmd`: the command-block client (Mike Wolak, EPROM Foundry Inc.) |
| `spi.c`, `spi.h` | SPI NOR procedures and the bus settings (`spi_config`) | ftdiflash (`amiga/third_party/ftdiflash`, ISC) on the MPSSE stream; changes listed in the file's header |
| `flashchips.c`, `flashchips.h` | Part lookup | This directory |
| `flashchips_gen.py`, `flashchips_tab.c` | 402 SPI NOR parts generated from flashrom 1.4.0 (`amiga/third_party/flashrom`, GPL-2.0-or-later) | Generated at build time; the generated file is kept so the build needs no host Python |
| `../jtag/mpsse.c`, `jtag.c`, `chain.c` | MPSSE engine, TAP, chain scan | `amiga/tools/jtag` (TAP and chain from panoflash) |

Build: `make` (m68k-amigaos-gcc; `GIT_HASH` sets the version shown in the About box).

## 6 Results on a G2 revision C unit (firmware 0x6C1FBC9F, fabric ab5ef2d, NTSC 640 x 200)

| Test | Result |
|---|---|
| Device page | Port 4, 0403:6010 FT2232H, channel A, mpsse, 512-byte packets, latency 1 ms, pins F8 |
| JTAG, Loop Wire with AD1-AD2 jumper | 20,480 bytes, 0 bit errors, 13,864 bytes/s: PASS |
| SPI Detect with the AD1-AD2 jumper (MOSI looped to MISO) | ID 00 00 00, "no part answers": correct for a loop |
| EEPROM Read | 256 words of FFFF (the breakout's EEPROM blank or absent), shown in the Hex Editor; cursor, editing and scrolling work |
| SPI Detect, Atmel AT25SF081 on a socket adapter (channel A, defaults) | ID 1F 85 01, AT25SF081, 1,024 KB, page 256 bytes, erasers 20 52 D8 60 C7 |
| SPI mode against clock, AT25SF081, 0x9F | Mode 0: 1F 85 01 at 1, 6 and 30 MHz. Mode 3 (ftdiflash's): 1F 85 01 at 6 MHz (divide by 5), FF FF FF at 1.2 MHz and 1 MHz. LSB first: FF FF FF (the part receives 0xF9) |
| Read, 32,768 bytes, by block size | 4,096 B 2,242 ms; 1,024 B 2,311 ms; 512 B 2,374 ms; 256 B 2,561 ms |
| Read in the panel, 1,048,576 bytes | 1:22, 12,692 bytes/s; equal to the chip as read by ftdiflash on a Mac (first 32,220 bytes, sha256 808df4e1e679ca6a) |
| Write All, 0x000000-0x0FFFFF, 4 KB erase blocks, from a 1 MB file (the 32,220-byte image and FF; a marker of 256 x 55h programmed at 0x080000 before) | Erase about 15 s (68,661 bytes/s at 35 %), Program (blank pages skipped), Verify 1:29: 0 bytes differ. A separate read-back of 1 MB equals the file (sha256 8f69159f0c4a5201); 0x080000 reads FF |
| CRC32 (assembler) against zlib | `midiori-firmware.bin` A65F5A2F, the 1 MB image 3641ED66, equal to Python zlib at buffer offsets 0 to 3 and in two runs (7 bytes, the rest); 1,048,576 bytes in 841 ms, 32,220 bytes in 24 ms |
| Open, then Verify, AT25SF081 holding `midiori-firmware.bin` | file A65F5A2F, chip A65F5A2F (green); "Verify: 0x000000-0x007DDB equal, CRC32 A65F5A2F (buffer A65F5A2F)" |
| Hex Editor, 1 MB buffer: scroller drag and arrows (pointer and button driven through input.device) | Drag from the top to 0x089940; down arrow twice: cursor to the bottom line 0x089A00, then 0x089A10 with the view one line down; up arrow held 2 s: 28 lines up. Before the change the knob filled the track (65,536 lines read as 0) and did not move |
| Preferences | Saved to ENVARC:ftdimon (12 variables); StartPage, SCKIdle/SampleEdge (mode 3), LSBFirst and Clock/Divide5 take effect at start |
| EEPROM write and erase | Untested on a fitted EEPROM |
| JTAG scan of a real chain | Untested: no target wired |
| Other screen modes (PAL 640 x 256, interlace) | Untested |
