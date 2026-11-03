/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* flashcli.c -- FlashCLI: the G2's configuration flash from the AmigaDOS shell through panoflash.device.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 *   FlashCLI INFO   [unit]                         the part, the map, the guard, the unit
 *   FlashCLI READ   <unit> <offset> <length> <file> bytes to a file (length 0: to the end of the unit)
 *   FlashCLI CRC    <unit> <offset> <length>        CRC32 computed by the firmware
 *   FlashCLI VERIFY <unit> <offset> <file>          the file against the flash, by CRC32
 *   FlashCLI WRITE  <unit> <offset> <file>          erase, program, read back; then the range's CRC32 against the file
 *   FlashCLI ERASE  <unit> <offset> <length>
 * Units: 0 or CHIP (read only), 1 or MAIN, 2 or FREE.  Numbers decimal or 0x hex. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "panoflash.h"
#include "crc32.h"

static const char ver[] __attribute__((used)) = "$VER: FlashCLI 1.0";
static struct MsgPort *port;
static struct IOStdReq *io;

static const char *err_s(int e)
{
    switch (e) {
    case 0: return "ok";
    case IOERR_BADADDRESS: return "offset or length outside the unit, or not on a sector boundary";
    case IOERR_BADLENGTH: return "bad length";
    case IOERR_NOCMD: return "command not supported";
    case PFLERR_BADREQ: return "the firmware refused the request";
    case PFLERR_PROTECTED: return "protected range (unit 0 is read only)";
    case PFLERR_GUARD: return "the fabric's write guard refused a command";
    case PFLERR_TIMEOUT: return "the flash stayed busy";
    case PFLERR_VERIFY: return "VERIFY FAILED: the bytes read back differ";
    case PFLERR_DATACRC: return "the data window was not received intact";
    case PFLERR_NOPART: return "the flash ID is not a known part";
    case PFLERR_NOGUARD: return "the fabric has no write guard: no write is made";
    case PFLERR_BP: return "block-protect bits set";
    case PFLERR_NOWINDOW: return "no flash window on HD0 (firmware without it, or the disk's layout has no room)";
    case PFLERR_NOANSWER: return "the firmware did not finish the request";
    case PFLERR_DISK: return "scsi.device unit 0 failed";
    default: return "error";
    }
}

static int unit_of(const char *s)
{
    if (!stricmp(s, "CHIP")) return 0;
    if (!stricmp(s, "MAIN")) return 1;
    if (!stricmp(s, "FREE")) return 2;
    return (int)strtoul(s, NULL, 0);
}

static int dev_cmd(UWORD cmd, ULONG off, APTR data, ULONG len)
{
    io->io_Command = cmd; io->io_Offset = off; io->io_Data = data; io->io_Length = len;
    return DoIO((struct IORequest *)io);
}

static int fail(const char *what, int e)
{
    printf("%s: %s (error %d)\n", what, err_s(e), e);
    return RETURN_ERROR;
}

static int load(const char *name, UBYTE **buf, ULONG *len)
{
    BPTR f = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!f) { printf("cannot open %s\n", name); return -1; }
    Seek(f, 0, OFFSET_END); n = Seek(f, 0, OFFSET_BEGINNING);
    if (n <= 0 || !(*buf = AllocMem((ULONG)n, MEMF_ANY))) { Close(f); printf("%s: empty or no memory\n", name); return -1; }
    if (Read(f, *buf, n) != n) { Close(f); FreeMem(*buf, (ULONG)n); printf("%s: read failed\n", name); return -1; }
    Close(f); *len = (ULONG)n;
    return 0;
}

