/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* spi.c -- see spi.h.  Each function is ftdiflash's of the same name (amiga/third_party/ftdiflash/ftdiflash.c) with
 * libftdi's send_byte/send_spi/xfer_spi replaced by one MPSSE command string per call, sent with mpsse_cmd: set_gpio
 * (0x80 value, direction 0x93), 0x11 (bytes out, MSB first, on the falling edge) and 0x31 (out and in).  Changes:
 * flash_read takes the data with 0x20 (bytes in only) instead of shifting out zeros with 0x31; flash_wait polls with
 * a time limit; addresses may be 4 bytes (flash_addr4); flash_erase_block takes the opcode of the part's eraser;
 * flash_read_rems, flash_read_res, flash_status and flash_unprotect are added; the bus is set by spi_config (SCK
 * level between transfers, sampling edge, bit order, chip select pin and level, CRESET) instead of ftdiflash's fixed
 * SPI mode 3 with chip select on ADBUS4.  The default is mode 0, which SPI NOR parts support at every clock of the
 * Clock list (README.md section 6 has the measurements).
 *
 * MPSSE opcodes (AN_108): bit 0 bytes out on the falling edge, bit 2 bytes in on the falling edge, bit 3 LSB first,
 * bit 4 bytes out, bit 5 bytes in.  MOSI changes on the edge before the sampling edge: in modes 0 and 3 out on the
 * falling and in on the rising edge (0x11, 0x20, 0x31), in modes 1 and 2 out on the rising and in on the falling
 * edge (0x10, 0x24, 0x34).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga; ftdiflash: Clifford Wolf, Dean Miller (ISC). */
#include <string.h>
#include <proto/dos.h>
#include "link.h"
#include "spi.h"

static unsigned char cb[4200];
static int cn, a4;
static spi_cfg_t cfg = { 0, 0, 0, 4, 0, 0 };
static unsigned char op_wr = 0x11, op_rd = 0x20, op_wrrd = 0x31;

void spi_config(const spi_cfg_t *c)
{
    int fall = (c->cpol != 0) == (c->cpha != 0);    /* modes 0 and 3: out on the falling edge */
    cfg = *c;
    if (cfg.cs_pin < 3 || cfg.cs_pin > 7) cfg.cs_pin = 4;
    op_wr = (unsigned char)(0x10 | (fall ? 0x01 : 0) | (cfg.lsb ? 0x08 : 0));
    op_rd = (unsigned char)(0x20 | (fall ? 0 : 0x04) | (cfg.lsb ? 0x08 : 0));
    op_wrrd = (unsigned char)(op_wr | op_rd);
}
void spi_config_get(spi_cfg_t *c) { *c = cfg; }
void spi_opcodes(unsigned char *wr, unsigned char *rd, unsigned char *wrrd) { *wr = op_wr; *rd = op_rd; *wrrd = op_wrrd; }

/* ADBUS value and direction: SCK at its idle level, MOSI low, chip select (de)asserted, CRESET as set */
static void set_gpio(int slavesel_b, int creset_b)
{
    unsigned char cs = (unsigned char)(1u << cfg.cs_pin), gpio = (unsigned char)(cfg.cpol ? 0x01 : 0), dir = (unsigned char)(0x03 | cs);
    if (slavesel_b != cfg.cs_high) gpio |= cs;
    if (cfg.cs_pin != 7 && cfg.creset != 2) {
        dir |= 0x80;
        if (cfg.creset == 1 || (cfg.creset == 0 && creset_b)) gpio |= 0x80;
    }
    cb[cn++] = 0x80; cb[cn++] = gpio; cb[cn++] = dir;
}
static void send_spi(const unsigned char *data, int n)
{
    if (n < 1) return;
    cb[cn++] = op_wr; cb[cn++] = (unsigned char)(n - 1); cb[cn++] = (unsigned char)((n - 1) >> 8);
    memcpy(cb + cn, data, (size_t)n); cn += n;
}
static void xfer_spi(const unsigned char *data, int n)
{
    if (n < 1) return;
    cb[cn++] = op_wrrd; cb[cn++] = (unsigned char)(n - 1); cb[cn++] = (unsigned char)((n - 1) >> 8);
    memcpy(cb + cn, data, (size_t)n); cn += n;
}
static void recv_spi(int n)
{
    cb[cn++] = op_rd; cb[cn++] = (unsigned char)(n - 1); cb[cn++] = (unsigned char)((n - 1) >> 8);
}
static int go(unsigned char *ans, int na) { int r = mpsse_cmd(&lk, cb, cn, ans, na); cn = 0; return r; }
static int addr_bytes(unsigned char *c, unsigned long addr)
{
    int k = 0;
    if (a4) c[k++] = (unsigned char)(addr >> 24);
    c[k++] = (unsigned char)(addr >> 16); c[k++] = (unsigned char)(addr >> 8); c[k++] = (unsigned char)addr;
    return k;
}

