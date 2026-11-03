/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* panoflash.h -- panoflash.device: the G2's configuration flash (M25P64, 8 MB) for AmigaOS programs.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 * The part is read at every request (RDID): the M25P64 of revision C (8 MB, 64 KB sectors) and the M25P128 of
 * revisions A and B (16 MB, 256 KB sectors) each have their map (fw/spiflash.h); any other ID refuses every write.
 * The device reaches the flash through the firmware's flash window on HD0 (scsi.device unit 0,
 * fw/amiga/pflash.h).  Each unit is a region of the flash; io_Offset counts from the start of the unit.
 *
 *   unit 0  PFL_UNIT_CHIP  the whole part, read only
 *   unit 1  PFL_UNIT_MAIN  the main slot (M25P64 0x380000-0x6BFFFF, M25P128 0x480000-0x8BFFFF): the fabric and
 *                          the firmware loaded at power-up
 *   unit 2  PFL_UNIT_FREE  the free region (M25P64 0x6D0000-0x7FFFFF, M25P128 0x900000-0xFFFFFF): U-Boot, the DDR
 *                          shell, the boot-select word
 * The multiboot header, the golden image and the configuration table lie in no writable unit; the firmware and the
 * fabric's write guard refuse them as well.
 *
 * Commands (all IOStdReq; a request runs in the caller's task and returns when it is done):
 *   CMD_READ          io_Length bytes from io_Offset (any alignment)
 *   CMD_WRITE         io_Offset on a sector boundary: every sector the range touches is erased, the bytes programmed
 *   TD_FORMAT         and read back; bytes of the last sector after the range read 0xFF.  A sector that already
 *                     holds the bytes is not erased.  io_Actual: the bytes written
 *   PFL_CMD_ERASE     io_Offset and io_Length on sector boundaries
 *   PFL_CMD_CRC       io_Actual = CRC32 (PKZIP) of [io_Offset, io_Offset + io_Length), computed by the firmware; with
 *                     io_Data pointing to a ULONG, the CRC32 continues from it (the CRC32 of the bytes before)
 *   PFL_CMD_INFO      io_Data a struct PanoFlashInfo, io_Length its size
 *   TD_GETGEOMETRY    a DriveGeometry with one erase sector (64 KB or 256 KB) per track
 *   TD_PROTSTATUS     io_Actual non-zero on unit 0
 *   CMD_UPDATE, CMD_CLEAR, TD_MOTOR, TD_CHANGENUM, TD_CHANGESTATE   accepted, nothing to do
 */
#ifndef PANOFLASH_H
#define PANOFLASH_H
#include <exec/types.h>
#include <exec/io.h>

#define PANOFLASH_NAME   "panoflash.device"
#define PFL_UNIT_CHIP    0
#define PFL_UNIT_MAIN    1
#define PFL_UNIT_FREE    2
#define PFL_UNITS        3

/* above trackdisk.device's commands (TD_LASTCOMM), so TD_ and PFL_ numbers never meet */
#define PFL_CMD_INFO     0x5040
#define PFL_CMD_ERASE    0x5041
#define PFL_CMD_CRC      0x5042

/* io_Error beyond exec's IOERR_ codes: the firmware's results (fw/amiga/pflash.h PFL_R_), then the device's own */
#define PFLERR_BADREQ     1
#define PFLERR_PROTECTED  2    /* outside the unit's writable range, or unit 0 */
#define PFLERR_GUARD      3    /* the fabric's write guard refused a command */
#define PFLERR_TIMEOUT    4    /* the flash stayed busy */
#define PFLERR_VERIFY     5    /* the bytes read back differ */
#define PFLERR_DATACRC    6    /* the data window was not received intact */
#define PFLERR_NOPART     7    /* the flash ID is not a known part */
#define PFLERR_NOGUARD    8    /* the fabric has no write guard */
#define PFLERR_BP         9    /* block-protect bits set */
#define PFLERR_NOWINDOW  20    /* no flash window on HD0 (firmware without it, or a disk laid out without room) */
#define PFLERR_NOANSWER  21    /* the firmware did not finish a request within its limit */
#define PFLERR_DISK      22    /* scsi.device unit 0 failed or did not open */

struct PanoFlashInfo {
    ULONG pfi_RDID;            /* JEDEC ID, 0x202017 for the M25P64 */
    ULONG pfi_Status;          /* status register */
    ULONG pfi_ChipSize;        /* bytes */
    ULONG pfi_SectorSize;      /* erase sector, bytes */
    ULONG pfi_UnitBase;        /* the unit's first flash address */
    ULONG pfi_UnitSize;        /* the unit's bytes */
    ULONG pfi_Golden, pfi_Main, pfi_MainEnd, pfi_Free, pfi_FreeEnd, pfi_Config;   /* the part's map */
    ULONG pfi_Guard;           /* 1 when the fabric has the SPI write guard */
    ULONG pfi_Refusals;        /* the guard's refusal count */
    ULONG pfi_Passed;          /* write and erase commands the guard passed */
    ULONG pfi_Erased;          /* sectors erased since the firmware started */
    char  pfi_Message[96];     /* the firmware's line for the last request */
};
#endif
