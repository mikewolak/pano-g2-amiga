/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/pflash.h -- the configuration flash for AmigaOS: the flash window of HD0, served
 * for amiga/panoflash (panoflash.device).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 * The part and its map are fw/spiflash.c's (spif_map: M25P64 revision C, M25P128 revisions A and B); WRITE erases the
 * sector of the part (64 KB or 256 KB), so on the M25P128 a sector takes one WRITE and three PROGRAM requests.
 *
 * Blocks of HD0, answered by the firmware and never written to the file, when the RigidDiskBlock uses blocks below
 * PFL_REQ only and the partitions start at PFL_DATA + PFL_DATA_N or later (hd_create: cylinder 2, block 2016):
 *   PFL_REQ   written  0 'PFW1'  4 sequence number  8 operation  12 flash address  16 length  20 CRC32 of the data
 *                      window's first <length> bytes (WRITE, PROGRAM); CRC: the CRC32
 *                      of the bytes before the range (0 for none), so that ranges in turn give one CRC32
 *   PFL_STAT  read     0 'PFA1'  4 sequence number of the last request  8 state: 0 none, 1 running, 2 done
 *                     12 result (PFL_R_)  16 bytes done  20 CRC32 (READ: of the data read; CRC: of the range; WRITE,
 *                        PROGRAM: of the bytes read back)  24 RDID  28 status register  32 flash size  36 sector size
 *                     40 main slot  44 its end  48 free region  52 its end  56 configuration table
 *                     60 1 when the fabric has the SPI write guard  64 guard refusals  68 write/erase commands passed
 *                     72 sectors erased since the firmware started  76 sectors erased by this request
 *                     80 bytes that differ (WRITE, PROGRAM verify)  84 offset of the first  88 data window bytes
 *                     92 golden image
 *                     96 a line of text, NUL-terminated
 *   PFL_DATA  read and written: the data window, PFL_WIN bytes
 * A request starts when PFL_REQ is written with 'PFW1' and a sequence number other than the last, or with the last
 * when no request runs.  The operation runs from the main loop a step at a time (at most one page program, one
 * status read or 1,024 bytes read a step), so the machine, the drives and USB keep running during an erase. */
#ifndef PFLASH_H
#define PFLASH_H
#include <stdint.h>

#define PFL_REQ    144u
#define PFL_STAT   145u
#define PFL_DATA   256u
#define PFL_WIN    65536u
#define PFL_DATA_N (PFL_WIN / 512u)

#define PFL_OP_INFO    1u    /* the part, the map and the guard; nothing else */
#define PFL_OP_READ    2u    /* <length> (at most PFL_WIN) bytes from <address> into the data window */
#define PFL_OP_WRITE   3u    /* the sector at <address> (a sector boundary) erased, the window's <length> bytes
                              * programmed from its start, read back; nothing written when the sector already holds
                              * them followed by erased bytes */
#define PFL_OP_ERASE   4u    /* the sectors of [<address>, <address> + <length>), both on sector boundaries */
#define PFL_OP_CRC     5u    /* CRC32 of [<address>, <address> + <length>) */
#define PFL_OP_PROGRAM 6u    /* the window's <length> bytes programmed at <address> (erased flash), read back */

#define PFL_R_OK        0u
#define PFL_R_BADREQ    1u   /* operation, address, length or alignment */
#define PFL_R_PROTECTED 2u   /* a write outside the main slot and the free region */
#define PFL_R_GUARD     3u   /* the fabric's write guard refused a command */
#define PFL_R_TIMEOUT   4u   /* the flash stayed busy (erase 8 s, page program 5 ms) */
#define PFL_R_VERIFY    5u   /* the bytes read back differ */
#define PFL_R_DATACRC   6u   /* the data window does not hold the bytes the request's CRC32 names */
#define PFL_R_NOPART    7u   /* RDID names neither the M25P64 nor the M25P128 */
#define PFL_R_NOGUARD   8u   /* the fabric has no write guard: no write is made */
#define PFL_R_BP        9u   /* block-protect bits set in the status register */

extern uint8_t pfl_data[PFL_WIN];
void pfl_request(const uint8_t *blk);    /* a block written to PFL_REQ */
void pfl_status(uint8_t *blk);           /* the 512 bytes of PFL_STAT */
int  pfl_service(void);                  /* one step; 1 when a request other than INFO finished in this step */
const char *pfl_message(void);           /* the last request's line */
#endif
