/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
#include "s3e.h"
#include <stdlib.h>
#include <string.h>
#include "ptime.h"
int s3e_idcode(jtag_t *j, uint32_t *id)
{
    uint8_t z[4] = {0,0,0,0}, d[4] = {0,0,0,0};
    if (jtag_shift_ir(j, S3E_IR_IDCODE, NULL)) return -1;
    if (jtag_shift_dr(j, z, d, 32)) return -2;
    *id = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    return 0;
}
int s3e_usercode(jtag_t *j, uint32_t *uc)
{
    uint8_t z[4] = {0,0,0,0}, d[4] = {0,0,0,0};
    if (jtag_shift_ir(j, S3E_IR_USERCODE, NULL)) return -1;
    if (jtag_shift_dr(j, z, d, 32)) return -2;
    *uc = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    return 0;
}
static uint8_t rev8(uint8_t b) { b = (uint8_t)((b >> 4) | (b << 4)); b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2)); return (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1)); }
int s3e_program(jtag_t *j, const uint8_t *cfg, size_t len, int *done_bit)
{
    uint32_t cap = 0; int tries = 0;
    /* configuration data is shifted MSB-first per byte; the engine shifts LSB first */
    uint8_t *rev = malloc(len); if (!rev) return -1;
    for (size_t i = 0; i < len; i++) rev[i] = rev8(cfg[i]);
    if (jtag_shift_ir(j, S3E_IR_JPROGRAM, NULL)) { free(rev); return -2; }
    do {                                         /* wait until the configuration memory is cleared (INIT high) */
        if (jtag_shift_ir(j, S3E_IR_CFG_IN, &cap)) { free(rev); return -3; }
        if (++tries > 10000) { free(rev); return -4; }
    } while (!(cap & 0x10));
    if (jtag_shift_ir(j, S3E_IR_JSHUTDOWN, NULL)) { free(rev); return -5; }
    if (jtag_idle(j, 16)) { free(rev); return -6; }
    if (jtag_shift_ir(j, S3E_IR_CFG_IN, NULL)) { free(rev); return -7; }
    if (jtag_shift_dr(j, rev, NULL, (int)(len * 8))) { free(rev); return -8; }
    free(rev);
    if (jtag_idle(j, 1)) return -9;
    if (jtag_shift_ir(j, S3E_IR_JSTART, NULL)) return -10;
    if (jtag_idle(j, 32)) return -11;
    if (jtag_shift_ir(j, S3E_IR_BYPASS, &cap)) return -12;   /* IR capture bit 5 = DONE on Spartan-3 */
    { uint8_t z = 0; if (jtag_shift_dr(j, &z, NULL, 1)) return -13; }
    if (done_bit) *done_bit = (cap >> 5) & 1;
    return 0;
}
int s3e_done(jtag_t *j, int *done_bit)
{
    uint32_t cap = 0;
    if (jtag_shift_ir(j, S3E_IR_BYPASS, &cap)) return -1;
    if (done_bit) *done_bit = (cap >> 5) & 1;
    return 0;
}
int s3e_reboot(jtag_t *j, int timeout_ms, int *done_bit)
{
    int d = 0; double t0 = pt_now();
    if (jtag_shift_ir(j, S3E_IR_JPROGRAM, NULL)) return -1;
    if (jtag_idle(j, 100)) return -2;
    do {
        pt_sleep_ms(20);
        if (jtag_reset(j)) return -3;                 /* the TAP may have been reset by the reconfiguration */
        if (s3e_done(j, &d)) return -4;
        if (d) break;
    } while ((pt_now() - t0) * 1000.0 < timeout_ms);
    if (done_bit) *done_bit = d;
    return 0;
}
