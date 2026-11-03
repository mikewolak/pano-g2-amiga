/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/share.h -- the file server of the Amiga's SHARE: volume (fw/amiga/share.c) */
#ifndef SHARE_H
#define SHARE_H
#include <stdint.h>

#define SHARE_DATA_BYTES 65536u                 /* the data of one READ or WRITE request */
extern uint8_t share_data[SHARE_DATA_BYTES];

/* the shared directory (a FatFs path, made when missing; NULL keeps /amiga/share) and the test for a file in use as a
 * drive image (full FatFs path; 1 in use); every lock and file handle of an earlier session is dropped */
void share_init(const char *root, int (*busy)(const char *path));
const char *share_root(void);
/* one request (from its action field) into its response (from success); the response's length */
unsigned int share_request(const uint8_t *req, unsigned int len, uint8_t *res);

#endif
