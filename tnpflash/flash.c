/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
#include <stdlib.h>
/* tnpflash/flash.c -- see flash.h. */
#include <stdio.h>
#include <string.h>
#include "flash.h"

volatile int flash_cancel = 0;
unsigned flash_read_depth = 4;      /* batches in flight; the device answers only so many outstanding requests */

const char *g1_board_name(uint32_t c)
{
    switch (c) {
    case 0x54447020u: return "AttoP1"; case 0x54435032u: return "AttoP2"; case 0x00020001u: return "AttoP2";
    case 0x00040001u: return "P4"; case 0x00040100u: return "P5"; case 0x00040200u: return "P6"; case 0x00040400u: return "P7";
    case 0x00040401u: return "P8"; case 0x00040402u: return "P9"; case 0x00050000u: return "RevC"; case 0x00050001u: return "RevD";
    case 0xFFFFFFFFu: return "unprogrammed";
    default: return NULL;
    }
}

void board_identify(uint32_t cfg0, uint32_t cfg1, board_t *b)
{
    const char *g1 = g1_board_name(cfg0);
    b->series = (cfg0 >> 26) & 0x3Fu;
    if (g1) { b->name = g1; b->series = 0; b->bytes = FLASH_BYTES; return; }
    if (b->series == 2u) {
        unsigned t = (cfg1 >> 23) & 7u;
        if (t == 1u) { b->name = "G2 rev C (LX100, M25P64)"; b->bytes = 0x800000u; return; }
        if (t == 0u) { b->name = "G2 rev A/B (LX150, M25P128)"; b->bytes = 0x1000000u; return; }
    }
    b->name = "UNKNOWN"; b->bytes = 0;
}

int flash_read(tnp_stream_t *c, uint32_t w0, uint32_t nwords, uint8_t *buf, unsigned batch, progress_fn pf)
{
/* ONE ROUND TRIP PER BATCH IS THE WHOLE COST.  The device answers a read with a packet per word, so a batch of 16 words
 * spent a full round trip on 64 bytes -- about 3 KB/s over a 21 ms link, whatever the link's capacity.  So several
 * batches are put in flight before any replies are collected, and each batch is as large as a packet will hold. */

    struct { unsigned tids[64]; unsigned cnt; uint32_t w; } fly[16];
    unsigned depth = flash_read_depth < 1u ? 1u : (flash_read_depth > 16u ? 16u : flash_read_depth);

    /* THE DEVICE'S REPLY FIFO IS THE CEILING, AND IT IS SILENT WHEN IT OVERFLOWS.  Measured on a G1 RevC unit:
     * 320 words outstanding always completes, 352 loses replies -- sometimes on the second batch, sometimes 700 words in,
     * which is the worst way for it to fail.  A ReadReturn is 12 bytes on the wire, so 341 of them are one 4 KB FIFO, and
     * the numbers say that is exactly what it is.  OUTSTANDING_MAX keeps a quarter of it in hand, because a read that
     * fails at word 700000 of a backup costs far more than the few KB/s the last batch would have bought. */
#define OUTSTANDING_MAX 256u
    while (depth > 1u && (uint32_t)depth * (uint32_t)batch > OUTSTANDING_MAX) depth--;
    unsigned head = 0, tail = 0, inflight = 0;
    uint32_t w = w0, end = w0 + nwords, last_pf = 0;
    int rc;
    if (!batch) batch = 1u;
    if (batch > 64u) batch = 64u;
    if (c->win_size < depth + 2u) tnp_set_window(c, depth + 2u);

    while (w < end || inflight) {
        while (w < end && inflight < depth) {
            uint8_t p[TNP_MTU], m[16]; size_t n = 0;
            unsigned cnt = (unsigned)(end - w < batch ? end - w : batch), k;
            if (flash_cancel) goto cancelled;
            for (k = 0; k < cnt; k++) {
                uint32_t a = 0x80000000u | (w + k);
                size_t n2 = tnp_ep_put(p, n, sizeof p, 0u, m, ep0_build(m, EP0_WRITE, REG_SPI_ADDR, tnp_next_tid(c), &a, 1u));
                if (!n2) break;
                fly[tail].tids[k] = tnp_next_tid(c);
                n2 = tnp_ep_put(p, n2, sizeof p, 0u, m, ep0_build(m, EP0_READ, REG_SPI_DATA, fly[tail].tids[k], NULL, 1u));
                if (!n2) break;
                n = n2;
            }
            if (!k) { sprintf(c->err, "a single read does not fit in a packet"); return -1; }
            cnt = k;
            fly[tail].cnt = cnt; fly[tail].w = w;
            if ((rc = tnp_send_ep0(c, p, n))) return rc;
            w += cnt; tail = (tail + 1u) % depth; inflight++;
        }
        if (inflight) {
            unsigned k, cnt = fly[head].cnt; uint32_t bw = fly[head].w;
            for (k = 0; k < cnt; k++) {
                ep0_msg_t e;
                if (flash_cancel) goto cancelled;   /* stop between words, not only between batches */
                if ((rc = tnp_wait_reply(c, fly[head].tids[k], &e, 5000u))) {
                    snprintf(c->err + strlen(c->err), sizeof c->err - strlen(c->err), " (word 0x%lX)", (unsigned long)(bw + k));
                    return rc;
                }
                { size_t o = (size_t)(bw + k - w0) * 4u;
                  buf[o] = (uint8_t)(e.w[0] >> 24); buf[o+1] = (uint8_t)(e.w[0] >> 16);
                  buf[o+2] = (uint8_t)(e.w[0] >> 8); buf[o+3] = (uint8_t)e.w[0]; }
            }
            head = (head + 1u) % depth; inflight--;
            if (pf && (bw + cnt - last_pf >= 0x800u || bw + cnt == end)) { last_pf = bw + cnt; pf(bw + cnt - w0, nwords, "read"); }
        }
    }
    return 0;

cancelled:
    /* A CANCEL MUST HAND BACK A USABLE SESSION.  The batches already sent are still on their way; left unclaimed they sit in
     * the receive ring and crowd out the next operation's replies, so the user who cancels and immediately retries gets a
     * mysterious timeout.  They are collected and thrown away here.  The whole drain is on one deadline: if the device has
     * genuinely stopped answering, Cancel still returns at once instead of waiting out a timeout per outstanding word. */
    {
        uint32_t deadline = plat_ms() + 500u;
        while (inflight) {
            unsigned k; ep0_msg_t e;
            for (k = 0; k < fly[head].cnt && (int32_t)(plat_ms() - deadline) < 0; k++)
                (void)tnp_wait_reply(c, fly[head].tids[k], &e, 50u);
            head = (head + 1u) % depth; inflight--;
        }
    }
    sprintf(c->err, "cancelled");
    return -99;
}


