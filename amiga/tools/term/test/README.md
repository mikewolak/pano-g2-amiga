# libvterm tests on the Amiga

libvterm's `t/run-test.pl` drives its harness interactively through a pipe. Here the harness runs on the Amiga and
the comparison on the host:

| Step | Where | Command |
|---|---|---|
| 1 | host | `vtt.py gen T.test T.in` for each test: the harness commands in order (run-test.pl's conversion of the test's Perl strings, parsed by vtt.py) |
| 2 | Amiga | `harness <T.in >T.out` (`harness.c` here, built with the library: `m68k-amigaos-gcc -std=gnu99 -noixemul -m68020 -O2 -Iinclude '-Dvisibility(x)=' -o harness src/*.c harness.c`) |
| 3 | host | `vtt.py check T.test T.out`: every expectation and assertion as run-test.pl compares them |

`harness.c` is the library's `t/harness.c` with `strpe_color` reading `unsigned int` with `%u`: libnix's `sscanf`
returns 3 for `%hhu` but stores nothing (a test program on the unit: `rgb(252,253,254)` read as 0,0,0).

Result on a G2 revision C unit, 68020 core, libvterm 0.3.3: 43 of 43 tests pass.

Mike Wolak, EPROM Foundry Inc., pano-g2-amiga. `harness.c`: Paul Evans (MIT).
