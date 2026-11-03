/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/afs.c -- Amiga file systems (OFS, FFS) inside hard-disk and floppy images, on ADFlib (adflib/, LGPL-2.1)
 *.
 *
 * THE DEVICE.  ADFlib is given one device per mount through the driver "pano": block 0 of that device is the first
 * block of the partition, its length the partition's, so ADFlib mounts it as an unpartitioned hard file (class
 * HARDFILE, or FLOP for 1,760 and 3,520 blocks).  The partition comes from the image's RigidDiskBlock: RDSK at block
 * 0 (rdb_PartitionList, long 7), the first PART block's DosEnvVec (from long 32): de_Surfaces 35, de_BlocksPerTrack
 * 37, de_LowCyl 41, de_HighCyl 42.  An image without RDSK at block 0 is mounted whole.  ADFlib's own RigidDiskBlock
 * code is not used.
 *
 * PATHS.  '/' separates names; a leading '/' or none both start at the root.  Names are matched by ADFlib as AmigaDOS
 * matches them (case-insensitive, international upper case for DOS\2 and up). */
#include <stdlib.h>
#include <string.h>
#include "afs.h"
#include "adflib.h"
#include "adf_dev_driver.h"
#include "adf_dev_drivers.h"
#include "adf_env.h"
#include "adf_raw.h"
#include "adf_util.h"
#include "adf_bitm.h"

extern const struct AdfDeviceDriver adfDeviceDriverPano;

struct afs_vol {
    struct afs_io      io;
    uint32_t           first, blocks;
    int                rdb;
    char               part[32];
    struct AdfDevice  *dev;
    struct AdfVolume  *vol;
};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* ---- driver "pano": the partition of one struct afs_vol ------------------------------------------------------ */
static struct afs_vol *s_opening;                   /* the mount in progress (openDev takes only a name) */

static struct AdfDevice *pano_open(const char * const name, const AdfAccessMode mode)
{
    struct afs_vol *v = s_opening;
    struct AdfDevice *dev;
    if (v == NULL) return NULL;
    dev = (struct AdfDevice *)calloc(1, sizeof *dev);
    if (dev == NULL) return NULL;
    dev->name = strdup(name);
    dev->readOnly = (mode != ADF_ACCESS_MODE_READWRITE);
    dev->sizeBlocks = v->blocks;
    dev->geometry.blockSize = ADF_DEV_BLOCK_SIZE;
    dev->drv = &adfDeviceDriverPano;
    dev->drvData = v;
    return dev;
}
static ADF_RETCODE pano_close(struct AdfDevice * const dev)
{
    free(dev->name);
    free(dev);
    return ADF_RC_OK;
}
static ADF_RETCODE pano_rd(const struct AdfDevice * const dev, const uint32_t blk, const uint32_t n, uint8_t * const buf)
{
    const struct afs_vol *v = (const struct afs_vol *)dev->drvData;
    if (blk + n > v->blocks || blk + n < blk) return ADF_RC_ERROR;
    return v->io.rd(v->io.ctx, v->first + blk, n, buf) ? ADF_RC_ERROR : ADF_RC_OK;
}
static ADF_RETCODE pano_wr(const struct AdfDevice * const dev, const uint32_t blk, const uint32_t n,
                           const uint8_t * const buf)
{
    const struct afs_vol *v = (const struct afs_vol *)dev->drvData;
    if (dev->readOnly || v->io.wr == NULL) return ADF_RC_ERROR;
    if (blk + n > v->blocks || blk + n < blk) return ADF_RC_ERROR;
    return v->io.wr(v->io.ctx, v->first + blk, n, buf) ? ADF_RC_ERROR : ADF_RC_OK;
}
static bool pano_native(void) { return false; }

const struct AdfDeviceDriver adfDeviceDriverPano = {
    .name = "pano", .data = NULL, .createDev = NULL, .openDev = pano_open, .closeDev = pano_close,
    .readSectors = pano_rd, .writeSectors = pano_wr, .isNative = pano_native, .isDevice = NULL
};

#ifdef AFS_VERBOSE                                  /* ADFlib's messages on stderr (host builds) */
#include <stdio.h>
#include <stdarg.h>
static void quiet(const char * const fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr); }
#else
static void quiet(const char * const fmt, ...) { (void)fmt; }
#endif

