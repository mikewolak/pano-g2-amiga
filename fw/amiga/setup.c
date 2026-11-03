/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/setup.c -- the setup screen of the G2 Amiga firmware (setup.h): the page, its frame, the help pane, the
 * key bar, and the pop-ups.  Drawing goes through fw/vgacon.c only, in cells at fixed positions (vgacon_text,
 * vgacon_fill, vgacon_cell); characters 0xB3..0xDA are the line-drawing set of code page 437 in the EGA font.
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <string.h>
#include <stdio.h>
#include "../vgacon.h"
#include "setup.h"

/* colours: the page on blue, values bright, the selection bar black on cyan, pop-ups on grey */
#define PG_FG   VC_GREY
#define PG_BG   VC_BLUE
#define LN_FG   VC_LCYAN                         /* frame lines */
#define VAL_FG  VC_WHITE
#define INF_FG  VC_LCYAN
#define SEC_FG  VC_YELLOW
#define DIM_FG  VC_LBLUE
#define BAR_FG  VC_BLACK
#define BAR_BG  VC_CYAN
#define POP_FG  VC_BLACK
#define POP_BG  VC_GREY
#define POP_LN  VC_BLUE

/* the line-drawing characters, code page 437 */
#define H2  0xCDu
#define V2  0xBAu
#define TL2 0xC9u
#define TR2 0xBBu
#define BL2 0xC8u
#define BR2 0xBCu
#define TT2 0xD1u                                /* double horizontal, single down */
#define BT2 0xCFu                                /* double horizontal, single up */
#define V1  0xB3u
#define H1  0xC4u
#define ARR_UP 0x18u
#define ARR_DN 0x19u
#define TRI_R  0x10u
#define TRI_L  0x11u
#define MORE   0xAFu                             /* a value clipped at its field's end */

/* the layout of the current geometry */
typedef struct {
    unsigned int W, H, wide;
    unsigned int split;                          /* the column of the line between items and help (wide) */
    unsigned int r0, r1;                         /* the item rows, inclusive */
    unsigned int lx, lw, vx, vw;                 /* label and value fields */
    unsigned int hx, hw, hy0, hy1;               /* the help pane */
} lay_t;

static void layout(lay_t *L)
{
    L->W = vgacon_cols(); L->H = vgacon_rows(); L->wide = L->W >= 64u;
    if (L->wide) {
        L->split = L->W - 26u;
        L->r0 = 3u; L->r1 = L->H - 4u;
        L->lx = 3u; L->lw = 17u; L->vx = L->lx + L->lw + 1u; L->vw = L->split - 2u - L->vx;
        L->hx = L->split + 2u; L->hw = L->W - L->split - 4u; L->hy0 = 3u; L->hy1 = L->H - 4u;
    } else {
        L->split = L->W - 1u;
        L->r0 = 3u; L->r1 = L->H - 6u;
        L->lx = 2u; L->lw = 16u; L->vx = L->lx + L->lw + 1u; L->vw = L->split - 1u - L->vx;
        L->hx = 1u; L->hw = L->W - 2u; L->hy0 = L->H - 4u; L->hy1 = L->H - 2u;
    }
}

/* text wrapped at word boundaries into rows y0..y1 of a field x, w; '\n' ends a line.  A word longer than the field
 * is cut at its end.  Returns the next free row; with draw 0 nothing is drawn (the rows are counted). */
static unsigned int wrapd(unsigned int x, unsigned int y0, unsigned int y1, unsigned int w, const char *t, int draw)
{
    char ln[96]; unsigned int y = y0, n = 0u;
    if (w >= sizeof ln) w = sizeof ln - 1u;
    while (t && y <= y1) {                       /* words joined by one blank, whatever the text has between them */
        const char *e; unsigned int L;
        while (*t == ' ') t++;
        if (!*t || *t == '\n') {
            if (n || *t == '\n') { ln[n] = 0; if (draw) vgacon_text(x, y, w, ln); y++; n = 0u; }
            if (!*t) break;
            t++; continue;
        }
        for (e = t; *e && *e != ' ' && *e != '\n'; e++) { }
        L = (unsigned int)(e - t);
        if (n && n + 1u + L > w) { ln[n] = 0; if (draw) vgacon_text(x, y, w, ln); y++; n = 0u; if (y > y1) break; }
        if (n) ln[n++] = ' ';
        if (L > w - n) L = w - n;
        memcpy(ln + n, t, L); n += L; t = e;
    }
    return y;
}
static unsigned int wrap(unsigned int x, unsigned int y0, unsigned int y1, unsigned int w, const char *t)
{
    return wrapd(x, y0, y1, w, t, 1);
}

