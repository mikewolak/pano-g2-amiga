/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* swdtool.c -- STM32 Serial Wire Debug from the Amiga through an FTDI FT2232H on the G2's USB hub (channel A in MPSSE
 * mode; the firmware passes the serial driver's bytes to the chip with AMIGA.INI FTDI=MPSSE).
 *
 *   swdtool [DEVICE=g2serial.device] [UNIT=n] [CLOCK=hz] [NRST] <command> [arguments]
 *
 *   ID                         DPIDR, AP IDR, CPUID, the STM32 device ID and revision, flash size, family
 *   READ FILE=f [ADDR=a] [LEN=n]       memory to a file; default the whole flash from 0x08000000
 *   WRITE FILE=f [ADDR=a] [NOERASE] [NOVERIFY] [NORUN]
 *                              erase the pages or sectors the file covers, program, verify, reset and run
 *   ERASE [ALL | ADDR=a LEN=n] mass erase, or the pages or sectors of a range
 *   RESET [HALT]               system reset (AIRCR SYSRESETREQ; with NRST the ADBUS4 line); HALT stops at the reset vector
 *   PEEK ADDR=a [LEN=n]        32-bit words, printed
 *   POKE ADDR=a VALUE=v        one 32-bit word
 *
 * Numbers: decimal, or hexadecimal with 0x.  CLOCK is SWCLK in Hz (default 1,000,000; 30 MHz / (1 + divisor)).
 * NRST: ADBUS4 is wired to the target's NRST and is used for RESET.
 *
 * Flash programming: STM32F0, F1 and F3 (flash interface at 0x40022000: halfword programming, pages of 1 or 2 KB) and
 * STM32F2, F4 and F7 (flash interface at 0x40023C00: word programming with PSIZE x32, which needs VDD 2.7-3.6 V;
 * sectors of 16/64/128 KB, or 32/128/256 KB on the F74x, F75x, F76x and F77x).  Other STM32 families are identified
 * and their memory read and written; their flash is not programmed.  The procedures are those of the reference
 * manuals RM0008 (F1, section 3.3.3 and 3.3.5, chapter 3.4), RM0091 (F0), RM0316 (F3), RM0033 (F2), RM0090 (F4,
 * section 3.6) and RM0385/RM0410/RM0431 (F7).
 *
 * Return code 0 done, 5 failed, 10 usage, 20 the stream did not open or the target did not answer.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ZIO_IMPL                    /* zio.h without its lrzsz stream macros */
#include "zio.h"
#include "xpc.h"
#include "swd.h"
#include "ptime.h"

#define FLASH_BASE 0x08000000u
#define DHCSR      0xE000EDF0u
#define DEMCR      0xE000EDFCu
#define AIRCR      0xE000ED0Cu
#define CPUID      0xE000ED00u
#define KEY1       0x45670123u
#define KEY2       0xCDEF89ABu

enum { FAM_NONE, FAM_F1, FAM_F4 };          /* flash interface: none programmed, F0/F1/F3, F2/F4/F7 */

typedef struct {
    unsigned id; const char *name; int fam;
    uint32_t size_reg;               /* flash size in KB, 16 bits; 0 not known */
    unsigned page;                   /* F0/F1/F3 page in bytes; F2/F4/F7 the first sector in KB (16 or 32) */
} part_t;

