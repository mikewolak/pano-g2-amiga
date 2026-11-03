/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/plat.c -- see plat.h. */
#ifndef _WIN32
#define _DEFAULT_SOURCE 1                /* glibc: -std=c99 alone hides clock_gettime, nanosleep, struct timespec */
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <string.h>
#include "plat.h"

#ifdef _WIN32
int plat_init(void) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) ? -1 : 0; }
void plat_done(void) { WSACleanup(); }
uint32_t plat_ms(void) { return (uint32_t)GetTickCount(); }
void plat_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
int plat_init(void) { return 0; }
void plat_done(void) { }
uint32_t plat_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u); }
void plat_sleep_ms(unsigned ms) { struct timespec t; t.tv_sec = ms / 1000u; t.tv_nsec = (long)(ms % 1000u) * 1000000L; nanosleep(&t, NULL); }
#endif

int plat_wait_readable(sock_t s, unsigned ms)
{
    fd_set r; struct timeval tv; int n;
    FD_ZERO(&r); FD_SET(s, &r);
    tv.tv_sec = (long)(ms / 1000u); tv.tv_usec = (long)(ms % 1000u) * 1000L;
    n = select((int)s + 1, &r, NULL, NULL, &tv);
    return n > 0 ? 1 : n;
}

int plat_parse_ip(const char *s, uint32_t *out)
{
    unsigned a, b, c, d; char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255u || b > 255u || c > 255u || d > 255u) return 0;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 1;
}

const char *plat_ip_str(uint32_t ip, char buf[16])
{
    sprintf(buf, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255u), (unsigned)((ip >> 8) & 255u), (unsigned)(ip & 255u));
    return buf;
}
