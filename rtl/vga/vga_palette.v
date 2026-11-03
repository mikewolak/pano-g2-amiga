/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
/* vga_palette.v -- the 256-entry colour table the head looks up during scanout.
 *
 * WHY THIS EXISTS.  A 320x200 frame of BYTES holds one palette index per pixel.  Without the table the CPU
 * expands every one of those 64,000 bytes into a 32-bit colour and stored it into the framebuffer, and
 * that expand IS the frame: measured on the board, 41 ms of a 90..200 ms frame, at 6.2 MB/s, which is
 * exactly the rate of a 32-bit store into LPDDR here (no byte mask, so every store is a burst
 * read-modify-write).  The copy engine cannot help with it -- the engine moves bytes, and the cost is the
 * per-pixel LOOKUP, not the move.
 *
 * So the head does the lookup instead, at the pixel clock, from this table.  The frame the CPU writes is
 * then the byte frame the program already drew: no expand, no staging buffer, no blit.  Scanout also reads a
 * QUARTER of the memory it used to, because a pixel is one byte rather than two 16-bit words.
 *
 * Dual-clock by construction, exactly like vga_linebuf: written from the CPU's register page in its own
 * clock, read by the scanner in fclk.  One RAMB16 on a Spartan-3E (256 x 30 = 7,680 bits).
 *
 * 30 bits out, not 24: the head's internal pixel is {R[29:20], G[19:10], B[9:0]} and the THS8135 takes
 * [9:2] of each field.  The register page hands us 8 bits a component and we place them at [9:2] of their
 * field, so what the DAC latches is the byte the CPU wrote, unshifted and unrounded.
 *
 * No reset: a palette is meaningless until it is written, and the firmware writes all 256 entries before
 * it enables paletted mode.  A reset value would only be a colour that is wrong in a different way. */
module vga_palette (
    input  wire        wclk,
    input  wire        we,
    input  wire [7:0]  waddr,
    input  wire [29:0] wdata,
    input  wire        rclk,
    input  wire [7:0]  raddr,
    output reg  [29:0] rdata
);
    reg [29:0] mem [0:255];
    always @(posedge wclk) if (we) mem[waddr] <= wdata;
    always @(posedge rclk) rdata <= mem[raddr];
endmodule
