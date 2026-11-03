/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ethphy.c -- IEEE 802.3 clause 22 MDIO, bit-banged through board page op 6.  See fw/ethphy.h.
 *
 * THE WIRE.  MDIO is a synchronous serial bus with one clock the MAC always drives (MDC) and one data line
 * that changes hands (MDIO).  A frame is:
 *
 *     <32 x 1>  PRE     preamble, so a PHY that has lost sync can find the start
 *     01        ST      start of frame
 *     10 / 01   OP      read / write
 *     AAAAA     PHYAD   which PHY on the bus
 *     RRRRR     REGAD   which register
 *     Z0 / 10   TA      turnaround: on a READ the MAC lets go and the PHY drives the second bit low
 *     DDDD...   DATA    16 bits, MSB first
 *
 * WHO DRIVES WHEN.  The MAC presents a bit while MDC is LOW and the PHY samples it on the RISING edge; on a
 * read the PHY presents its bit just after the rising edge and the MAC samples it before the next one.  So
 * this code does: set the line, MDC high, (sample), MDC low.  Getting the turnaround wrong is the classic
 * MDIO bug and it does not look like a bug -- it looks like a PHY that answers 0xFFFF, which is also what an
 * absent PHY looks like.  That ambiguity is exactly why fw/phytest.c proves the read can FAIL before it
 * believes a read that succeeds.
 *
 * SPEED.  Clause 22 caps MDC at 2.5 MHz.  Each half-period here is a delay loop plus a bus write, which on a
 * 50 MHz core lands near 500 kHz (PANO_CYC_PER_US per microsecond, fw/board.h) -- five times slower than the limit
 * and entirely fast enough for a bus that
 * carries a few dozen registers per link event.
 */
#include "ethphy.h"
#include "board.h"                         /* PANO_CYC_PER_US */

#define BRD_W   (*(volatile unsigned int *)0x80005000u)   /* W: [31:28] op */
#define BRD_RD  (*(volatile unsigned int *)0x8000501Cu)   /* R: [0] SPI MISO, [1] MDIO, [2] PHY link LED */

/* board page op 6: {x1_en[4], rst_n[3], mdc[2], mdio_oe[1], mdio_o[0]} -- rtl/pano_g1_top.v */
#ifdef PANO_G2
#define ETH(x1, rstn, mdc, oe, o)                                                     \
    ((void)(x1), (void)(rstn),                                                        \
     BRD_W = 0x60000000u | 0x10u | 0x08u |                                            \
             ((mdc) ? 0x04u : 0u) | ((oe) ? 0x02u : 0u) | ((o) ? 0x01u : 0u))
#else
#define ETH(x1, rstn, mdc, oe, o)                                                     \
    (BRD_W = 0x60000000u | ((x1) ? 0x10u : 0u) | ((rstn) ? 0x08u : 0u) |               \
             ((mdc) ? 0x04u : 0u) | ((oe) ? 0x02u : 0u) | ((o) ? 0x01u : 0u))
#endif

#define MDIO_IN()   ((BRD_RD >> 1) & 1u)

/* The two pieces of state the bus does not carry: whether the reference clock is running and whether the PHY
 * is out of reset.  Every ETH() write sets all five bits at once, so they have to be remembered. */
static unsigned int s_x1 = 0u, s_rstn = 0u;

static unsigned int rdcycle_(void)
{
    unsigned int c;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}
static void wait_us(unsigned int us)
{
    unsigned int c0 = rdcycle_();
    while (rdcycle_() - c0 < us * PANO_CYC_PER_US) ;
}

/* One MDC period with the MAC driving `bit'.  Returns what the PHY had on the line while the clock was high,
 * which is meaningless unless the MAC has let go -- the read path ignores it when it is driving. */
static unsigned int mdio_clk(unsigned int oe, unsigned int bit)
{
    unsigned int in;
    ETH(s_x1, s_rstn, 0, oe, bit);           /* set up while MDC is low */
    wait_us(1);
    ETH(s_x1, s_rstn, 1, oe, bit);           /* rising edge: the PHY samples here */
    wait_us(1);
    in = MDIO_IN();                          /* and presents its own bit for us to take */
    ETH(s_x1, s_rstn, 0, oe, bit);
    return in;
}

