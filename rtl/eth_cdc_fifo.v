/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// eth_cdc_fifo.v -- the MAC's two clock crossings: a small dual-clock FIFO, gray-coded pointers, first-word-
// fall-through read, and a FREE count on the write side.
//
// WHY A FREE COUNT.  The receive side must never lose an end-of-frame marker: a lost EOF merges two frames, and
// that is how the first MAC misframed every frame after the first odd-length one.  So the writer pushes DATA only
// while at least two entries are free and pushes an EOF whenever one is -- the reservation is the writer's, and it
// needs to know the level, not just "full".
//
// WHY GRAY CODE.  eth_rxc / eth_txc are PHY-sourced, genuinely asynchronous to clk50.  A gray pointer changes one
// bit per step, so a synchroniser that samples it mid-change sees the old or the new value, never a third.  The
// synchronisers are the only paths that cross; the UCF gives each a DATAPATHONLY MAXDELAY rather than a TIG, so
// TRCE still sees them.
//
// Distributed RAM (no RAMB16 is spare: 36/36).  The read data is mem[rd_ptr] combinationally: a registered read
// that advanced while the consumer stalled was the second bug in the first MAC's FIFO.
`timescale 1ns / 1ps
`default_nettype none

module eth_cdc_fifo #(
    parameter DW = 10,
    parameter AW = 5                        // 2^AW entries
)(
    input  wire          wr_clk,
    input  wire          wr_rst,            // wr_clk domain; resets only with the whole SoC (see soc_ethlite)
    input  wire [DW-1:0] wr_data,
    input  wire          wr_en,             // ignored when full
    output wire          full,
    output wire [AW:0]   wr_free,           // entries free, as the writer sees it (conservative: never too high)

    input  wire          rd_clk,
    input  wire          rd_rst,
    output wire [DW-1:0] rd_data,           // first-word-fall-through
    input  wire          rd_en,             // ignored when empty
    output wire          empty
);
    localparam DEPTH = 1 << AW;
    (* ram_style = "distributed" *) reg [DW-1:0] mem [0:DEPTH-1];

    reg [AW:0] wp = 0, rp = 0;              // binary, {wrap, index}
    reg [AW:0] wg = 0, rg = 0;              // gray, registered (a gray value built from a binary adder is not
                                            // glitch-free; the synchroniser must sample a flop)
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [AW:0] rg_s1 = 0, rg_s2 = 0;   // rd -> wr
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [AW:0] wg_s1 = 0, wg_s2 = 0;   // wr -> rd

    function automatic [AW:0] g2b; input [AW:0] g; integer i; begin   /* automatic: XST accepts the loop only so (1b9c113) */
        g2b[AW] = g[AW];
        for (i = AW-1; i >= 0; i = i - 1) g2b[i] = g2b[i+1] ^ g[i];
    end endfunction

    // ---- write side ----
    wire [AW:0] rp_w  = g2b(rg_s2);
    wire [AW:0] used_w = wp - rp_w;
    assign full    = (used_w == DEPTH);
    assign wr_free = DEPTH - used_w;
    wire        wpush = wr_en && !full;
    always @(posedge wr_clk) begin
        {rg_s2, rg_s1} <= {rg_s1, rg};
        if (wpush) mem[wp[AW-1:0]] <= wr_data;
        if (wr_rst) begin wp <= 0; wg <= 0; end
        else if (wpush) begin wp <= wp + 1'b1; wg <= (wp + 1'b1) ^ ((wp + 1'b1) >> 1); end
    end

    // ---- read side ----
    wire [AW:0] wp_r = g2b(wg_s2);
    assign empty   = (wp_r == rp);
    assign rd_data = mem[rp[AW-1:0]];
    wire        rpop = rd_en && !empty;
    always @(posedge rd_clk) begin
        {wg_s2, wg_s1} <= {wg_s1, wg};
        if (rd_rst) begin rp <= 0; rg <= 0; end
        else if (rpop) begin rp <= rp + 1'b1; rg <= (rp + 1'b1) ^ ((rp + 1'b1) >> 1); end
    end
endmodule
`default_nettype wire
