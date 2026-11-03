/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/share.c -- the file server of the Amiga's SHARE: volume: a directory of the stick as an AmigaDOS file
 * system.  The requests and responses are those of a314fs (Niklas Ekström, a314 repository Software/a314fs, CC0-1.0):
 * a314fs.py's process_* functions in C on FatFs; amiga/share/share-handler.c on the Amiga sends them through blocks
 * of HD0 (fw/amiga/amiga_main.c THE SHARE BLOCKS).
 *
 * Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
 *
 * A request starts with its 16-bit action (a314fs messages.h without has_response); its response starts with
 * success and error_code; every field is big-endian.  Differences from a314fs.py: names are matched by FatFs
 * (case-insensitive) instead of a directory listing; protection bits and comments are not stored (FAT has no place for
 * them and a314fs's ":a314" side files are not valid FAT names): SET_PROTECT and SET_COMMENT succeed and change
 * nothing, every object reads protection 0 and no comment; SEEK outside the file fails with ERROR_SEEK_ERROR; a lock
 * or file handle the server does not hold fails with ERROR_INVALID_LOCK; a file in use as a drive image cannot be
 * opened, deleted or renamed (ERROR_OBJECT_IN_USE). */
#include <stdint.h>
#include <string.h>
#include "ff.h"
#include "share.h"

#define ACTION_LOCATE_OBJECT  8
#define ACTION_FREE_LOCK      15
#define ACTION_DELETE_OBJECT  16
#define ACTION_RENAME_OBJECT  17
#define ACTION_COPY_DIR       19
#define ACTION_SET_PROTECT    21
#define ACTION_CREATE_DIR     22
#define ACTION_EXAMINE_OBJECT 23
#define ACTION_EXAMINE_NEXT   24
#define ACTION_DISK_INFO      25
#define ACTION_INFO           26
#define ACTION_SET_COMMENT    28
#define ACTION_PARENT         29
#define ACTION_SAME_LOCK      40
#define ACTION_READ           'R'
#define ACTION_WRITE          'W'
#define ACTION_FINDUPDATE     1004
#define ACTION_FINDINPUT      1005
#define ACTION_FINDOUTPUT     1006
#define ACTION_END            1007
#define ACTION_SEEK           1008
#define ACTION_EXAMINE_FH     1034
#define ACTION_UNSUPPORTED    65535

#define ERROR_OBJECT_IN_USE         202
#define ERROR_OBJECT_EXISTS         203
#define ERROR_OBJECT_NOT_FOUND      205
#define ERROR_ACTION_NOT_KNOWN      209
#define ERROR_INVALID_LOCK          211
#define ERROR_OBJECT_WRONG_TYPE     212
#define ERROR_DISK_FULL             221
#define ERROR_DIRECTORY_NOT_EMPTY   216
#define ERROR_SEEK_ERROR            219
#define ERROR_NO_MORE_ENTRIES       232

#define SHARED_LOCK   (-2)
#define ST_USERDIR    2
#define ST_FILE       (-3)
#define OFFSET_BEGINNING (-1)
#define OFFSET_END       1
#define LOCK_SAME        0
#define LOCK_SAME_VOLUME 1
#define LOCK_DIFFERENT   (-1)

#define NLOCKS  48u
#define NFILES  8u
#define PATHMAX 256u

static struct { uint32_t key; char path[PATHMAX]; DIR dir; unsigned int dir_open; } s_lk[NLOCKS];
static struct { uint32_t fp; char path[PATHMAX]; FIL f; } s_fh[NFILES];
static uint32_t s_next_key = 1u, s_next_fp = 1u;
static char s_root[PATHMAX] = "/amiga/share";
static const char *s_vol = "USBStick";
static int (*s_busy)(const char *path);
uint8_t share_data[SHARE_DATA_BYTES];

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static unsigned int fail(uint8_t *res, unsigned int err) { wr16(res, 0u); wr16(res + 2, err); return 4u; }
static unsigned int ok_key(uint8_t *res, uint32_t key) { wr16(res, 1u); wr16(res + 2, 0u); wr32(res + 4, key); return 8u; }
static unsigned int ok0(uint8_t *res) { wr16(res, 1u); wr16(res + 2, 0u); return 4u; }
static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; }
    return *a == *b;
}

