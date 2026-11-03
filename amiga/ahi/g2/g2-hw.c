/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
 * Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
 */
/*
 *  Toccata register access: AD1848 format and DAC level, FIFO refill and
 *  the level 6 interrupt server.  Register offsets and status bits are in
 *  toccata.h; the behaviour relied on is that of
 *  amiga/third_party/fpga-toccata/toccata.sv, toccata_fifo.sv and
 *  rtl/src/toccata_playback.sv.
 */

#include <config.h>

#include <exec/types.h>
#include <proto/exec.h>

#include "library.h"
#include "DriverData.h"
#include "toccata.h"

/******************************************************************************
** AD1848 *********************************************************************
******************************************************************************/

/* Index 8 is written inside a mode change (MCE set), as the AD1848 data
 * sheet requires; the index register then reads AD_INIT until the codec
 * is ready.  A change of the stereo or companding bit resets the card's
 * playback FIFO (toccata_playback.sv), so this is called before the FIFO
 * is filled. */

VOID
G2CodecFormat( volatile UBYTE* board, UBYTE fmt )
{
  ULONG i;

  board[ TOCC_CODEC_ADDR ] = AD_MCE | AD_FORMAT;
  board[ TOCC_CODEC_REG ]  = fmt;
  board[ TOCC_CODEC_ADDR ] = AD_FORMAT;

  for( i = 0; i < 100000 && ( board[ TOCC_CODEC_ADDR ] & AD_INIT ); i++ )
  {
  }
}

/* Indices 6 and 7: bits 5:0 attenuation in 1.5 dB steps, bit 7 mute.  The
 * control writes 0x95 and 0x01 set or clear bit 7 only. */

VOID
G2CodecVolume( volatile UBYTE* board, UBYTE att, BOOL mute )
{
  UBYTE v = ( att & 0x3f ) | ( mute ? 0x80 : 0x00 );

  board[ TOCC_CODEC_ADDR ] = AD_LDAC;
  board[ TOCC_CODEC_REG ]  = v;
  board[ TOCC_CODEC_ADDR ] = AD_RDAC;
  board[ TOCC_CODEC_REG ]  = v;
}


/******************************************************************************
** FIFO refill ****************************************************************
******************************************************************************/

/* Moves 'bytes' (a multiple of 4) from the ring into the FIFO.  A 16-bit
 * write to the FIFO stores D7:0 first and D15:8 second (toccata.sv), and
 * the playback side assembles each sample low byte first, so a big-endian
 * sample word is written unchanged.  When the ring holds less than 'bytes'
 * the rest is silence: the half-empty interrupt is an edge as the fill
 * falls through 504, and a FIFO left below that never interrupts again. */

VOID
G2FifoWrite( struct G2Data* dd, ULONG bytes )
{
  volatile UWORD* fifo = (volatile UWORD*)
                         ( dd->ahisubbase->board + TOCC_FIFO_DATA );
  ULONG rd    = dd->ringrd;
  ULONG avail = dd->ringwr - rd;
  ULONG pad   = 0;
  ULONG n;

  if( avail < bytes )
  {
    pad   = bytes - avail;
    bytes = avail;
    dd->underruns++;
  }

  while( bytes != 0 )
  {
    ULONG        off = rd & dd->ringmask;
    ULONG        seg = dd->ringsize - off;
    const UWORD* src = (const UWORD*) ( dd->ring + off );

    if( seg > bytes )
    {
      seg = bytes;
    }

    rd    += seg;
    bytes -= seg;

    for( n = seg >> 3; n != 0; n-- )    // two frames per pass
    {
      *fifo = *src++;
      *fifo = *src++;
      *fifo = *src++;
      *fifo = *src++;
    }

    if( seg & 4 )
    {
      *fifo = *src++;
      *fifo = *src++;
    }
  }

  dd->ringrd = rd;

  for( n = pad >> 2; n != 0; n-- )
  {
    *fifo = 0;
    *fifo = 0;
  }
}


/******************************************************************************
** Level 6 interrupt **********************************************************
******************************************************************************/

/* INTB_EXTER server.  Reading the status byte clears the card's interrupt.
 * Returns 0 when the card is not interrupting so the chain continues. */

ULONG
G2Interrupt( struct G2Data* dd )
{
  struct DriverBase* AHIsubBase = (struct DriverBase*) dd->ahisubbase;
  volatile UBYTE*    board      = dd->ahisubbase->board;
  UBYTE              status;

  status = board[ TOCC_STATUS ];

  if( status & TOCC_FIFO_INT )          // active low: not this card
  {
    return 0;
  }

  if( ( status & TOCC_FIFO_PBHE ) && dd->playing )
  {
    G2FifoWrite( dd, TOCC_FIFO_HALF );

    if( dd->ringwr - dd->ringrd < dd->highwater )
    {
      Signal( (struct Task*) dd->slavetask, 1UL << dd->playsignal );
    }

    return 1;
  }

  // Unexpected interrupt: switch the card's interrupts off.

  board[ TOCC_STATUS ] = TOCC_ACT;
  return 1;
}

/* Server entry: is_Data in A1.  Exec tests the Z flag on return, so D0 is
 * tested here rather than relying on the compiler's last instruction. */

asm( "	.text\n"
     "	.even\n"
     "	.globl	_G2IntEntry\n"
     "_G2IntEntry:\n"
     "	move.l	%a1,-(%sp)\n"
     "	jsr	_G2Interrupt\n"
     "	addq.l	#4,%sp\n"
     "	tst.l	%d0\n"
     "	rts\n" );