static void no_notify(ADF_SECTNUM s, int t) { (void)s; (void)t; }
static void no_rwh(ADF_SECTNUM p, ADF_SECTNUM l, bool w) { (void)p; (void)l; (void)w; }
static void no_progress(int pc) { (void)pc; }

/* the settings adfEnvInitDefault makes, with message functions that print nothing (adfEnvInitDefault itself would
 * link ADFlib's stdio printers) */
int afs_init(void)
{
    adfEnv.vFct = quiet; adfEnv.wFct = quiet; adfEnv.eFct = quiet;
    adfEnv.notifyFct = no_notify; adfEnv.rwhAccess = no_rwh; adfEnv.progressBar = no_progress;
    adfEnv.useNotify = false; adfEnv.useRWAccess = false; adfEnv.useProgressBar = false; adfEnv.useDirCache = false;
    adfEnv.ignoreChecksumErrors = false; adfEnv.quiet = false;
    return adfAddDeviceDriver(&adfDeviceDriverPano) == ADF_RC_OK ? AFS_OK : AFS_E_LIB;
}

#ifdef PANO_G2
/* ADFlib's assertions: the C library's __assert_func prints through stdio and aborts; this one returns to the caller's
 * afs_abort_jb (afs.h) with the place recorded, or parks when no caller is armed */
jmp_buf     afs_abort_jb;
int         afs_abort_armed;
const char *afs_abort_file;
int         afs_abort_line;
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)func; (void)expr;
    afs_abort_file = file; afs_abort_line = line;
    if (afs_abort_armed) { afs_abort_armed = 0; longjmp(afs_abort_jb, 1); }
    for (;;) { }
}

/* ADFlib dates new entries with localtime() (adf_util.c adfGiveCurrentTime).  The firmware has no time zone: this
 * definition takes the place of the C library's, whose time-zone parser (tzset, getenv, sscanf) is not linked. */
#include <time.h>
struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    return gmtime_r(t, &tm);
}
#endif

/* ---- partition from the RigidDiskBlock ------------------------------------------------------------------------ */
static int find_part(struct afs_vol *v)
{
    uint8_t b[512];
    uint32_t pl, surf, bpt, lo, hi;
    v->first = 0; v->blocks = v->io.blocks; v->rdb = 0; v->part[0] = 0;
    if (v->io.rd(v->io.ctx, 0, 1, b)) return AFS_E_IO;
    if (memcmp(b, "RDSK", 4) != 0) return AFS_OK;
    pl = be32(b + 7 * 4);
    if (pl == 0xFFFFFFFFu || pl >= v->io.blocks) return AFS_E_RDB;
    if (v->io.rd(v->io.ctx, pl, 1, b)) return AFS_E_IO;
    if (memcmp(b, "PART", 4) != 0) return AFS_E_RDB;
    if (b[36] > 0 && b[36] < sizeof v->part) { memcpy(v->part, b + 37, b[36]); v->part[b[36]] = 0; }
    surf = be32(b + 35 * 4); bpt = be32(b + 37 * 4); lo = be32(b + 41 * 4); hi = be32(b + 42 * 4);
    if (surf == 0 || bpt == 0 || hi < lo) return AFS_E_RDB;
    v->first = lo * surf * bpt;
    v->blocks = (hi - lo + 1u) * surf * bpt;
    if (v->first + v->blocks > v->io.blocks) return AFS_E_RDB;
    v->rdb = 1;
    return AFS_OK;
}

int afs_mount(const struct afs_io *io, int write, struct afs_vol **out)
{
    struct afs_vol *v;
    int rc;
    AdfAccessMode m = write ? ADF_ACCESS_MODE_READWRITE : ADF_ACCESS_MODE_READONLY;
    *out = NULL;
    v = (struct afs_vol *)calloc(1, sizeof *v);
    if (v == NULL) return AFS_E_MEM;
    v->io = *io;
    if ((rc = find_part(v)) != AFS_OK) { free(v); return rc; }
    s_opening = v;
    v->dev = adfDevOpenWithDriver("pano", "image", m);
    s_opening = NULL;
    if (v->dev == NULL) { free(v); return AFS_E_LIB; }
    if (adfDevMount(v->dev) != ADF_RC_OK) { adfDevClose(v->dev); free(v); return AFS_E_NOFS; }
    v->vol = adfVolMount(v->dev, 0, m);
    if (v->vol == NULL) { adfDevClose(v->dev); free(v); return AFS_E_NOFS; }
    if (write && !adfVolBitmapIsMarkedValid(v->vol)) { afs_umount(v); return AFS_E_BITMAP; }
    *out = v;
    return AFS_OK;
}

