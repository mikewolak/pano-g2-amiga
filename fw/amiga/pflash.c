/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/pflash.c -- see pflash.h.  The flash procedures are fw/spiflash.c's (RDID, RDSR, READ, SE, PP and the map of
 * the part RDID names); this file only orders them into steps and checks every write three ways: the address against
 * spif_write_allowed() (main slot and free region), the fabric's write guard (rtl/g2/g2_spi_guard.v: its refusal
 * count must not rise, and with no guard no write is made) and the bytes read back.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../g2.h"
#include "../g2_regs.h"
#include "../spiflash.h"
#include "../crc32.h"
#include "../timer_ms.h"
#include "pflash.h"

#define STEP_READ   1024u    /* bytes read a step: about 1 ms at 11.1 MHz SCK */
#define ERASE_MS    8000u    /* SE: M25P64 3 s maximum (64 KB), M25P128 256 KB sectors: a bound above both */

/* phases of a request */
enum { PH_IDLE, PH_READ, PH_CRC, PH_CHECK, PH_ERASE, PH_ERASE_WAIT, PH_PROGRAM, PH_VERIFY };

uint8_t pfl_data[PFL_WIN];
static uint32_t s_seq, s_state, s_result, s_done, s_value, s_erased_all, s_erased, s_bad, s_first;
static uint32_t s_op, s_addr, s_len, s_pos, s_end, s_crc, s_t0, s_guard0;
static unsigned int s_phase, s_same;
static char s_msg[416];

