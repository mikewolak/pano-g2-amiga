/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/setup_items.h -- the pages and items of the setup screen (amiga_main.c menu, drawn by setup.c): every
 * AMIGA.INI key the firmware reads has an item.  Values are filled in by amiga_main.c (setup_value); this table holds
 * what does not change: page, kind, label, AMIGA.INI key and help text.
 *
 * When a change takes effect: "at once" (the drive, the zone, the FTDI mode, the output mode), "when setup is left"
 * (outputs, offset: applied as the picture is set up again), "at the next reset" (the machine's configuration: kept
 * as a pending change until Save and reset, and given back by Discard).
 *
 * Author: Mike Wolak, EPROM Foundry Inc., project pano-g2-amiga. */
#ifndef SETUP_ITEMS_H
#define SETUP_ITEMS_H
#include "setup.h"

enum { PG_MACHINE, PG_DRIVES, PG_VIDEO, PG_PERIPH, PG_NETWORK, PG_SYSTEM, NPG };
static const char *const s_pg_name[NPG] = { "Machine", "Drives", "Video", "Peripherals", "Network", "System" };

enum {
    IT_NONE,
    IT_PROFILE, IT_CPU, IT_CHIPSET, IT_CHIP, IT_SLOW, IT_FAST, IT_KICK,
    IT_DF0, IT_DF1, IT_DF2, IT_DF3, IT_WRITE, IT_HD0, IT_HD1,
    IT_REGION, IT_VIDEO, IT_SCREEN, IT_HOFS,
    IT_FTDI, IT_FTDI_DEV, IT_MIDI, IT_KBD, IT_MOUSE, IT_PAD1, IT_PAD2, IT_SHARE,
    IT_TZ, IT_NTPWAIT, IT_CLOCK, IT_LINK, IT_IP, IT_CONSOLE,
    IT_RESUME, IT_SAVE, IT_DISCARD, IT_RESET, IT_LOG, IT_GOLDEN, IT_FWID, IT_FABRIC
};

typedef struct { unsigned char page, kind, id; const char *label, *key, *help; } su_def_t;

#define DF_HELP(n) "The disk image (*.adf) in drive " n ", or none.  The change is immediate, as a disk swap in a " \
                   "real drive, and is written to AMIGA.INI."
#define HD_HELP    "A hard-disk image (*.hdf with a RigidDiskBlock) on the Gayle IDE port, a new image of 128 MB to " \
                   "1 GB, or none.  The image opens at once and is written to AMIGA.INI; the machine starts cold " \
                   "when setup is left."

