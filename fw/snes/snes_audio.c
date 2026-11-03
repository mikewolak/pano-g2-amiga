/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/snes/snes_audio.c -- the core's audio output in the fabric FIFO,
 * resampled to the codec's 48,000 and written to the g2_audio FIFO from its watermark interrupt.
 *
 * INPUT RATE.  The DSP's sample clock is a clock enable of MCLK: snes/mister/rtl/DSP.vhd CEGen with IN_CLK
 * MCLK_NTSC_FREQ 21,477,270 and OUT_CLK ACLK_TYPE_FREQ 4,096,000 (DSP_PKG.vhd, CTRL.dsp_freq 0), one output frame per
 * 128 enables: 32,000 Hz while MCLK runs at 21.477 MHz, and proportionally less while the console is held or stopped.
 * RESAMPLER.  Polyphase FIR, 32 phases x 8 taps, from a windowed sinc (Kaiser, beta 6) with the cut-off at 15 kHz,
 * Q15 coefficients normalised per phase to unity gain.  Input position in Q16; the step is the nominal
 * 32,000 / 48,000 plus the distance of the software ring from half full, in Q16 units (one unit per frame, at most
 * +/- 2 % of the step): a first-order loop with a time constant of 1.4 s (48,000 / 65,536 frames a second per unit),
 * which follows the frame lock's trim of the core clock and refills the ring after a pause of the console.
 * SERVICE.  At each interrupt (codec FIFO below CODEC_LOW frames): every frame waiting in the core's audio FIFO goes to the
 * ring; output frames are produced until the codec FIFO holds CODEC_HIGH: 192 frames, 4 ms of audio, a refill, so the
 * handler (173 clk_sys cycles a frame in the whole-SoC simulation, 33,216 cycles, 0.66 ms) stays inside the 1 ms timer
 * period.  An empty ring produces silence (counted), so the
 * interrupt always ends.  The handler's mcycle count is accumulated for the CPU load (snes_audio_stats).
 * COEFFICIENTS.  Computed by snes_audio_init, or, with AUD_COEF_TABLE defined (the name of a header made by
 * tools/audcoef/audcoef from the same arithmetic), taken from that table, which links no floating-point code.
 * WIDE (AUD_WIDE, the Amiga build): when the fabric reports 18-bit samples (CAPS bit 0; rtl/g2/amiga_g2.v: Paula's 16
 * bits plus the Toccata card's 16, summed in 18), the input is read from AUD_WL and AUD_WR, the ring holds 18 bits,
 * the filter accumulates in 64 bits, and g2_audio runs wide: each frame two words, left then right, a 24-bit sample
 * left-justified ({S[23:0], 8'd0}; the 18 bits << 6), the codec FIFO and its watermark counted in words. */
#include <string.h>
#include <stdint.h>
#ifndef AUD_COEF_TABLE
#include <math.h>
#endif
#include "snes_audio.h"
#include "snes_regs.h"

#define PHASES      32
#define TAPS        8
#define RING        4096u                       /* power of two */
#define CODEC_LOW   1344u                       /* 192 frames a refill */
#define CODEC_HIGH  1536u
#ifndef AUD_IN_HZ
#define AUD_IN_HZ   32000u                      /* the input rate; the Amiga fabric's Paula FIFO: 48,000 (fw/amiga) */
#endif
#define STEP_NOM    ((AUD_IN_HZ * 65536u + 24000u) / 48000u)   /* 32,000 / 48,000 x 65,536 = 43,691 */
#define CORR_MAX    (STEP_NOM / 50u)            /* 2 % */

snes_audio_stats_t snes_audio_stats;
#ifdef AUD_COEF_TABLE
#include AUD_COEF_TABLE
#else
static int16_t  coef[PHASES][TAPS];
#endif
#ifdef AUD_WIDE
#define SNES_AUD_WL   SNES_R(0x05C)             /* rtl/g2/amiga_g2.v 0x017: left, 18 bits sign-extended, no pop */
#define SNES_AUD_WR   SNES_R(0x064)             /* 0x019: right; pops */
#define SNES_CAPS     SNES_R(0x07C)             /* 0x01F: [0] 18-bit samples */
typedef int32_t smp_t;
static unsigned int wide;                        /* the fabric's 18-bit samples and g2_audio's wide frames */
#else
typedef int16_t smp_t;
#define wide 0u
#endif
static smp_t    ring_l[RING], ring_r[RING];
static uint32_t wr, rd;                          /* ring indices (free-running) */
static uint32_t frac;                            /* Q16 fraction of the input position past rd */

#ifndef AUD_COEF_TABLE
static double bessel_i0(double x)
{
    double s = 1.0, t = 1.0; int k;
    for (k = 1; k < 30; k++) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
    return s;
}

void snes_audio_init(void)
{
    const double fc = 15000.0 / (double)AUD_IN_HZ;   /* cut-off relative to the input rate */
    const double beta = 6.0, i0b = bessel_i0(6.0);
    int p, k;
    for (p = 0; p < PHASES; p++) {
        double h[TAPS], sum = 0.0;
        for (k = 0; k < TAPS; k++) {
            double x = (double)(k - (TAPS / 2 - 1)) - (double)p / PHASES;   /* tap position relative to the output */
            double s = (fabs(x) < 1e-9) ? 2.0 * fc : sin(2.0 * M_PI * fc * x) / (M_PI * x);
            double w = (double)(k) + 1.0 - (double)p / PHASES;                 /* 0 .. TAPS across the window */
            double r = (w - TAPS / 2.0) / (TAPS / 2.0);
            double win = (fabs(r) <= 1.0) ? bessel_i0(beta * sqrt(1.0 - r * r)) / i0b : 0.0;
            h[k] = s * win; sum += h[k];
        }
        for (k = 0; k < TAPS; k++) coef[p][k] = (int16_t)lrint(h[k] / sum * 32767.0);
    }
    snes_audio_reset();
}
#else
void snes_audio_init(void) { snes_audio_reset(); }
#endif

void snes_audio_reset(void)
{
    memset(ring_l, 0, sizeof ring_l); memset(ring_r, 0, sizeof ring_r);
    wr = RING / 2u; rd = 0u; frac = 0u;
    memset(&snes_audio_stats, 0, sizeof snes_audio_stats);
}

void snes_audio_start(void)
{
    unsigned int i;
    SNES_CACHE = CACHE_AUD_CLEAR;
    wr = RING / 2u; rd = 0u; frac = 0u;
#ifdef AUD_WIDE
    wide = SNES_CAPS & 1u;
#endif
    AUD_CTRL = 0x2u;                             /* flush */
    if (wide) {                                  /* the FIFO and its watermark count words, two a frame */
        AUD_CTRL = 0x4u;
        for (i = 0; i < CODEC_HIGH; i++) AUD_DATA = 0u;
        AUD_CTRL = 0x5u;                         /* run, 24-bit words */
    } else {
        for (i = 0; i < CODEC_HIGH; i++) AUD_DATA = 0u;
        AUD_CTRL = 0x1u;                         /* run, 16-bit frames */
    }
    AUD_WMARK = 0x80000000u | CODEC_LOW;
}

void snes_audio_stop(void)
{
    AUD_WMARK = CODEC_LOW;                       /* interrupt off */
}

static inline int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

#ifdef __riscv
static inline uint32_t rdcyc(void) { uint32_t c; __asm__ volatile ("csrr %0, mcycle" : "=r"(c)); return c; }
#else
static inline uint32_t rdcyc(void) { return 0u; }   /* host build (snes/tools/audiotest.c) */
#endif

void snes_audio_service(void)
{
    uint32_t n, lvl, step, room, c0 = rdcyc(), dc;
    int32_t err;
    snes_audio_stats.irqs++;
    /* the core's audio FIFO into the ring */
    n = SNES_AUD_LEVEL & 0x7FFu;
    while (n--) {
#ifdef AUD_WIDE
        if (wide) {
            int32_t l = (int32_t)SNES_AUD_WL, r = (int32_t)SNES_AUD_WR;   /* the right read pops */
            if (wr - rd >= RING - TAPS) { snes_audio_stats.ring_over++; continue; }
            ring_l[wr & (RING - 1u)] = l; ring_r[wr & (RING - 1u)] = r;
            wr++; snes_audio_stats.in_frames++;
            continue;
        }
#endif
        {
            uint32_t v = SNES_AUD_DATA;
            if (wr - rd >= RING - TAPS) { snes_audio_stats.ring_over++; continue; }
            ring_l[wr & (RING - 1u)] = (int16_t)(v >> 16);
            ring_r[wr & (RING - 1u)] = (int16_t)(v & 0xFFFFu);
            wr++; snes_audio_stats.in_frames++;
        }
    }
    /* ratio: nominal plus one Q16 unit per frame of distance from a half-full ring, at most 2 % */
    err = (int32_t)(wr - rd) - (int32_t)(RING / 2u);
    snes_audio_stats.ring_level = (int32_t)(wr - rd);
    if (err > (int32_t)CORR_MAX) err = (int32_t)CORR_MAX;
    if (err < -(int32_t)CORR_MAX) err = -(int32_t)CORR_MAX;
    step = (uint32_t)((int32_t)STEP_NOM + err);
    snes_audio_stats.ratio_q16 = step;
    lvl = AUD_STATUS & 0x1FFFu;
    if (lvl == 0u) snes_audio_stats.codec_under++;
    room = lvl < CODEC_HIGH ? CODEC_HIGH - lvl : 0u;
#ifdef AUD_WIDE
    if (wide) room /= 2u;                        /* words to frames */
#endif
    while (room--) {
        int32_t al = 0, ar = 0;
        uint32_t p = frac >> 11, k, base;              /* 32 phases from the Q16 fraction */
        if (wr - rd < TAPS + 1u) {
            AUD_DATA = 0u; if (wide) AUD_DATA = 0u;
            snes_audio_stats.ring_under++; continue;
        }
        base = rd;
#ifdef AUD_WIDE
        if (wide) {                              /* Q15 x 18 bits in 64; >> 9 is the 18 bits as 24 */
            int64_t wl = 0, wrr = 0;
            int32_t sl, sr;
            for (k = 0; k < TAPS; k++) {
                wl  += (int64_t)coef[p][k] * ring_l[(base + k) & (RING - 1u)];
                wrr += (int64_t)coef[p][k] * ring_r[(base + k) & (RING - 1u)];
            }
            sl = (int32_t)(wl >> 9); sr = (int32_t)(wrr >> 9);
            sl = sl > 0x7FFFFF ? 0x7FFFFF : sl < -0x800000 ? -0x800000 : sl;
            sr = sr > 0x7FFFFF ? 0x7FFFFF : sr < -0x800000 ? -0x800000 : sr;
            AUD_DATA = (uint32_t)sl << 8; AUD_DATA = (uint32_t)sr << 8;
            snes_audio_stats.out_frames++;
            frac += step;
            rd += frac >> 16; frac &= 0xFFFFu;
            continue;
        }
#endif
        for (k = 0; k < TAPS; k++) {
            al += (int32_t)coef[p][k] * ring_l[(base + k) & (RING - 1u)];
            ar += (int32_t)coef[p][k] * ring_r[(base + k) & (RING - 1u)];
        }
        AUD_DATA = ((uint32_t)(uint16_t)clamp16(al >> 15) << 16) | (uint16_t)clamp16(ar >> 15);
        snes_audio_stats.out_frames++;
        frac += step;
        rd += frac >> 16; frac &= 0xFFFFu;
    }
    dc = rdcyc() - c0;
    snes_audio_stats.cycles += dc;
    if (dc > snes_audio_stats.cycles_max) snes_audio_stats.cycles_max = dc;
}
