/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* jtagtool.c -- JTAG from the Amiga through an FTDI FT2232H on the G2's USB hub (channel A in MPSSE mode; the firmware
 * passes the serial driver's bytes to the chip with AMIGA.INI FTDI=MPSSE).
 *
 *   jtagtool [DEVICE=g2serial.device] [UNIT=n] [CLOCK=hz] LOOP [INTERNAL|EXTERNAL|BOTH] [BYTES=n] [ROUNDS=n]
 *   jtagtool [DEVICE=...] [CLOCK=hz] SCAN
 *   jtagtool [DEVICE=...] [CLOCK=hz] PROGRAM FILE=design.bit
 *   jtagtool [DEVICE=...] [CLOCK=hz] REBOOT
 *
 *   LOOP      TDI to TDO: INTERNAL through the chip's loopback (0x84, no wiring), EXTERNAL through a wire from ADBUS1
 *             (TDI) to ADBUS2 (TDO); BOTH (default) runs the two.  Each round sends BYTES bytes of a pattern (pseudo-
 *             random, 0x55/0xAA, walking one, all zeros, all ones in turn) and compares what comes back.  Prints
 *             bytes, bit errors, the round trip and bytes/s; PASS with 0 bit errors.
 *   SCAN      the chain from Test-Logic-Reset: every IDCODE (vendor and part when known) and the total IR length
 *             (chain.c).
 *   PROGRAM   configuration of a Xilinx XC3S1600E (Pano G1) or XC6SLX100 (Pano G2 revision C) from a .bit file over
 *             JTAG: the device's IDCODE checked, then s3e.c's sequence (JPROGRAM, CFG_IN, JSTART); PASS when DONE
 *             goes high.  The configuration is in the FPGA only (lost at power-off); its SPI flash is not written.
 *   REBOOT    JPROGRAM: the FPGA reconfigures from its own flash; PASS when DONE goes high within 8 s.
 *
 * jtag.c, chain.c, s3e.c, s3e.h and bitfile.c are Mike Wolak's G1/G2 JTAG flasher's, unchanged; mpsse.c is their bit engine here.
 * The serial stream is amiga/tools/xfer/zio_amiga.c.  Return code 0 PASS, 5 FAIL, 20 the stream did not open.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define ZIO_IMPL                    /* zio.h without its lrzsz stream macros */
#include "zio.h"
#include "xpc.h"
#include "jtag.h"
#include "chain.h"
#include "s3e.h"
#include "bitfile.h"
#include "ptime.h"

static unsigned long s_seed = 0x2545F491u;
static uint8_t rnd8(void) { s_seed = s_seed * 1103515245u + 12345u; return (uint8_t)(s_seed >> 16); }

static void pattern(uint8_t *b, int n, int round)
{
    int i;
    switch (round % 5) {
    case 0: for (i = 0; i < n; i++) b[i] = rnd8(); break;
    case 1: for (i = 0; i < n; i++) b[i] = (i & 1) ? 0xAA : 0x55; break;
    case 2: for (i = 0; i < n; i++) b[i] = (uint8_t)(1u << (i & 7)); break;
    case 3: memset(b, 0x00, (size_t)n); break;
    default: memset(b, 0xFF, (size_t)n); break;
    }
}

static int popcount8(unsigned v) { int c = 0; while (v) { c += v & 1u; v >>= 1; } return c; }

/* a number with thousands separators */
static const char *th(unsigned long v)
{
    static char b[4][20]; static int k;
    char t[20], *o = b[k = (k + 1) & 3];
    int n = snprintf(t, sizeof t, "%lu", v), i, j = 0;
    for (i = 0; i < n; i++) { if (i && (n - i) % 3 == 0) o[j++] = ','; o[j++] = t[i]; }
    o[j] = 0;
    return o;
}

