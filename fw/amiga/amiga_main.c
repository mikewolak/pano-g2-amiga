/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/amiga_main.c -- the firmware of the G2 Amiga fabric: stick, settings, Kickstart,
 * floppy images, keyboard, mouse and pads to the core's companion modules, the sector server, the frame buffers on
 * the heads, the audio interrupt and the menu.
 *
 * Runs from DDR2 chip A (fw/link_ddr.ld at 0x1000_0000), started by fw/amiga/amiga_ldr.c.  Drivers are the G2 DDR
 * shell's, used as fw/snes/snes_main.c uses them: fw/usb.c + fw/usb_ehci.c (EHCI host, split stagger and async holds),
 * fw/usbmsd.c + fw/diskio_msd_g2.c + FatFs R0.16 (stick), fw/usbpad.c (DualShock 3/4), fw/usbhid.c (boot keyboard
 * and boot mouse), fw/vga.c + fw/vgacon.c + fw/ch7301.c (heads and text console), fw/codec_g2.c (WM8750BL), and
 * fw/snes/snes_audio.c (resampler and codec refill) built with AUD_IN_HZ 48,000.
 *
 * THE STICK.  Directory \amiga: Kickstart images (*.rom, 262,144 or 524,288 bytes), floppy images (*.adf, at most
 * 1,802,240 bytes), AMIGA.INI.  Nothing outside \amiga is read or written.
 * AMIGA.INI (\amiga\AMIGA.INI, first 1,024 bytes, one KEY=value a line, keys in any case):
 *   KICK=<file>     the Kickstart image in \amiga                         default: KICK.ROM, else the first *.rom
 *   DF0=<file> .. DF3=<file>   floppy images in \amiga; an empty value is an empty drive    default: DF0 the first
 *                  *.adf when there is no DF0 line and no HD0, else empty.  A change from the menu is written back here
 *                  (ini_set), so the next boot starts with the same disks
 *   HD0=<file>, HD1=<file>     IDE hard-disk images (*.hdf with a RigidDiskBlock, the core takes the geometry from
 *                  it) in \amiga, Gayle IDE master and slave; with either set, sysctrl "I" 1   default: none
 *   CHIP=0..3       chip memory 512 KB, 1 MB, 1.5 MB, 2 MB (sysctrl "Y")  default 3 (2 MB; the ECS chipset addresses it)
 *   SLOW=0..3       slow memory none, 512 KB, 1 MB, 1.5 MB (sysctrl "X")   default 1
 *   FAST=0..3       fast memory none, 2 MB, 4 MB, 8 MB: one Zorro II board at $200000 (sysctrl "Q"; amiga_g2.v FAST RAM)
 *                  default 0, A1200 profile 3
 *   CHIPSET=0..3    OCS-A500, OCS-A1000, ECS, AGA (sysctrl "C"; AGA needs the G2_AMIGA_AGA fabric)   default 2
 *   CPU=68000|68010|68020   sysctrl "T" (the 68010 and 68020 need the G2_AMIGA_AGA fabric's TG68K)   default 68000
 *   MACHINE=A500|A1200      the defaults of a profile, each overridden by its own key: A500 68000, ECS, 2 MB chip,
 *                  512 KB slow, KICK.ROM; A1200 68020, AGA, 2 MB chip, no slow, KICK_A1200.ROM   default A500
 *   WRITE=0|1       floppy writes off (write protected) or on             default 1
 *   SCREEN=0|1|2    micro-HDMI head, DVI-I head, both                     default 2
 *   SHARE=<dir>     the stick directory the Amiga's SHARE: volume shows (fw/amiga/share.c; the firmware loaded from
 *                  \amiga\AMIGA.FW only)                                                    default /amiga/share
 *   TZ=[+|-]h[:mm]  the time zone, east of UTC: the battery clock ($DC0000) and the dates afs writes hold local time
 *                  (NTP plus the zone); the menu's Time zone item sets it                      default 0
 *   NTPWAIT=0..120  seconds after start-up the first start of the machine waits for NTP, so Kickstart finds the clock
 *                  set; 0 starts at once                                                         default 20
 *   VIDEO=SCALE|NATIVE SCALE: head mode 5 (640x480, 31 kHz), every Amiga mode scaled to it; NATIVE: 15 kHz, head mode
 *                  7 (640x288, 50.15 Hz) with REGION=PAL, 3 (640x240, 59.72 Hz) with NTSC, the menu in mode 4 (the
 *                  40x17 console, fw/snes/snes_main.c NATIVE)   default: SCALE
 *   HOFS=0..400     the first captured sample of a 15 kHz line, in samples of the 14 MHz grid after hblank ends
 *                  (AMI_VID hofs); the console's `vid hofs <n>' sets it at run time       default 77
 *   REGION=NTSC|PAL NTSC: 60 Hz, 262 lines, 28.636364 MHz; PAL: 50 Hz, 312 lines, 28.375160 MHz (sysctrl "V",
 *                  the slot's NCO and audio NCO, the heads' source height 240 or 288)   default: NTSC
 *   FTDI=SERIAL|MPSSE an FTDI chip on the hub carries Paula's or the serial FIFO board's bytes: SERIAL as a serial
 *                  adapter (rate from SERPER or the driver); MPSSE: a multi-channel chip's channel A (FT2232H) in MPSSE
 *                  mode, the bytes MPSSE commands and their answers (JTAG, SPI; FTDI AN_108); the menu item FTDI
 *                  sets it                                                                     default: SERIAL
 * The CPU and chipset come from the CPU=, CHIPSET= and MACHINE= keys.
 *
 * FLOPPY IMAGES are read whole into chip A (AMI_DISK_UC + drive x 2 MB) when inserted; the sector server answers
 * every request of the core from there.  A sector the core writes goes to the copy and to the file at once (f_write),
 * and the file is synchronised (f_sync) once no sector has been written for 1 s.
 * BOOT.LOG (\amiga\BOOT.LOG): every console line of this boot (the newest 32 KB; the menu item Log shows it), with a time stamp in milliseconds
 * since the firmware started, and a status line a minute while the machine runs; the file is rewritten from the main
 * loop only (never from an interrupt) when the machine is prepared, when a failure waits for a key, when the menu
 * opens, before an exit, and every 60 s while there are new lines.  The console UART (the micro-HDMI DDC pins) has
 * no listener on a unit.
 * NEW HARD-DISK IMAGES are made from the HD0/HD1 chooser (hd_create): NEW_<size>M.hdf in \amiga, 128 MB to 1 GB, with a
 * RigidDiskBlock (16 heads, 63 sectors a track, RDSK at block 0, one bootable partition DH<n> of DosType DOS\3 from
 * cylinder 2 to the last); the partition is unformatted, Workbench formats it.
 * HARD-DISK IMAGES stay on the stick, read and written through a write-back cache of 128 aligned 64 KB blocks in chip A
 * (HDC_MEM, 8 MB, shared by both drives, least recently used replaced): a miss reads the block whole, a write changes
 * the cached block only, and a changed block goes to the file in one 64 KB write when it is replaced, when no sector
 * has been written for 1 s, after an afs command, before a console command and before the file is closed.
 * INPUT.  Keyboard: every key and modifier change goes to hid.v command 1 as {release[7], code[6:0]}, the code the
 * HID usage (modifiers 0x68 + bit, nanomig src/misc/amiga_keymap.v); F12 opens the menu instead.  Mouse: hid.v
 * command 2 {buttons, dx, dy}.  Pads: pad 1 to the joystick port (game port 2), pad 2 to the mouse port (game port 1), hid.v command 3,
 * bits {fire2[5], fire[4], up[3], down[2], left[1], right[0]}: Cross fire, Circle fire 2, d-pad or left stick; PS
 * opens the menu.
 * SETUP (F12, PS, the unit's front-panel button): the core stopped (CTRL run 0) and the setup screen on the heads
 * (setup.c, setup_items.h): six pages, Machine (profile, CPU, chipset, chip, slow and fast memory, Kickstart), Drives
 * (DF0 to DF3, disk writes, HD0 and HD1: choose an image, make a new one, or none), Video (region, output mode,
 * outputs, picture offset), Peripherals (FTDI mode, the USB devices found, the SHARE: folder), Network (time zone, NTP
 * wait, the clock, the Ethernet state) and System (resume, save and reset, discard, reset, the boot log, exit to the
 * golden image).  Every AMIGA.INI key has an item.  The machine's configuration is changed in setup and written by
 * Save and reset (F10, Triangle: a dialog first; with nothing changed, a cold start), or given back by Discard; drives, zone, FTDI mode and the picture keys are written
 * at once.  Up/Down an item, Left/Right a page, Enter a list of the item's values, Escape leaves (a dialog when
 * something is not saved).  A keyboard's LCtrl + LAmiga + RAmiga resets the machine through hid.v. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "afs.h"
#include "xsurf.h"
#include "setup_items.h"
#include "../ntptime.h"
#include "../console.h"
#include "../board.h"
#include "../g2.h"
#include "../trap.h"
#include "../timer_ms.h"
#include "../usb.h"
#include "../usbmsd.h"
#include "../usbpad.h"
#include "../usbhid.h"
#include "../usbmidi.h"
#include "../usbftdi.h"
#include "../vga.h"
#include "../vgacon.h"
#include "../ch7301.h"
#include "../codec_g2.h"
#include "../crc32.h"
#ifdef AMIGA_PFLASH
#include "pflash.h"
#endif
#include "../netsh.h"
#include "../ethmac.h"
#include "../../tools/panoldr/panoldr.h"
#ifdef AMIGA_SOCK
#include "sock.h"
#endif
#ifdef AMIGA_SHARE
#include "share.h"
#endif
#ifndef FW_ID
#define FW_ID 0u                                  /* the git commit the firmware was built from (Makefile) */
#endif
#include "../g2_regs.h"
#include "ff.h"
#include "amiga_regs.h"
#include "amiga_flash.h"
#include "../snes/snes_regs.h"
#include "../snes/snes_audio.h"

#define T_RESET_US   2000u
#define T_PORT_US    60000u
#define USB_PORTS    4u                          /* the USB2514's downstream ports */
#define CON_OFF      0x00600000u                 /* the text console's buffers in head 1's window (as the DDR shell) */
#define PAD_SLOT(i)  (3u + (i))
#define KBD_SLOT     1u
#define MOUSE_SLOT   2u
#define MAX_FILES    512u
#define DRIVES       4u
#define ADF_MAX      1802240u                    /* 84 cylinders x 2 heads x 22 sectors x 512 bytes (HD) */
#define VID_W        640u                        /* captured samples a line: 640 of the 14 MHz grid, hires 1:1 */
#define VID_HOFS     77u                         /* the standard display window (Workbench, DPaint IV AGA hires): its
                                                    first pixel is sample 77 after hblank, measured with snap on a G2 revision C unit
                                                    (76 and 78 each put the border into the 640 samples) */
#define SRC_H_NTSC   240u                        /* heads' source height: the NTSC picture with its border */
#define SRC_H_PAL    288u                        /* the PAL picture with its border */
#define SRC_H        (s_pal ? SRC_H_PAL : SRC_H_NTSC)
#define VID_HOFS_FAST 57u                        /* 31 kHz lines: the first sample after hblank, in 28 MHz samples */

#ifdef AMIGA_SIM
msd_t *diskio_msds[4];                           /* simulation: no USB storage; the RAM disk (fw/snes/diskio_ram.c) */
#else
extern msd_t *diskio_msds[4];
#endif

/* Milliseconds from the 64-bit cycle counter (fw/snes/snes_main.c snes_ms). */
static uint32_t ami_ms(void)
{
    uint32_t hi, lo, hi2;
    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi));
        __asm__ volatile ("csrr %0, mcycle" : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi2));
    } while (hi != hi2);
    return (uint32_t)((((uint64_t)hi << 32) | lo) / (uint64_t)PANO_CYC_PER_MS);
}
#define get_millis ami_ms
static void iprog_exit_now(void);

/* ---- small helpers --------------------------------------------------------------------------------------------- */
static void wait_us(unsigned int us)
{
    unsigned int c0 = rdcycle();
    while (rdcycle() - c0 < us * PANO_CYC_PER_US) { }
}
#define LOG_MAX 32768u
static char     s_log[LOG_MAX];
static uint32_t s_log_n, s_log_saved, s_log_bol = 1u;
static int      s_log_ok;                        /* the stick is mounted: BOOT.LOG may be written */
static void log_put(const char *s)
{
    for (; *s; s++) {
        if (s_log_n >= LOG_MAX - 16u) {          /* full: the older half dropped at a line start, the newest lines kept */
            uint32_t k = LOG_MAX / 2u;
            while (k < s_log_n && s_log[k - 1u] != '\n') k++;
            memmove(s_log, s_log + k, s_log_n - k); s_log_n -= k; s_log_saved = 0xFFFFFFFFu;
        }
        if (s_log_bol) {                         /* "[  12345] " at the start of each line */
            char b[12]; int i = 10; uint32_t v = get_millis(); b[11] = 0; b[10] = ']';
            do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i > 1);
            while (i > 1) b[--i] = ' ';
            b[0] = '[';
            for (i = 0; b[i]; i++) s_log[s_log_n++] = b[i];
            s_log[s_log_n++] = ' ';
            s_log_bol = 0u;
        }
        if (*s == '\n') { s_log[s_log_n++] = '\r'; s_log_bol = 1u; }
        s_log[s_log_n++] = *s;
    }
}
static unsigned int s_con_quiet;                  /* 1 while the setup screen is shown: say() to the UART and the log only */
static void say(const char *s) { con_puts(s); if (vgacon_ready() && !s_con_quiet) vgacon_puts(s); log_put(s); }
/* \amiga\BOOT.LOG rewritten with every line so far, when there is something new */
static void log_save(void)
{
    static FIL f; UINT w;
    if (!s_log_ok || s_log_n == s_log_saved) return;
    if (f_open(&f, "/amiga/BOOT.LOG", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
    (void)f_write(&f, s_log, s_log_n, &w);
    f_close(&f);
    s_log_saved = s_log_n;
}
static void saydec(uint32_t v)
{
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i);
    say(&b[i]);
}
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int ci_cmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}
static int ends_with(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    return n > m && ci_cmp(s + n - m, ext) == 0;
}

/* ---- heads (fw/snes/snes_main.c, 31 kHz only) ------------------------------------------------------------------ */
#define HEAD_R(h, o) (*(volatile uint32_t *)((h) ? (0x80007000u + (o)) : (0x80006000u + (o))))
static unsigned int s_screen = 2u;               /* AMIGA.INI SCREEN= */
static unsigned int s_hofs = VID_HOFS;           /* AMIGA.INI HOFS=, console `vid hofs' */
static unsigned int s_pal;                       /* AMIGA.INI REGION=PAL: 50 Hz, 312 lines, the PAL clock */
static unsigned int s_native;                    /* AMIGA.INI VIDEO=NATIVE: 15 kHz head modes */
static unsigned int s_src_h;                     /* the heads' source height now */
static unsigned int s_rtc_due;                     /* the battery clock to be loaded again now (a zone change) */
static unsigned int s_ntpwait = 20u;               /* AMIGA.INI NTPWAIT=: seconds after start-up the first start waits for NTP */
static unsigned int s_heads = 3u;                /* bit 0 head 1, bit 1 head 2 */

static void console_on(void)
{
    vgacon_head_mode(0u);
    vgacon_double(1);                            /* two console buffers from CON_OFF, below AMI_FB_OFF: the menu is
                                                    composed off screen, shown at a vertical blank */
    vga_select(0u); vgacon_init(VC_BOOT, 0x94000000u + CON_OFF); (void)vgacon_geometry(s_native ? VC_ZOOM : VC_BOOT);
    vga_select(1u); vgacon_mirror(0x01000000u); (void)vgacon_geometry(s_native ? VC_ZOOM : VC_BOOT); vga_select(0u);
}

static void video_boot(void)
{
    (void)ch7301_init(s_native ? 4u : 0u);
    (void)ch7301_write(0x75u, 0x23u, 0x04u);    /* HPDD: outputs on without a hot-plug level */
    (void)ch7301_write(0x76u, 0x23u, 0x04u);
    HEAD_R(0, 0x24) = 0u; HEAD_R(1, 0x24) = 0u;  /* both heads read the MCB port */
    vgacon_color(VC_GREY, VC_BLACK);
    console_on();
}

/* the Amiga picture: head mode 5 (640x480, 31 kHz; VIDEO=SCALE) or 7 / 3 (640x288 / 640x240, 15 kHz; VIDEO=NATIVE),
 * RGB565, source VID_W x s_src_h from the frame buffers at a line pitch of 2 x VID_W bytes (the head's pitch is
 * twice its source width).  s_src_h is SRC_H until a frame of 31 kHz or interlaced lines arrives (frame_irq). */
static void ami_heads_on(unsigned int buf)
{
    unsigned int h, mode = s_native ? (s_pal ? 7u : 3u) : 5u;
    s_src_h = SRC_H;
    for (h = 0; h < 2u; h++) {
        if (!(s_heads & (1u << h))) { HEAD_R(h, 0x00) = 0u; continue; }
        HEAD_R(h, 0x04) = (AMI_FB_OFF + buf * AMI_FB_STEP) >> 1;
        HEAD_R(h, 0x08) = (s_src_h << 16) | VID_W;
        HEAD_R(h, 0x0C) = 1u;
        HEAD_R(h, 0x00) = (1u << 5) | (mode << 1) | 1u;          /* fmt565, mode, en */
    }
}
static void heads_show(unsigned int buf)
{
    unsigned int h;
    for (h = 0; h < 2u; h++) if (s_heads & (1u << h)) { HEAD_R(h, 0x04) = (AMI_FB_OFF + buf * AMI_FB_STEP) >> 1; HEAD_R(h, 0x0C) = 1u; }
}
#ifndef AMIGA_SIM
/* all rows of the three buffers black in the windows in use, before the picture starts */
static void fb_black(void)
{
    unsigned int w, b;
    uint32_t i, n = SRC_H * VID_W * 2u / 4u;
    for (w = 0; w < 2u; w++) {
        if (!(s_heads & (1u << w))) continue;
        for (b = 0; b < 3u; b++) {
            volatile uint32_t *p = (volatile uint32_t *)(0x94000000u + w * AMI_HEAD2_DELTA + AMI_FB_OFF + b * AMI_FB_STEP);
            for (i = 0; i < n; i++) p[i] = 0u;
            g2_wdt_kick();
        }
    }
}
#endif

/* ---- the frame interrupt: the heads take the last complete buffer (snes_main.c frame_irq without the frame lock:
 * the core runs at the nominal NTSC rate, 59.94 Hz, against the head's 60.10 Hz, and the three buffers absorb the
 * difference) ------------------------------------------------------------------------------------------------------- */
static volatile uint32_t s_frames;
static volatile uint32_t s_mcu_pend;             /* the companion interrupt is pending: line masked until acknowledged */
/* INTERRUPT LINE G2_IRQ_LINE_AMI carries the page's frame bit (CTRL frm_irq_en), the audio watermark and, with no
 * enable bit, sysctrl.v's interrupt (IRQ bit 2: int_out_n, set from reset by the cold-boot notification).  That one is
 * acknowledged only through MCU_TX (command 5 with bit 0, command 6 for the cold-boot flag), and MCU_TX bytes reach
 * the core only while its clock runs, so the line is unmasked after cold_start's acknowledge, and masked by the
 * handler whenever the companion bit is set again (a reset of the core sets it) until the main loop acknowledges. */
static void irq_line(int on)
{
    if (on) __asm__ volatile ("csrs 0xBC0, %0" :: "r"(1u << G2_IRQ_LINE_AMI));
    else    __asm__ volatile ("csrc 0xBC0, %0" :: "r"(1u << G2_IRQ_LINE_AMI));
}
static void frame_irq(void)
{
    uint32_t fi = AMI_FRAME_INFO;
    AMI_IRQ = 1u;
    {   /* the buffer the selected head scans now (its read-back base), for the writer's choice of the next buffer */
        unsigned int h = (s_heads & 2u) ? 1u : 0u, b = ((HEAD_R(h, 0x0C) << 1) - AMI_FB_OFF) / AMI_FB_STEP;
        AMI_DISP = b < 3u ? b : 3u;
    }
    {   /* the source height: the frame's own lines (twice for interlace) once they are 31 kHz or interlaced lines */
        uint32_t ln = (fi >> 16) & 0x3FFu, hh = ((fi >> 26) & 1u) || (fi & 4u) ? ln << ((fi >> 2) & 1u) : SRC_H;
        unsigned int h;
        if (hh >= 200u && hh <= 768u && hh != s_src_h) {
            s_src_h = hh;
            for (h = 0; h < 2u; h++) if (s_heads & (1u << h)) HEAD_R(h, 0x08) = (hh << 16) | VID_W;
        }
    }
    heads_show(fi & 3u);
    s_frames++;
}

static void midi_pump(void);
/* the USB host's interrupt (externalInterruptArray[3]): USBSTS.USBINT, raised when a qTD with IOC completes; only the
 * MIDI slots carry IOC */
#define G2_IRQ_LINE_USB 3u
static void usb_irq_line(int on)
{
    if (on) __asm__ volatile ("csrs 0xBC0, %0" :: "r"(1u << G2_IRQ_LINE_USB));
    else    __asm__ volatile ("csrc 0xBC0, %0" :: "r"(1u << G2_IRQ_LINE_USB));
}
static uint32_t ami_irq(uint32_t cause, uint32_t epc, struct trap_frame *f)
{
    uint32_t pend;
    (void)cause; (void)f;
    __asm__ volatile ("csrr %0, 0xFC0" : "=r"(pend));
    if (pend & (1u << G2_IRQ_LINE_USB)) (void)usb_int_irq_ack();
    if (pend & (1u << G2_IRQ_LINE_AMI)) {
        if (AMI_IRQ & 4u) { irq_line(0); s_mcu_pend = 1u; }   /* the companion interrupt: acknowledged from the main loop */
        if (AMI_IRQ & 1u) frame_irq();
        if ((AUD_WMARK & 0x80000000u) && (AUD_STATUS & 0x1FFFu) < (AUD_WMARK & 0x1FFFu)) snes_audio_service();
    }
    if (pend & ((1u << G2_IRQ_LINE_USB) | (1u << G2_IRQ_LINE_AMI))) midi_pump();
    return epc;
}

/* ---- USB: host, hub, stick, pads, keyboard, mouse (fw/snes/snes_main.c) --------------------------------------- */
static FATFS s_fs;
static msd_t s_drive[USB_PORTS];
static int   s_stick_port = -1;
static usb_pad_t s_pad[2];
static usb_hid_t s_kbd_h, s_mouse_h;
static usb_kbd_t s_kbd;
static int   s_have_kbd, s_have_mouse;
#define MIDI_SLOT     5u                            /* fw/usb_ehci.c interrupt slots: the MIDI device's bulk IN */
#define MIDI_OUT_SLOT 6u                            /* and its bulk OUT */
#define MIDI_ERR_MAX  8u                            /* failed transfers in a row before the device is released */
/* Paula's bytes go to the device as they arrive.  Paula's divider paces them below the DIN wire's 31,250 baud, as on
 * an Amiga: SERPER 113 from the PAL colour clock (3,546,895 Hz / 114 = 31,113 baud, -0.44 %), SERPER 114 from the
 * NTSC one (3,579,545 Hz / 115 = 31,126 baud, -0.39 %); the core keeps those clocks exactly (amiga_g2.v CLOCK GATE,
 * without sdc_hold).  When the OUT queue has no room the serial bridge's interrupt is masked; the USB interrupt of
 * the finished transfer takes the bytes waiting in its 1,023-byte RX FIFO, and a full RX FIFO holds Paula's
 * transmitter (rtl/g2/amiga_serbridge.v), so nothing is lost. */
static usb_midi_t   s_midi;
static usb_ftdi_t s_ftdi;                         /* an FTDI USB serial adapter: Paula's serial port instead of MIDI */
static uint32_t   s_ftdi_serper = 0xFFFFFFFFu;     /* the SERPER the adapter's rate was last set from */
static uint32_t   s_ftdi_mo = 0xFFFFFFFFu;         /* CIA-B's {DTR, RTS} last sent to the adapter */
static uint32_t   s_ftdi_force;                    /* console `midi baud': a fixed rate instead of SERPER's; 0 none */
static uint32_t   s_ftdi_mi = 0xFFFFFFFFu;         /* the {RI, CD, DSR, CTS} last given to CIA-B */
static unsigned int s_ftdi_want_mpsse;             /* AMIGA.INI FTDI=MPSSE: a multi-channel chip's channel A in MPSSE */
static unsigned char s_frx[USB_FTDI_IN_MAX];       /* the adapter's last IN packet, given to the bridge as it has room */
static unsigned int s_frx_n, s_frx_i;
static uint32_t     s_ser_irq;                      /* ACTRL_SER_IRQ or 0: the CTRL bit the handler last chose */
static unsigned int s_midi_out_max = USB_MIDI_OUT_MAX;  /* console `midi max': bytes of event packets in one OUT transfer */
/* console `midi trace': the MIDI path in the order it happened, records of {dir, length, mcycle (4 bytes, little
 * endian), the bytes}: dir 0 the event packets of an OUT transfer as it starts, 1 those of an IN transfer as it
 * completes (fw/usbmidi.c usb_midi_trace), 2 bytes taken from Paula's transmitter, 3 bytes for Paula's receiver.
 * Recording stops when the buffer is full; `midi trace save' writes it to \amiga\MIDITRC.BIN. */
#define MTRC_BYTES (2u << 20)
static uint8_t  s_mtrc[MTRC_BYTES];
static volatile uint32_t s_mtrc_n, s_mtrc_on, s_mtrc_lost;
static void mtrc_rec(unsigned int dir, const unsigned char *p, unsigned int n)
{
    uint32_t t, i = s_mtrc_n;
    if (!s_mtrc_on || !n) return;
    if (n > 255u) n = 255u;
    if (i + 6u + n > MTRC_BYTES) { s_mtrc_lost++; return; }
    __asm__ volatile ("csrr %0, mcycle" : "=r"(t));
    s_mtrc[i] = (uint8_t)dir; s_mtrc[i + 1u] = (uint8_t)n;
    s_mtrc[i + 2u] = (uint8_t)t; s_mtrc[i + 3u] = (uint8_t)(t >> 8); s_mtrc[i + 4u] = (uint8_t)(t >> 16); s_mtrc[i + 5u] = (uint8_t)(t >> 24);
    memcpy(s_mtrc + i + 6u, p, n);
    s_mtrc_n = i + 6u + n;
}
/* CTRL written by the main loop: with interrupts off, so the handler's ACTRL_SER_IRQ is kept */
static void ctrl_set(uint32_t v)
{
    uint32_t ms;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    AMI_CTRL = (v & ~ACTRL_SER_IRQ) | s_ser_irq;
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
}
static void ctrl_clr(uint32_t m)
{
    uint32_t ms;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    AMI_CTRL = AMI_CTRL & ~m;
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
}

static int usb_up(void)
{
    unsigned int ps, attempt, t_end, ok = 0u;
    say("USB: starting the EHCI host\n");
    ps = usb_host_init(wait_us, T_RESET_US, T_PORT_US);
    for (attempt = 0u; attempt < 5u && ps != 0x00001005u; attempt++) {
        g2_wdt_kick();
        ps = usb_host_init(wait_us, T_RESET_US, T_PORT_US);
    }
    if (ps != 0x00001005u) { say("USB: root port did not start\n"); return -1; }
    wait_us(300000u);
    t_end = get_millis() + 8000u; attempt = 0u;
    while ((int)(t_end - get_millis()) > 0) {
        g2_wdt_kick();
        if (usb_enum_hubs(wait_us, T_RESET_US, T_PORT_US) == 0) { ok = 1u; break; }
        if (++attempt == 3u) (void)usb_host_init(wait_us, T_RESET_US, T_PORT_US);
        wait_us(200000u);
    }
    say(ok ? "USB: hub ready\n" : "USB: hub did not start\n");
    return ok ? 0 : -1;
}

#define STK_NODEV   1                               /* no device or no device descriptor */
#define STK_MSD     2                               /* msd_open failed: s_stick_code = its MSD_ code */
#define STK_FAT     3                               /* f_mount failed: s_stick_code = its FRESULT */
static unsigned int s_stick_why[USB_PORTS + 1u];
static int          s_stick_code[USB_PORTS + 1u];

static const char *fr_name(FRESULT r)
{
    static const char *const n[] = { "FR_OK", "FR_DISK_ERR", "FR_INT_ERR", "FR_NOT_READY", "FR_NO_FILE", "FR_NO_PATH",
        "FR_INVALID_NAME", "FR_DENIED", "FR_EXIST", "FR_INVALID_OBJECT", "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE",
        "FR_NOT_ENABLED", "FR_NO_FILESYSTEM", "FR_MKFS_ABORTED", "FR_TIMEOUT", "FR_LOCKED", "FR_NOT_ENOUGH_CORE",
        "FR_TOO_MANY_OPEN_FILES", "FR_INVALID_PARAMETER" };
    return (unsigned int)r < sizeof n / sizeof n[0] ? n[r] : "FR_?";
}
static void say_fr(FRESULT r) { say("FatFs error "); saydec((uint32_t)r); say(" "); say(fr_name(r)); }

#ifdef AMIGA_BOOT
/* THE LOADER (Makefile amiga_g2.img, -DAMIGA_BOOT): the firmware in flash only starts \amiga\AMIGA.FW, the firmware
 * that runs the Amiga (Makefile target amiga_fw).  After the stick is mounted, fw_stick checks its tools/panoldr header
 * (magic numbers, header CRC32, the length against the file's, load 0x1000_0000 (Makefile AMIGA_ORIGIN), an entry in
 * DDR2 chip A, at most FW_STAGE_MAX bytes), reads the payload to FW_STAGE and checks its CRC32, stops the Ethernet MAC
 * and the USB host controller and enters fw_tramp, copied to FW_TRAMP: the copy to the load address, fence.i, the
 * jump.  When there is no stick, no file, or a check fails, the reason is shown and the loader stays at its network
 * console (put the file, then reboot); it never starts the Amiga.  FW_STAGE and FW_TRAMP lie above both images and
 * their bss, below the end of chip A's mapped window. */
