/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/g2.c -- Pano Logic G2 board support (PANO_G2).  See fw/g2.h. */
#include "g2.h"
#include "spiflash.h"

int g2_sys_present, g2_spi_present;

void g2_init(void)
{
    g2_sys_present = (G2_REC_ID == G2_REC_ID_VAL);
    g2_spi_present = (G2_BOARD_ID == G2_BOARD_ID_VAL);
    g2_wdt_kick();
}

static unsigned rdcyc(void) { unsigned c; __asm__ volatile ("csrr %0, mcycle" : "=r"(c)); return c; }

int g2_iprog(uint32_t addr)
{
    unsigned t0;
    if (!g2_sys_present) return -2;
    G2_IPROG_ADDR = addr & 0x00FFFFFFu;
    G2_IPROG = G2_IPROG_KEY;
    t0 = rdcyc();
    while (rdcyc() - t0 < 100u * PANO_CYC_PER_MS) { }   /* 100 ms: the FPGA is reconfiguring long before this ends */
    return -1;
}

int g2_ddr_ready(void) { return (G2_DDR_STATUS & G2_DDR_A_CALIB) != 0u; }
unsigned int g2_guard_refusals(void) { return g2_spi_present ? G2_SPI_STATUS >> 16 : 0u; }

static int overlaps(uint32_t a, uint32_t n, uint32_t lo, uint32_t len)
{
    return n && a < lo + len && lo < a + n;
}
int g2_in_ddr(uint32_t addr, uint32_t n)
{
    if (n && addr + n < addr) return 1;       /* a wrapping range touches everything */
    return overlaps(addr, n, G2_DDR_A_BASE, G2_DDR_A_SIZE) ||
           overlaps(addr, n, G2_DDR_A_BASE | G2_UNCACHED, G2_DDR_A_SIZE);
}
static int inside(uint32_t a, uint32_t n, uint32_t lo, uint32_t len)
{
    return n && a >= lo && n <= len && a - lo <= len - n;
}
int g2_in_ddr_whole(uint32_t addr, uint32_t n)
{
    return inside(addr, n, G2_DDR_A_BASE, G2_DDR_A_SIZE) || inside(addr, n, G2_DDR_A_BASE | G2_UNCACHED, G2_DDR_A_SIZE);
}
int g2_in_bram(uint32_t addr, uint32_t n) { return inside(addr, n, G2_BRAM_BASE, G2_BRAM_SIZE); }

int g2_cfg_reg(unsigned reg, uint32_t *val)
{
    uint8_t b[256];
    unsigned k;
    spif_read(SPIF_G2_CONFIG, b, sizeof b);
    for (k = 0; k + 8u <= sizeof b; k += 8u) {
        uint32_t tag = ((uint32_t)b[k] << 24) | ((uint32_t)b[k + 1] << 16) | ((uint32_t)b[k + 2] << 8) | b[k + 3];
        uint32_t v = ((uint32_t)b[k + 4] << 24) | ((uint32_t)b[k + 5] << 16) | ((uint32_t)b[k + 6] << 8) | b[k + 7];
        if (tag == 0xFFFFFFFFu) break;
        if (tag == (0x10000000u | (reg & 0xFFu))) { *val = v; return 1; }
    }
    return 0;
}

int g2_factory_mac(uint8_t mac[6])
{
    uint32_t hi, lo;
    if (!g2_cfg_reg(0x10u, &hi) || !g2_cfg_reg(0x11u, &lo)) return 0;
    if ((hi & 0xFFFFu) == 0xFFFFu || (hi & 0x0100u)) return 0;   /* erased, or a multicast first octet */
    mac[0] = (uint8_t)(hi >> 8); mac[1] = (uint8_t)hi;
    mac[2] = (uint8_t)(lo >> 24); mac[3] = (uint8_t)(lo >> 16); mac[4] = (uint8_t)(lo >> 8); mac[5] = (uint8_t)lo;
    return 1;
}