static const part_t parts[] = {
    { 0x412, "STM32F10x low-density",            FAM_F1, 0x1FFFF7E0u, 1024 },
    { 0x410, "STM32F10x medium-density",         FAM_F1, 0x1FFFF7E0u, 1024 },
    { 0x414, "STM32F10x high-density",           FAM_F1, 0x1FFFF7E0u, 2048 },
    { 0x418, "STM32F105/107 connectivity line",  FAM_F1, 0x1FFFF7E0u, 2048 },
    { 0x420, "STM32F100 value line LD/MD",       FAM_F1, 0x1FFFF7E0u, 1024 },
    { 0x428, "STM32F100 value line HD",          FAM_F1, 0x1FFFF7E0u, 2048 },
    { 0x430, "STM32F10x XL-density",             FAM_F1, 0x1FFFF7E0u, 2048 },
    { 0x444, "STM32F03x",                        FAM_F1, 0x1FFFF7CCu, 1024 },
    { 0x445, "STM32F04x",                        FAM_F1, 0x1FFFF7CCu, 1024 },
    { 0x440, "STM32F05x",                        FAM_F1, 0x1FFFF7CCu, 1024 },
    { 0x448, "STM32F07x",                        FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x442, "STM32F09x",                        FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x422, "STM32F30x/F31x",                   FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x432, "STM32F37x",                        FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x438, "STM32F303x6/8, F334",              FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x439, "STM32F301, F318",                  FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x446, "STM32F303xD/E, F398",              FAM_F1, 0x1FFFF7CCu, 2048 },
    { 0x411, "STM32F2xx",                        FAM_F4, 0x1FFF7A22u, 16 },
    { 0x413, "STM32F405/407/415/417",            FAM_F4, 0x1FFF7A22u, 16 },
    { 0x419, "STM32F42x/43x",                    FAM_F4, 0x1FFF7A22u, 16 },
    { 0x423, "STM32F401xB/C",                    FAM_F4, 0x1FFF7A22u, 16 },
    { 0x433, "STM32F401xD/E",                    FAM_F4, 0x1FFF7A22u, 16 },
    { 0x431, "STM32F411",                        FAM_F4, 0x1FFF7A22u, 16 },
    { 0x458, "STM32F410",                        FAM_F4, 0x1FFF7A22u, 16 },
    { 0x441, "STM32F412",                        FAM_F4, 0x1FFF7A22u, 16 },
    { 0x421, "STM32F446",                        FAM_F4, 0x1FFF7A22u, 16 },
    { 0x434, "STM32F469/479",                    FAM_F4, 0x1FFF7A22u, 16 },
    { 0x463, "STM32F413/423",                    FAM_F4, 0x1FFF7A22u, 16 },
    { 0x449, "STM32F74x/75x",                    FAM_F4, 0x1FF0F442u, 32 },
    { 0x451, "STM32F76x/77x",                    FAM_F4, 0x1FF0F442u, 32 },
    { 0x452, "STM32F72x/73x",                    FAM_F4, 0x1FF07A22u, 16 },
    { 0x450, "STM32H74x/75x",                    FAM_NONE, 0x1FF1E880u, 0 },
    { 0x480, "STM32H7A3/B3",                     FAM_NONE, 0, 0 },
    { 0x483, "STM32H72x/73x",                    FAM_NONE, 0, 0 },
    { 0x415, "STM32L47x/48x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x435, "STM32L43x/44x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x461, "STM32L49x/4Ax",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x462, "STM32L45x/46x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x464, "STM32L41x/42x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x466, "STM32G03x/04x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x460, "STM32G07x/08x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x468, "STM32G43x/44x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x469, "STM32G47x/48x",                    FAM_NONE, 0x1FFF75E0u, 0 },
    { 0x416, "STM32L1xx cat. 1",                 FAM_NONE, 0, 0 },
    { 0x417, "STM32L05x/06x",                    FAM_NONE, 0, 0 },
    { 0x495, "STM32WB5x",                        FAM_NONE, 0, 0 },
};

static swd_t s_swd;
static unsigned long s_hz = 1000000u;

static unsigned long num(const char *p) { return strtoul(p, NULL, 0); }

static const part_t *part_of(unsigned id)
{
    unsigned i;
    for (i = 0; i < sizeof parts / sizeof parts[0]; i++) if (parts[i].id == id) return &parts[i];
    return NULL;
}

/* a 16-bit value at any even address */
static int read16(uint32_t a, unsigned *v)
{
    uint32_t w;
    if (swd_read32(&s_swd, a & ~3u, &w)) return -1;
    *v = (a & 2u) ? (unsigned)(w >> 16) : (unsigned)(w & 0xFFFFu);
    return 0;
}

typedef struct { uint32_t cpuid, dbgmcu; unsigned devid, rev, kb; const part_t *p; } target_t;

static int identify(target_t *t, int quiet)
{
    unsigned core;
    uint32_t dbg;
    memset(t, 0, sizeof *t);
    if (swd_read32(&s_swd, CPUID, &t->cpuid)) { printf("CPUID: the read failed\n"); return -1; }
    core = (unsigned)(t->cpuid >> 4) & 0xFFFu;
    dbg = (core == 0xC20u || core == 0xC60u) ? 0x40015800u : 0xE0042000u;     /* Cortex-M0/M0+: on the APB */
    if (swd_read32(&s_swd, dbg, &t->dbgmcu) || !(t->dbgmcu & 0xFFFu)) {
        dbg = 0x5C001000u;                                                     /* STM32H7 */
        if (swd_read32(&s_swd, dbg, &t->dbgmcu)) t->dbgmcu = 0;
    }
    t->devid = (unsigned)t->dbgmcu & 0xFFFu; t->rev = (unsigned)(t->dbgmcu >> 16);
    t->p = part_of(t->devid);
    if (t->p && t->p->size_reg && read16(t->p->size_reg, &t->kb)) t->kb = 0;
    if (!quiet) {
        static const struct { unsigned n; const char *s; } cores[] = {
            { 0xC20, "Cortex-M0" }, { 0xC60, "Cortex-M0+" }, { 0xC23, "Cortex-M3" }, { 0xC24, "Cortex-M4" },
            { 0xC27, "Cortex-M7" }, { 0xD20, "Cortex-M23" }, { 0xD21, "Cortex-M33" } };
        const char *cn = "unknown core"; unsigned i;
        for (i = 0; i < sizeof cores / sizeof cores[0]; i++) if (cores[i].n == core) cn = cores[i].s;
        printf("CPUID 0x%08lX  %s r%lup%lu\n", (unsigned long)t->cpuid, cn, (unsigned long)(t->cpuid >> 20) & 15ul,
               (unsigned long)t->cpuid & 15ul);
        printf("DBGMCU_IDCODE 0x%08lX (at 0x%08lX)  device 0x%03X  revision 0x%04X\n", (unsigned long)t->dbgmcu,
               (unsigned long)dbg, t->devid, t->rev);
        if (t->p) printf("part %s, flash %u KB%s\n", t->p->name, t->kb,
                         t->p->fam == FAM_NONE ? " (identified; flash programming not supported for this family)" : "");
        else printf("part: device 0x%03X is not in the table\n", t->devid);
    }
    return 0;
}

static int halt(void)
{
    uint32_t v = 0;
    int i;
    if (swd_write32(&s_swd, DHCSR, 0xA05F0003u)) return -1;                   /* DBGKEY, C_HALT, C_DEBUGEN */
    for (i = 0; i < 10; i++) { if (swd_read32(&s_swd, DHCSR, &v) == 0 && (v & 0x20000u)) return 0; }  /* S_HALT */
    return -1;
}

static int sys_reset(int stop, int nrst)
{
    uint32_t v = 0;
    if (swd_write32(&s_swd, DEMCR, stop ? 1u : 0u)) return -1;               /* VC_CORERESET */
    if (swd_write32(&s_swd, DHCSR, stop ? 0xA05F0001u : 0xA05F0000u)) return -1;
    if (nrst) { swd_nrst(&s_swd, 1); pt_sleep_ms(20); swd_nrst(&s_swd, 0); }
    else (void)swd_write32(&s_swd, AIRCR, 0x05FA0004u);                         /* SYSRESETREQ; the answer may be lost */
    pt_sleep_ms(50);
    if (swd_connect(&s_swd, &v)) return -2;
    if (stop) { if (swd_read32(&s_swd, DHCSR, &v) || !(v & 0x20000u)) return -3; }
    return 0;
}

/* ---- flash: F0/F1/F3 ----------------------------------------------------------------------------------------- */
#define F1_REG(a, r) (0x40022000u + ((a) >= 0x08080000u ? 0x40u : 0u) + (r))   /* XL-density: bank 2 above 512 KB */
#define F1_KEYR 0x04u
#define F1_SR   0x0Cu
#define F1_CR   0x10u
#define F1_AR   0x14u

static int f1_wait(uint32_t a, double tmo, uint32_t *sr)
{
    double t0 = pt_now();
    for (;;) {
        if (swd_read32(&s_swd, F1_REG(a, F1_SR), sr)) return -1;
        if (!(*sr & 1u)) return (*sr & 0x14u) ? -2 : 0;                      /* BSY; PGERR, WRPRTERR */
        if (pt_now() - t0 > tmo) return -3;
    }
}
static int f1_unlock(uint32_t a)
{
    uint32_t cr = 0;
    if (swd_read32(&s_swd, F1_REG(a, F1_CR), &cr)) return -1;
    if (cr & 0x80u) {
        if (swd_write32(&s_swd, F1_REG(a, F1_KEYR), KEY1) || swd_write32(&s_swd, F1_REG(a, F1_KEYR), KEY2)) return -1;
        if (swd_read32(&s_swd, F1_REG(a, F1_CR), &cr) || (cr & 0x80u)) return -1;
    }
    return swd_write32(&s_swd, F1_REG(a, F1_SR), 0x34u);                     /* EOP, WRPRTERR, PGERR cleared */
}
static int f1_erase_page(uint32_t a)
{
    uint32_t sr;
    if (f1_unlock(a)) return -1;
    if (swd_write32(&s_swd, F1_REG(a, F1_CR), 0x02u) || swd_write32(&s_swd, F1_REG(a, F1_AR), a) ||
        swd_write32(&s_swd, F1_REG(a, F1_CR), 0x42u)) return -1;              /* PER, then PER|STRT */
    if (f1_wait(a, 2.0, &sr)) return -1;
    return swd_write32(&s_swd, F1_REG(a, F1_CR), 0u);
}
static int f1_mass_erase(int two_banks)
{
    uint32_t sr;
    int b;
    for (b = 0; b <= two_banks; b++) {
        uint32_t a = b ? 0x08080000u : FLASH_BASE;
        if (f1_unlock(a)) return -1;
        if (swd_write32(&s_swd, F1_REG(a, F1_CR), 0x04u) || swd_write32(&s_swd, F1_REG(a, F1_CR), 0x44u)) return -1;
        if (f1_wait(a, 10.0, &sr)) return -1;
        if (swd_write32(&s_swd, F1_REG(a, F1_CR), 0u)) return -1;
    }
    return 0;
}
/* halfwords at a; the bus stalls while a halfword programs (52.5-70 us, RM0008 table of characteristics in the data
 * sheets), so each write is followed by idle cycles of 75 us */
static int f1_program(uint32_t a, const uint8_t *d, uint32_t n)
{
    uint16_t h[256];
    uint32_t sr, off = 0;
    unsigned save = s_swd.idle;
    s_swd.idle = (unsigned)(s_hz * 75u / 1000000u);
    while (off < n) {
        uint32_t k = n - off, i, aa = a + off;
        int done = 0;
        if (k > 512u) k = 512u;
        if (aa < 0x08080000u && aa + k > 0x08080000u) k = 0x08080000u - aa;    /* one bank per piece */
        if (f1_unlock(aa) || swd_write32(&s_swd, F1_REG(aa, F1_CR), 0x01u)) { s_swd.idle = save; return -1; }    /* PG */
        for (i = 0; i < k; i += 2) h[i / 2] = (uint16_t)(d[off + i] | ((i + 1 < k ? d[off + i + 1] : 0xFFu) << 8));
        if (swd_write_half(&s_swd, aa, h, (int)((k + 1) / 2), &done)) {
            printf("\nprogram: halfword at 0x%08lX not accepted (raise the idle time: a lower CLOCK)\n", (unsigned long)(aa + 2u * (uint32_t)done));
            s_swd.idle = save; return -1;
        }
        if (f1_wait(aa, 1.0, &sr)) { printf("\nprogram: SR 0x%02lX after 0x%08lX\n", (unsigned long)sr, (unsigned long)aa); s_swd.idle = save; return -1; }
        if (swd_write32(&s_swd, F1_REG(aa, F1_CR), 0u)) { s_swd.idle = save; return -1; }
        off += k;
        printf("\rprogram: %lu of %lu bytes", (unsigned long)off, (unsigned long)n); fflush(stdout);
    }
    s_swd.idle = save;
    printf("\n");
    return 0;
}

/* ---- flash: F2/F4/F7 ----------------------------------------------------------------------------------------- */
#define F4_KEYR 0x40023C04u
#define F4_SR   0x40023C0Cu
#define F4_CR   0x40023C10u
#define F4_PSIZE32 0x200u

static int f4_wait(double tmo, uint32_t *sr)
{
    double t0 = pt_now();
    for (;;) {
        if (swd_read32(&s_swd, F4_SR, sr)) return -1;
        if (!(*sr & 0x10000u)) return (*sr & 0x1F2u) ? -2 : 0;               /* BSY; OPERR, WRPERR, PGAERR, PGPERR, PGSERR, RDERR */
        if (pt_now() - t0 > tmo) return -3;
    }
}
static int f4_unlock(void)
{
    uint32_t cr = 0;
    if (swd_read32(&s_swd, F4_CR, &cr)) return -1;
    if (cr & 0x80000000u) {
        if (swd_write32(&s_swd, F4_KEYR, KEY1) || swd_write32(&s_swd, F4_KEYR, KEY2)) return -1;
        if (swd_read32(&s_swd, F4_CR, &cr) || (cr & 0x80000000u)) return -1;
    }
    return swd_write32(&s_swd, F4_SR, 0x1F3u);
}
/* sector n's start and size: u, u, u, u, 4u, 8u, 8u ... per bank of 1 MB (u = 16 KB) or 2 MB (u = 32 KB); a second
 * bank's sectors are numbered from 12 and written to SNB as 16 and up */
static void f4_sector(const target_t *t, int n, uint32_t *start, uint32_t *size)
{
    uint32_t u = (uint32_t)t->p->page * 1024u, bank = u * 64u, o = 0;
    int i, k = n % 12;
    for (i = 0; i < k; i++) o += i < 4 ? u : i == 4 ? 4u * u : 8u * u;
    *size = k < 4 ? u : k == 4 ? 4u * u : 8u * u;
    *start = FLASH_BASE + (uint32_t)(n / 12) * bank + o;
}
static int f4_sector_of(const target_t *t, uint32_t a)
{
    int n;
    uint32_t st, sz;
    for (n = 0; n < 24; n++) { f4_sector(t, n, &st, &sz); if (a >= st && a < st + sz) return n; }
    return -1;
}
static int f4_erase_sector(int n)
{
    uint32_t sr, snb = (uint32_t)(n < 12 ? n : n + 4);
    if (f4_unlock()) return -1;
    if (swd_write32(&s_swd, F4_CR, F4_PSIZE32 | (snb << 3) | 0x2u) ||
        swd_write32(&s_swd, F4_CR, F4_PSIZE32 | (snb << 3) | 0x2u | 0x10000u)) return -1;    /* SER, then STRT */
    if (f4_wait(30.0, &sr)) { printf("erase: sector %d, SR 0x%08lX\n", n, (unsigned long)sr); return -1; }
    return swd_write32(&s_swd, F4_CR, 0u);
}
static int f4_mass_erase(int two_banks)
{
    uint32_t sr;
    if (f4_unlock()) return -1;
    if (swd_write32(&s_swd, F4_CR, F4_PSIZE32 | 0x4u | (two_banks ? 0x8000u : 0u)) ||
        swd_write32(&s_swd, F4_CR, F4_PSIZE32 | 0x4u | (two_banks ? 0x8000u : 0u) | 0x10000u)) return -1;   /* MER (MER1), STRT */
    if (f4_wait(60.0, &sr)) { printf("erase: SR 0x%08lX\n", (unsigned long)sr); return -1; }
    return swd_write32(&s_swd, F4_CR, 0u);
}
/* words at a (16 us each typical, RM0090 data sheets): idle cycles of 30 us after each */
static int f4_program(uint32_t a, const uint8_t *d, uint32_t n)
{
    uint32_t w[128], sr, off = 0;
    unsigned save = s_swd.idle;
    if (f4_unlock() || swd_write32(&s_swd, F4_CR, F4_PSIZE32 | 0x1u)) return -1;                       /* PG */
    s_swd.idle = (unsigned)(s_hz * 30u / 1000000u);
    while (off < n) {
        uint32_t k = n - off, i;
        if (k > 512u) k = 512u;
        for (i = 0; i < (k + 3u) / 4u; i++) {
            uint32_t j, v = 0;
            for (j = 0; j < 4u; j++) v |= (uint32_t)(4u * i + j < k ? d[off + 4u * i + j] : 0xFFu) << (8u * j);
            w[i] = v;
        }
        if (swd_write_block(&s_swd, a + off, w, (int)((k + 3u) / 4u)) || f4_wait(1.0, &sr)) {
            printf("\nprogram: failed in 0x%08lX-0x%08lX, SR 0x%08lX\n", (unsigned long)(a + off), (unsigned long)(a + off + k - 1u), (unsigned long)sr);
            s_swd.idle = save; return -1;
        }
        off += k;
        printf("\rprogram: %lu of %lu bytes", (unsigned long)off, (unsigned long)n); fflush(stdout);
    }
    s_swd.idle = save;
    printf("\n");
    return swd_write32(&s_swd, F4_CR, 0u);
}

static int lock_flash(const target_t *t)
{
    if (t->p->fam == FAM_F1) return swd_write32(&s_swd, F1_REG(FLASH_BASE, F1_CR), 0x80u);
    return swd_write32(&s_swd, F4_CR, 0x80000000u);
}

static int erase_range(const target_t *t, uint32_t a, uint32_t n)
{
    uint32_t end = a + n, p;
    if (t->p->fam == FAM_F1) {
        for (p = a & ~(uint32_t)(t->p->page - 1u); p < end; p += t->p->page) {
            printf("\rerase: page 0x%08lX", (unsigned long)p); fflush(stdout);
            if (f1_erase_page(p)) { printf("\nerase: page 0x%08lX failed\n", (unsigned long)p); return -1; }
        }
    } else {
        int s0 = f4_sector_of(t, a), s1 = f4_sector_of(t, end - 1u), k;
        if (s0 < 0 || s1 < 0) { printf("erase: range outside the sector map\n"); return -1; }
        for (k = s0; k <= s1; k++) { printf("\rerase: sector %d", k); fflush(stdout); if (f4_erase_sector(k)) return -1; }
    }
    printf("\n");
    return 0;
}

static int read_mem(uint32_t a, uint8_t *d, uint32_t n)
{
    uint32_t w[256], off = 0;
    while (off < n) {
        uint32_t k = n - off, i;
        if (k > 1024u) k = 1024u;
        if (swd_read_block(&s_swd, a + off, w, (int)((k + 3u) / 4u))) {
            printf("\nread: bus fault at 0x%08lX-0x%08lX (no memory there, or flash read protection level 1)\n",
                   (unsigned long)(a + off), (unsigned long)(a + off + k - 1u));
            return -1;
        }
        for (i = 0; i < k; i++) d[off + i] = (uint8_t)(w[i / 4u] >> (8u * (i & 3u)));
        off += k;
        if (n > 4096u) { printf("\rread: %lu of %lu bytes", (unsigned long)off, (unsigned long)n); fflush(stdout); }
    }
    if (n > 4096u) printf("\n");
    return 0;
}

static uint8_t *load_file(const char *f, uint32_t *n)
{
    FILE *fp = fopen(f, "rb");
    uint8_t *d;
    long sz;
    if (!fp) { printf("%s: cannot open\n", f); return NULL; }
    fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || !(d = malloc((size_t)sz))) { printf("%s: empty or no memory\n", f); fclose(fp); return NULL; }
    if (fread(d, 1, (size_t)sz, fp) != (size_t)sz) { printf("%s: read error\n", f); free(d); fclose(fp); return NULL; }
    fclose(fp);
    *n = (uint32_t)sz;
    return d;
}

