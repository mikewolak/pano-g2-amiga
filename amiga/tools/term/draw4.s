| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
| SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
| pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
| draw4.s -- four terminal cells of one colour pair into the 4 bitplanes (term.c, section 3), 68020.
|
| void term_draw4(UBYTE **pl, LONG off, LONG bpr, const UBYTE *g0, const UBYTE *g1, const UBYTE *g2,
|                 const UBYTE *g3, ULONG col, ULONG ul, LONG off2)
|
| The 8 glyph rows of the four cells are packed into d0-d7 (row r: g0[r] in bits 31-24 ... g3[r] in bits 7-0; row 7
| all ones when ul is set); then each plane p gets, for its 8 lines from pl[p] + off with a pitch of bpr bytes,
| 0 (foreground and background bit p clear), the glyph (foreground bit only), the inverted glyph (background bit
| only) or all ones (both).  col: foreground in bits 3-0, background in bits 7-4.  32 longword writes, 32 byte reads.
| off2 not 0: the same 8 lines again from pl[p] + off2 (the slot's mirror, term.c section 2), 32 writes more.
|
| Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.

	.text
	.even
	.globl	_term_draw4

| one glyph row into register \r: four bytes, the first in the top byte (the shifts move the rest out)
.macro	ROW r
	move.b	(a0)+,\r
	lsl.l	#8,\r
	move.b	(a1)+,\r
	lsl.l	#8,\r
	move.b	(a2)+,\r
	lsl.l	#8,\r
	move.b	(a3)+,\r
.endm

| the 8 lines of a plane from d0-d7
.macro	WREG
	move.l	d0,(a4)
	add.l	a5,a4
	move.l	d1,(a4)
	add.l	a5,a4
	move.l	d2,(a4)
	add.l	a5,a4
	move.l	d3,(a4)
	add.l	a5,a4
	move.l	d4,(a4)
	add.l	a5,a4
	move.l	d5,(a4)
	add.l	a5,a4
	move.l	d6,(a4)
	add.l	a5,a4
	move.l	d7,(a4)
.endm

.macro	NOTALL
	not.l	d0
	not.l	d1
	not.l	d2
	not.l	d3
	not.l	d4
	not.l	d5
	not.l	d6
	not.l	d7
.endm

| the 8 lines of a plane set to a constant
.macro	WCON v
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
	add.l	a5,a4
	move.l	#\v,(a4)
.endm

| plane \p at offset \o: its address, then one of the four cases by the colour bits (the stack frame: 44 bytes saved)
.macro	PLANE p o
	move.l	(a6),a4
	add.l	(4+44+\o,sp),a4
	btst	#\p,(4+44+28+3,sp)
	jne	1f
	btst	#\p+4,(4+44+28+3,sp)
	jne	2f
	WCON	0
	jra	9f
2:	NOTALL
	WREG
	NOTALL
	jra	9f
1:	btst	#\p+4,(4+44+28+3,sp)
	jne	3f
	WREG
	jra	9f
3:	WCON	-1
9:
.endm

_term_draw4:
	movem.l	d2-d7/a2-a6,-(sp)
	movem.l	(4+44+12,sp),a0-a3
	ROW	d0
	ROW	d1
	ROW	d2
	ROW	d3
	ROW	d4
	ROW	d5
	ROW	d6
	ROW	d7
	tst.l	(4+44+32,sp)
	jeq	0f
	moveq	#-1,d7
0:	move.l	(4+44,sp),a6
	move.l	(4+44+8,sp),a5
	PLANE	0 4
	addq.l	#4,a6
	PLANE	1 4
	addq.l	#4,a6
	PLANE	2 4
	addq.l	#4,a6
	PLANE	3 4
	tst.l	(4+44+36,sp)
	jeq	8f
	move.l	(4+44,sp),a6
	PLANE	0 36
	addq.l	#4,a6
	PLANE	1 36
	addq.l	#4,a6
	PLANE	2 36
	addq.l	#4,a6
	PLANE	3 36
8:	movem.l	(sp)+,d2-d7/a2-a6
	rts