/* "[value]" in a field of w cells; a value that does not fit ends in MORE before the bracket */
static void bracket(unsigned int x, unsigned int y, unsigned int w, const char *v, int brackets)
{
    char b[96]; unsigned int n = (unsigned int)strlen(v), room = brackets ? w - 2u : w, k = 0u;
    if (w < 3u || w >= sizeof b) return;
    if (brackets) b[k++] = '[';
    if (n <= room) { memcpy(b + k, v, n); k += n; }
    else { memcpy(b + k, v, room - 1u); k += room - 1u; b[k++] = (char)MORE; }
    if (brackets) b[k++] = ']';
    b[k] = 0;
    vgacon_text(x, y, w, b);
}

static int selectable(const su_item_t *it) { return (it->kind == SU_VALUE || it->kind == SU_ACTION) && !it->dim; }

unsigned int su_next(const su_screen_t *s, int dir)
{
    unsigned int i, j = s->sel;
    for (i = 0; i < s->n; i++) {
        j = dir > 0 ? (j + 1u) % s->n : (j ? j - 1u : s->n - 1u);
        if (selectable(&s->it[j])) return j;
    }
    return s->sel;
}

static void tabs(const lay_t *L, const su_screen_t *s)
{
    unsigned int i, x = 1u;
    vgacon_color(PG_FG, PG_BG); vgacon_fill(0u, 1u, L->W, 1u, ' ');
    if (L->wide) {
        for (i = 0; i < s->ntabs; i++) {
            unsigned int n = (unsigned int)strlen(s->tabs[i]) + 2u;
            if (x + n >= L->W) break;
            if (i == s->tab) vgacon_color(PG_BG, VC_GREY); else vgacon_color(PG_FG, PG_BG);
            vgacon_cell(x, 1u, ' '); vgacon_text(x + 1u, 1u, n - 2u, s->tabs[i]); vgacon_cell(x + n - 1u, 1u, ' ');
            x += n + 1u;
        }
    } else {                                     /* one page name between arrows, and its number */
        char b[48]; unsigned int n;
        snprintf(b, sizeof b, "%c %s %c", TRI_L, s->tabs[s->tab], TRI_R);
        n = (unsigned int)strlen(b);
        vgacon_color(PG_BG, VC_GREY); vgacon_text(1u, 1u, n, b);
        snprintf(b, sizeof b, "%u/%u", s->tab + 1u, s->ntabs);
        vgacon_color(PG_FG, PG_BG); vgacon_text(L->W - 1u - (unsigned int)strlen(b), 1u, (unsigned int)strlen(b), b);
    }
}

static void frame(const lay_t *L)
{
    unsigned int y, top = 2u, bot = L->wide ? L->H - 3u : L->H - 5u;
    vgacon_color(LN_FG, PG_BG);
    vgacon_fill(1u, top, L->W - 2u, 1u, H2); vgacon_fill(1u, bot, L->W - 2u, 1u, H2);
    vgacon_cell(0u, top, TL2); vgacon_cell(L->W - 1u, top, TR2);
    vgacon_cell(0u, bot, BL2); vgacon_cell(L->W - 1u, bot, BR2);
    for (y = top + 1u; y < bot; y++) { vgacon_cell(0u, y, V2); vgacon_cell(L->W - 1u, y, V2); }
    if (L->wide) {
        vgacon_cell(L->split, top, TT2); vgacon_cell(L->split, bot, BT2);
        for (y = top + 1u; y < bot; y++) vgacon_cell(L->split, y, V1);
    }
}