void flash_addr4(int on) { a4 = on; }

int flash_read_id(unsigned char *id, int n)
{
    unsigned char data[21] = { 0x9F }, ans[21];
    if (n > 20) n = 20;
    set_gpio(0, 0); xfer_spi(data, 21); set_gpio(1, 0);
    if (go(ans, 21)) return -1;
    memcpy(id, ans + 1, (size_t)n);
    return 0;
}

int flash_read_rems(unsigned char *mfg, unsigned char *dev)
{
    unsigned char data[6] = { 0x90, 0, 0, 0, 0, 0 }, ans[6];
    set_gpio(0, 0); xfer_spi(data, 6); set_gpio(1, 0);
    if (go(ans, 6)) return -1;
    *mfg = ans[4]; *dev = ans[5];
    return 0;
}

int flash_read_res(unsigned char *sig)
{
    unsigned char data[5] = { 0xAB, 0, 0, 0, 0 }, ans[5];
    set_gpio(0, 0); xfer_spi(data, 5); set_gpio(1, 0);
    if (go(ans, 5)) return -1;
    *sig = ans[4];
    return 0;
}

int flash_power_up(void)
{
    unsigned char data[1] = { 0xAB }, ans[1];
    set_gpio(0, 0); xfer_spi(data, 1); set_gpio(1, 0);
    return go(ans, 1);
}

int flash_power_down(void)
{
    unsigned char data[1] = { 0xB9 }, ans[1];
    set_gpio(0, 0); xfer_spi(data, 1); set_gpio(1, 0);
    return go(ans, 1);
}

int flash_write_enable(void)
{
    unsigned char data[1] = { 0x06 }, ans[1];
    set_gpio(0, 0); xfer_spi(data, 1); set_gpio(1, 0);
    return go(ans, 1);
}

int flash_bulk_erase(void)
{
    unsigned char data[1] = { 0xC7 }, ans[1];
    set_gpio(0, 0); xfer_spi(data, 1); set_gpio(1, 0);
    return go(ans, 1);
}

int flash_erase_block(int op, unsigned long addr)
{
    unsigned char command[5];
    command[0] = (unsigned char)op;
    set_gpio(0, 0); send_spi(command, 1 + addr_bytes(command + 1, addr)); set_gpio(1, 0);
    return go(NULL, 0);
}

int flash_prog(unsigned long addr, const unsigned char *data, int n)
{
    unsigned char command[5] = { 0x02 };
    set_gpio(0, 0); send_spi(command, 1 + addr_bytes(command + 1, addr)); send_spi(data, n); set_gpio(1, 0);
    return go(NULL, 0);
}

int flash_read(unsigned long addr, unsigned char *data, int n)
{
    unsigned char command[5] = { 0x03 };
    if (n < 1 || n > 4096) return -1;
    set_gpio(0, 0); send_spi(command, 1 + addr_bytes(command + 1, addr)); recv_spi(n); set_gpio(1, 0);
    return go(data, n);
}

int flash_status(unsigned char *sr)
{
    unsigned char data[2] = { 0x05 }, ans[2];
    set_gpio(0, 0); xfer_spi(data, 2); set_gpio(1, 0);
    if (go(ans, 2)) return -1;
    *sr = ans[1];
    return 0;
}

int flash_wait(unsigned long timeout_ms)
{
    unsigned long t;
    unsigned char sr;
    for (t = 0; ; t += 20) {
        if (flash_status(&sr)) return -1;
        if ((sr & 0x01) == 0) return 0;
        if (t >= timeout_ms) return -1;
        Delay(1);
    }
}

int flash_unprotect(void)
{
    unsigned char ewsr[1] = { 0x50 }, wrsr[2] = { 0x01, 0x00 };
    if (flash_write_enable()) return -1;
    set_gpio(0, 0); send_spi(ewsr, 1); set_gpio(1, 0);      /* SST parts: enable write status register */
    set_gpio(0, 0); send_spi(wrsr, 2); set_gpio(1, 0);
    if (go(NULL, 0)) return -1;
    return flash_wait(1000);
}

int flash_enter_4byte(void)
{
    unsigned char data[1] = { 0xB7 };
    if (flash_write_enable()) return -1;
    set_gpio(0, 0); send_spi(data, 1); set_gpio(1, 0);
    return go(NULL, 0);
}
