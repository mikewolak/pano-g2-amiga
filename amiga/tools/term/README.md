# term: VT100/VT220 Terminal for serialmon

A VT100/VT220 terminal for AmigaOS 3 on the NanoMig core of the Pano Logic G2, opened from serialmon (`V`, the
Terminal button, or the Project menu). libvterm 0.3.3 (`amiga/third_party/libvterm`, Paul Evans, MIT) parses the
byte stream and keeps the terminal state; `term.c` keeps the cells, draws them and scrolls the display. Mike Wolak,
EPROM Foundry Inc., pano-g2-amiga.

## 1 Function

| Item | Description |
|---|---|
| Presentations | A sizeable window on serialmon's screen (20 x 4 cells to the whole screen; ANSI colours drawn in serialmon's pens), or full screen: a screen of its own in serialmon's display mode and size, 16 ANSI colours, no borders, the pointer blanked. Right Amiga F switches; the cells are kept and the terminal takes the new size |
| Scrolling | Full screen: smooth, the display offset moved by 2 pixel lines a frame toward the target (faster as it falls behind; a jump beyond the ring) by RasInfo RyOffset and ScrollVPort. Window: jump scrolling, the whole area blitted at most every second frame after a scroll |
| Drawing | Cells drawn off the display into a ring of text lines and shown by the scroll (full screen) or by a blit (window); 8 x 8 cells from the ROM topaz 8 (ISO 8859-1) and glyphs for the DEC special graphics; bold (bright colours), underline, reverse, conceal; double-width (DECDWL) and double-height (DECDHL) lines, glyph bits doubled |
| Characters | 8-bit as a VT220: C1 controls 0x80-0x9F, 0xA0-0xFF ISO 8859-1; `term_set_utf8` for a UTF-8 host |
| Keys | The keymap's characters; cursor keys, F1-F10, Del, Backspace, Return, Enter, Tab, Esc as VT220 sequences (libvterm); Right Amiga F full screen or window, Right Amiga S smooth or jump scrolling, Right Amiga U send a file (serialmon's transfer panel), Shift Up and Down scrollback, Right Amiga Q (or the close gadget) closes |
| Scrollback | Lines scrolled off the top (a full-width scroll from row 0) kept in 256 KB of cells (819 lines at 80 columns); Shift Up and Down page, the bottom row shows the place, input waits meanwhile; any other key goes back |
| Data | While open, serialmon's received bytes go to the terminal (not to its monitor); keys and the terminal's answers (device attributes, cursor reports) go out through serialmon's transmit ring |

## 2 Files

| File | Content |
|---|---|
| `term.c`, `term.h` | The terminal: libvterm's state callbacks, the cells and ring, the two presentations, pacing, keys |
| `draw4.s` | 68020 assembler: four cells of one colour pair, glyph rows in d0-d7, each plane written from registers |
| `vtplay.c` | Test player: a file through the terminal at a set rate (`vtplay FILE [RATE=bytes/s] [HOLD=s] [BUDGET=us] [WINDOW] [JUMP] [MODE=id WIDTH=w HEIGHT=h]`), the figures printed at the end |
| `test/` | libvterm's own tests run on the Amiga (`test/README.md`): 43 of 43 pass |

## 3 Speed

Measured on a G2 revision C unit (68020 core, fast RAM 8 MB, NTSC 640 x 200), vtplay with 300 coloured lines (24,300 bytes,
80 x 25 full screen; the window 60 x 18 on the Workbench):

| Stage | Bytes/s | libvterm | Drawing a cell |
|---|---|---|---|
| libvterm screen layer, its cells read back (first version) | 1,676 | 235 us a byte | 97 us |
| State layer, cells kept here, C drawing | 4,137 | 63 us a byte | 42 us |
| Plain-text path past libvterm (`term.c` 4a) | 9,499 | 15 us a byte | 43 us |
| `draw4.s` for one-colour groups | 10,891 | 15 us a byte | 24 us |
| Input budgeted by the drawing cost (full screen) | 9,770 to 10,451 | 16 us a byte | 24 us |
| Window, the whole area blitted at most every second frame | 5,651 | 22 us a byte | 26 us |
| Mirror lines written with the cells (no blitter copy): full screen | 11,842 | 16 us a byte | 29 us |
| The same, window | 6,117 | 22 us a byte | 32 us |
| Full screen, NTSC interlace 640 x 400 (80 x 50; `vtplay MODE=00019004`) | 8,396 | 17 us a byte | 32 us |
| Full screen, PAL 640 x 256 (80 x 32, 50 frames a second; `MODE=00029000`) | 7,751 | 17 us a byte | 30 us |

The view offset is set at the start of each update, after the vertical blank when the caller ticks there, with
the lines drawn in the update before. The firmware's `snap` copies a finished capture buffer while the capture
writes the next ones, so pictures of a moving display can show a seam (the seam was seen in such pictures; the
display itself has not been checked by camera).

At 5,760 bytes/s (57,600 baud) full screen keeps up with 0 jumps, 22 of 303 updates over the 10 ms budget; at
2,000 bytes/s both presentations keep up. Above about 10,000 bytes/s (full screen) or 5,600 bytes/s (window) the
input waits in the terminal's 64 KB queue and the full-screen scroll jumps. libvterm's own text path costs 44 us a
byte on this core with empty callbacks (`vtbench`); the plain-text path places printable ASCII directly when
libvterm's parser and state would only place glyphs.

## 4 Untested

Mouse reporting, the window mode on screens other
than serialmon's and the Workbench.  In PAL the G2's video path shows 240 lines while the Amiga is set to NTSC
(AMIGA.INI), so a PAL screen's last 16 lines are not seen there; the interlaced mode was shown in full (the firmware
reports a 484-line interlaced source).