void share_init(const char *root, int (*busy)(const char *path))
{
    unsigned int i;
    if (root && *root) { strncpy(s_root, root, PATHMAX - 1u); s_root[PATHMAX - 1u] = 0; }
    { size_t n = strlen(s_root); while (n > 1u && s_root[n - 1u] == '/') s_root[--n] = 0; }
    s_busy = busy;
    for (i = 0; i < NLOCKS; i++) { if (s_lk[i].key && s_lk[i].dir_open) f_closedir(&s_lk[i].dir); s_lk[i].key = 0u; s_lk[i].dir_open = 0u; }
    for (i = 0; i < NFILES; i++) { if (s_fh[i].fp) f_close(&s_fh[i].f); s_fh[i].fp = 0u; }
    (void)f_mkdir(s_root);
}
const char *share_root(void) { return s_root; }

/* a lock's path below the root, "" for the root itself (key 0); NULL for a key the server does not hold */
static int lk_find(uint32_t key)
{
    unsigned int i;
    for (i = 0; i < NLOCKS; i++) if (s_lk[i].key && s_lk[i].key == key) return (int)i;
    return -1;
}
static const char *lk_path(uint32_t key)
{
    int i;
    if (key == 0u) return "";
    i = lk_find(key);
    return i < 0 ? 0 : s_lk[i].path;
}
static uint32_t lk_new(const char *path)
{
    unsigned int i;
    uint32_t key;
    for (i = 0; i < NLOCKS && s_lk[i].key; i++) { }
    if (i == NLOCKS) return 0u;
    do { key = s_next_key; s_next_key = s_next_key == 0x7FFFFFFFu ? 1u : s_next_key + 1u; } while (lk_find(key) >= 0);
    s_lk[i].key = key; s_lk[i].dir_open = 0u;
    strncpy(s_lk[i].path, path, PATHMAX - 1u); s_lk[i].path[PATHMAX - 1u] = 0;
    return key;
}
static void lk_free(uint32_t key)
{
    int i = lk_find(key);
    if (i < 0) return;
    if (s_lk[i].dir_open) f_closedir(&s_lk[i].dir);
    s_lk[i].key = 0u; s_lk[i].dir_open = 0u;
}

/* a314fs.py find_path: name relative to the lock; "vol:" names the root, an empty component the parent.  out is the
 * path below the root, components joined by '/'.  0, or -1 (above the root, too long, unknown lock) */
