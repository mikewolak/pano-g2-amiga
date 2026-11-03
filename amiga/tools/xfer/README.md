# xfer: lsz and lrz for AmigaOS

ZMODEM, YMODEM and XMODEM send and receive for AmigaOS 3 on the NanoMig core of the Pano Logic G2, built from lrzsz
0.12.20 (`amiga/third_party/lrzsz`, Uwe Ohse and Chuck Forsberg, GPL-2) over an Exec serial device. Port: Mike
Wolak, EPROM Foundry Inc., pano-g2-amiga. Licence of the programs: GPL-2.

## 1 Files

| File | Content |
|---|---|
| `zio.h` | Included ahead of every lrzsz source: the modem stream (descriptors 0 and 1, `putchar`, `fwrite`, `fflush` on stdout, `read`, `write`) mapped to zio; `exit` through zio; `signal`, `alarm`, `setvbuf` without effect; `fopen`, `access`, `stat`, `printf` traced |
| `zio_amiga.c` | The stream: an Exec serial device (CMD_READ with a timer.device timeout, CMD_WRITE from two 4,096-byte buffers, SDCMD_QUERY, CMD_CLEAR, SDCMD_BREAK) or the in-memory test pipe (a public port with two 64 KB rings, one per side); Ctrl-C at a wait sends the cancel string and exits with 130; the trace |
| `zreadline.c` | Edited copy of lrzsz's: the read with its timeout through `zio_read_timeout` in place of `read` under `alarm` |
| `rbsb_amiga.c` | In place of lrzsz's `rbsb.c`: no tty modes; `rdchk`, `purgeline`, `sendbrk` on the stream; stubs for TCP, `gethostname`, `execl` |
| `main_amiga.c` | The entry: the stream's arguments, DOS requesters off, the program name without volume or directory, lrzsz's `main` |
| `config.h` | lrzsz's configuration for libnix, written by hand |

## 2 Use

```
lsz [DEVICE=name] [UNIT=n] [BAUD=n] [BUFFER=bytes] [RTSCTS] [TRACE=file] [LOG=file] <lsz options> FILE ...
lrz [DEVICE=name] [UNIT=n] [BAUD=n] [BUFFER=bytes] [RTSCTS] [TRACE=file] [LOG=file] <lrz options> [FILE]
```

| Argument | Meaning | Default |
|---|---|---|
| `DEVICE`, `UNIT` | The Exec serial device and unit | `serial.device`, 0 |
| `BAUD` | Rate set with SDCMD_SETPARAMS (8N1, xON/xOFF off) | 115,200 |
| `BUFFER` | The device's read buffer | 16,384 |
| `RTSCTS` | Hardware handshake (SERF_7WIRE) | off |
| `PIPE=name`, `SIDE=0\|1` | The in-memory test pipe in place of a device | |
| `TRACE=file` | One line per stream call | |
| `LOG=file` | lrzsz's messages (stderr) to a file | |

The protocol options are lrzsz's: `-X` XMODEM (`lsz -k`: 1,024-byte blocks; `lrz -c`: CRC-16), `--ymodem`, ZMODEM
by default; `lrz -y` overwrites. The programs must keep the names `lsz` and `lrz`: lrzsz selects its protocol from
the program name, and a name with more after `rz` or `sz` (`lrz2`) makes `lrz` pipe the file into a command.

Build: `make` in this directory on a host with `m68k-amigaos-gcc` (bebbo, libnix).

## 3 Tests

| Test | Setting | Result |
|---|---|---|
| ZMODEM, pipe | 200,000-byte random file, `lrz -y` | sha256 equal |
| XMODEM CRC, pipe | 131,072 bytes, `lrz -X -c`, `lsz -X` | equal |
| XMODEM-1K, pipe | 131,072 bytes, `lsz -X -k` | equal |
| YMODEM, pipe | 131,072 bytes, `--ymodem` | equal |
| ZMODEM with errors, pipe | 131,072 bytes, `lrz --errors 30000` | equal |
| Serial device | | Untested: needs a second serial port at the far end |

Pipe tests: a G2 revision C unit, 68020 core, both programs on the one Amiga.
