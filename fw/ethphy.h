/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ethphy.h -- bit-banged MDIO to the Micrel KSZ8721BL, through board page op 6.
 *
 * WHY BIT-BANG.  The same argument the codec plan makes for its I2C, and the same one that put the SPI flash
 * on board page op 5: a management bus is a handful of pins moved slowly, and putting it in fabric costs
 * slices we do not have (92% occupied, no block RAM) and puts the register program behind a 40-minute ISE
 * build.  In C it is iterated in seconds.  MDIO runs once per link event; it is nowhere near a hot path.
 *
 */
#ifndef ETHPHY_H
#define ETHPHY_H

/* Clause 22 register numbers we care about. */
#define PHY_BMCR        0x00u       /* control: reset, loopback, speed, autoneg */
#define PHY_BMSR        0x01u       /* status: link up, autoneg complete */
#define PHY_ID1         0x02u       /* OUI high -- Micrel's is 0x0022 */
#define PHY_ID2         0x03u       /* OUI low, model, revision */
#define PHY_ANAR        0x04u
#define PHY_ANLPAR      0x05u

#define BMCR_RESET      0x8000u
#define BMCR_LOOPBACK   0x4000u
#define BMCR_ANEG_EN    0x1000u
#define BMCR_ANEG_RST   0x0200u
#define BMSR_LINK       0x0004u
#define BMSR_ANEG_DONE  0x0020u

#ifdef PANO_G2
/* The G2's Marvell 88E1119R: MDIO address 0, OUI 0x005043 (ID1 reads 0x0141).  Register 9 and the Marvell-specific
 * registers 17 and 22 follow the layout of the Marvell Alaska copper PHYs (88E1111 public data sheet: 17 = copper
 * specific status, 22 = page address); on the 88E1119R that layout is untested. */
#define G2_PHY_ADDR     0u
#define PHY_ID1_MARVELL 0x0141u
#define PHY_GBCR        0x09u       /* 1000BASE-T control */
#define PHY_GBSR        0x0Au       /* 1000BASE-T status */
#define PHY_M_SSR       0x11u       /* copper specific status */
#define PHY_M_PAGE      0x16u       /* page address */
#define GBCR_ADV_1000F  0x0200u
#define GBCR_ADV_1000H  0x0100u
#define ANAR_10_100_ALL 0x01E1u     /* 100 full, 100 half, 10 full, 10 half, selector IEEE 802.3 */
#define SSR_LINK_RT     0x0400u     /* [10] link, real time */
#define SSR_RESOLVED    0x0800u     /* [11] speed and duplex resolved */
#define SSR_DUPLEX      0x2000u     /* [13] full duplex */
void         phy_g2_setup(unsigned int phyad);                  /* 1000 full and 10/100, autonegotiation restarted */
void         phy_g2_advertise(unsigned int phyad, int gig);     /* gig 0: 10/100 only; 1: 1000 full as well; restarts */
int          phy_g2_gig_advertised(unsigned int phyad);
unsigned int phy_g2_speed(unsigned int phyad, int *full);
#endif

/* Bring the PHY's world up: start the 25 MHz reference, then release reset and wait out the RC network on
 * eth_rst_ (UCF:272).  Safe to call more than once. */
void     phy_power_up(void);
/* Hold the PHY in reset with the reference clock stopped -- the board's power-on state, and the negative
 * control for the read gate: every register must read 0xFFFF while this is in force. */
void     phy_power_down(void);

unsigned int phy_read(unsigned int phyad, unsigned int reg);           /* 16 bits, 0xFFFF if nobody answers */
void         phy_write(unsigned int phyad, unsigned int reg, unsigned int val);

/* Walk all 32 addresses and return the first that answers with an ID that is neither 0x0000 nor 0xFFFF,
 * or -1.  The strap address on this board is not documented in our notes, so it is discovered, not assumed. */
int      phy_find(void);

/* The PHY's link LED as the fabric sees it (board page +0x501C bit 2) -- an observation of the pin, wholly
 * independent of anything MDIO says about itself. */
int      phy_link_pin(void);

#endif