#define FW_STICK      "/amiga/AMIGA.FW"
#define FW_STAGE      0x11000000u
#define FW_STAGE_MAX  0x00800000u
#define FW_TRAMP      0x11F00000u
#define UNCACHED(a)   (((a) & 0x0FFFFFFFu) + 0x90000000u)
/* The data cache is write-back (fw/bootsh.c mem_sync): fw_tramp first writes back and invalidates (cbo.flush) every
 * line of [a4, a5), the cached window both images and their bss use, then copies a2 words from a1 to a0 (both
 * uncached), fence.i, and jumps to a3.  Position independent, registers only: no cached line is made between the
 * flush and the jump. */
extern const uint32_t fw_tramp_s[], fw_tramp_e[];
__asm__(".section .text.fw_tramp,\"ax\"\n\t.p2align 2\n\t.globl fw_tramp_s\nfw_tramp_s:\n"
        "0:\tbgeu a4, a5, 1f\n\t.insn i 0x0F, 2, x0, a4, 2\n\taddi a4, a4, 64\n\tj 0b\n"
        "1:\tfence rw, rw\n"
        "2:\tbeqz a2, 3f\n\tlw t0, 0(a1)\n\tsw t0, 0(a0)\n\taddi a0, a0, 4\n\taddi a1, a1, 4\n\taddi a2, a2, -1\n\tj 2b\n"
        "3:\tfence rw, rw\n\tfence.i\n\tjr a3\n\t.globl fw_tramp_e\nfw_tramp_e:\n\t.text\n");
/* every line of [a, end) of the cached window written back and invalidated (fw/bootsh.c mem_sync) */
static void fw_flush(uint32_t a, uint32_t end)
{
    for (a &= ~63u; a < end; a += 64u) {
        __asm__ volatile (".insn i 0x0F, 2, x0, %0, 2" :: "r"(a) : "memory");   /* cbo.flush (a) */
        if (!(a & 0xFFFFu)) g2_wdt_kick();
    }
    __asm__ volatile ("fence rw, rw" ::: "memory");
}
/* AMIGA.FW started; returns only when it cannot be, with the reason shown */
static void fw_stick(void)
{
    FIL f; UINT n; panoldr_hdr_t h; uint32_t off, crc, hc; const char *why = 0;
    if (f_open(&f, FW_STICK, FA_READ) != FR_OK) { say("Loader: no \\amiga\\AMIGA.FW on the stick\n"); return; }
    if (f_read(&f, &h, sizeof h, &n) != FR_OK || n != sizeof h) why = "header not read";
    else if (h.magic0 != PANOLDR_MAGIC0 || h.magic1 != PANOLDR_MAGIC1) why = "not a PANOLDR image";
    else if ((hc = crc32_run(0xFFFFFFFFu, (const uint8_t *)&h, 28u) ^ 0xFFFFFFFFu) != h.hdr_crc32) why = "header CRC mismatch";
    else if (!h.length || h.length > FW_STAGE_MAX || h.length + PANOLDR_HDR_BYTES != f_size(&f)) why = "length not credible";
    else if (h.load != 0x10000000u || (h.entry & 0xFC000000u) != 0x10000000u) why = "load or entry address not 0x1000_0000";
    else {
        fw_flush(FW_STAGE, 0x12000000u);               /* staging and trampoline: written uncached from here */
        crc = 0xFFFFFFFFu;
        for (off = 0u; off < h.length && !why; off += n) {
            uint32_t k = h.length - off; if (k > 65536u) k = 65536u;
            if (f_read(&f, (void *)UNCACHED(FW_STAGE + off), k, &n) != FR_OK || n != k) why = "payload not read";
            else crc = crc32_run(crc, (const uint8_t *)UNCACHED(FW_STAGE + off), n);
            g2_wdt_kick();
        }
        if (!why && (crc ^ 0xFFFFFFFFu) != h.crc32) why = "payload CRC mismatch";
    }
    f_close(&f);
    if (why) { say("Loader: \\amiga\\AMIGA.FW "); say(why); say("\n"); return; }
    say("Loader: \\amiga\\AMIGA.FW, "); saydec(h.length); say(" bytes, starting\n"); log_save();
    ethmac_stop();
    (void)usb_ehci_hc("stop", 0, 0u);
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    memcpy((void *)UNCACHED(FW_TRAMP), fw_tramp_s, (size_t)((const char *)fw_tramp_e - (const char *)fw_tramp_s));
    __asm__ volatile ("fence.i" ::: "memory");
    ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))FW_TRAMP)
        (UNCACHED(h.load), UNCACHED(FW_STAGE), (h.length + 3u) / 4u, h.entry, 0x10000000u, 0x12000000u);
}
#endif

static int stick_mount(unsigned int scans, unsigned int skip)
{
    static const char *vol[4] = { "0:", "1:", "2:", "3:" };
    unsigned int scan, jack;
    s_stick_port = -1;
    for (scan = 0u; scan < scans; scan++) {
        if (scan) wait_us(500000u);
        for (jack = 1u; jack <= USB_PORTS; jack++) {
            usb_dev_t d; int r; FRESULT fr;
            if (skip & (1u << jack)) continue;
            g2_wdt_kick();
            diskio_msds[jack - 1u] = 0;
            if (usb_enum_jack(wait_us, T_RESET_US, T_PORT_US, jack, 2u + jack, &d) <= 0) { s_stick_why[jack] = STK_NODEV; continue; }
            r = msd_open(&s_drive[jack - 1u], &d, wait_us);
            if (r != MSD_OK) { s_stick_why[jack] = STK_MSD; s_stick_code[jack] = r; continue; }
            diskio_msds[jack - 1u] = &s_drive[jack - 1u];
            fr = f_mount(&s_fs, vol[jack - 1u], 1);
            if (fr == FR_OK) {
                (void)f_chdrive(vol[jack - 1u]);
                s_stick_port = (int)jack;
                say("Storage: "); say(s_drive[jack - 1u].vendor); say(" "); say(s_drive[jack - 1u].product);
                say(", port "); saydec(jack); say("\n");
                return 0;
            }
            f_mount(0, vol[jack - 1u], 0);
            s_stick_why[jack] = STK_FAT; s_stick_code[jack] = (int)fr;
            diskio_msds[jack - 1u] = 0;
        }
    }
    say("Storage: none found");
    for (jack = 1u; jack <= USB_PORTS; jack++) {
        if (skip & (1u << jack)) continue;
        say(jack == 1u ? " (" : "; "); say("port "); saydec(jack); say(": ");
        if (s_stick_why[jack] == STK_MSD) {
            say(s_stick_code[jack] == MSD_NODEV ? "not mass storage" : "mass storage error -");
            if (s_stick_code[jack] != MSD_NODEV) saydec((uint32_t)-s_stick_code[jack]);
        } else if (s_stick_why[jack] == STK_FAT) say_fr((FRESULT)s_stick_code[jack]);
        else say("no device");
    }
    say(")\n");
    return -1;
}

static unsigned int busy_ports(void);
static int stick_remount(void)
{
    static const char *vol[4] = { "0:", "1:", "2:", "3:" };
    if (s_stick_port > 0) {
        f_mount(0, vol[s_stick_port - 1], 0);
        diskio_msds[s_stick_port - 1] = 0;
        usb_qh_forget(2u + (unsigned int)s_stick_port);
    }
    return stick_mount(2u, busy_ports());
}

/* pads_scan() as fw/snes/snes_main.c, with a boot mouse taken in a third HID slot beside the keyboard, and the DDR
 * shell's per-port backoff (hubwatch) in place of the slot's first rule that a
 * rejected port waits for an unplug: a port whose enumeration or open failed is tried again REJ_RETRY_MS after the
 * failure, so a device that failed once at power-up is not lost */
#define SCAN_MS      2000u
#define ENUM_ROUNDS  3u
#define ENUM_STEP    4u
#define PAD_ERR_MAX  16u
#define REJ_DEV      1u
#define REJ_FULL     2u
#define REJ_RETRY_MS 30000u
static unsigned int s_rej[USB_PORTS + 1u];
static uint32_t     s_rej_ms[USB_PORTS + 1u];       /* when the port was last rejected */
static unsigned int s_pad_err[2];
static uint32_t     s_last_scan;
static uint32_t     s_padw[2];
static unsigned int s_usb_changed;
static int          s_usb_ok;

static unsigned int busy_ports(void)
{
    unsigned int i, m = 0u;
    for (i = 0; i < 2u; i++) if (s_pad[i].armed) m |= 1u << s_pad[i].dev.jack;
    if (s_have_kbd) m |= 1u << s_kbd_h.dev.jack;
    if (s_have_mouse) m |= 1u << s_mouse_h.dev.jack;
    if (s_midi.open) m |= 1u << s_midi.dev.jack;
    if (s_ftdi.open) m |= 1u << s_ftdi.dev.jack;
    return m;
}
static void hex4(unsigned int v)
{
    char b[5]; int i;
    for (i = 3; i >= 0; i--) { b[i] = "0123456789abcdef"[v & 15u]; v >>= 4; }
    b[4] = 0; say(b);
}
/* a pad's state to the core (hid.v command 3): pad 1 drives hid.v joystick 0, the Amiga's joystick port (game port 2,
 * tested on hardware), pad 2 drives joystick 1, the mouse port (game port 1, shared with the mouse) */
static int mcu_byte(unsigned int target, unsigned int start, unsigned int b);
static unsigned int s_fwd;                        /* 1: input goes to the core (running), 0: to the menu */
static void pad_send(unsigned int i, uint32_t w)
{
    (void)mcu_byte(MCU_HID, 1u, 3u); (void)mcu_byte(MCU_HID, 0u, i); (void)mcu_byte(MCU_HID, 0u, w & 0x3Fu);
}
static void pad_detach(unsigned int i, const char *why)
{
    unsigned int addr = s_pad[i].dev.addr, port = s_pad[i].dev.jack, p;
    usb_pad_close(&s_pad[i]);
    usb_qh_forget(addr);
    if (s_fwd && (s_padw[i] & 0x3Fu)) pad_send(i, 0u);   /* a held direction or fire is released in the core */
    s_padw[i] = 0u; s_pad_err[i] = 0u; s_usb_changed = 1u;
    say("Pad "); saydec(i + 1u); say(": port "); saydec(port); say(" "); say(why); say(", released\n");
    for (p = 1u; p <= USB_PORTS; p++) if (s_rej[p] == REJ_FULL) s_rej[p] = 0u;
}
static void hid_detach(usb_hid_t *h, int *have, const char *what, const char *why)
{
    unsigned int addr = h->dev.addr, port = h->dev.jack;
    usb_hid_close(h);
    usb_qh_forget(addr);
    *have = 0; s_usb_changed = 1u;
    say(what); say(": port "); saydec(port); say(" "); say(why); say(", released\n");
}
static void midi_detach(const char *why)
{
    unsigned int addr = s_midi.dev.addr, port = s_midi.dev.jack;
    uint32_t ms;
    usb_irq_line(0);
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    usb_midi_close(&s_midi);
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
    usb_qh_forget(addr);
    s_usb_changed = 1u;
    say("MIDI: port "); saydec(port); say(" "); say(why); say(", released\n");
}
static void ftdi_detach(const char *why)
{
    unsigned int addr = s_ftdi.dev.addr, port = s_ftdi.dev.jack;
    uint32_t ms;
    usb_irq_line(0);
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    usb_ftdi_close(&s_ftdi);
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
    usb_qh_forget(addr);
    s_usb_changed = 1u; s_ftdi_serper = 0xFFFFFFFFu; s_ftdi_mo = s_ftdi_mi = 0xFFFFFFFFu;
    s_frx_n = s_frx_i = 0u;
    AMI_MIDI_ST = 0xEu;                           /* CTS asserted, DSR, CD and RI not: the lines without an adapter */
    say("Serial: port "); saydec(port); say(" "); say(why); say(", released\n");
}
/* MIDI, in the interrupt handler: the device's cable-0 bytes into Paula's receiver, Paula's transmitted bytes to the
 * device (rtl/g2/amiga_serbridge.v).  Runs on the USB interrupt (an IN or OUT transfer completed), the serial
 * bridge's interrupt (a byte from Paula) and the frame interrupt.  The IN slot is held while the bridge's TX FIFO has
 * no room for a whole packet's bytes; the device keeps them.  Without a device Paula's bytes are read and dropped. */
static void midi_pump(void)
{
    unsigned char b[USB_FTDI_IN_MAX];
    unsigned int k, room; uint32_t v, want; int n;
    if (s_ftdi.open) {                            /* the FTDI adapter: the same path, raw bytes (fw/usbftdi.c) */
        usb_ftdi_out_kick(&s_ftdi);
        /* an IN packet (up to 510 bytes at high speed, more than the bridge's TX FIFO) is held here and given to the
         * bridge as it has room; the IN slot is armed again only when the packet is gone, so nothing is dropped */
        if (s_frx_i >= s_frx_n) {
            n = usb_ftdi_poll(&s_ftdi, s_frx, sizeof s_frx, 1);
            s_frx_n = n > 0 ? (unsigned int)n : 0u; s_frx_i = 0u;
        }
        while (s_frx_i < s_frx_n && (AMI_MIDI_TX & MIDI_READY)) AMI_MIDI_TX = s_frx[s_frx_i++];
        if (s_frx_i >= s_frx_n) usb_ftdi_in_resume(&s_ftdi);
        want = ACTRL_SER_IRQ;
        for (;;) {
            if (!usb_ftdi_out_room(&s_ftdi)) { want = 0u; break; }
            if (!((v = AMI_MIDI_RX) & MIDI_VALID)) break;
            usb_ftdi_put(&s_ftdi, v & 0xFFu);
        }
        usb_ftdi_out_kick(&s_ftdi);
    } else if (!s_midi.open) {
        for (k = 0; k < 1024u && ((v = AMI_MIDI_RX) & MIDI_VALID); k++) ;
        want = ACTRL_SER_IRQ;
    } else {
        s_midi.out_max = s_midi_out_max;
        usb_midi_out_kick(&s_midi);
        n = usb_midi_poll(&s_midi, b, sizeof b, 1);
        for (k = 0; n > 0 && k < (unsigned int)n; k++) {
            if (AMI_MIDI_TX & MIDI_READY) AMI_MIDI_TX = b[k];
            else s_midi.drops++;
        }
        if (n > 0) mtrc_rec(3u, b, (unsigned int)n);
        room = MIDI_TX_FIFO - (AMI_MIDI_ST & 0xFFu);
        if (room >= USB_MIDI_IN_MAX) usb_midi_in_resume(&s_midi);
        want = ACTRL_SER_IRQ;
        k = 0u;
        for (;;) {
            if (!usb_midi_out_room(&s_midi)) { want = 0u; break; }
            if (!((v = AMI_MIDI_RX) & MIDI_VALID)) break;
            if (k < sizeof b) b[k++] = (unsigned char)v;
            else { mtrc_rec(2u, b, k); b[0] = (unsigned char)v; k = 1u; }
            usb_midi_put(&s_midi, v & 0xFFu);
        }
        mtrc_rec(2u, b, k);
        usb_midi_out_kick(&s_midi);
    }
    if (want != s_ser_irq) { s_ser_irq = want; AMI_CTRL = (AMI_CTRL & ~ACTRL_SER_IRQ) | want; }
}
/* MIDI, in the main loop: a failed endpoint started again (a control transfer, with interrupts off: the handler
 * shares s_midi), the device released after MIDI_ERR_MAX failures in a row */
static void midi_service(void)
{
    uint32_t ms;
    if (s_ftdi.open && s_ftdi.index && s_ftdi.mpsse != s_ftdi_want_mpsse) {   /* FTDI=: the channel's mode */
        int r;
        __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
        r = usb_ftdi_mpsse(&s_ftdi, s_ftdi_want_mpsse);
        s_frx_n = s_frx_i = 0u;
        __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
        say("FTDI: channel A "); say(r < 0 ? "mode change failed\n" : s_ftdi.mpsse ? "in MPSSE mode\n" : "a serial port\n");
        if (r < 0) s_ftdi.fails++;
    }
    if (s_ftdi.open && s_ftdi.mpsse) {            /* MPSSE: no rate, no RS-232 lines; failures as below */
        if (s_ftdi.fails >= MIDI_ERR_MAX) { ftdi_detach("stopped answering"); return; }
        if (s_ftdi.in_fail || s_ftdi.out_fail) {
            __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
            (void)usb_ftdi_recover(&s_ftdi);
            __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
        }
        return;
    }
    if (s_ftdi.open) {                            /* the adapter's rate follows Paula's SERPER (bit time SERPER + 1 CCKs) */
        /* SERPER, or in FIFO mode (bit 31: the G2 serial FIFO board carries the bytes) the driver's rate itself */
        uint32_t sp = AMI_MIDI_SERPER, st = AMI_MIDI_ST, mo = (st >> 12) & 3u, mi;
        /* the RS-232 control lines both ways: CIA-B's DTR and RTS (0 asserted) to the adapter's pins; its CTS, DSR,
         * CD and RI (modem status, 1 asserted) to CIA-B port A (amiga_serbridge.v ST) */
        if (mo != s_ftdi_mo) {
            __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
            if (usb_ftdi_modem(&s_ftdi, !(mo & 2u), !(mo & 1u)) == 0) s_ftdi_mo = mo;
            __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
        }
        mi = ((s_ftdi.modem & 0x40u) ? 0u : 8u) | ((s_ftdi.modem & 0x80u) ? 0u : 4u) |
             ((s_ftdi.modem & 0x20u) ? 0u : 2u) | ((s_ftdi.modem & 0x10u) ? 0u : 1u);
        if (mi != s_ftdi_mi) { AMI_MIDI_ST = mi; s_ftdi_mi = mi; }
        if (s_ftdi.fails >= MIDI_ERR_MAX) { ftdi_detach("stopped answering"); return; }
        if (s_ftdi.in_fail || s_ftdi.out_fail) {
            __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
            (void)usb_ftdi_recover(&s_ftdi);
            __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
        }
        if (!(sp & 0x80000000u)) sp &= 0x7FFFu;
        if (!s_ftdi_force && sp != s_ftdi_serper && (sp & 0x7FFFFFFFu) != 0u) {
            uint32_t clock = s_pal ? 3546895u : 3579545u;
            uint32_t baud = (sp & 0x80000000u) ? (sp & 0x7FFFFFFFu) : (clock + (sp + 1u) / 2u) / (sp + 1u);
            __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
            (void)usb_ftdi_baud(&s_ftdi, baud);
            __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
            s_ftdi_serper = sp;
            log_put("serial: SERPER "); { char t[40]; snprintf(t, sizeof t, "%lu, %lu bit/s\n", (unsigned long)sp, (unsigned long)baud); log_put(t); }
        }
        return;
    }
    if (!s_midi.open || (!s_midi.in_fail && !s_midi.out_fail)) return;
    if (s_midi.fails >= MIDI_ERR_MAX) { midi_detach("stopped answering"); return; }
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    (void)usb_midi_recover(&s_midi);
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
}
static void port_reject(unsigned int port, const usb_dev_t *d, unsigned int kind, const char *why, int code)
{
    s_rej[port] = kind; s_rej_ms[port] = get_millis();
    say("USB port "); saydec(port); say(": ");
    if (d) { hex4(d->vid); say(":"); hex4(d->pid); say(" class "); saydec(d->cls); say(" "); }
    say(why);
    if (code < 0) { say(" (error -"); saydec((uint32_t)-code); say(")"); }
    say("\n");
}

static void pads_scan(void)
{
    unsigned int port, i;
    s_last_scan = get_millis();
    for (port = 1u; port <= USB_PORTS; port++) {
        usb_dev_t d; int r; unsigned int st, conn, chg, pad_slot = 2u;
        if ((int)port == s_stick_port) continue;
        g2_wdt_kick();
        st = usb_hub_port_status(2u, port);
        if (st == 0xFFFFFFFFu) continue;
        conn = st & 1u; chg = (st >> 16) & 1u;
        for (i = 0; i < 2u; i++) if (s_pad[i].armed && s_pad[i].dev.jack == port) pad_slot = i;
        if (pad_slot < 2u || (s_have_kbd && s_kbd_h.dev.jack == port) || (s_have_mouse && s_mouse_h.dev.jack == port)
            || (s_midi.open && s_midi.dev.jack == port) || (s_ftdi.open && s_ftdi.dev.jack == port)) {
            if (conn && !chg) continue;
            if (pad_slot < 2u) pad_detach(pad_slot, conn ? "re-plugged" : "disconnected");
            else if (s_midi.open && s_midi.dev.jack == port) midi_detach(conn ? "re-plugged" : "disconnected");
            else if (s_ftdi.open && s_ftdi.dev.jack == port) ftdi_detach(conn ? "re-plugged" : "disconnected");
            else if (s_have_kbd && s_kbd_h.dev.jack == port) hid_detach(&s_kbd_h, &s_have_kbd, "Keyboard", conn ? "re-plugged" : "disconnected");
            else hid_detach(&s_mouse_h, &s_have_mouse, "Mouse", conn ? "re-plugged" : "disconnected");
        }
        if (!conn) {
            s_rej[port] = 0u;
            if (chg) usb_hub_clear_port_feature(2u, port, USB_C_PORT_CONNECTION);
            continue;
        }
        if (chg) s_rej[port] = 0u;
        if (s_rej[port] == REJ_DEV && get_millis() - s_rej_ms[port] >= REJ_RETRY_MS) s_rej[port] = 0u;
        if (s_rej[port]) continue;
        for (i = 0; i < 2u; i++) if (!s_pad[i].armed) break;
        if (i == 2u && s_have_kbd && s_have_mouse && (s_midi.open || s_ftdi.open)) continue;
        s_padw[0] = s_padw[1] = 0u;
        r = usb_enum_port_retry(wait_us, T_RESET_US, T_PORT_US, 2u, port, 8u + port, ENUM_STEP, ENUM_ROUNDS, &d);
        if (r <= 0) {
            if (r == 0) continue;
            s_rej[port] = REJ_DEV; s_rej_ms[port] = get_millis();
            say("USB port "); saydec(port); say(": no device descriptor after "); saydec(usb_enum_rounds); say(" rounds\n");
            continue;
        }
        if (d.vid == 0x054Cu && (d.pid == 0x05C4u || d.pid == 0x09CCu || d.pid == 0x0BA0u || d.pid == 0x0268u)) {
            for (i = 0; i < 2u; i++) if (!s_pad[i].armed) break;
            if (i == 2u) { usb_qh_forget(d.addr); port_reject(port, &d, REJ_FULL, "pad, both pad slots in use", 0); continue; }
            r = usb_pad_open(&s_pad[i], &d, PAD_SLOT(i));
            if (r > 0) {
                s_pad_err[i] = 0u; s_usb_changed = 1u;
                say("Pad "); saydec(i + 1u); say(": "); say(usb_pad_name((unsigned int)r)); say(", port "); saydec(port); say("\n");
            } else {
                usb_pad_close(&s_pad[i]);
                usb_qh_forget(d.addr);
                port_reject(port, &d, REJ_DEV, "pad not opened", r);
            }
            continue;
        }
        {   /* a boot keyboard or a boot mouse, each in its own slot */
            usb_hid_t h;
            r = usb_hid_open(&h, &d, !s_have_kbd ? KBD_SLOT : MOUSE_SLOT);
            if (r == USB_HID_KBD && !s_have_kbd) {
                s_kbd_h = h; s_have_kbd = 1; s_usb_changed = 1u; memset(&s_kbd, 0, sizeof s_kbd);
                say("Keyboard: port "); saydec(port); say("\n");
                continue;
            }
            if (r == USB_HID_MOUSE && !s_have_mouse) {
                if (h.slot != MOUSE_SLOT) { usb_hid_close(&h); r = usb_hid_open(&h, &d, MOUSE_SLOT); }
                if (r == USB_HID_MOUSE) {
                    s_mouse_h = h; s_have_mouse = 1; s_usb_changed = 1u;
                    say("Mouse: port "); saydec(port); say("\n");
                    continue;
                }
            }
            if (r > 0) usb_hid_close(&h);
        }
        if (!s_midi.open && !s_ftdi.open && d.vid == 0x0403u) {   /* an FTDI serial adapter (fw/usbftdi.c) */
            r = usb_ftdi_open(&s_ftdi, &d, MIDI_SLOT, MIDI_OUT_SLOT, USB_HID_PAY(MIDI_SLOT));
            if (r > 0) {
                s_usb_changed = 1u; s_ftdi_serper = 0xFFFFFFFFu; s_ftdi_mo = s_ftdi_mi = 0xFFFFFFFFu;
                s_frx_n = s_frx_i = 0u;
                usb_int_irq_enable(1);
                usb_irq_line(1);
                say("Serial: port "); saydec(port); say(", FTDI "); hex4(d.vid); say(":"); hex4(d.pid);
                say(", IN "); saydec(s_ftdi.ep_in); say(" OUT "); saydec(s_ftdi.ep_out);
                say(", packets "); saydec(s_ftdi.mps_in); say(s_ftdi.index ? ", channel A\n" : "\n");
                continue;
            }
            usb_ftdi_close(&s_ftdi);
        }
        if (!s_midi.open && !s_ftdi.open) {   /* a class-compliant USB-MIDI device (fw/usbmidi.c) */
            r = usb_midi_open(&s_midi, &d, MIDI_SLOT, MIDI_OUT_SLOT, USB_HID_PAY(MIDI_SLOT));
            if (r > 0) {
                s_usb_changed = 1u;
                usb_int_irq_enable(1);
                usb_irq_line(1);
                say("MIDI: port "); saydec(port); say(", "); hex4(d.vid); say(":"); hex4(d.pid); say(", interface ");
                saydec(s_midi.iface); say(", IN "); saydec(s_midi.ep_in); say(" OUT "); saydec(s_midi.ep_out); say("\n");
                continue;
            }
            usb_midi_close(&s_midi);
        }
        usb_qh_forget(d.addr);
        port_reject(port, &d, REJ_DEV, "not a supported pad (054c:05c4, 09cc, 0ba0, 0268), boot keyboard, boot mouse, USB-MIDI device or FTDI serial adapter", 0);
    }
}
static int pads_service(void)
{
    unsigned int c;
    if (s_usb_ok && get_millis() - s_last_scan >= SCAN_MS) pads_scan();
    c = s_usb_changed; s_usb_changed = 0u;
    return (int)c;
}

/* ---- the companion stream (amiga_g2.v MCU_TX) ------------------------------------------------------------------ */
/* The companion takes no byte while a sector request of the core waits for its data (the IDE mount read of sector 0
 * among them), so the wait serves that request; with neither moving the core is stopped and the byte is dropped. */
static void sdc_service(void);
static int mcu_byte(unsigned int target, unsigned int start, unsigned int b)
{
    unsigned int t;
    for (t = 0u; AMI_MCU_TX & MCU_BUSY; t++) {
        if (AMI_SDC_REQ & SDC_PENDING) { sdc_service(); t = 0u; continue; }
        if (t > 100000u) return -1;
    }
    AMI_MCU_TX = (start ? MCU_START : 0u) | (target << 8) | (b & 0xFFu);
    return 0;
}
static void sys_set(char id, unsigned int v)
{
    (void)mcu_byte(MCU_SYS, 1u, 4u); (void)mcu_byte(MCU_SYS, 0u, (unsigned int)id); (void)mcu_byte(MCU_SYS, 0u, v);
}
/* sysctrl.v command 10, subcommand 0: minimig.v's MSM6242 registers ($DC0000, read by Kickstart's battclock.resource
 * at boot) set to the local time, the NTP time plus the zone of AMIGA.INI TZ= (AmigaDOS keeps local time): a flags
 * byte (0), then year since 1900, month 0-11, day of the month with the weekday (0 Sunday) in bits 7:5, hour, minute,
 * second, as struct tm holds them.  Nothing is sent before NTP has answered.  0 when every byte was taken. */
static int rtc_send(void)
{
    time_t t; struct tm tm;
    if (!ntptime_known()) return -1;
    t = (time_t)(ntptime_unix() + (uint32_t)(ntptime_tz_min * 60));
    gmtime_r(&t, &tm);
    if (mcu_byte(MCU_SYS, 1u, 10u) || mcu_byte(MCU_SYS, 0u, 0u) || mcu_byte(MCU_SYS, 0u, 0u) ||
        mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_year) || mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_mon) ||
        mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_mday | ((unsigned int)tm.tm_wday << 5)) ||
        mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_hour) || mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_min) ||
        mcu_byte(MCU_SYS, 0u, (unsigned int)tm.tm_sec)) return -1;
    return 0;
}
/* a zone in minutes east of UTC as AMIGA.INI TZ= holds it ("-5", "+5:30") */
static void tz_str(int z, char *b, size_t n)
{
    unsigned int a = (unsigned int)(z < 0 ? -z : z), h = (a / 60u) % 100u, mi = a % 60u;   /* -12 to +14 h: two digits */
    if (mi) snprintf(b, n, "%c%u:%02u", z < 0 ? '-' : '+', h, mi);
    else snprintf(b, n, "%c%u", z < 0 ? '-' : '+', h);
}
/* sysctrl.v's interrupt acknowledged (command 5, bit 0: sys_int; command 6: the cold-boot flag), then the line unmasked
 * once IRQ bit 2 reads clear; with the bit still set after 20 ms the line stays masked and the next call tries again */
static void mcu_ack(void)
{
    uint32_t t0;
    (void)mcu_byte(MCU_SYS, 1u, 5u); (void)mcu_byte(MCU_SYS, 0u, 0x01u);
    (void)mcu_byte(MCU_SYS, 1u, 6u); (void)mcu_byte(MCU_SYS, 0u, 0x00u);
    for (t0 = get_millis(); (AMI_IRQ & 4u) && get_millis() - t0 < 20u; ) { }
    if (AMI_IRQ & 4u) { s_mcu_pend = 1u; return; }
    s_mcu_pend = 0u;
    irq_line(1);
}

