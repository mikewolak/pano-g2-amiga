# ftdiflash

## 1 Origin

| Item | Value |
|---|---|
| Package | ftdiflash, a SPI flash programmer for FTDI MPSSE chips: Clifford Wolf's iceprog (Project IceStorm, 2015) modified by Dean Miller, Adafruit Industries (2017) |
| Source | `https://github.com/adafruit/ftdiflash`, commit `242ae956cbfc0652e862db4a381a26dfa275a5e8` (2017-06-28) |
| Copy | `ftdiflash.c`, `README.md`, `Makefile`, `config.mk` of that commit, unmodified |
| Licence | ISC-style permission notice in the header of `ftdiflash.c` (use, copy, modify and distribute with the notice kept) |

| File | sha256 |
|---|---|
| `ftdiflash.c` | `a91c001ff9ded48e7b83059412b1f61e15cb8ed7f562dd83ab5ba8c84edf3690` |
| `README.md` | `00535ed3f473ea7e39b538d09268754b08c3dbee78bb5d76d22d991f46e6d259` |
| `Makefile` | `50296de6501f48b27a94fe9e4eaa7df52fcb7830af8734eb689f3913e779e119` |
| `config.mk` | `152a004b213d7b79ab614234e4da8d73de760593c25d49e8cae8702c9240c0a7` |

## 2 Use in this repository

`amiga/tools/ftdimon` ports ftdiflash's SPI flash procedures (`flash_read_id`, `flash_power_up`, `flash_power_down`,
`flash_write_enable`, `flash_bulk_erase`, `flash_64kB_sector_erase`, `flash_prog`, `flash_read`, `flash_wait`) and its
MPSSE pin handling (`set_gpio`: SCK ADBUS0, MOSI ADBUS1, MISO ADBUS2, chip select ADBUS4, CRESET ADBUS7) onto the
MPSSE stream of `amiga/tools/jtag/mpsse.c`, with libftdi's calls replaced by that stream.  ftdiflash identifies no part:
it drives any SPI NOR flash with these commands and 24-bit addresses.  The part names, sizes and erase layouts come
from `amiga/third_party/flashrom`.
