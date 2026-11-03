/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/syscalls.c -- the floor newlib stands on.
 *
 * newlib is a complete C library that assumes an operating system underneath
 * it and calls out to one through about a dozen `_`-prefixed functions.  There
 * is no operating system here, so this file is it.  Only _sbrk, _write and
 * _read do anything; the rest exist so that a link error is impossible and so
 * that anything asking "is this a terminal?" gets a truthful answer.
 *
 * WE DO NOT USE nosys.specs.  libnosys would supply every one of these as a
 * stub returning -1, INCLUDING _sbrk -- and a _sbrk that always fails is a
 * malloc that always returns NULL, which is exactly the failure this whole
 * exercise is about.  Linking our own definitions and leaving libnosys out
 * means a syscall nobody implemented is an undefined reference at link time,
 * named, rather than a silent -1 at run time.
 *
 * FLOAT.  --specs=nano.specs selects newlib-nano, whose printf is integer-only
 * unless the link explicitly pulls in _printf_float.  We do not, and must not:
 * there is no FPU (RV32IM), so every float operation is a soft-float library
 * call, and the float formatter alone is several KB.  See fw/Makefile.
 *
 * WHAT THIS COSTS A PROGRAM THAT DOES NOT USE IT.  204 bytes of .text, measured
 * on hello.c -- 1,420 freestanding against 1,624 hosted.  --gc-sections removes
 * every libc function nobody called, so the only permanent residents are the
 * ones start.S names (__libc_init_array, exit) and the stubs below.  This is
 * only true because nothing here is referenced unconditionally: see the note on
 * stdio_unbuffered at the bottom of the file, which used to be a constructor
 * and cost 3,892 bytes in every binary for exactly that reason.
 */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>

#include "console.h"

/* Provided by link.ld / link_ddr.ld.  Declared as arrays, not pointers: a
 * linker symbol IS an address, and `extern char *__heap_start` would make the
 * compiler load a pointer FROM that address. */
#ifdef PANO_PIC
/* A POSITION-INDEPENDENT IMAGE HAS NO LINK-TIME HEAP.  It does not know its own address until
 * fw/start_pic.S has computed the load bias, so __heap_start cannot be a linker symbol here -- it is
 * derived at runtime from the bias and _end (fw/pic.c).  __heap_end is unchanged in spirit: the top of
 * the LPDDR is hardware, not program, and does not move when the image does. */
#include "pic.h"
#define __heap_start ((char *)pic_heap_start())
#define __heap_end   ((char *)pic_heap_end())
#else
extern char __heap_start[];
extern char __heap_end[];
#endif

/*----------------------------------------------------------------------------
 * _sbrk -- the one that matters.
 *
 * malloc asks for memory in chunks and never gives it back to us; this hands
 * out a monotonically rising pointer inside [__heap_start, __heap_end) and
 * returns (void *)-1 with ENOMEM the moment the request would cross the end.
 *
 * THE CHECK IS WRITTEN TO SURVIVE OVERFLOW.  `brk_cur + incr > __heap_end` is
 * the obvious form and it is wrong: with a large incr the sum wraps and the
 * comparison passes, so an absurd allocation succeeds and hands back a pointer
 * into nothing.  Comparing the REMAINING SPACE against incr cannot wrap.
 *
 * A negative incr is legal -- newlib gives memory back on realloc-shrink and on
 * free of the topmost block -- so it is handled rather than rejected, but it is
 * clamped at __heap_start so a bad caller cannot walk the break below the heap.
 *--------------------------------------------------------------------------*/
static char *brk_cur;

/* fd 3: a read-only file image held in memory, opened as "DOOM.WAD" (any directory, any case); not used by the
 * Amiga firmware. */