static int loop_test(xpc_t *x, int internal, int bytes, int rounds)
{
    uint8_t *out = malloc((size_t)bytes), *in = malloc((size_t)bytes);
    unsigned long errs = 0, total = 0, first_bad = 0, ones = 0;
    clock_t t0, t1;
    double s, rt_max = 0.0;
    int r, i, bad_rounds = 0;
    if (!out || !in) { printf("out of memory\n"); free(out); free(in); return 5; }
    if (mpsse_loopback(x, internal)) { printf("loopback %s: the command failed (%d)\n", internal ? "on" : "off", x->err); free(out); free(in); return 5; }
    t0 = clock();
    for (r = 0; r < rounds; r++) {
        clock_t a = clock(), b;
        unsigned long e = 0;
        pattern(out, bytes, r);
        memset(in, 0, (size_t)bytes);
        if (mpsse_xfer(x, out, in, bytes)) { printf("round %d: no answer (%d)\n", r, x->err); break; }
        b = clock();
        if ((double)(b - a) / CLOCKS_PER_SEC > rt_max) rt_max = (double)(b - a) / CLOCKS_PER_SEC;
        for (i = 0; i < bytes; i++) { e += (unsigned long)popcount8(out[i] ^ in[i]); ones += (unsigned long)popcount8(in[i]); }
        if (e) { if (!bad_rounds) first_bad = (unsigned long)r; bad_rounds++; }
        errs += e; total += (unsigned long)bytes;
    }
    t1 = clock();
    s = (double)(t1 - t0) / CLOCKS_PER_SEC;
    printf("loopback %s: %d rounds of %s bytes, %s bytes, bit errors %s",
           internal ? "internal (0x84)" : "external (ADBUS1 TDI to ADBUS2 TDO)", r, th((unsigned long)bytes), th(total), th(errs));
    if (bad_rounds) printf(" in %d rounds (first: round %lu)", bad_rounds, first_bad);
    printf(", %.2f s, %s bytes/s, longest round trip %.1f ms", s, th(s > 0.0 ? (unsigned long)(total / s) : 0ul), rt_max * 1000.0);
    if (r == rounds && !errs) printf(": PASS\n");
    else {
        printf(": FAIL");
        if (!internal && total && ones == 0) printf(" (TDO read 0 throughout: no wire, or TDO held low)");
        else if (!internal && total && ones == total * 8u) printf(" (TDO read 1 throughout: no wire, TDO pulled up)");
        printf("\n");
    }
    (void)mpsse_loopback(x, 0);
    free(out); free(in);
    return (r == rounds && !errs) ? 0 : 5;
}

static int scan(xpc_t *x)
{
    jtag_t j;
    chain_dev_t d[CHAIN_MAX];
    int n = 0, ir = 0, i, r;
    if (jtag_init(&j, x, 6)) { printf("TAP reset failed (%d)\n", x->err); return 5; }
    r = chain_scan(&j, d, &n, &ir);
    if (r) { printf("chain scan failed: %d (%d)\n", r, x->err); return 5; }
    printf("chain: %d device%s, IR total %d bits\n", n, n == 1 ? "" : "s", ir);
    for (i = 0; i < n; i++)
        printf("  %d: IDCODE 0x%08lX  %s %s  IR %d\n", i, (unsigned long)d[i].idcode, d[i].vendor, d[i].part, d[i].irlen);
    if (!n) printf("  no device: TDO not connected, or the chain not powered\n");
    return n ? 0 : 5;
}

/* panoflash's pf_load_mem and pf_reboot (the JTAG flasher) on this engine, printing */
static int program(xpc_t *x, const char *path)
{
    bitfile_t b; jtag_t j; uint32_t id = 0; int r, d = 0; double t0;
    if ((r = bitfile_load(&b, path)) != 0) { printf("bitfile: %s: bad or unreadable [%d]\n", path, r); return 5; }
    printf("bitfile: %s  part %s  %s %s  %lu config bytes\n", b.design, b.part, b.date, b.time, (unsigned long)b.cfg_len);
    if (strncmp(b.part, "3s1600e", 7) != 0 && strncmp(b.part, "6slx100", 7) != 0)
        printf("bitfile: WARNING part is '%s', the boards are 3s1600efg320 (G1) and 6slx100fgg484 (G2)\n", b.part);
    if (jtag_init(&j, x, S3E_IRLEN) || s3e_idcode(&j, &id) || !PF_IDCODE_OK(id)) {
        printf("load: refusing, IDCODE %08lX is neither an XC3S1600E nor an XC6SLX100\n", (unsigned long)id);
        bitfile_free(&b); return 5;
    }
    printf("load: %s, programming %lu bytes over JTAG at TCK %lu Hz...\n", PF_IDCODE_NAME(id), (unsigned long)b.cfg_len, x->tck_hz);
    t0 = pt_now();
    r = s3e_program(&j, b.cfg, b.cfg_len, &d);
    bitfile_free(&b);
    if (r) { printf("load: FAILED [%d] (%d)\n", r, x->err); return 5; }
    printf("load: %s, %.1f s\n", d ? "programmed, DONE high" : "DONE did not go high", pt_now() - t0);
    return d ? 0 : 5;
}

