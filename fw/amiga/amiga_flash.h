/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* fw/amiga/amiga_flash.h -- where the Amiga fabric and its firmware live in the configuration flash of a revision C
 * unit (M25P64, 8 MB, 64 KB sectors), and where the firmware reconfigures from when it leaves.
 *
 *   0x380000-0x67FFFF  the Amiga fabric in the multiboot slot, loaded at power-up (ise/build_g2.sh G2_AMIGA=1:
 *                      3,079,464 bytes at 57e2c6b)
 *   0x680000-0x6BFFFF  the Amiga firmware, a tools/panoldr image (header + payload, 256 KB at most)
 *   0x6C0000-0x6CFFFF  configuration table and MAC (never written)
 * Both the fabric and the firmware lie inside the multiboot slot 0x380000-0x6BFFFF, so one image written at 0x380000
 * (fw/amiga/mkslot.sh) carries both, and the rest of the flash (0x6D0000 up) is not touched.  Leaving reconfigures
 * from the golden image at 0x040000. */
#ifndef AMIGA_FLASH_H
#define AMIGA_FLASH_H
#define AMIGA_FLASH_FABRIC  0x00380000u   /* SPIF_G2_MAIN, revision C */
#define AMIGA_FLASH_FW      0x00680000u
#define AMIGA_FLASH_FW_END  0x006C0000u
#define AMIGA_FLASH_EXIT    0x00040000u   /* SPIF_G2_GOLDEN */
#define AMIGA_EXIT_S        "the golden image"
#endif