#define WAD_FD 3
#ifdef PANO_G2
#define WAD_IMAGE ((const unsigned char *)0x10800000u)      /* DDR2 chip A, 8 MB up */
#else
#define WAD_IMAGE ((const unsigned char *)0x11400000u)      /* in the cached window */
#endif
static unsigned int wad_pos, wad_len;
static unsigned int wad_image_len(void)
{
    const unsigned char *b = WAD_IMAGE; unsigned int nl, ofs;
    if (!((b[0] == 'I' || b[0] == 'P') && b[1] == 'W' && b[2] == 'A' && b[3] == 'D')) return 0;
    nl = b[4] | (b[5] << 8) | (b[6] << 16) | ((unsigned)b[7] << 24); ofs = b[8] | (b[9] << 8) | (b[10] << 16) | ((unsigned)b[11] << 24);
    return (ofs > 0x00C00000u || nl > 65536u) ? 0 : ofs + 16u * nl;
}
static int is_wad_name(const char *p)
{
    const char *b = p, *q; static const char want[] = "doom.wad"; int i;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    for (i = 0; i < 8; i++) { char c = b[i]; if (c >= 'A' && c <= 'Z') c += 32; if (c != want[i]) return 0; }
    return b[8] == 0;
}
unsigned int heap_ceiling = 0;
unsigned int con_write_nonblocking = 0;  /* 1: _write drops rather than waits for a console nobody is draining */          /* a program may cap malloc below __heap_end  */

void *_sbrk(ptrdiff_t incr)
{
    char *prev;

    if (brk_cur == 0)
        brk_cur = __heap_start;

    if (incr > 0) {
        if ((ptrdiff_t)((heap_ceiling ? (char *)heap_ceiling : __heap_end) - brk_cur) < incr) {
            errno = ENOMEM;
            return (void *)-1;
        }
    } else if (incr < 0) {
        if ((ptrdiff_t)(brk_cur - __heap_start) < -incr) {
            errno = EINVAL;
            return (void *)-1;
        }
    }

    prev = brk_cur;
    brk_cur += incr;
    return prev;
}

/* Not a syscall -- a courtesy for firmware that wants to report its own heap
 * use, and for libctest, which prints it. */
void *heap_break(void)  { return brk_cur ? (void *)brk_cur : (void *)__heap_start; }
void *heap_start(void)  { return (void *)__heap_start; }
void *heap_limit(void)  { return (void *)__heap_end; }

/*----------------------------------------------------------------------------
 * _write -- stdout and stderr go to the console; everything else is EBADF.
 *
 * con_putc spins on the TX ready bit, so this never drops.  The console FIFO
 * DROPS when full rather than stalling the CPU (fw/console.h), which is the
 * right choice for a machine nobody may be listening to -- but a formatted
 * printf is exactly the case where losing the tail is worst, so we wait.
 *--------------------------------------------------------------------------*/
int _write(int fd, const char *buf, int len)
{
    int i;

    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    /* BLOCKING BY DEFAULT, AND THAT IS DELIBERATE: for every gate in rtl/sim the program's OUTPUT IS the
     * result, the bench reads it through this same FIFO, and a dropped character is a lost verdict.  A program
     * whose log is somewhere else (one that draws on the VGA) sets con_write_nonblocking and takes the other
     * trade: the serial becomes best-effort and a missing reader can no longer park the machine inside a print. */
    for (i = 0; i < len; i++) {
        if (con_write_nonblocking) con_putc_nb(buf[i]);
        else                       con_putc(buf[i]);
    }
    return len;
}

/*----------------------------------------------------------------------------
 * _read -- stdin from the console, BLOCKING, one byte at a time.
 *
 * It returns as soon as it has a byte rather than filling the buffer, which is
 * what a character device is supposed to do: getchar() on a terminal must not
 * wait for `len` keystrokes.  There is no EOF here -- the cable is always
 * "open" -- so this can block for ever, and that is the honest behaviour: the
 * caller asked for a byte from a terminal nobody is typing at.
 *--------------------------------------------------------------------------*/
int _read(int fd, char *buf, int len)
{
    int c;

    if (fd == WAD_FD) {
        unsigned int n = (unsigned)len;
        if (wad_pos >= wad_len) return 0;
        if (n > wad_len - wad_pos) n = wad_len - wad_pos;
        memcpy(buf, WAD_IMAGE + wad_pos, n); wad_pos += n;
        return (int)n;
    }
    if (fd != 0) {
        errno = EBADF;
        return -1;
    }
    if (len <= 0)
        return 0;

    do {
        c = con_getc();
    } while (c < 0);

    buf[0] = (char)c;
    return 1;
}

