/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* s3e.h -- Spartan-3E over JTAG: IDCODE, and configuration from a .bit file
 * (the xc3sprog "legacy" flow: JPROGRAM, wait for INIT, JSHUTDOWN, CFG_IN with
 * the bit-reversed bitstream, JSTART, BYPASS).  IR is 6 bits. */
#ifndef S3E_H
#define S3E_H
#include "jtag.h"
#define S3E_IRLEN     6
#define S3E_IR_BYPASS 0x3Fu
#define S3E_IR_IDCODE 0x09u
#define S3E_IR_USERCODE 0x08u   /* bitgen -g UserID: the build id */
#define S3E_IR_USER1  0x02u
#define S3E_IR_USER2  0x03u
#define S3E_IR_CFG_IN 0x05u
#define S3E_IR_JSTART 0x0Cu
#define S3E_IR_JSHUTDOWN 0x0Du
#define S3E_IR_JPROGRAM  0x0Bu
#define S3E_IR_ISC_ENABLE 0x10u
#define S3E_IDCODE_XC3S1600E 0x01C3A093u     /* version nibble masked off */
#define S6_IDCODE_XC6SLX100  0x04011093u     /* Pano Logic G2 revision C (Spartan-6 LX100, FGG484); the JTAG
                                              * configuration instructions and the 6-bit IR are those of the S3E */
#define PF_IDCODE_OK(id) (((id) & 0x0FFFFFFFu) == S3E_IDCODE_XC3S1600E || ((id) & 0x0FFFFFFFu) == S6_IDCODE_XC6SLX100)
#define PF_IDCODE_NAME(id) ((((id) & 0x0FFFFFFFu) == S3E_IDCODE_XC3S1600E) ? "XC3S1600E" : (((id) & 0x0FFFFFFFu) == S6_IDCODE_XC6SLX100) ? "XC6SLX100" : "unknown")
int s3e_idcode(jtag_t *j, uint32_t *id);
int s3e_usercode(jtag_t *j, uint32_t *uc);
/* program from raw configuration bytes (the .bit file's 'e' section, MSB-first
 * per byte as Xilinx writes it); reports DONE via the IR capture bits. */
int s3e_program(jtag_t *j, const uint8_t *cfg, size_t len, int *done_bit);
/* JPROGRAM: the FPGA clears and reconfigures from its mode-pin source (the SPI
 * flash on the G1).  Polls DONE (IR capture bit 5) for up to `timeout_ms`. */
int s3e_reboot(jtag_t *j, int timeout_ms, int *done_bit);
/* DONE right now, from the IR capture of a BYPASS shift */
int s3e_done(jtag_t *j, int *done_bit);
#endif
