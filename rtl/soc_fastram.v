/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// soc_fastram.v -- 1K x 32 of fast RAM in the memory map, dual-ported.
//
// WHY.  Spare block RAM as a 1K x 32 fast RAM in the memory map, for row
// buffers, DMA sources and hot data structures.  The boot RAM is code and stack; the LPDDR is 20 ns against a 300 ns
// burst, read-modify-write per word for anything sub-word; this is the place
// for the row buffer a renderer fills byte by byte and the handful of tables
// it hits per column.  fw/fastram.h puts a variable here with one attribute.
//
// WHERE.  Two views of the same 4 KB, the same trick as the LPDDR windows:
//   0x0000_4000  cached   -- contiguous with the boot RAM, single-cycle on a
//                           data-cache hit, write-through like everything else
//   0x8000_8000  uncached -- address bit 31; every access is one direct RAM
//                           cycle, no line fills, no cache pollution: what a
//                           write-once-then-DMA row buffer wants
//
// SHAPE.  Four byte-wide block RAMs behind a 32-bit port, exactly as soc_ram:
// pixels are bytes and a Spartan-3E RAMB16 has no per-byte write enable in
// its x16/x32 modes, so the x8 shape is what makes a byte store one cycle
// instead of a read-modify-write, which is the thing this memory exists to
// avoid.  1K x 8 uses half of each RAMB16's 2K x 8; the other half is the
// price of byte enables on this part.  4 RAMB16 of the 36.
//
// TWO PORTS.  Port A is the SoC bus, on the SoC clock.  Port B is the DMA
// engine's, on the controller's clk90 -- RAMB16 is true dual-port with a
// clock per port, so a row written by the CPU here streams into the LPDDR in
// the controller's domain without crossing a clock boundary as data (only
// the engine's start/done cross, and they already do).  Port B is a plain
// synchronous read/write port; the engine's fill and drain sequences drive
// it.  Both ports are read-first on a same-cycle collision; the engine never
// runs while the CPU is writing the same rows, by construction of the
// software that uses it.
//
// SINGLE CYCLE, INCLUDING SUB-WORD WRITES.  Port A is
// clocked on the FALLING edge of the SoC clock: the CPU presents address,
// data and byte enables from the rising edge, the RAM takes them half a
// cycle later, and a read's data is on the bus before the next rising edge,
// so the SoC acknowledges the access in the cycle it is made -- one bus cycle
// for a read, one for a write of any width.  The half-cycle each way is
// 10 ns at 50 MHz, against a RAMB16 clock-to-out of about 3 ns; both arcs are
// posedge->negedge paths that TRCE constrains at the half period.  A byte or
// halfword write is one write through its lanes; nothing here ever reads to
// write.
//
// XST infers this as dual-port block RAM from two clocked processes on one
// array per lane; the template is the one soc_ram already uses, doubled, with
// port A on the inverted clock (the RAMB16's own clock inverter).
`timescale 1ns / 1ps
`default_nettype none
module soc_fastram #(
    parameter AW = 10                       // words: 10 -> 1024 -> 4 KB
) (
    // port A: the SoC bus
    input  wire            clk,
    input  wire            en,
    input  wire [AW-1:0]   addr,
    input  wire [31:0]     wdata,
    input  wire [3:0]      wstrb,
    output wire [31:0]     rdata,
    // port B: the DMA engine, in its own clock
    input  wire            clkb,
    input  wire            enb,
    input  wire [AW-1:0]   addrb,
    input  wire [31:0]     wdatab,
    input  wire [3:0]      wstrbb,
    output wire [31:0]     rdatab
);
    localparam DEPTH = (1 << AW);
    reg [7:0] m0 [0:DEPTH-1];
    reg [7:0] m1 [0:DEPTH-1];
    reg [7:0] m2 [0:DEPTH-1];
    reg [7:0] m3 [0:DEPTH-1];
    reg [7:0] qa0, qa1, qa2, qa3;
    reg [7:0] qb0, qb1, qb2, qb3;

    always @(negedge clk) if (en) begin
        if (wstrb[0]) m0[addr] <= wdata[7:0];
        qa0 <= m0[addr];
    end
    always @(negedge clk) if (en) begin
        if (wstrb[1]) m1[addr] <= wdata[15:8];
        qa1 <= m1[addr];
    end
    always @(negedge clk) if (en) begin
        if (wstrb[2]) m2[addr] <= wdata[23:16];
        qa2 <= m2[addr];
    end
    always @(negedge clk) if (en) begin
        if (wstrb[3]) m3[addr] <= wdata[31:24];
        qa3 <= m3[addr];
    end

    always @(posedge clkb) if (enb) begin
        if (wstrbb[0]) m0[addrb] <= wdatab[7:0];
        qb0 <= m0[addrb];
    end
    always @(posedge clkb) if (enb) begin
        if (wstrbb[1]) m1[addrb] <= wdatab[15:8];
        qb1 <= m1[addrb];
    end
    always @(posedge clkb) if (enb) begin
        if (wstrbb[2]) m2[addrb] <= wdatab[23:16];
        qb2 <= m2[addrb];
    end
    always @(posedge clkb) if (enb) begin
        if (wstrbb[3]) m3[addrb] <= wdatab[31:24];
        qb3 <= m3[addrb];
    end

    assign rdata  = {qa3, qa2, qa1, qa0};
    assign rdatab = {qb3, qb2, qb1, qb0};
endmodule
`default_nettype wire
