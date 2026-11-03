# panoflash.device: the G2's configuration flash for AmigaOS

An Exec device for the G2 NanoMig core that reads, writes and erases the unit's SPI configuration flash (M25P64 on
revision C, M25P128 on revisions A and B), and FlashCLI, the shell tool on it.  The device reaches the flash through
the firmware's flash window on HD0 (`fw/amiga/pflash.c`).  Mike Wolak, EPROM Foundry Inc.,
pano-g2-amiga.  The graphical program is `amiga/tools/flashmon`.

## 1 Units

| Unit | Name | M25P64 (revision C) | M25P128 (revisions A, B) | Access |
|---|---|---|---|---|
| 0 | `PFL_UNIT_CHIP` | 0x000000-0x7FFFFF | 0x000000-0xFFFFFF | read only |
| 1 | `PFL_UNIT_MAIN` | 0x380000-0x6BFFFF: the fabric and the firmware loaded at power-up | 0x480000-0x8BFFFF | read, write, erase |
| 2 | `PFL_UNIT_FREE` | 0x6D0000-0x7FFFFF: U-Boot, the DDR shell, the boot-select word | 0x900000-0xFFFFFF | read, write, erase |

The map is the firmware's (`fw/spiflash.c` `spif_map`), chosen by the part's RDID at every request.  The multiboot
header, the golden image and the configuration table lie in no writable unit.  A write to them is refused three
times over: by the device's units, by the firmware's `spif_write_allowed`, and by the fabric's write guard
(`rtl/g2/g2_spi_guard.v`), which withholds the clock edge that would complete the command.

## 2 Commands

| Command | Operation |
|---|---|
| `CMD_READ` | `io_Length` bytes from `io_Offset`, any alignment |
| `CMD_WRITE`, `TD_FORMAT` | `io_Offset` on a sector boundary; each sector the range touches erased, programmed, read back by the firmware; a sector that already holds the bytes is not erased |
| `PFL_CMD_ERASE` (0x5041) | whole sectors |
| `PFL_CMD_CRC` (0x5042) | `io_Actual` = CRC32 (PKZIP) of the range, computed by the firmware; `io_Data` may point to the CRC32 of the bytes before |
| `PFL_CMD_INFO` (0x5040) | a `struct PanoFlashInfo`: RDID, status register, sizes, the unit, the map, the guard's counts, the firmware's last line |
| `TD_GETGEOMETRY` | one erase sector a track |
| `TD_PROTSTATUS` | non-zero on unit 0 |

Errors beyond exec's are the firmware's results 1 to 9 and the device's 20 (no flash window), 21 (no answer), 22
(scsi.device failed); `panoflash.h` lists them.  A request runs in the caller's task and returns when it is done;
a semaphore keeps one request at a time on the window.

## 3 FlashCLI

```
FlashCLI INFO   [unit]
FlashCLI READ   <unit> <offset> <length> <file>     (length 0: to the end of the unit)
FlashCLI CRC    <unit> <offset> <length>
FlashCLI VERIFY <unit> <offset> <file>
FlashCLI WRITE  <unit> <offset> <file>
FlashCLI ERASE  <unit> <offset> <length>
```

Units by number or name (CHIP, MAIN, FREE); numbers decimal or 0x hex.

## 4 Files and origin

| File | Content | Origin |
|---|---|---|
| `entry.s` | segment start and register-to-stack stubs | `amiga/serial/entry.s` without its interrupt stubs |
| `panoflash.c` | the device | the skeleton (romtag, vectors, open, close, expunge) of `amiga/serial/g2serial.c`; the window protocol new |
| `panoflash.h` | the public interface | this directory |
| `flashcli.c` | FlashCLI | this directory |
| `../tools/ftdimon/crc32.c` | CRC32 in 68020 assembler | `amiga/tools/ftdimon` |

Build: `make` (m68k-amigaos-gcc).  Install: `panoflash.device` to `DEVS:`, `FlashCLI` to `C:`, and `AMIGA.FW` built
from this tree (`make -C fw/amiga amiga_fw`) on the stick.

## 5 Results on a G2 revision C unit (M25P64, fabric ab5ef2d)

| Test | Result |
|---|---|
| INFO, units 0 to 2 | RDID 202017, 8,192 KB, 64 KB sectors, guard present; the three units as section 1 |
| Read unit 0 0x040000, 1,048,576 bytes | 7.96 s (132 KB/s); equal to the unit's 8 MB backup (CRC32 F4CAFB82) |
| CRC unit 0 0x040000, 1,048,576 bytes | 3.32 s, F4CAFB82 |
| Write 100,000 bytes at 0x7D0000 (unit 2, two erased sectors) | 3.66 s; read back equal, CRC32 126467F5; the second sector's tail reads FF |
| The same write again | 1.32 s; no sector erased (equal bytes) |
| Erase 0x7D0000-0x7EFFFF | 1.54 s; both sectors CRC32 DEAB7E4E (erased) |
| Read unit 1, 3,407,872 bytes | 26.3 s; sync word at byte 16, `PANOLDR!` at +0x300000 |
| Write unit 1 offset 0 with its own first 1,048,576 bytes | 12.2 s; verified, no sector erased |
| Write to unit 0; write at offset 0x100001 of unit 2 | refused: protected (error 2); bad address (error -5) |
| Guard refusals during the tests | 0 |
| Revision B (M25P128) | untested: no revision B unit on the bench |
| Erase and write of the whole main slot (flashmon) | 52 sectors erased about 41 s; written and read back 93.6 s; CRC32 B746E0F8 equal; the unit boots from it (`amiga/tools/flashmon/README.md` 6) |
