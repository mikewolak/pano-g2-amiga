| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
| SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
| pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
| tstub.s -- PanoTCP's call trace: one 10-byte stub for each library vector, used in place of the function when PanoTCP
| is started with TRACE.  A stub pushes its vector number and enters trace_common, which records the call (trace_in
| in panotcp.c, given the registers), calls the function with the registers it was given, records the result
| (trace_out) and returns the function's d0 and d1 to the caller.
|
| Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.

	.text
	.globl	_trace_stubs
_trace_stubs:
	.set	k,1
	.rept	136
	move.l	#k,-(sp)
	bra.w	trace_common
	.set	k,k+1
	.endr

	.if	(. - _trace_stubs) - 1360	| the C side finds stub k at _trace_stubs + 10 (k - 1)
	.err
	.endif

trace_common:				| stack: vector number, the caller's return address
	movem.l	d0-d7/a0-a6,-(sp)	| 60 bytes of registers
	move.l	sp,-(sp)
	jsr	_trace_in		| d0: the function
	addq.l	#4,sp
	move.l	d0,-(sp)		| stack: function, registers, vector number, return address
	movem.l	4(sp),d0-d7/a0-a6
	pea	trace_back(pc)
	move.l	4(sp),-(sp)
	rts				| into the function, which returns to trace_back
trace_back:
	move.l	d0,4(sp)		| the results over the saved d0 and d1
	move.l	d1,8(sp)
	pea	4(sp)
	jsr	_trace_out
	addq.l	#8,sp			| the argument and the function
	movem.l	(sp)+,d0-d7/a0-a6
	addq.l	#4,sp			| the vector number
	rts
