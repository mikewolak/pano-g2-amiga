/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/ch7301.c -- set-up of the Pano Logic G2's two Chrontel CH7301C DVI encoders.
 *
 * Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
 *
 * 1 WHY BIT-BANGED.  The encoders are written once at boot and once per mode change: about a dozen register
 *   writes to each of two addresses.  A bit-banged bus costs two open-drain pins and one register in the video
 *   page (rtl/vga/g2_video.v, +0x1C); an I2C master block would cost a state machine, a clock divider and a
 *   command/status register pair for a bus that is idle for the life of the program.  It is also the discipline
 *   the G1 already uses for the WM8750 codec (fw/codec.c), so the same sequence of pin states is reused.
 *
 * 2 THE REGISTERS (Chrontel CH7301C data sheet 201-0000-056 rev 1.32, table 8 and section 4.2).  Written in this
 *   order to each encoder:
 *
 *   | Reg | Value | Field                                 | Meaning                                              |
 *   |-----|-------|---------------------------------------|------------------------------------------------------|
 *   | 1Ch | 00h   | XCM = 0, MCP = 0                      | XCLK at the pixel rate, data on both edges; the      |
 *   |     |       |                                       | rising edge latches the first half (P0a)              |
 *   | 1Dh | 40h   | XCMD = 0                              | no de-skew (table 9); bits 7:4 at their default      |
 *   | 1Fh | 98h   | IDF = 0, HSP = 1, VSP = 1, DES = 0    | 12-bit RGB 8-8-8 multiplexed (P0a = {G[3:0], B},     |
 *   |     |       |                                       | P0b = {R, G[7:4]}); H and V active high: the video   |
 *   |     |       |                                       | page presents both syncs active high in every mode;  |
 *   |     |       |                                       | bit 7 reserved, default 1                            |
 *   | 21h | 09h   | SYNCO0 = 1, DACG = 00, DACBP = 1      | DAC bypass: the RGB input drives the three DACs      |
 *   |     |       |                                       | (the DVI-I analog pins); SYNCO0 enables the H and V  |
 *   |     |       |                                       | sync outputs (DC register description)               |
 *   | 33h | 08h   | TPCP (DVI PLL charge pump)            | table 10, pixel clock <= 65 MHz (06h above)          |
 *   | 34h | 16h   | TPD (DVI PLL dividers)                | table 10, <= 65 MHz (26h above)                      |
 *   | 36h | 60h   | TPF (DVI PLL filter)                  | table 10, <= 65 MHz (A0h above)                      |
 *   | 48h | 18h   | ResetIB = 1, ResetDB = 1, TSTP = 00   | serial port and datapath out of reset, no test image |
 *   | 49h | C0h   | DVIP = 1, DVIL = 1, DACPD = 000, FPD 0| DVI encoder, serialiser and PLL on; DACs on (table 12)|
 *
 *   The references read for this set are listed in rtl/VENDORED.md (G2 video output).
 *   Not written, left at their defaults: 1Eh (GPIO), 20h (hot-plug interrupt off), 23h (hardware hot-plug
 *   detection ON: the DVI outputs are tri-stated while no DVI sink pulls HPDET high), 31h, 35h, 37h, 56h.
 *
 * 3 UNTESTED WITHOUT HARDWARE: the I2C transaction format (the data sheet defers to application note AN-41, which
 *   is not in the repository), the RESET* pulse width, register read-back, which address is the DVI-I encoder,
 *   the sync polarity the encoder puts on the DVI and analog outputs, and the XCMD de-skew setting at 65 MHz.
 */
#include "ch7301.h"
#include "board.h"                         /* PANO_CYC_PER_US */

#define P_SCL_LOW   0x01u
#define P_SDA_LOW   0x02u
#define P_DVI_RUN   0x10u     /* dvi_reset_n released */
#define P_HDMI_RUN  0x20u     /* hdmi_reset_n released */

#ifdef CH7301_HOST
/* tools/ch7301_host: the pin register and the delay are the host test's I2C slave model */
void         ch7301_host_wr(unsigned int v);
unsigned int ch7301_host_rd(void);
void         ch7301_host_wait_us(unsigned int us);
#define pins_wr(v)  ch7301_host_wr(v)
#define pins_rd()   ch7301_host_rd()
#define wait_us(u)  ch7301_host_wait_us(u)
#else
#define VID_PINS (*(volatile unsigned int *)0x8000601Cu)
static void         pins_wr(unsigned int v) { VID_PINS = v; }
static unsigned int pins_rd(void)           { return VID_PINS; }
static unsigned int rdcycle_(void)
{
    unsigned int c;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}
