| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
| SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
| pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
| entry.s -- g2serial.device: the first code of the segment and the register-to-stack entry stubs.
|
| Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  New implementation from the functional specification
| a functional analysis of BaudBandit.device 1.4b.
|
| Exec calls the device vectors with arguments in registers (Open: a1 request, d0 unit, d1 flags; Close, BeginIO,
| AbortIO: a1 request; all: a6 device base) and interrupt code with a1 = is_Data.  Each stub pushes them as the C
| functions in g2serial.c take them (gcc: arguments on the stack, d0/d1/a0/a1 scratch, d2-d7/a2-a6 preserved).
| The INT2 server returns with the Z flag clear when the board was the source, as exec's server chain requires.

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

	.globl	_rbf_stub
_rbf_stub:				| SetIntVector(INTB_RBF): a0 custom, a1 is_Data
	move.l	a1,-(sp)
	jsr	_rbf_int
	addq.l	#4,sp
	rts

	.globl	_tbe_stub
_tbe_stub:				| SetIntVector(INTB_TBE)
	move.l	a1,-(sp)
	jsr	_tbe_int
	addq.l	#4,sp
	rts

	.globl	_ports_stub
_ports_stub:				| AddIntServer(INTB_PORTS)
	move.l	a1,-(sp)
	jsr	_ports_int
	addq.l	#4,sp
	tst.l	d0
	rts

	.globl	_soft_stub
_soft_stub:				| PA_SOFTINT port's software interrupt
	move.l	a1,-(sp)
	jsr	_soft_int
	addq.l	#4,sp
	rts
