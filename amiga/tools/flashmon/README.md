# flashmon: the G2's configuration flash from the Amiga

An AmigaOS program for the G2 NanoMig core, in the house style (amiga-ui-style), on `panoflash.device`
(`amiga/panoflash`).  It shows the flash part, its map and the write guard; reads, verifies, writes and erases any
range; computes CRC32 in the firmware; dumps any range to a file; extracts the bitstream of a slot as a Xilinx `.bit`
for a JTAG load into another unit; shows the configuration table (MAC address, serial number, board ID); and edits the
buffer in a hex editor.  Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.

## 1 Pages and panels

| Page or panel | Key | Content |
|---|---|---|
| Flash | 1 | Part and Map: part, JEDEC ID, status register and sector size, write guard and its refusals, main slot, free region, sectors erased since the firmware started; a map strip of the part to scale (protected dark red, writable dark green, the range of Start and Length amber).  Flash: Region (whole chip, main slot, free region, golden image, header, configuration, typed range), Start and Length (hex); Read, Verify, Write, Erase, CRC, Dump..., Open..., Save...; progress (per cent, m:ss, bytes/s as the phase's average); CRC32 of the file loaded and of the flash range last read, verified, written or summed |
| Hex Editor | 2 | the buffer with flash addresses: cursor keys, hex and text editing, Go to, Open..., Save..., Fill FF; smooth scrolling (ftdimon's view) |
| Device Info | I | the configuration table (section 4) in a modal panel |
| About | ? | serialmon's About box |

Keys on the Flash page: R read, V verify, W write, E erase, C CRC, D dump, O open, S save, B bitstream, I device
info.  Q or Esc quits; a running job is stopped first.

## 2 Jobs

| Job | Range | Device requests |
|---|---|---|
| Read | Start, Length into the buffer | `CMD_READ` unit 0, 64 KB each |
| Dump... | Start, Length to a file of any size | as Read, each piece written to the file |
| Verify | the buffer against the flash at Start | `PFL_CMD_CRC` per 64 KB; a piece that differs is read and its bytes counted |
| Write | the buffer at Start (a sector boundary) | `CMD_WRITE` unit 1 or 2 a sector at a time, then the range's CRC32 against the buffer's |
| Erase | Start, Length (sector boundaries) | `PFL_CMD_ERASE` a sector at a time |
| CRC | Start, Length | `PFL_CMD_CRC` per 256 KB, each continued from the last |

A job runs in a process of its own, so the display keeps 60 updates a second; its button becomes Stop and the other
buttons are disabled.  A write or erase asks first, and is refused when the range is not inside the main slot or the
free region, or when a write at the main slot's start has no bitstream sync word in its first 64 bytes.

## 3 Bitstream

Bitstream... walks the buffer's configuration packets from the sync word (AA 99 55 66 in the first 64 bytes) to the
write of DESYNC and the NO-OPs after it (Xilinx UG380 section 5) and saves the
bytes from the buffer's start to there:

| File name ends in | Content |
|---|---|
| `.bit` (default) | the Xilinx header (13-byte magic; fields a design and source address, b part, c date, d time, e length) and the bitstream, as `tools/panoflash` `bitfile.c` reads it |
| `.bin` | the bitstream alone |

The part in field b comes from the stream's write of the IDCODE register: 0x04011093 6slx100fgg484 (revision C),
0x0401D093 6slx150fgg484 (revisions A and B).  The buffer must hold the whole slot: Region Main slot or Golden image,
then Read.

Files are written only when the volume has room; RAM: is judged by free memory less 512 KB, since its Info() reports
no free blocks.  DOS requesters are off for the program and its job process, so a full volume is an error message and
the partial file is deleted.

## 4 Configuration table

The table at 0x6C0000 (M25P64) or 0x8C0000 (M25P128) holds records of a 32-bit tag and a 32-bit value up to the
first tag FFFFFFFF.

| Field | Tags | Source of the reading |
|---|---|---|
| MAC address | 0x10000010 (bytes 0-1), 0x10000011 (bytes 2-5) | `fw/g2.c` `g2_factory_mac` |
| Serial number | 0x10000004-0x10000007, 16 characters | the text these tags hold; not confirmed as the serial number by any document here |
| Board ID | 0x10000000; bits 31:26 the series | `fw/bootsh.c` `flash id` |
| Revision, FPGA | from the flash part | the M25P64 on revision C, the M25P128 on revisions A and B |

## 5 Files and origin

| File | Content | Origin |
|---|---|---|
| `flashmon.c` | the program | screen, palette, drawing, buttons, progress bar, CRC line, hex view, file requester and About box from `amiga/tools/ftdimon` (serialmon's); pages, jobs, map strip, Device Info and bitstream extraction new |
| `../ftdimon/font8x8.c`, `crc32.c` | About box font; CRC32 in 68020 assembler | `amiga/tools/ftdimon` |
| `../../panoflash/panoflash.h` | the device interface | `amiga/panoflash` |

Build: `make` (m68k-amigaos-gcc; `GIT_HASH` sets the version in the About box).  Needs `DEVS:panoflash.device` and
`AMIGA.FW` with the flash window.

## 6 Results on a G2 revision C unit (M25P64, fabric ab5ef2d, NTSC 640 x 200)

| Test | Result |
|---|---|
| Flash page | part M25P64 8,192 KB, ID 20 20 17, status 00, guard present 0 refused; map strip and range drawn |
| Read, main slot, 3,407,872 bytes | progress and Stop drawn; 83,292 bytes/s at 32.6 % while screenshots were taken |
| Device Info | MAC 00:1C:02:64:CB:D9 (the unit's), text 3621BC472-9D0CLP, board ID 08010002 (series 2), revision C, 29 records |
| Bitstream of the main slot to RAM: as .bit | 3,092,005 bytes: header a `flash_380000;from flash 0x380000`, b `6slx100fgg484`, e 3,091,908 bytes; IDCODE 04011093; parsed by `tools/panoflash` `bitfile.c` (result 0); the bitstream equals the build's `pano_g2_amiga_ab5ef2d.bin` (3,091,908 bytes, sha256 a52e1b693830b860) |
| Saving the .bit and the .bin together to RAM: beside a 3.4 MB buffer (first build) | RAM: full; DOS requesters stopped the machine until a cold start.  Changed: one format per save, room checked, requesters off |
| JTAG load of an extracted .bit into another unit | untested |
| Erase, main slot 0x380000-0x6BFFFF (52 sectors) | about 41 s; guard refusals 0 |
| Write, main slot, the slot's own image read before (3,407,872 bytes, CRC32 B746E0F8) into the erased slot | 52 sectors written and read back in 93.6 s (1.8 s a sector), the range's CRC32 check 10 s: B746E0F8, equal to the file |
| Reboot from the rewritten slot (firmware `reboot`, IPROG 0x380000) | fabric 0x0AB5EF2D, loader, AMIGA.FW and AmigaOS up; the slot's CRC32 B746E0F8 |
| bytes/s during a job (first build) | stayed at the value of the first second (0 during a write or erase): the 1 s window compared a sample slot with itself after the fourth sample.  Now the phase's average: CRC of the main slot 250,653 bytes/s at 38.4 %, 309,692 at the end |
