/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/main.c -- back up, verify and write the flash of a Pano Logic G2 revision C that runs its STOCK image, over the
 * network, with no Pano Logic software.  Mike Wolak, EPROM Foundry Inc.
 *
 *   tnpflash discover [-b <broadcast-or-unicast ip>]... [-l <local ip>] [-t seconds]
 *   tnpflash info <ip>
 *   tnpflash backup <ip> <out.bin>                    the whole flash, read twice, compared, hashed
 *   tnpflash verify <ip> FILE[@0xOFF]...              is the flash exactly these files (read-only)
 *   tnpflash g2write <ip> <backup-8MB.bin> <raw.bin>  the main image at 0x380000 only, verified
 *   tnpflash reset <ip>                               clear a session a dead client left open
 * -v traces every datagram in hex. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tnp.h"
#include "stream.h"
#include "flash.h"
#include "sha256.h"
#include "bitfile.h"
#include <signal.h>

static int g_trace;

static void print_info(const pano_info_t *i)
{
    char a[16], b[16], c[16];
    printf("%-16s %-16s %-16s %02x-%02x-%02x-%02x-%02x-%02x  rev %u.%u  cfg0 %08lX\n", plat_ip_str(i->ip, a), plat_ip_str(i->mask, b), plat_ip_str(i->gw, c),
           i->mac[0], i->mac[1], i->mac[2], i->mac[3], i->mac[4], i->mac[5], (unsigned)(i->rev >> 16), (unsigned)(i->rev & 0xFFFFu), (unsigned long)i->cfg0);
}

static int cmd_discover(int argc, char **argv)
{
    uint32_t dst[8], local = 0, seen[32]; unsigned nd = 0, ns = 0, secs = 3, k, i; tnp_dgram_t d; uint32_t t0, last = 0; int first = 1;
    for (k = 0; k + 1 < (unsigned)argc; k += 2) {
        if (!strcmp(argv[k], "-b") && nd < 8u) { if (!plat_parse_ip(argv[k + 1], &dst[nd++])) { fprintf(stderr, "bad address %s\n", argv[k + 1]); return 2; } }
        else if (!strcmp(argv[k], "-l")) { if (!plat_parse_ip(argv[k + 1], &local)) { fprintf(stderr, "bad address %s\n", argv[k + 1]); return 2; } }
        else if (!strcmp(argv[k], "-t")) secs = (unsigned)atoi(argv[k + 1]);
        else { fprintf(stderr, "discover: unknown option %s\n", argv[k]); return 2; }
    }
    if (!nd) dst[nd++] = 0xFFFFFFFFu;                         /* limited broadcast, as Pano's tool does */
    if (tnp_dgram_open(&d, local, TNP_DISC_SPORT, 1) && tnp_dgram_open(&d, local, 0, 1)) { fprintf(stderr, "cannot open a UDP socket\n"); return 1; }
    d.trace = g_trace;
    printf("%-16s %-16s %-16s %-18s %s\n", "IP ADDRESS", "SUBNET MASK", "GATEWAY", "MAC ADDRESS", "REVISION");
    t0 = plat_ms();
    while ((uint32_t)(plat_ms() - t0) < secs * 1000u) {
        uint8_t r[1600]; struct sockaddr_in from; socklen_plat fl = sizeof from; int n;
        if (first || (uint32_t)(plat_ms() - last) >= 1000u) {                   /* the probe: CfgRead of register 0, once a second */
            uint8_t p[32], m[16]; tnp_hdr_t h; size_t len; struct sockaddr_in a;
            first = 0; last = plat_ms();
            h.flags = TNP_F_DGRAM; h.seq = d.seq; h.ack = 0; h.security = 0; d.seq = (d.seq + 1u) & 0xFFFu;
            tnp_hdr_put(p, &h); len = tnp_ep_put(p, 8u, sizeof p, 0u, m, ep0_build(m, EP0_CFGREAD, 0u, 1u, NULL, 1u));
            for (i = 0; i < nd; i++) {
                memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(dst[i]); a.sin_port = htons(TNP_PORT);
                if (g_trace) hexdump("tx", p, len);
                sendto(d.s, (const char *)p, (int)len, 0, (struct sockaddr *)&a, sizeof a);
            }
        }
        if (plat_wait_readable(d.s, 200u) <= 0) continue;
        n = recvfrom(d.s, (char *)r, sizeof r, 0, (struct sockaddr *)&from, &fl);
        if (n < 8) continue;
        if (g_trace) hexdump("rx", r, (size_t)n);
        { tnp_hdr_t h; uint32_t ip = ntohl(from.sin_addr.s_addr); int dup = 0;
          if (!tnp_hdr_get(r, (size_t)n, &h)) continue;
          for (i = 0; i < ns; i++) if (seen[i] == ip) dup = 1;
          if (dup || ns >= 32u) continue;
          seen[ns++] = ip; }
    }
    tnp_dgram_close(&d);
    for (i = 0; i < ns; i++) {                                                   /* ask each responder for the rest, unicast */
        tnp_dgram_t u; pano_info_t inf; char a[16];
        if (tnp_dgram_open(&u, local, 0, 0)) continue;
        u.trace = g_trace;
        if (!pano_info_read(&u, seen[i], &inf)) print_info(&inf); else printf("%-16s answered the probe but not the register reads\n", plat_ip_str(seen[i], a));
        tnp_dgram_close(&u);
    }
    if (!ns) printf("(nothing answered on UDP %u in %u s -- is it powered, on this segment, and holding a DHCP lease? try -b <subnet broadcast> or -l <local ip>)\n", TNP_PORT, secs);
    return ns ? 0 : 1;
}

