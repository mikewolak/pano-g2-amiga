| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
| SPDX-License-Identifier: GPL-2.0-or-later
| pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
| in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
| entry.s -- panoflash.device: the first code of the segment and the register-to-stack entry stubs (the stubs of
| amiga/serial/entry.s without its interrupt stubs).
|
| Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
|
| Exec calls the device vectors with arguments in registers (Open: a1 request, d0 unit, d1 flags; Close, BeginIO,
| AbortIO: a1 request; all: a6 device base).  Each stub pushes them as the C functions in panoflash.c take them.

	.text
	moveq	#-1,d0			| the segment run as a program: return at once
	rts

	.globl	_dev_init_stub
_dev_init_stub:				| d0 base, a0 segment list, a6 SysBase
	move.l	a6,-(sp)
	move.l	a0,-(sp)
	move.l	d0,-(sp)
	jsr	_dev_init
	lea	12(sp),sp
	rts

	.globl	_dev_open_stub
_dev_open_stub:
	move.l	a6,-(sp)
	move.l	d1,-(sp)
	move.l	d0,-(sp)
	move.l	a1,-(sp)
	jsr	_dev_open
	lea	16(sp),sp
	rts

	.globl	_dev_close_stub
_dev_close_stub:
	move.l	a6,-(sp)
	move.l	a1,-(sp)
	jsr	_dev_close
	addq.l	#8,sp
	rts

	.globl	_dev_expunge_stub
_dev_expunge_stub:
	move.l	a6,-(sp)
	jsr	_dev_expunge
	addq.l	#4,sp
	rts

	.globl	_dev_null_stub
_dev_null_stub:
	moveq	#0,d0
	rts

	.globl	_dev_beginio_stub
_dev_beginio_stub:
	move.l	a6,-(sp)
	move.l	a1,-(sp)
	jsr	_dev_beginio
	addq.l	#8,sp
	rts

	.globl	_dev_abortio_stub
_dev_abortio_stub:
	move.l	a6,-(sp)
	move.l	a1,-(sp)
	jsr	_dev_abortio
	addq.l	#8,sp
	rts