static int latch(tnp_stream_t *c, uint32_t w) { uint32_t a = 0x80000000u | w; return reg_write(c, 0, REG_SPI_ADDR, a); }

static int erase_sector(tnp_stream_t *c, uint32_t sector, uint32_t limit, char *err, size_t en)
{
    uint32_t w = sector * SECTOR_WORDS, v = 0, t0, k; int rc;
    if (w + SECTOR_WORDS > limit) { snprintf(err, en, "sector %lu is at or above the protected area", (unsigned long)sector); return -9; }
    if ((rc = latch(c, w)) || (rc = reg_write(c, 0, REG_SPI_ERASE, 0u))) { snprintf(err, en, "erase command for sector %lu: %s", (unsigned long)sector, c->err); return rc; }
    plat_sleep_ms(3000u);                                       /* what Pano's updater waits; then we LOOK instead of assuming */
    t0 = plat_ms();
    for (;;) {
        int allff = 1;
        for (k = 0; k < 4u && allff; k++) {                      /* four words spread over the sector */
            if ((rc = latch(c, w + k * (SECTOR_WORDS / 4u))) || (rc = reg_read(c, 0, REG_SPI_DATA, &v))) { snprintf(err, en, "read after erase, sector %lu: %s", (unsigned long)sector, c->err); return rc; }
            if (v != 0xFFFFFFFFu) allff = 0;
        }
        if (allff) return 0;
        if ((uint32_t)(plat_ms() - t0) > 6000u) { snprintf(err, en, "sector %lu did not erase (word reads back 0x%08lX)", (unsigned long)sector, (unsigned long)v); return -8; }
        plat_sleep_ms(200u);
    }
}

