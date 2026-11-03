/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
#ifndef SHA256_H
#define SHA256_H
#include <stdint.h>
#include <stddef.h>
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
#endif
