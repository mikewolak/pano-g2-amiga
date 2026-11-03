/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* g2serial.h -- g2serial.device: the device base, the unit state and the hardware definitions it uses.
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  New implementation from the functional specification
 * a functional analysis of BaudBandit.device 1.4b.
 *
 * One unit (0).  Two data paths, chosen at the first open:
 *   FIFO   the G2 serial FIFO board (rtl/src/g2ser_fifo.v, Zorro II, manufacturer 0x0A1C product 1): 512-byte RX and
 *          TX FIFOs moved in words, one INT2 server; the board's BAUD register sets the USB adapter's rate.
 *   PAULA  Paula's SERDAT/SERDATR with the RBF (level 5) and TBE (level 1) interrupt vectors; the rate comes from
 *          SERPER, bit time (SERPER + 1) colour clocks.
 * The RS-232 control lines are CIA-B port A in both paths (0: asserted): DTR PA7, RTS PA6 outputs; CD PA5, CTS PA4,
 * DSR PA3, RI/SEL PA2 inputs. */
#ifndef G2SERIAL_H
#define G2SERIAL_H

#include <exec/types.h>
#include <exec/devices.h>
#include <exec/interrupts.h>
#include <exec/ports.h>
#include <exec/lists.h>
#include <devices/serial.h>
#include <devices/timer.h>
#include <libraries/configvars.h>

#define G2SER_VERSION   1
#define G2SER_REVISION  0

/* io_Flags bits of a serial request (devices/serial.h keeps these under DEVICES_SERIAL_H_OBSOLETE) */
#define G2_IOSERF_ACTIVE  (1 << 4)
#define G2_IOSERF_ABORT   (1 << 5)
#define G2_IOSERF_QUEUED  (1 << 6)

/* OpenDevice() flags argument: bit 0 selects the Paula path although the FIFO board is present */
#define G2SER_OPENF_PAULA 1

/* G2 serial FIFO board: Zorro II identity and register map (word offsets from the board base) */
#define G2FIFO_MANUF      0x0A1C
#define G2FIFO_PROD       1
#define BD_RXCOUNT        0     /* 0x00 R  bytes waiting */
#define BD_TXSPACE        1     /* 0x02 R  free places in the TX FIFO */
#define BD_CTRL           2     /* 0x04 RW [0] FIFO mode, [1] RX irq enable, [2] TX irq enable; R [8] RX, [9] TX pending */
#define BD_RXTHRESH       3     /* 0x06 RW RX interrupt threshold */
#define BD_TXTHRESH       4     /* 0x08 RW TX interrupt threshold (free places) */
#define BD_BAUDH          6     /* 0x0C RW rate [31:16] */
#define BD_BAUDL          7     /* 0x0E RW rate [15:0] */
#define BD_RXDATA2        8     /* 0x10 R  two bytes, first in [15:8]; pops both */
#define BD_RXDATA2B       9     /* 0x12 R  as RXDATA2: a longword read at 0x10 pops four bytes */
#define BD_TXDATA2        11    /* 0x16 W  as TXDATA: a longword write at 0x14 pushes four bytes */
#define BD_RXDATA1        12    /* 0x18 R  one byte in [7:0]; pops it */
#define BD_TXDATA         10    /* 0x14 W  word: pushes [15:8] then [7:0]; byte write: pushes that byte */
#define CTRL_FIFO         0x0001
#define CTRL_RXIE         0x0002
#define CTRL_TXIE         0x0004
#define CTRL_RXPEND       0x0100
#define CTRL_TXPEND       0x0200
#define FIFO_N            512   /* bytes in each board FIFO */
#define FIFO_RXTHRESH     256   /* RX interrupt at half full, or 73 us after the last byte (the board's idle timer) */
#define FIFO_TXCAP_7WIRE  64    /* TX FIFO fill limit with RTS/CTS, so a CTS negation stops the wire within 64 bytes */
#define FIFO_BAUD_MIN     183   /* FT232R: 3 MHz / 16384 */
#define FIFO_BAUD_MAX     3000000

/* CIA-B port A */
#define CIAB_PRA          ((volatile UBYTE *)0xBFD000)
#define CIAB_DDRA         ((volatile UBYTE *)0xBFD200)
#define CIAF_DTR          0x80
#define CIAF_RTS          0x40
#define CIAF_CD           0x20
#define CIAF_CTS          0x10
#define CIAF_DSR          0x08
#define CIAF_SEL          0x04

