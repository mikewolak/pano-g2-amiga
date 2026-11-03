/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/plat.h -- the few things that differ between Winsock (32-bit Windows, XP and later, msvcrt) and BSD sockets. */
#ifndef PLAT_H
#define PLAT_H
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define sock_close closesocket
typedef int socklen_plat;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define sock_close close
typedef socklen_t socklen_plat;
#endif

int      plat_init(void);                       /* WSAStartup; 0 = ok */
void     plat_done(void);
uint32_t plat_ms(void);                         /* a monotonic millisecond counter (wraps; use differences) */
void     plat_sleep_ms(unsigned ms);
/* wait up to ms for the socket to be readable: 1 readable, 0 timeout, -1 error */
int      plat_wait_readable(sock_t s, unsigned ms);
int      plat_parse_ip(const char *s, uint32_t *host_order);   /* dotted quad only; 1 = ok */
const char *plat_ip_str(uint32_t host_order, char buf[16]);
#endif