/* ---- input ------------------------------------------------------------------------------------------------------ */
#define IN_MENU   0x80000000u                    /* not an Amiga input: F12 or PS */
#define IN_UP     0x08u
#define IN_DOWN   0x04u
#define IN_LEFT   0x02u
#define IN_RIGHT  0x01u
#define IN_FIRE   0x10u
#define IN_FIRE2  0x20u
#define IN_OK     0x40000000u                    /* menu: Enter, Cross, Start */
#define IN_BACK   0x20000000u                    /* menu: Escape, Circle */
#define IN_SAVE   0x10000000u                    /* menu: F10, Triangle (setup.h SU_*: the same bits) */
static uint32_t s_kmenu;                          /* the keyboard's menu bits (arrows, Enter, Escape, F12) */

static uint32_t pad_map(const usb_pad_state_t *st)
{
    uint32_t b = st->buttons, w = 0u;
    if (b & PAD_CROSS) w |= IN_FIRE | IN_OK;
    if (b & PAD_CIRCLE) w |= IN_FIRE2 | IN_BACK;
    if (b & PAD_START) w |= IN_OK;
    if ((b & PAD_UP) || st->ly < 88u) w |= IN_UP;
    if ((b & PAD_DOWN) || st->ly > 168u) w |= IN_DOWN;
    if ((b & PAD_LEFT) || st->lx < 88u) w |= IN_LEFT;
    if ((b & PAD_RIGHT) || st->lx > 168u) w |= IN_RIGHT;
    if (b & PAD_PS) w |= IN_MENU;
    if (b & PAD_TRIANGLE) w |= IN_SAVE;
    return w;
}
static uint32_t kbd_menu_bit(unsigned int u)
{
    switch (u) {
    case 0x52: return IN_UP;   case 0x51: return IN_DOWN; case 0x50: return IN_LEFT; case 0x4F: return IN_RIGHT;
    case 0x28: return IN_OK;   case 0x29: return IN_BACK; case 0x45: return IN_MENU; case 0x43: return IN_SAVE;
    default:   return 0u;
    }
}
static void kbd_send(unsigned int code, unsigned int release)
{
    if (!s_fwd || code >= 0x80u) return;
    (void)mcu_byte(MCU_HID, 1u, 1u);
    (void)mcu_byte(MCU_HID, 0u, (release ? 0x80u : 0u) | code);
}
static void input_poll(void)
{
    unsigned int i;
    for (i = 0; i < 2u; i++) {
        int r;
        if (!s_pad[i].armed) { s_padw[i] = 0u; continue; }
        r = usb_pad_poll(&s_pad[i]);
        if (r > 0) {
            usb_pad_state_t st; uint32_t w;
            usb_pad_decode(&s_pad[i], &st); w = pad_map(&st); s_pad_err[i] = 0u;
            if (s_fwd && (w & 0x3Fu) != (s_padw[i] & 0x3Fu)) {
                pad_send(i, w);
            }
            s_padw[i] = w;
        } else if (r < 0 && ++s_pad_err[i] >= PAD_ERR_MAX) pad_detach(i, "stopped answering");
    }
    if (s_have_kbd) {
        unsigned char rep[8], mk[8], bk[8]; unsigned int pm = s_kbd.mods; int n = usb_hid_poll(&s_kbd_h, rep, sizeof rep);
        if (n > 0) {
            int k, nm = usb_kbd_apply(&s_kbd, rep, n, mk, bk);
            unsigned int ch = pm ^ s_kbd.mods;
            for (k = 0; k < 8; k++) if (ch & (1u << k)) kbd_send(0x68u + (unsigned int)k, !(s_kbd.mods & (1u << k)));
            for (k = 0; k < 6; k++) if (bk[k] && bk[k] != 0x45u) kbd_send(bk[k], 1u);
            for (k = 0; k < nm; k++) if (mk[k] && mk[k] != 0x45u) kbd_send(mk[k], 0u);
            s_kmenu = 0u;
            for (k = 0; k < 6; k++) s_kmenu |= kbd_menu_bit(s_kbd.keys[k]);
            if (!s_fwd) s_kmenu &= ~IN_MENU;
            else s_kmenu &= IN_MENU;                 /* running: only F12 is the firmware's */
        }
    }
    if (s_have_mouse) {
        unsigned char rep[8]; int n = usb_hid_poll(&s_mouse_h, rep, sizeof rep);
        if (n >= 3 && s_fwd) {
            (void)mcu_byte(MCU_HID, 1u, 2u);
            (void)mcu_byte(MCU_HID, 0u, rep[0] & 7u);
            wait_us(2u);                              /* hid.v: the 7 MHz receiver takes x and y as two events */
            (void)mcu_byte(MCU_HID, 0u, rep[1]);
            wait_us(2u);
            (void)mcu_byte(MCU_HID, 0u, rep[2]);
        }
    }
    midi_service();
}
/* the unit's front-panel button (H12, rtl/g2/g2_recovery.v STATUS bit 0, debounced): outside the reset window it only
 * reads, so here it is the menu key */
static uint32_t input_any(void) { return s_padw[0] | s_padw[1] | s_kmenu | ((G2_REC_STATUS & G2_REC_BUTTON) ? IN_MENU : 0u); }

/* ---- settings ---------------------------------------------------------------------------------------------------- */
static char s_kick_name[256];
static char s_df_name[DRIVES][256];
static unsigned int s_df0_key;                    /* AMIGA.INI has a DF0= line (empty: no disk) */
#define HDS 2u
static char s_hd_name[HDS][256];
static unsigned int s_chip = 3u, s_slow = 1u, s_chipset = 2u, s_write = 1u, s_cpu = 0u, s_fast = 0u;

