/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/codec_g2.c -- the WM8750BL register program over the G2's second bit-banged I2C bus.  The bus register is the
 * USB wrapper's +0x28 (rtl/g2/g2_usb_host.v): bits [3:2] drive SCL and SDA low on bus 1 (U17/AB17), bits [7:6] read
 * them back.  10 us half steps (50 kHz), SCL stretching waited for up to 1 ms.  Each codec write is three bytes,
 * {0x34}, {reg[6:0], data[8]}, {data[7:0]} (WM8750BL data sheet, 2-wire control).  The program is the G1 driver's
 * (fw/codec.c) with the MCLK halver on: USB clock mode at 24.000 MHz, 48 kHz, 24-bit I2S slave, both DACs into the
 * output mixers, headphone and speaker outputs on, 0 dB.  fw/bootsh.c carries the same table for its `codec' command. */
#include "codec_g2.h"
#include "board.h"                         /* PANO_CYC_PER_US */
#define I2C_REG (*(volatile uint32_t *)0x81000028u)
static unsigned int s_oe;
static unsigned int rdcyc(void) { unsigned int c; __asm__ volatile ("csrr %0, mcycle" : "=r"(c)); return c; }
static void wait_us(unsigned int us) { unsigned int c0 = rdcyc(); while (rdcyc() - c0 < us * PANO_CYC_PER_US) ; }
static void i2c_set(unsigned int scl_low, unsigned int sda_low)
{
    s_oe = (s_oe & ~(3u << 2)) | ((scl_low ? 1u : 0u) << 2) | ((sda_low ? 2u : 0u) << 2);
    I2C_REG = s_oe;
    wait_us(10u);
}
static unsigned int i2c_sda(void) { return (I2C_REG >> 7) & 1u; }
static unsigned int i2c_scl(void) { return (I2C_REG >> 6) & 1u; }
static void i2c_scl_high(unsigned int sda_low)
{
    unsigned int n;
    i2c_set(0u, sda_low);
    for (n = 0; n < 100u && !i2c_scl(); n++) wait_us(10u);
}
static void i2c_start(void) { i2c_set(0u, 0u); i2c_set(0u, 1u); i2c_set(1u, 1u); }
static void i2c_stop(void)  { i2c_set(1u, 1u); i2c_scl_high(1u); i2c_set(0u, 0u); }
static unsigned int i2c_wbyte(unsigned int v)
{
    unsigned int k, ack;
    for (k = 0; k < 8u; k++) {
        unsigned int bit = (v >> (7u - k)) & 1u;
        i2c_set(1u, !bit); i2c_scl_high(!bit); i2c_set(1u, !bit);
    }
    i2c_set(1u, 0u); i2c_scl_high(0u); ack = !i2c_sda(); i2c_set(1u, 0u);
    return ack;
}
static int codec_wr(unsigned int reg, unsigned int data)
{
    unsigned int ack;
    s_oe = 0u; I2C_REG = 0u; wait_us(20u);
    i2c_start();
    ack = i2c_wbyte(0x34u);
    if (ack) ack = i2c_wbyte(((reg & 0x7Fu) << 1) | ((data >> 8) & 1u));
    if (ack) ack = i2c_wbyte(data & 0xFFu);
    i2c_stop();
    s_oe = 0u; I2C_REG = 0u;
    return (int)ack;
}
static const unsigned short codec_prog[][2] = {
    { 15, 0x000u },   /* reset */
    {  7, 0x00Au },   /* WL = 10 (24-bit), FORMAT = 10 (I2S), MS = 0 (slave) */
    {  8, 0x041u },   /* USB = 1, CLKDIV2 = 1, SR = 00000: 48 kHz from the 24 MHz MCLK halved */
    { 34, 0x150u },   /* LD2LO: left DAC -> left output mixer */
    { 37, 0x150u },   /* RD2RO: right DAC -> right output mixer */
    { 24, 0x010u },   /* ROUT2INV */
    { 25, 0x0FCu },   /* VMIDSEL = 01, VREF, AINL, AINR, ADCL, ADCR */
    { 26, 0x1F8u },   /* DACL, DACR, LOUT1, ROUT1, LOUT2, ROUT2 */
    {  0, 0x017u },   /* LINVOL 0 dB */
    {  1, 0x017u },   /* RINVOL 0 dB */
    {  2, 0x179u },   /* LOUT1VOL 0 dB, zero-cross, both channels */
    {  3, 0x179u },   /* ROUT1VOL 0 dB */
    { 40, 0x179u },   /* LOUT2VOL 0 dB */
    { 41, 0x179u },   /* ROUT2VOL 0 dB */
    {  5, 0x000u }    /* DACMU = 0 */
};
int codec_g2_present(void) { return AUDG2_ID == AUDG2_ID_VALUE; }
int codec_g2_reset(void) { return codec_wr(15u, 0x000u); }
int codec_g2_init(unsigned int *ack_mask)
{
    unsigned int k, m = 0u, n = sizeof codec_prog / sizeof codec_prog[0];
    for (k = 0; k < n; k++) {
        if (codec_wr(codec_prog[k][0], codec_prog[k][1])) m |= 1u << k;
        if (k == 0u) wait_us(10000u);             /* the part settles after the reset write */
    }
    if (ack_mask) *ack_mask = m;
    return m == (1u << n) - 1u;
}
