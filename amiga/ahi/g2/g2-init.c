/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
 * Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
 */

#include <config.h>

#include <exec/execbase.h>
#include <libraries/configvars.h>
#include <proto/expansion.h>

#include "library.h"
#include "DriverData.h"
#include "toccata.h"

/* The 16 rates of the AD1848 data sheet as rtl/src/toccata_playback.sv
 * produces them (a phase accumulator in core time, exact in the mean, PAL
 * and NTSC alike), ascending: the rate in Hz, rounded, and the {CSS,
 * CFS[2:0]} code that selects it. */
static const struct
{
  ULONG rate;
  UBYTE code;
} rtl_rates[ G2_RATES ] =
{
  {  5513, 0x8 }, {  6615, 0xf }, {  8000, 0x0 }, {  9600, 0x7 },
  { 11025, 0x9 }, { 16000, 0x1 }, { 18900, 0xa }, { 22050, 0xb },
  { 27429, 0x2 }, { 32000, 0x3 }, { 33075, 0xe }, { 37800, 0xc },
  { 44100, 0xd }, { 48000, 0x6 }, { 54857, 0x4 }, { 64000, 0x5 }
};

/******************************************************************************
** Custom driver init *********************************************************
******************************************************************************/

BOOL
DriverInit( struct DriverBase* AHIsubBase )
{
  struct G2Base*    G2Base = (struct G2Base*) AHIsubBase;
  struct ConfigDev* cd;
  int               i;

  G2Base->dosbase = OpenLibrary( DOSNAME, 37 );

  if( DOSBase == NULL )
  {
    Req( "Unable to open 'dos.library' version 37.\n" );
    return FALSE;
  }

  G2Base->expansionbase = OpenLibrary( "expansion.library", 37 );

  if( ExpansionBase == NULL )
  {
    Req( "Unable to open 'expansion.library' version 37.\n" );
    return FALSE;
  }

  // Fail if no hardware is present (this check prevents the audio
  // modes from being added to the database if the driver cannot be
  // used).

  cd = FindConfigDev( NULL, TOCC_MANUFACTURER, TOCC_PRODUCT );

  if( cd == NULL )
  {
    Req( "No Toccata board (manufacturer %ld, product %ld) present.\n",
         TOCC_MANUFACTURER, TOCC_PRODUCT );
    return FALSE;
  }

  G2Base->board  = (volatile UBYTE*) cd->cd_BoardAddr;
  G2Base->outvol = 0x10000;
  G2Base->att    = 0;

  for( i = 0; i < G2_RATES; i++ )
  {
    UBYTE code = rtl_rates[ i ].code;

    G2Base->freq[ i ]    = rtl_rates[ i ].rate;
    G2Base->fmtbits[ i ] = ( ( code & 7 ) << 1 ) | ( code >> 3 );
  }

  // Board init: reset, wait, activate.  Activation alone resets the
  // FIFO, clears the interrupt and mutes both DACs.

  G2Base->board[ TOCC_STATUS ] = TOCC_RST;

  if( SysBase->ThisTask->tc_Node.ln_Type == NT_PROCESS )
  {
    Delay( 25 );                        // 500 ms
  }

  G2Base->board[ TOCC_STATUS ] = TOCC_ACT;

  return TRUE;
}


/******************************************************************************
** Custom driver clean-up *****************************************************
******************************************************************************/

VOID
DriverCleanup( struct DriverBase* AHIsubBase )
{
  struct G2Base* G2Base = (struct G2Base*) AHIsubBase;

  if( G2Base->board != NULL )
  {
    G2Base->board[ TOCC_STATUS ] = TOCC_ACT;
  }

  CloseLibrary( ExpansionBase );
  CloseLibrary( (struct Library*) DOSBase );
}