int flash_write_regions(tnp_stream_t *c, const region_t *r, unsigned nr, uint32_t limit, progress_fn pf, char *err, size_t en)
{
    unsigned i, s, nsect = 0; uint8_t sect[256]; uint32_t total = 0, done = 0; int rc;
    memset(sect, 0, sizeof sect);
    for (i = 0; i < nr; i++) {
        uint32_t w0 = r[i].byte_off / 4u, w1 = (uint32_t)((r[i].byte_off + r[i].len + 3u) / 4u);
        if (r[i].byte_off & 3u) { snprintf(err, en, "%s: offset 0x%lX is not word aligned", r[i].path, (unsigned long)r[i].byte_off); return -9; }
        if (w1 > limit || w1 > 256u * SECTOR_WORDS) { snprintf(err, en, "%s would reach word 0x%lX: at or above the protected area (byte 0x%lX)", r[i].path, (unsigned long)w1, (unsigned long)limit * 4u); return -9; }
        for (s = w0 / SECTOR_WORDS; s < (w1 + SECTOR_WORDS - 1u) / SECTOR_WORDS; s++) sect[s] = 1;
        total += (uint32_t)((r[i].len + 3u) / 4u);
    }
    for (s = 0; s < 256u; s++) if (sect[s]) nsect++;
    /* 0xFC = 1 BEFORE the erases, as Pano's updater does (flashlatestfpga: the 0xFC write, then the erase loop, then the words) */
    if ((rc = reg_write(c, 0, REG_PROG_EN, 1u))) { snprintf(err, en, "0xFC = 1: %s", c->err); return rc; }
    for (s = 0; s < 256u; s++) if (sect[s]) {
        if (pf) pf(0, nsect, "erase");
        if ((rc = erase_sector(c, s, limit, err, en))) { reg_write(c, 0, REG_PROG_EN, 0u); return rc; }
    }
    for (i = 0; i < nr; i++) {
        uint32_t w0 = r[i].byte_off / 4u, nw = (uint32_t)((r[i].len + 3u) / 4u), k;
        for (k = 0; k < nw; k++) {
            uint8_t p[64], m[16], b[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; size_t n = 0;
            if (flash_cancel) { snprintf(err, en, "cancelled"); reg_write(c, 0, REG_PROG_EN, 0u); return -99; } uint32_t a = 0x80000000u | (w0 + k), v; unsigned j;
            for (j = 0; j < 4u && 4u * k + j < r[i].len; j++) b[j] = r[i].data[4u * k + j];
            v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
            n = tnp_ep_put(p, n, sizeof p, 0u, m, ep0_build(m, EP0_WRITE, REG_SPI_ADDR, tnp_next_tid(c), &a, 1u));
            n = tnp_ep_put(p, n, sizeof p, 0u, m, ep0_build(m, EP0_WRITE, REG_SPI_DATA, tnp_next_tid(c), &v, 1u));
            if ((rc = tnp_send_ep0(c, p, n))) { snprintf(err, en, "programming word 0x%lX of %s: %s", (unsigned long)(w0 + k), r[i].path, c->err); reg_write(c, 0, REG_PROG_EN, 0u); return rc; }
            if ((k & 63u) == 63u) plat_sleep_ms(10u);           /* Pano's pacing */
            done++;
            if (pf && ((done & 0x3FFu) == 0u || done == total)) pf(done, total, "program");
        }
    }
    if ((rc = reg_write(c, 0, REG_PROG_EN, 0u))) { snprintf(err, en, "0xFC = 0: %s", c->err); return rc; }
    { long d = flash_verify_regions(c, r, nr, pf, err, en); if (d < 0) return (int)d; if (d > 0) { snprintf(err, en, "VERIFY FAILED: %ld word(s) read back differently", d); return -7; } }
    return 0;
}

long flash_verify_regions(tnp_stream_t *c, const region_t *r, unsigned nr, progress_fn pf, char *err, size_t en)
{
    unsigned i; long bad = 0;
    for (i = 0; i < nr; i++) {
        uint32_t w0 = r[i].byte_off / 4u, nw = (uint32_t)((r[i].len + 3u) / 4u), k; int rc;
        uint8_t *buf = (uint8_t *)malloc((size_t)nw * 4u);          /* a G2 image region is 3.3 MB */
        if (!buf) { snprintf(err, en, "out of memory for %s", r[i].path); return -1; }
        if ((rc = flash_read(c, w0, nw, buf, 64u, pf))) { snprintf(err, en, "reading back %s: %s", r[i].path, c->err); free(buf); return rc; }
        for (k = 0; k < nw; k++) {
            uint8_t b[4] = { 0xFF, 0xFF, 0xFF, 0xFF }; unsigned j;
            for (j = 0; j < 4u && 4u * k + j < r[i].len; j++) b[j] = r[i].data[4u * k + j];
            if (memcmp(b, buf + 4u * k, 4)) { if (bad < 4) fprintf(stderr, "  mismatch at byte 0x%lX: flash %02x%02x%02x%02x file %02x%02x%02x%02x\n", (unsigned long)(r[i].byte_off + 4u * k), buf[4*k], buf[4*k+1], buf[4*k+2], buf[4*k+3], b[0], b[1], b[2], b[3]); bad++; }
        }
        free(buf);
    }
    return bad;
}
