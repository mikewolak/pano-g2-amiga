# lrzsz 0.12.20

## 1 Origin

| Item | Value |
|---|---|
| Package | lrzsz 0.12.20, Uwe Ohse's maintained version of Chuck Forsberg's rz/sz (Omen Technology) |
| Archive | `lrzsz-0.12.20.tar.gz`, sha256 `c28b36b14bddb014d9e9c97c52459852f97bd405f89113f30bee45ed92728ff1` |
| Copy | `lrzsz-0.12.20/`, the archive extracted, unmodified |
| Licence | GNU General Public License version 2 (`lrzsz-0.12.20/COPYING`) |

## 2 Use in this repository

`amiga/tools/xfer` builds `lsz` and `lrz` for AmigaOS from this tree: every source compiles unchanged except
`src/zreadline.c` (an edited copy in `amiga/tools/xfer`) and `src/rbsb.c` (replaced by `amiga/tools/xfer/rbsb_amiga.c`).
Programs built from it are GPL-2 and ship with their source.
