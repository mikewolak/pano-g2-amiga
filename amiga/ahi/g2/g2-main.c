/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
 * Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
 */
/*
 *  AHI driver for the Toccata Zorro II card of the G2 NanoMig core.
 *  Playback only.  AHI mixes in a slave process into a ring buffer of
 *  16-bit stereo frames; the card's level 6 half-empty interrupt moves 512
 *  bytes from the ring into its 1024-byte FIFO and wakes the slave when the
 *  ring falls below its high-water mark.  Recording is not supported.
 */

#include <config.h>

#include <devices/ahi.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <hardware/intbits.h>
#include <libraries/ahi_sub.h>
#include <proto/ahi_sub.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <stddef.h>

#include "library.h"
#include "DriverData.h"
#include "toccata.h"

#define dd (*(struct G2Data**) &AudioCtrl->ahiac_DriverData)

/* Slave process priority.  The ring holds about two mixing passes, so the
 * mixer has to run ahead of ordinary priority 0 tasks. */
#define SLAVE_PRI 5

void
SlaveEntry( void );

PROCGW( static, void,  slaveentry, SlaveEntry );


/******************************************************************************
** Rates and levels ***********************************************************
******************************************************************************/

static ULONG
FreqIndex( struct G2Base* G2Base, LONG argument )
{
  ULONG i;

  if( argument <= G2Base->freq[ 0 ] )
  {
    return 0;
  }

  if( argument >= G2Base->freq[ G2_RATES - 1 ] )
  {
    return G2_RATES - 1;
  }

  for( i = 1; i < G2_RATES; i++ )
  {
    if( G2Base->freq[ i ] > argument )
    {
      if( ( argument - G2Base->freq[ i - 1 ] ) <
          ( G2Base->freq[ i ] - argument ) )
      {
        return i-1;
      }
      else
      {
        return i;
      }
    }
  }

  return 0;  // Will not happen
}

/* DAC gain of attenuation step i is 0.8409^i (-1.5 dB per step), the factor
 * toccata_volume.sv applies (27553 / 32768).  Gains are kept as 2.30 fixed
 * point; a Fixed volume is 16.16. */

static ULONG
AttGain( UBYTE att )
{
  ULONG g = 1UL << 30;

  while( att-- != 0 )
  {
    g = ( g >> 15 ) * 27553;
  }

  return g;
}

static UBYTE
VolumeToAtt( Fixed volume )
{
  ULONG v;
  ULONG g = 1UL << 30;
  UBYTE att;

  if( volume >= 0x10000 )
  {
    return 0;
  }

  if( volume <= 0 )
  {
    return 63;
  }

  v = (ULONG) volume << 14;

  for( att = 0; att < 63; att++ )
  {
    ULONG next = ( g >> 15 ) * 27553;

    if( v >= next + ( g - next ) / 2 )  // nearest step
    {
      break;
    }

    g = next;
  }

  return att;
}


/******************************************************************************
** AHIsub_AllocAudio **********************************************************
******************************************************************************/

ULONG
_AHIsub_AllocAudio( struct TagItem*         taglist,
		    struct AHIAudioCtrlDrv* AudioCtrl,
		    struct DriverBase*      AHIsubBase )
{
  struct G2Base* G2Base = (struct G2Base*) AHIsubBase;
  ULONG          idx;

  // The card plays one stream: a second AllocAudio fails.

  Forbid();

  if( G2Base->inuse )
  {
    Permit();
    return AHISF_ERROR;
  }

  G2Base->inuse = TRUE;

  Permit();

  dd = AllocVec( sizeof( struct G2Data ),
		 MEMF_CLEAR | MEMF_PUBLIC );

  if( dd != NULL )
  {
    dd->slavesignal      = -1;
    dd->playsignal       = -1;
    dd->mastersignal     = AllocSignal( -1 );
    dd->mastertask       = (struct Process*) FindTask( NULL );
    dd->ahisubbase       = G2Base;
  }
  else
  {
    G2Base->inuse = FALSE;
    return AHISF_ERROR;
  }

  if( dd->mastersignal == -1 )
  {
    return AHISF_ERROR;
  }

  // The card runs in 16-bit stereo at the produced rate nearest the
  // request; AHI mixes at that rate.

  idx = FreqIndex( G2Base, AudioCtrl->ahiac_MixFreq );

  AudioCtrl->ahiac_MixFreq = G2Base->freq[ idx ];
  dd->fmt = AD_FMT_16 | AD_FMT_STEREO | G2Base->fmtbits[ idx ];

  return ( AHISF_KNOWHIFI |
	   AHISF_KNOWSTEREO |
	   AHISF_MIXING |
	   AHISF_TIMING );
}


/******************************************************************************
** AHIsub_FreeAudio ***********************************************************
******************************************************************************/

