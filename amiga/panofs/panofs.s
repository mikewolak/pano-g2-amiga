; Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
; SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
; pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
; amiga/panofs/panofs.s -- PANOFS, the Amiga side of the firmware's volume handshake.
;
; Polls the mailbox block of HD0 (block 2 of the image: cylinder 0, before the partition at cylinder 2; the firmware
; answers it from memory, fw/amiga/amiga_main.c sdc_service) through scsi.device unit 0 every 10 ticks (0.2 s).  A
; new request in it is carried out on the volume DH0: with dos.library Inhibit and acknowledged by writing the block
; back; the firmware takes the written block as the acknowledgement and does not write it to the image.  Every 25
; polls (5 s) the block is written with the machine's figures, read by the firmware's "amiga" console command.
;
; MAILBOX BLOCK, read (firmware to PANOFS), longs, big-endian:
;   0  'PFS1'       4  sequence number of the request       8  request: 0 none, 1 inhibit, 2 release
; MAILBOX BLOCK, written (PANOFS to firmware):
;   0  'PFAK'       4  sequence number acknowledged          8  request acknowledged (0: a figures-only write)
;  12  result: 0 done, 1 Inhibit returned FALSE              16 PANOFS version (1)
;  20  exec.library version << 16 | revision                 24 free chip memory (AvailMem MEMF_CHIP)
;  28  free fast memory (MEMF_FAST)                          32 largest free chip block   36 largest free fast block
;  40  polls since start                                     44 1 while DH0: is inhibited by PANOFS
; The request found in the block when PANOFS starts is taken as already handled.
;
; Start: "Run >NIL: C:PANOFS" in S:User-Startup.  Ends on CTRL-C (Break), releasing DH0: first if it holds it.
; Needs Kickstart 2.0 (dos.library 36: Inhibit, PutStr; exec 36: CreateMsgPort, CreateIORequest).
;
; LIBRARY CALLS AND REGISTERS (offsets from the NDK 3.2 lvo/*.i; exec and dos calls may change d0-d1/a0-a1):
;   exec AllocMem(d0 size, d1 flags) -> d0       exec FreeMem(a1, d0)           exec AvailMem(d1 flags) -> d0
;   exec OpenLibrary(a1 name, d0 version) -> d0  exec CloseLibrary(a1)
;   exec CreateMsgPort() -> d0                   exec DeleteMsgPort(a0)
;   exec CreateIORequest(a0 port, d0 size) -> d0 exec DeleteIORequest(a0)
;   exec OpenDevice(a0 name, d0 unit, a1 io, d1 flags) -> d0 (0 = opened)       exec CloseDevice(a1)
;   exec DoIO(a1 io) -> d0 (io_Error)            exec SetSignal(d0 new, d1 mask) -> d0 old signals
;   dos  Delay(d1 ticks)                         dos  Inhibit(d1 name, d2 flag) -> d0 (DOSTRUE on success)
;   dos  PutStr(d1 string)
; Register use here: a5 the variable block (V_*), a6 the library base of the call, d5 the sequence number and d6
; the request being handled.

        include "exec/types.i"
        include "exec/io.i"
        include "exec/memory.i"
        include "exec/libraries.i"
        include "dos/dos.i"
        include "lvo/exec_lib.i"
        include "lvo/dos_lib.i"

MBX_OFFSET      equ     1024            ; block 2
POLL_TICKS      equ     10
STATS_POLLS     equ     25
VERSION         equ     1

V_DOS           equ     0               ; dos.library base
V_PORT          equ     4
V_IO            equ     8               ; IOStdReq for scsi.device unit 0
V_BUF           equ     12              ; 1,024 bytes: the block read at 0, the block written at 512
V_SEQ           equ     16              ; sequence number last handled
V_POLLS         equ     20
V_INH           equ     24              ; 1 while DH0: is inhibited
V_OPEN          equ     28              ; 1 once the device is open
V_BASE          equ     32              ; 1 once the first block has been read
V_REQ           equ     36              ; request last handled
V_RES           equ     40              ; its result
V_SIZE          equ     44

start:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  4.w,a6
        move.l  #V_SIZE,d0
        move.l  #MEMF_PUBLIC|MEMF_CLEAR,d1
        jsr     _LVOAllocMem(a6)
        tst.l   d0
        beq     .novars
        move.l  d0,a5
        lea     dosname(pc),a1
        moveq   #36,d0
        jsr     _LVOOpenLibrary(a6)
        move.l  d0,V_DOS(a5)
        beq     .cleanup
        jsr     _LVOCreateMsgPort(a6)
        move.l  d0,V_PORT(a5)
        beq     .cleanup
        move.l  d0,a0
        moveq   #IOSTD_SIZE,d0
        jsr     _LVOCreateIORequest(a6)
        move.l  d0,V_IO(a5)
        beq     .cleanup
        move.l  #1024,d0
        move.l  #MEMF_PUBLIC|MEMF_CLEAR,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_BUF(a5)
        beq     .cleanup
        lea     devname(pc),a0
        moveq   #0,d0
        move.l  V_IO(a5),a1
        moveq   #0,d1
        jsr     _LVOOpenDevice(a6)
        tst.b   d0
        bne     .nodev
        move.l  #1,V_OPEN(a5)
        lea     msg_start(pc),a0
        bsr     say

.loop:
        move.l  4.w,a6
        moveq   #0,d0
        moveq   #0,d1
        jsr     _LVOSetSignal(a6)
        btst    #SIGBREAKB_CTRL_C,d0
        bne     .break
        move.l  V_DOS(a5),a6
        moveq   #POLL_TICKS,d1
        jsr     _LVODelay(a6)
        addq.l  #1,V_POLLS(a5)
        bsr     readblk
        tst.l   d0
        bne     .loop                   ; a read error: the next poll tries again
        move.l  V_BUF(a5),a0
        cmp.l   #'PFS1',(a0)
        bne     .figures
        move.l  4(a0),d5
        tst.l   V_BASE(a5)
        bne     .based
        move.l  #1,V_BASE(a5)           ; the request present at start is not carried out
        move.l  d5,V_SEQ(a5)
        bra     .figures
.based:
        cmp.l   V_SEQ(a5),d5
        beq     .figures
        move.l  d5,V_SEQ(a5)
        move.l  8(a0),d6
        move.l  d6,V_REQ(a5)
        clr.l   V_RES(a5)
        cmp.l   #1,d6
        beq     .inhibit
        cmp.l   #2,d6
        beq     .release
        bra     .ack
.inhibit:
        moveq   #DOSTRUE,d2
        bsr     inhibit
        tst.l   d0
        beq     .failed
        move.l  #1,V_INH(a5)
        bra     .ack
.release:
        moveq   #DOSFALSE,d2
        bsr     inhibit
        tst.l   d0
        beq     .failed
        clr.l   V_INH(a5)
        bra     .ack
.failed:
        move.l  #1,V_RES(a5)
.ack:
        bsr     writeblk
        bra     .loop
.figures:
        move.l  V_POLLS(a5),d0
        divu    #STATS_POLLS,d0
        swap    d0
        tst.w   d0
        bne     .loop
        clr.l   V_REQ(a5)               ; a figures-only write carries request 0
        bsr     writeblk
        bra     .loop

.break:
        tst.l   V_INH(a5)
        beq     .ended
        moveq   #DOSFALSE,d2
        bsr     inhibit
.ended:
        lea     msg_end(pc),a0
        bsr     say
        bra     .cleanup
.nodev:
        lea     msg_nodev(pc),a0
        bsr     say
.cleanup:
        move.l  4.w,a6
        tst.l   V_OPEN(a5)
        beq     .c1
        move.l  V_IO(a5),a1
        jsr     _LVOCloseDevice(a6)
.c1:    move.l  V_BUF(a5),d0
        beq     .c2
        move.l  d0,a1
        move.l  #1024,d0
        jsr     _LVOFreeMem(a6)
.c2:    move.l  V_IO(a5),d0
        beq     .c3
        move.l  d0,a0
        jsr     _LVODeleteIORequest(a6)
.c3:    move.l  V_PORT(a5),d0
        beq     .c4
        move.l  d0,a0
        jsr     _LVODeleteMsgPort(a6)
.c4:    move.l  V_DOS(a5),d0
        beq     .c5
        move.l  d0,a1
        jsr     _LVOCloseLibrary(a6)
.c5:    move.l  a5,a1
        move.l  #V_SIZE,d0
        jsr     _LVOFreeMem(a6)
.novars:
        movem.l (sp)+,d2-d7/a2-a6
        moveq   #RETURN_OK,d0
        rts

; dos Inhibit("DH0:", d2) -> d0
inhibit:
        move.l  V_DOS(a5),a6
        lea     volname(pc),a0
        move.l  a0,d1
        jsr     _LVOInhibit(a6)
        rts

; the mailbox block into V_BUF -> d0 io_Error (0 on success)
readblk:
        move.l  4.w,a6
        move.l  V_IO(a5),a1
        move.w  #CMD_READ,IO_COMMAND(a1)
        move.l  V_BUF(a5),IO_DATA(a1)
        bra     doblk

; the acknowledgement and the figures from V_BUF + 512 into the mailbox block -> d0 io_Error
writeblk:
        move.l  4.w,a6
        move.l  V_BUF(a5),a2
        lea     512(a2),a2
        move.l  #'PFAK',(a2)
        move.l  V_SEQ(a5),4(a2)
        move.l  V_REQ(a5),8(a2)
        move.l  V_RES(a5),12(a2)
        move.l  #VERSION,16(a2)
        move.w  LIB_VERSION(a6),20(a2)
        move.w  LIB_REVISION(a6),22(a2)
        move.l  #MEMF_CHIP,d1
        jsr     _LVOAvailMem(a6)
        move.l  d0,24(a2)
        move.l  #MEMF_FAST,d1
        jsr     _LVOAvailMem(a6)
        move.l  d0,28(a2)
        move.l  #MEMF_CHIP|MEMF_LARGEST,d1
        jsr     _LVOAvailMem(a6)
        move.l  d0,32(a2)
        move.l  #MEMF_FAST|MEMF_LARGEST,d1
        jsr     _LVOAvailMem(a6)
        move.l  d0,36(a2)
        move.l  V_POLLS(a5),40(a2)
        move.l  V_INH(a5),44(a2)
        move.l  V_IO(a5),a1
        move.w  #CMD_WRITE,IO_COMMAND(a1)
        move.l  a2,IO_DATA(a1)
doblk:
        move.l  #512,IO_LENGTH(a1)
        move.l  #MBX_OFFSET,IO_OFFSET(a1)
        jsr     _LVODoIO(a6)
        ext.w   d0
        ext.l   d0
        rts

; dos PutStr(a0)
say:
        move.l  V_DOS(a5),a6
        move.l  a0,d1
        jsr     _LVOPutStr(a6)
        rts

dosname:        dc.b    "dos.library",0
devname:        dc.b    "scsi.device",0
volname:        dc.b    "DH0:",0
msg_start:      dc.b    "PANOFS 1: DH0: (scsi.device unit 0) handed to the firmware on request; CTRL-C ends",10,0
msg_nodev:      dc.b    "PANOFS: scsi.device unit 0 does not open",10,0
msg_end:        dc.b    "PANOFS: ended",10,0
        even