static int find_path(uint32_t key, const uint8_t *name, unsigned int nlen, char *out)
{
    const char *cp;
    unsigned int i, s, n;
    for (i = 0; i < nlen && name[i] != ':'; i++) { }
    if (i < nlen) { key = 0u; name += i + 1u; nlen -= i + 1u; }
    if (!(cp = lk_path(key))) return -1;
    strcpy(out, cp); n = (unsigned int)strlen(out);
    for (s = 0; s <= nlen; ) {
        for (i = s; i < nlen && name[i] != '/'; i++) { }
        if (i == nlen && s == nlen) break;
        if (i == s) {                                          /* "/": the parent */
            char *p;
            if (!n) return -1;
            p = strrchr(out, '/'); n = p ? (unsigned int)(p - out) : 0u; out[n] = 0;
        } else {
            if (n + 1u + (i - s) >= PATHMAX - strlen(s_root) - 2u) return -1;
            if (n) out[n++] = '/';
            memcpy(out + n, name + s, i - s); n += i - s; out[n] = 0;
        }
        s = i + 1u;
    }
    return 0;
}
static void full_path(const char *rel, char *out)
{
    strcpy(out, s_root);
    if (*rel) { if (strcmp(s_root, "/")) strcat(out, "/"); strcat(out, rel); }
}
/* 1 a directory, 0 a file, -1 not there; size and the FAT date and time */
static int stat_path(const char *rel, FSIZE_t *size, WORD *fdate, WORD *ftime)
{
    char full[PATHMAX + 16]; FILINFO fi;
    full_path(rel, full);
    *size = 0; *fdate = 0; *ftime = 0;
    if (!*rel) { if (f_stat(s_root, &fi) == FR_OK) { *fdate = fi.fdate; *ftime = fi.ftime; } return 1; }
    if (f_stat(full, &fi) != FR_OK) return -1;
    *size = fi.fsize; *fdate = fi.fdate; *ftime = fi.ftime;
    return (fi.fattrib & AM_DIR) ? 1 : 0;
}
/* a FAT date and time as the AmigaDOS DateStamp: days since 1 January 1978, minutes, ticks (50 a second) */
static void dmt(WORD fdate, WORD ftime, uint32_t *d)
{
    static const uint16_t cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint32_t y = 1980u + (fdate >> 9), m = (fdate >> 5) & 15u, dd = fdate & 31u, days;
    if (m < 1u || m > 12u || dd < 1u) { d[0] = d[1] = d[2] = 0u; return; }
    days = (y - 1978u) * 365u + (y - 1977u) / 4u + cum[m - 1u] + dd - 1u;
    if (m > 2u && (y % 4u) == 0u) days++;
    d[0] = days; d[1] = (uint32_t)(ftime >> 11) * 60u + ((ftime >> 5) & 63u); d[2] = (uint32_t)(ftime & 31u) * 2u * 50u;
}
/* the ExamineObject/ExamineNext response: disk_key, type, size, protection, date, BCPL name, BCPL comment */
static unsigned int exam_res(uint8_t *res, unsigned int disk_key, int dir, FSIZE_t size, WORD fdate, WORD ftime, const char *name)
{
    uint32_t d[3]; unsigned int n = (unsigned int)strlen(name);
    if (n > 107u) n = 107u;
    dmt(fdate, ftime, d);
    wr16(res, 1u); wr16(res + 2, 0u); wr16(res + 4, disk_key); wr16(res + 6, (uint32_t)(dir ? ST_USERDIR : ST_FILE));
    wr32(res + 8, size > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)size); wr32(res + 12, 0u);
    wr32(res + 16, d[0]); wr32(res + 20, d[1]); wr32(res + 24, d[2]);
    res[28] = (uint8_t)n; memcpy(res + 29, name, n); res[29 + n] = 0;
    return 30u + n;
}
static const char *last_comp(const char *rel) { const char *p = strrchr(rel, '/'); return p ? p + 1 : rel; }
static int busy(const char *rel) { char full[PATHMAX + 16]; full_path(rel, full); return s_busy && s_busy(full); }

static int fh_find(uint32_t fp)
{
    unsigned int i;
    for (i = 0; i < NFILES; i++) if (s_fh[i].fp && s_fh[i].fp == fp) return (int)i;
    return -1;
}

