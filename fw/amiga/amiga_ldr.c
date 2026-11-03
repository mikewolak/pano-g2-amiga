/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/amiga_ldr.c -- the block-RAM program of the G2 Amiga fabric: loads the Amiga firmware from flash into DDR2
 * chip A and starts it.
 *
 * The Amiga fabric (G2_AMIGA, with G2_SNES) has 32 KB of block RAM at 0x0000_0000 (rtl/g2/pano_g2_top.v SOC_RAM_AW 13).  This
 * program runs there after configuration:
 *   1 waits up to 3 s for chip A to calibrate;
 *   2 reads the tools/panoldr header at AMIGA_FLASH_FW (up to three reads: the first SPI transfer after configuration
 *     can see the flash still finishing the configuration read), checks the magic numbers and the header CRC32;
 *   3 copies the payload to its load address through the uncached alias, checks the payload CRC32;
 *   4 jumps to the entry point with interrupts off.
 * Any failure is reported on the UART and the board reconfigures from AMIGA_FLASH_EXIT, the golden image, by IPROG
 * after 3 s (amiga_flash.h).  A configuration stream that does not load at all falls back to the golden image
 * by the Spartan-6 multiboot fallback (GENERAL3/4 = 0x040000, rtl/g2/g2_icap_iprog.v). */
#include <stdint.h>
#include <string.h>
#include "../console.h"
#include "../g2.h"
#include "../spiflash.h"
#include "../crc32.h"
#include "../../tools/panoldr/panoldr.h"
#include "amiga_flash.h"

#include "../board.h"
#define CYC_PER_MS PANO_CYC_PER_MS          /* the Amiga fabric: 50 MHz (board.h, G2_SNES) */

static void say(const char *s) { con_puts(s); }
static void say_hex(uint32_t v) { con_hex32(v); }
static void wait_ms(unsigned int ms)
{
    while (ms--) { unsigned int c0 = rdcycle(); while (rdcycle() - c0 < CYC_PER_MS) ; g2_wdt_kick(); }
}

static void give_up(const char *why)
{
    say("amiga: "); say(why); say(" -- back to " AMIGA_EXIT_S " at 0x"); say_hex(AMIGA_FLASH_EXIT); say(" in 3 s\r\n");
    wait_ms(3000u);
    (void)g2_iprog(AMIGA_FLASH_EXIT);
    for (;;) g2_wdt_kick();
}

int main(void)
{
    uint8_t hdr[PANOLDR_HDR_BYTES];
    const uint32_t *h = (const uint32_t *)(const void *)hdr;
    uint32_t hc = 0u, len, load, entry, crc, got, off, n;
    unsigned int reads, t;
    uint8_t *dst;

    g2_init();
    say("\r\nG2 Amiga fabric: loader\r\n");
    for (t = 0u; t < 3000u && !g2_ddr_ready(); t++) wait_ms(1u);
    if (!g2_ddr_ready()) give_up("DDR2 chip A did not calibrate");

    { uint8_t dummy[4]; spif_read(AMIGA_FLASH_FW, dummy, sizeof dummy); }
    for (reads = 1u; ; reads++) {
        spif_read(AMIGA_FLASH_FW, hdr, sizeof hdr);
        hc = crc32_run(0xFFFFFFFFu, hdr, 28u) ^ 0xFFFFFFFFu;
        if ((h[0] == PANOLDR_MAGIC0 && h[1] == PANOLDR_MAGIC1 && hc == h[7]) || reads >= 3u) break;
    }
    if (h[0] != PANOLDR_MAGIC0 || h[1] != PANOLDR_MAGIC1) give_up("no Amiga firmware in flash");
    if (hc != h[7]) give_up("Amiga firmware header CRC mismatch");
    len = h[3]; load = h[4]; entry = h[5]; crc = h[6];
    if (!len || len > AMIGA_FLASH_FW_END - AMIGA_FLASH_FW - PANOLDR_HDR_BYTES) give_up("Amiga firmware length not credible");
    if ((load & 0xFC000000u) != 0x10000000u) give_up("Amiga firmware load address outside DDR2 chip A");
    say("amiga: firmware "); say_hex(len); say(" bytes to "); say_hex(load); say("\r\n");

    dst = (uint8_t *)((load & 0x0FFFFFFFu) + 0x90000000u);        /* uncached alias */
    got = 0xFFFFFFFFu;
    for (off = 0u; off < len; off += n) {
        n = len - off; if (n > 4096u) n = 4096u;
        spif_read(AMIGA_FLASH_FW + PANOLDR_HDR_BYTES + off, dst + off, n);
        got = crc32_run(got, dst + off, n);
        g2_wdt_kick();
    }
    got ^= 0xFFFFFFFFu;
    if (got != crc) give_up("Amiga firmware payload CRC mismatch");

    say("amiga: starting at "); say_hex(entry); say("\r\n");
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    __asm__ volatile ("fence.i" ::: "memory");
    ((void (*)(void))((entry & 0x0FFFFFFFu) + 0x10000000u))();
    give_up("the Amiga firmware returned");
    return 0;
}
