# g2serial.device: Serial Device for the G2 NanoMig Core

Exec device with the serial.device command set for the G2 NanoMig AGA core (68020). It moves the serial data through the G2 serial FIFO board (`amiga/rtl/src/g2ser_fifo.v`) when the board is present and through Paula otherwise. New implementation from a functional analysis of BaudBandit.device 1.4b. Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.

Status: built, not tested. No part of the driver has run on an Amiga, in an emulator or on the G2 unit.

---

## 1 Files

| File | Bytes | Content | Origin |
|---|---|---|---|
| `g2serial.c` | 41714 | Device vectors, commands, interrupt code of both paths, parameters, timers | New, EPROM Foundry Inc. |
| `g2serial.h` | 7244 | Device base, board register map, CIA-B bits | New, EPROM Foundry Inc. |
| `entry.s` | 2071 | First code of the segment; register-to-stack stubs for the device vectors and the interrupt code | New, EPROM Foundry Inc. |
| `Makefile` | 1009 | Build with m68k-amigaos-gcc 6.5.0b | New, EPROM Foundry Inc. |
| `g2serial.device` | 11944 | Build output, stripped; MD5 `9ff695c9b3e878029b8a352064b976db` | |

Interface definitions: NDK `devices/serial.h` 47.1, `devices/timer.h`, `resources/misc.h`, `libraries/configvars.h` (the toolchain's `ndk-include`). No third-party code is linked; `-nostdlib`, libgcc only.

## 2 Build and Installation

1. `make` in this directory with the bebbo amiga-gcc 6.5.0b toolchain (`TOOLS` defaults to `~/opt/amiga/bin`).
2. Copy `g2serial.device` to `DEVS:`.
3. Open it as `g2serial.device`, unit 0. The ROM serial.device is not removed; both cannot be open at the same time, because each holds `MR_SERIALPORT` and `MR_SERIALBITS` of misc.resource while open.

| Item | Value |
|---|---|
| Resident tag | `RTF_AUTOINIT`, `NT_DEVICE`, version 1.0, priority 0, name `g2serial.device` |
| Compiler flags | `-m68020 -O2 -fomit-frame-pointer -fno-builtin -ffreestanding -fno-function-cse` |
| Code | One code hunk of 11,176 bytes, one BSS hunk of 16 bytes (library base pointers) |
| Units | 0 only; any other unit: `IOERR_OPENFAIL` |
| Exclusive access | Default; `SERF_SHARED` from the first and every later opener allows several |

## 3 Data Paths

| Item | FIFO path | Paula path |
|---|---|---|
| Selected when | The board (Zorro II manufacturer 0x0A1C, product 1, found by `FindConfigDev`, configured, `cd_Driver` free) is present | The board is absent, or ENV variable `G2SERIAL` begins with `P` (`PAULA`), or bit 0 of the `OpenDevice()` flags argument is set (`G2SER_OPENF_PAULA`) |
| Rate | The board's 32-bit BAUD register; 183 .. 3,000,000 Bd (FT232R limits) | SERPER = round(colour clock / baud) - 1, colour clock = `ex_EClockFrequency` x 5 (3,546,895 Hz PAL, 3,579,545 Hz NTSC); 110 Bd .. colour clock / 2 |
| Receive | RX FIFO interrupt at 256 bytes or 73 us after the last byte; up to 512 bytes per interrupt, 16-bit reads of RXDATA2, unrolled by four | RBF, one SERDATR read per byte; RBF cleared after the read, then tested again |
| Transmit | TX FIFO interrupt while 256 places are free (480 with `SERF_7WIRE`); up to 512 bytes per interrupt, 16-bit writes to TXDATA, unrolled by four | TBE, one SERDAT word per interrupt; started by setting TBE in INTREQ |
| Interrupt installation | `AddIntServer(INTB_PORTS)`, priority 5; the server tests the CTRL pending bits and returns Z clear only for its own interrupt | `SetIntVector(INTB_RBF)`, `SetIntVector(INTB_TBE)`; previous vectors restored at the last close |
| Frame on the wire | 8N1 (the board has no format register); 7 bits with parity or with a stop bit are formed in bit 7 | Formed in SERDAT: 7 or 8 bits, parity, 1 or 2 stop bits, SERPER bit 15 for 8 bits with parity |

The path is fixed from the first open to the last close. The FIFO path writes BAUD before it sets CTRL bit 0 (FIFO mode), discards stale RX bytes, and at the last close waits up to 1 s (10 ms timer steps) for the TX FIFO to empty before CTRL returns to 0, which gives the serial bridge back to Paula. The RS-232 lines are CIA-B port A in both paths: DTR (PA7) and RTS (PA6) are set as outputs and asserted at the first open and restored at the last close.

## 4 Interrupt Strategy and Buffers

| Item | Implementation |
|---|---|
| Receive delivery | While a CMD_READ is active and the ring is empty, the interrupt stores bytes directly into `io_Data`; otherwise into the ring. The read is completed and replied inside the interrupt, one `ReplyMsg` per request, and the next queued read is made active in the same interrupt |
| Ring hand-over | The task copies ring bytes (`CopyMem`, at most two pieces) with interrupts enabled and sets direct delivery under `Disable` only when the ring is then empty, so byte order is kept without changes to `mn_ReplyPort` |
| Ring | Power of two, 512 bytes .. 16 MB, default 65,536; `io_RBufLen` is rounded up and written back. Free-running 32-bit head (written by the interrupt) and tail (written by the task); index by mask |
| Full ring | Never overwritten. The receive interrupt is switched off and the hardware holds the data: Paula's RBF stays set, so `uart_port.v` sends nothing further; the board's RX FIFO fills, so `amiga_serbridge.v` stops. The task switches the interrupt on again after it has taken bytes out |
| Buffer replacement | The new buffer is allocated first, swapped under `Disable`, the old one freed afterwards |
| Write completion | From the interrupt when the last byte has been handed to SERDAT or to the TX FIFO; the next queued write is made active in the same interrupt |
| Locking | `BeginIO` and `AbortIO` run under `Forbid`; state shared with the interrupts changes under `Disable` |
| Overrun | SERDATR OVRUN (bit 15) sets `IO_STATF_OVERRUN` and ends the read with `SerErr_LineErr` |

## 5 Commands

| Command | Behaviour |
|---|---|
| CMD_READ | As section 4. `io_Length` 0 completes at once. Synchronous completion honours `IOF_QUICK`. EOF mode (`SERF_EOFMODE`) ends the read after any of the eight `io_TermArray` characters (256-bit look-up table) |
| CMD_WRITE | Queued; `io_Length` -1 sends up to the terminating NUL; always asynchronous |
| CMD_CLEAR | Empties the ring and, on the FIFO path, the board's RX FIFO; clears overrun and break status; re-asserts RTS and sends xON when xOFF was sent |
| CMD_RESET | Aborts every request (active and queued, a break in progress), restores the defaults (9,600 Bd, 8N1, 65,536-byte ring, `SER_DEFAULT_CTLCHAR`, 250,000 us break), clears the ring and status |
| CMD_FLUSH | Aborts the queued reads, writes and queued breaks; the active ones continue |
| CMD_STOP / CMD_START | STOP holds the transmitter and stops completion of reads (received bytes go to the ring); START resumes both |
| CMD_UPDATE | Completes at once (writes are never held back for an update) |
| SDCMD_QUERY | `io_Status` = CIA-B PRA bits 7..2 (DTR, RTS, CD, CTS, DSR, SEL/RI, as serial.h defines them) plus bits 8..12 (overrun, break sent, break received, xOFF on transmit, xOFF on receive); bits 8..10 are cleared by the query. `io_Actual` = bytes in the ring plus, on the FIFO path, the board's RXCOUNT |
| SDCMD_BREAK | `io_BrkTime` us through timer.device UNIT_MICROHZ; Paula path sets ADKCON UARTBRK for that time. `SERF_QUEUEDBRK` (or a break already in progress) queues it behind the writes. On the FIFO path no break reaches the wire (the board has no break control) |
| SDCMD_SETPARAMS | Baud (0 keeps the current rate), read/write length 7 or 8, stop bits 1 or 2, parity even/odd/mark/space, xON/xOFF with `io_CtlChar`, EOF mode, `SERF_7WIRE`, `SERF_RAD_BOOGIE` (forces 8 bits, no parity, xON/xOFF off), buffer size. Errors: `SerErr_InvParam`, `SerErr_BaudMismatch`, `SerErr_BufErr` |
| Others | `IOERR_NOCMD` |

AbortIO acts on the active and queued reads and writes and on a break; an aborted request receives `IOERR_ABORTED` with `io_Actual` equal to the bytes transferred. The last close aborts and replies every outstanding request, aborts and collects both timerequests (the timer port is set to `PA_IGNORE` first), then closes timer.device. Expunge is refused while the device is open (`LIBF_DELEXP`).

## 6 Flow Control

| Item | Implementation |
|---|---|
| RTS (`SERF_7WIRE`) | Negated when the ring reaches 3/4 of its size, asserted again from the task when it is down to 1/4, and by CMD_CLEAR, CMD_RESET and SETPARAMS. Without `SERF_7WIRE` RTS stays asserted |
| CTS (`SERF_7WIRE`) | Sampled before every Paula byte and before every TX FIFO refill. Negated: the transmitter stops and a 2,000 us UNIT_MICROHZ timerequest, replied to a `PA_SOFTINT` port, polls it. On the FIFO path the TX FIFO fill is limited to 64 bytes, so at most 64 bytes leave after CTS is negated. Without `SERF_7WIRE` CTS is ignored |
| xON/xOFF | Active unless `SERF_XDISABLED`. Received xON/xOFF characters release or hold the transmitter and are not stored. xOFF is sent at 3/4 of the ring and xON at 1/4, ahead of queued data |
| Fast path | 8 bits, no parity, xON/xOFF off and no EOF mode select the per-byte code without parity, control character or terminator tests (receive) and the word copy (FIFO path) |

## 7 Relation to the Specification

| Specification item | Status |
|---|---|
| 9.2.1 one reply per request from the interrupt, no per-byte signal | Implemented |
| 9.2.2 direct delivery, buffered bytes copied in at most two blocks | Implemented (EOF mode copies byte by byte) |
| 9.2.3 one SERDAT write per TBE, start by software-set TBE | Implemented |
| 9.2.4 SERDATR read before the RBF clear, RBF tested again | Implemented |
| 9.2.5 short interrupt-off windows, task copies with interrupts enabled | Implemented |
| 9.2.6 flag clearing on entry, AbortIO semantics | Implemented |
| 9.3.1 SetIntVector, no autovector writes | Implemented; INT2 server for the board |
| 9.3.2 no vector swapping | One handler per source; a mode byte selects direct or ring delivery |
| 9.3.3 state in the device base through `is_Data` | Implemented; one unit |
| 9.3.4 SERPER rounding, colour clock from the E clock | Implemented |
| 9.3.5 68020 code, power-of-two ring | `-m68020`, 32-bit divide for SERPER, mask indexing; bit-field instructions not used |
| 9.3.6 overrun reported, unread data never overwritten | Implemented; a full ring holds the hardware instead of dropping (section 4) |
| 9.3.7 RTS hysteresis 3/4 and 1/4 | Implemented |
| 9.3.8 SERF_7WIRE honoured, UNIT_MICROHZ through PA_SOFTINT | Implemented |
| 9.3.9 close aborts requests and timerequests | Implemented |
| 9.3.10 buffer replaced under Disable | Implemented |
| 9.3.11 no RemDevice of serial.device | Implemented; own name |
| 9.4 parity, 7/8 bits, 1/2 stop, xON/xOFF, break, EOF, STOP/START, QUERY, RAD_BOOGIE, large buffers, high rates | Implemented within the limits of section 8 |

## 8 Deviations and Limits

| Item | Value |
|---|---|
| Full ring | Holds the hardware (section 4) instead of reporting `SerErr_BufOverflow`; the data wait in `uart_port.v`, the board, the bridge and the firmware. `SerErr_BufOverflow` is not returned |
| Error position | Parity and break errors found while bytes are buffered end the next read when the ring has been copied, not at the byte's position |
| 8 bits with parity | FIFO path: `SerErr_InvParam`. Paula path: SERPER bit 15 and a 9-bit SERDAT word; `uart_port.v` carries 8N1 frames only, so on the G2 such frames are not carried correctly (untested) |
| Two stop bits | FIFO path: accepted, one stop bit on the wire |
| Break | Received break detected on the Paula path only (a zero byte with a low stop bit); `uart_port.v` drops such frames, so it does not occur on the G2. Sent break does not reach the wire on the FIFO path; on the Paula path a queued break may start while the last character is still in the shift register |
| xON/xOFF characters | Sent with the configured parity computed, not as given in `io_CtlChar` |
| Board word ports | RXDATA2 is decoded at 0x10 and 0x12 and TXDATA at 0x14 and 0x16 (rtl/src/g2ser_fifo.v), so a 68020 `move.l` moves four bytes either way; RXDATA1 is at 0x18. The driver moves longwords, with a word and single bytes at the ends |
| ENV variable | Read with `GetVar(GVF_GLOBAL_ONLY)` only when the first opener is a process |

## 9 Verification Plan

Steps 1, 3 and 4 have run at the rates in section 9.1; the other rates and steps are untested. `serialmon` (`amiga/tools/serialmon`) with a loopback plug (TXD to RXD, RTS to CTS) on the USB serial adapter; each run 10 s at 100 % load.

| Step | Command | Pass criterion |
|---|---|---|
| 1 | `serialmon NOWINDOW DEVICE=serial.device BAUD=115200 SECONDS=10 RATE=100` | Reference: PASS, figures recorded |
| 2 | `serialmon NOWINDOW DEVICE=BaudBandit.device BAUD=115200 SECONDS=10 RATE=100` | Reference |
| 3 | `setenv G2SERIAL PAULA`, then step 1 with `DEVICE=g2serial.device` at 9,600, 19,200, 38,400, 57,600, 115,200, 230,400 | PASS, lost 0, throughput at or above steps 1 and 2 |
| 4 | `unsetenv G2SERIAL`, `DEVICE=g2serial.device` at 115,200, 230,400, 460,800, 921,600, 1,500,000, 3,000,000 | PASS, lost 0; throughput near 10 bits per byte of the rate; record the highest rate that passes |
| 5 | Step 4 with `FLOW=RTSCTS` and `FLOW=XONXOFF` (FORMAT 8N1, flow tests only), `FORMAT=7E1`, `7O1`, `7N1`, `8N2` | PASS (XONXOFF: the test data contain 11h/13h, so a FAIL there is expected, as section 3 of serialmon states) |
| 6 | Step 4 with `BUFFER=512`, `BUFFER=262144` and `BOOGIE` | PASS |
| 7 | serialmon window: driver switch between serial.device, BaudBandit.device and g2serial.device while traffic runs; break key; CMD_CLEAR | No request kept after AbortIO; driver removed by RemDevice; the ROM serial.device works after g2serial.device is closed |
| 8 | CPU load during step 4 at 921,600 (serialmon figure, or a second task counting a loop) | Recorded against serial.device at 115,200 |

### 9.1 Results

Fabric ab5ef2d (Paula receive holding register), firmware 0xDBAFF347, 68020 core, NTSC; `serialmon NOWINDOW ... SECONDS=10 RATE=100 FLOW=NONE`, run detached with no other network traffic to the Amiga. Every run: PASS, errors 0, overruns 0, overflows 0.

| Driver, path | Baud | Sent and received | Lost | Out, bytes/s | In, bytes/s | Round trip mean / max, ms |
|---|---|---|---|---|---|---|
| serial.device | 57,600 | 17,246 | 0 | 5,624 | 5,624 | 32.8 / 45.0 |
| serial.device | 115,200 | 33,907 | 0 | 10,451 | 10,453 | 28.8 / 85.8 |
| g2serial.device, Paula | 57,600 | 17,246 | 0 | 5,707 | 5,710 | 31.9 / 45.7 |
| g2serial.device, Paula | 115,200 | 34,465 | 0 | 11,130 | 11,138 | 29.5 / 52.9 |
| g2serial.device, FIFO | 115,200 | 34,486 | 0 | 11,484 | 11,476 | 10.4 / 23.3 |

With fabric fc62141 (no holding register) the same runs lost 288 and 2,475 bytes (serial.device) and 798 and 3,903 bytes (g2serial.device, Paula) at 57,600 and 115,200, with overruns 0: the receiver did not start a frame while RBF was set, so bytes arriving during the interrupt response were dropped before Paula.
