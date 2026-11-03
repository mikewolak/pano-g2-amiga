/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/snes/snes_audio.h -- the core's audio output to the 48 kHz codec FIFO. */
#ifndef SNES_AUDIO_H
#define SNES_AUDIO_H
#include <stdint.h>
void snes_audio_init(void);          /* coefficient table, rings cleared; the codec is programmed elsewhere */
void snes_audio_reset(void);         /* rings, position and statistics cleared; the coefficient table kept */
void snes_audio_start(void);         /* codec FIFO primed with silence, watermark interrupt on */
void snes_audio_stop(void);          /* watermark interrupt off, codec FIFO left to drain */
void snes_audio_service(void);       /* from the interrupt handler: drain the core's audio FIFO, resample, refill the codec */
typedef struct {
    uint32_t in_frames, out_frames, irqs, ring_under, ring_over, codec_under, ratio_q16;
    int32_t  ring_level;
    uint64_t cycles;                 /* mcycle spent in snes_audio_service */
    uint32_t cycles_max;             /* the longest call */
} snes_audio_stats_t;
extern snes_audio_stats_t snes_audio_stats;
#endif