static void ini_value(const char *b, const char *key, char *out, unsigned int n)
{
    const char *p = b; size_t kl = strlen(key);
    while (*p) {
        const char *q = p; unsigned int k = 0u;
        while (*q == ' ' || *q == '\t') q++;
        if (strncmp(q, key, kl) == 0 && q[kl] == '=') {
            for (q += kl + 1u; *q && *q != '\r' && *q != '\n' && k + 1u < n; q++) out[k++] = *q;
            while (k && (out[k - 1u] == ' ' || out[k - 1u] == '\t')) k--;
            out[k] = 0;
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}
static unsigned int ini_num(const char *lb, const char *key, unsigned int dflt, unsigned int max)
{
    char v[16]; v[0] = 0;
    ini_value(lb, key, v, sizeof v);
    if (v[0] >= '0' && v[0] <= '9' && (unsigned int)(v[0] - '0') <= max) return (unsigned int)(v[0] - '0');
    return dflt;
}

/* a time zone "[+|-]h[:mm]" -> minutes east of UTC; 0 on a malformed value (*ok 0) */
static int tz_parse(const char *v, int *ok)
{
    int neg = 0, h = 0, m = 0, dig = 0;
    *ok = 0;
    if (*v == '+' || *v == '-') neg = *v++ == '-';
    while (*v >= '0' && *v <= '9' && dig < 2) { h = h * 10 + (*v++ - '0'); dig++; }
    if (!dig || h > 14) return 0;
    if (*v == ':') {
        v++;
        if (v[0] < '0' || v[0] > '5' || v[1] < '0' || v[1] > '9') return 0;
        m = (v[0] - '0') * 10 + (v[1] - '0'); v += 2;
    }
    if (*v) return 0;
    *ok = 1;
    return neg ? -(h * 60 + m) : h * 60 + m;
}

#ifdef AMIGA_SHARE
static int share_busy(const char *path);
static char s_share_ini[200];                     /* AMIGA.INI SHARE= as read (lower case), shown by setup */
#endif
static void ini_read(void)
{
    static FIL f; UINT n = 0; static char b[1025], lb[1025]; unsigned int i;
    s_kick_name[0] = 0; for (i = 0; i < DRIVES; i++) s_df_name[i][0] = 0;
    for (i = 0; i < HDS; i++) s_hd_name[i][0] = 0;
    if (f_open(&f, "/amiga/AMIGA.INI", FA_READ) != FR_OK) { say("AMIGA.INI: none, defaults\n"); return; }
    (void)f_read(&f, b, 1024, &n); f_close(&f); b[n] = 0;
    for (i = 0; i <= n; i++) lb[i] = (char)lower((unsigned char)b[i]);
    {   /* names keep their case: the key is found in the lower-case copy, the value taken from the original */
        static const char *const keys[] = { "kick", "df0", "df1", "df2", "df3", "hd0", "hd1" };
        char *outs[7]; unsigned int k;
        outs[0] = s_kick_name; for (k = 0; k < DRIVES; k++) outs[k + 1u] = s_df_name[k];
        for (k = 0; k < HDS; k++) outs[k + 5u] = s_hd_name[k];
        for (k = 0; k < 7u; k++) {
            char tmp[256]; tmp[0] = 0;
            ini_value(lb, keys[k], tmp, sizeof tmp);
            if (tmp[0]) {   /* the same span of the original text */
                char *p = strstr(lb, tmp);
                size_t L = strlen(tmp);
                if (p) { memcpy(outs[k], b + (p - lb), L); outs[k][L] = 0; } else strcpy(outs[k], tmp);
            }
        }
    }
    {   const char *p = lb; s_df0_key = 0u;
        while (*p) { while (*p == ' ' || *p == '\t') p++; if (!strncmp(p, "df0=", 4)) { s_df0_key = 1u; break; } while (*p && *p != '\n') p++; if (*p) p++; } }
    {   /* MACHINE= gives the defaults of a profile; each explicit key below overrides its part */
        char v[16]; unsigned int a1200;
        v[0] = 0; ini_value(lb, "machine", v, sizeof v);
        a1200 = !strcmp(v, "a1200");
        s_chip = ini_num(lb, "chip", 3u, 3u);
        s_slow = ini_num(lb, "slow", a1200 ? 0u : 1u, 3u);
        s_chipset = ini_num(lb, "chipset", a1200 ? 3u : 2u, 3u);
        v[0] = 0; ini_value(lb, "cpu", v, sizeof v);
        s_cpu = !strcmp(v, "68020") ? 2u : !strcmp(v, "68010") ? 1u : !strcmp(v, "68000") ? 0u : (a1200 ? 2u : 0u);
        s_fast = ini_num(lb, "fast", a1200 ? 3u : 0u, 3u);
        if (a1200 && !s_kick_name[0]) strcpy(s_kick_name, "KICK_A1200.ROM");
    }
    s_write = ini_num(lb, "write", 1u, 1u);
    s_screen = ini_num(lb, "screen", 2u, 2u);
    {   char v[8]; unsigned int k, n = 0u; v[0] = 0; ini_value(lb, "hofs", v, sizeof v);
        for (k = 0; v[k] >= '0' && v[k] <= '9' && k < 3u; k++) n = n * 10u + (unsigned int)(v[k] - '0');
        s_hofs = (k && !v[k] && n <= 400u) ? n : VID_HOFS; }
    { char v[8]; v[0] = 0; ini_value(lb, "region", v, sizeof v); s_pal = !strncmp(v, "pal", 3); }
    { char v[8]; v[0] = 0; ini_value(lb, "video", v, sizeof v); s_native = !strncmp(v, "native", 6); }
    { char v[8]; v[0] = 0; ini_value(lb, "ftdi", v, sizeof v); s_ftdi_want_mpsse = !strncmp(v, "mpsse", 5); }
    { char v[12]; int ok, z; v[0] = 0; ini_value(lb, "tz", v, sizeof v); z = tz_parse(v, &ok); if (ok) ntptime_tz_min = z; }
#ifdef AMIGA_SHARE
    { char v[200]; v[0] = 0; ini_value(lb, "share", v, sizeof v); share_init(v[0] ? v : 0, share_busy); strcpy(s_share_ini, v); }
#endif
#ifdef AMIGA_SOCK
    sk_init();
#endif
    {   char v[8]; unsigned int k, w = 0u; v[0] = 0; ini_value(lb, "ntpwait", v, sizeof v);
        for (k = 0; v[k] >= '0' && v[k] <= '9' && k < 3u; k++) w = w * 10u + (unsigned int)(v[k] - '0');
        if (k && !v[k] && w <= 120u) s_ntpwait = w; }
    say("AMIGA.INI: read\n");
}

/* AMIGA.INI with the line KEY=value set: the first line whose key (case ignored, leading blanks allowed) is KEY is
 * replaced, every other line kept as it was; with no such line it is appended.  The file is rewritten whole
 * (FA_CREATE_ALWAYS) from its first 1,024 bytes, the part ini_read reads.  A drive change from the menu calls it, so
 * the next boot starts with the same disks. */
static void ini_set(const char *key, const char *value)
{
    static char b[1025], o[1400];
    static FIL f; UINT n = 0, w; size_t kl = strlen(key), on = 0u; int done = 0;
    char *p, *e;
    if (f_open(&f, "/amiga/AMIGA.INI", FA_READ) == FR_OK) { (void)f_read(&f, b, 1024, &n); f_close(&f); }
    b[n] = 0;
    #define OUT(str, len) do { size_t l_ = (len); if (on + l_ < sizeof o) { memcpy(o + on, (str), l_); on += l_; } } while (0)
    for (p = b; *p; p = e) {
        const char *q = p; size_t k;
        for (e = p; *e && *e != '\n'; e++) { }
        if (*e) e++;
        while (*q == ' ' || *q == '\t') q++;
        for (k = 0; k < kl && q[k] && lower((unsigned char)q[k]) == lower((unsigned char)key[k]); k++) { }
        if (!done && k == kl && q[kl] == '=') {
            OUT(key, kl); OUT("=", 1u); OUT(value, strlen(value)); OUT("\r\n", 2u); done = 1;
        } else OUT(p, (size_t)(e - p));
    }
    if (!done) {
        if (on && o[on - 1u] != '\n') OUT("\r\n", 2u);
        OUT(key, kl); OUT("=", 1u); OUT(value, strlen(value)); OUT("\r\n", 2u);
    }
    #undef OUT
    if (f_open(&f, "/amiga/AMIGA.INI", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) { say("AMIGA.INI: not written\n"); return; }
    (void)f_write(&f, o, (UINT)on, &w); (void)f_sync(&f); f_close(&f);
    log_put("AMIGA.INI: "); log_put(key); log_put("="); log_put(value); log_put("\n");
}

/* ---- files in \amiga ------------------------------------------------------------------------------------------- */
static char *s_files[MAX_FILES];
static uint32_t s_fsize[MAX_FILES];
static unsigned int s_nfiles;
static void list_free(void) { unsigned int i; for (i = 0; i < s_nfiles; i++) free(s_files[i]); s_nfiles = 0u; }
/* every file in \amiga with extension ext, sorted without regard to case */
static FRESULT list_ext(const char *ext)
{
    DIR d; FILINFO fi; FRESULT fr; unsigned int i, j;
    list_free();
    if (s_stick_port < 0) return FR_NOT_READY;
    fr = f_opendir(&d, "/amiga");
    if (fr != FR_OK) return fr;
    while (s_nfiles < MAX_FILES) {
        fr = f_readdir(&d, &fi);
        if (fr != FR_OK || !fi.fname[0]) break;
        if (fi.fattrib & AM_DIR) continue;
        if (!ends_with(fi.fname, ext)) continue;
        s_files[s_nfiles] = strdup(fi.fname);
        if (!s_files[s_nfiles]) break;
        s_fsize[s_nfiles++] = (uint32_t)fi.fsize;
        g2_wdt_kick();
    }
    f_closedir(&d);
    for (i = 1; i < s_nfiles; i++) {
        char *t = s_files[i]; uint32_t sz = s_fsize[i];
        for (j = i; j > 0 && ci_cmp(s_files[j - 1], t) > 0; j--) { s_files[j] = s_files[j - 1]; s_fsize[j] = s_fsize[j - 1]; }
        s_files[j] = t; s_fsize[j] = sz;
    }
    return fr == FR_OK ? FR_OK : fr;
}

/* ---- Kickstart -------------------------------------------------------------------------------------------------
 * The image goes to chip memory 0x78_0000 with the two bytes of every 16-bit word exchanged (amiga_cache.v WORD
 * LAYOUT: the 68000's high byte at the odd byte address); a 256 KB image is stored twice (0x78_0000, 0x7C_0000), so
 * the 512 KB window holds it at both halves.  The same bytes go to fast-RAM byte 0x78_0000 (AMI_FKICK_UC): with turbo
 * kick (the G2_AMIGA_AGA fabric, amiga_g2.v) the CPU fetches Kickstart instructions there through its cache.  The core
 * is stopped and in reset. */
static int kick_load(void)
{
    static FIL f; UINT n; uint32_t sz, off, i;
    char path[300];
    volatile uint8_t *dst = (volatile uint8_t *)AMI_KICK_UC, *fdst = (volatile uint8_t *)AMI_FKICK_UC;
    static uint8_t buf[4096];
    if (!s_kick_name[0]) {
        if (f_stat("/amiga/KICK.ROM", 0) == FR_OK) strcpy(s_kick_name, "KICK.ROM");
        else if (list_ext(".rom") == FR_OK && s_nfiles) { strncpy(s_kick_name, s_files[0], sizeof s_kick_name - 1u); }
    }
    if (!s_kick_name[0]) { say("Kickstart: no *.rom in \\amiga\n"); return -1; }
    strcpy(path, "/amiga/"); strncat(path, s_kick_name, sizeof path - 8u);
    if (f_open(&f, path, FA_READ) != FR_OK) { say("Kickstart: cannot open "); say(s_kick_name); say("\n"); return -1; }
    sz = (uint32_t)f_size(&f);
    if (sz != 262144u && sz != 524288u) { f_close(&f); say("Kickstart: "); say(s_kick_name); say(" is not 256 KB or 512 KB\n"); return -1; }
    say("Kickstart: "); say(s_kick_name); say(", "); saydec(sz / 1024u); say(" KB\n");
    for (off = 0; off < sz; off += n) {
        if (f_read(&f, buf, sizeof buf, &n) != FR_OK || n == 0u) break;
        for (i = 0; i + 1u < n; i += 2u) {
            dst[off + i] = buf[i + 1u]; dst[off + i + 1u] = buf[i];
            fdst[off + i] = buf[i + 1u]; fdst[off + i + 1u] = buf[i];
        }
        if (sz == 262144u) for (i = 0; i + 1u < n; i += 2u) {
            dst[0x40000u + off + i] = buf[i + 1u]; dst[0x40000u + off + i + 1u] = buf[i];
            fdst[0x40000u + off + i] = buf[i + 1u]; fdst[0x40000u + off + i + 1u] = buf[i];
        }
        g2_wdt_kick();
    }
    f_close(&f);
    if (off != sz) { say("Kickstart: read error\n"); return -1; }
    return 0;
}

/* ---- floppy drives ---------------------------------------------------------------------------------------------- */
static FIL      s_df_f[DRIVES];
static uint32_t s_df_size[DRIVES];
static unsigned int s_df_open[DRIVES], s_df_dirty[DRIVES];
static uint32_t s_df_wr_ms[DRIVES];
static uint32_t s_reads, s_writes, s_hd_reads, s_hd_writes;
/* the core's write requests: SDC_GO to the end of the transfer in CPU cycles (the longest, the sum, the count), the
 * transfers seen ended by the next request instead of DONE, and those whose end did not come within the wait (the
 * sector is not taken) */
static uint32_t s_wdone_max, s_wdone_n, s_wdone_to, s_wdone_next;
static uint64_t s_wdone_sum;
static uint32_t cyc_now(void) { uint32_t c; __asm__ volatile ("rdcycle %0" : "=r"(c)); return c; }

/* MOUNTS.  amiga_g2.v's SDC_MOUNT is one toggle with the last mask and size written: two writes before the core takes
 * the first cancel, and a mount is taken only from core edge 100 after reset on.  So a mount is queued here and
 * written only while the core runs, one drive at a time with 200 us between (several thousand core edges); while the
 * core is stopped or in reset the queue waits for cold_start or run_resume. */
static uint32_t     s_mnt_size[8];
static unsigned int s_mnt_pend;                  /* bit n: drive n (0-3 floppies, 4-5 IDE) has a mount queued */
static void mount_flush(void)
{
    unsigned int d;
    if (!(AMI_CTRL & ACTRL_RUN) || (AMI_CTRL & ACTRL_RESET)) return;
    for (d = 0; d < 8u; d++) if (s_mnt_pend & (1u << d)) {
        AMI_SDC_SIZE = s_mnt_size[d]; AMI_SDC_MOUNT = 1u << d;
        wait_us(200u);
        s_mnt_pend &= ~(1u << d);
    }
}
static void mount_req(unsigned int d, uint32_t size) { s_mnt_size[d] = size; s_mnt_pend |= 1u << d; mount_flush(); }

static void df_eject(unsigned int d)
{
    if (s_df_open[d]) { (void)f_sync(&s_df_f[d]); f_close(&s_df_f[d]); }
    s_df_open[d] = 0u; s_df_dirty[d] = 0u; s_df_size[d] = 0u; s_df_name[d][0] = 0;
    mount_req(d, 0u);
}
/* the image read whole into chip A and kept open for writes; the core sees the mount */
static int df_insert(unsigned int d, const char *name)
{
    char path[300]; UINT n; uint32_t off, sz;
    uint8_t *dst = (uint8_t *)(AMI_DISK_UC + d * AMI_DISK_STEP);
    if (s_df_open[d]) df_eject(d);
    strcpy(path, "/amiga/"); strncat(path, name, sizeof path - 8u);
    if (f_open(&s_df_f[d], path, s_write ? (FA_READ | FA_WRITE) : FA_READ) != FR_OK &&
        f_open(&s_df_f[d], path, FA_READ) != FR_OK) { say("DF"); saydec(d); say(": cannot open "); say(name); say("\n"); return -1; }
    sz = (uint32_t)f_size(&s_df_f[d]);
    if (sz == 0u || sz > ADF_MAX) { f_close(&s_df_f[d]); say("DF"); saydec(d); say(": "); say(name); say(" is not a floppy image\n"); return -1; }
    for (off = 0; off < sz; off += n) {
        UINT want = (sz - off) > 65536u ? 65536u : (UINT)(sz - off);
        if (f_read(&s_df_f[d], dst + off, want, &n) != FR_OK || n == 0u) break;
        g2_wdt_kick();
    }
    if (off != sz) { f_close(&s_df_f[d]); say("DF"); saydec(d); say(": read error\n"); return -1; }
    s_df_open[d] = 1u; s_df_size[d] = sz; s_df_dirty[d] = 0u;
    if (name != s_df_name[d]) { strncpy(s_df_name[d], name, sizeof s_df_name[d] - 1u); s_df_name[d][sizeof s_df_name[d] - 1u] = 0; }
    mount_req(d, sz);
    say("DF"); saydec(d); say(": "); say(name); say("\n");
    return 0;
}

/* ---- IDE hard-disk images -------------------------------------------------------------------------------------- */
#define HD_BLK 65536u
static FIL      s_hd_f[HDS];
static uint32_t s_hd_size[HDS];
static unsigned int s_hd_open[HDS], s_hd_dirty[HDS];
static uint32_t s_hd_wr_ms[HDS];
/* the block cache: chip A 0x0280_0000-0x02FF_FFFF (cached alias), between the floppy copies (AMI_DISK_UC, 8 MB) and
 * the fast RAM (amiga_g2.v FAST_BASE 0x0300_0000); a slot's tag is (drive << 16 | block) + 1, 0 when empty */
#define HDC_N   128u
#define HDC_MEM ((uint8_t *)0x12800000u)
static uint32_t s_hdc_tag[HDC_N], s_hdc_use[HDC_N], s_hdc_clock;
static uint8_t  s_hdc_dirty[HDC_N];
/* THE MAILBOX BLOCK: block 2 of a hard-disk image whose RigidDiskBlock uses blocks 0 and 1
 * only (rdb_HighRDSKBlock, long 38, below 2) and whose partitions start after it (rdb_LoCylinder x rdb_CylBlocks,
 * longs 34 and 36, above 2), as hd_create lays it out.  The core's reads of it are answered from s_mbx (the request to
 * PANOFS), its writes are taken into s_mbx_ack (PANOFS's acknowledgement and figures) and not written to the file. */
#define MBX_BLOCK 2u
static uint8_t  s_mbx[HDS][512], s_mbx_ack[HDS][512];
static unsigned int s_mbx_ok[HDS], s_mbx_got[HDS];
static uint32_t s_mbx_seq[HDS], s_mbx_seen[HDS];     /* sequence of the last request; get_millis() of the last read */
/* THE COMMAND BLOCK: block 3 of a hard-disk image whose RigidDiskBlock uses blocks 0 to 2 at most
 * and whose partitions start after it.  A program on the Amiga writes a console command into it and reads the output
 * back; the firmware runs the command from the main loop (cmd_service) with its output captured.  Neither direction
 * reaches the file.
 *   written by the Amiga  0 'PFC1'  4 sequence number  8 the command line, NUL-terminated (at most 503 characters)
 *                         0 'PFP1'  4 sequence number  8 the page of the output to answer with
 *   read by the Amiga     0 'PFR1'  4 sequence number  8 state: 0 none, 1 waiting or running, 2 done
 *                        12 output length (at most CMD_OUT bytes)  16 page  20 that page: 492 bytes of the output */
#define CMD_BLOCK 3u
#define CMD_OUT   16384u
#define CMD_PAGE  492u
static unsigned int s_cmd_ok[HDS], s_cmd_state[HDS], s_cmd_page[HDS], s_cmd_new[HDS];
static uint32_t s_cmd_seq[HDS], s_cmd_len[HDS];
static char     s_cmd_line[HDS][504], s_cmd_out[HDS][CMD_OUT];
#ifdef AMIGA_SHARE
/* THE SHARE BLOCKS: blocks 4 to 143 of HD0 when its RigidDiskBlock uses blocks 0 to 3 at most
 * and its partitions start after block 143, as hd_create lays it out.  amiga/share/share-handler.c, the SHARE:
 * volume's handler, writes a request and reads the response through scsi.device; fw/amiga/share.c answers it.
 * Neither direction reaches the file.
 *   blocks 4-5   written   0 'PSQ1'  4 sequence number  8 request length  12 the request (a314fs, from its action)
 *   blocks 6-7   read      0 'PSR1'  4 sequence number of the last request  8 response length  12 the response
 *   blocks 16-143 written before a WRITE request, read after a READ: share_data (64 KB)
 * The request is answered when block 5 is written; a request with the sequence number of the last is not run again.
 * Blocks 6-7 read 'PSR1' from the start, which tells the handler the server is there before it writes anything. */
#define SHR_REQ    4u
#define SHR_RES    6u
#define SHR_DATA   16u
#define SHR_DATA_N (SHARE_DATA_BYTES / 512u)
static unsigned int s_shr_ok, s_shr_n;
static uint8_t  s_shr_req[1024], s_shr_res[1024];
static uint32_t s_shr_seq;
#endif
#ifdef AMIGA_SOCK
static uint32_t s_skw_sec, s_skw_t, s_skw_pend;   /* the socket-window block being written by the core */
static uint32_t s_net_last, s_net_gap;           /* cycles between two services of the stack, the longest since read */
static uint32_t s_go_max, s_wr_max;              /* the longest waits of sdc_go and sdc_wr_wait, cycles */
static uint32_t s_ns[3], s_nsum[3], s_ssum[5], s_ncall;  /* net_service parts (drives and flash, sk_poll, netsh_poll): longest, sums */
static uint32_t s_seg_t, s_seg[5];               /* the main loop's parts, longest each: sectors, input, network, X-Surf, rest */
#endif
#ifdef AMIGA_PFLASH
/* THE FLASH WINDOW (pflash.h): blocks PFL_REQ, PFL_STAT and PFL_DATA to PFL_DATA + 127 of HD0
 * when its RigidDiskBlock uses blocks below PFL_REQ only and its partitions start after the data window */
static unsigned int s_pfl_ok;
#endif
static uint8_t *hd_block(unsigned int d, uint32_t off, unsigned int wr);
/* a cached block to the file; 0, or -1 on a write failure (the block stays changed) */
static int hdc_write(unsigned int i)
{
    unsigned int d = (s_hdc_tag[i] - 1u) >> 16; uint32_t b = (s_hdc_tag[i] - 1u) & 0xFFFFu, n; UINT w;
    n = s_hd_size[d] - b * HD_BLK; if (n > HD_BLK) n = HD_BLK;
    if (f_lseek(&s_hd_f[d], b * HD_BLK) != FR_OK || f_write(&s_hd_f[d], HDC_MEM + i * HD_BLK, n, &w) != FR_OK || w != n)
        return -1;
    s_hdc_dirty[i] = 0u;
    return 0;
}
/* every changed block of drive d to the file, then the file synchronised */
static void hd_flush(unsigned int d)
{
    unsigned int i;
    if (!s_hd_open[d]) return;
    for (i = 0; i < HDC_N; i++) if (s_hdc_dirty[i] && (s_hdc_tag[i] - 1u) >> 16 == d) { g2_wdt_kick(); (void)hdc_write(i); }
    (void)f_sync(&s_hd_f[d]); s_hd_dirty[d] = 0u;
}
static void hdc_drop(unsigned int d)
{
    unsigned int i;
    for (i = 0; i < HDC_N; i++) if (s_hdc_tag[i] && (s_hdc_tag[i] - 1u) >> 16 == d) { s_hdc_tag[i] = 0u; s_hdc_dirty[i] = 0u; }
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void be32_put(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void mbx_setup(unsigned int d)
{
    const uint8_t *b = hd_block(d, 0u, 0u);
    s_mbx_ok[d] = b && !memcmp(b, "RDSK", 4) && be32(b + 38 * 4) < MBX_BLOCK && be32(b + 34 * 4) * be32(b + 36 * 4) > MBX_BLOCK;
    memset(s_mbx[d], 0, 512u); memset(s_mbx_ack[d], 0, 512u);
    memcpy(s_mbx[d], "PFS1", 4); be32_put(s_mbx[d] + 4, s_mbx_seq[d]);
    s_mbx_got[d] = 0u; s_mbx_seen[d] = 0u;
    s_cmd_ok[d] = b && !memcmp(b, "RDSK", 4) && be32(b + 38 * 4) < CMD_BLOCK && be32(b + 34 * 4) * be32(b + 36 * 4) > CMD_BLOCK;
    s_cmd_state[d] = 0u; s_cmd_new[d] = 0u; s_cmd_len[d] = 0u; s_cmd_seq[d] = 0u;
#ifdef AMIGA_SHARE
    if (d == 0u) {
        s_shr_ok = b && !memcmp(b, "RDSK", 4) && be32(b + 38 * 4) < SHR_REQ && be32(b + 34 * 4) * be32(b + 36 * 4) >= SHR_DATA + SHR_DATA_N;
        memset(s_shr_res, 0, sizeof s_shr_res); memcpy(s_shr_res, "PSR1", 4); s_shr_seq = 0u;
    }
#endif
#ifdef AMIGA_PFLASH
    if (d == 0u)
        s_pfl_ok = b && !memcmp(b, "RDSK", 4) && be32(b + 38 * 4) < PFL_REQ && be32(b + 34 * 4) * be32(b + 36 * 4) >= PFL_DATA + PFL_DATA_N;
#endif
#ifdef AMIGA_SOCK
    if (d == 0u) sk_setup(b && !memcmp(b, "RDSK", 4) && be32(b + 38 * 4) < SK_REQ && be32(b + 34 * 4) * be32(b + 36 * 4) >= SK_END);
#endif
}

static int hd_insert(unsigned int d, const char *name)
{
    char path[300];
    strcpy(path, "/amiga/"); strncat(path, name, sizeof path - 8u);
    if (f_open(&s_hd_f[d], path, s_write ? (FA_READ | FA_WRITE) : FA_READ) != FR_OK &&
        f_open(&s_hd_f[d], path, FA_READ) != FR_OK) { say("HD"); saydec(d); say(": cannot open "); say(name); say("\n"); return -1; }
    s_hd_size[d] = (uint32_t)f_size(&s_hd_f[d]);
    hdc_drop(d);
    if (s_hd_size[d] < 0x10000u || (s_hd_size[d] & 511u)) { f_close(&s_hd_f[d]); say("HD"); saydec(d); say(": "); say(name); say(" is not a hard-disk image\n"); return -1; }
    s_hd_open[d] = 1u; s_hd_dirty[d] = 0u;
    mbx_setup(d);
    say("HD"); saydec(d); say(": "); say(name); say(", "); saydec(s_hd_size[d] / 1048576u); say(" MB\n");
    return 0;
}
static void hd_close(unsigned int d)
{
    if (s_hd_open[d]) { hd_flush(d); f_close(&s_hd_f[d]); }
    s_hd_open[d] = 0u; s_hd_dirty[d] = 0u; hdc_drop(d);
}
static void hd_eject(unsigned int d)
{
    hd_close(d); s_hd_size[d] = 0u;
}
/* ---- new hard-disk images --------------------------------------------------------------------------------------
 * The RigidDiskBlock and PartitionBlock layouts of the AmigaDOS Rigid Disk Block specification (devices/hardblocks.h):
 * 64 summed longs each, the checksum making their sum zero.  nanomig.v reads the geometry from RDSK longs 16-18. */
#define HDC_HEADS 16u
#define HDC_SECS  63u
#define HDC_LOCYL 2u
static const unsigned int s_hdc_mb[] = { 128u, 256u, 512u, 1024u };
#define NHDC (sizeof s_hdc_mb / sizeof s_hdc_mb[0])
static void be_put(uint8_t *b, unsigned int l, uint32_t v)
{
    b[l * 4u] = (uint8_t)(v >> 24); b[l * 4u + 1u] = (uint8_t)(v >> 16); b[l * 4u + 2u] = (uint8_t)(v >> 8); b[l * 4u + 3u] = (uint8_t)v;
}
static void rdb_sum(uint8_t *b)
{
    uint32_t sum = 0u; unsigned int l;
    be_put(b, 2u, 0u);
    for (l = 0; l < 64u; l++) sum += ((uint32_t)b[l * 4u] << 24) | ((uint32_t)b[l * 4u + 1u] << 16) | ((uint32_t)b[l * 4u + 2u] << 8) | b[l * 4u + 3u];
    be_put(b, 2u, (uint32_t)(0u - sum));
}
/* \amiga\NEW_<mb>M.hdf (or NEW_<mb>M_2.hdf ...), the file extended to the whole size, cylinders 0-1 and the first
 * 64 KB of the partition zeroed, RDSK at block 0 and PART at block 1; the name in out.  0 on success, the file
 * removed on a failure. */
static int hd_create(unsigned int drive, unsigned int mb, char *out, size_t outsz)
{
    static uint8_t z[65536];
    uint8_t *b = z;
    static FIL f; char path[300]; UINT w; unsigned int k, i; uint32_t off;
    uint32_t cyls = (uint32_t)mb * 1048576u / (HDC_HEADS * HDC_SECS * 512u), cylb = HDC_HEADS * HDC_SECS;
    uint32_t size = cyls * cylb * 512u;
    for (k = 1u; k < 100u; k++) {
        if (k == 1u) snprintf(out, outsz, "NEW_%uM.hdf", mb); else snprintf(out, outsz, "NEW_%uM_%u.hdf", mb, k);
        snprintf(path, sizeof path, "/amiga/%s", out);
        if (f_stat(path, 0) == FR_NO_FILE) break;
    }
    if (k == 100u) { say("no free name\n"); return -1; }
    say("Creating "); say(out); say(", "); saydec(mb); say(" MB\n");
    if (f_open(&f, path, FA_WRITE | FA_CREATE_NEW) != FR_OK) { say("cannot create it\n"); return -1; }
    for (off = 0u; off < size; ) {               /* the clusters allocated 16 MB at a time, the contents left as found */
        off = size - off > 0x1000000u ? off + 0x1000000u : size;
        if (f_lseek(&f, off) != FR_OK || f_tell(&f) != off) { f_close(&f); (void)f_unlink(path); say("the stick has no room for it\n"); return -1; }
        g2_wdt_kick();
    }
    memset(z, 0, sizeof z);
    if (f_lseek(&f, 0) != FR_OK) { f_close(&f); (void)f_unlink(path); return -1; }
    for (off = 0u; off < HDC_LOCYL * cylb * 512u + sizeof z; off += w) {   /* cylinders 0, 1 and 64 KB of the partition */
        uint32_t n = HDC_LOCYL * cylb * 512u + sizeof z - off;
        if (f_write(&f, z, n > sizeof z ? sizeof z : n, &w) != FR_OK || w == 0u) { f_close(&f); (void)f_unlink(path); say("write error, removed\n"); return -1; }
        g2_wdt_kick();
    }
    /* RDSK, block 0 */
    memset(b, 0, 512u);
    be_put(b, 0u, 0x5244534Bu); be_put(b, 1u, 64u); be_put(b, 3u, 7u); be_put(b, 4u, 512u); be_put(b, 5u, 7u);
    be_put(b, 6u, 0xFFFFFFFFu); be_put(b, 7u, 1u); be_put(b, 8u, 0xFFFFFFFFu); be_put(b, 9u, 0xFFFFFFFFu);
    for (i = 10u; i <= 15u; i++) be_put(b, i, 0xFFFFFFFFu);
    be_put(b, 16u, cyls); be_put(b, 17u, HDC_SECS); be_put(b, 18u, HDC_HEADS); be_put(b, 19u, 1u); be_put(b, 20u, cyls);
    be_put(b, 24u, cyls); be_put(b, 25u, cyls); be_put(b, 26u, 3u);
    be_put(b, 32u, 0u); be_put(b, 33u, HDC_LOCYL * cylb - 1u); be_put(b, 34u, HDC_LOCYL); be_put(b, 35u, cyls - 1u);
    be_put(b, 36u, cylb); be_put(b, 38u, 1u);
    memcpy(b + 40u * 4u, "NANOMIG ", 8u); memcpy(b + 42u * 4u, "HARDFILE        ", 16u); memcpy(b + 46u * 4u, "1.0 ", 4u);
    rdb_sum(b);
    /* PART, block 1 */
    memset(b + 512u, 0, 512u); b += 512u;
    be_put(b, 0u, 0x50415254u); be_put(b, 1u, 64u); be_put(b, 3u, 7u); be_put(b, 4u, 0xFFFFFFFFu); be_put(b, 5u, 1u);
    be_put(b, 6u, 0xFFFFFFFFu); be_put(b, 7u, 0xFFFFFFFFu); be_put(b, 8u, 0u);
    b[9u * 4u] = 3u; b[9u * 4u + 1u] = 'D'; b[9u * 4u + 2u] = 'H'; b[9u * 4u + 3u] = (uint8_t)('0' + drive);
    for (i = 17u; i <= 31u; i++) be_put(b, i, 0xFFFFFFFFu);
    be_put(b, 32u, 16u); be_put(b, 33u, 128u); be_put(b, 34u, 0u); be_put(b, 35u, HDC_HEADS); be_put(b, 36u, 1u);
    be_put(b, 37u, HDC_SECS); be_put(b, 38u, 2u); be_put(b, 39u, 0u); be_put(b, 40u, 0u);
    be_put(b, 41u, HDC_LOCYL); be_put(b, 42u, cyls - 1u); be_put(b, 43u, 30u); be_put(b, 44u, 0u);
    be_put(b, 45u, 0x0001FE00u); be_put(b, 46u, 0x7FFFFFFEu); be_put(b, 47u, 0u); be_put(b, 48u, 0x444F5303u);
    rdb_sum(b);
    if (f_lseek(&f, 0) != FR_OK || f_write(&f, z, 1024u, &w) != FR_OK || w != 1024u || f_close(&f) != FR_OK) {
        (void)f_unlink(path); say("write error, removed\n"); return -1; }
    say("Created: "); saydec(cyls); say(" cylinders, "); saydec(size / 1048576u); say(" MB; format DH"); saydec(drive);
    say(" from Workbench\n");
    return 0;
}
/* byte offset off of drive d in the block cache, the block marked changed when wr; 0 on a read failure, or when the
 * slot to replace cannot be written back */
static uint8_t *hd_block(unsigned int d, uint32_t off, unsigned int wr)
{
    uint32_t tag = ((d << 16) | (off / HD_BLK)) + 1u; unsigned int i, v = 0; UINT n;
    for (i = 0; i < HDC_N; i++) if (s_hdc_tag[i] == tag) break;
    if (i == HDC_N) {
        for (i = 0; i < HDC_N; i++) {
            if (!s_hdc_tag[i]) { v = i; break; }
            if (s_hdc_use[i] < s_hdc_use[v]) v = i;
        }
        i = v;
        if (s_hdc_dirty[i] && hdc_write(i) != 0) return 0;
        s_hdc_tag[i] = 0u;
        if (f_lseek(&s_hd_f[d], (off / HD_BLK) * HD_BLK) != FR_OK || f_read(&s_hd_f[d], HDC_MEM + i * HD_BLK, HD_BLK, &n) != FR_OK)
            return 0;
        if (n < HD_BLK) memset(HDC_MEM + i * HD_BLK + n, 0, HD_BLK - n);
        s_hdc_tag[i] = tag;
    }
    s_hdc_use[i] = ++s_hdc_clock;
    if (wr) { s_hdc_dirty[i] = 1u; s_hd_dirty[d] = 1u; s_hd_wr_ms[d] = get_millis(); }
    return HDC_MEM + i * HD_BLK + (off % HD_BLK);
}

/* SDC_GO, then wait until the request no longer reads pending.  A fabric whose SDC_REQ reports pending until the core
 * domain takes the GO toggle (amiga_g2.v before the pending bit was masked by the toggle) otherwise shows the served
 * request again to a poll right after the write, and a second SDC_GO releases the next request before its buffer is
 * filled.  With the masked pending bit the loop ends at once. */
/* THE END OF A WRITE TRANSFER.  SDC_DONE (q_done) is a level only until the core's next request: Q_IDLE clears it as
 * the request is taken, and the IDE drive issues the next sector of a multi-sector write as soon as it sees the done
 * pulse.  A poll that comes late (an interrupt between two reads) then finds DONE clear and the next request pending.
 * A pending request can only follow the end of the transfer, and the sector buffer keeps the written bytes until the
 * next SDC_GO, so DONE or PENDING both mean the 512 bytes are there to read.  0 ended, 1 by the next request, -1 none
 * within the wait. */
static int sdc_wr_wait(void)
{
    uint32_t t, r;
#ifdef AMIGA_SOCK
    uint32_t c0 = cyc_now();
#endif
    for (t = 0u; t <= 2000000u; t++) {
        r = AMI_SDC_REQ;
#ifdef AMIGA_SOCK
        if (r & (SDC_DONE | SDC_PENDING) && cyc_now() - c0 > s_wr_max) s_wr_max = cyc_now() - c0;
#endif
        if (r & SDC_DONE) return 0;
        if (r & SDC_PENDING) return 1;
    }
    return -1;
}
static void sdc_go(void)
{
    uint32_t t;
#ifdef AMIGA_SOCK
    uint32_t c0 = cyc_now();
#endif
    AMI_SDC_GO = 0u;
    for (t = 0u; (AMI_SDC_REQ & SDC_PENDING) && t < 2000000u; t++) { }
#ifdef AMIGA_SOCK
    if (cyc_now() - c0 > s_go_max) s_go_max = cyc_now() - c0;
#endif
}

#ifdef AMIGA_SHARE
/* a block of the share area (THE SHARE BLOCKS) */
static void share_block(uint32_t r, uint32_t sec)
{
    uint8_t *p = sec < SHR_RES ? s_shr_req + (sec - SHR_REQ) * 512u : sec < SHR_DATA ? s_shr_res + (sec - SHR_RES) * 512u
                                                                                    : share_data + (sec - SHR_DATA) * 512u;
    uint32_t i;
    if (!(r & SDC_WRITE)) {
        for (i = 0; i < 512u; i++) AMI_SDC_DATA = p[i];
        sdc_go();
        s_reads++; s_hd_reads++;
        return;
    }
    sdc_go();
    if (sdc_wr_wait() < 0) { s_wdone_to++; return; }
    for (i = 0; i < 512u; i++) p[i] = (uint8_t)AMI_SDC_DATA;
    s_writes++; s_hd_writes++;
    if (sec == SHR_REQ + 1u && !memcmp(s_shr_req, "PSQ1", 4) && be32(s_shr_req + 4) != s_shr_seq) {
        uint32_t len = be32(s_shr_req + 8), n;
        if (len > sizeof s_shr_req - 12u) len = sizeof s_shr_req - 12u;
        memset(s_shr_res + 12, 0, sizeof s_shr_res - 12u);
        n = share_request(s_shr_req + 12, len, s_shr_res + 12);
        s_shr_seq = be32(s_shr_req + 4);
        memcpy(s_shr_res, "PSR1", 4); be32_put(s_shr_res + 4, s_shr_seq); be32_put(s_shr_res + 8, n);
        s_shr_n++;
    }
}
#endif

#ifdef AMIGA_PFLASH
/* a block of the flash window (THE FLASH WINDOW) */
static void pfl_block(uint32_t r, uint32_t sec)
{
    uint8_t st[512];
    uint32_t i;
    if (!(r & SDC_WRITE)) {
        const uint8_t *p = sec == PFL_STAT ? st : sec >= PFL_DATA ? pfl_data + (sec - PFL_DATA) * 512u : 0;
        if (sec == PFL_STAT) pfl_status(st);
        for (i = 0; i < 512u; i++) AMI_SDC_DATA = p ? p[i] : 0u;
        sdc_go();
        s_reads++; s_hd_reads++;
        return;
    }
    sdc_go();
    if (sdc_wr_wait() < 0) { s_wdone_to++; return; }
    if (sec >= PFL_DATA) { uint8_t *p = pfl_data + (sec - PFL_DATA) * 512u; for (i = 0; i < 512u; i++) p[i] = (uint8_t)AMI_SDC_DATA; }
    else { for (i = 0; i < 512u; i++) st[i] = (uint8_t)AMI_SDC_DATA; if (sec == PFL_REQ) pfl_request(st); }
    s_writes++; s_hd_writes++;
}
#endif

#ifdef AMIGA_SOCK
/* a block of the socket window (fw/amiga/sock.h) */
static void sk_blk(uint32_t r, uint32_t sec)
{
    static uint8_t b[512];
    uint32_t i;
    if (!(r & SDC_WRITE)) {
        sk_read(sec, b);
        for (i = 0; i < 512u; i++) AMI_SDC_DATA = b[i];
        sdc_go();
        s_reads++; s_hd_reads++;
        return;
    }
    sdc_go();                                    /* the data is taken by sdc_service once the core has written it */
    s_skw_sec = sec; s_skw_t = cyc_now(); s_skw_pend = 1u;
}
/* a written block of the window, once the core has it: 1 while the write is still under way */
static int sk_blk_done(void)
{
    static uint8_t b[512];
    uint32_t r = AMI_SDC_REQ, i;
    if (!(r & (SDC_DONE | SDC_PENDING))) {
        if (cyc_now() - s_skw_t < 3u * PANO_CPU_HZ) return 1;
        s_wdone_to++; s_skw_pend = 0u;
        return 0;
    }
    for (i = 0; i < 512u; i++) b[i] = (uint8_t)AMI_SDC_DATA;
    s_skw_pend = 0u;
    sk_write(s_skw_sec, b);
    s_writes++; s_hd_writes++;
    return 0;
}
#endif

/* one request of the core's sector port: drive mask bits 3:0 the floppies (served from their copies in chip A, writes
 * to the copy and the file), bits 5:4 the IDE drives (served from the file) */
static void sdc_service(void)
{
    uint32_t r, sec, off, i;
    unsigned int d, mask;
#ifdef AMIGA_SOCK
    if (s_skw_pend && sk_blk_done()) return;     /* a socket-window write: the main loop runs while the core writes */
#endif
    r = AMI_SDC_REQ;
    if (!(r & SDC_PENDING)) {
        for (d = 0; d < DRIVES; d++)
            if (s_df_dirty[d] && get_millis() - s_df_wr_ms[d] > 1000u) { (void)f_sync(&s_df_f[d]); s_df_dirty[d] = 0u; }
        for (d = 0; d < HDS; d++)
            if (s_hd_dirty[d] && get_millis() - s_hd_wr_ms[d] > 1000u) hd_flush(d);
        return;
    }
    sec = AMI_SDC_SECTOR; mask = r & 0xFFu;
    for (d = 0; d < 8u && !(mask & (1u << d)); d++) { }
    off = sec * 512u;
    if (d == 4u || d == 5u) {
        unsigned int h = d - 4u;
        int ok = s_hd_open[h] && off + 512u <= s_hd_size[h];
        int mbx = ok && s_mbx_ok[h] && sec == MBX_BLOCK, cmd = ok && s_cmd_ok[h] && sec == CMD_BLOCK;
#ifdef AMIGA_SHARE
        if (ok && h == 0u && s_shr_ok && ((sec >= SHR_REQ && sec < SHR_RES + 2u) || (sec >= SHR_DATA && sec < SHR_DATA + SHR_DATA_N))) {
            share_block(r, sec);
            return;
        }
#endif
#ifdef AMIGA_SOCK
        if (ok && h == 0u && sk_block(sec)) {
            sk_blk(r, sec);
            return;
        }
#endif
#ifdef AMIGA_PFLASH
        if (ok && h == 0u && s_pfl_ok && (sec == PFL_REQ || sec == PFL_STAT || (sec >= PFL_DATA && sec < PFL_DATA + PFL_DATA_N))) {
            pfl_block(r, sec);
            return;
        }
#endif
        if (!(r & SDC_WRITE) && cmd) {           /* the command block: the state and a page of the output */
            uint8_t rb[20]; uint32_t base = s_cmd_page[h] * CMD_PAGE;
            memcpy(rb, "PFR1", 4); be32_put(rb + 4, s_cmd_seq[h]); be32_put(rb + 8, s_cmd_state[h]);
            be32_put(rb + 12, s_cmd_len[h]); be32_put(rb + 16, s_cmd_page[h]);
            for (i = 0; i < 20u; i++) AMI_SDC_DATA = rb[i];
            for (i = 0; i < CMD_PAGE; i++) AMI_SDC_DATA = base + i < s_cmd_len[h] ? (uint8_t)s_cmd_out[h][base + i] : 0u;
            sdc_go();
            s_reads++; s_hd_reads++;
            return;
        }
        if (!(r & SDC_WRITE)) {
            const uint8_t *src = !ok ? 0 : mbx ? s_mbx[h] : hd_block(h, off, 0u);
            if (mbx) s_mbx_seen[h] = get_millis() | 1u;
            for (i = 0; i < 512u; i++) AMI_SDC_DATA = src ? src[i] : 0u;
            sdc_go();
            s_reads++; s_hd_reads++;
            return;
        }
        {
            uint32_t c0 = cyc_now(), dc; int e;
            sdc_go();
            if ((e = sdc_wr_wait()) < 0) { s_wdone_to++; return; }
            if (e) s_wdone_next++;
            dc = cyc_now() - c0; s_wdone_n++; s_wdone_sum += dc; if (dc > s_wdone_max) s_wdone_max = dc;
        }
        if (cmd) {                               /* a command or a page request: kept, not written to the file */
            uint8_t wb[512];
            for (i = 0; i < 512u; i++) wb[i] = (uint8_t)AMI_SDC_DATA;
            if (!memcmp(wb, "PFC1", 4) && (be32(wb + 4) != s_cmd_seq[h] || s_cmd_state[h] == 0u)) {
                memcpy(s_cmd_line[h], wb + 8, sizeof s_cmd_line[h] - 1u); s_cmd_line[h][sizeof s_cmd_line[h] - 1u] = 0;
                s_cmd_seq[h] = be32(wb + 4); s_cmd_state[h] = 1u; s_cmd_len[h] = 0u; s_cmd_page[h] = 0u; s_cmd_new[h] = 1u;
            } else if (!memcmp(wb, "PFP1", 4) && be32(wb + 4) == s_cmd_seq[h]) s_cmd_page[h] = be32(wb + 8);
            return;
        }
        if (mbx) {                               /* PANOFS's acknowledgement: kept, not written to the file */
            for (i = 0; i < 512u; i++) s_mbx_ack[h][i] = (uint8_t)AMI_SDC_DATA;
            s_mbx_got[h] = 1u;
            return;
        }
        if (ok && s_write) {
            uint8_t *p = hd_block(h, off, 1u);
            if (p) for (i = 0; i < 512u; i++) p[i] = (uint8_t)AMI_SDC_DATA;
        }
        s_writes++; s_hd_writes++;
        return;
    }
    if (!(r & SDC_WRITE)) {
        const volatile uint8_t *src = (const volatile uint8_t *)(AMI_DISK_UC + d * AMI_DISK_STEP + off);
        int ok = d < DRIVES && s_df_open[d] && off + 512u <= s_df_size[d];
        for (i = 0; i < 512u; i++) AMI_SDC_DATA = ok ? src[i] : 0u;
        sdc_go();
        s_reads++;
        return;
    }
    sdc_go();
    if (sdc_wr_wait() < 0) { s_wdone_to++; return; }
    if (d < DRIVES && s_df_open[d] && off + 512u <= s_df_size[d]) {
        volatile uint8_t *dst = (volatile uint8_t *)(AMI_DISK_UC + d * AMI_DISK_STEP + off);
        for (i = 0; i < 512u; i++) dst[i] = (uint8_t)AMI_SDC_DATA;
        if (s_write) {
            UINT w;
            if (f_lseek(&s_df_f[d], off) == FR_OK) (void)f_write(&s_df_f[d], (const void *)dst, 512u, &w);
            s_df_dirty[d] = 1u; s_df_wr_ms[d] = get_millis();
        }
    }
    s_writes++;
}

/* ---- running ------------------------------------------------------------------------------------------------------ */
static void ami_video_setup(void)
{
    uint32_t fb = AMI_FB_OFF;
    s_heads = s_screen == 0u ? 2u : s_screen == 1u ? 1u : 3u;
    if (s_heads == 2u) fb += AMI_HEAD2_DELTA;
    AMI_FB_BASE = fb;
    AMI_HEAD_SEL = (s_heads & 2u) ? 1u : 0u;
    AMI_VID = (s_hofs << 16) | VID_W; AMI_VID_FAST = VID_HOFS_FAST;
#ifndef AMIGA_SIM                                /* simulation: no encoders; the buffers start as the model's memory */
    fb_black();
    (void)ch7301_init(s_native ? 4u : 0u);
    (void)ch7301_write(0x75u, 0x23u, 0x04u);
    (void)ch7301_write(0x76u, 0x23u, 0x04u);
#endif
    ami_heads_on(0u);
}
/* CTRL of a running machine.  No sdc_hold: the gate gives the core every edge of its clock (amiga_g2.v NCO: PAL
 * 28.375160 MHz, NTSC 28.636364 MHz), so the frame rate, the CIA timers and Paula's serial and audio periods are those
 * of the original machine; a sector the core waits for arrives through the drive's own busy handshake.  With sdc_hold a
 * wait longer than OWE_MAX edges (amiga_g2.v) drops edges and the machine runs slow. */
static uint32_t run_ctrl(void) { return ACTRL_FRM_IRQ | (s_heads == 3u ? ACTRL_MIRROR : 0u); }
/* the machine from reset: the cache invalidated, the core released, the settings sent to sysctrl.v (it holds the
 * machine in reset until "R" 0) */
/* COLD START.  A machine that ran before leaves three things behind that a reset of the core does not clear:
 *   1 a sector request of the slot's queue (amiga_g2.v qst/q_pend are not reset by core_reset); with sdc_hold the gate
 *     gives the core no edges while it waits, so the core could not leave reset and the settings below never arrived
 *     (sysctrl.v then releases the machine after 3 s with its own defaults);
 *   2 chip and slow RAM: Kickstart finds the old ExecBase and runs its reset vectors (ColdCapture, KickTags,
 *     resident modules) -- a warm reset into what the program installed, not a cold one;
 *   3 the cache's lines of that memory.
 * So: the core held in reset with its clock running and no sector hold, every waiting request answered; the core
 * stopped; chip RAM (0x000000-0x1FFFFF of the core's RAM space) and slow RAM (0x600000-0x77FFFF) cleared in DDR2,
 * Kickstart (0x780000-0x7FFFFF) kept (minimig_sram_bridge.v); the cache invalidated; then the release. */
static void cold_drain(void)
{
    uint32_t t0 = get_millis(), served = 0u;
    ctrl_set(ACTRL_RESET | ACTRL_RUN);              /* clock running, core in reset, no sector hold */
    while (get_millis() - t0 < 50u) {
        if (AMI_SDC_REQ & SDC_PENDING) {
            unsigned int i;
            if (!(AMI_SDC_REQ & SDC_WRITE)) for (i = 0; i < 512u; i++) AMI_SDC_DATA = 0u;
            sdc_go(); served++;
            t0 = get_millis();                       /* 50 ms with nothing pending after the last answer */
        }
        g2_wdt_kick();
    }
    ctrl_set(ACTRL_RESET);                          /* stopped */
    if (served) { say("cold start: "); saydec(served); say(" waiting sector request(s) answered\n"); }
}
#ifndef AMIGA_SIM
static void cold_clear(void)
{
    volatile uint32_t *p;
    uint32_t i;
    p = (volatile uint32_t *)AMI_CHIP_UC;                         for (i = 0; i < 0x200000u / 4u; i++) { p[i] = 0u; if (!(i & 0xFFFFu)) g2_wdt_kick(); }
    p = (volatile uint32_t *)(AMI_CHIP_UC + 0x600000u);           for (i = 0; i < 0x180000u / 4u; i++) { p[i] = 0u; if (!(i & 0xFFFFu)) g2_wdt_kick(); }
}
#endif
static void cold_start(void)
{
    unsigned int d;
    irq_line(0);
    cold_drain();
#ifndef AMIGA_SIM                                    /* simulation: the memory model starts cleared */
    cold_clear();
#endif
    ctrl_set(ACTRL_RESET);
    AMI_CACHE = ACACHE_INV;
    s_mnt_pend = 0u;                             /* the core restarts with no drive mounted: every open drive again */
    for (d = 0; d < DRIVES; d++) if (s_df_open[d]) { s_mnt_size[d] = s_df_size[d]; s_mnt_pend |= 1u << d; }
    for (d = 0; d < HDS; d++) if (s_hd_open[d]) { s_mnt_size[4u + d] = s_hd_size[d]; s_mnt_pend |= 1u << (4u + d); }
    AMI_NCO_INC = s_pal ? AMI_NCO_PAL : AMI_NCO_NTSC; AMI_AUD_INC = s_pal ? AMI_AUD_PAL : AMI_AUD_NTSC;
    ctrl_set(run_ctrl() | ACTRL_RUN);
    wait_us(1000u);                              /* past the core's power-on reset (core edge 16) and edge 100 */
    sys_set('T', s_cpu); sys_set('V', s_pal ? 0u : 1u); sys_set('C', s_chipset); sys_set('Y', s_chip); sys_set('X', s_slow);
    sys_set('D', 3u); sys_set('P', s_write ? 0u : 1u); sys_set('I', (s_hd_open[0] || s_hd_open[1]) ? 1u : 0u);
    sys_set('Q', s_fast);
    (void)rtc_send();                            /* before the reset is released: Kickstart reads the clock at boot */
    (void)xsurf_start();                         /* the X-Surf board's address and mailbox (AGA fabric) */
    sys_set('R', 0u);
    mcu_ack();
    mount_flush();                               /* after the settings: the IDE mount reads sector 0 at once */
    log_put("cold start: IRQ "); { char b[12]; int i = 11; uint32_t v = AMI_IRQ; b[i] = 0; do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i); log_put(&b[i]); }
    log_put(s_mcu_pend ? ", companion interrupt not acknowledged: frame and audio polled\n" : ", companion interrupt acknowledged\n");
    s_fwd = 1u;
    snes_audio_reset();                          /* the ring cleared: snes_audio_start counts its first half as queued */
    snes_audio_start();
}
static void cold_start(void);
/* the machine running again after the menu; a cold start instead when afs wrote a hard disk meanwhile (s_afs_cold) */
static unsigned int s_afs_cold;
static void run_resume(void)
{
    if (s_afs_cold) { s_afs_cold = 0u; log_put("afs: a hard disk was written during the menu: cold start\n"); cold_start(); return; }
    ctrl_set(run_ctrl() | ACTRL_RUN); mount_flush(); s_fwd = 1u; snes_audio_reset(); snes_audio_start();
}
static void run_stop(void)
{
    snes_audio_stop();
    s_fwd = 0u;
    ctrl_clr(ACTRL_RUN | ACTRL_FRM_IRQ);
}

static void net_service(void);
static unsigned int wait_press(void)
{
    uint32_t prev = input_any(), now;
    for (;;) {
        g2_wdt_kick();
        input_poll();
        now = input_any();
        if (now & ~prev) return now & ~prev;
        prev = now;
        if (pads_service()) return 0;
        net_service();
        wait_us(10000u);
    }
}

static void quiesce_for_exit(void);
/* the menu: the core stopped, the console on the heads */
/* the menu's machine profiles: CHIP, SLOW, CHIPSET, CPU and KICK of AMIGA.INI; the A1200 profile needs the
 * G2_AMIGA_AGA fabric (AGA chipset, TG68K 68020) and \amiga\KICK_A1200.ROM */
static const struct { const char *name; unsigned char chip, slow, chipset, cpu, fast; const char *kick, *tag; } s_prof[] = {
    { "A500, OCS, 512K chip + 512K slow", 0u, 1u, 0u, 0u, 0u, "KICK.ROM", "A500 OCS" },
    { "A500+, ECS, 1M chip",              1u, 0u, 2u, 0u, 0u, "KICK.ROM", "A500+ ECS" },
    { "ECS, 2M chip + 512K slow",         3u, 1u, 2u, 0u, 0u, "KICK.ROM", "ECS 2 MB" },
    { "A1200, AGA, 68020, 2M chip + 8M fast", 3u, 0u, 3u, 2u, 3u, "KICK_A1200.ROM", "A1200 AGA" },
};
static const char *const s_cpu_name[] = { "68000", "68010", "68020" };
#define NPROF (sizeof s_prof / sizeof s_prof[0])
static unsigned int prof_now(void)
{
    unsigned int i;
    for (i = 0; i < NPROF; i++)
        if (s_prof[i].chip == s_chip && s_prof[i].slow == s_slow && s_prof[i].chipset == s_chipset && s_prof[i].cpu == s_cpu &&
            s_prof[i].fast == s_fast) return i;
    return NPROF;
}

/* ---- the network console -------------------------------------------------------------------------------------------
 * fw/netsh.c (lwIP, DHCP, TCP 2323, the UDP beacon on 48951) with this command table, in the protocol of
 * a client: a greeting and the prompt "> " on connect, each line echoed, its output, then "> ".  get and put are
 * fw/bootsh.c's f_read / f_write paths: "<who>: <N> bytes follow" or "<who>: ready for <N> bytes", the bytes, and the
 * trailer "<who>: <N> bytes CRC32 0x<crc>, <ms> ms" .  Files are named inside \amiga only.
 * The console is served from the main loop (net_service): a get or put holds the sector server for its duration. */
static char     s_nl[256];
static unsigned s_nl_n, s_net_gen, s_net_up;
static uint32_t stack_used(void);
/* the console output; while a command block's command runs (cmd_service) it is captured instead */
static char    *s_np_cap; static uint32_t s_np_n, s_np_max;
static void np(const char *s)
{
    if (s_np_cap) { while (*s && s_np_n < s_np_max) s_np_cap[s_np_n++] = *s++; return; }
    netsh_puts(s);
}
static void npdec(uint32_t v) { char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i); np(&b[i]); }
static void nphex(uint32_t v) { char b[11]; int i; b[0] = '0'; b[1] = 'x'; for (i = 0; i < 8; i++) b[2 + i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15u]; b[10] = 0; np(b); }
/* THE LAST WORD OF AN UNCLAIMED EXCEPTION, as fw/bootsh.c net_trap (trap.c fatal() calls trap_screen, then halts):
 * mcause, mepc, mtval, sp and ra written to the top 4 KB of chip A (uncached, G2_TRAP_REC) and sent to the session,
 * whose output is pushed for at most 2 s; fatal() then spins without kicking the watchdog, which returns the unit to
 * the golden image.  trap_report prints and clears a record a previous run left when the DDR kept it. */
#define G2_TRAP_REC   0x93FFF000u
#define G2_TRAP_MAGIC 0x50415254u               /* "TRAP" */
static void net_trap(const char *what, uint32_t cause, uint32_t epc)
{
    static int busy;
    uint32_t tval, sp = 0, ra = 0;
    volatile uint32_t *r = (volatile uint32_t *)G2_TRAP_REC;
    __asm__ volatile ("csrr %0, mtval" : "=r"(tval));
    if (trap_last_frame) { sp = (uint32_t)(uintptr_t)trap_last_frame + 64u; ra = trap_last_frame->ra; }
    if (g2_ddr_ready()) {
        r[1] = cause; r[2] = epc; r[3] = tval; r[4] = sp; r[5] = ra; r[6] = SOC_BUILD_ID; r[7] = (r[0] == G2_TRAP_MAGIC) ? r[7] + 1u : 1u;
        r[0] = G2_TRAP_MAGIC;
    }
    if (busy++) return;                          /* an exception inside this report: the record is all there is */
    np("\n!! TRAP "); np(what); np(": mcause "); nphex(cause); np(" mepc "); nphex(epc); np(" mtval "); nphex(tval);
    np(" sp "); nphex(sp); np(" ra "); nphex(ra); np(" -- halted; the watchdog returns the unit to the golden image\n");
    netsh_drain(2000u);
}
static void trap_report(void)
{
    volatile uint32_t *r = (volatile uint32_t *)G2_TRAP_REC;
    char b[120];
    if (r[0] != G2_TRAP_MAGIC) return;
    snprintf(b, sizeof b, "previous run: trap mcause 0x%08lX mepc 0x%08lX mtval 0x%08lX sp 0x%08lX ra 0x%08lX\n",
             (unsigned long)r[1], (unsigned long)r[2], (unsigned long)r[3], (unsigned long)r[4], (unsigned long)r[5]);
    log_put(b); r[0] = 0u;
}
static void npdec64(uint64_t v) { char b[24]; int i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i); np(&b[i]); }
static int net_u64(const char *p, uint64_t *v)
{
    uint64_t x = 0;
    if (!*p) return 0;
    for (; *p; p++) { if (*p < '0' || *p > '9') return 0; x = x * 10u + (uint64_t)(*p - '0'); }
    *v = x; return 1;
}
static uint32_t net_hex(const char *p, int *ok)
{
    uint32_t x = 0; *ok = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    if (!*p) return 0;
    for (; *p; p++) {
        int c = lower((unsigned char)*p);
        if (c >= '0' && c <= '9') x = (x << 4) | (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') x = (x << 4) | (uint32_t)(c - 'a' + 10);
        else return 0;
    }
    *ok = 1; return x;
}
/* "/amiga/<name>" from a name or from a path that already starts with /amiga/ or amiga/; 0 for anything else */
static int net_path(const char *a, char *out, size_t n)
{
    if (!strncmp(a, "/amiga/", 7)) a += 7; else if (!strncmp(a, "amiga/", 6)) a += 6;
    if (!*a || strchr(a, '/') || strchr(a, '\\') || strlen(a) + 8u > n) return 0;
    strcpy(out, "/amiga/"); strcat(out, a);
    return 1;
}
/* a drive holding the file open: 1 (puts and removals are refused) */
#ifdef AMIGA_SHARE
/* a file of the stick in use as a drive image: path in full ("/amiga/<name>") */
static int share_busy(const char *path)
{
    unsigned int d;
    char pre[8];
    if (strlen(path) < 8u) return 0;
    memcpy(pre, path, 7u); pre[7] = 0;
    if (ci_cmp(pre, "/amiga/")) return 0;
    for (d = 0; d < DRIVES; d++) if (s_df_open[d] && !ci_cmp(path + 7, s_df_name[d])) return 1;
    for (d = 0; d < HDS; d++) if (s_hd_open[d] && !ci_cmp(path + 7, s_hd_name[d])) return 1;
    return 0;
}
#endif
static int net_busy(const char *path)
{
    unsigned int d;
    for (d = 0; d < DRIVES; d++) if (s_df_open[d] && !ci_cmp(path + 7, s_df_name[d])) return 1;
    for (d = 0; d < HDS; d++) if (s_hd_open[d] && !ci_cmp(path + 7, s_hd_name[d])) return 1;
    return 0;
}
static void net_trailer(const char *who, uint64_t n, uint32_t crc, uint32_t ms, int failed, uint64_t at, uint32_t st)
{
    np("\n"); np(who); np(": ");
    if (failed) { np("FAILED at byte "); npdec64(at); np(", status "); nphex(st); np("; "); }
    npdec64(n); np(" bytes CRC32 "); nphex(crc ^ 0xFFFFFFFFu); np(", "); npdec(ms); np(" ms\n");
}
#define NET_PIECE 0x80000u
static uint8_t s_net_buf[NET_PIECE];
static void net_get(const char *path)
{
    static FIL f; UINT br; uint64_t size, done = 0; uint32_t crc = 0xFFFFFFFFu, n, st = 0, t0; int failed = 0;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) { np("get: "); np(path); np(": open failed, FatFs "); npdec((uint32_t)fr); np("\n"); return; }
    size = (uint64_t)f_size(&f);
    np("get: "); npdec64(size); np(" bytes follow\n");
    t0 = get_millis();
    while (done < size) {
        n = size - done > NET_PIECE ? NET_PIECE : (uint32_t)(size - done);
        if (!failed) {
            fr = f_read(&f, s_net_buf, n, &br);
            if (fr != FR_OK || br != n) { failed = 1; st = fr != FR_OK ? (uint32_t)fr : 0x100u; memset(s_net_buf, 0, n); }
        } else memset(s_net_buf, 0, n);
        crc = crc32_run(crc, s_net_buf, n);
        if (netsh_write_all(s_net_buf, n) < 0) { f_close(&f); return; }
        netsh_drain(10000u);
        done += n;
        g2_wdt_kick();
    }
    f_close(&f);
    net_trailer("get", size, crc, get_millis() - t0, failed, done, st);
}
/* snap [n]: n consecutive pictures as the capture finished them (FRAME_INFO's buffer at each change), each copied
 * at once, then written to \amiga\SNAP.RAW for `get`: RGB565 words as captured, VID_W samples a line, SNAP_LINES
 * lines a picture, picture after picture */
#define SNAP_LINES 288u
#define SNAP_MAX   16u
static uint16_t s_snap[SNAP_MAX][VID_W * SNAP_LINES];
static void net_snap(unsigned int n)
{
    static FIL f;
    unsigned int k; uint32_t fi, last, t0; UINT bw; FRESULT fr;
    if (n < 1u) n = 1u;
    if (n > SNAP_MAX) n = SNAP_MAX;
    last = AMI_FRAME_INFO & 3u;
    for (k = 0; k < n; k++) {
        t0 = get_millis();
        while (((fi = AMI_FRAME_INFO) & 3u) == last) if (get_millis() - t0 > 200u) { np("snap: no picture finished\n"); return; }
        last = fi & 3u;
        memcpy(s_snap[k], (const void *)(0x94000000u + (s_heads == 2u ? AMI_HEAD2_DELTA : 0u) + AMI_FB_OFF + last * AMI_FB_STEP),
               sizeof s_snap[k]);
    }
    fr = f_open(&f, "/amiga/SNAP.RAW", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) { np("snap: open failed, FatFs "); npdec((uint32_t)fr); np("\n"); return; }
    for (k = 0; k < n && fr == FR_OK; k++) { fr = f_write(&f, s_snap[k], sizeof s_snap[k], &bw); g2_wdt_kick(); }
    f_close(&f);
    np("snap: "); npdec(n); np(" pictures of "); npdec(VID_W); np(" x "); npdec(SNAP_LINES);
    np(" RGB565 in \\amiga\\SNAP.RAW"); np(fr == FR_OK ? "\n" : ", write FAILED\n");
}
static void net_put(const char *path, uint64_t size)
{
    static FIL f; UINT bw; uint64_t done = 0; uint32_t crc = 0xFFFFFFFFu, n, st = 0, t0; int failed = 0, g;
    FRESULT fr;
    if (net_busy(path)) { np("put: "); np(path); np(" is in a drive; eject it first\n"); return; }
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { np("put: "); np(path); np(": create failed, FatFs "); npdec((uint32_t)fr); np("\n"); return; }
    np("put: ready for "); npdec64(size); np(" bytes\n");
    netsh_flush();
    t0 = get_millis();
    while (done < size) {
        n = size - done > NET_PIECE ? NET_PIECE : (uint32_t)(size - done);
        g = netsh_sink(s_net_buf, n, &crc, 10000u);
        if (g != (int)n) {
            f_close(&f);
            net_trailer("put", done + (uint64_t)(g > 0 ? g : 0), crc, get_millis() - t0, 1, done, 0);
            return;
        }
        if (!failed) {
            fr = f_write(&f, s_net_buf, n, &bw);
            if (fr != FR_OK || bw != n) { failed = 1; st = fr != FR_OK ? (uint32_t)fr : 0x100u; }
        }
        done += n;
        g2_wdt_kick();
    }
    fr = f_close(&f);
    if (fr != FR_OK && !failed) { failed = 1; st = (uint32_t)fr; }
    net_trailer("put", size, crc, get_millis() - t0, failed, done, st);
}
static void net_ls(void)
{
    DIR d; FILINFO fi; unsigned int n = 0;
    if (f_opendir(&d, "/amiga") != FR_OK) { np("ls: \\amiga not readable\n"); return; }
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        char s[16]; int i = 15; uint64_t v = (uint64_t)fi.fsize;
        s[i] = 0; do { s[--i] = (char)('0' + v % 10u); v /= 10u; } while (v && i);
        while (i > 3) s[--i] = ' ';
        np(&s[i]); np("  "); np(fi.fname); np((fi.fattrib & AM_DIR) ? "/\n" : "\n");
        n++;
    }
    f_closedir(&d);
    npdec(n); np(" entries\n");
}
static void net_status(void)
{
    unsigned int d;
    np("firmware "); nphex(FW_ID); np("  fabric "); nphex(SOC_BUILD_ID); np("  Kickstart "); np(s_kick_name); np("  stack "); npdec(stack_used()); np(" of 16384 bytes used\n");
    np("machine: chip "); npdec(s_chip); np(" slow "); npdec(s_slow); np(" chipset "); npdec(s_chipset); np(" cpu ");
    npdec(s_cpu); np(s_pal ? " PAL" : " NTSC"); np("\n");
    for (d = 0; d < DRIVES; d++) { np("DF"); npdec(d); np(": "); np(s_df_open[d] ? s_df_name[d] : "(empty)"); np("\n"); }
    for (d = 0; d < HDS; d++) {
        np("HD"); npdec(d); np(": ");
        if (s_hd_open[d]) { np(s_hd_name[d]); np(", "); npdec(s_hd_size[d]); np(" bytes"); } else np("(none)");
        np("\n");
    }
    np("frames "); npdec(AMI_FRAMES); np(", sectors read "); npdec(s_reads); np(", written "); npdec(s_writes);
    np(", hard-disk reads "); npdec(s_hd_reads); np(", writes "); npdec(s_hd_writes); np("\n");
    np("clock gate: "); npdec(get_millis()); np(" ms, held "); npdec(AMI_HELD); np(" clocks, owed "); npdec(AMI_OWED & 0xFFFFu);
    np(" edges, lost "); npdec(AMI_OWED >> 16); np(" edges (saturates at 65535); sdc_hold "); npdec((AMI_CTRL & ACTRL_SDC_HOLD) ? 1u : 0u); np("\n");
    np("write DONE: "); npdec(s_wdone_n); np(" in time, mean "); npdec(s_wdone_n ? (uint32_t)(s_wdone_sum / s_wdone_n) : 0u);
    np(" cycles, longest "); npdec(s_wdone_max); np(" cycles, "); npdec(s_wdone_next); np(" ended by the next request, ");
    npdec(s_wdone_to); np(" timed out (sector not taken); ");
    npdec(PANO_CYC_PER_MS); np(" cycles a ms\n");
    {   uint32_t fi = AMI_FRAME_INFO;
        np("video: "); np(s_native ? (s_pal ? "native 640x288 15 kHz" : "native 640x240 15 kHz") : "scale 640x480 31 kHz");
        np(", Amiga lines "); npdec((fi >> 16) & 0x3FFu); np((fi >> 26) & 1u ? " 31 kHz" : " 15 kHz");
        np((fi & 4u) ? " interlaced" : ""); np(", source height "); npdec(s_src_h); np(", lines dropped "); npdec(AMI_LINE_DROPS); np("\n"); }
    np("MIDI: ");
    if (s_midi.open) {
        np("port "); npdec(s_midi.dev.jack); np(", packets in "); npdec(s_midi.rx_pkts); np(" ("); npdec(s_midi.rx_bytes);
        np(" bytes), out "); npdec(s_midi.tx_pkts); np(" ("); npdec(s_midi.tx_bytes); np(" bytes), IN errors ");
        npdec(s_midi.in_errs); np(", OUT errors "); npdec(s_midi.out_errs); np(", dropped "); npdec(s_midi.drops);
    } else np("(no device)");
    np(", UART "); nphex(AMI_MIDI_ST); np("\n");
    np("FTDI: ");
    if (s_ftdi.open) {
        np("port "); npdec(s_ftdi.dev.jack); np(" "); nphex(s_ftdi.dev.vid << 16 | s_ftdi.dev.pid);
        np(s_ftdi.mpsse ? ", channel A MPSSE" : s_ftdi.index ? ", channel A serial" : ", serial");
        np(", packets "); npdec(s_ftdi.mps_in); np(", in "); npdec(s_ftdi.rx_pkts); np(" ("); npdec(s_ftdi.rx_bytes);
        np(" bytes), out "); npdec(s_ftdi.tx_pkts); np(" ("); npdec(s_ftdi.tx_bytes); np(" bytes), IN errors ");
        npdec(s_ftdi.in_errs); np(", OUT errors "); npdec(s_ftdi.out_errs); np(", dropped "); npdec(s_ftdi.drops);
    } else np("(no device)");
    np(s_ftdi_want_mpsse ? ", FTDI=MPSSE\n" : ", FTDI=SERIAL\n");
    np("CTRL "); nphex(AMI_CTRL); np("  STATUS "); nphex(AMI_STATUS); np("  SDC_REQ "); nphex(AMI_SDC_REQ);
    np("  SDC_SECTOR "); nphex(AMI_SDC_SECTOR); np("\n");
}
static void net_log(void)
{
    uint32_t i, j = 0; char b[129];
    for (i = 0; i < s_log_n; i++) {
        if (s_log[i] == '\r') continue;
        b[j++] = s_log[i];
        if (j == 128u) { b[j] = 0; np(b); j = 0; netsh_flush(); netsh_drain(2000u); }
    }
    b[j] = 0; np(b);
}
/* ---- Amiga file systems inside the images (fw/amiga/afs.c) ------------------------------------
 * Drives DF0-DF3 and HD0-HD1.  A hard-disk image is read through hd_block and written as sdc_service writes a sector
 * of the core (the file and the held block); a floppy image through its copy in chip A and its file.  AmigaOS holds a
 * mounted volume's blocks, so a write to a hard-disk image stops the core and ends with a cold start
 *, and a write to a floppy image ejects it from the core first and inserts it again
 * AFS_REINS_MS after (5.4).  Reads are made with the machine running and may lag blocks AmigaOS has not written yet. */
/* time() for ADFlib's entry dates (adfGiveCurrentTime, through the UTC localtime of fw/amiga/afs.c): the NTP time of
 * fw/ntptime.c plus the zone of AMIGA.INI TZ=, since AmigaDOS keeps local time; before NTP has answered,
 * 1978-01-01 00:00:00, the first day AmigaDOS dates can hold.  The C library's time() would call _gettimeofday,
 * which fw/syscalls.c does not provide. */
#define AMIGA_EPOCH_UNIX 252460800u
time_t time(time_t *t)
{
    time_t v = ntptime_known() ? (time_t)(ntptime_unix() + (uint32_t)(ntptime_tz_min * 60)) : (time_t)AMIGA_EPOCH_UNIX;
    if (t) *t = v;
    return v;
}
/* date [zone]: the time of day and its source; with a zone ("[+|-]h[:mm]", minutes east of UTC) the zone is set and
 * written to AMIGA.INI as TZ= */
static void net_date(int argc, char **argv)
{
    unsigned Y, M, D, h, m, sec; char b[48]; int z = ntptime_tz_min, ok = 1;
    if (argc > 1) {
        z = tz_parse(argv[1], &ok);
        if (!ok) { np("usage: date [zone: +h, -h:mm, ...]\n"); return; }
        ntptime_tz_min = z; ini_set("TZ", argv[1]); s_rtc_due = 1u;
    }
    z = z < 0 ? -z : z;
    if (!ntptime_known()) { np("date: unknown (NTP has not answered"); np(ntptime_busy() ? ", asking)\n" : ")\n"); }
    else {
        uint32_t ip = ntptime_server();
        ntptime_civil(ntptime_unix() + (uint32_t)(ntptime_tz_min * 60), &Y, &M, &D, &h, &m, &sec);
        snprintf(b, sizeof b, "date: %04u-%02u-%02u %02u:%02u:%02u", Y, M, D, h, m, sec); np(b);
        snprintf(b, sizeof b, ", NTP %u.%u.%u.%u\n", (unsigned)(ip >> 24), (unsigned)(ip >> 16) & 255u, (unsigned)(ip >> 8) & 255u, (unsigned)ip & 255u); np(b);
    }
    snprintf(b, sizeof b, "zone: UTC%c%02d:%02d\n", ntptime_tz_min < 0 ? '-' : '+', z / 60, z % 60); np(b);
}
#define AFS_REINS_MS 3000u
static unsigned int s_afs_ok;
static uint32_t s_afs_reins[DRIVES];                /* get_millis() of a pending insertion (bit 0 set), 0 none */
static int afs_hd_rd(void *ctx, uint32_t blk, uint32_t n, uint8_t *buf)
{
    unsigned int h = (unsigned int)(uintptr_t)ctx;
    for (; n; n--, blk++, buf += 512) { const uint8_t *s = hd_block(h, blk * 512u, 0u); if (!s) return -1; memcpy(buf, s, 512u); }
    return 0;
}
static int afs_hd_wr(void *ctx, uint32_t blk, uint32_t n, const uint8_t *buf)
{
    unsigned int h = (unsigned int)(uintptr_t)ctx;
    for (; n; n--, blk++, buf += 512) { uint8_t *p = hd_block(h, blk * 512u, 1u); if (!p) return -1; memcpy(p, buf, 512u); }
    return 0;
}
static int afs_df_rd(void *ctx, uint32_t blk, uint32_t n, uint8_t *buf)
{
    unsigned int d = (unsigned int)(uintptr_t)ctx; uint32_t i;
    const volatile uint8_t *s = (const volatile uint8_t *)(AMI_DISK_UC + d * AMI_DISK_STEP + blk * 512u);
    for (i = 0; i < n * 512u; i++) buf[i] = s[i];
    return 0;
}
static int afs_df_wr(void *ctx, uint32_t blk, uint32_t n, const uint8_t *buf)
{
    unsigned int d = (unsigned int)(uintptr_t)ctx; uint32_t i; UINT w;
    volatile uint8_t *t = (volatile uint8_t *)(AMI_DISK_UC + d * AMI_DISK_STEP + blk * 512u);
    for (i = 0; i < n * 512u; i++) t[i] = buf[i];
    if (f_lseek(&s_df_f[d], blk * 512u) != FR_OK || f_write(&s_df_f[d], buf, n * 512u, &w) != FR_OK || w != n * 512u) return -1;
    s_df_dirty[d] = 1u; s_df_wr_ms[d] = get_millis();
    return 0;
}
/* the floppy images ejected by a write, inserted again once AFS_REINS_MS have passed (from net_service) */
#define AFS_HOLD_MS 600000u
static unsigned int s_afs_held[HDS], s_afs_hold[HDS];
static uint32_t s_afs_held_ms[HDS];
static void afs_give(unsigned int d);
static void afs_poll(void)
{
    unsigned int d;
    for (d = 0; d < HDS; d++)
        if (s_afs_hold[d] && s_afs_held[d] && get_millis() - s_afs_held_ms[d] >= AFS_HOLD_MS) {
            log_put("afs: hold of HD"); { char b[2] = { (char)('0' + d), 0 }; log_put(b); } log_put(" given back after 600 s without a console command\n");
            afs_give(d);
        }
    for (d = 0; d < DRIVES; d++)
        if (s_afs_reins[d] && get_millis() - s_afs_reins[d] >= AFS_REINS_MS) {
            char b[2] = { (char)('0' + d), 0 };
            s_afs_reins[d] = 0u;
            if (s_df_open[d]) { mount_req(d, s_df_size[d]); log_put("afs: DF"); log_put(b); log_put(" inserted again\n"); }
        }
}
/* "HD0:path", "df1:path", "HD0" -> kind 'H' or 'D', the drive, the path after the colon */
static int afs_drive(const char *a, char *kind, unsigned int *d, const char **path)
{
    int k = lower((unsigned char)a[0]);
    if ((k != 'h' && k != 'd') || lower((unsigned char)a[1]) != (k == 'h' ? 'd' : 'f') || a[2] < '0' || a[2] > '9') return 0;
    *d = (unsigned int)(a[2] - '0');
    if ((k == 'h' && *d >= HDS) || (k == 'd' && *d >= DRIVES)) return 0;
    if (a[3] != 0 && a[3] != ':') return 0;
    *kind = k == 'h' ? 'H' : 'D'; *path = a[3] ? a + 4 : "";
    return 1;
}
static int afs_io_of(char kind, unsigned int d, int write, struct afs_io *io)
{
    if (!s_afs_ok) { if (afs_init() != AFS_OK) return AFS_E_LIB; s_afs_ok = 1u; }
    if (kind == 'H') {
        if (!s_hd_open[d]) return AFS_E_NOTFOUND;
        io->rd = afs_hd_rd; io->wr = afs_hd_wr; io->blocks = s_hd_size[d] / 512u;
    } else {
        if (!s_df_open[d]) return AFS_E_NOTFOUND;
        io->rd = afs_df_rd; io->wr = afs_df_wr; io->blocks = s_df_size[d] / 512u;
    }
    io->ctx = (void *)(uintptr_t)d;
    if (!write) io->wr = 0;
    else if (!s_write) return AFS_E_RO;
    return AFS_OK;
}
static void afs_date_np(const struct afs_date *t)
{
    char b[24];
    snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", t->year, t->month, t->day, t->hour, t->min, t->sec); np(b);
}
static void afs_ent_np(void *u, const struct afs_ent *e)
{
    char p[9], b[12]; uint32_t r = e->prot;
    (void)u;
    p[0] = (r & 0x80u) ? 'h' : '-'; p[1] = (r & 0x40u) ? 's' : '-'; p[2] = (r & 0x20u) ? 'p' : '-'; p[3] = (r & 0x10u) ? 'a' : '-';
    p[4] = (r & 0x08u) ? '-' : 'r'; p[5] = (r & 0x04u) ? '-' : 'w'; p[6] = (r & 0x02u) ? '-' : 'e'; p[7] = (r & 0x01u) ? '-' : 'd'; p[8] = 0;
    snprintf(b, sizeof b, "%10lu", (unsigned long)e->size);
    np(e->dir == 1 ? "dir  " : e->dir == 2 ? "link " : "file "); np(b); np(" "); np(p); np(" "); afs_date_np(&e->date);
    np("  "); np(e->name); if (e->dir == 1) np("/");
    if (e->comment[0]) { np("  ; "); np(e->comment); }
    np("\n");
}
/* between chunks of a copy the core's pending sector request is served: with the volume inhibited by PANOFS the
 * machine runs, and its other disk requests (the mailbox block, the floppies) must not wait for the whole copy */
/* during a long afs command: the network served (netsh_poll moves frames and output only, it runs no command) every
 * 50 ms, so the session's output flows and the unit answers; with a tar being read its offset every 2 s */
static FIL *s_afs_tar;
static void afs_net(void)
{
    static uint32_t t_net, t_say;
    uint32_t now = get_millis();
    g2_wdt_kick(); sdc_service();
    if (now - t_net < 50u) return;
    t_net = now;
    if (s_afs_tar && now - t_say >= 2000u) { t_say = now; np("untar: offset "); npdec((uint32_t)f_tell(s_afs_tar)); np("\n"); }
    (void)netsh_poll();
}
static int afs_src_fil(void *u, uint8_t *buf, uint32_t n) { UINT br; afs_net(); return f_read((FIL *)u, buf, n, &br) == FR_OK ? (int)br : -1; }
static int afs_dst_fil(void *u, const uint8_t *buf, uint32_t n) { UINT bw; afs_net(); return f_write((FIL *)u, buf, n, &bw) == FR_OK && bw == n ? 0 : -1; }
/* PANOFS (amiga/panofs): alive when it has read the mailbox block within MBX_ALIVE_MS.  A request
 * (1 inhibit, 2 release) is put in the block with a new sequence number; the Amiga keeps running and its sectors are
 * served while the acknowledgement of that number is awaited, at most MBX_WAIT_MS.  0 done, 1 refused, -1 no answer. */
#define MBX_ALIVE_MS 2000u
#define MBX_WAIT_MS  3000u
static int mbx_alive(unsigned int h) { return s_mbx_ok[h] && s_mbx_seen[h] && get_millis() - s_mbx_seen[h] < MBX_ALIVE_MS; }
static int mbx_request(unsigned int h, uint32_t req)
{
    uint32_t t0 = get_millis(), seq = ++s_mbx_seq[h];
    be32_put(s_mbx[h] + 8, req); be32_put(s_mbx[h] + 4, seq);
    s_mbx_got[h] = 0u;
    while (get_millis() - t0 < MBX_WAIT_MS) {
        g2_wdt_kick(); sdc_service();
        if (s_mbx_got[h]) {
            s_mbx_got[h] = 0u;
            if (!memcmp(s_mbx_ack[h], "PFAK", 4) && be32(s_mbx_ack[h] + 4) == seq && be32(s_mbx_ack[h] + 8) == req)
                return be32(s_mbx_ack[h] + 12) ? 1 : 0;
        }
    }
    return -1;
}
/* amiga: what PANOFS last reported (the figures it writes every 5 s) */
static void net_amiga(void)
{
    unsigned int h; char b[96];
    for (h = 0; h < HDS; h++) {
        const uint8_t *a = s_mbx_ack[h];
        np("HD"); npdec(h); np(": ");
        if (!s_hd_open[h]) { np("no image\n"); continue; }
        if (!s_mbx_ok[h]) { np("no mailbox block (the RigidDiskBlock uses block 2)\n"); continue; }
        if (!s_mbx_seen[h]) { np("PANOFS not seen since the image was opened\n"); continue; }
        snprintf(b, sizeof b, "PANOFS %s, last poll %lu ms ago\n", mbx_alive(h) ? "running" : "silent",
                 (unsigned long)(get_millis() - s_mbx_seen[h])); np(b);
        if (memcmp(a, "PFAK", 4)) continue;
        snprintf(b, sizeof b, "  PANOFS version %lu, exec.library %u.%u, polls %lu, volume %s\n", (unsigned long)be32(a + 16),
                 (unsigned)(be32(a + 20) >> 16), (unsigned)(be32(a + 20) & 0xFFFFu), (unsigned long)be32(a + 40),
                 be32(a + 44) ? "inhibited" : "in use by AmigaOS"); np(b);
        snprintf(b, sizeof b, "  free chip %lu (largest %lu), free fast %lu (largest %lu) bytes\n", (unsigned long)be32(a + 24),
                 (unsigned long)be32(a + 32), (unsigned long)be32(a + 28), (unsigned long)be32(a + 36)); np(b);
        snprintf(b, sizeof b, "  last request %lu acknowledged: %s\n", (unsigned long)be32(a + 4),
                 be32(a + 12) ? "refused" : "done"); np(b);
    }
}
/* A hard-disk volume taken for writing: 1 inhibited by PANOFS (the machine runs), 2 the machine
 * stopped by afs (a cold start gives it back), 3 the machine already stopped by the menu (a cold start at its Resume,
 * s_afs_cold).  "afs hold" keeps the volume taken across commands (s_afs_hold), every console command renewing it
 * (net_exec, so a long file transfer between two afs commands does not end it); a hold left AFS_HOLD_MS without a
 * command is given back by afs_poll.  PANOFS seen since the image was opened but silent now (the machine rebooting,
 * as after an afs cold start) is awaited up to MBX_BOOT_MS before the machine is stopped instead: a stop would end in
 * another cold start, and successive commands in a chain of them. */
#define MBX_BOOT_MS 30000u
static void afs_take(unsigned int d)
{
    int running = (AMI_CTRL & ACTRL_RUN) && !(AMI_CTRL & ACTRL_RESET);
    s_afs_held_ms[d] = get_millis();
    if (s_afs_held[d]) return;
    if (!running) { s_afs_held[d] = 3u; s_afs_cold = 1u; return; }
    if (!mbx_alive(d) && s_mbx_seen[d]) {
        uint32_t t0 = get_millis();
        np("afs: waiting for PANOFS\n"); netsh_flush();
        while (!mbx_alive(d) && get_millis() - t0 < MBX_BOOT_MS) { g2_wdt_kick(); sdc_service(); }
    }
    if (mbx_alive(d)) {
        int r = mbx_request(d, 1u);
        if (r == 0) { s_afs_held[d] = 1u; np("afs: DH0: inhibited by PANOFS\n"); return; }
        np(r > 0 ? "afs: PANOFS refused to inhibit the volume: the machine is stopped instead\n"
                 : "afs: PANOFS did not answer: the machine is stopped instead\n");
    }
    run_stop(); s_afs_held[d] = 2u;
}
static void afs_give(unsigned int d)
{
    unsigned int how = s_afs_held[d];
    s_afs_held[d] = 0u; s_afs_hold[d] = 0u;
    if (how == 1u) {
        if (mbx_request(d, 2u) == 0) { np("afs: DH0: released, AmigaOS reads the volume again\n"); return; }
        np("afs: PANOFS did not release the volume: cold start\n"); log_put("afs: PANOFS did not release DH0:, cold start\n");
        run_stop(); ami_video_setup(); cold_start();
    } else if (how == 2u) { np("afs: cold start (the Amiga held the volume)\n"); ami_video_setup(); cold_start(); }
    else if (how == 3u) np("afs: the machine starts cold at the menu's Resume\n");
}
/* reload: AMIGA.INI read again, its Kickstart loaded and its drives opened, then a cold start (as at power-up) */
static void net_reload(void)
{
    unsigned int d;
    np("reload: AMIGA.INI read again, cold start\n"); netsh_flush();
    run_stop(); ctrl_set(ACTRL_RESET);
    for (d = 0; d < DRIVES; d++) if (s_df_open[d]) { (void)f_sync(&s_df_f[d]); f_close(&s_df_f[d]); s_df_open[d] = 0u; s_df_dirty[d] = 0u; }
    for (d = 0; d < HDS; d++) if (s_hd_open[d]) hd_eject(d);
    ini_read();
    if (kick_load() != 0) { np("reload: the Kickstart did not load\n"); log_save(); return; }
    for (d = 0; d < DRIVES; d++) if (s_df_name[d][0]) { char nm[256]; strcpy(nm, s_df_name[d]); (void)df_insert(d, nm); }
    for (d = 0; d < HDS; d++) if (s_hd_name[d][0]) (void)hd_insert(d, s_hd_name[d]);
    for (d = 0; d < DRIVES; d++) if (!s_df_open[d]) mount_req(d, 0u);   /* the core still holds the closed images' mounts */
    log_put("network: reload\n"); log_save();
    ami_video_setup(); cold_start();
}
/* afs untar: the archive on the stick read through the volume service's ustar reader (fw/amiga/afs.c afs_untar) */
static int afs_tar_read(void *u, uint8_t *buf, uint32_t n) { UINT br; afs_net(); return f_read((FIL *)u, buf, n, &br) == FR_OK ? (int)br : -1; }
static int afs_tar_skip(void *u, uint32_t n) { FIL *f = (FIL *)u; afs_net(); return f_lseek(f, f_tell(f) + n) == FR_OK ? 0 : -1; }
/* afs <verb> ...: the Amiga file-system verbs (afs alone lists them) */
static void net_afs(int argc, char **argv)
{
    char kind, path[300]; unsigned int d; const char *ap; struct afs_io io; struct afs_vol *v = 0;
    static FIL f;                                /* static, as every FIL here: one holds a 4 KB sector buffer (FF_MAX_SS,
                                                  * FF_FS_TINY 0) and the stack is 16 KB of block RAM */
    volatile int rc; int write;
    const char *volatile verb = argc > 1 ? argv[1] : "";   /* volatile: live across the afs error longjmp */
    const char *spec = (!ci_cmp(verb, "put") || !ci_cmp(verb, "untar")) ? (argc > 3 ? argv[3] : "") : (argc > 2 ? argv[2] : "");
    int fmt = !ci_cmp(verb, "format");
    if ((!ci_cmp(verb, "hold") || !ci_cmp(verb, "release")) && argc == 3 && afs_drive(argv[2], &kind, &d, &ap) && kind == 'H') {
        if (!ci_cmp(verb, "hold")) {
            if (!s_hd_open[d]) { np("afs: hold: no image in HD"); npdec(d); np("\n"); return; }
            afs_take(d); s_afs_hold[d] = 1u;
            np("afs: hold HD"); npdec(d); np(s_afs_held[d] == 1u ? ": held through PANOFS\n" : ": held, the machine stopped\n");
        } else {
            np("afs: release HD"); npdec(d); np(s_afs_held[d] ? "\n" : ": not held\n");
            if (s_afs_held[d]) afs_give(d);
        }
        return;
    }
    write = fmt || !ci_cmp(verb, "put") || !ci_cmp(verb, "untar") || !ci_cmp(verb, "mkdir") || !ci_cmp(verb, "rm") ||
            !ci_cmp(verb, "mv");
    if (!afs_drive(spec, &kind, &d, &ap) ||
        !((!ci_cmp(verb, "info") && argc == 3) || (!ci_cmp(verb, "ls") && argc == 3) || (!ci_cmp(verb, "stat") && argc == 3) ||
          (!ci_cmp(verb, "get") && argc == 4) || (!ci_cmp(verb, "put") && argc == 4) || (!ci_cmp(verb, "mkdir") && argc == 3) ||
          (!ci_cmp(verb, "rm") && argc == 3) || (!ci_cmp(verb, "mv") && argc == 4) || (fmt && argc == 4) ||
          (!ci_cmp(verb, "untar") && argc == 4)) ||
        (write && kind == 'H' && !s_hd_open[d])) {
        np("usage: afs info <drive> | ls <drive>:[path] | stat <drive>:<path> | get <drive>:<path> <file of \\amiga> |\n"
           "           put <file of \\amiga> <drive>:<path> | mkdir <drive>:<path> | rm <drive>:<path> |\n"
           "           mv <drive>:<path> <path> | format <drive> <volume name>  (FFS, international: DOS\\3) |\n"
           "           hold HD<n> | release HD<n>  (the volume kept taken across writes, given back after 60 s idle) |\n"
           "           untar <ustar file of \\amiga> <drive>:<path>\n"
           "       drives DF0-DF3, HD0-HD1 (with an image)\n");
        return;
    }
    if (write && kind == 'H') afs_take(d);
    if (write && kind == 'D' && s_df_open[d]) { mount_req(d, 0u); s_afs_reins[d] = 0u; }
    rc = afs_io_of(kind, d, write, &io);
    afs_abort_armed = 1;
    if (setjmp(afs_abort_jb)) {
        np("afs: ADFlib assertion at "); np(afs_abort_file); np(":"); npdec((uint32_t)afs_abort_line); np("\n");
        log_put("afs: ADFlib assertion\n");
        rc = AFS_E_LIB;
    } else if (rc == AFS_OK && fmt) rc = afs_format(&io, argv[3], 3u);
    else if (rc == AFS_OK && (rc = afs_mount(&io, write, &v)) == AFS_OK) {
        if (!ci_cmp(verb, "info")) {
            struct afs_info inf;
            if ((rc = afs_info(v, &inf)) == AFS_OK) {
                np("volume      "); np(inf.name); np("\nfilesystem  "); np(inf.fs); np(" (DOS\\"); npdec(inf.dostype & 0xFFu); np(")\n");
                np("partition   "); np(inf.rdb ? inf.part : "(none)"); np(" blocks "); npdec(inf.first); np(".."); npdec(inf.first + inf.blocks - 1u);
                np(" ("); npdec(inf.blocks); np(" blocks)\nfree        "); npdec(inf.free); np(" blocks ("); npdec64((uint64_t)inf.free * 512u);
                np(" bytes)\nbitmap      "); np(inf.bitmap_valid ? "valid" : "NOT VALID"); np("\ncreated     "); afs_date_np(&inf.created);
                np("\nchanged     "); afs_date_np(&inf.changed); np("\n");
            }
        } else if (!ci_cmp(verb, "ls")) rc = afs_ls(v, ap, afs_ent_np, 0);
        else if (!ci_cmp(verb, "stat")) rc = afs_stat(v, ap, afs_ent_np, 0);
        else if (!ci_cmp(verb, "get")) {
            uint32_t n = 0;
            if (!net_path(argv[3], path, sizeof path) || net_busy(path)) rc = AFS_E_NAME;
            else if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) rc = AFS_E_SRC;
            else {
                rc = afs_get(v, ap, afs_dst_fil, &f, &n);
                if (f_close(&f) != FR_OK && rc == AFS_OK) rc = AFS_E_SRC;
                if (rc == AFS_OK) { np("afs: "); npdec(n); np(" bytes to "); np(path); np("\n"); }
            }
        } else if (!ci_cmp(verb, "put")) {
            if (!net_path(argv[2], path, sizeof path)) rc = AFS_E_NAME;
            else if (f_open(&f, path, FA_READ) != FR_OK) rc = AFS_E_SRC;
            else { rc = afs_put(v, ap, afs_src_fil, &f); f_close(&f); }
        } else if (!ci_cmp(verb, "untar")) {
            unsigned int nf = 0u, nd = 0u;
            if (!net_path(argv[2], path, sizeof path)) rc = AFS_E_NAME;
            else if (f_open(&f, path, FA_READ) != FR_OK) rc = AFS_E_SRC;
            else {
                s_afs_tar = &f;
                rc = afs_untar(v, afs_tar_read, afs_tar_skip, &f, ap, &nf, &nd); f_close(&f);
                s_afs_tar = 0;
                np("afs: untar: "); npdec(nf); np(" files, "); npdec(nd); np(" directories\n");
            }
        } else if (!ci_cmp(verb, "mkdir")) rc = afs_mkdir(v, ap);
        else if (!ci_cmp(verb, "rm")) rc = afs_rm(v, ap);
        else rc = afs_mv(v, ap, argv[3]);
        afs_umount(v);
    }
    afs_abort_armed = 0;
    if (write && kind == 'H') hd_flush(d);
    if (write && kind == 'D' && s_df_open[d]) { (void)f_sync(&s_df_f[d]); s_afs_reins[d] = get_millis() | 1u; }
    np("afs: "); np(verb); np(" "); np(spec); np(": "); np(afs_strerror(rc)); np("\n");
    if (write && rc == AFS_OK) { log_put("afs: "); log_put(verb); log_put(" "); log_put(spec); log_put("\n"); }
    if (write && kind == 'H' && !s_afs_hold[d]) afs_give(d);
}
/* df: the floppy drives; df <n> <file of \amiga>: the image inserted into DF<n> (read whole, written back as the
 * machine writes it); df <n> eject.  For this session: AMIGA.INI is not changed. */