/* a new, empty volume over the partition (or the whole image without a RigidDiskBlock): boot block, root block and
 * bitmap, as AmigaDOS Format with QUICK writes them; type the DosType's last byte (1 FFS, 3 FFS international) */
int afs_format(const struct afs_io *io, const char *name, unsigned int type)
{
    struct afs_vol *v;
    struct AdfVolume *vol;
    uint32_t cyl;
    int rc;
    if (name == NULL || name[0] == 0 || strlen(name) > ADF_MAX_NAME_LEN || strchr(name, ':') || strchr(name, '/'))
        return AFS_E_NAME;
    if (type > 7u || io->wr == NULL) return io->wr == NULL ? AFS_E_RO : AFS_E_NAME;
    v = (struct afs_vol *)calloc(1, sizeof *v);
    if (v == NULL) return AFS_E_MEM;
    v->io = *io;
    if ((rc = find_part(v)) != AFS_OK) { free(v); return rc; }
    s_opening = v;
    v->dev = adfDevOpenWithDriver("pano", "image", ADF_ACCESS_MODE_READWRITE);
    s_opening = NULL;
    if (v->dev == NULL) { free(v); return AFS_E_LIB; }
    cyl = v->dev->geometry.heads * v->dev->geometry.sectors;
    vol = cyl ? adfVolCreate(v->dev, 0, v->dev->sizeBlocks / cyl, name, (uint8_t)type) : NULL;
    rc = vol ? AFS_OK : AFS_E_IO;
    if (vol) { free(vol->volName); free(vol); }
    adfDevClose(v->dev);
    free(v);
    return rc;
}

void afs_umount(struct afs_vol *v)
{
    if (v == NULL) return;
    if (v->vol) adfVolUnMount(v->vol);
    if (v->dev) adfDevClose(v->dev);
    free(v);
}

static void stamp(struct afs_date *d, int32_t days, int32_t mins, int32_t ticks)
{
    adfDays2Date(days, &d->year, &d->month, &d->day);
    d->hour = (int)(mins / 60); d->min = (int)(mins % 60); d->sec = (int)(ticks / 50);
}

int afs_info(struct afs_vol *v, struct afs_info *inf)
{
    struct AdfRootBlock root;
    uint8_t t;
    memset(inf, 0, sizeof *inf);
    if (adfReadRootBlock(v->vol, (uint32_t)v->vol->rootBlock, &root) != ADF_RC_OK) return AFS_E_IO;
    if (v->vol->volName) strncpy(inf->name, v->vol->volName, sizeof inf->name - 1u);
    t = v->vol->fs.type;
    inf->dostype = 0x444F5300u | t;
    strcpy(inf->fs, (t & 1u) ? "FFS" : "OFS");
    if (t & 4u) strcat(inf->fs, "-DC"); else if (t & 2u) strcat(inf->fs, "-INTL");
    inf->first = v->first; inf->blocks = v->blocks; inf->rdb = v->rdb;
    strcpy(inf->part, v->part);
    inf->free = adfCountFreeBlocks(v->vol);
    inf->bitmap_valid = (root.bmFlag == ADF_BM_VALID);
    if (root.cDays || root.cMins || root.cTicks) stamp(&inf->created, root.cDays, root.cMins, root.cTicks);
    else stamp(&inf->created, root.coDays, root.coMins, root.coTicks);   /* adfVolCreate fills only the second */
    stamp(&inf->changed, root.days, root.mins, root.ticks);
    return AFS_OK;
}

/* ---- paths ------------------------------------------------------------------------------------------------------ */
/* walks every name of path but the last into the current directory; the last name is copied to leaf ("" for a path
 * that names the root) */
