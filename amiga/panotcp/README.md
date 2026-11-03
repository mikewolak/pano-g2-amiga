# PanoTCP: bsdsocket.library on the G2 firmware's TCP/IP stack

`bsdsocket.library` version 4.1 for AmigaOS 3.x on the G2 NanoMig core.  The library passes every socket call to the
firmware's socket service (`fw/amiga/sock.c`), which runs the sockets on lwIP; the Amiga side holds no protocol code.
Design, the window protocol and the measurements: `docs/G2_NETWORK.pdf` and `docs/G2_AMIGA_TECHNICAL_REFERENCE.pdf`.  Mike Wolak, EPROM Foundry Inc.,
pano-g2-amiga.

## 1 Files

| File | Content |
|---|---|
| `panotcp.c` | the library (one base per opener) and the PanoTCP process that makes it |
| `tstub.s` | the call trace's vector stubs (PanoTCP TRACE) |
| `pnwin.c`, `pnwin.h` | the socket window on HD0 through `scsi.device` |
| `sktest.c` | the window driven directly: state, name lookup, TCP stream to or from `tools/sksrv.py` |
| `bsdtest.c` | the library through `proto/bsdsocket.h`: lookup, connect, recv, WaitSelect, send |
| `tools/sksrv.py` | the host side of `sktest` and `bsdtest` (port 5001: `GET n`, `PUT n`) |

## 2 Build and Install

1. `make` (m68k-amigaos-gcc 6.5.0b, bebbo amiga-gcc): `PanoTCP`, `sktest`, `bsdtest`.
2. `AMIGA.FW` built from this tree (`make -C fw/amiga amiga_fw`, which defines `AMIGA_SOCK`) on the stick, and the
   unit restarted (`reboot`).
3. `PanoTCP` to `C:`.  Start: `run >NIL: C:PanoTCP`.  Started from `S:User-Startup`, it must follow the programs that
   are to keep AmiTCP's library (AmiTCP's `startnet` and the programs it runs), for example through a script with
   `Wait 5` before `C:PanoTCP`.

## 3 Use

| Command | Effect |
|---|---|
| `PanoTCP` | makes the library; waits; CTRL-C removes it once no program has it open |
| `PanoTCP TRACE` | the same, with every call recorded |
| `PanoTCP DUMP` | prints the recorded calls of the running PanoTCP |
| `sktest stat` | the window's state: address, name servers, sockets |
| `sktest get <host> <port> <bytes>`, `put` | a TCP stream through the window, with its rate |
| `bsdtest <host> <port> get\|put\|sel <bytes>` | the same through the library |

The firmware's console command `sock` shows the sockets, the MAC's frame counts and the main loop's timing.

## 4 Programs Run on It

| Program | Result |
|---|---|
| LumiFTP 1.2 (Matthias Muench, freeware) | connects (non-blocking connect, WaitSelect, SO_ERROR), logs in, downloads and uploads over plain FTP; address-book profile "EPROM Foundry" stored |
| bebbossh / bebboscp 1.46.1 (Stefan Franke, fixes by Matthias Muench) | host-key check, password login, SFTP download |
