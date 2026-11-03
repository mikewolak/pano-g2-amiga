/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* vtplay.c -- a file played through the terminal (term.c) for tests: the bytes go in at RATE bytes a second (0: as
 * fast as the terminal takes them), one term_tick a display frame (WaitTOF), the screen held HOLD seconds at the
 * end (or until Right Amiga Q), then the figures printed.
 *
 *   vtplay FILE [RATE=bytes/s] [HOLD=s] [BUDGET=us] [WINDOW] [JUMP] [MODE=id WIDTH=w HEIGHT=h]
 *   WINDOW: a window on the host screen, not full screen; JUMP: jump scrolling; MODE: the host is a screen opened in
 *   display mode `id` (hex), w x h, instead of the Workbench
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include "term.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;

static unsigned long long eclock(ULONG *f)
{
    struct EClockVal v;
    ULONG ef = ReadEClock(&v);
    if (f) *f = ef;
    return ((unsigned long long)v.ev_hi << 32) | v.ev_lo;
}

int main(void)
{
    LONG opt[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    struct Screen *hs = NULL;
    struct RDArgs *ra;
    struct timerequest tr;
    struct Screen *wb;
    ULONG mode, rate = 0, hold = 3, budget = 10000, ef, w, h;
    BPTR fh;
    term_t *t;
    static UBYTE buf[4096];
    LONG bn = 0, bi = 0;
    int eof = 0, run = 1;
    unsigned long long e0, e1, hold_until = 0, fed = 0;
    term_stats_t s;

    if (!(ra = ReadArgs((CONST_STRPTR)"FILE/A,RATE/N,HOLD/N,BUDGET/N,WINDOW/S,JUMP/S,MODE/K,WIDTH/N,HEIGHT/N", opt, NULL))) { printf("usage: vtplay FILE [RATE=bytes/s] [HOLD=s] [BUDGET=us]\n"); return 20; }
    if (opt[1]) rate = (ULONG)*(LONG *)opt[1];
    if (opt[2]) hold = (ULONG)*(LONG *)opt[2];
    if (opt[3]) budget = (ULONG)*(LONG *)opt[3];
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    memset(&tr, 0, sizeof tr);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0)) { printf("vtplay: no timer.device\n"); return 20; }
    TimerBase = tr.tr_node.io_Device;
    if (!(fh = Open((CONST_STRPTR)opt[0], MODE_OLDFILE))) { printf("vtplay: %s did not open\n", (char *)opt[0]); return 20; }
    wb = LockPubScreen((CONST_STRPTR)"Workbench");
    mode = GetVPModeID(&wb->ViewPort); w = wb->Width; h = wb->Height;
    if (opt[6]) {                                        /* a host screen in the mode asked for */
        mode = strtoul((char *)opt[6], NULL, 16);
        if (opt[7]) w = (ULONG)*(LONG *)opt[7];
        if (opt[8]) h = (ULONG)*(LONG *)opt[8];
        hs = OpenScreenTags(NULL, SA_DisplayID, mode, SA_Width, w, SA_Height, h, SA_Depth, 4, SA_Behind, TRUE,
                            SA_Quiet, TRUE, SA_Type, CUSTOMSCREEN, TAG_DONE);
        if (!hs) { printf("vtplay: mode 0x%08lx %lux%lu did not open\n", (unsigned long)mode, (unsigned long)w, (unsigned long)h); UnlockPubScreen(NULL, wb); Close(fh); return 20; }
    }
    t = term_open(hs ? hs : wb, NULL, opt[4] ? 0 : 1, NULL, NULL);
    UnlockPubScreen(NULL, wb);
    if (!t) { printf("vtplay: the terminal did not open\n"); Close(fh); return 20; }
    if (opt[5]) term_set_smooth(t, 0);
    e0 = eclock(&ef);
    while (run) {
        unsigned long long now = eclock(NULL);
        /* bytes in: at RATE, or as many as the queue takes */
        ULONG want = rate ? (ULONG)((now - e0) * rate / ef - fed) : 65535u;
        while (want && !eof) {
            ULONG k;
            if (bi >= bn) { bn = Read(fh, buf, sizeof buf); bi = 0; if (bn <= 0) { eof = 1; break; } }
            k = (ULONG)(bn - bi); if (k > want) k = want;
            k = term_input(t, buf + bi, k);
            if (!k) break;
            bi += (LONG)k; fed += k; want -= k;
        }
        if (eof && !term_pending(t) && !hold_until) { e1 = now; hold_until = now + (unsigned long long)hold * ef; }
        if (hold_until && now >= hold_until) run = 0;
        WaitTOF();
        term_tick(t, budget);
        if (SetSignal(0, 0) & term_sigmask(t)) if (!term_events(t)) run = 0;
    }
    if (!hold_until) e1 = eclock(NULL);
    term_stats(t, &s);
    term_close(t);
    if (hs) CloseScreen(hs);
    Close(fh);
    {
        ULONG ms = (ULONG)((e1 - e0) * 1000ull / ef);
        printf("vtplay: %lu bytes in %lu.%03lu s (%lu bytes/s); %lu frames, %lu ticks over the budget of %lu us, "
               "longest tick %lu us; %lu screen scrolls, %lu jumps; mode 0x%08lx %lux%lu\n",
               (unsigned long)s.bytes, (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u),
               (unsigned long)(ms ? (unsigned long long)s.bytes * 1000u / ms : 0), (unsigned long)s.frames,
               (unsigned long)s.late, (unsigned long)budget, (unsigned long)s.max_us, (unsigned long)s.scrolls,
               (unsigned long)s.jumps, (unsigned long)mode, (unsigned long)w, (unsigned long)h);
    }
    printf("vtplay: %lu bytes by the plain-text path\n", (unsigned long)s.fast);
    printf("vtplay: %lu cells drawn, %lu mirror lines; us a cell: get %lu, draw %lu; vterm_input_write %lu ms, mirror copies %lu ms\n",
           (unsigned long)s.cells, (unsigned long)s.mirrors, (unsigned long)(s.cells ? (unsigned long long)s.e_get * 1000000u / ef / s.cells : 0),
           (unsigned long)(s.cells ? (unsigned long long)s.e_put * 1000000u / ef / s.cells : 0),
           (unsigned long)((unsigned long long)s.e_vt * 1000u / ef), (unsigned long)((unsigned long long)s.e_mir * 1000u / ef));
    CloseDevice((struct IORequest *)&tr);
    FreeArgs(ra);
    return 0;
}