/*----------------------------------------------------------------------------
 * The stubs.  None of them can succeed and none of them should pretend to,
 * with two deliberate exceptions.
 *
 * _fstat reports S_IFCHR.  This is not decoration: newlib chooses a stream's
 * buffering from it, and a character device gets line buffering rather than
 * the 1 KB fully-buffered mode a regular file gets.  Reporting a regular file
 * would make printf hold output until 1024 bytes accumulated, which on a
 * console being polled by a host looks exactly like a hung program.  (We go
 * further and force unbuffered -- see console_stdio_init below.)
 *
 * _lseek returns 0 rather than -1.  newlib calls it while setting a stream up;
 * answering "position 0" on an unseekable device is what a pipe does.
 *--------------------------------------------------------------------------*/
int _close(int fd)
{
    if (fd == WAD_FD) return 0;
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    if (fd == WAD_FD) {
        int base = whence == 1 ? (int)wad_pos : whence == 2 ? (int)wad_len : 0;
        if (base + offset < 0 || (unsigned)(base + offset) > wad_len) { errno = EINVAL; return -1; }
        wad_pos = (unsigned)(base + offset); return (int)wad_pos;
    }
    (void)offset; (void)whence;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    if (fd == WAD_FD) { st->st_mode = S_IFREG; st->st_size = (off_t)wad_len; return 0; }
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    return fd != WAD_FD;
}

int _open(const char *path, int flags, int mode)
{
    (void)flags; (void)mode;
    if (is_wad_name(path) && (wad_len = wad_image_len()) != 0) { wad_pos = 0; return WAD_FD; }
    errno = ENOENT;
    return -1;
}

int _link(const char *old, const char *new)
{
    (void)old; (void)new;
    errno = EMLINK;
    return -1;
}

int _unlink(const char *path)
{
    (void)path;
    errno = ENOENT;
    return -1;
}

int _stat(const char *path, struct stat *st)
{
    (void)path; (void)st;
    errno = ENOENT;
    return -1;
}

clock_t _times(struct tms *buf)
{
    (void)buf;
    errno = ENOSYS;
    return (clock_t)-1;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tv; (void)tz;
    errno = ENOSYS;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

/* PARK, do not return.  start.S also parks if main returns, but exit() can be
 * called from anywhere -- an assert, abort(), a fatal path -- and letting it
 * fall off the end would fetch whatever follows in memory.  Spinning leaves
 * the machine in a state the host can still see with 'V' and re-load with 'M'. */
/* A program that must not stop (a parked CPU stops kicking the fabric watchdog) defines pano_exit_hook and
 * gets exit() as well; every other image parks as before. */
void __attribute__((weak)) pano_exit_hook(int code);
void _exit(int code)
{
    if (pano_exit_hook) pano_exit_hook(code);
    for (;;)
        ;
}

/*----------------------------------------------------------------------------
 * stdio_unbuffered -- OPT IN, and it is deliberately not a constructor.
 *
 * The first version of this file made it __attribute__((constructor)) so that
 * every program got it automatically.  That cost 3,892 bytes in EVERY BINARY,
 * measured -- hello went from 1,624 to 5,516 bytes of .text -- because a
 * reference to setvbuf from .init_array drags the whole of stdio in past
 * --gc-sections whether or not the program ever prints.  On a 32 KB budget
 * that is 12 % of the machine spent on programs that do not use it.
 *
 * Without it the fixed cost of having a C library at all is 204 bytes.
 *
 * What a program gets by default is fine: _fstat reports S_IFCHR, so newlib
 * line-buffers stdout and every printf ending in '\n' is flushed.  Call this
 * only when the buffering itself is the problem -- typically when tracing a
 * hang, where the last partial line is the one you need and it is still
 * sitting in the buffer.
 *
 * NOTE that line buffering makes the FIRST printf allocate a 1 KB buffer from
 * the heap, i.e. from LPDDR.  If LPDDR is dead, newlib falls back to its
 * internal one-byte buffer and printf still works -- slowly, but it works, so
 * a memory fault cannot silence the console that would report it.
 *--------------------------------------------------------------------------*/
void stdio_unbuffered(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}
