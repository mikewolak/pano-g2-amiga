/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ethmac.h -- the driver for rtl/soc_ethlite.v, the MII MAC whose packets live in the fast RAM's upper 4 KB.
 *
 *   registers  0x8000_B000                   (word offsets below)
 *   RX ring    0x8000_A000 .. 0x8000_A9FF    2,560 bytes: records {header word, payload padded to a word}, wrapping
 *   TX buffer  0x8000_AA00 .. 0x8000_AFFF    1,536 bytes: one frame
 *
 * Both windows are UNCACHED and single-cycle: nothing here needs a cache flush, and reading a frame in place costs what
 * reading the same bytes from the fast RAM costs.  The hardware writes the RX ring and nothing else; the CPU writes the
 * TX buffer and RX_RD.  The MAC is not a bus master and cannot touch LPDDR, so nothing has to stop it before exec.
 */
#ifndef ETHMAC_H
#define ETHMAC_H
#include <stdint.h>

#define ETHMAC_REG(n)       (*(volatile uint32_t *)(0x8000B000u + 4u * (n)))
#define ETHMAC_CTRL         ETHMAC_REG(0)   /* [0] RX_EN [1] TX_EN [2] PROMISC [3] ALLMULTI */
#define ETHMAC_STATUS       ETHMAC_REG(1)   /* [0] TX_BUSY [1] RX pending [2] link [3] last TX underran */
#define ETHMAC_RX_WR        ETHMAC_REG(2)   /* byte offset in the ring, read-only */
#define ETHMAC_RX_RD        ETHMAC_REG(3)   /* byte offset in the ring, ours */
#define ETHMAC_TX_LEN       ETHMAC_REG(4)   /* write [10:0] length | bit 31 GO */
#define ETHMAC_MAC_LO       ETHMAC_REG(5)
#define ETHMAC_MAC_HI       ETHMAC_REG(6)
#define ETHMAC_STAT_SEL     ETHMAC_REG(7)
#define ETHMAC_STAT         ETHMAC_REG(8)
#define ETHMAC_INFO         ETHMAC_REG(9)   /* [15:0] RX ring bytes, [30:16] TX buffer bytes, [31] records on even words */
#define ETHMAC_SPEED        ETHMAC_REG(10)  /* G2 (rtl/g2/g2_gmac.v): W [1:0] 0 automatic, 1 MII, 2 GMII; R [4] GMII in use,
                                             * [5] measured, [31:16] RX_CLK / 8 edges in 16,384 clk_cpu clocks.  Reads 0 on
                                             * the G1 MAC and on G2 fabrics before the GMII MAC */
#define ETHMAC_SPEED_GMII   0x10u
#define ETHMAC_RING_BASE    ETHMAC_REG(11)  /* G2 large-ring MAC: R the CPU address of the RX ring in use (0x8000_A000 or
                                             * 0x8200_0000); reads 0 on earlier MACs */
#define ETHMAC_CTRL_RX_BIG  0x100u  /* G2: the 16 KB RX ring at 0x8200_0000; taken while RX_EN is 0; ignored elsewhere */

#define ETHMAC_CTRL_RX_EN    0x1u
#define ETHMAC_CTRL_TX_EN    0x2u
#define ETHMAC_CTRL_PROMISC  0x4u
#define ETHMAC_CTRL_ALLMULTI 0x8u
#define ETHMAC_CTRL_RX_IE    0x10u   /* interrupt while frames are waiting (clears itself when RX_RD catches up) */
#define ETHMAC_CTRL_TX_IE    0x20u   /* interrupt when a frame has gone (STATUS[4], write 1 to clear) */
#define ETHMAC_S_TX_BUSY    0x1u
#define ETHMAC_S_RX_PEND    0x2u
#define ETHMAC_S_LINK       0x4u    /* the RAW eth_led0 pin: ACTIVE-LOW and it blinks with activity -- use MDIO BMSR for link */
#define ETHMAC_S_TX_UNDER    0x8u
#define ETHMAC_S_TX_DONE     0x10u   /* sticky; write it back as 1 to clear */
#define ETHMAC_IRQ_LINE      2u      /* externalInterruptArray[2]: mask CSR 0xBC0, pending CSR 0xFC0, mcause 11 */

#define ETHMAC_RING         ((volatile uint32_t *)0x8000A000u)   /* the fast RAM ring; fw/ethmac.c uses the ring in use
                                                                  * (RING_BASE, INFO) */
#define ETHMAC_RING_BYTES   2560u   /* the fast RAM ring's size; the G2 large ring is 16,384 (INFO[15:0]) */
#define ETHMAC_TXBUF        ((volatile uint32_t *)0x8000AA00u)
#define ETHMAC_TX_MAX       1536u

enum { ETHMAC_ST_RX_OK, ETHMAC_ST_RX_BAD, ETHMAC_ST_RX_SPACE, ETHMAC_ST_RX_FILT,
       ETHMAC_ST_RX_LEN, ETHMAC_ST_RX_FIFO, ETHMAC_ST_TX_OK, ETHMAC_ST_TX_UNDER, ETHMAC_NSTATS };

/* mac[0] is the first byte on the wire.  flags: ETHMAC_CTRL_PROMISC / _ALLMULTI.  Starts empty, RX and TX enabled. */
void     ethmac_init(const uint8_t mac[6], uint32_t flags);
void     ethmac_stop(void);
/* 0 queued; -1 the previous frame is still going (TX_BUSY -- try again); -2 too long.  Short frames are padded by the
 * hardware to the 60-byte minimum. */
int      ethmac_send(const void *frame, unsigned len);
/* the next received frame copied to out (at most max bytes); returns its length, or 0 when there is none.  A frame
 * longer than max is truncated in the copy and still consumed. */
int      ethmac_recv(void *out, unsigned max);
/* zero-copy-ish access to the frame at the read index: its length (0 = none), a copy of any part of it, and consuming
 * it.  ethmac_recv is these three in one. */
int      ethmac_peek_len(void);
void     ethmac_copy(void *dst, unsigned off, unsigned n);
void     ethmac_consume(void);
/* the frame at the read index in place (the uncached fast-RAM window, single-cycle reads), or 0 if there is none or
 * it wraps the end of the ring.  Valid until ethmac_consume(). */
const volatile uint8_t *ethmac_peek_ptr(void);
/* the waiting frame IN PLACE as two ring segments -- *a: its first *na bytes, up to the ring's end; *b: the ring's
 * start, holding the rest -- so a wrapped frame needs no copy.  Returns its length, 0 if none.  Valid until consume. */
int      ethmac_peek_seg(const volatile uint8_t **a, unsigned *na, const volatile uint8_t **b);
int      ethmac_tx_busy(void);
uint32_t ethmac_stat(unsigned which);
#endif