static int walk(struct afs_vol *v, const char *path, char *leaf)
{
    char name[ADF_MAX_NAME_LEN + 1];
    const char *p = path, *e;
    size_t n;
    if (adfToRootDir(v->vol) != ADF_RC_OK) return AFS_E_IO;
    leaf[0] = 0;
    while (*p == '/') p++;
    while (*p) {
        for (e = p; *e && *e != '/'; e++) { }
        n = (size_t)(e - p);
        if (n == 0 || n > ADF_MAX_NAME_LEN) return AFS_E_NAME;
        memcpy(name, p, n); name[n] = 0;
        while (*e == '/') e++;
        if (*e == 0) { strcpy(leaf, name); return AFS_OK; }
        if (adfChangeDir(v->vol, name) != ADF_RC_OK) return AFS_E_NOTDIR;
        p = e;
    }
    return AFS_OK;
}

static void to_ent(const struct AdfEntry *a, struct afs_ent *e)
{
    memset(e, 0, sizeof *e);
    e->name = a->name;
    e->dir = (a->type == ADF_ST_DIR || a->type == ADF_ST_ROOT) ? 1 :
             (a->type == ADF_ST_FILE) ? 0 : 2;
    e->size = a->size;
    e->prot = (uint32_t)a->access;
    e->date.year = a->year; e->date.month = a->month; e->date.day = a->days;
    e->date.hour = a->hour; e->date.min = a->mins; e->date.sec = a->secs;
    e->comment = a->comment ? a->comment : "";
    e->block = (uint32_t)a->sector;
}

int afs_ls(struct afs_vol *v, const char *path, afs_ls_fn fn, void *u)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    struct AdfList *list, *c;
    struct afs_ent e;
    int rc = walk(v, path, leaf);
    if (rc != AFS_OK) return rc;
    if (leaf[0] && adfChangeDir(v->vol, leaf) != ADF_RC_OK) return AFS_E_NOTDIR;
    list = adfGetDirEnt(v->vol, v->vol->curDirPtr);
    for (c = list; c != NULL; c = c->next) {
        if (c->content == NULL) continue;
        to_ent((const struct AdfEntry *)c->content, &e);
        fn(u, &e);
    }
    if (list) adfFreeDirList(list);
    return AFS_OK;
}

int afs_stat(struct afs_vol *v, const char *path, afs_ls_fn fn, void *u)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    struct AdfEntry a;
    struct afs_ent e;
    int rc = walk(v, path, leaf);
    if (rc != AFS_OK) return rc;
    if (leaf[0] == 0) return AFS_E_NAME;
    memset(&a, 0, sizeof a);
    if (adfGetEntry(v->vol, v->vol->curDirPtr, leaf, &a) != ADF_RC_OK) return AFS_E_NOTFOUND;
    to_ent(&a, &e);
    fn(u, &e);
    free(a.name); free(a.comment);
    return AFS_OK;
}

/* ---- files ------------------------------------------------------------------------------------------------------ */
#define AFS_CHUNK 4096u

int afs_get(struct afs_vol *v, const char *path, afs_dst_fn dst, void *u, uint32_t *size)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    struct AdfFile *f;
    uint8_t *buf;
    uint32_t n, total = 0;
    int rc = walk(v, path, leaf);
    if (rc != AFS_OK) return rc;
    if (leaf[0] == 0) return AFS_E_NAME;
    if (adfGetEntryBlockNum(v->vol, v->vol->curDirPtr, leaf) <= 0) return AFS_E_NOTFOUND;
    f = adfFileOpen(v->vol, leaf, ADF_FILE_MODE_READ);
    if (f == NULL) return AFS_E_NOTFOUND;
    buf = (uint8_t *)malloc(AFS_CHUNK);
    if (buf == NULL) { adfFileClose(f); return AFS_E_MEM; }
    while (!adfFileAtEOF(f)) {
        n = adfFileRead(f, AFS_CHUNK, buf);
        if (n == 0) { rc = AFS_E_IO; break; }
        if (dst(u, buf, n)) { rc = AFS_E_SRC; break; }
        total += n;
    }
    free(buf);
    adfFileClose(f);
    if (size) *size = total;
    return rc;
}

