/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
 * Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
 */

#include <config.h>

#include <devices/ahi.h>
#include <exec/execbase.h>
#include <libraries/ahi_sub.h>

#include "DriverData.h"
#include "library.h"

#define dd ((struct G2Data*) AudioCtrl->ahiac_DriverData)

/******************************************************************************
** Mix buffer to ring *********************************************************
******************************************************************************/

/* Appends 'frames' frames of the mix buffer to the ring as 16-bit stereo,
 * big-endian words (the order G2FifoWrite() sends to the card).  32-bit
 * (HiFi) samples keep their upper 16 bits; mono goes to both channels. */

static void
MixToRing( struct G2Data* d, ULONG type, const void* mix, ULONG frames )
{
  ULONG  wr   = d->ringwr;
  ULONG  mask = d->ringmask;
  UBYTE* ring = d->ring;

  switch( type )
  {
    case AHIST_S16S:
    {
      const ULONG* src = mix;

      while( frames-- != 0 )
      {
        *(ULONG*) ( ring + ( wr & mask ) ) = *src++;
        wr += 4;
      }
      break;
    }

    case AHIST_M16S:
    {
      const UWORD* src = mix;

      while( frames-- != 0 )
      {
        ULONG s = *src++;

        *(ULONG*) ( ring + ( wr & mask ) ) = ( s << 16 ) | s;
        wr += 4;
      }
      break;
    }

    case AHIST_S32S:
    {
      const ULONG* src = mix;

      while( frames-- != 0 )
      {
        ULONG l = *src++;
        ULONG r = *src++;

        *(ULONG*) ( ring + ( wr & mask ) ) = ( l & 0xffff0000 ) | ( r >> 16 );
        wr += 4;
      }
      break;
    }

    case AHIST_M32S:
    {
      const ULONG* src = mix;

      while( frames-- != 0 )
      {
        ULONG s = *src++;

        *(ULONG*) ( ring + ( wr & mask ) ) = ( s & 0xffff0000 ) | ( s >> 16 );
        wr += 4;
      }
      break;
    }

    default:                            // not offered by the mode file
    {
      while( frames-- != 0 )
      {
        *(ULONG*) ( ring + ( wr & mask ) ) = 0;
        wr += 4;
      }
      break;
    }
  }

  d->ringwr = wr;                       // published after the data
}


/******************************************************************************
** The slave process **********************************************************
******************************************************************************/

#undef SysBase

void Slave( struct ExecBase* SysBase );

void SlaveEntry(void)
{
  struct ExecBase* SysBase = *((struct ExecBase**) 4);

  Slave( SysBase );
}

void
Slave( struct ExecBase* SysBase )
{
  struct AHIAudioCtrlDrv* AudioCtrl;
  struct DriverBase*      AHIsubBase;
  BOOL                    running;
  BOOL                    told;
  ULONG                   signals;
  ULONG                   stopmask;

  /* Note that in OS4, we cannot call FindTask(NULL) here, since IExec
   * is inside AHIsubBase! */
  AudioCtrl  = (struct AHIAudioCtrlDrv*) SysBase->ThisTask->tc_UserData;
  AHIsubBase = (struct DriverBase*) dd->ahisubbase;

  dd->playsignal  = AllocSignal( -1 );
  dd->slavesignal = AllocSignal( -1 );

  if( dd->slavesignal != -1 && dd->playsignal != -1 )
  {
    stopmask = SIGBREAKF_CTRL_C | (1L << dd->slavesignal);
    running  = TRUE;
    told     = FALSE;

    while( running )
    {
      // Mix while the ring is below its high-water mark.  The first time
      // it is reached, tell Master we're alive and the ring is full.

      while( dd->ringwr - dd->ringrd < dd->highwater )
      {
        if( SetSignal( 0L, 0L ) & stopmask )
        {
          break;
        }

        CallHookPkt( AudioCtrl->ahiac_PlayerFunc, AudioCtrl, NULL );
        CallHookPkt( AudioCtrl->ahiac_MixerFunc, AudioCtrl, dd->mixbuffer );

        // The mixing buffer is now filled with AudioCtrl->ahiac_BuffSamples
        // of sample frames (type AudioCtrl->ahiac_BuffType).

        MixToRing( dd, AudioCtrl->ahiac_BuffType, dd->mixbuffer,
                   AudioCtrl->ahiac_BuffSamples );
      }

      if( ! told )
      {
        told = TRUE;
        Signal( (struct Task*) dd->mastertask,
                1L << dd->mastersignal );
      }

      signals = Wait( stopmask | (1L << dd->playsignal) );

      if( signals & stopmask )
      {
        running = FALSE;
      }
    }
  }

  FreeSignal( dd->slavesignal );
  dd->slavesignal = -1;
  FreeSignal( dd->playsignal );
  dd->playsignal = -1;

  Forbid();

  // Tell the Master we're dying

  Signal( (struct Task*) dd->mastertask,
          1L << dd->mastersignal );

  dd->slavetask = NULL;

  // Multitaking will resume when we are dead.
}