static void net_df(int argc, char **argv)
{
    unsigned int d;
    if (argc == 1) {
        for (d = 0; d < DRIVES; d++) {
            np("DF"); npdec(d); np(": ");
            if (s_df_open[d]) { np(s_df_name[d]); np(", "); npdec(s_df_size[d]); np(" bytes"); np(s_df_dirty[d] ? ", changed\n" : "\n"); }
            else np("(empty)\n");
        }
        return;
    }
    if (argc != 3 || argv[1][0] < '0' || argv[1][0] > '3' || argv[1][1]) { np("usage: df [<0-3> <file of \\amiga> | <0-3> eject]\n"); return; }
    d = (unsigned int)(argv[1][0] - '0');
    if (!ci_cmp(argv[2], "eject")) {
        if (!s_df_open[d]) { np("df: DF"); npdec(d); np(" is empty\n"); return; }
        df_eject(d); s_afs_reins[d] = 0u;
        np("df: DF"); npdec(d); np(" ejected\n"); log_put("network: df eject\n");
        return;
    }
    {
        char nm[256], path[300]; unsigned int k;
        strncpy(nm, argv[2], sizeof nm - 1u); nm[sizeof nm - 1u] = 0;
        if (!net_path(nm, path, sizeof path)) { np("df: bad file name\n"); return; }
        for (k = 0; k < DRIVES; k++)
            if (k != d && s_df_open[k] && !ci_cmp(s_df_name[k], nm)) { np("df: "); np(nm); np(" is in DF"); npdec(k); np("\n"); return; }
        s_afs_reins[d] = 0u;
        if (df_insert(d, nm) != 0) { np("df: "); np(nm); np(" not inserted (missing, or not a floppy image)\n"); return; }
        np("df: "); np(nm); np(" in DF"); npdec(d); np("\n"); log_put("network: df insert\n");
    }
}
/* adf new <file of \amiga> [<volume name> [hd]]: a blank floppy image, 901,120 bytes (1,802,240 with hd), formatted
 * FFS international (DOS\3) when a volume name is given */