/* an existing file of the same name is replaced; a directory of that name is refused */
int afs_put(struct afs_vol *v, const char *path, afs_src_fn src, void *u)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    struct AdfEntry a;
    struct AdfFile *f;
    uint8_t *buf;
    int n, rc;
    if (v->vol->dev->readOnly) return AFS_E_RO;
    if ((rc = walk(v, path, leaf)) != AFS_OK) return rc;
    if (leaf[0] == 0) return AFS_E_NAME;
    memset(&a, 0, sizeof a);
    if (adfGetEntry(v->vol, v->vol->curDirPtr, leaf, &a) == ADF_RC_OK) {
        int isdir = (a.type == ADF_ST_DIR);
        free(a.name); free(a.comment);
        if (isdir) return AFS_E_EXISTS;
        if (adfRemoveEntry(v->vol, v->vol->curDirPtr, leaf) != ADF_RC_OK) return AFS_E_LIB;
    }
    f = adfFileOpen(v->vol, leaf, ADF_FILE_MODE_WRITE);
    if (f == NULL) return AFS_E_FULL;
    buf = (uint8_t *)malloc(AFS_CHUNK);
    if (buf == NULL) { adfFileClose(f); return AFS_E_MEM; }
    rc = AFS_OK;
    for (;;) {
        n = src(u, buf, AFS_CHUNK);
        if (n < 0) { rc = AFS_E_SRC; break; }
        if (n == 0) break;
        if (adfFileWrite(f, (uint32_t)n, buf) != (uint32_t)n) { rc = AFS_E_FULL; break; }
    }
    free(buf);
    adfFileClose(f);
    if (rc != AFS_OK) (void)adfRemoveEntry(v->vol, v->vol->curDirPtr, leaf);
    return rc;
}

int afs_mkdir(struct afs_vol *v, const char *path)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    int rc;
    if (v->vol->dev->readOnly) return AFS_E_RO;
    if ((rc = walk(v, path, leaf)) != AFS_OK) return rc;
    if (leaf[0] == 0) return AFS_E_NAME;
    if (adfGetEntryBlockNum(v->vol, v->vol->curDirPtr, leaf) > 0) return AFS_E_EXISTS;
    return adfCreateDir(v->vol, v->vol->curDirPtr, leaf) == ADF_RC_OK ? AFS_OK : AFS_E_FULL;
}

int afs_rm(struct afs_vol *v, const char *path)
{
    char leaf[ADF_MAX_NAME_LEN + 1];
    struct AdfEntry a;
    int rc;
    if (v->vol->dev->readOnly) return AFS_E_RO;
    if ((rc = walk(v, path, leaf)) != AFS_OK) return rc;
    if (leaf[0] == 0) return AFS_E_NAME;
    memset(&a, 0, sizeof a);
    if (adfGetEntry(v->vol, v->vol->curDirPtr, leaf, &a) != ADF_RC_OK) return AFS_E_NOTFOUND;
    free(a.name); free(a.comment);
    if (a.type == ADF_ST_DIR) {
        ADF_SECTNUM here = v->vol->curDirPtr;
        struct AdfList *l;
        if (adfChangeDir(v->vol, leaf) != ADF_RC_OK) return AFS_E_IO;
        l = adfGetDirEnt(v->vol, v->vol->curDirPtr);
        v->vol->curDirPtr = here;
        if (l) { adfFreeDirList(l); return AFS_E_NOTEMPTY; }
    }
    return adfRemoveEntry(v->vol, v->vol->curDirPtr, leaf) == ADF_RC_OK ? AFS_OK : AFS_E_LIB;
}

int afs_mv(struct afs_vol *v, const char *from, const char *to)
{
    char ol[ADF_MAX_NAME_LEN + 1], nl[ADF_MAX_NAME_LEN + 1];
    ADF_SECTNUM od;
    int rc;
    if (v->vol->dev->readOnly) return AFS_E_RO;
    if ((rc = walk(v, from, ol)) != AFS_OK) return rc;
    if (ol[0] == 0) return AFS_E_NAME;
    od = v->vol->curDirPtr;
    if (adfGetEntryBlockNum(v->vol, od, ol) <= 0) return AFS_E_NOTFOUND;
    if ((rc = walk(v, to, nl)) != AFS_OK) return rc;
    if (nl[0] == 0) return AFS_E_NAME;
    if (adfGetEntryBlockNum(v->vol, v->vol->curDirPtr, nl) > 0) return AFS_E_EXISTS;
    return adfRenameEntry(v->vol, od, ol, v->vol->curDirPtr, nl) == ADF_RC_OK ? AFS_OK : AFS_E_LIB;
}

