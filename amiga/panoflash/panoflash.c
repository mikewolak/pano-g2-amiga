/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * pano-g2-amiga: free software under the GNU General Public License, version 2 or any later version; the terms are
 * in LICENSES/GPL-2.0.txt.  This program contains a table derived from flashrom (GPL-2.0-or-later).
 */
/* panoflash.c -- panoflash.device (panoflash.h): the G2's configuration flash through the firmware's flash window on
 * HD0 (fw/amiga/pflash.h).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.  The device skeleton (romtag, vectors, open,
 * close, expunge) follows amiga/serial/g2serial.c.
 *
 * A request runs in the task that calls BeginIO: the task's own message port carries the scsi.device and
 * timer.device requests, and a semaphore keeps one request at a time on the window.  Every request is complete when
 * BeginIO returns (replied unless IOF_QUICK); AbortIO has nothing to abort.
 *
 * One firmware request moves at most the data window (64 KB): the device writes the data blocks, then the request
 * block, and reads the status block every 20 ms until the firmware reports the request done. */
#include <stddef.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <exec/resident.h>
#include <exec/devices.h>
#include <exec/semaphores.h>
#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include "panoflash.h"
#include "crc32.h"

#define PFL_VERSION   1
#define PFL_REVISION  0

/* the firmware's flash window (fw/amiga/pflash.h) */
#define W_REQ    144
#define W_STAT   145
#define W_DATA   256
#define W_BYTES  65536u
#define OP_INFO    1
#define OP_READ    2
#define OP_WRITE   3
#define OP_ERASE   4
#define OP_CRC     5
#define OP_PROGRAM 6

struct PFlash {
    struct Device         dev;
    struct Unit           unit[PFL_UNITS];
    BPTR                  seglist;
    struct SignalSemaphore lock;
    ULONG                 seq;
    UBYTE                *buf;            /* W_BYTES: the data window's copy */
    UBYTE                 blk[512];
    UBYTE                 st[512];        /* the last status block */
    ULONG                 base[PFL_UNITS], size[PFL_UNITS], sector;
};

/* one task's channel to scsi.device unit 0 and timer.device */
struct Chan {
    struct MsgPort     *port;
    struct IOStdReq    *io;
    struct timerequest *tr;
};

struct ExecBase *SysBase;

static const char dev_name[] = PANOFLASH_NAME;
static const char dev_id[]   = "panoflash 1.0 (G2 configuration flash)\r\n";
const char pfl_version[]     = "$VER: panoflash.device 1.0";

extern void dev_init_stub(void), dev_open_stub(void), dev_close_stub(void), dev_expunge_stub(void);
extern void dev_null_stub(void), dev_beginio_stub(void), dev_abortio_stub(void);

static const APTR dev_vectors[] = {
    (APTR)dev_open_stub, (APTR)dev_close_stub, (APTR)dev_expunge_stub, (APTR)dev_null_stub,
    (APTR)dev_beginio_stub, (APTR)dev_abortio_stub, (APTR)-1
};
static const APTR dev_inittab[4] = {
    (APTR)sizeof(struct PFlash), (APTR)dev_vectors, NULL, (APTR)dev_init_stub
};
const struct Resident pfl_romtag __attribute__((used)) = {
    RTC_MATCHWORD, (struct Resident *)&pfl_romtag, (APTR)(&pfl_romtag + 1), RTF_AUTOINIT,
    PFL_VERSION, NT_DEVICE, 0, (char *)dev_name, (char *)dev_id, (APTR)dev_inittab
};

/* ---- helpers ---------------------------------------------------------------------------------------------------- */
static void zero(void *p, ULONG n) { UBYTE *b = p; while (n--) *b++ = 0; }
static void copy(void *d, const void *s, ULONG n) { UBYTE *a = d; const UBYTE *b = s; while (n--) *a++ = *b++; }
static void fill(void *p, UBYTE v, ULONG n) { UBYTE *b = p; while (n--) *b++ = v; }
static ULONG be32(const UBYTE *p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }
static void be32_put(UBYTE *p, ULONG v) { p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v; }