static void item(const lay_t *L, const su_item_t *it, unsigned int y, int bar)
{
    unsigned int bg = bar ? BAR_BG : PG_BG, x1 = L->split - 1u;   /* the bar spans columns 1 .. split-1 */
    vgacon_color(PG_FG, bg); vgacon_fill(1u, y, x1, 1u, ' ');
    switch (it->kind) {
    case SU_SECTION: {
        unsigned int n = (unsigned int)strlen(it->label);
        if (n > x1 - 3u) n = x1 - 3u;
        vgacon_color(SEC_FG, PG_BG); vgacon_text(2u, y, n, it->label);
        vgacon_color(LN_FG, PG_BG); vgacon_fill(3u + n, y, x1 - 3u - n, 1u, H1);
        break;
    }
    case SU_VALUE:
        vgacon_color(bar ? BAR_FG : it->dim ? DIM_FG : PG_FG, bg); vgacon_text(L->lx, y, L->lw, it->label);
        vgacon_color(bar ? BAR_FG : it->dim ? DIM_FG : it->changed ? SEC_FG : VAL_FG, bg); bracket(L->vx, y, L->vw, it->value, 1);
        break;
    case SU_INFO:
        vgacon_color(PG_FG, bg); vgacon_text(L->lx, y, L->lw, it->label);
        vgacon_color(INF_FG, bg); bracket(L->vx, y, L->vw, it->value, 0);
        break;
    case SU_ACTION:
        vgacon_color(bar ? BAR_FG : it->dim ? DIM_FG : VAL_FG, bg);
        vgacon_cell(L->lx, y, TRI_R); vgacon_text(L->lx + 2u, y, L->vx - L->lx - 2u, it->label);
        if (it->value[0]) { vgacon_color(bar ? BAR_FG : it->dim ? DIM_FG : PG_FG, bg); bracket(L->vx, y, L->vw, it->value, 0); }
        break;
    default: break;
    }
}

static void help(const lay_t *L, const su_item_t *it)
{
    vgacon_color(PG_FG, PG_BG);
    if (L->wide) {
        unsigned int y = L->hy0;
        vgacon_color(SEC_FG, PG_BG); vgacon_text(L->hx, y++, L->hw, it->label);
        vgacon_color(LN_FG, PG_BG); vgacon_fill(L->hx, y++, L->hw, 1u, H1);
        vgacon_color(PG_FG, PG_BG);
        y = wrap(L->hx, y, L->hy1 - (it->key ? 2u : 0u), L->hw, it->help);
        if (it->key) {
            char b[40]; snprintf(b, sizeof b, "%s=", it->key);
            vgacon_color(DIM_FG, PG_BG); vgacon_text(L->hx, L->hy1 - 1u, L->hw, "AMIGA.INI key");
            vgacon_color(INF_FG, PG_BG); vgacon_text(L->hx, L->hy1, L->hw, b);
        }
    } else (void)wrap(L->hx, L->hy0, L->hy1, L->hw, it->help);
}

/* the key bar: pairs of key and action, keys in blue, as many as the width holds */
static void keybar(const lay_t *L)
{
    static const char *const wide[] = { "\x18\x19", "Select", "\x1B\x1A", "Page", "Enter", "Change",
                                        "Esc", "Leave", "F10", "Save and reset" };
    static const char *const narrow[] = { "\x18\x19", "Item", "\x1B\x1A", "Page", "Ent", "Set", "Esc", "Out" };
    const char *const *k = L->wide ? wide : narrow;
    unsigned int n = L->wide ? 10u : 8u, i, x = 1u, y = L->H - 1u;
    vgacon_color(POP_FG, POP_BG); vgacon_fill(0u, y, L->W, 1u, ' ');
    for (i = 0; i + 1u < n; i += 2u) {
        unsigned int a = (unsigned int)strlen(k[i]), b = (unsigned int)strlen(k[i + 1u]);
        if (x + a + 1u + b >= L->W) break;
        vgacon_color(VC_BLUE, POP_BG); vgacon_text(x, y, a, k[i]);
        vgacon_color(POP_FG, POP_BG); vgacon_text(x + a + 1u, y, b, k[i + 1u]);
        x += a + 1u + b + (L->wide ? 3u : 1u);
    }
}