static int cmd_info(const char *ips)
{
    uint32_t ip; tnp_dgram_t u; pano_info_t inf; int rc;
    if (!plat_parse_ip(ips, &ip)) { fprintf(stderr, "bad address %s\n", ips); return 2; }
    if (tnp_dgram_open(&u, 0, 0, 0)) { fprintf(stderr, "cannot open a UDP socket\n"); return 1; }
    u.trace = g_trace;
    rc = pano_info_read(&u, ip, &inf); tnp_dgram_close(&u);
    if (rc) { fprintf(stderr, "%s: no answer (rc %d)\n", ips, rc); return 1; }
    print_info(&inf);
    { board_t bd; board_identify(inf.cfg0, inf.rev, &bd);
      printf("board: %s%s\n", bd.name, bd.series == 2u && bd.bytes == 0x800000u ? " -- g2write accepts this unit" : " -- g2write refuses this unit"); }
    return 0;
}

static void progress(uint32_t done, uint32_t total, const char *what)
{
    fprintf(stderr, "\r%s %5.1f%%  (%lu / %lu words)   ", what, 100.0 * done / total, (unsigned long)done, (unsigned long)total);
    if (done == total) fprintf(stderr, "\n");
}

/* connect, identify, switch the device's watchdog off (cfg 0x27 = 0, as Pano's updaters do on both series).  allow_g2 admits a
 * G2 -- for the READ-ONLY commands only; every command that erases or programs opens through open_g1. */