static int fpga_reboot(xpc_t *x)
{
    jtag_t j; uint32_t id = 0; int r, d = 0; double t0;
    if (jtag_init(&j, x, S3E_IRLEN) || s3e_idcode(&j, &id) || !PF_IDCODE_OK(id)) {
        printf("reboot: refusing, IDCODE %08lX is neither an XC3S1600E nor an XC6SLX100\n", (unsigned long)id);
        return 5;
    }
    printf("reboot: JPROGRAM -- the FPGA reconfigures from the SPI flash...\n");
    t0 = pt_now();
    r = s3e_reboot(&j, 8000, &d);
    printf("reboot: %s after %.2f s\n", r ? "JTAG error" : d ? "DONE high, the flash image is running" :
           "DONE stayed LOW -- the flash holds no valid bitstream (or the sync word is missing)", pt_now() - t0);
    return (!r && d) ? 0 : 5;
}

int main(int argc, char **argv)
{
    zio_cfg_t c;
    xpc_t x;
    unsigned long hz = 1000000u;
    int bytes = 1024, rounds = 100, which = 3, rc = 0, i;
    const char *cmd = NULL, *file = NULL;
    zio_args(&argc, &argv, &c);
    if (!strcmp(c.device, "serial.device")) c.device = "g2serial.device";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "CLOCK=", 6)) hz = strtoul(a + 6, NULL, 10);
        else if (!strncmp(a, "BYTES=", 6)) bytes = (int)strtoul(a + 6, NULL, 10);
        else if (!strncmp(a, "ROUNDS=", 7)) rounds = (int)strtoul(a + 7, NULL, 10);
        else if (!strcmp(a, "INTERNAL")) which = 1;
        else if (!strcmp(a, "EXTERNAL")) which = 2;
        else if (!strcmp(a, "BOTH")) which = 3;
        else if (!strncmp(a, "FILE=", 5)) file = a + 5;
        else if (!strcmp(a, "LOOP") || !strcmp(a, "SCAN") || !strcmp(a, "PROGRAM") || !strcmp(a, "REBOOT")) cmd = a;
        else { cmd = NULL; break; }
    }
    if (!cmd || bytes < 1 || bytes > 4096 || rounds < 1 || (!strcmp(cmd, "PROGRAM") && !file)) {
        printf("usage: jtagtool [DEVICE=g2serial.device] [UNIT=n] [CLOCK=hz] LOOP [INTERNAL|EXTERNAL|BOTH] [BYTES=1-4096] [ROUNDS=n]\n"
               "       jtagtool [DEVICE=...] [CLOCK=hz] SCAN\n"
               "       jtagtool [DEVICE=...] [CLOCK=hz] PROGRAM FILE=design.bit\n"
               "       jtagtool [DEVICE=...] [CLOCK=hz] REBOOT\n");
        return 10;
    }
    if (zio_open(&c)) return 20;
    if (mpsse_open(&x, hz)) {
        printf("MPSSE: no answer to the sync command (%d): is the FT2232H on the hub, AMIGA.INI FTDI=MPSSE, %s the serial "
               "FIFO board's driver?\n", x.err, c.device);
        zio_close();
        return 20;
    }
    printf("MPSSE: sync 0xFA 0xAA, TCK %s Hz, %s unit %lu\n", th(x.tck_hz), c.device, c.unit);
    if (!strcmp(cmd, "LOOP")) {
        if (which & 1) rc |= loop_test(&x, 1, bytes, rounds);
        if (which & 2) rc |= loop_test(&x, 0, bytes, rounds);
    } else if (!strcmp(cmd, "PROGRAM")) rc = program(&x, file);
    else if (!strcmp(cmd, "REBOOT")) rc = fpga_reboot(&x);
    else rc = scan(&x);
    printf("bytes written %s, read %s\n", th(x.stat_out), th(x.stat_in));
    zio_close();
    return rc ? 5 : 0;
}