int main(int argc, char **argv)
{
    zio_cfg_t c;
    xpc_t x;
    target_t t;
    uint32_t dpidr = 0, apidr = 0, addr = FLASH_BASE, len = 0, value = 0;
    const char *cmd = NULL, *file = NULL;
    int all = 0, haltf = 0, noerase = 0, noverify = 0, norun = 0, nrst = 0, have_addr = 0, have_len = 0, have_val = 0;
    int i, rc = 0;
    double t0;
    zio_args(&argc, &argv, &c);
    if (!strcmp(c.device, "serial.device")) c.device = "g2serial.device";
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strncmp(a, "CLOCK=", 6)) s_hz = num(a + 6);
        else if (!strncmp(a, "ADDR=", 5)) { addr = (uint32_t)num(a + 5); have_addr = 1; }
        else if (!strncmp(a, "LEN=", 4)) { len = (uint32_t)num(a + 4); have_len = 1; }
        else if (!strncmp(a, "VALUE=", 6)) { value = (uint32_t)num(a + 6); have_val = 1; }
        else if (!strncmp(a, "FILE=", 5)) file = a + 5;
        else if (!strcmp(a, "ALL")) all = 1;
        else if (!strcmp(a, "HALT")) haltf = 1;
        else if (!strcmp(a, "NOERASE")) noerase = 1;
        else if (!strcmp(a, "NOVERIFY")) noverify = 1;
        else if (!strcmp(a, "NORUN")) norun = 1;
        else if (!strcmp(a, "NRST")) nrst = 1;
        else if (!strcmp(a, "ID") || !strcmp(a, "READ") || !strcmp(a, "WRITE") || !strcmp(a, "ERASE") ||
                 !strcmp(a, "RESET") || !strcmp(a, "PEEK") || !strcmp(a, "POKE")) cmd = a;
        else { cmd = NULL; break; }
    }
    if (!cmd || ((!strcmp(cmd, "READ") || !strcmp(cmd, "WRITE")) && !file) || (!strcmp(cmd, "POKE") && (!have_addr || !have_val)) ||
        (!strcmp(cmd, "PEEK") && !have_addr) || (!strcmp(cmd, "ERASE") && !all && !(have_addr && have_len))) {
        printf("usage: swdtool [DEVICE=g2serial.device] [UNIT=n] [CLOCK=hz] [NRST] ID\n"
               "       swdtool ... READ FILE=f [ADDR=a] [LEN=n]\n"
               "       swdtool ... WRITE FILE=f [ADDR=a] [NOERASE] [NOVERIFY] [NORUN]\n"
               "       swdtool ... ERASE ALL | ERASE ADDR=a LEN=n\n"
               "       swdtool ... RESET [HALT]\n"
               "       swdtool ... PEEK ADDR=a [LEN=n] | POKE ADDR=a VALUE=v\n");
        return 10;
    }
    if (zio_open(&c)) return 20;
    if (mpsse_open(&x, s_hz)) {
        printf("MPSSE: no answer to the sync command (%d): is the FT2232H on the hub, AMIGA.INI FTDI=MPSSE, %s the serial "
               "FIFO board's driver?\n", x.err, c.device);
        zio_close();
        return 20;
    }
    s_hz = x.tck_hz;
    if (swd_open(&s_swd, &x) || swd_connect(&s_swd, &dpidr)) {
        printf("SWD: no answer from the target at SWCLK %lu Hz (wiring: ADBUS0 SWCLK, ADBUS1 through 470 ohm and ADBUS2 to "
               "SWDIO, GND; target powered)\n", s_hz);
        zio_close();
        return 20;
    }
    (void)swd_select(&s_swd, AP_IDR_BANK); (void)swd_ap_read(&s_swd, AP_IDR, &apidr); (void)swd_select(&s_swd, 0);
    printf("SWD: SWCLK %lu Hz, DPIDR 0x%08lX, AP IDR 0x%08lX\n", s_hz, (unsigned long)dpidr, (unsigned long)apidr);
    if (identify(&t, strcmp(cmd, "ID") != 0)) { zio_close(); return 5; }
    t0 = pt_now();

    if (!strcmp(cmd, "ID")) rc = 0;
    else if (!strcmp(cmd, "PEEK")) {
        uint32_t n = have_len ? (len + 3u) / 4u : 1u, k, w[64];
        if (n > 64u) n = 64u;
        if (swd_read_block(&s_swd, addr & ~3u, w, (int)n)) { printf("PEEK: bus fault\n"); rc = 5; }
        else for (k = 0; k < n; k++) {
            if (!(k & 3u)) printf("%s0x%08lX:", k ? "\n" : "", (unsigned long)((addr & ~3u) + 4u * k));
            printf(" %08lX", (unsigned long)w[k]);
        }
        if (!rc) printf("\n");
    } else if (!strcmp(cmd, "POKE")) {
        rc = swd_write32(&s_swd, addr & ~3u, value) ? 5 : 0;
        printf("POKE 0x%08lX = 0x%08lX: %s\n", (unsigned long)(addr & ~3u), (unsigned long)value, rc ? "bus fault" : "done");
    } else if (!strcmp(cmd, "RESET")) {
        int r = sys_reset(haltf, nrst);
        printf("RESET%s: %s\n", haltf ? " HALT" : "", r == 0 ? (haltf ? "halted at the reset vector" : "running") :
               r == -3 ? "the core did not halt" : "no answer after the reset");
        rc = r ? 5 : 0;
    } else if (!strcmp(cmd, "READ")) {
        uint8_t *d;
        FILE *fp;
        if (!have_len) len = t.kb ? t.kb * 1024u : 0u;
        if (!len) { printf("READ: the flash size is not known: give LEN=\n"); zio_close(); return 10; }
        if (!(d = malloc(len))) { printf("READ: no memory for %lu bytes\n", (unsigned long)len); zio_close(); return 5; }
        if (read_mem(addr, d, len)) rc = 5;
        else if (!(fp = fopen(file, "wb")) || fwrite(d, 1, len, fp) != len) { printf("%s: write error\n", file); rc = 5; if (fp) fclose(fp); }
        else { fclose(fp); printf("READ: 0x%08lX-0x%08lX to %s, %.1f s\n", (unsigned long)addr, (unsigned long)(addr + len - 1u), file, pt_now() - t0); }
        free(d);
    } else {                                                                  /* WRITE, ERASE */
        if (!t.p || t.p->fam == FAM_NONE) { printf("%s: flash programming is not supported for device 0x%03X\n", cmd, t.devid); zio_close(); return 5; }
        if (halt()) { printf("%s: the core did not halt\n", cmd); zio_close(); return 5; }
        if (!strcmp(cmd, "ERASE")) {
            if (all) rc = (t.p->fam == FAM_F1 ? f1_mass_erase(t.devid == 0x430 && t.kb > 512u) : f4_mass_erase(t.kb > 1024u && t.p->page == 16u)) ? 5 : 0;
            else rc = erase_range(&t, addr, len) ? 5 : 0;
            (void)lock_flash(&t);
            printf("ERASE: %s, %.1f s\n", rc ? "failed" : "done", pt_now() - t0);
        } else {
            uint32_t n = 0;
            uint8_t *d = load_file(file, &n), *b = NULL;
            if (!d) { zio_close(); return 5; }
            if (addr & (t.p->fam == FAM_F1 ? 1u : 3u)) { printf("WRITE: ADDR must be %s-aligned\n", t.p->fam == FAM_F1 ? "halfword" : "word"); free(d); zio_close(); return 10; }
            if (t.kb && (addr < FLASH_BASE || addr + n > FLASH_BASE + t.kb * 1024u)) {
                printf("WRITE: 0x%08lX-0x%08lX is outside the flash (%u KB at 0x08000000)\n", (unsigned long)addr, (unsigned long)(addr + n - 1u), t.kb);
                free(d); zio_close(); return 5;
            }
            if (!noerase && erase_range(&t, addr, n)) rc = 5;
            if (!rc && (t.p->fam == FAM_F1 ? f1_program(addr, d, n) : f4_program(addr, d, n))) rc = 5;
            (void)lock_flash(&t);
            if (!rc && !noverify) {
                if (!(b = malloc(n)) || read_mem(addr, b, n)) rc = 5;
                else {
                    uint32_t k, diff = 0, first = 0;
                    for (k = 0; k < n; k++) if (b[k] != d[k]) { if (!diff) first = k; diff++; }
                    if (diff) { printf("verify: %lu bytes differ, the first at 0x%08lX\n", (unsigned long)diff, (unsigned long)(addr + first)); rc = 5; }
                    else printf("verify: %lu bytes equal\n", (unsigned long)n);
                }
                free(b);
            }
            printf("WRITE: %s, %lu bytes, %.1f s\n", rc ? "failed" : "done", (unsigned long)n, pt_now() - t0);
            free(d);
            if (!rc && !norun) { int r = sys_reset(0, nrst); printf("reset: %s\n", r ? "no answer after the reset" : "running"); }
        }
    }
    printf("bytes written %lu, read %lu\n", x.stat_out, x.stat_in);
    zio_close();
    return rc;
}
