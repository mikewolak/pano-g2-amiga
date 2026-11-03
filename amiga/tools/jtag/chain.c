/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
#include "chain.h"
#include <string.h>
static const struct { uint32_t id; const char *part; int irlen; } xparts[] = {
    {0x01C10093, "XC3S100E", 6}, {0x01C1A093, "XC3S250E", 6}, {0x01C22093, "XC3S500E", 6}, {0x01C2E093, "XC3S1200E", 6}, {0x01C3A093, "XC3S1600E", 6},
    {0x0140C093, "XC3S50", 6}, {0x01414093, "XC3S200", 6}, {0x0141C093, "XC3S400", 6}, {0x01428093, "XC3S1000", 6}, {0x01434093, "XC3S1500", 6}, {0x01440093, "XC3S2000", 6}, {0x01448093, "XC3S4000", 6}, {0x01450093, "XC3S5000", 6},
    {0x02210093, "XC3S50A", 6}, {0x02218093, "XC3S200A", 6}, {0x02220093, "XC3S400A", 6}, {0x02228093, "XC3S700A", 6}, {0x02230093, "XC3S1400A", 6},
    {0x04001093, "XC6SLX9", 6}, {0x04002093, "XC6SLX16", 6}, {0x04004093, "XC6SLX25", 6}, {0x04008093, "XC6SLX45", 6},
    {0x05044093, "XCF01S", 8}, {0x05045093, "XCF02S", 8}, {0x05046093, "XCF04S", 8}, {0x05057093, "XCF08P", 16}, {0x05058093, "XCF16P", 16}, {0x05059093, "XCF32P", 16},
    {0x0362D093, "XC7A35T", 6}, {0x0362C093, "XC7A50T", 6}, {0x03631093, "XC7A100T", 6}, {0x03636093, "XC7A200T", 6},
};
static const struct { uint32_t mfg; const char *name; } vendors[] = {
    {0x049, "Xilinx"}, {0x06E, "Altera/Intel"}, {0x043, "Lattice"}, {0x01F, "Atmel/Microchip"}, {0x00E, "Freescale"}, {0x020, "STMicro"}, {0x0BF, "Broadcom"}, {0x00F, "TI"}, {0x0EF, "Winbond"},
};
void chain_decode(chain_dev_t *d)
{
    d->vendor = "?"; d->part = "unknown"; d->irlen = 0;
    if (d->idcode == 0) { d->vendor = "-"; d->part = "BYPASS-only device (no IDCODE)"; return; }
    uint32_t mfg = (d->idcode >> 1) & 0x7FF;
    for (size_t i = 0; i < sizeof vendors / sizeof vendors[0]; i++) if (vendors[i].mfg == mfg) d->vendor = vendors[i].name;
    for (size_t i = 0; i < sizeof xparts / sizeof xparts[0]; i++) if (xparts[i].id == (d->idcode & 0x0FFFFFFF)) { d->part = xparts[i].part; d->irlen = xparts[i].irlen; }
}
int chain_scan(jtag_t *j, chain_dev_t *dev, int *ndev, int *ir_total)
{
    uint8_t ones[64], out[64]; memset(ones, 0xFF, sizeof ones);
    *ndev = 0; *ir_total = 0;
    if (jtag_reset(j)) return -1;                       /* TLR loads IDCODE (or BYPASS) into every DR */
    /* DR: read up to CHAIN_MAX*32 bits while shifting ones; a device whose first bit is 0 is in BYPASS */
    int total = CHAIN_MAX * 32 + 32;
    uint8_t *tdo = out; (void)tdo;
    uint8_t big_in[CHAIN_MAX * 4 + 8], big_out[CHAIN_MAX * 4 + 8];
    memset(big_in, 0xFF, sizeof big_in); memset(big_out, 0, sizeof big_out);
    if (jtag_shift_dr(j, big_in, big_out, total)) return -2;
    int pos = 0;
    while (pos < total && *ndev < CHAIN_MAX) {
        int bit = (big_out[pos >> 3] >> (pos & 7)) & 1;
        if (!bit) { dev[*ndev].idcode = 0; chain_decode(&dev[*ndev]); (*ndev)++; pos += 1; continue; }
        if (pos + 32 > total) break;
        uint32_t id = 0; for (int k = 0; k < 32; k++) id |= (uint32_t)((big_out[(pos + k) >> 3] >> ((pos + k) & 7)) & 1) << k;
        if (id == 0xFFFFFFFFu) break;                   /* our own ones came round: end of chain */
        dev[*ndev].idcode = id; chain_decode(&dev[*ndev]); (*ndev)++; pos += 32;
    }
    /* IR length: fill the IR chain with zeros, then shift ones and count until the first one emerges */
    { uint8_t seq = 0x03; if (xpc_tms(j->x, &seq, 4)) return -3; }              /* RTI -> Shift-IR */
    uint8_t zeros[64]; memset(zeros, 0, sizeof zeros);
    if (xpc_shift(j->x, zeros, NULL, 256, 0)) return -4;
    memset(out, 0, sizeof out);
    if (xpc_shift(j->x, ones, out, 256, 1)) return -5;
    for (int i = 0; i < 256; i++) if ((out[i >> 3] >> (i & 7)) & 1) { *ir_total = i; break; }
    { uint8_t seq = 0x01; if (xpc_tms(j->x, &seq, 2)) return -6; }              /* Exit1-IR -> Update-IR -> RTI (IR = all ones = BYPASS) */
    return jtag_reset(j);
}