void su_draw(su_screen_t *s)
{
    lay_t L; unsigned int rows, i;
    layout(&L);
    rows = L.r1 - L.r0 + 1u;
    if (s->sel < s->top) { s->top = s->sel; if (s->top && s->it[s->top - 1u].kind == SU_SECTION) s->top--; }
    if (s->sel >= s->top + rows) s->top = s->sel - rows + 1u;
    if (s->top + rows > s->n) s->top = s->n > rows ? s->n - rows : 0u;
    vgacon_frame_begin();
    vgacon_color(PG_FG, PG_BG); vgacon_clear();
    vgacon_header(s->title, s->right);
    tabs(&L, s);
    frame(&L);
    for (i = 0; i < rows; i++) {
        unsigned int k = s->top + i;
        if (k < s->n) item(&L, &s->it[k], L.r0 + i, k == s->sel);
    }
    vgacon_color(SEC_FG, PG_BG);
    if (s->top) vgacon_cell(L.split - 1u, L.r0, ARR_UP);
    if (s->top + rows < s->n) vgacon_cell(L.split - 1u, L.r1, ARR_DN);
    if (s->n) help(&L, &s->it[s->sel]);
    if (L.wide && s->status) { vgacon_color(SEC_FG, PG_BG); vgacon_text(1u, L.H - 2u, L.W - 2u, s->status); }
    keybar(&L);
    vgacon_frame_end();
}

/* ---- pop-ups ---------------------------------------------------------------------------------------------------- */
/* a box with a shadow and its title in the top line; the inside cleared */
static void box(unsigned int x, unsigned int y, unsigned int w, unsigned int h, const char *title)
{
    unsigned int i, n = title ? (unsigned int)strlen(title) : 0u;
    vgacon_color(VC_BLACK, VC_BLACK);
    vgacon_fill(x + 2u, y + h, w, 1u, ' '); vgacon_fill(x + w, y + 1u, 2u, h, ' ');
    vgacon_color(POP_LN, POP_BG);
    vgacon_fill(x, y, w, h, ' ');
    vgacon_fill(x + 1u, y, w - 2u, 1u, H2); vgacon_fill(x + 1u, y + h - 1u, w - 2u, 1u, H2);
    for (i = 1u; i + 1u < h; i++) { vgacon_cell(x, y + i, V2); vgacon_cell(x + w - 1u, y + i, V2); }
    vgacon_cell(x, y, TL2); vgacon_cell(x + w - 1u, y, TR2); vgacon_cell(x, y + h - 1u, BL2); vgacon_cell(x + w - 1u, y + h - 1u, BR2);
    if (n) {
        if (n + 4u > w) n = w - 4u;
        vgacon_color(VC_WHITE, POP_LN);
        vgacon_cell(x + (w - n) / 2u - 1u, y, ' '); vgacon_text(x + (w - n) / 2u, y, n, title); vgacon_cell(x + (w - n) / 2u + n, y, ' ');
    }
}

static unsigned int maxu(unsigned int a, unsigned int b) { return a > b ? a : b; }