static FIL s_adf_f;
static int adf_file_rd(void *ctx, uint32_t blk, uint32_t n, uint8_t *buf)
{
    UINT r; (void)ctx;
    return f_lseek(&s_adf_f, blk * 512u) == FR_OK && f_read(&s_adf_f, buf, n * 512u, &r) == FR_OK && r == n * 512u ? 0 : -1;
}
static int adf_file_wr(void *ctx, uint32_t blk, uint32_t n, const uint8_t *buf)
{
    UINT w; (void)ctx;
    return f_lseek(&s_adf_f, blk * 512u) == FR_OK && f_write(&s_adf_f, buf, n * 512u, &w) == FR_OK && w == n * 512u ? 0 : -1;
}
static void net_adf(int argc, char **argv)
{
    static uint8_t z[11264];                     /* one track of a DD disk */
    char path[300]; uint32_t size, off; UINT w; int rc = AFS_OK;
    if (argc < 3 || argc > 5 || ci_cmp(argv[1], "new") || (argc == 5 && ci_cmp(argv[4], "hd"))) {
        np("usage: adf new <file of \\amiga> [<volume name> [hd]]\n"); return;
    }
    if (!net_path(argv[2], path, sizeof path)) { np("adf: bad file name\n"); return; }
    if (f_stat(path, 0) != FR_NO_FILE) { np("adf: "); np(path); np(" exists\n"); return; }
    size = argc == 5 ? 1802240u : 901120u;
    if (f_open(&s_adf_f, path, FA_READ | FA_WRITE | FA_CREATE_NEW) != FR_OK) { np("adf: cannot create "); np(path); np("\n"); return; }
    memset(z, 0, sizeof z);
    for (off = 0; off < size; off += sizeof z) {
        if (f_write(&s_adf_f, z, sizeof z, &w) != FR_OK || w != sizeof z) { rc = AFS_E_IO; break; }
        g2_wdt_kick(); sdc_service();
    }
    if (rc == AFS_OK && argc >= 4) {
        struct afs_io io;
        if (!s_afs_ok) { if (afs_init() != AFS_OK) rc = AFS_E_LIB; else s_afs_ok = 1u; }
        io.rd = adf_file_rd; io.wr = adf_file_wr; io.ctx = 0; io.blocks = size / 512u;
        if (rc == AFS_OK) {
            afs_abort_armed = 1;
            if (setjmp(afs_abort_jb)) rc = AFS_E_LIB;
            else rc = afs_format(&io, argv[3], 3u);
            afs_abort_armed = 0;
        }
    }
    if (f_close(&s_adf_f) != FR_OK && rc == AFS_OK) rc = AFS_E_IO;
    if (rc != AFS_OK) { (void)f_unlink(path); np("adf: new "); np(path); np(": "); np(afs_strerror(rc)); np(", removed\n"); return; }
    np("adf: "); np(path); np(", "); npdec(size); np(" bytes"); if (argc >= 4) { np(", formatted as "); np(argv[3]); } np("\n");
    log_put("network: adf new\n");
}
/* key <text>: keys typed into the machine through hid.v command 1, the path of a USB keyboard (kbd_send), to whatever
 * window AmigaOS has active; it works while AmigaOS waits in a requester.  Characters of the US layout as the HID
 * usage of their key (with left Shift where the layout needs it); \n Return, \t Tab, \e Escape, \\ and \{;
 * {NAME} or {MOD+...+NAME}: NAME esc f1..f10 help del bs tab return space up down left right, or one character; MOD
 * shift ctrl alt amiga (left Amiga) ramiga ralt rshift; {wait:MS} pauses.  A key is held KEY_MS and released, with
 * KEY_MS after it; the machine's sectors are served meanwhile. */