static void chan_close(struct Chan *c)
{
    if (c->tr) { if (c->tr->tr_node.io_Device) CloseDevice((struct IORequest *)c->tr); DeleteIORequest((struct IORequest *)c->tr); }
    if (c->io) { if (c->io->io_Device) CloseDevice((struct IORequest *)c->io); DeleteIORequest((struct IORequest *)c->io); }
    if (c->port) DeleteMsgPort(c->port);
    c->tr = NULL; c->io = NULL; c->port = NULL;
}
static int chan_open(struct Chan *c)
{
    c->port = CreateMsgPort();
    c->io = c->port ? (struct IOStdReq *)CreateIORequest(c->port, sizeof(struct IOStdReq)) : NULL;
    c->tr = c->port ? (struct timerequest *)CreateIORequest(c->port, sizeof(struct timerequest)) : NULL;
    if (!c->io || !c->tr) { chan_close(c); return -1; }
    if (OpenDevice((CONST_STRPTR)"scsi.device", 0, (struct IORequest *)c->io, 0)) { c->io->io_Device = NULL; chan_close(c); return -1; }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)c->tr, 0)) { c->tr->tr_node.io_Device = NULL; chan_close(c); return -1; }
    return 0;
}
static void chan_wait(struct Chan *c, ULONG us)
{
    c->tr->tr_node.io_Command = TR_ADDREQUEST;
    c->tr->tr_time.tv_secs = us / 1000000u; c->tr->tr_time.tv_micro = us % 1000000u;
    DoIO((struct IORequest *)c->tr);
}
static int blk_io(struct Chan *c, UWORD cmd, ULONG block, APTR buf, ULONG len)
{
    c->io->io_Command = cmd; c->io->io_Data = buf; c->io->io_Length = len; c->io->io_Offset = block * 512u;
    return DoIO((struct IORequest *)c->io) ? -1 : 0;
}

/* one firmware request; the status block stays in g->st.  0 or an io_Error value */
static BYTE fw_req(struct PFlash *g, struct Chan *c, ULONG op, ULONG addr, ULONG len, ULONG crc)
{
    ULONG seq = ++g->seq, polls = 0, limit = (10u + (len >> 16)) * 50u;
    for (;;) {
        if (polls % 50u == 0) {                        /* the request, again every second until the firmware takes it */
            zero(g->blk, 512); copy(g->blk, "PFW1", 4);
            be32_put(g->blk + 4, seq); be32_put(g->blk + 8, op); be32_put(g->blk + 12, addr); be32_put(g->blk + 16, len);
            be32_put(g->blk + 20, crc);
            if (blk_io(c, CMD_WRITE, W_REQ, g->blk, 512)) return PFLERR_DISK;
        }
        if (blk_io(c, CMD_READ, W_STAT, g->st, 512)) return PFLERR_DISK;
        if (be32(g->st) != 0x50464131u) return PFLERR_NOWINDOW;          /* 'PFA1' */
        if (be32(g->st + 4) == seq && be32(g->st + 8) == 2u) return (BYTE)be32(g->st + 12);
        if (++polls > limit) return PFLERR_NOANSWER;
        chan_wait(c, 20000u);
    }
}

/* the map of the part, from an INFO request */
static BYTE fw_map(struct PFlash *g, struct Chan *c)
{
    BYTE e = fw_req(g, c, OP_INFO, 0, 0, 0);
    ULONG chip;
    if (e) return e;
    chip = be32(g->st + 32);
    if (!chip) return PFLERR_NOPART;
    g->sector = be32(g->st + 36);
    g->base[PFL_UNIT_CHIP] = 0;                  g->size[PFL_UNIT_CHIP] = chip;
    g->base[PFL_UNIT_MAIN] = be32(g->st + 40);   g->size[PFL_UNIT_MAIN] = be32(g->st + 44) - be32(g->st + 40);
    g->base[PFL_UNIT_FREE] = be32(g->st + 48);   g->size[PFL_UNIT_FREE] = be32(g->st + 52) - be32(g->st + 48);
    return 0;
}

/* ---- commands --------------------------------------------------------------------------------------------------- */
static BYTE do_read(struct PFlash *g, struct Chan *c, ULONG a, UBYTE *d, ULONG len, ULONG *actual)
{
    while (len) {
        ULONG n = len > W_BYTES ? W_BYTES : len;
        BYTE e = fw_req(g, c, OP_READ, a, n, 0);
        if (e) return e;
        if (blk_io(c, CMD_READ, W_DATA, g->buf, (n + 511u) & ~511u)) return PFLERR_DISK;
        if (crc32(g->buf, n) != be32(g->st + 20)) return PFLERR_DATACRC;
        copy(d, g->buf, n);
        d += n; a += n; len -= n; *actual += n;
    }
    return 0;
}