static void wait_us(unsigned int us)
{
    unsigned int c0 = rdcycle_();
    while (rdcycle_() - c0 < us * PANO_CYC_PER_US) ;
}
#endif

static unsigned int s_rst = P_DVI_RUN | P_HDMI_RUN;
unsigned int ch7301_step_us = 5u;              /* half-step; the shell's i2cspeed command changes it */

/* SCL / SDA: 1 = released (the pull-up takes the line high), 0 = driven low.  5 us per step: 100 kHz. */
static void bus(unsigned int scl, unsigned int sda)
{
    pins_wr(s_rst | (scl ? 0u : P_SCL_LOW) | (sda ? 0u : P_SDA_LOW));
    wait_us(ch7301_step_us);
    if (scl) {                                 /* a released SCL may be held low by a slave: wait, bounded */
        unsigned int n = 0;
        while (!(pins_rd() & 1u) && ++n < 1000u) wait_us(1);
    }
}
static unsigned int sda_in(void) { return (pins_rd() >> 1) & 1u; }

static void i2c_start(void)   { bus(1, 1); bus(1, 0); bus(0, 0); }
static void i2c_restart(void) { bus(0, 1); bus(1, 1); bus(1, 0); bus(0, 0); }
static void i2c_stop(void)    { bus(0, 0); bus(1, 0); bus(1, 1); }

/* MSB first; returns 1 when the slave pulled SDA low on the ninth clock */
static int i2c_tx(unsigned int b)
{
    int i, ack;
    for (i = 7; i >= 0; i--) {
        unsigned int bit = (b >> i) & 1u;
        bus(0, bit);
        bus(1, bit);
    }
    bus(0, 1);                                 /* release SDA for the acknowledge */
    bus(1, 1);
    ack = (sda_in() == 0u);
    bus(0, 1);
    return ack;
}
/* MSB first, the master answers NACK (last byte) */
static unsigned int i2c_rx(void)
{
    unsigned int b = 0;
    int i;
    for (i = 7; i >= 0; i--) {
        bus(0, 1);
        bus(1, 1);
        if (sda_in()) b |= (1u << i);
    }
    bus(0, 1);
    bus(1, 1);                                 /* NACK */
    bus(0, 1);
    return b;
}

int ch7301_write(unsigned int addr, unsigned int reg, unsigned int val)
{
    int ok = 1;
    i2c_start();
    ok &= i2c_tx((addr << 1) | 0u);
    ok &= i2c_tx(reg & 0xFFu);
    ok &= i2c_tx(val & 0xFFu);
    i2c_stop();
    return ok;
}

int ch7301_read(unsigned int addr, unsigned int reg)
{
    int ok = 1;
    unsigned int v;
    i2c_start();
    ok &= i2c_tx((addr << 1) | 0u);
    ok &= i2c_tx(reg & 0xFFu);
    i2c_restart();
    ok &= i2c_tx((addr << 1) | 1u);
    v = i2c_rx();
    i2c_stop();
    return ok ? (int)v : -1;
}

/* write one register to both encoders; bit 0 / bit 1 of the result: 0x75 / 0x76 acknowledged */
static unsigned int both(unsigned int reg, unsigned int val)
{
    unsigned int m = 0;
    if (ch7301_write(CH7301_ADDR_A, reg, val)) m |= 1u;
    if (ch7301_write(CH7301_ADDR_B, reg, val)) m |= 2u;
    return m;
}

int ch7301_set_mode(unsigned int mode_id)
{
    unsigned int m = 3u;
    if (mode_id == 3u || mode_id > 4u) return -1;   /* 65 MHz is the fastest pixel clock the G2 head drives; mode 4 runs XCLK at 26.8 MHz */
    m &= both(0x33u, 0x08u);                   /* table 10, <= 65 MHz */
    m &= both(0x34u, 0x16u);
    m &= both(0x36u, 0x60u);
    return (int)m;
}

int ch7301_init(unsigned int mode_id)
{
    unsigned int m = 3u;
    int r;
    if (mode_id == 3u || mode_id > 4u) return -1;
    /* RESET* low, then released: registers to their defaults (both encoders) */
    s_rst = 0u;
    pins_wr(0u);
    wait_us(100);
    s_rst = P_DVI_RUN | P_HDMI_RUN;
    pins_wr(s_rst);
    wait_us(1000);
    bus(1, 1);

    m &= both(0x1Cu, 0x00u);
    m &= both(0x1Du, 0x40u);
    m &= both(0x1Fu, 0x98u);
    m &= both(0x21u, 0x09u);
    r = ch7301_set_mode(mode_id);
    m &= (r < 0) ? 0u : (unsigned int)r;
    m &= both(0x48u, 0x18u);
    m &= both(0x49u, 0xC0u);
    return (int)m;
}