#define KEY_MS 20u
static void key_wait(uint32_t ms)
{
    uint32_t t0 = get_millis();
    while (get_millis() - t0 < ms) { g2_wdt_kick(); sdc_service(); }
}
/* the HID usage of character c (US layout), *shift set when it needs Shift; 0 when it has no key */
static unsigned int key_usage(char c, unsigned int *shift)
{
    static const char sh_num[] = ")!@#$%^&*(";
    static const char pun[]  = "-=[]\\;'`,./";
    static const char spun[] = "_+{}|:\"~<>?";
    static const uint8_t pun_u[] = { 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    const char *q;
    *shift = 0u;
    if (c >= 'a' && c <= 'z') return 0x04u + (unsigned int)(c - 'a');
    if (c >= 'A' && c <= 'Z') { *shift = 1u; return 0x04u + (unsigned int)(c - 'A'); }
    if (c >= '1' && c <= '9') return 0x1Eu + (unsigned int)(c - '1');
    if (c == '0') return 0x27u;
    if (c == ' ') return 0x2Cu;
    if ((q = strchr(sh_num, c)) && c) { *shift = 1u; return q == sh_num ? 0x27u : 0x1Eu + (unsigned int)(q - sh_num) - 1u; }
    if ((q = strchr(pun, c)) && c) return pun_u[q - pun];
    if ((q = strchr(spun, c)) && c) { *shift = 1u; return pun_u[q - spun]; }
    return 0u;
}
static void key_stroke(unsigned int mods, unsigned int usage)
{
    unsigned int b;
    for (b = 0; b < 8u; b++) if (mods & (1u << b)) kbd_send(0x68u + b, 0u);
    kbd_send(usage, 0u); key_wait(KEY_MS);
    kbd_send(usage, 1u);
    for (b = 0; b < 8u; b++) if (mods & (1u << b)) kbd_send(0x68u + b, 1u);
    key_wait(KEY_MS);
}
static void net_key(int argc, char **argv)
{
    static const struct { const char *n; uint8_t u; } names[] = {
        { "esc", 0x29 }, { "help", 0x4D }, { "del", 0x4C }, { "bs", 0x2A }, { "tab", 0x2B }, { "return", 0x28 },
        { "enter", 0x28 }, { "space", 0x2C }, { "up", 0x52 }, { "down", 0x51 }, { "left", 0x50 }, { "right", 0x4F } };
    static const struct { const char *n; uint8_t bit; } mods[] = {
        { "ctrl", 0 }, { "shift", 1 }, { "alt", 2 }, { "amiga", 3 }, { "lamiga", 3 }, { "rshift", 5 }, { "ralt", 6 },
        { "ramiga", 7 } };
    const char *p; unsigned int n = 0u, sh, u, i;
    if (argc != 2) { np("usage: key <text>  (\\n \\t \\e, {f1} {help} {amiga+v} {ctrl+c} {wait:500}; quote text with spaces)\n"); return; }
    if (!s_fwd) { np("key: the machine is not running (menu open or stopped)\n"); return; }
    for (p = argv[1]; *p; ) {
        unsigned int m = 0u;
        if (*p == '\\' && p[1]) {
            p++;
            u = *p == 'n' || *p == 'r' ? 0x28u : *p == 't' ? 0x2Bu : *p == 'e' ? 0x29u : key_usage(*p, &sh);
            if (*p != 'n' && *p != 'r' && *p != 't' && *p != 'e' && sh) m = 2u;
            p++;
        } else if (*p == '{') {
            const char *e = strchr(p, '}'), *t = p + 1; size_t len;
            if (!e) { np("key: { without }\n"); return; }
            if (!strncmp(t, "wait:", 5)) { key_wait((uint32_t)atoi(t + 5)); p = e + 1; continue; }
            for (;;) {
                const char *plus = memchr(t, '+', (size_t)(e - t));
                if (!plus || plus == t) break;
                for (i = 0; i < sizeof mods / sizeof mods[0]; i++)
                    if (strlen(mods[i].n) == (size_t)(plus - t) && !strncmp(t, mods[i].n, (size_t)(plus - t))) break;
                if (i == sizeof mods / sizeof mods[0]) { np("key: unknown qualifier\n"); return; }
                m |= 1u << mods[i].bit; t = plus + 1;
            }
            len = (size_t)(e - t); u = 0u;
            for (i = 0; i < sizeof names / sizeof names[0]; i++) if (strlen(names[i].n) == len && !strncmp(t, names[i].n, len)) u = names[i].u;
            if (!u && (len == 2 || len == 3) && t[0] == 'f') { int f = atoi(t + 1); if (f >= 1 && f <= 10) u = 0x3Au + (unsigned int)(f - 1); }
            if (!u && len == 1) { u = key_usage(*t, &sh); if (sh) m |= 2u; }
            if (!u) { np("key: unknown key name\n"); return; }
            p = e + 1;
        } else { u = key_usage(*p, &sh); if (sh) m = 2u; p++; }
        if (!u) { np("key: a character has no key\n"); return; }
        key_stroke(m, u); n++;
    }
    np("key: "); npdec(n); np(" keys\n");
}
static void net_exec(char *line);
/* the console's `ftdi': the chip's control requests (FTDI AN232B-05 and the D2XX functions they serve: 0x09/0x0A latency
 * timer, 0x0B bit mode, 0x0C pins, 0x90/0x91/0x92 EEPROM read, write, erase), for programs on the Amiga through the
 * command block.  Every transfer runs with the USB interrupt masked (the interrupt handler shares s_ftdi). */
static int ftdi_ctl(unsigned int type, unsigned int req, unsigned int value, unsigned int index, unsigned char *b,
                    unsigned int len)
{
    uint32_t ms; int r;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
    r = usb_ftdi_ctrl(&s_ftdi, type, req, value, index, b, len);
    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
    return r;
}
static void np_bytes(const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) { char t[4]; t[0] = "0123456789ABCDEF"[b[i] >> 4]; t[1] = "0123456789ABCDEF"[b[i] & 15u]; t[2] = ' '; t[3] = 0; np(t); }
}
static void net_ftdi(int argc, char **argv)
{
    unsigned char b[64]; char t[160]; int r;
    if (!s_ftdi.open) { np("ftdi: error no device\n"); return; }
    if (argc >= 3 && !ci_cmp(argv[1], "mode")) {
        s_ftdi_want_mpsse = !ci_cmp(argv[2], "mpsse");
        ini_set("FTDI", s_ftdi_want_mpsse ? "MPSSE" : "SERIAL"); log_save();
        midi_service();                          /* the change made now (midi_service applies it) */
        np(s_ftdi.mpsse == s_ftdi_want_mpsse ? "ftdi: ok\n" : "ftdi: error mode not changed\n");
        return;
    }
    if (argc >= 4 && !ci_cmp(argv[1], "ctl")) {
        unsigned int type = (unsigned int)strtoul(argv[2], 0, 16), req = 0u, value, index, len = 0u;
        char *c = strchr(argv[2], ':');
        if (!c) { np("ftdi: error usage\n"); return; }
        req = (unsigned int)strtoul(c + 1, 0, 16);
        value = (unsigned int)strtoul(argv[3], 0, 16);
        index = (argc > 4 && strcmp(argv[4], "-")) ? (unsigned int)strtoul(argv[4], 0, 16) : USB_FTDI_PORT;
        if (argc > 5) len = (unsigned int)strtoul(argv[5], 0, 10);
        if (len > sizeof b) len = sizeof b;
        r = ftdi_ctl(type, req, value, index, b, len);
        if (r < 0) { np("ftdi: error transfer\n"); return; }
        np("ftdi: ok "); np_bytes(b, r); np("\n");
        return;
    }
    if (argc >= 2 && !ci_cmp(argv[1], "ee")) {
        unsigned int a;
        if (argc >= 5 && !ci_cmp(argv[2], "write")) {
            a = (unsigned int)strtoul(argv[3], 0, 16);
            r = ftdi_ctl(0x40u, 0x91u, (unsigned int)strtoul(argv[4], 0, 16), a, b, 0u);
            np(r < 0 ? "ftdi: error transfer\n" : "ftdi: ok\n");
            return;
        }
        if (argc >= 3 && !ci_cmp(argv[2], "erase")) {
            r = ftdi_ctl(0x40u, 0x92u, 0u, 0u, b, 0u);
            np(r < 0 ? "ftdi: error transfer\n" : "ftdi: ok\n");
            return;
        }
        np("ftdi: ok");
        for (a = 0u; a < 256u; a++) {            /* 93C66: 256 words; a 93C46/56 repeats its 64/128 words */
            if (!(a & 15u)) np("\n");
            r = ftdi_ctl(0xC0u, 0x90u, 0u, a, b, 2u);
            if (r != 2) { np("\nftdi: error transfer\n"); return; }
            snprintf(t, sizeof t, "%02X%02X ", b[1], b[0]); np(t);
        }
        np("\n");
        return;
    }
    snprintf(t, sizeof t, "ftdi: ok port %u vid %04X pid %04X index %u iface %u mode %s packets %u",
             s_ftdi.dev.jack, s_ftdi.dev.vid, s_ftdi.dev.pid, s_ftdi.index, s_ftdi.iface,
             s_ftdi.mpsse ? "mpsse" : "serial", s_ftdi.mps_in);
    np(t);
    r = ftdi_ctl(0xC0u, 0x0Au, 0u, USB_FTDI_PORT, b, 1u);
    if (r == 1) { snprintf(t, sizeof t, " latency %u", b[0]); np(t); }
    r = ftdi_ctl(0xC0u, 0x0Cu, 0u, USB_FTDI_PORT, b, 1u);
    if (r == 1) { snprintf(t, sizeof t, " pins %02X", b[0]); np(t); }
    snprintf(t, sizeof t, " in %u out %u errors %u/%u dropped %u\n", s_ftdi.rx_bytes, s_ftdi.tx_bytes, s_ftdi.in_errs,
             s_ftdi.out_errs, s_ftdi.drops);
    np(t);
}
/* a command written into a hard disk's command block, run with its output captured (from the main loop and every wait) */
static void cmd_service(void)
{
    static unsigned int busy;
    unsigned int h;
    if (busy) return;
    for (h = 0; h < HDS; h++) if (s_cmd_new[h]) {
        char line[504];
        busy = 1u; s_cmd_new[h] = 0u;
        strcpy(line, s_cmd_line[h]);
        log_put("command block: "); log_put(line); log_put("\n");
        s_np_cap = s_cmd_out[h]; s_np_n = 0u; s_np_max = CMD_OUT;
        net_exec(line);
        s_cmd_len[h] = s_np_n; s_np_cap = 0;
        s_cmd_page[h] = 0u; s_cmd_state[h] = 2u;
        busy = 0u;
    }
}
static void net_exec(char *line)
{
    char *argv[8]; int argc = 0; char path[300]; unsigned int hd;
    for (hd = 0; hd < HDS; hd++) {
        if (s_hd_dirty[hd]) hd_flush(hd);           /* the stick's files as the drives see them */
        if (s_afs_held[hd]) s_afs_held_ms[hd] = get_millis();
    }
    while (*line && argc < 8) {
        while (*line == ' ' || *line == '\t') *line++ = 0;
        if (!*line) break;
        if (*line == '"') { argv[argc++] = ++line; while (*line && *line != '"') line++; if (*line) *line++ = 0; }
        else { argv[argc++] = line; while (*line && *line != ' ' && *line != '\t') line++; }
    }
    if (!argc) return;
    if (!ci_cmp(argv[0], "help")) {
        np("status                 the machine, the drives, counters and the core's registers\n"
           "log                    the console log (BOOT.LOG)\n"
           "ls                     the files in \\amiga\n"
           "get <file>             a file of \\amiga to the session (CRC32 trailer)\n"
           "put <file> <bytes>     the session to a file of \\amiga (CRC32 trailer)\n"
           "rm <file>              removes a file of \\amiga\n"
           "peek <addr> [words]    32-bit words read from the SoC's address space\n"
           "reset                  resets the Amiga (a cold start)\n"
           "golden                 closes the drives and reconfigures to the golden image\n"
           "afs <verb> ...         files inside the Amiga volumes of DF0-DF3 and HD0-HD1 (afs alone: the verbs)\n"
           "date [zone]            the time of day (NTP); a zone (-5, +5:30) is set and written to AMIGA.INI TZ=\n"
           "amiga                  what PANOFS on the Amiga reports: memory, exec version, the volume's state\n"
           "reload                 AMIGA.INI read again, its Kickstart and drives loaded, a cold start\n"
           "xsurf                  the X-Surf 100 board: its address, frames sent and received (AGA fabric)\n"
#ifdef AMIGA_SOCK
           "sock                   the Amiga's sockets (bsdsocket.library): requests, sockets open, bytes\n"
#endif
           "df [<n> <file>|<n> eject]  the floppy drives; an image of \\amiga inserted into DF<n>, or ejected\n"
           "adf new <file> [<name> [hd]]  a blank floppy image in \\amiga, formatted (FFS) when named\n"
           "key <text>             keys typed into the active window ({amiga+v} {amiga+b} answer a requester)\n"
           "snap [n]               n consecutive pictures (16 at most) as captured, to \\amiga\\SNAP.RAW\n"
           "vid [hofs <n>]         the capture window: first sample of a 15 kHz line after hblank (AMIGA.INI HOFS=)\n"
           "midi [max <bytes>] [reset]  USB-MIDI: bytes of event packets in one OUT transfer (4-64); reset releases the\n"
           "                       device, enumerated again (a port reset) at the next scan\n"
           "reboot                 reconfigure from the main slot (0x380000): the loader, the firmware in flash and\n"
           "                       \\amiga\\AMIGA.FW start again, as at power-up with USB and the codec still powered\n"
           "midi baud <n>          the FTDI serial adapter at n bit/s instead of Paula's SERPER rate (0: SERPER's)\n"
           "midi trace on|off|save  record the MIDI path (OUT and IN transfers, Paula's bytes) to \\amiga\\MIDITRC.BIN\n"
           "ftdi                   the FTDI chip: port, IDs, channel, mode, packet size, latency, pins\n"
           "ftdi mode serial|mpsse  the channel's mode, written to AMIGA.INI FTDI=\n"
           "ftdi ctl <tt:rr> <value> <index|-> [<len>]  one control transfer (tt 40 vendor OUT, C0 vendor IN of len\n"
           "                       bytes; - the channel's port); prints `ftdi: ok' and the bytes read, or `ftdi: error'\n"
           "ftdi ee [read|write <addr> <word>|erase]  the configuration EEPROM: 256 words read (hex), one word\n"
           "                       written, or all erased\n");
    } else if (!ci_cmp(argv[0], "status")) net_status();
    else if (!ci_cmp(argv[0], "log")) net_log();
    else if (!ci_cmp(argv[0], "midi")) {
        int i; char b[200]; uint32_t ms;
        for (i = 1; i < argc; i++) {
            if (!ci_cmp(argv[i], "reset")) { if (s_midi.open) midi_detach("reset by the console"); }
            else if (!ci_cmp(argv[i], "baud") && i + 1 < argc) {      /* the FTDI adapter's rate; 0: SERPER's again */
                s_ftdi_force = (uint32_t)atoi(argv[++i]); s_ftdi_serper = 0xFFFFFFFFu;
                if (s_ftdi_force && s_ftdi.open) {
                    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
                    (void)usb_ftdi_baud(&s_ftdi, s_ftdi_force);
                    __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
                }
            }
            else if (!ci_cmp(argv[i], "trace") && i + 1 < argc) {
                i++;
                if (!ci_cmp(argv[i], "on")) { s_mtrc_on = 0u; s_mtrc_n = 0u; s_mtrc_lost = 0u; usb_midi_trace = mtrc_rec; s_mtrc_on = 1u; }
                else if (!ci_cmp(argv[i], "off")) s_mtrc_on = 0u;
                else if (!ci_cmp(argv[i], "save")) {
                    FIL f; UINT w; FRESULT fr;
                    s_mtrc_on = 0u;
                    fr = f_open(&f, "/amiga/MIDITRC.BIN", FA_WRITE | FA_CREATE_ALWAYS);
                    if (fr == FR_OK) { fr = f_write(&f, s_mtrc, s_mtrc_n, &w); f_close(&f); }
                    np(fr == FR_OK && w == s_mtrc_n ? "midi trace: saved to \\amiga\\MIDITRC.BIN\n" : "midi trace: not saved\n");
                }
            }
            else if (!ci_cmp(argv[i], "max") && i + 1 < argc) {
                unsigned int v = (unsigned int)atoi(argv[++i]);
                __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
                if (v >= 4u && v <= USB_MIDI_OUT_MAX) s_midi_out_max = v & ~3u;
                __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
            }
        }
        snprintf(b, sizeof b, "midi: max %u bytes an OUT transfer; open %u, queued %u, busy %u, IN armed %u\n",
                 s_midi_out_max, s_midi.open, s_midi.out_n, s_midi.out_busy, s_midi.in_armed);
        np(b);
        snprintf(b, sizeof b, "serial (FTDI): open %u, %u bit/s, rx %u tx %u bytes, in errors %u, out errors %u, dropped %u\n",
                 s_ftdi.open, s_ftdi.baud, s_ftdi.rx_bytes, s_ftdi.tx_bytes, s_ftdi.in_errs, s_ftdi.out_errs, s_ftdi.drops);
        np(b);
        snprintf(b, sizeof b, "midi trace: %s, %lu of %u bytes, %lu records not taken (full)\n", s_mtrc_on ? "on" : "off",
                 (unsigned long)s_mtrc_n, MTRC_BYTES, (unsigned long)s_mtrc_lost);
        np(b);
    }
    else if (!ci_cmp(argv[0], "ftdi")) net_ftdi(argc, argv);
    else if (!ci_cmp(argv[0], "snap")) net_snap(argc > 1 ? (unsigned int)atoi(argv[1]) : 1u);
    else if (!ci_cmp(argv[0], "vid")) {         /* the capture window of 15 kHz lines: show, or `vid hofs <n>' */
        char b[96];
        if (argc > 2 && !ci_cmp(argv[1], "hofs")) {
            int n = atoi(argv[2]);
            if (n >= 0 && n <= 400) { s_hofs = (unsigned int)n; AMI_VID = (s_hofs << 16) | VID_W; }
            else np("usage: vid [hofs <0..400>]\n");
        }
        snprintf(b, sizeof b, "vid: hofs %u (15 kHz lines), %u (31 kHz lines), width %u samples\n",
                 (unsigned int)(AMI_VID >> 16) & 0x7FFu, (unsigned int)AMI_VID_FAST, (unsigned int)AMI_VID & 0x7FFu);
        np(b);
    }
    else if (!ci_cmp(argv[0], "ls")) net_ls();
    else if (!ci_cmp(argv[0], "get")) {
        if (argc < 2 || !net_path(argv[1], path, sizeof path)) np("usage: get <file of \\amiga>\n"); else net_get(path);
    } else if (!ci_cmp(argv[0], "put")) {
        uint64_t n;
        if (argc < 3 || !net_path(argv[1], path, sizeof path) || !net_u64(argv[2], &n)) np("usage: put <file of \\amiga> <bytes>\n");
        else { net_put(path, n); log_put("network: put "); log_put(path); log_put("\n"); }
    } else if (!ci_cmp(argv[0], "rm")) {
        if (argc < 2 || !net_path(argv[1], path, sizeof path)) np("usage: rm <file of \\amiga>\n");
        else if (net_busy(path)) { np("rm: "); np(path); np(" is in a drive\n"); }
        else { FRESULT fr = f_unlink(path); np("rm: "); np(path); np(fr == FR_OK ? " removed\n" : " not removed\n"); }
    } else if (!ci_cmp(argv[0], "peek")) {
        int ok, ok2 = 1; uint32_t a, n = 1, i;
        a = argc > 1 ? net_hex(argv[1], &ok) : (ok = 0);
        if (argc > 2) n = net_hex(argv[2], &ok2);
        if (!ok || !ok2 || (a & 3u) || n == 0u || n > 256u) np("usage: peek <hex address, word aligned> [hex count <= 0x100]\n");
        else for (i = 0; i < n; i++) {
            if (!(i & 3u)) { if (i) np("\n"); nphex(a + 4u * i); np(":"); }
            np(" "); nphex(*(volatile uint32_t *)(uintptr_t)(a + 4u * i));
        }
        if (ok && ok2) np("\n");
    } else if (!ci_cmp(argv[0], "reset")) { np("reset: cold start\n"); log_put("network: reset\n"); ami_video_setup(); cold_start(); }
    else if (!ci_cmp(argv[0], "afs")) net_afs(argc, argv);
    else if (!ci_cmp(argv[0], "date")) net_date(argc, argv);
    else if (!ci_cmp(argv[0], "amiga")) net_amiga();
    else if (!ci_cmp(argv[0], "reload")) net_reload();
    else if (!ci_cmp(argv[0], "df")) net_df(argc, argv);
    else if (!ci_cmp(argv[0], "adf")) net_adf(argc, argv);
    else if (!ci_cmp(argv[0], "key")) net_key(argc, argv);
#ifdef AMIGA_SOCK
    else if (!ci_cmp(argv[0], "sock")) {
        uint32_t q, o, rx, tx; char b[240];
        sk_counts(&q, &o, &rx, &tx);
        snprintf(b, sizeof b, "sock: %lu requests, %lu sockets open, %lu bytes received, %lu sent\n",
                 (unsigned long)q, (unsigned long)o, (unsigned long)rx, (unsigned long)tx);
        np(b);
        { unsigned i; char l[220]; for (i = 0; i < SK_N; i++) if (sk_line(i, l, sizeof l)) np(l); }
        snprintf(b, sizeof b, "sock: MAC frames %lu, dropped (ring full) %lu; longest gap between stack services %lu us\n",
                 (unsigned long)ethmac_stat(ETHMAC_ST_RX_OK), (unsigned long)ethmac_stat(ETHMAC_ST_RX_SPACE),
                 (unsigned long)(s_net_gap / 50u));
        np(b); s_net_gap = 0u;
        snprintf(b, sizeof b, "sock: longest sector waits: read %lu us, write %lu us\n", (unsigned long)(s_go_max / 50u), (unsigned long)(s_wr_max / 50u));
        np(b); s_go_max = s_wr_max = 0u;
        snprintf(b, sizeof b, "sock: longest main-loop parts: sectors %lu us, input %lu us, network %lu us, X-Surf %lu us, rest %lu us\n",
                 (unsigned long)(s_seg[0] / 50u), (unsigned long)(s_seg[1] / 50u), (unsigned long)(s_seg[2] / 50u),
                 (unsigned long)(s_seg[3] / 50u), (unsigned long)(s_seg[4] / 50u));
        np(b); memset(s_seg, 0, sizeof s_seg);
        snprintf(b, sizeof b, "sock: longest network parts: drives and flash %lu us, sockets %lu us, lwIP %lu us\n",
                 (unsigned long)(s_ns[0] / 50u), (unsigned long)(s_ns[1] / 50u), (unsigned long)(s_ns[2] / 50u));
        np(b); memset(s_ns, 0, sizeof s_ns);
        snprintf(b, sizeof b, "sock: time since last read, ms: sectors %lu, input %lu, network %lu (drives %lu, sockets %lu, lwIP %lu in %lu calls), X-Surf %lu, rest %lu\n",
                 (unsigned long)(s_ssum[0] / 50000u), (unsigned long)(s_ssum[1] / 50000u), (unsigned long)(s_ssum[2] / 50000u),
                 (unsigned long)(s_nsum[0] / 50000u), (unsigned long)(s_nsum[1] / 50000u), (unsigned long)(s_nsum[2] / 50000u),
                 (unsigned long)s_ncall, (unsigned long)(s_ssum[3] / 50000u), (unsigned long)(s_ssum[4] / 50000u));
        np(b); memset(s_nsum, 0, sizeof s_nsum); memset(s_ssum, 0, sizeof s_ssum); s_ncall = 0u;
    }
#endif
    else if (!ci_cmp(argv[0], "xsurf")) {
        struct xsurf_stats x; char b[120];
        if (!xsurf_info(&x)) np("xsurf: no X-Surf board in this fabric\n");
        else {
            snprintf(b, sizeof b, "xsurf: address %02x:%02x:%02x:%02x:%02x:%02x, heartbeat %lu, flags 0x%04lx, status 0x%04lx\n",
                     x.mac[0], x.mac[1], x.mac[2], x.mac[3], x.mac[4], x.mac[5], (unsigned long)x.heartbeat,
                     (unsigned long)x.flags, (unsigned long)x.status); np(b);
            snprintf(b, sizeof b, "xsurf: sent %lu (failed %lu), received %lu (dropped %lu)\n", (unsigned long)x.tx,
                     (unsigned long)x.tx_err, (unsigned long)x.rx, (unsigned long)x.rx_drop); np(b);
        }
    }
    else if (!ci_cmp(argv[0], "golden")) {
        unsigned int i;
        np("golden: closing the drives and reconfiguring to the golden image\n"); netsh_flush(); netsh_drain(1000u);
        for (i = 0; i < DRIVES; i++) if (s_df_open[i]) { (void)f_sync(&s_df_f[i]); f_close(&s_df_f[i]); s_df_open[i] = 0u; }
        for (i = 0; i < HDS; i++) hd_close(i);
        log_put("network: golden\n"); log_save(); wait_us(200000u);
        iprog_exit_now(); for (;;) g2_wdt_kick();
    } else if (!ci_cmp(argv[0], "reboot")) {             /* the main slot again: loader, flash firmware, AMIGA.FW */
        unsigned int i;
        np("reboot: closing the drives and reconfiguring from the main slot\n"); netsh_flush(); netsh_drain(1000u);
        for (i = 0; i < DRIVES; i++) if (s_df_open[i]) { (void)f_sync(&s_df_f[i]); f_close(&s_df_f[i]); s_df_open[i] = 0u; }
        for (i = 0; i < HDS; i++) hd_close(i);
        log_put("network: reboot\n"); log_save(); wait_us(200000u);
        (void)g2_iprog(AMIGA_FLASH_FABRIC); for (;;) g2_wdt_kick();
    } else { np(argv[0]); np(": unknown command (help)\n"); }
}
/* from the main loop and every wait: the stack, a new session's greeting, one input line at a time */
static void net_service(void)
{
    int c;
#ifdef AMIGA_SOCK
    uint32_t t0 = cyc_now(), t1;
#endif
    afs_poll();
    cmd_service();
#ifdef AMIGA_PFLASH
    if (pfl_service()) { log_put(pfl_message()); log_put("\n"); }
#endif
#ifdef AMIGA_SOCK
    t1 = cyc_now(); if (t1 - t0 > s_ns[0]) s_ns[0] = t1 - t0; s_nsum[0] += t1 - t0; t0 = t1;
#endif
    if (!s_net_up) return;
#ifdef AMIGA_SOCK
    { uint32_t c = cyc_now(); if (s_net_last && c - s_net_last > s_net_gap) s_net_gap = c - s_net_last; s_net_last = c; }
    sk_poll();
    t1 = cyc_now(); if (t1 - t0 > s_ns[1]) s_ns[1] = t1 - t0; s_nsum[1] += t1 - t0; t0 = t1;
#endif
    c = netsh_poll();
#ifdef AMIGA_SOCK
    t1 = cyc_now(); if (t1 - t0 > s_ns[2]) s_ns[2] = t1 - t0; s_nsum[2] += t1 - t0; s_ncall++; t0 = t1;
#endif
    if (c) {
        netsh_info_t i; netsh_info(&i);
        log_put("network: address "); { char b[16]; uint32_t ip = i.ip; int k; for (k = 0; k < 4; k++) { int j = 15; uint32_t v = (ip >> (8 * k)) & 255u; b[j] = 0; do { b[--j] = (char)('0' + v % 10u); v /= 10u; } while (v); log_put(&b[j]); if (k < 3) log_put("."); } }
        log_put(", TCP 2323\n");
    }
    if (netsh_session() && netsh_gen() != s_net_gen) {
        s_net_gen = netsh_gen(); s_nl_n = 0u;
        np("G2 Amiga firmware "); nphex(FW_ID); np(" (help)\n> ");
    } else if (!netsh_session()) s_net_gen = netsh_gen();
    while ((c = netsh_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (!s_nl_n && c == '\n') continue;
            s_nl[s_nl_n] = 0; np(s_nl); np("\n");
            net_exec(s_nl);
            s_nl_n = 0u;
            np("> ");
            netsh_flush();
            break;
        }
        if (s_nl_n + 1u < sizeof s_nl) s_nl[s_nl_n++] = (char)c;
    }
}

/* ---- the setup screen ----------------------------------------------------------------------------------------------
 * setup.c draws it, setup_items.h holds its pages and items; this part fills in the values and carries out a change.
 * The machine's configuration (profile, CPU, chipset, memory, Kickstart, region, disk writes) is changed in the
 * variables only and compared with s_saved, taken when setup opens: Save and reset writes the keys that differ to
 * AMIGA.INI and starts the machine cold, Discard gives them back.  Drives, zone, FTDI mode and the picture keys are
 * written at once, as before. */

/* a key for the setup screen: a new press; an arrow held, again after 400 ms and then every 60 ms; or 0 after 1 s
 * without one, so the page is drawn again (the clock, the devices) */
static unsigned int menu_key(void)
{
    static uint32_t held, t_rep;
    uint32_t prev = input_any(), now, t0 = get_millis();
    for (;;) {
        g2_wdt_kick();
        input_poll();
        now = input_any();
        if (now & ~prev) {
            held = now & ~prev & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT); t_rep = get_millis() + 400u;
            return now & ~prev;
        }
        held &= now;
        if (held && (int32_t)(get_millis() - t_rep) >= 0) { t_rep = get_millis() + 60u; return held; }
        prev = now;
        if (pads_service()) return 0u;
        net_service();
        if (get_millis() - t0 >= 1000u) return 0u;
        wait_us(5000u);
    }
}

/* the console log (s_log, what BOOT.LOG holds) on the heads: the newest page first; Up/Down a line, Left/Right a page */
static void menu_log(void)
{
    static uint32_t st[1024];
    unsigned int nl = 0u, top, rows, i, k, W, H;
    uint32_t p;
    for (p = 0u; p < s_log_n && nl < 1024u; ) {
        st[nl++] = p;
        while (p < s_log_n && s_log[p] != '\n') p++;
        p++;
    }
    H = vgacon_rows(); rows = H > 3u ? H - 3u : 1u;
    top = nl > rows ? nl - rows : 0u;
    for (;;) {
        W = vgacon_cols();
        vgacon_frame_begin();
        vgacon_color(VC_GREY, VC_BLACK); vgacon_clear(); vgacon_header("Amiga setup: boot log", "BOOT.LOG");
        for (i = 0; i < rows; i++) {
            char ln[VC_MAXCOLS + 1]; unsigned int j = 0u;
            if (top + i < nl) {
                uint32_t q = st[top + i];
                while (q < s_log_n && s_log[q] != '\n' && s_log[q] != '\r' && j < W) {
                    char c = s_log[q++]; ln[j++] = (c < ' ' || c > '~') ? ' ' : c;
                }
            }
            ln[j] = 0; vgacon_text(0u, 2u + i, W, ln);
        }
        vgacon_color(VC_BLACK, VC_GREY);
        vgacon_text(0u, H - 1u, W, W >= 64u ? " \x18\x19 Line   \x1B\x1A Page   Esc Back" : " \x18\x19 Line  \x1B\x1A Page  Esc Back");
        vgacon_frame_end();
        k = menu_key();
        if (k & IN_UP) { if (top) top--; }
        else if (k & IN_DOWN) { if (top + rows < nl) top++; }
        else if (k & IN_LEFT) top = top > rows ? top - rows : 0u;
        else if (k & IN_RIGHT) top = top + 2u * rows < nl ? top + rows : (nl > rows ? nl - rows : 0u);
        else if (k & (IN_BACK | IN_OK | IN_MENU)) return;
    }
}

static const char *const s_chipset_nm[] = { "OCS (A500)", "OCS (A1000)", "ECS", "AGA" };
static const char *const s_chip_nm[] = { "512 KB", "1 MB", "1.5 MB", "2 MB" };
static const char *const s_slow_nm[] = { "None", "512 KB", "1 MB", "1.5 MB" };
static const char *const s_fast_nm[] = { "None", "2 MB", "4 MB", "8 MB" };
static const char *const s_write_nm[] = { "Write protected", "Enabled" };
static const char *const s_region_nm[] = { "NTSC 60 Hz", "PAL 50 Hz" };
static const char *const s_video_nm[] = { "Scaled 640x480 31 kHz", "Native 15 kHz" };
static const char *const s_screen_nm[] = { "micro-HDMI", "DVI-I", "Both" };
static const char *const s_ftdi_nm[] = { "Serial port", "MPSSE (JTAG, SPI, SWD)" };
static const unsigned char s_ntpw[] = { 0u, 5u, 10u, 15u, 20u, 30u, 45u, 60u, 90u, 120u };
#define NNTPW (sizeof s_ntpw / sizeof s_ntpw[0])

static struct { unsigned int chip, slow, chipset, cpu, fast, pal, write; char kick[256]; } s_saved;
static unsigned int s_hd_chg;                     /* a hard disk changed: the machine starts cold when setup is left */
static void setup_snapshot(void)
{
    s_saved.chip = s_chip; s_saved.slow = s_slow; s_saved.chipset = s_chipset; s_saved.cpu = s_cpu; s_saved.fast = s_fast;
    s_saved.pal = s_pal; s_saved.write = s_write; strcpy(s_saved.kick, s_kick_name);
}
static void setup_restore(void)
{
    s_chip = s_saved.chip; s_slow = s_saved.slow; s_chipset = s_saved.chipset; s_cpu = s_saved.cpu; s_fast = s_saved.fast;
    s_pal = s_saved.pal; s_write = s_saved.write; strcpy(s_kick_name, s_saved.kick);
}
/* the items whose value differs from s_saved, as "CPU, Fast RAM" in b; their number */
static unsigned int setup_pending(char *b, size_t n)
{
    const char *nm[8]; unsigned int k = 0u, i;
    if (s_cpu != s_saved.cpu) nm[k++] = "CPU";
    if (s_chipset != s_saved.chipset) nm[k++] = "Chipset";
    if (s_chip != s_saved.chip) nm[k++] = "Chip RAM";
    if (s_slow != s_saved.slow) nm[k++] = "Slow RAM";
    if (s_fast != s_saved.fast) nm[k++] = "Fast RAM";
    if (strcmp(s_kick_name, s_saved.kick)) nm[k++] = "Kickstart";
    if (s_pal != s_saved.pal) nm[k++] = "Region";
    if (s_write != s_saved.write) nm[k++] = "Disk writes";
    if (b && n) {
        b[0] = 0;
        for (i = 0; i < k; i++) { if (i) strncat(b, ", ", n - strlen(b) - 1u); strncat(b, nm[i], n - strlen(b) - 1u); }
    }
    return k;
}

static void setup_value(unsigned int id, su_item_t *t)
{
    char *v = t->value; size_t n = sizeof t->value;
    v[0] = 0;
    switch (id) {
    case IT_PROFILE: { unsigned int c = prof_now(); snprintf(v, n, "%s", c < NPROF ? s_prof[c].tag : "Custom");
                       t->changed = s_chip != s_saved.chip || s_slow != s_saved.slow || s_chipset != s_saved.chipset ||
                                    s_cpu != s_saved.cpu || s_fast != s_saved.fast; break; }
    case IT_CPU:     snprintf(v, n, "%s", s_cpu_name[s_cpu]); t->changed = s_cpu != s_saved.cpu; break;
    case IT_CHIPSET: snprintf(v, n, "%s", s_chipset_nm[s_chipset & 3u]); t->changed = s_chipset != s_saved.chipset; break;
    case IT_CHIP:    snprintf(v, n, "%s", s_chip_nm[s_chip & 3u]); t->changed = s_chip != s_saved.chip; break;
    case IT_SLOW:    snprintf(v, n, "%s", s_slow_nm[s_slow & 3u]); t->changed = s_slow != s_saved.slow; break;
    case IT_FAST:    snprintf(v, n, "%s", s_fast_nm[s_fast & 3u]); t->changed = s_fast != s_saved.fast; break;
    case IT_KICK:    snprintf(v, n, "%.63s", s_kick_name[0] ? s_kick_name : "None"); t->changed = strcmp(s_kick_name, s_saved.kick) != 0; break;
    case IT_DF0: case IT_DF1: case IT_DF2: case IT_DF3: {
        unsigned int d = id - IT_DF0; snprintf(v, n, "%.63s", s_df_open[d] ? s_df_name[d] : "Empty"); break; }
    case IT_WRITE:   snprintf(v, n, "%s", s_write_nm[s_write & 1u]); t->changed = s_write != s_saved.write; break;
    case IT_HD0: case IT_HD1: {
        unsigned int h = id - IT_HD0; snprintf(v, n, "%.63s", s_hd_open[h] ? s_hd_name[h] : "None"); break; }
    case IT_REGION:  snprintf(v, n, "%s", s_region_nm[s_pal & 1u]); t->changed = s_pal != s_saved.pal; break;
    case IT_VIDEO:   snprintf(v, n, "%s", s_video_nm[s_native & 1u]); break;
    case IT_SCREEN:  snprintf(v, n, "%s", s_screen_nm[s_screen < 3u ? s_screen : 2u]); break;
    case IT_HOFS:    snprintf(v, n, "%u samples", s_hofs); break;
    case IT_FTDI:    snprintf(v, n, "%s", s_ftdi_nm[s_ftdi_want_mpsse & 1u]); break;
    case IT_FTDI_DEV: if (s_ftdi.open) snprintf(v, n, "open, hub port %u", (unsigned int)s_ftdi.dev.jack); else snprintf(v, n, "none"); break;
    case IT_MIDI:    if (s_midi.open) snprintf(v, n, "open, hub port %u", (unsigned int)s_midi.dev.jack); else snprintf(v, n, "none"); break;
    case IT_KBD:     if (s_have_kbd) snprintf(v, n, "hub port %u", (unsigned int)s_kbd_h.dev.jack); else snprintf(v, n, "none"); break;
    case IT_MOUSE:   if (s_have_mouse) snprintf(v, n, "hub port %u", (unsigned int)s_mouse_h.dev.jack); else snprintf(v, n, "none"); break;
    case IT_PAD1: case IT_PAD2: {
        unsigned int i = id - IT_PAD1;
        if (s_pad[i].armed) snprintf(v, n, "hub port %u, game port %u", (unsigned int)s_pad[i].dev.jack, i ? 1u : 2u);
        else snprintf(v, n, "none");
        break; }
#ifdef AMIGA_SHARE
    case IT_SHARE:   snprintf(v, n, "%.63s", s_share_ini[0] ? s_share_ini : "/amiga/share"); break;
#endif
    case IT_TZ:      { char z[8]; tz_str(ntptime_tz_min, z, sizeof z); snprintf(v, n, "UTC%s", z); break; }
    case IT_NTPWAIT: snprintf(v, n, "%u s", s_ntpwait); break;
    case IT_CLOCK:
        if (ntptime_known()) {
            unsigned Y, M, D, h, m, sc;
            ntptime_civil(ntptime_unix() + (uint32_t)(ntptime_tz_min * 60), &Y, &M, &D, &h, &m, &sc);
            snprintf(v, n, "%04u-%02u-%02u %02u:%02u:%02u", Y, M, D, h, m, sc);
        } else snprintf(v, n, "no network time");
        break;
    case IT_LINK: case IT_IP: case IT_CONSOLE: {
        netsh_info_t i; netsh_info(&i);
        if (id == IT_LINK) snprintf(v, n, "%s", i.phy < 0 ? "no PHY" : i.link ? "up" : "down");
        else if (id == IT_IP) {
            if (i.bound) { const uint8_t *a = (const uint8_t *)&i.ip; snprintf(v, n, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]); }
            else snprintf(v, n, "%s", i.dhcp ? "DHCP, waiting" : "none");
        } else snprintf(v, n, "TCP 2323%s", i.session ? ", connected" : "");
        break; }
    case IT_SAVE:    (void)setup_pending(v, n); if (!v[0] && s_hd_chg) snprintf(v, n, "hard disk"); break;
    case IT_DISCARD: t->dim = !setup_pending(0, 0); break;
    case IT_FWID:    snprintf(v, n, "%08lx", (unsigned long)FW_ID); break;
    case IT_FABRIC:  snprintf(v, n, "%08lx", (unsigned long)SOC_BUILD_ID); break;
    default: break;
    }
}

