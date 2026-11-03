# libvterm 0.3.3

## 1 Origin

| Item | Value |
|---|---|
| Package | libvterm 0.3.3, Paul Evans, <https://www.leonerd.org.uk/code/libvterm/> |
| Archive | `libvterm-0.3.3.tar.gz`, sha256 `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0` |
| Copy | `libvterm-0.3.3/`, the archive extracted, unmodified (the generated `src/encoding/*.inc` as shipped) |
| Licence | MIT (`libvterm-0.3.3/LICENSE`) |

## 2 Use in this repository

The VT100/VT220 emulation of the serialmon terminal (`amiga/tools/term`). Built for AmigaOS with
`m68k-amigaos-gcc -std=gnu99 -m68020 -O2 -Iinclude '-Dvisibility(x)='`: the GNU assembler of the Amiga target has no
`.internal` directive, and the define empties the `visibility("internal")` attribute of `src/vterm_internal.h`
without an edit.

## 3 Test on the target

`amiga/tools/term/test`: the library's own 43 tests (`t/*.test`) run on a G2 revision C unit (68020 core): 43 pass.
