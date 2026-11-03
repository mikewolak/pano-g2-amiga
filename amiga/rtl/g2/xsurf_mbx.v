/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// xsurf_mbx.v -- the host mailbox of the X-Surf 100 board's device model (amiga/rtl/src/ethernet.v): the model's 64 KB mailbox window in block RAM, its eth_dma_* master on port A in the core domain, the
// firmware on port B in the clk domain (amiga_g2.v MBX_ADDR, MBX_DATA).
//
// LAYOUT.  The window is 32,768 16-bit words in the model's order (the even byte, the 68000's upper byte, in bits
// 15:8).  Word address A[15:1] is lane A[2:1] of row A[15:3]: four lanes of two byte RAMs each, 8,192 bytes a RAM, so
// one row holds the aligned 8-byte group a wide access moves (eth_ddr3_mailbox.v: lane 0 in bits 15:0 of the 64-bit
// word, lane 3 in bits 63:48).
//
// PORT A (the model's master: eth_ddr3_mailbox.v's side of eth_dma_*).  A request is held until eth_dma_ready; the
// RAMs read the row in the clock the request is taken and eth_dma_ready is high in the next clock with the data, then
// low for at least one clock, so a request presented right after cannot complete with the previous data.  A write
// stores the bytes selected by the active-low strobes (eth_dma_uds, eth_dma_lds) of its lane, or every byte of the
// row when eth_dma_wide is set.
//
// PORT B (firmware).  b_addr[15:1] names a word; b_q is that word one clock after b_addr settles; b_we writes both
// bytes.
`timescale 1ns / 1ps
`default_nettype none

module xsurf_mbx_ram (                            // 8,192 x 8 true dual-port RAM (ide_dpram.v's form)
    input  wire        clka,
    input  wire [12:0] ada,
    input  wire        wea,
    input  wire [7:0]  dina,
    output reg  [7:0]  douta,
    input  wire        clkb,
    input  wire [12:0] adb,
    input  wire        web,
    input  wire [7:0]  dinb,
    output reg  [7:0]  doutb
);
    reg [7:0] mem [0:8191];
    always @(posedge clka) begin
        if (wea) mem[ada] <= dina;
        douta <= mem[ada];
    end
    always @(posedge clkb) begin
        if (web) mem[adb] <= dinb;
        doutb <= mem[adb];
    end
endmodule

module xsurf_mbx (
    input  wire        clk_a,
    input  wire        eth_dma_req,
    input  wire        eth_dma_write,
    input  wire [15:1] eth_dma_addr,
    input  wire [15:0] eth_dma_wdata,
    input  wire        eth_dma_wide,
    input  wire [63:0] eth_dma_wdata64,
    input  wire        eth_dma_uds,
    input  wire        eth_dma_lds,
    output reg         eth_dma_ready = 1'b0,
    output wire [15:0] eth_dma_rdata,
    output wire [63:0] eth_dma_rdata64,

    input  wire        clk_b,
    input  wire [15:1] b_addr,
    input  wire        b_we,
    input  wire [15:0] b_wdata,
    output wire [15:0] b_q
);
    wire        take = eth_dma_req & ~eth_dma_ready;
    reg  [1:0]  lane_q = 2'd0;
    always @(posedge clk_a) begin
        eth_dma_ready <= take;
        if (take) lane_q <= eth_dma_addr[2:1];
    end

    reg  [1:0]  b_lane_q = 2'd0;
    always @(posedge clk_b) b_lane_q <= b_addr[2:1];

    wire [63:0] qa, qb;
    genvar l;
    generate for (l = 0; l < 4; l = l + 1) begin : g_lane
        wire        sel_l = eth_dma_wide | (eth_dma_addr[2:1] == l);
        wire [15:0] wd    = eth_dma_wide ? eth_dma_wdata64[16 * l + 15 -: 16] : eth_dma_wdata;
        wire        b_sel = b_addr[2:1] == l;
        xsurf_mbx_ram u_hi (
            .clka(clk_a), .ada(eth_dma_addr[15:3]), .wea(take & eth_dma_write & sel_l & (eth_dma_wide | ~eth_dma_uds)),
            .dina(wd[15:8]), .douta(qa[16 * l + 15 -: 8]),
            .clkb(clk_b), .adb(b_addr[15:3]), .web(b_we & b_sel), .dinb(b_wdata[15:8]), .doutb(qb[16 * l + 15 -: 8]));
        xsurf_mbx_ram u_lo (
            .clka(clk_a), .ada(eth_dma_addr[15:3]), .wea(take & eth_dma_write & sel_l & (eth_dma_wide | ~eth_dma_lds)),
            .dina(wd[7:0]), .douta(qa[16 * l + 7 -: 8]),
            .clkb(clk_b), .adb(b_addr[15:3]), .web(b_we & b_sel), .dinb(b_wdata[7:0]), .doutb(qb[16 * l + 7 -: 8]));
    end endgenerate

    assign eth_dma_rdata64 = qa;
    assign eth_dma_rdata   = lane_q == 2'd0 ? qa[15:0] : lane_q == 2'd1 ? qa[31:16] : lane_q == 2'd2 ? qa[47:32] : qa[63:48];
    assign b_q             = b_lane_q == 2'd0 ? qb[15:0] : b_lane_q == 2'd1 ? qb[31:16] : b_lane_q == 2'd2 ? qb[47:32] : qb[63:48];
endmodule

`default_nettype wire
