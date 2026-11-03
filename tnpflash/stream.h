/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* tnpflash/stream.h -- the connection-oriented side of TNP, as a stop-and-wait client, and endpoint-0 on top of it.
 *
 *   connect:  SYN (flags 0x80, seq 0, ack 0) every 50 ms, up to 60 times; the device answers SYN+ACK (0xC0) with ITS seq D;
 *             we complete with a pure ACK: flags 0x40, seq 1, ack D.  The first data packet also carries seq 1.
 *   data:     flags 0x40, seq = ours (advances only on packets that carry a payload), ack = the last in-order seq received.
 *             A packet is done when an ACK arrives whose ack equals its seq; otherwise it is sent again (40 ms, no backoff).
 *   receive:  seq == rxseq+1 is delivered and acknowledged at once; anything at or behind rxseq is a duplicate and is
 *             acknowledged again (Pano's host does not, and that is a weakness, not a rule); ahead of it, NAK with rxseq+1.
 *   close:    RST (0x10), seq = next unused, ack = rxseq.
 * One packet in flight, several endpoint-0 messages per packet. */
#ifndef STREAM_H
#define STREAM_H
#include "tnp.h"

#define TNP_MTU 1464u                      /* payload bytes after the 8-byte header on a 1500-byte path (Pano's own number) */
#define RXQ     512

typedef struct {
    sock_t s; uint32_t ip; int up, trace;
    unsigned txseq, rxseq, tid;            /* txseq: the NEXT unused sequence number */
    ep0_msg_t rxq[RXQ]; unsigned rxh, rxt; /* endpoint-0 messages received and not yet claimed */
    unsigned long n_tx, n_retx, n_rx, n_dup, n_nak, n_other_ep, n_interrupt, n_error;
    unsigned long n_tx_bytes, n_rx_bytes;   /* everything that went out and came back, for a bandwidth meter */
    unsigned      rtt_ms, srtt_ms;          /* the last acknowledgement's round trip, and a smoothed one */
    unsigned      ack_pending; uint32_t ack_sent_ms;   /* acknowledgements we have not sent yet, and when we last did */
    /* the send window: packets sent and not yet acknowledged, oldest first (go-back-N on a timeout or a NAK) */
    struct { unsigned seq; size_t len; uint32_t sent_ms; uint8_t buf[8 + TNP_MTU]; } win[32];
    unsigned win_n, win_size;              /* in flight; how many may be (1 = stop-and-wait) */
    void (*irq_cb)(void *ctx, uint32_t data); void *irq_ctx;   /* endpoint-0 Interrupt messages (VDC tokens, the button) */
    unsigned n_usb_retry;              /* USB reads re-asked after a lost reply */
    void (*ep_cb)(void *ctx, unsigned ep, const uint8_t *m, size_t n); void *ep_ctx;   /* messages on any other endpoint */
    char err[160];
} tnp_stream_t;

/* THE DEVICE HOLDS ONE SESSION, KEYED BY THE CLIENT'S SOURCE PORT.  A client that dies without sending RST leaves it
 * held, and every later connection from a different port is refused (SYN+NAK) -- which used to mean pulling the plug.
 * So we always call from the SAME port and send a RST from it first: our own stale session, if any, is gone in one
 * packet.  If the port is taken (a second client on this machine) an ephemeral one is used and recovery falls back to
 * tnp_force_reset. */
#define TNP_CLIENT_PORT 8322u
int  tnp_connect(tnp_stream_t *c, uint32_t ip, int trace);      /* 0 ok */
/* clear a session left open by a client that is gone: a RST from every source port, about a second.  Returns 0 if the
 * device accepts a connection afterwards. */
int  tnp_force_reset(uint32_t ip);
void tnp_close(tnp_stream_t *c);
/* send one packet made of whole endpoint messages: with win_size 1 it waits for the ACK (stop-and-wait); with a larger window
 * it returns as soon as the packet is queued and there is room, so several may be in flight.  0 ok */
int  tnp_send_ep0(tnp_stream_t *c, const uint8_t *msgs, size_t len);
int  tnp_send_ep(tnp_stream_t *c, unsigned ep, const uint8_t *msg, size_t mlen);   /* one message to any endpoint, same rules */
int  tnp_drain(tnp_stream_t *c);           /* wait until everything in flight is acknowledged.  0 ok */
void tnp_set_window(tnp_stream_t *c, unsigned n);
int  tnp_poll(tnp_stream_t *c, unsigned ms);   /* service the socket for up to ms: ACKs, retransmits, incoming messages.  <0 = gone */
/* wait for the endpoint-0 reply with this transaction id.  0 ok, -1 timeout, -2 the device answered Error */
int  tnp_wait_reply(tnp_stream_t *c, unsigned tid, ep0_msg_t *out, unsigned timeout_ms);

/* ---- registers ------------------------------------------------------------------------------------------------------ */
int  reg_write(tnp_stream_t *c, int cfg, uint32_t addr, uint32_t val);
int  reg_read(tnp_stream_t *c, int cfg, uint32_t addr, uint32_t *val);
/* a batch: up to `n' messages built by the caller with ep0_build, the read replies collected by transaction id */
unsigned tnp_next_tid(tnp_stream_t *c);
#endif