/* reasons the transmitter is held */
#define HOLD_STOP         0x01  /* CMD_STOP */
#define HOLD_XOFF         0x02  /* xOFF received */
#define HOLD_CTS          0x04  /* CTS negated with SERF_7WIRE */
#define HOLD_BRK          0x08  /* a break is being sent */

#define PATH_PAULA        0
#define PATH_FIFO         1

#define RBUF_DEFAULT      65536
#define RBUF_MIN          512
#define RBUF_MAX          (16UL << 20)
#define CTS_POLL_US       2000

struct G2Ser {
    struct Device         dev;
    struct Unit           unit;
    BPTR                  seglist;

    /* path and hardware */
    UBYTE                 path;            /* PATH_PAULA or PATH_FIFO */
    UBYTE                 opening;         /* first open or last close in progress */
    UBYTE                 shared;          /* the first opener set SERF_SHARED */
    UBYTE                 have_misc;       /* bit 0 MR_SERIALPORT, bit 1 MR_SERIALBITS held */
    volatile UWORD       *bd;              /* FIFO board registers */
    struct ConfigDev     *cd;
    struct Library       *misc;
    UWORD                 ctrl;            /* shadow of the board's CTRL */
    UWORD                 tx_cap;          /* TX FIFO fill limit */
    ULONG                 colour_hz;       /* colour clock: E clock x 5 */
    UBYTE                 old_ddra, old_pra;

    /* receive ring (power of two); rhead written only by the interrupt, rtail only by the task */
    UBYTE                *ring;
    ULONG                 rsize, rmask;
    volatile ULONG        rhead, rtail;
    ULONG                 rx_hi, rx_lo;    /* flow control: stop at 3/4 full, resume at 1/4 */
    volatile UBYTE        throttled;       /* RTS negated and/or xOFF sent */
    volatile UBYTE        rx_hold;         /* ring full: the receive interrupt is off, the hardware holds the data */
    volatile BYTE         rx_err;          /* error to report with the next read completion */

    /* active and queued reads */
    struct MinList        rq;
    struct IOExtSer      *rd;
    UBYTE                *rd_ptr;
    ULONG                 rd_left;
    volatile UBYTE        rd_direct;       /* the interrupt delivers straight into rd (the ring is empty) */

    /* transmit */
    volatile UBYTE        tx_on;           /* TBE / board TX interrupt enabled */
    volatile UBYTE        hold;            /* HOLD_* */
    volatile UWORD        ctl_tx;          /* xON/xOFF to send ahead of the data: 0x100 | char, or 0 */
    struct MinList        wq;
    struct IOExtSer      *wr;
    UBYTE                *wr_ptr;
    ULONG                 wr_left;

    /* parameters */
    ULONG                 baud, ctlchar, rbuflen, extflags, brktime;
    struct IOTArray       term;
    UBYTE                 readlen, writelen, stopbits, serflags;
    UBYTE                 xon, xoff;
    UBYTE                 fast_rx, fast_tx; /* 8 bits, no parity, no xON/xOFF, no EOF scan */
    UBYTE                 par;             /* 0 none, 1 even, 2 odd, 3 mark, 4 space */
    UBYTE                 xon_on;          /* xON/xOFF active */
    UBYTE                 eof;             /* SERF_EOFMODE */
    UBYTE                 brk_detect;      /* Paula: a zero byte with a low stop bit is a break */
    UWORD                 rx_dmask, rx_stop;
    UWORD                 tx_dmask, tx_stop;
    UBYTE                 term_map[32];
    volatile UWORD        stat;            /* io_Status bits 8..12 */

    /* interrupts */
    struct Interrupt      rbf_is, tbe_is, ports_is, soft_is;
    struct Interrupt     *old_rbf, *old_tbe;
    UBYTE                 server_on, vectors_on;

    /* timer.device UNIT_MICROHZ: CTS poll and break duration, replied to a PA_SOFTINT port */
    struct MsgPort        tport;
    struct timerequest    cts_tr, brk_tr;
    UBYTE                 timer_open;
    volatile UBYTE        cts_busy, brk_busy, brk_abort;
    struct IOExtSer      *brk_req;
};

#endif
