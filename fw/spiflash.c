/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/spiflash.c -- the M25P80 from the CPU: board page op 5 claims the SPI pins from rtl/spi_link_client.v and the CPU
 * bit-bangs SPI mode 0.  The READ half is fw/stage1.c's, byte for byte; this adds RDID, RDSR, WREN, SE and PP.
 *
 * stage1.c keeps its own copy for now: stage1 lives in the bitstream's boot RAM, so changing it means a new bitstream.
 * At the next ISE build stage1 should link this file instead, so there is one implementation (the plan's section 5).
 */
#include "spiflash.h"
#ifdef PANO_G2
#include "g2.h"
#endif

#define BRD_W    (*(volatile unsigned int *)0x80005000u)   /* W: [31:28] op */
#define BRD_MISO (*(volatile unsigned int *)0x8000501Cu)   /* R: [0] the SPI flash's MISO pin */
/* board-page op 5: {claim[8], cs_[2], sck[1], mosi[0]} -- see rtl/pano_g1_top.v's ownership note */
#define SPI(claim, cs_, sck, mosi) \
    (BRD_W = 0x50000000u | ((claim) ? 0x100u : 0u) | ((cs_) ? 4u : 0u) | ((sck) ? 2u : 0u) | ((mosi) ? 1u : 0u))

static void spi_begin(void) { SPI(1, 1, 0, 0); SPI(1, 0, 0, 0); }   /* claim, then CS low with SCK idle low */
static void spi_end(void)   { SPI(1, 1, 0, 0); SPI(0, 1, 0, 0); }   /* CS high, then hand the pins back */

/* one byte, MSB first, SPI mode 0 -- fw/stage1.c's routine exactly, which is the one proven on this board: MOSI is set
 * while SCK is low, the device presents MISO on the falling edge and holds it while SCK is high, so the sample is taken
 * after SCK goes high. */
static unsigned int spi_byte(unsigned int out)
{
    unsigned int i, in = 0u;
    for (i = 0; i < 8u; i++) {
        unsigned int m = (out >> (7u - i)) & 1u;
        SPI(1, 0, 0, m);
        SPI(1, 0, 1, m);
        in = (in << 1) | (BRD_MISO & 1u);
    }
    return in;
}

#ifdef PANO_G2
/* THE G2's BYTE ENGINE (0x8000_C000, fw/g2_regs.h): one register write shifts a byte at 12.5 MHz SCK, against the
 * sixteen board-page writes of the bit-bang routine above.  Both reach the pins only through the fabric's write guard.
 * The bit-bang routine stays as the path for a bitstream without the engine (the board ID register says which). */
static void g2_begin(void) { if (g2_spi_present) G2_SPI_CTRL = (G2_SPI_DIV << 8) | 1u; else spi_begin(); }
static void g2_end(void)   { if (g2_spi_present) G2_SPI_CTRL = G2_SPI_DIV << 8; else spi_end(); }
static unsigned int g2_byte(unsigned int out)
{
    unsigned int r, n = 0;
    if (!g2_spi_present) return spi_byte(out);
    G2_SPI_DATA = out & 0xFFu;
    while ((r = G2_SPI_DATA) & G2_SPI_BUSY) if (++n > 10000u) return 0xFFu;   /* 8 SCK periods are 32 clocks */
    return r & 0xFFu;
}
#define spi_begin g2_begin
#define spi_end   g2_end
#define spi_byte  g2_byte
#endif

static void cmd_addr(unsigned int op, uint32_t a)
{
    spi_byte(op); spi_byte((a >> 16) & 0xFFu); spi_byte((a >> 8) & 0xFFu); spi_byte(a & 0xFFu);
}

uint32_t spif_rdid(void)
{
    uint32_t id;
    spi_begin(); spi_byte(0x9Fu);
    id = spi_byte(0) << 16; id |= spi_byte(0) << 8; id |= spi_byte(0);
    spi_end();
    return id;
}

uint8_t spif_rdsr(void)
{
    uint8_t s;
    spi_begin(); spi_byte(0x05u); s = (uint8_t)spi_byte(0); spi_end();
    return s;
}

void spif_read(uint32_t addr, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;
    spi_begin(); cmd_addr(0x03u, addr);
    for (i = 0; i < n; i++) {
#ifdef PANO_G2
        if (!(i & 0xFFFu)) g2_wdt_kick();       /* ~4 KB per few ms: a multi-megabyte read outlasts the watchdog */
#endif
        d[i] = (uint8_t)spi_byte(0);
    }
    spi_end();
}

static void wren(void) { spi_begin(); spi_byte(0x06u); spi_end(); }

/* WIP polling, bounded.  The bound is generous (SE is 3 s max per the datasheet) but a bound: a flash that never
 * finishes must become an error message, not a shell that stops answering. */
static int wait_wip(unsigned int spins)
{
    while (spins--) {
        if (!(spif_rdsr() & 1u)) return 0;
#ifdef PANO_G2
        if (!(spins & 0x3FFu)) g2_wdt_kick();   /* a sector erase takes up to 3 s: longer than the watchdog allows */
#endif
    }
    return -1;
}

#ifdef PANO_G2
static const spif_map_t s_map_m25p64 = {
    0x202017u, 0x800000u, 0x10000u,
    0x040000u, 0x380000u, 0x6C0000u, 0x6C0000u, 0x6D0000u, 0x800000u, 0x6D0000u, 0x120000u, 0x7FFFF0u, 0x750000u,
    "M25P64", "M25P64, 8 MB, 64 KB sectors", "64 KB",
    "0x000000 header, 0x040000 golden, 0x380000 multiboot, 0x6C0000 config, 0x6D0000 free",
    "0x380000-0x6BFFFF (multiboot image) and 0x6D0000-0x7FFFFF (free)",
    "image (0x040000-0x37FFFF) and the configuration table (0x6C0000)",
    "0x380000", "0x6D0000", "0x7FFFF0", "0x750000"
};
static const spif_map_t s_map_m25p128 = {
    0x202018u, 0x1000000u, 0x40000u,
    0x040000u, 0x480000u, 0x8C0000u, 0x8C0000u, 0x900000u, 0x1000000u, 0x900000u, 0x680000u, 0xFFFFF0u, 0x980000u,
    "M25P128", "M25P128, 16 MB, 256 KB sectors", "256 KB",
    "0x000000 header, 0x040000 golden, 0x480000 multiboot, 0x8C0000 config, 0x900000 free",
    "0x480000-0x8BFFFF (multiboot image) and 0x900000-0xFFFFFF (free)",
    "image (0x040000-0x47FFFF) and the configuration table (0x8C0000)",
    "0x480000", "0x900000", "0xFFFFF0", "0x980000"
};
static const spif_map_t s_map_none = {
    0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    "an unknown part", "an unknown part (no write is made)", "?",
    "none: the flash part is not known", "none", "every region", "(none)", "(none)", "(none)", "(none)"
};
static const spif_map_t *s_map;
/* the map of the part the JEDEC ID names; a recognised ID is kept, any other is read again at the next call.  Up to
 * three reads a call: the first SPI transfer after configuration can see the flash still finishing the configuration
 * read (fw/snes/snes_ldr.c). */
const spif_map_t *spif_map(void)
{
    uint32_t id; unsigned int k;
    if (s_map) return s_map;
    for (k = 0u; k < 3u; k++) {
        id = spif_rdid();
        if (id == s_map_m25p64.rdid)  return s_map = &s_map_m25p64;
        if (id == s_map_m25p128.rdid) return s_map = &s_map_m25p128;
    }
    return &s_map_none;
}

int spif_write_allowed(uint32_t addr, uint32_t n)
{
    if (!n || addr >= SPIF_SIZE || n > SPIF_SIZE - addr) return 0;
    if (addr >= SPIF_G2_MAIN && addr + n <= SPIF_G2_MAIN_END) return 1;
    if (addr >= SPIF_G2_FREE && addr + n <= SPIF_G2_FREE_END) return 1;
    return 0;
}
#endif

int spif_sector_erase(uint32_t addr)
{
#ifdef PANO_G2
    if (!spif_write_allowed(addr & ~(SPIF_SECTOR - 1u), SPIF_SECTOR)) return -2;
#endif
    wren();
    spi_begin(); cmd_addr(0xD8u, addr); spi_end();
    return wait_wip(2000000u);
}

/* the erase without its wait, for a caller that polls spif_wip() between other work: 0 sent, -2 an address outside the
 * writable regions (nothing sent) */
int spif_sector_erase_start(uint32_t addr)
{
#ifdef PANO_G2
    if (!spif_write_allowed(addr & ~(SPIF_SECTOR - 1u), SPIF_SECTOR)) return -2;
#endif
    wren();
    spi_begin(); cmd_addr(0xD8u, addr); spi_end();
    return 0;
}

int spif_wip(void) { return spif_rdsr() & 1u; }

int spif_page_program(uint32_t addr, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;
    if (!n || n > SPIF_PAGE || ((addr & (SPIF_PAGE - 1u)) + n) > SPIF_PAGE) return -1;
#ifdef PANO_G2
    if (!spif_write_allowed(addr, n)) return -2;
#endif
    wren();
    spi_begin(); cmd_addr(0x02u, addr);
    for (i = 0; i < n; i++) spi_byte(s[i]);
    spi_end();
    return wait_wip(200000u);
}
