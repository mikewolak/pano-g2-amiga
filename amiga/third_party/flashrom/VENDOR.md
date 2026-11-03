# flashrom flash chip table

## 1 Origin

| Item | Value |
|---|---|
| Package | flashrom 1.4.0, the flashrom project (Carl-Daniel Hailfinger and contributors; copyright holders in each file) |
| Archive | `https://github.com/flashrom/flashrom/archive/refs/tags/v1.4.0.tar.gz`, sha256 `b77ce458b3b874387270a71805e899ac63f057d7553dc320da7e4c58e750577c` |
| Copy | `flashchips.c`, `include/flashchips.h` and `COPYING` of that archive, unmodified; the rest of flashrom is not taken |
| Licence | GNU General Public License version 2 or later (`COPYING`, file headers) |

| File | sha256 |
|---|---|
| `flashchips.c` | `ad07a9eeb47abf29653901cd8312733f6fc40e6c804e09ce45cc55ea9797b9f4` |
| `include/flashchips.h` | `831dd5b17ea77b71eccb907ca71e1fe9f48337db61d2bf74cd5c4178dc9de4e2` |
| `COPYING` | `ab15fd526bd8dd18a9e77ebc139656bf4d33e97fc7238cd11bf60e2b9b8666c6` |

## 2 Use in this repository

`amiga/tools/ftdimon/flashchips_gen.py` reads these two files at build time and writes `flashchips_tab.c`: the 402
SPI NOR parts identified by JEDEC ID (0x9F), REMS (0x90) or RES (0xAB) and written by page program, byte program or
SST AAI, each with vendor, name, IDs, size, page size, 4-byte addressing, supply range and erase opcodes with their
block layouts.  ftdimon is therefore distributed under GPL-2.0-or-later.