static int open_board(const char *ips, tnp_stream_t *c, uint32_t *cfg0, uint32_t *rev, board_t *bd, int allow_g2)
{
    uint32_t ip;
    if (!plat_parse_ip(ips, &ip)) { fprintf(stderr, "bad address %s\n", ips); return 2; }
    if (tnp_connect(c, ip, g_trace)) { fprintf(stderr, "%s: %s\n", ips, c->err); return 1; }
    if (reg_read(c, 1, 0x00u, cfg0) || reg_read(c, 1, 0x01u, rev)) { fprintf(stderr, "%s: connected but no answer to a config read: %s\n", ips, c->err); tnp_close(c); return 1; }
    board_identify(*cfg0, *rev, bd);
    printf("board id 0x%08lX (%s), FPGA rev %u.%u, flash %lu bytes\n", (unsigned long)*cfg0, bd->name, (unsigned)(*rev >> 16), (unsigned)(*rev & 0xFFFFu), (unsigned long)bd->bytes);
    if (!bd->bytes || (bd->series != 0u && !allow_g2)) {
        fprintf(stderr, bd->bytes ? "a G2: this command writes, and writing a G2 is not supported -- refusing\n" : "that board is not one this tool knows -- refusing\n");
        tnp_close(c); return 3;
    }
    if (reg_write(c, 1, 0x27u, 0u)) { fprintf(stderr, "could not switch the watchdog off: %s\n", c->err); tnp_close(c); return 1; }
    return 0;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static long find_sync(const uint8_t *a, uint32_t from, uint32_t span)
{
    uint32_t i;
    for (i = from; i + 4u <= from + span; i++) if (a[i] == 0xAA && a[i+1] == 0x99 && a[i+2] == 0x55 && a[i+3] == 0x66) return (long)i;
    return -1;
}

static int cmd_backup(const char *ips, const char *path, unsigned batch)
{
    tnp_stream_t c; uint32_t cfg0, rev, nbytes, nwords; board_t bd; uint8_t *a, *b; int rc; FILE *f; uint8_t dg[32]; char hex[65]; unsigned i; uint32_t t0;
    if ((rc = open_board(ips, &c, &cfg0, &rev, &bd, 1))) return rc;
    nbytes = bd.bytes; nwords = nbytes / 4u;
    a = (uint8_t *)malloc(nbytes); b = (uint8_t *)malloc(nbytes);
    if (!a || !b) { fprintf(stderr, "out of memory\n"); tnp_close(&c); return 1; }
    t0 = plat_ms();
    printf("reading the whole flash (%lu words) -- pass 1\n", (unsigned long)nwords);
    if ((rc = flash_read(&c, 0u, nwords, a, batch, progress))) { fprintf(stderr, rc == -99 ? "\n%s\n" : "read failed: %s\n", c.err); tnp_close(&c); return 1; }
    printf("pass 2 (the two passes must agree)\n");
    if ((rc = flash_read(&c, 0u, nwords, b, batch, progress))) { fprintf(stderr, rc == -99 ? "\n%s\n" : "read failed: %s\n", c.err); tnp_close(&c); return 1; }
    tnp_close(&c);
    if (memcmp(a, b, nbytes)) { unsigned long d = 0; for (i = 0; i < nbytes; i++) if (a[i] != b[i]) d++; fprintf(stderr, "THE TWO PASSES DIFFER in %lu bytes: not writing a file you could not trust\n", d); return 4; }
    sha256(a, nbytes, dg); for (i = 0; i < 32u; i++) sprintf(hex + 2*i, "%02x", dg[i]);
    if (!(f = fopen(path, "wb")) || fwrite(a, 1, nbytes, f) != nbytes) { fprintf(stderr, "cannot write %s\n", path); if (f) fclose(f); return 1; }
    fclose(f);
    printf("wrote %s: %lu bytes, sha256 %s, %lu s, %lu packets (%lu resent)\n", path, (unsigned long)nbytes, hex, (unsigned long)((plat_ms() - t0) / 1000u), c.n_tx, c.n_retx);
    if (bd.series == 0u) {
        long so = find_sync(a, 0u, 4096u);
        printf("sync word AA995566 at byte %ld%s\n", so, so < 0 ? "  -- NOT FOUND: the byte order or the read path is wrong, do not trust this dump" : "");
        printf("bytes 0xE0000..: %02x %02x %02x %02x %02x %02x %02x %02x  (Pano's MAC/lock area; the tool never writes there)\n", a[0xE0000], a[0xE0001], a[0xE0002], a[0xE0003], a[0xE0004], a[0xE0005], a[0xE0006], a[0xE0007]);
    } else if (nbytes == 0x800000u) {
        /* the rev C map, checked against what Pano's Series 2 updater writes */
        long s0 = find_sync(a, 0u, 64u), sg = find_sync(a, G2C_GOLDEN, 4096u), sm = find_sync(a, G2C_MAIN, 4096u);
        uint32_t t0w = be32(a + G2C_CONFIG), mac_hi = 0, mac_lo = 0; unsigned k;
        for (k = 0; k + 8u <= 0x100u; k += 8u) {
            uint32_t tag = be32(a + G2C_CONFIG + k), val = be32(a + G2C_CONFIG + k + 4u);
            if (tag == 0x10000010u) mac_hi = val;
            if (tag == 0x10000011u) mac_lo = val;
            if (tag == 0xFFFFFFFFu) break;
        }
        printf("header sync at byte %ld%s\n", s0, s0 < 0 ? "  -- NOT FOUND" : "  (multiboot header)");
        printf("golden image sync at byte %ld%s\n", sg, sg < 0 ? "  -- NOT FOUND" : "");
        printf("main image sync at byte %ld%s\n", sm, sm < 0 ? "  -- NOT FOUND" : "");
        printf("config table at 0x6C0000: first tag 0x%08lX; MAC %04lX%08lX from the table\n", (unsigned long)t0w, (unsigned long)(mac_hi & 0xFFFFu), (unsigned long)mac_lo);
    }
    free(a); free(b);
    return 0;
}

static int load_regions(int argc, char **argv, region_t *r, unsigned *nr)
{
    int i;
    for (i = 0; i < argc && *nr < 8u; i++) {
        char *at = strrchr(argv[i], '@'); bitfile_t bf; long so; char err[160]; uint32_t off = 0;
        if (at) { off = (uint32_t)strtoul(at + 1, NULL, 0); *at = 0; }
        if (bitfile_load(argv[i], &bf, &so, err, sizeof err)) { fprintf(stderr, "%s\n", err); return -1; }
        if (off == 0 && (so < 0 || so > 64)) { fprintf(stderr, "%s: no Xilinx sync word near the start -- that is not a bitstream, and offset 0 is where the FPGA boots from\n", argv[i]); return -1; }
        if (off == 0 && bf.len != 746212u) fprintf(stderr, "note: %s is %lu bytes; an XC3S1600E bitstream is 746,212\n", argv[i], (unsigned long)bf.len);
        r[*nr].path = argv[i]; r[*nr].byte_off = off; r[*nr].data = bf.data; r[*nr].len = bf.len;
        printf("region %u: %s%s -> flash bytes 0x%06lX..0x%06lX (%lu bytes)%s\n", *nr, argv[i], bf.was_bit ? " (.bit unwrapped)" : "", (unsigned long)off, (unsigned long)(off + bf.len - 1u), (unsigned long)bf.len, bf.was_bit ? "" : "");
        if (bf.was_bit) printf("          design %s  part %s  %s %s\n", bf.design, bf.part, bf.date, bf.time);
        (*nr)++;
    }
    return 0;
}

static int cmd_verify(const char *ips, int argc, char **argv)
{
    tnp_stream_t c; uint32_t cfg0, rev; region_t r[8]; unsigned nr = 0; int rc; long d; char err[200];
    board_t bd;
    if (load_regions(argc, argv, r, &nr) || !nr) return 2;
    if ((rc = open_board(ips, &c, &cfg0, &rev, &bd, 1))) return rc;
    d = flash_verify_regions(&c, r, nr, progress, err, sizeof err); tnp_close(&c);
    if (d < 0) { fprintf(stderr, "%s\n", err); return 1; }
    printf(d ? "DIFFERENT: %ld word(s)\n" : "IDENTICAL: the flash holds exactly these files\n", d);
    return d ? 1 : 0;
}

/* g2write: a G2 revision C only.  The image goes to the multiboot (main) slot at 0x380000 and nothing else is erased or
 * programmed: the region must lie inside 0x380000-0x6BFFFF, so the header (0x000000), the golden image (0x040000) and the
 * configuration table (0x6C0000) are never touched.  The unit must be the one the 8 MB backup came from: the header sector
 * and the configuration-table sector are read and compared with the backup before anything is erased. */
static int cmd_g2write(const char *ips, const char *backup_path, const char *img_path)
{
    tnp_stream_t c; uint32_t cfg0, rev; board_t bd; region_t r; FILE *f; long n; int rc; char err[200]; uint32_t t0, j;
    static uint8_t was[0x800000u], img[G2C_CONFIG - G2C_MAIN + 1u], now[0x10000u];
    if (!(f = fopen(backup_path, "rb")) || fread(was, 1, sizeof was, f) != sizeof was) { fprintf(stderr, "%s: not an 8 MB G2 backup -- no backup, no write\n", backup_path); if (f) fclose(f); return 2; }
    fclose(f);
    if (!(was[4] == 0xAA && was[5] == 0x99 && was[6] == 0x55 && was[7] == 0x66 && was[0x10] == 0x32 && was[0x11] == 0x81 && was[0x12] == 0x03 && was[0x13] == 0x38)) { fprintf(stderr, "%s: the header does not load the main image from 0x380000 -- refusing\n", backup_path); return 2; }
    if (!(f = fopen(img_path, "rb"))) { fprintf(stderr, "%s: cannot open\n", img_path); return 2; }
    n = (long)fread(img, 1, sizeof img, f); fclose(f);
    if (n <= 0 || n > (long)(G2C_CONFIG - G2C_MAIN)) { fprintf(stderr, "%s: %ld bytes; the main slot holds %lu\n", img_path, n, (unsigned long)(G2C_CONFIG - G2C_MAIN)); return 2; }
    for (j = 0; j + 4u <= 64u && j + 4u <= (uint32_t)n; j++) if (img[j] == 0xAA && img[j+1] == 0x99 && img[j+2] == 0x55 && img[j+3] == 0x66) break;
    if (j + 4u > 64u || j + 4u > (uint32_t)n) { fprintf(stderr, "%s: no sync word 0xAA995566 in the first 64 bytes -- not a raw bitstream\n", img_path); return 2; }
    if ((rc = open_board(ips, &c, &cfg0, &rev, &bd, 1))) return rc;
    if (bd.series != 2 || bd.bytes != 0x800000u) { fprintf(stderr, "not a G2 revision C (%s) -- refusing\n", bd.name); tnp_close(&c); return 4; }
    if ((rc = flash_read(&c, 0u, 0x4000u, now, 64u, NULL)) || memcmp(now, was, 0x10000u)) { fprintf(stderr, "header sector differs from %s (or read failed) -- refusing\n", backup_path); tnp_close(&c); return 4; }
    if ((rc = flash_read(&c, G2C_CONFIG / 4u, 0x4000u, now, 64u, NULL)) || memcmp(now, was + G2C_CONFIG, 0x10000u)) { fprintf(stderr, "configuration table differs from %s (or read failed) -- refusing\n", backup_path); tnp_close(&c); return 4; }
    printf("unit matches %s (header and configuration table).  Writing %ld bytes to 0x%06lX..0x%06lX; nothing else is touched.\n", backup_path, n, (unsigned long)G2C_MAIN, (unsigned long)(G2C_MAIN + (uint32_t)n - 1u));
    fflush(stdout);
    r.path = img_path; r.byte_off = G2C_MAIN; r.data = img; r.len = (size_t)n;
    t0 = plat_ms();
    rc = flash_write_regions(&c, &r, 1u, G2C_CONFIG / 4u, progress, err, sizeof err);
    tnp_close(&c);
    if (rc) { fprintf(stderr, "\nWRITE FAILED: %s\nDO NOT POWER-CYCLE: the stock image still runs; g2write can be repeated.  The golden image is untouched.\n", err); return 1; }
    printf("\nDONE in %lu s: written and verified.  Power-cycle to boot it; a failed image falls back to the golden image.\n", (unsigned long)((plat_ms() - t0) / 1000u));
    return 0;
}

/* reset: a session left open by a host that died holds the device (it answers every new SYN with SYN+NAK, and only a
 * RST from the SAME source port clears it).  So send one RST from every port. */
static int cmd_reset(const char *ips)
{
    uint32_t ip;
    if (!plat_parse_ip(ips, &ip)) { fprintf(stderr, "bad address %s\n", ips); return 2; }
    printf("sending a reset from every source port…\n");
    if (tnp_force_reset(ip) == 0) { printf("the device accepts a connection again\n"); return 0; }
    printf("still refused: another machine may be holding the session\n");
    return 1;
}

static void on_sigint(int sig) { (void)sig; flash_cancel = 1; }
int main(int argc, char **argv)
{
    int rc = 2;
    signal(SIGINT, on_sigint);
    if (argc >= 2 && !strcmp(argv[1], "-v")) { g_trace = 1; argv++; argc--; }
    if (plat_init()) { fprintf(stderr, "network start-up failed\n"); return 1; }
    if (argc >= 2 && !strcmp(argv[1], "discover")) rc = cmd_discover(argc - 2, argv + 2);
    else if (argc == 3 && !strcmp(argv[1], "info")) rc = cmd_info(argv[2]);
    else if (argc >= 4 && !strcmp(argv[1], "backup")) rc = cmd_backup(argv[2], argv[3], argc >= 5 ? (unsigned)atoi(argv[4]) : 16u);
    else if (argc >= 4 && !strcmp(argv[1], "verify")) rc = cmd_verify(argv[2], argc - 3, argv + 3);
    else if (argc == 5 && !strcmp(argv[1], "g2write")) rc = cmd_g2write(argv[2], argv[3], argv[4]);
    else if (argc == 3 && !strcmp(argv[1], "reset")) rc = cmd_reset(argv[2]);
    else fprintf(stderr, "usage: tnpflash [-v] discover [-b ip]... [-l local-ip] [-t secs]\n"
                         "       tnpflash [-v] info <ip>\n"
                         "       tnpflash [-v] backup <ip> <out.bin> [reads-per-packet]  -- the whole flash, read twice, compared, hashed\n"
                         "       tnpflash [-v] verify <ip> FILE[@0xOFF]...                -- is the flash exactly these files (read-only)\n"
                         "       tnpflash [-v] g2write <ip> <backup-8MB.bin> <raw.bin>    -- G2 rev C: the main slot 0x380000 only, verified\n"
                         "       tnpflash reset <ip>                                      -- clear a session a dead client left open\n");
    plat_done();
    return rc;
}