static void mdio_out_bits(unsigned int val, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) mdio_clk(1u, (val >> i) & 1u);
}

static void mdio_preamble(void)
{
    int i;
    for (i = 0; i < 32; i++) mdio_clk(1u, 1u);
}

/* THE MAC MUST BE OFF THE LINE BEFORE THE PHY DRIVES IT.  On a read the frame ends ...REGAD, then TA: the MAC
 * releases during the first TA bit and the PHY pulls the second low.  Releasing one bit late means both ends
 * drive the same wire for a clock -- which usually still "works", and is the kind of thing that works on the
 * bench and fails on a different board. */
unsigned int phy_read(unsigned int phyad, unsigned int reg)
{
    unsigned int v = 0u;
    int i;

    mdio_preamble();
    mdio_out_bits(0x1u, 2);                  /* ST  = 01 */
    mdio_out_bits(0x2u, 2);                  /* OP  = 10, read */
    mdio_out_bits(phyad & 0x1Fu, 5);
    mdio_out_bits(reg & 0x1Fu, 5);

    /* TA: the MAC lets go and the KSZ8721 takes the line.  Clause 22 says two
     * bits (Z0), but on silicon the data comes back shifted LEFT one bit --
     * ID1 read 0x0045 where the datasheet says 0x0022, and 0x0045 == 0x0022<<1|1
     * -- so the PHY's turnaround is one bit shorter than the two the code was
     * clocking.  One TA clock aligns the data. */
    mdio_clk(0u, 1u);

    for (i = 0; i < 16; i++) v = (v << 1) | mdio_clk(0u, 1u);

    ETH(s_x1, s_rstn, 0, 0, 0);              /* leave the line released; the PHY's pull-up idles it high */
    return v & 0xFFFFu;
}

void phy_write(unsigned int phyad, unsigned int reg, unsigned int val)
{
    mdio_preamble();
    mdio_out_bits(0x1u, 2);                  /* ST = 01 */
    mdio_out_bits(0x1u, 2);                  /* OP = 01, write */
    mdio_out_bits(phyad & 0x1Fu, 5);
    mdio_out_bits(reg & 0x1Fu, 5);
    mdio_out_bits(0x2u, 2);                  /* TA = 10, driven by us on a write (the write TA is full two bits --
                                                only the READ turnaround is one bit short, because that is where the
                                                bus changes direction) */
    mdio_out_bits(val & 0xFFFFu, 16);
    ETH(s_x1, s_rstn, 0, 0, 0);
}

#ifdef PANO_G2
/* THE G2's PHY RESET CLOCKS THE FPGA.  GMII_RST_N (R11) also selects the 88E1119R's CLK125 output, and CLK125 on Y13 is
 * the SoC's only clock: 25 MHz in reset, 125 MHz out of it.  Pulling it low would stop the PLL
 * the CPU runs from, so the fabric holds it high and the firmware never touches it: power-up only waits, power-down
 * does nothing, and every ETH() write above carries rst_n = 1. */
void phy_power_up(void)
{
    s_x1 = 1u; s_rstn = 1u;
    ETH(s_x1, s_rstn, 0, 0, 0);
    wait_us(10000u);
}

void phy_power_down(void) { }

/* MDIO address 0 on the G2; the ID must read as Marvell's OUI */
int phy_find(void)
{
    unsigned int id = phy_read(G2_PHY_ADDR, PHY_ID1);
    return (id != 0x0000u && id != 0xFFFFu) ? (int)G2_PHY_ADDR : -1;
}