void
_AHIsub_FreeAudio( struct AHIAudioCtrlDrv* AudioCtrl,
		   struct DriverBase*      AHIsubBase )
{
  struct G2Base* G2Base = (struct G2Base*) AHIsubBase;

  if( AudioCtrl->ahiac_DriverData != NULL )
  {
    FreeSignal( dd->mastersignal );
    FreeVec( dd->mixbuffer );           // left by a Start that failed
    FreeVec( dd->ring );
    FreeVec( AudioCtrl->ahiac_DriverData );
    AudioCtrl->ahiac_DriverData = NULL;
    G2Base->inuse = FALSE;
  }
}


/******************************************************************************
** AHIsub_Disable *************************************************************
******************************************************************************/

void
_AHIsub_Disable( struct AHIAudioCtrlDrv* AudioCtrl,
		 struct DriverBase*      AHIsubBase )
{
  // V6 drivers do not have to preserve all registers.  Mixing runs in the
  // slave process only, so Forbid() keeps it out.

  Forbid();
}


/******************************************************************************
** AHIsub_Enable **************************************************************
******************************************************************************/

void
_AHIsub_Enable( struct AHIAudioCtrlDrv* AudioCtrl,
		struct DriverBase*      AHIsubBase )
{
  // V6 drivers do not have to preserve all registers

  Permit();
}


/******************************************************************************
** AHIsub_Start ***************************************************************
******************************************************************************/

ULONG
_AHIsub_Start( ULONG                   flags,
	       struct AHIAudioCtrlDrv* AudioCtrl,
	       struct DriverBase*      AHIsubBase )
{
  struct G2Base*  G2Base = (struct G2Base*) AHIsubBase;
  volatile UBYTE* board  = G2Base->board;

  AHIsub_Stop( flags, AudioCtrl );

  if(flags & AHISF_PLAY)
  {
    struct TagItem proctags[] =
    {
      { NP_Entry,     (ULONG) &slaveentry },
      { NP_Name,      (ULONG) LibName     },
      { NP_Priority,  SLAVE_PRI           },
      { TAG_DONE,     0                   }
    };
    ULONG passbytes;
    ULONG maxbytes;
    ULONG size;

    // Ring: the slave keeps it filled to two mixing passes plus one FIFO;
    // a pass started just below that mark must still fit.

    passbytes = AudioCtrl->ahiac_BuffSamples * 4;
    maxbytes  = AudioCtrl->ahiac_MaxBuffSamples * 4;

    if( maxbytes < passbytes )
    {
      maxbytes = passbytes;
    }

    dd->highwater = 2 * passbytes + TOCC_FIFO_SIZE;

    for( size = 8192; size < dd->highwater + maxbytes; size <<= 1 )
    {
    }

    dd->ringsize  = size;
    dd->ringmask  = size - 1;
    dd->ringwr    = 0;
    dd->ringrd    = 0;
    dd->underruns = 0;

    dd->mixbuffer = AllocVec( AudioCtrl->ahiac_BuffSize,
				MEMF_ANY | MEMF_PUBLIC );

    if( dd->mixbuffer == NULL ) return AHIE_NOMEM;

    dd->ring = AllocVec( dd->ringsize, MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR );

    if( dd->ring == NULL ) return AHIE_NOMEM;

    Forbid();

    dd->slavetask = CreateNewProc( proctags );

    if( dd->slavetask != NULL )
    {
      dd->slavetask->pr_Task.tc_UserData = AudioCtrl;
    }

    Permit();

    if( dd->slavetask != NULL )
    {
      Wait( 1L << dd->mastersignal );  // Wait for slave to fill the ring

      if( dd->slavetask == NULL )      // Is slave alive or dead?
      {
        return AHIE_UNKNOWN;
      }
    }
    else
    {
      return AHIE_NOMEM;                 // Well, out of memory or whatever...
    }

    // Playback: activate (FIFO reset), codec format, FIFO towards the
    // card, 3/4 FIFO, then FIFO to the codec with the half-empty
    // interrupt.  The last write also clears the DAC mute bits.

    board[ TOCC_STATUS ] = TOCC_ACT;
    G2CodecFormat( board, dd->fmt );
    G2CodecVolume( board, G2Base->att, TRUE );
    board[ TOCC_STATUS ] = TOCC_PB_PREP;

    dd->interrupt.is_Node.ln_Type = INTERRUPT_NODE_TYPE;
    dd->interrupt.is_Node.ln_Pri  = 0;
    dd->interrupt.is_Node.ln_Name = (char *) LibName;
    dd->interrupt.is_Data         = dd;
    dd->interrupt.is_Code         = (void (*)(void)) G2IntEntry;

    AddIntServer( INTB_EXTER, &dd->interrupt );
    dd->intadded = TRUE;

    G2FifoWrite( dd, TOCC_FIFO_PREFILL );
    dd->playing = TRUE;

    board[ TOCC_STATUS ] = TOCC_PB_MAIN;
  }

