/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vga_linebuf.v -- ping-pong source-line store between the SDRAM fetch
// domain (100 MHz write) and the pixel domain (read).  2048 x 30: two
// 1024-entry buffers selected by the address MSB; a source line is at most
// 800 pixels of 10:10:10.
//
// Written as the exact simple-dual-port dual-clock template Quartus 8 infers
// into M4K (one write port, one read port, independent clocks, registered
// read).  No read-during-write anywhere near the use pattern: the pixel side
// only reads a buffer the fetcher finished a full line ago.
module vga_linebuf #(
    parameter AW = 11                  // address bits: two lines of 2^(AW-1) source pixels
) (
    input  wire        wclk,
    input  wire        we,
    input  wire [10:0] waddr,
    input  wire [29:0] wdata,
    input  wire        rclk,
    input  wire [10:0] raddr,
    output reg  [29:0] rdata
);
    /* AW = 11: two lines of 1,024 source pixels (4 RAMB16 on a Spartan-3E); AW = 10: two lines of 512, for a part
     * whose block RAM is short (an XC3S1600E has 36 RAMB16). */
    reg [29:0] mem [0:(1<<AW)-1];
    always @(posedge wclk) if (we) mem[waddr[AW-1:0]] <= wdata;
    always @(posedge rclk) rdata <= mem[raddr[AW-1:0]];
endmodule
