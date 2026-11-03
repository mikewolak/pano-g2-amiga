/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// soc_ram.v -- the bring-up SoC's code/data memory: four byte-wide block RAMs
// behind one 32-bit port with byte write enables.
//
// WHY FOUR BYTE-WIDE ARRAYS AND NOT ONE 32-BIT ARRAY.  RV32 needs sb/sh, so the
// port needs byte enables, and a Spartan-3E RAMB16 has no per-byte write enable
// in the x16/x32 configurations that XST would pick for a 32-bit array.  Four
// independent x8 memories give byte writes for free and infer as plain single-
// port block RAM, which is the one memory template XST 14.7 never argues with.
//
// One clock of read latency: `q` is valid the cycle after `en`.  Read-during-
// write on the same lane returns the OLD contents (read-first); nothing in this
// SoC reads and writes the same address in the same cycle, and saying so here
// is cheaper than a comment war later.
//
// DEPTH is in 32-bit words.  32 KB = 8192 words = 16 RAMB16 of the part's 36.
// INITIALISED FROM THE BITSTREAM.  Until now this memory came up EMPTY at power-on and the only
// way to get code into it was a JTAG load with the CPU held in reset -- which is why the board could not boot
// itself.  With INIT_HEX set, the four byte lanes are loaded from four hex files at elaboration, so the
// bitstream carries the first stage and the CPU has something to execute the moment reset is released.
//
// FOUR FILES, NOT ONE, because the memory is four byte-wide arrays (see above).  Splitting a word-wide file
// across lanes inside an initial block is the sort of thing XST 14.7 either infers badly or refuses; one
// $readmemh straight into each array is the template it never argues with.  tools/mkraminit writes them.
//
// An empty INIT_HEX leaves the memory uninitialised, which is exactly the old behaviour -- so a build without
// a first stage behaves as every build before this one did.
`timescale 1ns / 1ps
`default_nettype none

module soc_ram #(
    parameter AW = 13,                      // word address width; bytes = 4 << AW
    parameter INIT_HEX = ""                 // "" = uninitialised; else the prefix: <INIT_HEX>0.hex .. 3.hex
) (
    input  wire            clk,
    input  wire            en,              // one access this cycle
    input  wire [AW-1:0]   addr,            // WORD address
    input  wire [31:0]     wdata,
    input  wire [3:0]      wstrb,           // per-byte write enable, 0 = read
    output wire [31:0]     rdata
);
    localparam DEPTH = (1 << AW);

    /* ram_style block, ON PURPOSE: when the video head's line buffer took the last RAMB16s, XST quietly
     * built two of these lanes out of ~4,100 LUTs and the design overmapped.  With the attribute a
     * lane that cannot get block RAM fails the map loudly instead of eating the fabric. */
    (* ram_style = "block" *) reg [7:0] m0 [0:DEPTH-1];
    (* ram_style = "block" *) reg [7:0] m1 [0:DEPTH-1];
    (* ram_style = "block" *) reg [7:0] m2 [0:DEPTH-1];
    (* ram_style = "block" *) reg [7:0] m3 [0:DEPTH-1];
    reg [7:0] q0, q1, q2, q3;

    /* generate, not a bare `if': XST evaluates the parameter at elaboration, so a build with no first stage
     * never sees a $readmemh for a file that does not exist. */
    generate
        if (INIT_HEX != "") begin : g_init
            initial begin
                $readmemh({INIT_HEX, "0.hex"}, m0);
                $readmemh({INIT_HEX, "1.hex"}, m1);
                $readmemh({INIT_HEX, "2.hex"}, m2);
                $readmemh({INIT_HEX, "3.hex"}, m3);
            end
        end
    endgenerate

    always @(posedge clk) if (en) begin
        if (wstrb[0]) m0[addr] <= wdata[7:0];
        q0 <= m0[addr];
    end
    always @(posedge clk) if (en) begin
        if (wstrb[1]) m1[addr] <= wdata[15:8];
        q1 <= m1[addr];
    end
    always @(posedge clk) if (en) begin
        if (wstrb[2]) m2[addr] <= wdata[23:16];
        q2 <= m2[addr];
    end
    always @(posedge clk) if (en) begin
        if (wstrb[3]) m3[addr] <= wdata[31:24];
        q3 <= m3[addr];
    end

    assign rdata = {q3, q2, q1, q0};
endmodule

`default_nettype wire