static const su_def_t s_su_def[] = {
    { PG_MACHINE, SU_SECTION, IT_NONE,    "Model", 0, 0 },
    { PG_MACHINE, SU_VALUE,   IT_PROFILE, "Profile", 0,
      "A set of CPU, chipset, memory and Kickstart: A500 OCS, A500+ ECS, ECS 2 MB, A1200 AGA.  Choosing one sets "
      "the items below.  A profile whose Kickstart image is not in \\amiga is refused.  Applied at the next reset." },
    { PG_MACHINE, SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_MACHINE, SU_SECTION, IT_NONE,    "Processor and chipset", 0, 0 },
    { PG_MACHINE, SU_VALUE,   IT_CPU,     "CPU", "CPU",
      "The processor of the core.  The 68010 and 68020 are the TG68K of the AGA fabric; the OCS/ECS fabric runs a "
      "68000 only.  Applied at the next reset." },
    { PG_MACHINE, SU_VALUE,   IT_CHIPSET, "Chipset", "CHIPSET",
      "OCS as in the A500, OCS as in the A1000, ECS, or AGA.  AGA needs the AGA fabric.  Applied at the next reset." },
    { PG_MACHINE, SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_MACHINE, SU_SECTION, IT_NONE,    "Memory", 0, 0 },
    { PG_MACHINE, SU_VALUE,   IT_CHIP,    "Chip RAM", "CHIP",
      "Memory shared by the CPU and the custom chips: 512 KB, 1 MB, 1.5 MB or 2 MB.  Applied at the next reset." },
    { PG_MACHINE, SU_VALUE,   IT_SLOW,    "Slow RAM", "SLOW",
      "Memory at $C00000 on the chip bus, as the A501 trapdoor board: none, 512 KB, 1 MB or 1.5 MB.  Applied at the "
      "next reset." },
    { PG_MACHINE, SU_VALUE,   IT_FAST,    "Fast RAM", "FAST",
      "One Zorro II board at $200000: none, 2 MB, 4 MB or 8 MB.  Only the CPU reads it.  Applied at the next reset." },
    { PG_MACHINE, SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_MACHINE, SU_SECTION, IT_NONE,    "Kickstart", 0, 0 },
    { PG_MACHINE, SU_VALUE,   IT_KICK,    "Kickstart ROM", "KICK",
      "The ROM image in \\amiga (*.rom, 256 KB or 512 KB).  Loaded at Save and reset; applied at the next reset." },

    { PG_DRIVES,  SU_SECTION, IT_NONE,    "Floppy drives", 0, 0 },
    { PG_DRIVES,  SU_VALUE,   IT_DF0,     "DF0:", "DF0", DF_HELP("DF0:") },
    { PG_DRIVES,  SU_VALUE,   IT_DF1,     "DF1:", "DF1", DF_HELP("DF1:") },
    { PG_DRIVES,  SU_VALUE,   IT_DF2,     "DF2:", "DF2", DF_HELP("DF2:") },
    { PG_DRIVES,  SU_VALUE,   IT_DF3,     "DF3:", "DF3", DF_HELP("DF3:") },
    { PG_DRIVES,  SU_VALUE,   IT_WRITE,   "Disk writes", "WRITE",
      "Enabled: sectors the Amiga writes go to the image files on the stick.  Write protected: the floppy drives "
      "report a protected disk and image files open read-only.  Applied at the next reset." },
    { PG_DRIVES,  SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_DRIVES,  SU_SECTION, IT_NONE,    "Hard disks (Gayle IDE)", 0, 0 },
    { PG_DRIVES,  SU_VALUE,   IT_HD0,     "HD0: master", "HD0", HD_HELP },
    { PG_DRIVES,  SU_VALUE,   IT_HD1,     "HD1: slave", "HD1", HD_HELP },

    { PG_VIDEO,   SU_SECTION, IT_NONE,    "Picture", 0, 0 },
    { PG_VIDEO,   SU_VALUE,   IT_REGION,  "Region", "REGION",
      "NTSC: 60 Hz, 262 lines, 28.636364 MHz.  PAL: 50 Hz, 312 lines, 28.375160 MHz.  Sets the chipset clock and "
      "the frame rate.  Applied at the next reset." },
    { PG_VIDEO,   SU_VALUE,   IT_VIDEO,   "Output mode", "VIDEO",
      "Scaled: every Amiga mode scaled to 640x480 at 31 kHz.  Native: 15 kHz, 640x240 (NTSC) or 640x288 (PAL), for a "
      "monitor that takes 15 kHz.  Applied at once: setup is shown in the new mode." },
    { PG_VIDEO,   SU_VALUE,   IT_SCREEN,  "Outputs", "SCREEN",
      "The heads that show the Amiga: the micro-HDMI head, the DVI-I head, or both.  Applied when setup is left." },
    { PG_VIDEO,   SU_VALUE,   IT_HOFS,    "Picture offset", "HOFS",
      "The first captured sample of a 15 kHz line, in samples of the 14 MHz grid after horizontal blank: 0 to 400.  "
      "77 centres the standard display window.  Applied when setup is left." },

    { PG_PERIPH,  SU_SECTION, IT_NONE,    "USB serial (FTDI)", 0, 0 },
    { PG_PERIPH,  SU_VALUE,   IT_FTDI,    "FTDI channel A", "FTDI",
      "Serial: an FTDI chip on the hub is Paula's serial port.  MPSSE: channel A of an FT2232H carries MPSSE "
      "commands (JTAG, SPI, SWD) for AmigaOS programs.  Applied at once." },
    { PG_PERIPH,  SU_INFO,    IT_FTDI_DEV, "FTDI device", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_MIDI,    "USB MIDI", 0, 0 },
    { PG_PERIPH,  SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_PERIPH,  SU_SECTION, IT_NONE,    "Input devices", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_KBD,     "Keyboard", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_MOUSE,   "Mouse", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_PAD1,    "Pad 1", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_PAD2,    "Pad 2", 0, 0 },
#ifdef AMIGA_SHARE
    { PG_PERIPH,  SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_PERIPH,  SU_SECTION, IT_NONE,    "Shared folder", 0, 0 },
    { PG_PERIPH,  SU_INFO,    IT_SHARE,   "SHARE: folder", "SHARE", 0 },
#endif

    { PG_NETWORK, SU_SECTION, IT_NONE,    "Time", 0, 0 },
    { PG_NETWORK, SU_VALUE,   IT_TZ,      "Time zone", "TZ",
      "Hours east of UTC, -12 to +14.  The battery clock and the dates written into disk images hold local time: "
      "the network time plus the zone.  Applied at once." },
    { PG_NETWORK, SU_VALUE,   IT_NTPWAIT, "NTP wait", "NTPWAIT",
      "Seconds after power-up the first start of the machine waits for network time, so Kickstart finds the clock "
      "set; 0 starts at once.  Applied at the next power-up." },
    { PG_NETWORK, SU_INFO,    IT_CLOCK,   "Local time", 0, 0 },
    { PG_NETWORK, SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_NETWORK, SU_SECTION, IT_NONE,    "Ethernet", 0, 0 },
    { PG_NETWORK, SU_INFO,    IT_LINK,    "Link", 0, 0 },
    { PG_NETWORK, SU_INFO,    IT_IP,      "Address", 0, 0 },
    { PG_NETWORK, SU_INFO,    IT_CONSOLE, "Console", 0, 0 },

    { PG_SYSTEM,  SU_SECTION, IT_NONE,    "Machine", 0, 0 },
    { PG_SYSTEM,  SU_ACTION,  IT_RESUME,  "Resume", 0,
      "The machine continues where it stopped.  With changes not saved, a dialog offers to save them and reset, or "
      "to discard them." },
    { PG_SYSTEM,  SU_ACTION,  IT_SAVE,    "Save and reset", 0,
      "Writes the changed items to AMIGA.INI, loads the Kickstart when it changed, and starts the machine cold; "
      "with nothing changed, only the cold start.  A dialog confirms it first.  F10 and Triangle do the same from "
      "every page." },
    { PG_SYSTEM,  SU_ACTION,  IT_DISCARD, "Discard changes", 0,
      "Gives back every item changed since setup was opened, and resumes." },
    { PG_SYSTEM,  SU_ACTION,  IT_RESET,   "Reset", 0,
      "Starts the machine cold with the saved settings; changes not saved are discarded." },
    { PG_SYSTEM,  SU_GAP,     IT_NONE,    "", 0, 0 },
    { PG_SYSTEM,  SU_SECTION, IT_NONE,    "Firmware", 0, 0 },
    { PG_SYSTEM,  SU_ACTION,  IT_LOG,     "Boot log", 0, "The console lines of this boot, as \\amiga\\BOOT.LOG holds them." },
    { PG_SYSTEM,  SU_ACTION,  IT_GOLDEN,  "Golden image", 0,
      "Closes every image file and returns the unit to the golden image in flash." },
    { PG_SYSTEM,  SU_INFO,    IT_FWID,    "Firmware", 0, 0 },
    { PG_SYSTEM,  SU_INFO,    IT_FABRIC,  "Fabric", 0, 0 },
};
#define NSU_DEF (sizeof s_su_def / sizeof s_su_def[0])
#endif
