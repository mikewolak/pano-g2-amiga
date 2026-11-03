/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* term.h -- a VT100/VT220 terminal (libvterm 0.3.3) on a screen of its own, for serialmon and vtplay.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef TERM_H
#define TERM_H

#include <exec/types.h>
#include <intuition/screens.h>

typedef struct term term_t;

/* bytes the terminal sends (keys, answers to queries) */
typedef void (*term_out_fn)(const UBYTE *b, ULONG n, void *user);

/* the terminal for host screen `host`: full screen (a screen of its own in the host's mode and size) or a sizeable
 * window on the host; `wpen` the host's pen for each of the 16 ANSI colours in the window (NULL: pens 0-15).
 * Right Amiga F switches between the two, Right Amiga Q or the close gadget closes. */
term_t *term_open(struct Screen *host, const UBYTE *wpen, int full, term_out_fn out, void *user);
void    term_close(term_t *t);
void    term_set_utf8(term_t *t, int on);
void    term_front(term_t *t);
void    term_set_smooth(term_t *t, int on);              /* full screen: smooth (default) or jump scrolling; Right Amiga S */                           /* its screen to the front, its window active */                /* bytes as UTF-8 (default: 8-bit, ISO 8859-1, C1 controls) */

/* received bytes, queued; term_tick works them in */
ULONG   term_input(term_t *t, const UBYTE *b, ULONG n);     /* bytes taken (the queue holds 64 KB) */
ULONG   term_pending(const term_t *t);                    /* bytes queued */

/* once a display frame: input worked in for at most `budget_us`, the scroll moved toward its target, the cursor */
void    term_tick(term_t *t, ULONG budget_us);

/* the window's signal, and its messages handled: 0 when the terminal is to close (Right Amiga Q) */
ULONG   term_sigmask(const term_t *t);
int     term_events(term_t *t);

/* figures: bytes worked in, display frames, ticks over budget, the longest tick in us */
typedef struct { ULONG bytes, frames, ticks, late, max_us, scrolls, jumps, cells, mirrors, keys, sent, fast, e_get, e_put, e_vt, e_mir; } term_stats_t;  /* cells drawn; e_: E clock ticks drawing (e_put), in vterm_input_write (e_vt), mirror copies (e_mir) */
void    term_stats(const term_t *t, term_stats_t *s);

/* requests the user made of the opener, cleared when read */
#define TERM_REQ_SEND 1ul                                /* Right Amiga U: send a file */
ULONG   term_requests(term_t *t);
void    term_window(term_t *t);                          /* to the window presentation (from full screen) and in front */

#endif