int su_list(const char *title, const char *const *opt, unsigned int n, unsigned int cur, su_key_fn key)
{
    unsigned int W = vgacon_cols(), H = vgacon_rows(), w = (unsigned int)strlen(title) + 12u, rows, x, y, top = 0u, sel, i, k;
    if (!n) return -1;
    for (i = 0; i < n; i++) w = maxu(w, (unsigned int)strlen(opt[i]) + 8u);
    if (w > W - 4u) w = W - 4u;
    rows = H > 8u ? H - 8u : 1u; if (rows > n) rows = n;
    x = (W - w) / 2u; y = (H - rows - 2u) / 2u;
    sel = cur < n ? cur : 0u;
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1u;
        vgacon_frame_begin();
        box(x, y, w, rows + 2u, title);
        for (i = 0; i < rows; i++) {
            unsigned int j = top + i;
            vgacon_color(j == sel ? VC_WHITE : POP_FG, j == sel ? VC_BLUE : POP_BG);
            vgacon_fill(x + 1u, y + 1u + i, w - 2u, 1u, ' ');
            bracket(x + 2u, y + 1u + i, w - 4u, opt[j], 0);
        }
        vgacon_color(POP_LN, POP_BG);
        if (top) vgacon_cell(x + w - 1u, y + 1u, ARR_UP);
        if (top + rows < n) vgacon_cell(x + w - 1u, y + rows, ARR_DN);
        vgacon_frame_end();
        k = key();
        if (k & SU_UP) sel = sel ? sel - 1u : n - 1u;
        else if (k & SU_DOWN) sel = (sel + 1u) % n;
        else if (k & SU_LEFT) sel = sel > rows ? sel - rows : 0u;
        else if (k & SU_RIGHT) sel = sel + rows < n ? sel + rows : n - 1u;
        else if (k & SU_OK) return (int)sel;
        else if (k & (SU_BACK | SU_MENU)) return -1;
    }
}

int su_number(const char *title, const char *unit, int v, int lo, int hi, int big, su_key_fn key)
{
    unsigned int W = vgacon_cols(), H = vgacon_rows(), w = W > 40u ? 36u : W - 4u, x = (W - w) / 2u, y = (H - 7u) / 2u, k;
    char b[48], h[48];
    snprintf(h, sizeof h, "%c%c %u  %c%c %u  Enter OK", 0x1B, 0x1A, 1u, ARR_UP, ARR_DN, (unsigned int)big);
    for (;;) {
        unsigned int n;
        vgacon_frame_begin();
        box(x, y, w, 7u, title);
        snprintf(b, sizeof b, "%c %d %s %c", TRI_L, v, unit, TRI_R);
        n = (unsigned int)strlen(b);
        vgacon_color(VC_WHITE, VC_BLUE); vgacon_text(x + (w - n) / 2u, y + 2u, n, b);
        snprintf(b, sizeof b, "%d to %d", lo, hi); n = (unsigned int)strlen(b);
        vgacon_color(POP_FG, POP_BG); vgacon_text(x + (w - n) / 2u, y + 3u, n, b);
        n = (unsigned int)strlen(h); if (n > w - 4u) n = w - 4u;
        vgacon_text(x + (w - n) / 2u, y + 5u, n, h);
        vgacon_frame_end();
        k = key();
        if (k & SU_LEFT) v = v > lo ? v - 1 : lo;
        else if (k & SU_RIGHT) v = v < hi ? v + 1 : hi;
        else if (k & SU_DOWN) v = v - big > lo ? v - big : lo;
        else if (k & SU_UP) v = v + big < hi ? v + big : hi;
        else if (k & SU_OK) return v;
        else if (k & (SU_BACK | SU_MENU)) return -1;
    }
}

void su_message(const char *title, const char *text, su_key_fn key)
{
    unsigned int W = vgacon_cols(), H = vgacon_rows(), w = W > 64u ? 60u : W - 4u, x = (W - w) / 2u, h, y, k;
    h = wrapd(0u, 0u, H - 8u, w - 4u, text, 0) + 5u;          /* the text's rows, a blank row, the key row, the lines */
    y = (H - h) / 2u;
    for (;;) {
        vgacon_frame_begin();
        box(x, y, w, h, title);
        vgacon_color(POP_FG, POP_BG); (void)wrap(x + 2u, y + 2u, y + h - 4u, w - 4u, text);
        vgacon_color(VC_BLUE, POP_BG); vgacon_text(x + (w - 9u) / 2u, y + h - 2u, 9u, "Enter: OK");
        vgacon_frame_end();
        k = key();
        if (k & (SU_OK | SU_BACK | SU_MENU)) return;
    }
}
