/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/afs.h -- Amiga file systems (OFS, FFS) inside hard-disk and floppy images, on ADFlib (adflib/, LGPL-2.1)
 *.  The image is reached through struct afs_io only, so the same code runs in the firmware (the
 * image layer of fw/amiga/amiga_main.c) and on a host (tools/afstool, image files). */
#ifndef AFS_H
#define AFS_H
#include <stdint.h>
#include <stddef.h>

/* the whole image, in 512-byte blocks; rd and wr return 0 on success */
struct afs_io {
    int      (*rd)(void *ctx, uint32_t blk, uint32_t n, uint8_t *buf);
    int      (*wr)(void *ctx, uint32_t blk, uint32_t n, const uint8_t *buf);
    void     *ctx;
    uint32_t blocks;
};

enum {
    AFS_OK = 0, AFS_E_IO = -1, AFS_E_NOFS = -2, AFS_E_RDB = -3, AFS_E_NOTFOUND = -4, AFS_E_EXISTS = -5,
    AFS_E_NOTDIR = -6, AFS_E_NOTEMPTY = -7, AFS_E_FULL = -8, AFS_E_RO = -9, AFS_E_BITMAP = -10, AFS_E_NAME = -11,
    AFS_E_MEM = -12, AFS_E_SRC = -13, AFS_E_LIB = -14
};

struct afs_vol;

struct afs_date { int year, month, day, hour, min, sec; };

struct afs_info {
    char     name[32];                /* volume name */
    char     fs[12];                  /* "OFS", "FFS", with "-INTL", "-DC" from the DosType */
    uint32_t dostype;                 /* 'DOS\0' .. 'DOS\7' */
    uint32_t first, blocks;           /* partition: first block in the image, length in blocks */
    uint32_t free;                    /* free blocks from the bitmap */
    int      bitmap_valid;            /* root block bm_flag */
    int      rdb;                     /* 1: partition from a RigidDiskBlock, 0: unpartitioned image */
    char     part[32];                /* partition (drive) name from the PART block, "" without RigidDiskBlock */
    struct afs_date created, changed; /* volume creation, last change of the volume */
};

struct afs_ent {
    const char *name;
    int         dir;                  /* 1 directory, 0 file, 2 link */
    uint32_t    size;
    uint32_t    prot;                 /* protection bits as stored (HSPARWED, RWED inverted) */
    struct afs_date date;
    const char *comment;
    uint32_t    block;                /* header block */
};

typedef void (*afs_ls_fn)(void *u, const struct afs_ent *e);
typedef int  (*afs_src_fn)(void *u, uint8_t *buf, uint32_t n);         /* bytes read, < 0 on error */
typedef int  (*afs_dst_fn)(void *u, const uint8_t *buf, uint32_t n);   /* 0 on success */

int  afs_init(void);
/* mounts the first partition of a RigidDiskBlock image, or the whole image without one; write 0 read-only.
 * Writing is refused (AFS_E_BITMAP) on a volume whose bitmap is marked not valid. */
int  afs_mount(const struct afs_io *io, int write, struct afs_vol **out);
int  afs_format(const struct afs_io *io, const char *name, unsigned int type);
void afs_umount(struct afs_vol *v);
int  afs_info(struct afs_vol *v, struct afs_info *inf);
int  afs_ls(struct afs_vol *v, const char *path, afs_ls_fn fn, void *u);
int  afs_stat(struct afs_vol *v, const char *path, afs_ls_fn fn, void *u);
int  afs_get(struct afs_vol *v, const char *path, afs_dst_fn dst, void *u, uint32_t *size);
int  afs_put(struct afs_vol *v, const char *path, afs_src_fn src, void *u);
int  afs_mkdir(struct afs_vol *v, const char *path);
int  afs_rm(struct afs_vol *v, const char *path);
int  afs_mv(struct afs_vol *v, const char *from, const char *to);
/* a POSIX ustar archive unpacked under base ("" the root): rd reads exactly n bytes (bytes read, < 0 on error), sk
 * skips n; directories and regular files are made (each file's directories first), other entries skipped; *nf, *nd
 * count them.  The first error ends it. */
typedef int (*afs_skip_fn)(void *u, uint32_t n);
int  afs_untar(struct afs_vol *v, afs_src_fn rd, afs_skip_fn sk, void *u, const char *base, unsigned int *nf, unsigned int *nd);
const char *afs_strerror(int rc);

#ifdef PANO_G2
/* firmware: an assertion inside ADFlib returns here (setjmp(afs_abort_jb) != 0 with afs_abort_armed set before the
 * call); the volume may be partly written and the library's allocations of that call are not freed */
#include <setjmp.h>
extern jmp_buf     afs_abort_jb;
extern int         afs_abort_armed;
extern const char *afs_abort_file;
extern int         afs_abort_line;
#endif

#endif