/* 1000BASE-T FULL DUPLEX AND 10/100, then autonegotiation again.  Page 0 is selected first (register 22), because every
 * register below is a page-0 copper register.  Register 9 (1000BASE-T control) advertises 1000 full [9] and not 1000
 * half [8]; register 4 advertises 10/100 half and full with the IEEE 802.3 selector; then BMCR restarts
 * autonegotiation with it enabled.  A change to registers 4 and 9 takes effect at the next autonegotiation, which the
 * restart starts.  The MAC (rtl/soc_ethlite.v) follows the resolved speed by itself: it measures RX_CLK (125 MHz at
 * 1000 Mb/s, 25 or 2.5 MHz otherwise) and switches between GMII and MII between frames, so nothing here tells it.
 * Register 20 (extended PHY specific control) is not written: its RX and TX clock delay bits [7] and [1] act in RGMII
 * mode only (88E1111 data sheet, register 20), and this board wires the PHY for GMII; the fabric centres GTX_CLK on the
 * data itself (an ODDR2 on the inverted clock) and samples RXD on the RX_CLK edge the timing analysis chose. */
void phy_g2_advertise(unsigned int phyad, int gig)
{
    unsigned int g = phy_read(phyad, PHY_GBCR) & ~(GBCR_ADV_1000F | GBCR_ADV_1000H);
    phy_write(phyad, PHY_M_PAGE, 0u);
    phy_write(phyad, PHY_GBCR, gig ? (g | GBCR_ADV_1000F) : g);
    phy_write(phyad, PHY_ANAR, ANAR_10_100_ALL);
    phy_write(phyad, PHY_BMCR, BMCR_ANEG_EN | BMCR_ANEG_RST);
}

/* at boot: a link already up with exactly this advertisement (stage 1 negotiated it before entering the DDR shell) is
 * kept, so the DDR shell does not drop it for another autonegotiation; anything else is set and renegotiated.  BMSR's
 * link bit latches low (clause 22.2.4.2), so it is read twice. */
void phy_g2_setup(unsigned int phyad)
{
    phy_write(phyad, PHY_M_PAGE, 0u);
    if ((phy_read(phyad, PHY_GBCR) & (GBCR_ADV_1000F | GBCR_ADV_1000H)) == GBCR_ADV_1000F &&
        (phy_read(phyad, PHY_ANAR) & 0x03FFu) == ANAR_10_100_ALL) {
        (void)phy_read(phyad, PHY_BMSR);
        if (phy_read(phyad, PHY_BMSR) & BMSR_LINK) return;
    }
    phy_g2_advertise(phyad, 1);
}

int phy_g2_gig_advertised(unsigned int phyad) { return (phy_read(phyad, PHY_GBCR) & GBCR_ADV_1000F) ? 1 : 0; }

/* resolved speed in Mb/s (10, 100, 1000) and duplex from the copper-specific status register 17; 0 while unresolved */
unsigned int phy_g2_speed(unsigned int phyad, int *full)
{
    unsigned int s = phy_read(phyad, PHY_M_SSR);
    if (s == 0xFFFFu || !(s & SSR_RESOLVED) || !(s & SSR_LINK_RT)) { *full = 0; return 0u; }
    *full = (s & SSR_DUPLEX) ? 1 : 0;
    switch ((s >> 14) & 3u) { case 0u: return 10u; case 1u: return 100u; case 2u: return 1000u; default: return 0u; }
}
#else
void phy_power_up(void)
{
    /* THE REFERENCE CLOCK FIRST, AND THEN A WAIT.  The KSZ8721's management interface is clocked from its own
     * X1 domain, so MDIO before the reference is running talks to nothing.  Then release reset -- eth_rst_
     * goes through an RC network (ise/pano_g1.ucf), so the pin takes time to actually rise, and the PHY wants
     * a settling period after it does.  10 ms is far more than either needs and costs nothing once. */
    s_x1 = 1u;
    ETH(s_x1, 0u, 0, 0, 0);
    wait_us(1000u);
    s_rstn = 1u;
    ETH(s_x1, s_rstn, 0, 0, 0);
    wait_us(10000u);
}

void phy_power_down(void)
{
    s_rstn = 0u; s_x1 = 0u;
    ETH(0u, 0u, 0, 0, 0);
    wait_us(1000u);
}

int phy_find(void)
{
    unsigned int a, id;
    for (a = 0u; a < 32u; a++) {
        id = phy_read(a, PHY_ID1);
        if (id != 0x0000u && id != 0xFFFFu) return (int)a;
    }
    return -1;
}
#endif

int phy_link_pin(void) { return (int)((BRD_RD >> 2) & 1u); }