unsigned int share_request(const uint8_t *q, unsigned int qlen, uint8_t *res)
{
    char rel[PATHMAX], rel2[PATHMAX], full[PATHMAX + 16], full2[PATHMAX + 16];
    unsigned int type, nlen;
    uint32_t key;
    int i, t;
    FSIZE_t size; WORD fd, ft;
    if (qlen < 2u) return fail(res, ERROR_ACTION_NOT_KNOWN);
    type = rd16(q);
    switch (type) {
    case ACTION_LOCATE_OBJECT:                             /* key, mode, name */
        nlen = q[8];
        if (find_path(rd32(q + 2), q + 9, nlen, rel) < 0 || stat_path(rel, &size, &fd, &ft) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (!(key = lk_new(rel))) return fail(res, ERROR_OBJECT_IN_USE);
        return ok_key(res, key);
    case ACTION_FREE_LOCK:
        lk_free(rd32(q + 2));
        return ok0(res);
    case ACTION_COPY_DIR: {
        const char *p = lk_path(rd32(q + 2));
        if (!p) return fail(res, ERROR_INVALID_LOCK);
        strcpy(rel, p);
        if (!(key = lk_new(rel))) return fail(res, ERROR_OBJECT_IN_USE);
        return ok_key(res, key);
    }
    case ACTION_PARENT: {
        const char *p = lk_path(rd32(q + 2)); char *s;
        if (!p) return fail(res, ERROR_INVALID_LOCK);
        if (!*p) return ok_key(res, 0u);
        strcpy(rel, p); s = strrchr(rel, '/'); if (s) *s = 0; else rel[0] = 0;
        if (!(key = lk_new(rel))) return fail(res, ERROR_OBJECT_IN_USE);
        return ok_key(res, key);
    }
    case ACTION_EXAMINE_OBJECT:
        key = rd32(q + 2);
        {
            const char *p = lk_path(key);
            if (!p) return fail(res, ERROR_INVALID_LOCK);
            strcpy(rel, p);
        }
        if ((t = stat_path(rel, &size, &fd, &ft)) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (t == 1 && key && (i = lk_find(key)) >= 0) {
            if (s_lk[i].dir_open) f_closedir(&s_lk[i].dir);
            full_path(rel, full);
            s_lk[i].dir_open = f_opendir(&s_lk[i].dir, full) == FR_OK;
        } else if (t == 1 && !key) {
            /* the root without a lock: ExamineNext on key 0 opens its listing itself */
        }
        return exam_res(res, 666u, t, size, fd, ft, *rel ? last_comp(rel) : s_vol);
    case ACTION_EXAMINE_NEXT: {                            /* key, disk_key */
        FILINFO fi;
        key = rd32(q + 2);
        i = lk_find(key);
        if (key && i < 0) return fail(res, ERROR_INVALID_LOCK);
        if (i < 0) return fail(res, ERROR_OBJECT_WRONG_TYPE);
        if (!s_lk[i].dir_open) {
            full_path(s_lk[i].path, full);
            if (f_opendir(&s_lk[i].dir, full) != FR_OK) return fail(res, ERROR_OBJECT_WRONG_TYPE);
            s_lk[i].dir_open = 1u;
        }
        if (f_readdir(&s_lk[i].dir, &fi) != FR_OK || !fi.fname[0]) return fail(res, ERROR_NO_MORE_ENTRIES);
        return exam_res(res, (rd16(q + 6) + 1u) & 0xFFFFu, (fi.fattrib & AM_DIR) != 0, fi.fsize, fi.fdate, fi.ftime, fi.fname);
    }
    case ACTION_EXAMINE_FH:
        if ((i = fh_find(rd32(q + 2))) < 0) return fail(res, ERROR_INVALID_LOCK);
        if ((t = stat_path(s_fh[i].path, &size, &fd, &ft)) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        return exam_res(res, 666u, t, f_size(&s_fh[i].f), fd, ft, last_comp(s_fh[i].path));
    case ACTION_FINDINPUT: case ACTION_FINDOUTPUT: case ACTION_FINDUPDATE: {   /* key, name */
        unsigned int k; BYTE mode; FRESULT fr;
        nlen = q[6];
        if (find_path(rd32(q + 2), q + 7, nlen, rel) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        t = stat_path(rel, &size, &fd, &ft);
        if (!*rel || t == 1) return fail(res, ERROR_OBJECT_WRONG_TYPE);
        if (busy(rel)) return fail(res, ERROR_OBJECT_IN_USE);
        for (k = 0; k < NFILES && s_fh[k].fp; k++) { }
        if (k == NFILES) return fail(res, ERROR_OBJECT_IN_USE);
        mode = type == ACTION_FINDINPUT ? (FA_READ | FA_WRITE) : type == ACTION_FINDOUTPUT ? (FA_READ | FA_WRITE | FA_CREATE_ALWAYS)
                                                                                           : (FA_READ | FA_WRITE | FA_OPEN_ALWAYS);
        full_path(rel, full);
        fr = f_open(&s_fh[k].f, full, mode);
        if (fr != FR_OK && type == ACTION_FINDINPUT) fr = f_open(&s_fh[k].f, full, FA_READ);   /* a read-only file */
        if (fr != FR_OK) return fail(res, ERROR_OBJECT_NOT_FOUND);
        do { key = s_next_fp; s_next_fp = s_next_fp == 0x7FFFFFFFu ? 1u : s_next_fp + 1u; } while (fh_find(key) >= 0);
        s_fh[k].fp = key; strcpy(s_fh[k].path, rel);
        return ok_key(res, key);
    }
    case ACTION_READ: {                                    /* arg1, address, length: the bytes to share_data */
        UINT n; uint32_t len = rd32(q + 10);
        if ((i = fh_find(rd32(q + 2))) < 0) return fail(res, ERROR_INVALID_LOCK);
        if (len > SHARE_DATA_BYTES) len = SHARE_DATA_BYTES;
        if (f_read(&s_fh[i].f, share_data, len, &n) != FR_OK) return fail(res, ERROR_OBJECT_NOT_FOUND);
        return ok_key(res, n);
    }
    case ACTION_WRITE: {                                   /* arg1, address, length: the bytes from share_data */
        UINT n; uint32_t len = rd32(q + 10);
        if ((i = fh_find(rd32(q + 2))) < 0) return fail(res, ERROR_INVALID_LOCK);
        if (len > SHARE_DATA_BYTES) len = SHARE_DATA_BYTES;
        if (f_write(&s_fh[i].f, share_data, len, &n) != FR_OK || n != len) return fail(res, ERROR_DISK_FULL);
        return ok_key(res, n);
    }
    case ACTION_SEEK: {                                    /* arg1, new_pos, mode; the old position */
        int32_t pos = (int32_t)rd32(q + 6), mode = (int32_t)rd32(q + 10);
        int64_t base, np;
        FSIZE_t old;
        if ((i = fh_find(rd32(q + 2))) < 0) return fail(res, ERROR_INVALID_LOCK);
        old = f_tell(&s_fh[i].f);
        base = mode == OFFSET_BEGINNING ? 0 : mode == OFFSET_END ? (int64_t)f_size(&s_fh[i].f) : (int64_t)old;
        np = base + pos;
        if (np < 0 || np > (int64_t)f_size(&s_fh[i].f) || f_lseek(&s_fh[i].f, (FSIZE_t)np) != FR_OK) return fail(res, ERROR_SEEK_ERROR);
        return ok_key(res, (uint32_t)old);
    }
    case ACTION_END:
        if ((i = fh_find(rd32(q + 2))) >= 0) { f_close(&s_fh[i].f); s_fh[i].fp = 0u; }
        return ok0(res);
    case ACTION_DELETE_OBJECT: {                           /* key, name */
        FRESULT fr;
        nlen = q[6];
        if (find_path(rd32(q + 2), q + 7, nlen, rel) < 0 || !*rel) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if ((t = stat_path(rel, &size, &fd, &ft)) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (busy(rel)) return fail(res, ERROR_OBJECT_IN_USE);
        full_path(rel, full);
        fr = f_unlink(full);
        if (fr == FR_OK) return ok0(res);
        return fail(res, t == 1 ? ERROR_DIRECTORY_NOT_EMPTY : ERROR_OBJECT_IN_USE);
    }
    case ACTION_RENAME_OBJECT: {                           /* key, target_dir, name_len, new_name_len, names */
        unsigned int n1 = q[10], n2 = q[11];
        if (find_path(rd32(q + 2), q + 12, n1, rel) < 0 || !*rel || stat_path(rel, &size, &fd, &ft) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (find_path(rd32(q + 6), q + 12 + n1, n2, rel2) < 0 || !*rel2) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (!strcmp(rel, rel2)) return ok0(res);
        if (busy(rel)) return fail(res, ERROR_OBJECT_IN_USE);
        if (!ci_eq(rel, rel2) && stat_path(rel2, &size, &fd, &ft) >= 0) return fail(res, ERROR_OBJECT_EXISTS);
        full_path(rel, full); full_path(rel2, full2);
        return f_rename(full, full2) == FR_OK ? ok0(res) : fail(res, ERROR_OBJECT_IN_USE);
    }
    case ACTION_CREATE_DIR:                                /* key, name; a lock on the new directory */
        nlen = q[6];
        if (find_path(rd32(q + 2), q + 7, nlen, rel) < 0 || !*rel) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (stat_path(rel, &size, &fd, &ft) >= 0) return fail(res, ERROR_OBJECT_EXISTS);
        full_path(rel, full);
        if (f_mkdir(full) != FR_OK) return fail(res, ERROR_OBJECT_NOT_FOUND);
        if (!(key = lk_new(rel))) return fail(res, ERROR_OBJECT_IN_USE);
        return ok_key(res, key);
    case ACTION_SET_PROTECT:                               /* key, mask, name: nothing stored */
        nlen = q[10];
        if (find_path(rd32(q + 2), q + 11, nlen, rel) < 0 || !*rel || stat_path(rel, &size, &fd, &ft) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        return ok0(res);
    case ACTION_SET_COMMENT:                               /* key, name_len, comment_len, name, comment: nothing stored */
        nlen = q[6];
        if (find_path(rd32(q + 2), q + 8, nlen, rel) < 0 || !*rel || stat_path(rel, &size, &fd, &ft) < 0) return fail(res, ERROR_OBJECT_NOT_FOUND);
        return ok0(res);
    case ACTION_SAME_LOCK: {
        const char *a = lk_path(rd32(q + 2)), *b = lk_path(rd32(q + 6));
        wr16(res + 2, 0u);
        if (!a || !b) { wr16(res, 0u); wr16(res + 2, (uint32_t)LOCK_DIFFERENT); }
        else if (ci_eq(a, b)) { wr16(res, 1u); wr16(res + 2, LOCK_SAME); }
        else { wr16(res, 0u); wr16(res + 2, LOCK_SAME_VOLUME); }
        return 4u;
    }
    case ACTION_DISK_INFO: case ACTION_INFO: {             /* total and used blocks of 512 bytes, the block size */
        FATFS *fs; DWORD fre; uint32_t total = 0u, used = 0u;
        if (f_getfree(s_root, &fre, &fs) == FR_OK) {
            uint64_t t64 = (uint64_t)(fs->n_fatent - 2u) * fs->csize, f64 = (uint64_t)fre * fs->csize;
            /* AmigaDOS Info multiplies the blocks by the block size in 32 bits: a total above 2 GB is reported as
             * 2 GB less 64 KB, the free blocks scaled with it, so the share in use stays exact */
            const uint64_t lim = 0x7FFF0000u / 512u;
            if (t64 > lim) { f64 = f64 * lim / t64; t64 = lim; }
            total = (uint32_t)t64; used = (uint32_t)(t64 - f64);
        }
        wr16(res, 1u); wr16(res + 2, 0u); wr32(res + 4, total); wr32(res + 8, used); wr32(res + 12, 512u);
        return 16u;
    }
    default:
        return fail(res, ERROR_ACTION_NOT_KNOWN);
    }
}