int main(int argc, char **argv)
{
    struct PanoFlashInfo pi;
    int unit, e, rc = RETURN_OK;
    const char *verb;
    ULONG off, len, t0;
    struct DateStamp d0, d1;
    if (argc < 2) {
        printf("FlashCLI INFO [unit] | READ <unit> <offset> <length> <file> | CRC <unit> <offset> <length>\n"
               "         VERIFY <unit> <offset> <file> | WRITE <unit> <offset> <file> | ERASE <unit> <offset> <length>\n"
               "units: 0 CHIP (read only), 1 MAIN, 2 FREE\n");
        return RETURN_WARN;
    }
    verb = argv[1];
    unit = argc > 2 ? unit_of(argv[2]) : 0;
    off = argc > 3 ? strtoul(argv[3], NULL, 0) : 0;
    port = CreateMsgPort();
    io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof *io) : NULL;
    if (!io) return RETURN_FAIL;
    if ((e = OpenDevice((CONST_STRPTR)PANOFLASH_NAME, (ULONG)unit, (struct IORequest *)io, 0))) {
        printf("%s unit %d: %s (error %d)\n", PANOFLASH_NAME, unit, err_s(e), e);
        DeleteIORequest((struct IORequest *)io); DeleteMsgPort(port);
        return RETURN_FAIL;
    }
    if ((e = dev_cmd(PFL_CMD_INFO, 0, &pi, sizeof pi))) { rc = fail("info", e); goto out; }
    DateStamp(&d0);
    if (!stricmp(verb, "INFO")) {
        printf("part      RDID %06lX, %lu KB, sectors %lu KB, status %02lX\n", pi.pfi_RDID, pi.pfi_ChipSize >> 10,
               pi.pfi_SectorSize >> 10, pi.pfi_Status);
        printf("map       golden 0x%06lX, main 0x%06lX-0x%06lX, free 0x%06lX-0x%06lX, configuration 0x%06lX\n", pi.pfi_Golden, pi.pfi_Main,
               pi.pfi_MainEnd - 1, pi.pfi_Free, pi.pfi_FreeEnd - 1, pi.pfi_Config);
        printf("guard     %s, refusals %lu, write/erase passed %lu, sectors erased %lu\n",
               pi.pfi_Guard ? "present" : "ABSENT", pi.pfi_Refusals, pi.pfi_Passed, pi.pfi_Erased);
        printf("unit %d    0x%06lX-0x%06lX, %lu bytes%s\n", unit, pi.pfi_UnitBase, pi.pfi_UnitBase + pi.pfi_UnitSize - 1,
               pi.pfi_UnitSize, unit == 0 ? ", read only" : "");
        printf("firmware  %s\n", pi.pfi_Message);
    } else if (!stricmp(verb, "CRC") && argc > 4) {
        len = strtoul(argv[4], NULL, 0); if (!len) len = pi.pfi_UnitSize - off;
        if ((e = dev_cmd(PFL_CMD_CRC, off, NULL, len))) rc = fail("crc", e);
        else printf("unit %d 0x%06lX %lu bytes: CRC32 %08lX\n", unit, off, len, io->io_Actual);
    } else if (!stricmp(verb, "READ") && argc > 5) {
        UBYTE *b; BPTR f;
        len = strtoul(argv[4], NULL, 0); if (!len) len = pi.pfi_UnitSize - off;
        if (!(b = AllocMem(len, MEMF_ANY))) { printf("no memory for %lu bytes\n", len); rc = RETURN_FAIL; goto out; }
        if ((e = dev_cmd(CMD_READ, off, b, len))) rc = fail("read", e);
        else if (!(f = Open((CONST_STRPTR)argv[5], MODE_NEWFILE))) { printf("cannot write %s\n", argv[5]); rc = RETURN_ERROR; }
        else {
            LONG w = Write(f, b, (LONG)len); Close(f);
            if (w != (LONG)len) { printf("%s: write failed\n", argv[5]); rc = RETURN_ERROR; }
            else printf("read unit %d 0x%06lX %lu bytes to %s, CRC32 %08lX\n", unit, off, len, argv[5], crc32(b, len));
        }
        FreeMem(b, len);
    } else if ((!stricmp(verb, "VERIFY") || !stricmp(verb, "WRITE")) && argc > 4) {
        UBYTE *b; ULONG fc;
        if (load(argv[4], &b, &len)) { rc = RETURN_ERROR; goto out; }
        fc = crc32(b, len);
        if (!stricmp(verb, "WRITE")) {
            if ((e = dev_cmd(CMD_WRITE, off, b, len))) { rc = fail("write", e); FreeMem(b, len); goto out; }
            printf("wrote unit %d 0x%06lX %lu bytes (0x%06lX-0x%06lX)\n", unit, off, len, pi.pfi_UnitBase + off,
                   pi.pfi_UnitBase + off + len - 1);
        }
        if ((e = dev_cmd(PFL_CMD_CRC, off, NULL, len))) rc = fail("crc", e);
        else if (io->io_Actual != fc) { printf("MISMATCH: flash CRC32 %08lX, file %08lX\n", io->io_Actual, fc); rc = RETURN_ERROR; }
        else printf("verified: %lu bytes, CRC32 %08lX (file and flash)\n", len, fc);
        FreeMem(b, len);
    } else if (!stricmp(verb, "ERASE") && argc > 4) {
        len = strtoul(argv[4], NULL, 0);
        if ((e = dev_cmd(PFL_CMD_ERASE, off, NULL, len))) rc = fail("erase", e);
        else printf("erased unit %d 0x%06lX %lu bytes\n", unit, off, len);
    } else { printf("bad arguments\n"); rc = RETURN_WARN; }
    DateStamp(&d1);
    t0 = (ULONG)((d1.ds_Minute - d0.ds_Minute) * 3000 + d1.ds_Tick - d0.ds_Tick) * 20u;
    if (stricmp(verb, "INFO")) printf("time %lu ms\n", t0);
out:
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io); DeleteMsgPort(port);
    return rc;
}
