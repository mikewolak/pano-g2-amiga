/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
 * Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
 */
#ifndef AHI_Drivers_G2_DriverData_h
#define AHI_Drivers_G2_DriverData_h

#include <exec/libraries.h>
#include <exec/interrupts.h>
#include <devices/ahi.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "DriverBase.h"

#define G2_RATES 16

struct G2Base
{
    struct DriverBase driverbase;
    struct Library*   dosbase;
    struct Library*   expansionbase;
    volatile UBYTE*   board;            /* Toccata board base (cd_BoardAddr) */
    UBYTE             inuse;            /* one AudioCtrl owns the card */
    UBYTE             pad0;
    UWORD             pad1;
    Fixed             outvol;           /* AHIC_OutputVolume, 0x10000 = 0 dB */
    UBYTE             att;              /* DAC attenuation 0..63, 1.5 dB steps */
    UBYTE             pad2[3];
    LONG              freq[ G2_RATES ]; /* rates produced, ascending */
    UBYTE             fmtbits[ G2_RATES ]; /* AD1848 index 8 CSS/CFS bits */
};

#define DRIVERBASE_SIZEOF (sizeof (struct G2Base))

#define DOSBase       (*(struct DosLibrary**) &G2Base->dosbase)
#define ExpansionBase (G2Base->expansionbase)

struct G2Data
{
    struct DriverData   driverdata;
    UBYTE		flags;
    UBYTE		pad1;
    BYTE		mastersignal;
    BYTE		slavesignal;
    struct Process*	mastertask;
    struct Process*	slavetask;
    struct G2Base*	ahisubbase;
    APTR		mixbuffer;

    /* Ring of card-format frames (16-bit stereo, big-endian words) between
     * the slave process (producer) and the level 6 interrupt (consumer).
     * ringwr and ringrd are free-running byte counts; fill = wr - rd. */
    UBYTE*		ring;
    ULONG		ringsize;       /* power of two, multiple of 512 */
    ULONG		ringmask;
    volatile ULONG	ringwr;
    volatile ULONG	ringrd;
    ULONG		highwater;      /* the slave mixes while fill < highwater */

    BYTE		playsignal;     /* interrupt -> slave: room in the ring */
    UBYTE		fmt;            /* AD1848 index 8 value */
    volatile UBYTE	playing;        /* interrupt copies to the FIFO */
    UBYTE		intadded;
    volatile ULONG	underruns;      /* FIFO refills padded with silence */

    struct Interrupt	interrupt;      /* INTB_EXTER server */
};

/* g2-hw.c */
VOID  G2CodecFormat( volatile UBYTE* board, UBYTE fmt );
VOID  G2CodecVolume( volatile UBYTE* board, UBYTE att, BOOL mute );
VOID  G2FifoWrite( struct G2Data* dd, ULONG bytes );
ULONG G2Interrupt( struct G2Data* dd );
extern void G2IntEntry( void );

#endif /* AHI_Drivers_G2_DriverData_h */
