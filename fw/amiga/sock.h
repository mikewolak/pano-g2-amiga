/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/sock.h -- the socket window of HD0: TCP and UDP sockets on the firmware's lwIP for
 * amiga/panotcp (bsdsocket.library).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga.
 *
 * Blocks of HD0, answered by the firmware and never written to the file, when the RigidDiskBlock uses blocks below
 * PFL_REQ only and the partitions start at SK_END or later (hd_create: cylinder 2, block 2016).  Every field is
 * big-endian.
 *   SK_TX .. SK_TX + 127   written  the transmit window, SK_WIN bytes: data of SEND, SENDTO and RESOLVE operations
 *   SK_REQ                 written  0 'PNQ1'  4 sequence number  8 operations (at most SK_OPS)
 *                                   12 transmit-window blocks that follow this block in the same write (0: the window
 *                                      was written before)  16 the operations, SK_OPLEN bytes each
 *   SK_RES                 read     0 'PNR1'  4 sequence number of the last request  8 operations run
 *                                  12 bytes placed in the receive window  16 the results, SK_RESLEN bytes each
 *   SK_STAT                read     the state of every socket at the time of the read (sk_status)
 *   SK_RX .. SK_RX + 127   read     the receive window: data of RECV and RECVFROM, at each result's offset
 * SK_REQ lies just below SK_TX and SK_RES, SK_STAT just below SK_RX, so a request with a short payload is one write and
 * its response with the state and a short payload one read.  A request runs when its last block is written: SK_REQ
 * itself, or SK_REQ + n when the request names n blocks after it.  A request with the sequence number of the last is
 * not run again.
 *
 * Operation (SK_OPLEN bytes): 0 code  1 flags  2 socket  4 a  8 b  12 c  16 d  20 e
 * Result (SK_RESLEN bytes):   0 value (signed; -1 on failure)  4 errno (BSD numbers, as AmiTCP's)  8 x  12 y
 *                            16 receive-window offset of the data */
#ifndef SOCK_H
#define SOCK_H
#include <stdint.h>

#define SK_REQ     511u
#define SK_TX      512u
#define SK_RES     766u
#define SK_STAT    767u
#define SK_RX      768u
#define SK_WIN     65536u
#define SK_END     (SK_RX + SK_WIN / 512u)
#define SK_OPS     20u
#define SK_OPLEN   24u
#define SK_RESLEN  20u
#define SK_N       32u               /* sockets */

/* operations: socket, a, b, c, d, e -> value, x, y, offset */
#define SK_OP_RESET     1u   /* every socket closed (aborted) -> value the firmware's identifier */
#define SK_OP_SOCKET    2u   /* a type (1 stream, 2 datagram) -> value the socket */
#define SK_OP_BIND      3u   /* a address  b port */
#define SK_OP_LISTEN    4u   /* a backlog */
#define SK_OP_ACCEPT    5u   /* -> value the new socket, x peer address, y peer port; EWOULDBLOCK when none waits */
#define SK_OP_CONNECT   6u   /* a address  b port; a stream returns EINPROGRESS, the state shows the outcome */
#define SK_OP_SEND      7u   /* a offset in the transmit window  b length  c flags -> value the bytes taken */
#define SK_OP_SENDTO    8u   /* a offset  b length  c address  d port */
#define SK_OP_RECV      9u   /* a length  b flags (MSG_PEEK 2)  c receive-window offset for the data (0xFFFFFFFF: after
                              * the request's last) -> value the bytes placed at the result's offset */
#define SK_OP_RECVFROM 10u   /* as RECV -> value the datagram's bytes placed, x address, y port */
#define SK_OP_SHUTDOWN 11u   /* a how (0 receive, 1 send, 2 both) */
#define SK_OP_CLOSE    12u
#define SK_OP_SETOPT   13u   /* a level  b option  c value */
#define SK_OP_NAME     14u   /* -> x local address, y local port */
#define SK_OP_PEER     15u   /* -> x peer address, y peer port */
#define SK_OP_RESOLVE  16u   /* a offset  b length of a host name in the transmit window -> x address; EINPROGRESS
                              * while the query runs (the operation is repeated), EHOSTUNREACH when no answer */

/* the state byte of a socket in SK_STAT */
#define SK_S_FREE       0u
#define SK_S_OPEN       1u   /* made, not connected */
#define SK_S_LISTEN     2u
#define SK_S_CONNECTING 3u
#define SK_S_CONNECTED  4u
#define SK_S_EOF        5u   /* the peer closed its side; data may still be waiting */
#define SK_S_ERROR      6u   /* reset, refused or timed out (errno in the entry) */
#define SK_S_CLOSING    7u   /* closed by the Amiga, the firmware still sending */

void sk_init(void);
int  sk_block(uint32_t sec);                   /* 1 when sec is a block of the window and the window is in use */
void sk_setup(int ok);                         /* the window in use (the RigidDiskBlock allows it) or not */
void sk_write(uint32_t sec, const uint8_t *b); /* a written block */
void sk_read(uint32_t sec, uint8_t *b);        /* the 512 bytes of a block to be read */
void sk_poll(void);                            /* from the main loop: the transmit rings into lwIP */
int  sk_line(unsigned i, char *b, unsigned n);   /* a socket's state as text; 0 when it is free */
void sk_counts(uint32_t *req, uint32_t *open, uint32_t *rx, uint32_t *tx);
#endif