  if( flags & AHISF_RECORD )
  {
    return AHIE_UNKNOWN;
  }

  return AHIE_OK;
}


/******************************************************************************
** AHIsub_Update **************************************************************
******************************************************************************/

void
_AHIsub_Update( ULONG                   flags,
		struct AHIAudioCtrlDrv* AudioCtrl,
		struct DriverBase*      AHIsubBase )
{
  // Empty function: the slave reads ahiac_BuffSamples after each pass.
}


/******************************************************************************
** AHIsub_Stop ****************************************************************
******************************************************************************/

void
_AHIsub_Stop( ULONG                   flags,
	      struct AHIAudioCtrlDrv* AudioCtrl,
	      struct DriverBase*      AHIsubBase )
{
  struct G2Base*  G2Base = (struct G2Base*) AHIsubBase;
  volatile UBYTE* board  = G2Base->board;

  if( flags & AHISF_PLAY )
  {
    if( dd->intadded )
    {
      board[ TOCC_STATUS ] = TOCC_PB_TAIL;     // interrupt off
      dd->playing = FALSE;
      RemIntServer( INTB_EXTER, &dd->interrupt );
      dd->intadded = FALSE;
      board[ TOCC_STATUS ] = TOCC_ACT;         // FIFO reset, DACs muted
    }

    if( dd->slavetask != NULL )
    {
      if( dd->slavesignal != -1 )
      {
        Signal( (struct Task*) dd->slavetask,
                1L << dd->slavesignal );         // Kill him!
      }

      Wait( 1L << dd->mastersignal );            // Wait for slave to die
    }

    FreeVec( dd->mixbuffer );
    dd->mixbuffer = NULL;

    FreeVec( dd->ring );
    dd->ring = NULL;
  }

  if(flags & AHISF_RECORD)
  {
    // Do nothing
  }
}


/******************************************************************************
** AHIsub_GetAttr *************************************************************
******************************************************************************/

LONG
_AHIsub_GetAttr( ULONG                   attribute,
		 LONG                    argument,
		 LONG                    def,
		 struct TagItem*         taglist,
		 struct AHIAudioCtrlDrv* AudioCtrl,
		 struct DriverBase*      AHIsubBase )
{
  struct G2Base* G2Base = (struct G2Base*) AHIsubBase;

  switch( attribute )
  {
    case AHIDB_Bits:
      return 16;

    case AHIDB_Frequencies:
      return G2_RATES;

    case AHIDB_Frequency: // Index->Frequency
      if( argument < 0 || argument >= G2_RATES )
      {
        return def;
      }
      return G2Base->freq[ argument ];

    case AHIDB_Index: // Frequency->Index
      return (LONG) FreqIndex( G2Base, argument );

    case AHIDB_Author:
      return (LONG) "Mike Wolak, EPROM Foundry Inc.";

    case AHIDB_Copyright:
      return (LONG) "EPROM Foundry Inc.";

    case AHIDB_Version:
      return (LONG) LibIDString;

    case AHIDB_Annotation:
      return (LONG) "Toccata Zorro II card of the G2 NanoMig core, "
                    "registers driven directly; playback only.";

    case AHIDB_Record:
      return FALSE;

    case AHIDB_FullDuplex:
      return FALSE;

    case AHIDB_Realtime:
      return TRUE;

    case AHIDB_MinOutputVolume:
      return (LONG) ( AttGain( 63 ) >> 14 );

    case AHIDB_MaxOutputVolume:
      return 0x10000;

    case AHIDB_Outputs:
      return 1;

    case AHIDB_Output:
      return (LONG) "Line";    // We have only one "output"!

    default:
      return def;
  }
}


/******************************************************************************
** AHIsub_HardwareControl *****************************************************
******************************************************************************/

ULONG
_AHIsub_HardwareControl( ULONG                   attribute,
			 LONG                    argument,
			 struct AHIAudioCtrlDrv* AudioCtrl,
			 struct DriverBase*      AHIsubBase )
{
  struct G2Base* G2Base = (struct G2Base*) AHIsubBase;

  switch( attribute )
  {
    case AHIC_OutputVolume:
      G2Base->outvol = argument;
      G2Base->att    = VolumeToAtt( argument );

      if( AudioCtrl != NULL && dd != NULL && dd->playing )
      {
        Forbid();
        G2CodecVolume( G2Base->board, G2Base->att, FALSE );
        Permit();
      }
      return TRUE;

    case AHIC_OutputVolume_Query:
      return G2Base->outvol;

    default:
      return FALSE;
  }
}
