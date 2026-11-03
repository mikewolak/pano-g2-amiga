# jtagtool and swdtool: JTAG and STM32 Serial Wire Debug from the Amiga through an FTDI FT2232H

An AmigaOS command for the G2 NanoMig core that drives a JTAG chain through an FTDI FT2232H on the G2's USB hub.
Channel A of the chip runs in MPSSE mode; the firmware passes the bytes of the serial FIFO board (`g2serial.device`)
to it when `\amiga\AMIGA.INI` holds `FTDI=MPSSE` (menu item FTDI).  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
Specification: the G2 FT2232H port data sheet (`docs/G2_FT2232H_PORT.pdf`).

## 1 Files and Origin

| File | Content | Origin |
|---|---|---|
| `jtag.c`, `jtag.h` | TAP driver: reset, IR and DR shifts, idle clocks; every operation ends in Run-Test/Idle | Mike Wolak, EPROM Foundry Inc. (the G1/G2 JTAG flasher), unchanged |
| `chain.c`, `chain.h` | Chain scan: IDCODEs, vendors and parts, total IR length | the same, unchanged |
| `s3e.c`, `s3e.h` | Spartan-3E and Spartan-6 LX100: IDCODE, USERCODE, configuration from a `.bit` file, JPROGRAM | the same, unchanged (`s3e.h` of commit 4b5725e3) |
| `bitfile.c`, `bitfile.h` | Xilinx `.bit` container | the same, unchanged |
| `ptime.h` | Clock and sleep interface | the same, unchanged |
| `xpc.h` | The bit engine interface under the files above (panoflash's engine calls), carried by MPSSE | This directory |
| `mpsse.c` | MPSSE engine: commands queued, answers read back into place (FTDI AN_108) | This directory, new implementation |
| `ptime_amiga.c` | `ptime.h` on AmigaOS | This directory |
| `jtagtool.c` | The command; PROGRAM and REBOOT are panoflash's `pf_load_mem` and `pf_reboot` on this engine | This directory |
| `swd.c`, `swd.h` | ARM Serial Wire Debug (ADIv5 SW-DP, MEM-AP) on the MPSSE stream | This directory, new implementation (pin scheme and edges of OpenOCD's ftdi driver, GPL-2.0, no code copied) |
| `swdtool.c` | STM32 identification, memory, flash erase and programming | This directory |
| `../xfer/zio_amiga.c` | The serial stream (Exec serial device, read with timeout) | amiga/tools/xfer |

## 2 Wiring (channel A)

| FT2232H pin | Signal |
|---|---|
| ADBUS0 | TCK |
| ADBUS1 | TDI (to the target's TDI) |
| ADBUS2 | TDO (from the target's TDO) |
| ADBUS3 | TMS |
| GND | GND of the target |

The FT2232H's I/O is 3.3 V.  The loopback test needs no target: INTERNAL uses the chip's own loopback, EXTERNAL a wire
from ADBUS1 to ADBUS2.

## 3 Use

```
jtagtool LOOP                       internal, then external loopback, 100 rounds of 1,024 bytes at TCK 1 MHz
jtagtool CLOCK=6000000 LOOP INTERNAL BYTES=4096 ROUNDS=50
jtagtool SCAN                       IDCODE and IR length of every device on the chain
jtagtool PROGRAM FILE=design.bit    configure an XC3S1600E or XC6SLX100 over JTAG (FPGA only, not its flash)
jtagtool REBOOT                     JPROGRAM: the FPGA reloads from its flash
```

| Argument | Meaning | Default |
|---|---|---|
| `DEVICE=`, `UNIT=` | The serial driver carrying the bytes | `g2serial.device`, 0 |
| `CLOCK=` | TCK in Hz: 30 MHz / (1 + divisor), 458 Hz to 30 MHz | 1,000,000 |
| `BYTES=` | Bytes per loopback round, 1 to 4,096 | 1,024 |
| `ROUNDS=` | Loopback rounds | 100 |

Return code 0 PASS, 5 FAIL, 10 usage, 20 the stream did not open or the chip did not answer the sync command (bad
command 0xAA, answer 0xFA 0xAA).

## 4 Firmware Side

| Item | Behaviour |
|---|---|
| Detection | The first FTDI device (vendor 0x0403) on the hub is opened; a multi-channel chip on channel A (vendor requests with wIndex 1) |
| Packets | High speed: 512-byte bulk packets in their own DMA RAM windows (0x7400, 0x7600; `usb_int_set_buffer`); an IN packet is held in the firmware and given to the bridge as it has room |
| Mode | `FTDI=MPSSE`: SET_BITMODE 0x02 after both FIFOs are purged; `FTDI=SERIAL`: SET_BITMODE 0x00.  Baud rate and modem lines are not sent in MPSSE mode |
| Status | Console `status`, line `FTDI:` (port, vendor and product, mode, packet size, counts) |

## 5 Results

| Test | State |
|---|---|
| Build (m68k-amigaos-gcc, -Wall) | No warnings |
| Firmware with `FTDI=MPSSE` on a G2 revision C unit (ID 0x3E340651), an FT232R on the hub | The FT232R opened as a serial adapter (single channel: MPSSE not applied); `jtagtool LOOP` reached the adapter through `g2serial.device`, and its TXD-RXD loop plug echoed the sync bytes, which the sync check rejected (return 20) |
| FT2232H (generic breakout, 0403:6010) on hub port 4 | Opened on channel A, MPSSE, 512-byte packets; four releases and re-opens ("stopped answering") within 70 s of being plugged in, none after |

Loopback on a G2 revision C unit (firmware 0x3E340651, fabric ab5ef2d), `jtagtool` through `g2serial.device`:

| Test | TCK | Rounds x bytes | Bit errors | Bytes/s | Longest round trip | Result |
|---|---|---|---|---|---|---|
| Internal (0x84) | 1 MHz | 100 x 1,024 | 0 | 13,008 | 88 ms | PASS |
| Internal | 6 MHz | 50 x 4,096 | 0 | 14,288 | 274 ms | PASS |
| Internal | 30 MHz | 50 x 4,096 | 0 | 14,253 | 279 ms | PASS |
| External, no wire | 1 MHz | 5 x 1,024 | 23,552 | 11,770 | 76 ms | FAIL as expected: TDO read 1 throughout |
| External, ADBUS1 to ADBUS2 | 1 MHz | 100 x 1,024 | 0 | 13,018 | 81 ms | PASS |
| External, ADBUS1 to ADBUS2 | 6 MHz | 50 x 4,096 | 0 | 14,295 | 274 ms | PASS |
| External, ADBUS1 to ADBUS2 | 30 MHz | 50 x 4,096 | 0 | 14,271 | 276 ms | PASS |
| `SCAN` with the wire | 1 MHz | | | | | 0 devices, IR 0 bits: a wire is a chain of length 0 |

The rate does not change with TCK: the path through `g2serial.device`, the serial bridge and the firmware limits it
to about 14 KB/s each way.  A configuration of 723 KB (a G1 bitstream) takes about 50 s at that rate (estimate).

## 6 swdtool: STM32 Serial Wire Debug

Wiring (channel A): ADBUS0 SWCLK; ADBUS1 to SWDIO through 470 ohm; ADBUS2 to SWDIO directly; ADBUS4 to NRST
(optional); GND.  ADBUS1 is an input while the target drives SWDIO.  3.3 V targets only; the target has its own supply.

```
swdtool [DEVICE=g2serial.device] [UNIT=n] [CLOCK=hz] [NRST] ID
swdtool ... READ FILE=f [ADDR=a] [LEN=n]                 memory to a file; default the whole flash
swdtool ... WRITE FILE=f [ADDR=a] [NOERASE] [NOVERIFY] [NORUN]
swdtool ... ERASE ALL | ERASE ADDR=a LEN=n
swdtool ... RESET [HALT]
swdtool ... PEEK ADDR=a [LEN=n] | POKE ADDR=a VALUE=v
```

| Item | Behaviour |
|---|---|
| Connect | JTAG-to-SWD sequence 0xE79E between line resets; DPIDR; ABORT; power-up request with ORUNDETECT; MEM-AP 0 CSW 32-bit, auto-increment |
| Exchange | Transactions queued and sent as one MPSSE exchange (up to 120 words); a WAIT or FAULT keeps its data phase (ORUNDETECT); reads retried after a line reset and ABORT |
| Identification | CPUID; DBGMCU_IDCODE at 0xE0042000, 0x40015800 (Cortex-M0/M0+) or 0x5C001000 (H7); 46 device IDs; flash size register |
| Flash F0/F1/F3 | 0x40022000: page erase, mass erase, halfword programming, 75 us idle after each halfword; XL-density bank 2 |
| Flash F2/F4/F7 | 0x40023C00: sector erase, mass erase, word programming with PSIZE x32 (VDD 2.7-3.6 V), 30 us idle after each word |
| Other families | Identified, memory read and written; flash not programmed |
| Return code | 0 done, 5 failed, 10 usage, 20 no stream or no answer from the target |

State: built with m68k-amigaos-gcc 6.5.0b, 0 warnings; SWD untested on hardware (no STM32 wired to the test unit).  Estimated rates from the 14 KB/s stream: read about
3,000 bytes/s, F1 programming about 1,100 bytes/s (untested).
