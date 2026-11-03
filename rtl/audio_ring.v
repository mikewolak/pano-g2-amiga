/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// audio_ring.v — small clock-crossing sample FIFO (gray-pointer async FIFO).
//
// Decouples the codec I2S sample clock from the DSP chain's sample clock.  The
// two are frame-rate-locked by the PLL (AUD_XCK and the core clock share one
// VCO), but they are still separate clock domains with a phase offset, so a
// short elastic buffer absorbs the jitter.  DEPTH is a power of two, <= 32 as
// requested (default 16 -> up to 15 stereo samples in flight = ~0.3 ms @48kHz).
//
// One WIDTH-bit word = one stereo sample {left, right}.  Standard gray-coded
// read/write pointers with 2-flop synchronizers on each side; full/empty are
// exact for a power-of-two depth.
`default_nettype none

module audio_ring #(
    parameter WIDTH = 48,          // stereo sample: {left[23:0], right[23:0]}
    parameter DEPTH = 16,          // power of two, <= 32
    parameter AW    = 4            // log2(DEPTH)
)(
    // write side (producer)
    input  wire              wclk,
    input  wire              wrst_n,
    input  wire              wr,        // push when !wfull
    input  wire [WIDTH-1:0]  wdata,
    output wire              wfull,
    // read side (consumer)
    input  wire              rclk,
    input  wire              rrst_n,
    input  wire              rd,        // pop when !rempty
    output reg  [WIDTH-1:0]  rdata,
    output wire              rempty
);
    reg [WIDTH-1:0] mem [0:DEPTH-1];
    reg [AW:0] rgray;   // read-domain gray pointer; declared here since the write
                        // domain's CDC synchronizer (below) reads it first

    // ---- write domain ----
    reg  [AW:0] wbin, wgray;           // binary + gray, one extra wrap bit
    reg  [AW:0] rgray_w1, rgray_w2;    // read gray synced into write domain
    wire [AW:0] wbin_nxt  = wbin + {{AW{1'b0}}, (wr & ~wfull)};   // sized
    wire [AW:0] wgray_nxt = wbin_nxt ^ (wbin_nxt >> 1);
    // full from the CURRENT registered wgray (textbook async FIFO).  Deriving it
    // from wgray_nxt makes wfull -> wbin_nxt -> wgray_nxt -> wfull a combinational
    // cycle that oscillates at the fill boundary when wr=1 (Verilator non-convergence,
    // real-silicon glitch); the current-pointer form breaks it and uses full depth.
    assign wfull = (wgray == {~rgray_w2[AW:AW-1], rgray_w2[AW-2:0]});
    always @(posedge wclk or negedge wrst_n) begin
        if (!wrst_n) begin wbin<=0; wgray<=0; rgray_w1<=0; rgray_w2<=0; end
        else begin
            if (wr & ~wfull) mem[wbin[AW-1:0]] <= wdata;
            wbin<=wbin_nxt; wgray<=wgray_nxt;
            rgray_w1<=rgray; rgray_w2<=rgray_w1;
        end
    end

    // ---- read domain ----
    reg  [AW:0] rbin;
    reg  [AW:0] wgray_r1, wgray_r2;    // write gray synced into read domain
    wire [AW:0] rbin_nxt  = rbin + {{AW{1'b0}}, (rd & ~rempty)};  // sized
    wire [AW:0] rgray_nxt = rbin_nxt ^ (rbin_nxt >> 1);
    assign rempty = (rgray == wgray_r2);
    always @(posedge rclk or negedge rrst_n) begin
        if (!rrst_n) begin rbin<=0; rgray<=0; wgray_r1<=0; wgray_r2<=0; rdata<=0; end
        else begin
            if (rd & ~rempty) rdata <= mem[rbin_nxt[AW-1:0]];
            else              rdata <= mem[rbin[AW-1:0]];
            rbin<=rbin_nxt; rgray<=rgray_nxt;
            wgray_r1<=wgray; wgray_r2<=wgray_r1;
        end
    end
endmodule
`default_nettype wire