/* ---- ustar -------------------------------------------------------------------------------------------------------
 * Each 512-byte header: name (0, 100 bytes), size (124, 12 octal digits), type (156: '0' or NUL a file, '5' a
 * directory), and with "ustar" at 257 a prefix (345, 155 bytes); the data follows in 512-byte blocks. */
struct afs_lim { afs_src_fn rd; void *u; uint32_t left; };
static int lim_rd(void *p, uint8_t *buf, uint32_t n)
{
    struct afs_lim *l = (struct afs_lim *)p; int r;
    if (n > l->left) n = l->left;
    if (n == 0u) return 0;
    r = l->rd(l->u, buf, n);
    if (r != (int)n) return -1;
    l->left -= n;
    return r;
}
static uint32_t tar_octal(const uint8_t *p, unsigned int n)
{
    uint32_t v = 0; unsigned int i;
    for (i = 0; i < n && p[i] == ' '; i++) { }
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8u + (uint32_t)(p[i] - '0');
    return v;
}
static void mkdirs(struct afs_vol *v, char *p)
{
    char *q;
    for (q = p; *q; q++) if (*q == '/') { *q = 0; (void)afs_mkdir(v, p); *q = '/'; }
}
int afs_untar(struct afs_vol *v, afs_src_fn rd, afs_skip_fn sk, void *u, const char *base, unsigned int *nf, unsigned int *nd)
{
    static uint8_t h[512];
    static char path[600];
    int rc;
    *nf = 0u; *nd = 0u;
    for (;;) {
        uint32_t size, used = 0u, skip;
        size_t bl = 0;
        if (rd(u, h, 512u) != 512) return AFS_E_SRC;
        if (h[0] == 0) return AFS_OK;                       /* the end-of-archive block */
        size = tar_octal(h + 124, 12u);
        path[0] = 0;
        if (base[0]) { strcpy(path, base); bl = strlen(path); if (path[bl - 1u] != '/') { path[bl++] = '/'; path[bl] = 0; } }
        if (!memcmp(h + 257, "ustar", 5) && h[345]) { strncat(path, (const char *)h + 345, 155); strcat(path, "/"); }
        strncat(path, (const char *)h, 100);
        while (path[0] == '.' && path[1] == '/') memmove(path, path + 2, strlen(path + 2) + 1u);
        bl = strlen(path); while (bl && path[bl - 1u] == '/') path[--bl] = 0;
        if (h[156] == '5') {
            if (bl) { strcat(path, "/"); mkdirs(v, path); (*nd)++; }
        } else if ((h[156] == '0' || h[156] == 0) && bl) {
            struct afs_lim l = { rd, u, size };
            mkdirs(v, path);
            rc = afs_put(v, path, lim_rd, &l);
            if (rc != AFS_OK) return rc;
            (*nf)++; used = size;
        }
        skip = ((size + 511u) & ~511u) - used;              /* the block padding, or a skipped entry's data */
        if (skip && sk(u, skip) != 0) return AFS_E_SRC;
    }
}

const char *afs_strerror(int rc)
{
    switch (rc) {
    case AFS_OK:          return "ok";
    case AFS_E_IO:        return "image read or write failed";
    case AFS_E_NOFS:      return "no OFS or FFS volume";
    case AFS_E_RDB:       return "RigidDiskBlock or PART block not usable";
    case AFS_E_NOTFOUND:  return "not found";
    case AFS_E_EXISTS:    return "exists";
    case AFS_E_NOTDIR:    return "not a directory";
    case AFS_E_NOTEMPTY:  return "directory not empty";
    case AFS_E_FULL:      return "volume full or entry not created";
    case AFS_E_RO:        return "mounted read-only";
    case AFS_E_BITMAP:    return "bitmap marked not valid: validate the volume on the Amiga first";
    case AFS_E_NAME:      return "bad name";
    case AFS_E_MEM:       return "out of memory";
    case AFS_E_SRC:       return "source or destination failed";
    default:              return "file-system library error";
    }
}