static BYTE do_write(struct PFlash *g, struct Chan *c, ULONG a, const UBYTE *s, ULONG len, ULONG *actual)
{
    while (len) {
        ULONG so = a & (g->sector - 1u), n = len, op = so ? OP_PROGRAM : OP_WRITE, k;
        BYTE e;
        if (n > W_BYTES) n = W_BYTES;
        if (n > g->sector - so) n = g->sector - so;
        copy(g->buf, s, n);
        k = (n + 511u) & ~511u;
        fill(g->buf + n, 0xFF, k - n);
        if (blk_io(c, CMD_WRITE, W_DATA, g->buf, k)) return PFLERR_DISK;
        if ((e = fw_req(g, c, op, a, n, crc32(g->buf, n)))) return e;
        s += n; a += n; len -= n; *actual += n;
    }
    return 0;
}

static void do_info(struct PFlash *g, struct IOStdReq *io, ULONG u)
{
    struct PanoFlashInfo pi;
    zero(&pi, sizeof pi);
    pi.pfi_RDID = be32(g->st + 24); pi.pfi_Status = be32(g->st + 28);
    pi.pfi_ChipSize = be32(g->st + 32); pi.pfi_SectorSize = be32(g->st + 36);
    pi.pfi_UnitBase = g->base[u]; pi.pfi_UnitSize = g->size[u];
    pi.pfi_Golden = be32(g->st + 92); pi.pfi_Main = be32(g->st + 40); pi.pfi_MainEnd = be32(g->st + 44); pi.pfi_Free = be32(g->st + 48);
    pi.pfi_FreeEnd = be32(g->st + 52); pi.pfi_Config = be32(g->st + 56);
    pi.pfi_Guard = be32(g->st + 60); pi.pfi_Refusals = be32(g->st + 64); pi.pfi_Passed = be32(g->st + 68);
    pi.pfi_Erased = be32(g->st + 72);
    copy(pi.pfi_Message, g->st + 96, sizeof pi.pfi_Message - 1);
    if (io->io_Length < sizeof pi) { io->io_Error = IOERR_BADLENGTH; return; }
    copy(io->io_Data, &pi, sizeof pi);
    io->io_Actual = sizeof pi;
}

static void run(struct PFlash *g, struct IOStdReq *io)
{
    ULONG u = (ULONG)(io->io_Unit - &g->unit[0]), off = io->io_Offset, len = io->io_Length, base, size;
    struct Chan c;
    io->io_Actual = 0;
    switch (io->io_Command) {
    case CMD_UPDATE: case CMD_CLEAR: case TD_MOTOR: case TD_CHANGESTATE: return;
    case TD_CHANGENUM: io->io_Actual = 1; return;
    case TD_PROTSTATUS: io->io_Actual = u == PFL_UNIT_CHIP; return;
    case CMD_READ: case CMD_WRITE: case TD_FORMAT: case PFL_CMD_ERASE: case PFL_CMD_CRC: case PFL_CMD_INFO:
    case TD_GETGEOMETRY: break;
    default: io->io_Error = IOERR_NOCMD; return;
    }
    zero(&c, sizeof c);
    if (chan_open(&c)) { io->io_Error = PFLERR_DISK; return; }
    ObtainSemaphore(&g->lock);
    if ((io->io_Error = fw_map(g, &c))) goto out;
    base = g->base[u]; size = g->size[u];
    switch (io->io_Command) {
    case PFL_CMD_INFO:
        do_info(g, io, u);
        break;
    case TD_GETGEOMETRY: {
        struct DriveGeometry *dg = (struct DriveGeometry *)io->io_Data;
        if (len < sizeof *dg) { io->io_Error = IOERR_BADLENGTH; break; }
        zero(dg, sizeof *dg);
        dg->dg_SectorSize = g->sector; dg->dg_TotalSectors = size / g->sector; dg->dg_Cylinders = size / g->sector;
        dg->dg_CylSectors = 1; dg->dg_Heads = 1; dg->dg_TrackSectors = 1; dg->dg_BufMemType = MEMF_PUBLIC;
        dg->dg_DeviceType = DG_DIRECT_ACCESS; dg->dg_Flags = 0;
        io->io_Actual = sizeof *dg;
        break;
    }
    case CMD_READ: case PFL_CMD_CRC:
        if (off > size || len > size - off) { io->io_Error = IOERR_BADADDRESS; break; }
        if (io->io_Command == CMD_READ) io->io_Error = do_read(g, &c, base + off, (UBYTE *)io->io_Data, len, &io->io_Actual);
        else if (len && !(io->io_Error = fw_req(g, &c, OP_CRC, base + off, len, io->io_Data ? *(ULONG *)io->io_Data : 0)))
            io->io_Actual = be32(g->st + 20);
        break;
    case CMD_WRITE: case TD_FORMAT: case PFL_CMD_ERASE:
        if (u == PFL_UNIT_CHIP) { io->io_Error = PFLERR_PROTECTED; break; }
        if (off > size || len > size - off || (off & (g->sector - 1u)) ||
            (io->io_Command == PFL_CMD_ERASE && (len & (g->sector - 1u)))) { io->io_Error = IOERR_BADADDRESS; break; }
        if (!len) break;
        if (io->io_Command == PFL_CMD_ERASE) { if (!(io->io_Error = fw_req(g, &c, OP_ERASE, base + off, len, 0))) io->io_Actual = len; }
        else io->io_Error = do_write(g, &c, base + off, (const UBYTE *)io->io_Data, len, &io->io_Actual);
        break;
    }
out:
    ReleaseSemaphore(&g->lock);
    chan_close(&c);
}