#define SU_MAXIT 24u
static su_item_t s_su_it[SU_MAXIT];
static unsigned char s_su_id[SU_MAXIT];
static unsigned int setup_build(unsigned int pg)
{
    unsigned int i, n = 0u;
    for (i = 0; i < NSU_DEF && n < SU_MAXIT; i++) {
        su_item_t *t;
        if (s_su_def[i].page != pg) continue;
        t = &s_su_it[n]; s_su_id[n] = s_su_def[i].id; n++;
        t->kind = s_su_def[i].kind; t->dim = 0u; t->changed = 0u;
        t->label = s_su_def[i].label; t->help = s_su_def[i].help; t->key = s_su_def[i].key;
        setup_value(s_su_def[i].id, t);
    }
    return n;
}

/* the changed configuration to AMIGA.INI, a changed Kickstart loaded first; 0 done, -1 the Kickstart did not load
 * (the saved one is loaded again and nothing is written) */
static int setup_save(void)
{
    char v[4];
    if (strcmp(s_kick_name, s_saved.kick)) {
        if (kick_load() != 0) {
            char m[600];
            snprintf(m, sizeof m, "%s did not load; the reason is in the boot log.  The Kickstart stays %s.", s_kick_name, s_saved.kick);
            strcpy(s_kick_name, s_saved.kick); (void)kick_load(); log_save();
            su_message("Kickstart", m, menu_key);
            return -1;
        }
        ini_set("KICK", s_kick_name);
    }
    v[1] = 0;
    if (s_chip != s_saved.chip)       { v[0] = (char)('0' + s_chip);    ini_set("CHIP", v); }
    if (s_slow != s_saved.slow)       { v[0] = (char)('0' + s_slow);    ini_set("SLOW", v); }
    if (s_chipset != s_saved.chipset) { v[0] = (char)('0' + s_chipset); ini_set("CHIPSET", v); }
    if (s_fast != s_saved.fast)       { v[0] = (char)('0' + s_fast);    ini_set("FAST", v); }
    if (s_cpu != s_saved.cpu)         ini_set("CPU", s_cpu_name[s_cpu]);
    if (s_pal != s_saved.pal)         ini_set("REGION", s_pal ? "PAL" : "NTSC");
    if (s_write != s_saved.write)     { v[0] = (char)('0' + s_write);   ini_set("WRITE", v); }
    { unsigned int c = prof_now(); if (c < NPROF) { log_put("machine: "); log_put(s_prof[c].name); log_put("\n"); } }
    setup_snapshot();
    log_save();
    return 0;
}

/* setup left: the picture set up again, the machine resumed or (cold, or a hard disk changed) started cold; 1 */
static int setup_exit(int cold)
{
    s_con_quiet = 0u;
    if (s_hd_chg) { cold = 1; s_hd_chg = 0u; }
    log_save();
    ami_video_setup();
    if (cold) cold_start(); else run_resume();
    return 1;
}
/* Escape, Resume: straight out when nothing changed, else the dialog; 1 when setup is left */
static int setup_leave(void)
{
    static const char *const o[] = { "Save and reset", "Discard changes", "Back to setup" };
    if (!setup_pending(0, 0) && !s_hd_chg) return setup_exit(0);
    switch (su_list("Settings changed", o, 3u, 0u, menu_key)) {
    case 0:  return setup_save() == 0 ? setup_exit(1) : 0;
    case 1:  setup_restore(); return setup_exit(0);
    default: return 0;
    }
}

/* F10, Triangle: a dialog, then the changes saved and the machine started cold; with nothing changed, the cold start
 * alone (the settings written at once take effect); 1 when setup is left */
static int setup_confirm(unsigned int changed)
{
    static const char *const o[] = { "Save and reset", "Back to setup" }, *const r[] = { "Reset", "Back to setup" };
    if (su_list(changed ? "Save changes and reset" : "Nothing to save: reset", changed ? o : r, 2u, 0u, menu_key) != 0)
        return 0;
    return setup_save() == 0 ? setup_exit(1) : 0;
}

/* a choice from a fixed list; the index, or -1 */
static int setup_pick(const char *title, const char *const *o, unsigned int n, unsigned int cur) { return su_list(title, o, n, cur, menu_key); }

static void setup_df(unsigned int d)
{
    static const char *opt[MAX_FILES + 1u];
    static const char *const key[DRIVES] = { "DF0", "DF1", "DF2", "DF3" };
    unsigned int i, cur = 0u; int r; char t[24];
    if (list_ext(".adf") != FR_OK) { su_message("Floppy images", "\\amiga cannot be listed.", menu_key); return; }
    opt[0] = "(eject)";
    for (i = 0; i < s_nfiles; i++) { opt[i + 1u] = s_files[i]; if (s_df_open[d] && !strcmp(s_files[i], s_df_name[d])) cur = i + 1u; }
    snprintf(t, sizeof t, "Insert into DF%u:", d);
    r = setup_pick(t, opt, s_nfiles + 1u, cur);
    if (r < 0) return;
    if (r == 0) { df_eject(d); ini_set(key[d], ""); }
    else {
        char nm[256]; strncpy(nm, s_files[r - 1], sizeof nm - 1u); nm[sizeof nm - 1u] = 0;
        if (df_insert(d, nm) == 0) ini_set(key[d], nm);
        else { char m[600]; snprintf(m, sizeof m, "%s did not open; the reason is in the boot log.", nm); su_message(t, m, menu_key); }
    }
    log_save();
}

static void setup_hd(unsigned int h)
{
    static const char *opt[MAX_FILES + 1u + NHDC];
    static char nw[NHDC][24];
    static const char *const key[HDS] = { "HD0", "HD1" };
    const char *title = h ? "HD1: slave" : "HD0: master";
    unsigned int i, cur = 0u; int r, bad = 0; char nm[256], m[640];
    if (list_ext(".hdf") != FR_OK) { su_message("Hard-disk images", "\\amiga cannot be listed.", menu_key); return; }
    log_put("hard-disk images:"); for (i = 0; i < s_nfiles; i++) { log_put(" "); log_put(s_files[i]); }
    log_put(s_nfiles ? "\n" : " none\n");
    opt[0] = "(none)";
    for (i = 0; i < NHDC; i++) { snprintf(nw[i], sizeof nw[i], "(new image, %u MB)", s_hdc_mb[i]); opt[1u + i] = nw[i]; }
    for (i = 0; i < s_nfiles; i++) {
        opt[1u + NHDC + i] = s_files[i];
        if (s_hd_open[h] && !strcmp(s_files[i], s_hd_name[h])) cur = 1u + NHDC + i;
    }
    r = setup_pick(title, opt, 1u + NHDC + s_nfiles, cur);
    if (r < 0) return;
    hd_eject(h); s_hd_name[h][0] = 0; s_hd_chg = 1u;
    if (r == 0) { ini_set(key[h], ""); log_save(); return; }
    if ((unsigned int)r <= NHDC) {               /* a new image: hd_create's lines on the console while it is written */
        s_con_quiet = 0u;
        vgacon_color(VC_GREY, VC_BLACK); vgacon_clear(); vgacon_header("Amiga setup: new hard disk", key[h]);
        bad = hd_create(h, s_hdc_mb[r - 1], nm, sizeof nm);
        s_con_quiet = 1u;
    } else { strncpy(nm, s_files[r - 1 - (int)NHDC], sizeof nm - 1u); nm[sizeof nm - 1u] = 0; }
    if (!bad) { say("Opening "); say(nm); say("\n"); bad = hd_insert(h, nm); }
    if (!bad) {
        strcpy(s_hd_name[h], nm); ini_set(key[h], nm);
        if ((unsigned int)r <= NHDC) {
            snprintf(m, sizeof m, "%s, %u MB, is %s.  Its partition DH%u is not formatted: Workbench formats it.", nm, s_hdc_mb[r - 1], key[h], h);
            su_message("New hard disk", m, menu_key);
        }
    } else su_message(title, "The image did not open; the reason is in the boot log.", menu_key);
    log_save();
}

/* Enter on item id; 1 when setup is left */
static int setup_choose(unsigned int id)
{
    int r; char v[16];
    switch (id) {
    case IT_PROFILE: {
        const char *o[NPROF]; unsigned int i, c = prof_now(); char kp[300];
        for (i = 0; i < NPROF; i++) o[i] = s_prof[i].name;
        if ((r = setup_pick("Profile", o, NPROF, c < NPROF ? c : 0u)) < 0) break;
        strcpy(kp, "/amiga/"); strcat(kp, s_prof[r].kick);
        if (f_stat(kp, 0) != FR_OK) {           /* a profile whose Kickstart is not in \amiga is passed over */
            char m[200]; snprintf(m, sizeof m, "No \\amiga\\%s on the stick: %s is not set.", s_prof[r].kick, s_prof[r].name);
            log_put("machine: "); log_put(m); log_put("\n");
            su_message("Profile", m, menu_key); break;
        }
        s_chip = s_prof[r].chip; s_slow = s_prof[r].slow; s_chipset = s_prof[r].chipset; s_cpu = s_prof[r].cpu;
        s_fast = s_prof[r].fast; strcpy(s_kick_name, s_prof[r].kick);
        break; }
    case IT_CPU:     if ((r = setup_pick("CPU", s_cpu_name, 3u, s_cpu)) >= 0) s_cpu = (unsigned int)r; break;
    case IT_CHIPSET: if ((r = setup_pick("Chipset", s_chipset_nm, 4u, s_chipset)) >= 0) s_chipset = (unsigned int)r; break;
    case IT_CHIP:    if ((r = setup_pick("Chip RAM", s_chip_nm, 4u, s_chip)) >= 0) s_chip = (unsigned int)r; break;
    case IT_SLOW:    if ((r = setup_pick("Slow RAM", s_slow_nm, 4u, s_slow)) >= 0) s_slow = (unsigned int)r; break;
    case IT_FAST:    if ((r = setup_pick("Fast RAM", s_fast_nm, 4u, s_fast)) >= 0) s_fast = (unsigned int)r; break;
    case IT_KICK: {
        unsigned int i, cur = 0u;
        if (list_ext(".rom") != FR_OK || !s_nfiles) { su_message("Kickstart", "No *.rom image in \\amiga.", menu_key); break; }
        for (i = 0; i < s_nfiles; i++) if (!strcmp(s_files[i], s_kick_name)) cur = i;
        if ((r = setup_pick("Kickstart ROM", (const char *const *)s_files, s_nfiles, cur)) >= 0) {
            strncpy(s_kick_name, s_files[r], sizeof s_kick_name - 1u); s_kick_name[sizeof s_kick_name - 1u] = 0;
        }
        break; }
    case IT_DF0: case IT_DF1: case IT_DF2: case IT_DF3: setup_df(id - IT_DF0); break;
    case IT_WRITE:   if ((r = setup_pick("Disk writes", s_write_nm, 2u, s_write)) >= 0) s_write = (unsigned int)r; break;
    case IT_HD0: case IT_HD1: setup_hd(id - IT_HD0); break;
    case IT_REGION:  if ((r = setup_pick("Region", s_region_nm, 2u, s_pal)) >= 0) s_pal = (unsigned int)r; break;
    case IT_VIDEO:                               /* setup itself is shown in the new mode at once */
        if ((r = setup_pick("Output mode", s_video_nm, 2u, s_native)) >= 0 && (unsigned int)r != s_native) {
            s_native = (unsigned int)r; ini_set("VIDEO", s_native ? "NATIVE" : "SCALE"); log_save();
            (void)ch7301_init(s_native ? 4u : 0u); console_on();
        }
        break;
    case IT_SCREEN:
        if ((r = setup_pick("Outputs", s_screen_nm, 3u, s_screen < 3u ? s_screen : 2u)) >= 0 && (unsigned int)r != s_screen) {
            s_screen = (unsigned int)r; v[0] = (char)('0' + s_screen); v[1] = 0; ini_set("SCREEN", v); log_save();
        }
        break;
    case IT_HOFS:
        if ((r = su_number("Picture offset", "samples", (int)s_hofs, 0, 400, 10, menu_key)) >= 0 && (unsigned int)r != s_hofs) {
            s_hofs = (unsigned int)r; snprintf(v, sizeof v, "%u", s_hofs); ini_set("HOFS", v); log_save();
        }
        break;
    case IT_FTDI:                                /* the mode is set by midi_service when a chip is open */
        if ((r = setup_pick("FTDI channel A", s_ftdi_nm, 2u, s_ftdi_want_mpsse)) >= 0 && (unsigned int)r != s_ftdi_want_mpsse) {
            s_ftdi_want_mpsse = (unsigned int)r; ini_set("FTDI", s_ftdi_want_mpsse ? "MPSSE" : "SERIAL"); log_save();
        }
        break;
    case IT_TZ: {                                /* UTC-12 to UTC+14 by hours */
        static char zs[27][16]; static const char *o[27]; unsigned int i; int cur = ntptime_tz_min / 60 + 12;
        for (i = 0; i < 27u; i++) { char z[8]; tz_str(((int)i - 12) * 60, z, sizeof z); snprintf(zs[i], sizeof zs[i], "UTC%s", z); o[i] = zs[i]; }
        if ((r = setup_pick("Time zone", o, 27u, cur >= 0 && cur < 27 ? (unsigned int)cur : 12u)) >= 0) {
            char z[8]; ntptime_tz_min = (r - 12) * 60; tz_str(ntptime_tz_min, z, sizeof z);
            ini_set("TZ", z); log_save(); s_rtc_due = 1u;
        }
        break; }
    case IT_NTPWAIT: {
        static char ws[NNTPW][8]; static const char *o[NNTPW]; unsigned int i, cur = 0u;
        for (i = 0; i < NNTPW; i++) { snprintf(ws[i], sizeof ws[i], "%u s", s_ntpw[i]); o[i] = ws[i]; if (s_ntpw[i] == s_ntpwait) cur = i; }
        if ((r = setup_pick("NTP wait", o, NNTPW, cur)) >= 0 && s_ntpw[r] != s_ntpwait) {
            s_ntpwait = s_ntpw[r]; snprintf(v, sizeof v, "%u", s_ntpwait); ini_set("NTPWAIT", v); log_save();
        }
        break; }
    case IT_RESUME:  return setup_leave();
    case IT_SAVE:    return setup_confirm(setup_pending(0, 0) || s_hd_chg);
    case IT_DISCARD: setup_restore(); return setup_exit(0);
    case IT_RESET:   setup_restore(); return setup_exit(1);
    case IT_LOG:     menu_log(); break;
    case IT_GOLDEN: {
        static const char *const o[] = { "Exit to the golden image", "Back to setup" };
        unsigned int i;
        if (setup_pick("Golden image", o, 2u, 1u) != 0) break;
        for (i = 0; i < DRIVES; i++) if (s_df_open[i]) { (void)f_sync(&s_df_f[i]); f_close(&s_df_f[i]); s_df_open[i] = 0u; }
        for (i = 0; i < HDS; i++) hd_close(i);
        s_con_quiet = 0u;
        vgacon_color(VC_GREY, VC_BLACK); vgacon_clear(); say("Returning to the golden image\n"); log_save(); wait_us(200000u);
        iprog_exit_now(); for (;;) g2_wdt_kick();
    }
    default: break;
    }
    return 0;
}

/* the menu: the core stopped, the setup screen on the heads until it is left */
static void menu(void)
{
    static unsigned int sel[NPG], top[NPG];
    su_screen_t s;
    unsigned int pg = PG_MACHINE, k, i;
    char right[40], chg[80], status[160];
    run_stop();
    log_put("menu opened\n"); log_save();
    console_on();
    setup_snapshot(); s_hd_chg = 0u; s_con_quiet = 1u;
    memset(&s, 0, sizeof s);
    s.tabs = s_pg_name; s.ntabs = NPG; s.title = "Amiga setup"; s.it = s_su_it;
    for (i = 0; i < NPG; i++) { sel[i] = 0xFFu; top[i] = 0u; }
    for (;;) {
        unsigned int np_ = setup_pending(chg, sizeof chg);
        s.tab = pg; s.n = setup_build(pg);
        s.sel = sel[pg] < s.n ? sel[pg] : 0u; s.top = top[pg];
        if (s.n && !((s_su_it[s.sel].kind == SU_VALUE || s_su_it[s.sel].kind == SU_ACTION) && !s_su_it[s.sel].dim)) s.sel = su_next(&s, 1);
        if (vgacon_cols() >= 64u) snprintf(right, sizeof right, "G2 firmware %08lx", (unsigned long)FW_ID);
        else snprintf(right, sizeof right, "%s", (np_ || s_hd_chg) ? "changed" : "G2");
        s.right = right;
        if (np_) { snprintf(status, sizeof status, "Changed: %s.  F10 or Triangle saves and resets.", chg); s.status = status; }
        else if (s_hd_chg) s.status = "A hard disk changed: the machine starts cold when setup is left.";
        else s.status = 0;
        su_draw(&s);
        sel[pg] = s.sel; top[pg] = s.top;
        k = menu_key();
        if (!k) continue;
        if (k & IN_UP) sel[pg] = su_next(&s, -1);
        else if (k & IN_DOWN) sel[pg] = su_next(&s, 1);
        else if (k & IN_LEFT) pg = pg ? pg - 1u : NPG - 1u;
        else if (k & IN_RIGHT) pg = (pg + 1u) % NPG;
        else if (k & IN_SAVE) { if (setup_confirm(np_ || s_hd_chg)) return; }
        else if (k & (IN_BACK | IN_MENU)) { if (setup_leave()) return; }
        else if ((k & IN_OK) && s.n && setup_choose(s_su_id[s.sel])) return;
    }
}

static void play(void)
{
    uint32_t t_stat = get_millis(), t_log = get_millis(), t_rtc = 0u;
    unsigned int rtc_ok = 0u;
    {   /* the first start waits for NTP (NTPWAIT=), so Kickstart's battclock.resource reads a set clock */
        netsh_info_t i; netsh_info(&i);
        if (s_net_up && i.phy >= 0 && s_ntpwait && !ntptime_known()) {
            say("Network time: waiting up to "); saydec(s_ntpwait); say(" s after start-up\n");
            while (!ntptime_known() && get_millis() < s_ntpwait * 1000u) { g2_wdt_kick(); net_service(); }
            log_put(ntptime_known() ? "network time: known before the start\n" : "network time: not known, the machine starts without it\n");
        }
    }
    ami_video_setup();
    cold_start();
    if (ntptime_known()) { rtc_ok = 1u; t_rtc = get_millis(); log_put("clock: set from NTP\n"); }
    for (;;) {
        g2_wdt_kick();
#ifdef AMIGA_SOCK
        { uint32_t c = cyc_now(); if (s_seg_t) { if (c - s_seg_t > s_seg[4]) s_seg[4] = c - s_seg_t; s_ssum[4] += c - s_seg_t; } s_seg_t = c; }
#endif
        sdc_service();
#ifdef AMIGA_SOCK
        { uint32_t c = cyc_now(); if (c - s_seg_t > s_seg[0]) s_seg[0] = c - s_seg_t; s_ssum[0] += c - s_seg_t; s_seg_t = c; }
#endif
        if (s_mcu_pend && (AMI_CTRL & ACTRL_RUN)) mcu_ack();
        if (s_mcu_pend) {                            /* line masked: the frame and audio bits served from here */
            uint32_t ms;
            if (AMI_IRQ & 1u) frame_irq();
            if ((AUD_WMARK & 0x80000000u) && (AUD_STATUS & 0x1FFFu) < (AUD_WMARK & 0x1FFFu)) snes_audio_service();
            __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(ms));
            midi_pump();
            __asm__ volatile ("csrs mstatus, %0" :: "r"(ms & 8u));
        }
        input_poll();
        if (input_any() & IN_MENU) {
            while (input_any() & IN_MENU) { input_poll(); sdc_service(); g2_wdt_kick(); }
            menu();
        }
#ifdef AMIGA_SOCK
        { uint32_t c = cyc_now(); if (c - s_seg_t > s_seg[1]) s_seg[1] = c - s_seg_t; s_ssum[1] += c - s_seg_t; s_seg_t = c; }
#endif
        (void)pads_service();
        net_service();
#ifdef AMIGA_SOCK
        { uint32_t c = cyc_now(); if (c - s_seg_t > s_seg[2]) s_seg[2] = c - s_seg_t; s_ssum[2] += c - s_seg_t; s_seg_t = c; }
#endif
        xsurf_poll();
#ifdef AMIGA_SOCK
        { uint32_t c = cyc_now(); if (c - s_seg_t > s_seg[3]) s_seg[3] = c - s_seg_t; s_ssum[3] += c - s_seg_t; s_seg_t = c; }
#endif
        if (ntptime_known() && (!rtc_ok || s_rtc_due || get_millis() - t_rtc >= 60000u)) {   /* once NTP answers, then a minute */
            t_rtc = get_millis(); s_rtc_due = 0u;
            if (rtc_send() == 0 && !rtc_ok) { rtc_ok = 1u; log_put("clock: set from NTP\n"); }
        }
#ifdef AMIGA_SIM
        if (get_millis() - t_stat >= 50u) {       /* simulation: a status line every 50 ms */
#else
        if (get_millis() - t_stat >= 10000u) {    /* a status line on the UART every 10 s */
#endif
            t_stat = get_millis();
            con_puts("amiga: frames "); con_hex32(AMI_FRAMES); con_puts(" sectors "); con_hex32(s_reads);
            con_puts("/"); con_hex32(s_writes); con_puts(" held "); con_hex32(AMI_HELD); con_puts(" owed "); con_hex32(AMI_OWED);
            con_puts("\r\n");
        }
        if (get_millis() - t_log >= 60000u) {        /* BOOT.LOG: a status line a minute */
            char b[16];
            t_log = get_millis();
            #define LOGDEC(v) do { uint32_t v_ = (v); int i_ = 15; b[i_] = 0; do { b[--i_] = (char)('0' + v_ % 10u); v_ /= 10u; } while (v_ && i_); log_put(&b[i_]); } while (0)
            log_put("status: frames "); LOGDEC(AMI_FRAMES); log_put(", sectors read "); LOGDEC(s_reads);
            log_put(", written "); LOGDEC(s_writes); log_put(", chip misses "); LOGDEC(AMI_CHIP_MISS);
            log_put(", owed "); LOGDEC(AMI_OWED & 0xFFFFu); log_put(", lost "); LOGDEC(AMI_OWED >> 16); log_put("\n");
            #undef LOGDEC
            log_save();
        }
    }
}

/* ---- leaving -----------------------------------------------------------------------------------------------------
 * Before a reconfiguration from AMIGA_FLASH_EXIT the devices this firmware set up are returned to their power-on
 * state (fw/snes/snes_main.c quiesce_for_exit): the core stopped and in reset, the audio stream stopped and the
 * WM8750 reset, both heads off and both CH7301C held in reset, the USB core, USB3300 and USB2514 in reset, the fabric
 * watchdog disabled.  The flash is not written by this firmware. */
static void quiesce_for_exit(void)
{
    ctrl_set(ACTRL_RESET);
    AUD_WMARK = 0u;
    AUD_CTRL = 0x2u;
    (void)codec_g2_reset();
    HEAD_R(0, 0x00) = 0u; HEAD_R(1, 0x00) = 0u;
    HEAD_R(0, 0x1C) = 0u;
    *(volatile uint32_t *)0x81000000u = 0x7u;
    G2_WDT_CTRL = 0x5A5A0000u;
}
static void iprog_exit_now(void)
{
    quiesce_for_exit();
    G2_IPROG_ADDR = AMIGA_FLASH_EXIT;
    G2_IPROG = G2_IPROG_KEY;
}
/* the boot guard (timer interrupt, 1 kHz): until the machine runs, a boot that stops leaves by AMIGA_FLASH_EXIT after
 * 180 s; the guard kicks the fabric watchdog meanwhile */
static volatile uint32_t s_guard_deadline, s_guard_fired;
static void boot_guard(void)
{
    if (s_guard_fired) return;
    if ((int32_t)(get_millis() - s_guard_deadline) > 0) {
        uint32_t t0;
        s_guard_fired = 1u;
        say("\nBoot guard: 180 s without a running machine -- back to the golden image in 10 s\n");
        for (t0 = get_millis(); get_millis() - t0 < 10000u; ) G2_WDT_KICK = G2_WDT_KEY;
        iprog_exit_now();
        return;
    }
    G2_WDT_KICK = G2_WDT_KEY;
}

/* the stick and \amiga: Kickstart and the drives; on a failure the reason, then any key reads the stick again, the
 * menu key leaves */
static void prepare(void)
{
    unsigned int d;
    for (;;) {
        s_log_ok = s_stick_port > 0;
        if (s_stick_port > 0) {
            ini_read();
            if (kick_load() == 0) {
                for (d = 0; d < DRIVES; d++) {
                    if (!s_df_name[d][0] && d == 0u && !s_df0_key && !s_hd_name[0][0] && list_ext(".adf") == FR_OK && s_nfiles)
                        strncpy(s_df_name[0], s_files[0], sizeof s_df_name[0] - 1u);
                    if (s_df_name[d][0]) { char nm[256]; strcpy(nm, s_df_name[d]); (void)df_insert(d, nm); }
                }
                for (d = 0; d < HDS; d++) if (s_hd_name[d][0]) (void)hd_insert(d, s_hd_name[d]);
                say("Starting the machine\n");
                log_save();
                return;
            }
        } else say("No stick mounted.\n");
        say("Enter, Cross or Start: read the stick again   F12, PS or the button: exit to the golden image\n");
        log_save();
        for (;;) {
            unsigned int k = wait_press();
            if (k & IN_MENU) { say("Exit to the golden image\n"); log_save(); iprog_exit_now(); for (;;) g2_wdt_kick(); }
            if (k & IN_OK) break;
        }
        (void)stick_remount();
    }
}

/* THE STACK is the 16 KB of block RAM below _stack_top (fw/link_ddr.ld), nothing else in it.  stack_paint fills the
 * part below the caller's frame with STACK_FILL at start-up; stack_used finds the deepest word since changed. */
#define STACK_FILL 0x5354434Bu                  /* "STCK" */
extern uint32_t _stack_top[];
static void __attribute__((noinline)) stack_paint(void)
{
    volatile uint32_t *p = (volatile uint32_t *)16u, *e = (volatile uint32_t *)((uintptr_t)__builtin_frame_address(0) - 256u);
    while (p < e) *p++ = STACK_FILL;
}
static uint32_t stack_used(void)
{
    const volatile uint32_t *p = (const volatile uint32_t *)16u;
    while (p < (const volatile uint32_t *)_stack_top && *p == STACK_FILL) p++;
    return (uint32_t)((uintptr_t)_stack_top - (uintptr_t)p);
}
int main(void)
{
    stack_paint();
    trap_init();
    g2_init();
    timer_ms_init(TIMER_MS_IRQ);
    s_guard_deadline = get_millis() + 180000u;
    timer_ms_set_hook(boot_guard);
    con_puts("\r\nG2 Amiga fabric firmware\r\n");
    if (AMI_ID != AMI_ID_VAL) { con_puts("no Amiga register page: not the Amiga fabric\r\n"); wait_us(3000000u); iprog_exit_now(); }
    ctrl_set(ACTRL_RESET);
#ifdef AMIGA_SIM
    /* simulation (rtl/sim g2-soc-amiga): no encoder, codec or USB device on the bench; the stick is a FAT image in
     * DDR2 chip A (fw/snes/diskio_ram.c) with \amiga\KICK.ROM and \amiga\DISK.ADF */
    snes_audio_init();
    trap_set_irq(11u, ami_irq);
    /* line G2_IRQ_LINE_AMI stays masked until cold_start has acknowledged sysctrl.v (irq_line) */
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));
    __asm__ volatile ("csrsi mstatus, 8");
    if (f_mount(&s_fs, "0:", 1) == FR_OK) { s_stick_port = 1; con_puts("Storage: RAM disk\r\n"); }
    prepare();
    timer_ms_set_hook(0);
    play();
#endif
    video_boot();
    say("Pano Logic G2 -- Amiga (NanoMig, Minimig chipset, FX68K)\n");
    {
        unsigned int ack = 0u;
        if (codec_g2_present()) { (void)codec_g2_init(&ack); say("Codec: WM8750BL programmed\n"); }
    }
    snes_audio_init();
    trap_set_irq(11u, ami_irq);
    /* line G2_IRQ_LINE_AMI stays masked until cold_start has acknowledged sysctrl.v (irq_line) */
    __asm__ volatile ("csrs mie, %0" :: "r"(1u << 11));
    __asm__ volatile ("csrsi mstatus, 8");
    netsh_init(); s_net_up = 1u;                  /* PHY, MAC, DHCP, TCP 2323 (fw/netsh.c); served by net_service */
    trap_screen = net_trap; trap_report();
    { netsh_info_t i; netsh_info(&i); log_put(i.phy >= 0 ? "network: PHY found, 10/100 advertised, DHCP started\n" : "network: no PHY on MDIO\n"); }
    if (usb_up() == 0) {
        s_usb_ok = 1;
        wait_us(300000u);
        (void)stick_mount(16u, 0u);
#ifdef AMIGA_BOOT
        if (s_stick_port >= 0) fw_stick();
        else say("Loader: no USB stick\n");
#endif
        usb_ehci_hold = USB_HOLD_SPLIT | USB_HOLD_BULK | USB_HOLD_HSCTL;
        pads_scan();
    }
#ifdef AMIGA_BOOT
    else say("Loader: the USB host did not start\n");
    say("Loader: network console only (put AMIGA.FW, then reboot)\n"); log_save();
    timer_ms_set_hook(0);
    for (;;) { g2_wdt_kick(); net_service(); }
#else
    prepare();
    timer_ms_set_hook(0);                        /* the main loop kicks the watchdog itself */
    play();
#endif
    return 0;
}