static void be32_put(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int finish(uint32_t r, const char *what)
{
    s_result = r; s_state = 2u; s_phase = PH_IDLE;
    snprintf(s_msg, sizeof s_msg, "flash window: %s", what);
    return 1;
}
static int guard_moved(void) { return g2_guard_refusals() != s_guard0; }

void pfl_request(const uint8_t *blk)
{
    uint32_t seq = be32(blk + 4);
    if (memcmp(blk, "PFW1", 4) || (seq == s_seq && s_state != 0u)) return;
    if (s_state == 1u) return;                       /* one request at a time: the running one is finished first */
    s_seq = seq; s_op = be32(blk + 8); s_addr = be32(blk + 12); s_len = be32(blk + 16); s_crc = be32(blk + 20);
    s_state = 1u; s_result = PFL_R_OK; s_done = 0u; s_value = 0u; s_erased = 0u; s_bad = 0u; s_first = 0u;
    s_phase = PH_IDLE; s_msg[0] = 0;
}

void pfl_status(uint8_t *blk)
{
    const spif_map_t *m = spif_map();
    memset(blk, 0, 512u);
    memcpy(blk, "PFA1", 4);
    be32_put(blk + 4, s_seq); be32_put(blk + 8, s_state); be32_put(blk + 12, s_result); be32_put(blk + 16, s_done);
    be32_put(blk + 20, s_value);
    be32_put(blk + 24, m->rdid ? m->rdid : spif_rdid()); be32_put(blk + 28, s_state == 1u ? 0xFFFFFFFFu : spif_rdsr());
    be32_put(blk + 32, m->size); be32_put(blk + 36, m->sector); be32_put(blk + 40, m->main); be32_put(blk + 44, m->main_end);
    be32_put(blk + 48, m->free); be32_put(blk + 52, m->free_end); be32_put(blk + 56, m->config);
    be32_put(blk + 60, g2_spi_present ? 1u : 0u); be32_put(blk + 64, g2_guard_refusals());
    be32_put(blk + 68, g2_spi_present ? (G2_SPI_PASSED & 0xFFFFu) : 0u);
    be32_put(blk + 72, s_erased_all); be32_put(blk + 76, s_erased); be32_put(blk + 80, s_bad); be32_put(blk + 84, s_first);
    be32_put(blk + 88, PFL_WIN); be32_put(blk + 92, m->golden);
    memcpy(blk + 96, s_msg, sizeof s_msg - 1u);
}

const char *pfl_message(void) { return s_msg; }

/* the checks every write passes before its first command */
static int write_checks(uint32_t addr, uint32_t len)
{
    const spif_map_t *m = spif_map();
    char t[160];
    if (!m->size) return finish(PFL_R_NOPART, "the flash part is not known (RDID): no write is made");
    if (!g2_spi_present) return finish(PFL_R_NOGUARD, "this fabric has no SPI write guard: no write is made");
    if (!len || !spif_write_allowed(addr, len)) {
        snprintf(t, sizeof t, "0x%06lX-0x%06lX is outside %s: refused", (unsigned long)addr,
                 (unsigned long)(addr + len - 1u), m->writable_s);
        return finish(PFL_R_PROTECTED, t);
    }
    if (spif_rdsr() & 0x1Cu) return finish(PFL_R_BP, "block-protect bits set in the status register: refused");
    return 0;
}

static int start(void)
{
    const spif_map_t *m = spif_map();
    char t[160];
    s_guard0 = g2_guard_refusals();
    switch (s_op) {
    case PFL_OP_INFO:                                /* not reported to the log (pfl_service returns 0) */
        snprintf(t, sizeof t, "%s, RDID 0x%06lX, guard %s", m->part_s, (unsigned long)spif_rdid(),
                 g2_spi_present ? "present" : "absent");
        (void)finish(PFL_R_OK, t);
        return 0;
    case PFL_OP_READ:
        if (!m->size || !s_len || s_len > PFL_WIN || s_addr >= m->size || s_len > m->size - s_addr)
            return finish(PFL_R_BADREQ, "read: bad address or length");
        s_pos = 0u; s_phase = PH_READ; return 0;
    case PFL_OP_CRC:
        if (!m->size || !s_len || s_addr >= m->size || s_len > m->size - s_addr)
            return finish(PFL_R_BADREQ, "crc: bad address or length");
        s_pos = 0u; s_value = ~s_crc; s_phase = PH_CRC; return 0;   /* continued from the CRC32 of the bytes before */
    case PFL_OP_ERASE:
        if (!m->size || (s_addr | s_len) & (m->sector - 1u)) return finish(PFL_R_BADREQ, "erase: address and length must be on sector boundaries");
        if (write_checks(s_addr, s_len)) return 1;
        s_pos = s_addr; s_end = s_addr + s_len; s_phase = PH_ERASE; return 0;
    case PFL_OP_WRITE:
    case PFL_OP_PROGRAM:
        if (!m->size || !s_len || s_len > PFL_WIN || (s_op == PFL_OP_WRITE && (s_addr & (m->sector - 1u))))
            return finish(PFL_R_BADREQ, s_op == PFL_OP_WRITE ? "write: the address must be on a sector boundary, the length at most the window"
                                                             : "program: the length must be at most the window");
        if (crc32_run(0xFFFFFFFFu, pfl_data, s_len) != ~s_crc) return finish(PFL_R_DATACRC, "the data window's CRC32 differs from the request's: nothing written");
        if (write_checks(s_addr, s_op == PFL_OP_WRITE ? m->sector : s_len)) return 1;
        s_pos = 0u; s_same = 1u; s_end = s_op == PFL_OP_WRITE ? m->sector : s_len;
        s_phase = s_op == PFL_OP_WRITE ? PH_CHECK : PH_PROGRAM;
        return 0;
    default:
        return finish(PFL_R_BADREQ, "unknown operation");
    }
}

int pfl_service(void)
{
    const spif_map_t *m;
    uint8_t buf[STEP_READ];
    uint32_t n, i;
    char t[200];
    if (s_state != 1u) return 0;
    if (s_phase == PH_IDLE) return start();
    m = spif_map();
    switch (s_phase) {
    case PH_READ:
        n = s_len - s_pos > STEP_READ ? STEP_READ : s_len - s_pos;
        spif_read(s_addr + s_pos, pfl_data + s_pos, n);
        s_pos += n; s_done = s_pos;
        if (s_pos < s_len) return 0;
        s_value = crc32_run(0xFFFFFFFFu, pfl_data, s_len) ^ 0xFFFFFFFFu;
        snprintf(t, sizeof t, "read 0x%06lX %lu bytes, CRC32 %08lX", (unsigned long)s_addr, (unsigned long)s_len, (unsigned long)s_value);
        return finish(PFL_R_OK, t);
    case PH_CRC:
        n = s_len - s_pos > STEP_READ ? STEP_READ : s_len - s_pos;
        spif_read(s_addr + s_pos, buf, n);
        s_value = crc32_run(s_value, buf, n);
        s_pos += n; s_done = s_pos;
        if (s_pos < s_len) return 0;
        s_value ^= 0xFFFFFFFFu;
        snprintf(t, sizeof t, "crc 0x%06lX %lu bytes, CRC32 %08lX", (unsigned long)s_addr, (unsigned long)s_len, (unsigned long)s_value);
        return finish(PFL_R_OK, t);
    case PH_CHECK:                                   /* WRITE: the sector as it is against the window, then erased bytes */
        n = s_end - s_pos > STEP_READ ? STEP_READ : s_end - s_pos;
        spif_read(s_addr + s_pos, buf, n);
        for (i = 0; i < n && s_same; i++)
            if (buf[i] != (s_pos + i < s_len ? pfl_data[s_pos + i] : 0xFFu)) s_same = 0u;
        s_pos += n;
        if (s_same && s_pos < s_end) return 0;
        if (s_same) {
            s_done = s_len; s_value = crc32_run(0xFFFFFFFFu, pfl_data, s_len) ^ 0xFFFFFFFFu;
            snprintf(t, sizeof t, "write 0x%06lX %lu bytes: the sector already holds them, not written", (unsigned long)s_addr, (unsigned long)s_len);
            return finish(PFL_R_OK, t);
        }
        s_pos = s_addr; s_end = s_addr + m->sector; s_phase = PH_ERASE;
        return 0;
    case PH_ERASE:
        if (spif_sector_erase_start(s_pos)) return finish(PFL_R_PROTECTED, "erase refused by the map");
        s_t0 = get_millis(); s_phase = PH_ERASE_WAIT;
        return 0;
    case PH_ERASE_WAIT:
        if (spif_wip()) {
            if (get_millis() - s_t0 > ERASE_MS) return finish(PFL_R_TIMEOUT, "erase: the flash stayed busy for 8 s");
            return 0;
        }
        if (guard_moved()) {
            snprintf(t, sizeof t, "erase of 0x%06lX refused by the write guard (last refusal 0x%05lX)", (unsigned long)s_pos,
                     (unsigned long)(G2_SPI_GUARD & 0x1FFFFu));
            return finish(PFL_R_GUARD, t);
        }
        s_erased++; s_erased_all++; s_pos += m->sector;
        if (s_op == PFL_OP_ERASE) {
            s_done = s_pos - s_addr;
            if (s_pos < s_end) { s_phase = PH_ERASE; return 0; }
            snprintf(t, sizeof t, "erased 0x%06lX-0x%06lX, %lu sectors", (unsigned long)s_addr, (unsigned long)(s_end - 1u), (unsigned long)s_erased);
            return finish(PFL_R_OK, t);
        }
        s_pos = 0u; s_phase = PH_PROGRAM;            /* WRITE: the sector is erased */
        return 0;
    case PH_PROGRAM: {                               /* one page a step; a page of erased bytes is not programmed */
        uint32_t a = s_addr + s_pos, room = SPIF_PAGE - (a & (SPIF_PAGE - 1u));
        int e;
        n = s_len - s_pos < room ? s_len - s_pos : room;
        for (i = 0; i < n && pfl_data[s_pos + i] == 0xFFu; i++) { }
        if (i < n) {
            e = spif_page_program(a, pfl_data + s_pos, n);
            if (e == -2) return finish(PFL_R_PROTECTED, "page program refused by the map");
            if (e) return finish(PFL_R_TIMEOUT, "page program: the flash stayed busy");
            if (guard_moved()) {
                snprintf(t, sizeof t, "page program at 0x%06lX refused by the write guard", (unsigned long)a);
                return finish(PFL_R_GUARD, t);
            }
        }
        s_pos += n; s_done = s_pos;
        if (s_pos < s_len) return 0;
        s_pos = 0u; s_value = 0xFFFFFFFFu; s_phase = PH_VERIFY;
        return 0;
    }
    case PH_VERIFY:
        n = s_len - s_pos > STEP_READ ? STEP_READ : s_len - s_pos;
        spif_read(s_addr + s_pos, buf, n);
        s_value = crc32_run(s_value, buf, n);
        for (i = 0; i < n; i++) if (buf[i] != pfl_data[s_pos + i]) { if (!s_bad) s_first = s_pos + i; s_bad++; }
        s_pos += n;
        if (s_pos < s_len) return 0;
        s_value ^= 0xFFFFFFFFu;
        if (s_bad) {
            snprintf(t, sizeof t, "%s 0x%06lX %lu bytes: VERIFY FAILED, %lu bytes differ, first at +0x%lX",
                     s_op == PFL_OP_WRITE ? "write" : "program", (unsigned long)s_addr, (unsigned long)s_len,
                     (unsigned long)s_bad, (unsigned long)s_first);
            return finish(PFL_R_VERIFY, t);
        }
        snprintf(t, sizeof t, "%s 0x%06lX %lu bytes, %lu sector erased, read back CRC32 %08lX",
                 s_op == PFL_OP_WRITE ? "write" : "program", (unsigned long)s_addr, (unsigned long)s_len,
                 (unsigned long)s_erased, (unsigned long)s_value);
        return finish(PFL_R_OK, t);
    default:
        return finish(PFL_R_BADREQ, "internal: unknown phase");
    }
}