/* ---- device vectors --------------------------------------------------------------------------------------------- */
struct PFlash *dev_init(struct PFlash *g, BPTR seg, struct ExecBase *sb)
{
    SysBase = sb;
    g->seglist = seg;
    g->dev.dd_Library.lib_Node.ln_Type = NT_DEVICE;
    g->dev.dd_Library.lib_Node.ln_Name = (char *)dev_name;
    g->dev.dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    g->dev.dd_Library.lib_Version = PFL_VERSION;
    g->dev.dd_Library.lib_Revision = PFL_REVISION;
    g->dev.dd_Library.lib_IdString = (APTR)dev_id;
    InitSemaphore(&g->lock);
    return g;
}

static BPTR expunge(struct PFlash *g)
{
    BPTR seg;
    if (g->dev.dd_Library.lib_OpenCnt) { g->dev.dd_Library.lib_Flags |= LIBF_DELEXP; return 0; }
    seg = g->seglist;
    Remove(&g->dev.dd_Library.lib_Node);
    if (g->buf) FreeMem(g->buf, W_BYTES);
    FreeMem((UBYTE *)g - g->dev.dd_Library.lib_NegSize, g->dev.dd_Library.lib_NegSize + g->dev.dd_Library.lib_PosSize);
    return seg;
}

BPTR dev_expunge(struct PFlash *g) { return expunge(g); }

void dev_open(struct IOStdReq *io, ULONG unit, ULONG flags, struct PFlash *g)
{
    struct Chan c;
    BYTE e = 0;
    (void)flags;
    g->dev.dd_Library.lib_OpenCnt++;              /* held while the open waits, so no expunge runs under it */
    if (unit >= PFL_UNITS) e = IOERR_OPENFAIL;
    else if (!g->buf && !(g->buf = (UBYTE *)AllocMem(W_BYTES, MEMF_PUBLIC))) e = IOERR_OPENFAIL;
    else {
        if (!g->seq) g->seq = (ULONG)FindTask(NULL) ^ 0x50460000u;
        zero(&c, sizeof c);
        if (chan_open(&c)) e = PFLERR_DISK;
        else {
            ObtainSemaphore(&g->lock);
            e = fw_map(g, &c);
            ReleaseSemaphore(&g->lock);
            chan_close(&c);
        }
    }
    if (e) {
        g->dev.dd_Library.lib_OpenCnt--;
        io->io_Error = e; io->io_Device = NULL;
        return;
    }
    g->dev.dd_Library.lib_Flags &= ~LIBF_DELEXP;
    g->unit[unit].unit_OpenCnt++;
    io->io_Error = 0;
    io->io_Device = &g->dev;
    io->io_Unit = &g->unit[unit];
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
}

BPTR dev_close(struct IOStdReq *io, struct PFlash *g)
{
    ((struct Unit *)io->io_Unit)->unit_OpenCnt--;
    io->io_Device = (APTR)-1;
    io->io_Unit = (APTR)-1;
    if (--g->dev.dd_Library.lib_OpenCnt == 0 && (g->dev.dd_Library.lib_Flags & LIBF_DELEXP)) return expunge(g);
    return 0;
}

void dev_beginio(struct IOStdReq *io, struct PFlash *g)
{
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->io_Error = 0;
    run(g, io);
    if (!(io->io_Flags & IOF_QUICK)) ReplyMsg(&io->io_Message);
}

LONG dev_abortio(struct IOStdReq *io, struct PFlash *g)
{
    (void)io; (void)g;
    return 0;
}
