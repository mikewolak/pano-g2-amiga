/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/setup.h -- the setup screen of the G2 Amiga firmware: pages of items in a frame, laid out by position on the
 * text console (fw/vgacon.c), and the pop-ups that change an item (a list, a number, a message).
 *
 * Geometry: 80 x 34 cells (640x480) shows the items on the left and the selected item's help in a pane on the right;
 * 40 x 17 cells (the 15 kHz console of VIDEO=NATIVE) shows the items across the whole width and the help in two rows
 * under them.  Every string is placed in a field of fixed width: it is clipped, never wrapped into the next row,
 * except help text, which is wrapped at word boundaries inside its pane.
 *
 * Every screen is one composed frame (vgacon_frame_begin .. vgacon_frame_end): the heads show it whole at a vertical
 * blank, and only the cells that changed are drawn.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef SETUP_H
#define SETUP_H

/* the key bits su_key returns (amiga_main.c IN_*: the same values) */
#define SU_RIGHT 0x01u
#define SU_LEFT  0x02u
#define SU_DOWN  0x04u
#define SU_UP    0x08u
#define SU_SAVE  0x10000000u                     /* F10, Triangle */
#define SU_BACK  0x20000000u                     /* Escape, Circle */
#define SU_OK    0x40000000u                     /* Enter, Cross, Start */
#define SU_MENU  0x80000000u                     /* F12, PS, the front-panel button */

enum { SU_SECTION, SU_VALUE, SU_INFO, SU_ACTION, SU_GAP };
typedef struct {
    unsigned char kind;                          /* SU_* */
    unsigned char dim;                           /* SU_ACTION, SU_VALUE: shown, but not available now */
    unsigned char changed;                       /* SU_VALUE: differs from the saved setting (shown in yellow) */
    const char   *label;
    char          value[64];                     /* SU_VALUE (shown in brackets), SU_INFO, SU_ACTION (a note) */
    const char   *help;                          /* the help pane's text; '\n' starts a paragraph */
    const char   *key;                           /* the AMIGA.INI key the item sets, or 0 */
} su_item_t;

typedef struct {
    const char *const *tabs; unsigned int ntabs, tab;   /* the pages and the one shown */
    const su_item_t *it; unsigned int n, sel;          /* its items and the selected one */
    unsigned int top;                                  /* the first item row shown; su_draw keeps sel in view */
    const char *title, *right;                         /* the title bar */
    const char *status;                                /* a line of state under the frame (wide geometry), or 0 */
} su_screen_t;

/* the key source: a new press (repeated while an arrow is held), or 0 when the screen should be drawn again */
typedef unsigned int (*su_key_fn)(void);

void su_draw(su_screen_t *s);                            /* the page, one composed frame */
unsigned int su_next(const su_screen_t *s, int dir);     /* the next selectable item from sel in direction dir */
/* a list over the page; the chosen index, or -1 (Escape) */
int  su_list(const char *title, const char *const *opt, unsigned int n, unsigned int cur, su_key_fn key);
/* a number from lo to hi: Left/Right by 1, Up/Down by big; the value chosen, or -1 (Escape) */
int  su_number(const char *title, const char *unit, int v, int lo, int hi, int big, su_key_fn key);
/* a message over the page until Enter or Escape */
void su_message(const char *title, const char *text, su_key_fn key);
#endif
