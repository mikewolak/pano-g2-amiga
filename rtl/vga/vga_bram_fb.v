/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vga_bram_fb.v -- a double-buffered 640x480 frame buffer in block RAM at 1 or 2 bits per pixel, written by the CPU
// through a Wishbone window and read by rtl/vga/vga_scan.v through its client port as PALETTED (one byte a pixel)
// source data.
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// 1 STORAGE.  WORDS = 2 x 640 x 480 x BPP / 32 words of 32 bits: 38,400 at 2 bpp (153,600 bytes), 19,200 at 1 bpp
//   (76,800 bytes).  Four byte lanes, each a dual-port RAM: port A read/write on clk (CPU), port B read on vclk
//   (head).  Buffer 0 is words 0 .. WORDS/2-1, buffer 1 the rest.
// 2 PIXEL PACKING (the CPU's view, little-endian): pixel p of the window is
//     2 bpp: byte p/4, bits [2*(p%4) +: 2]      1 bpp: byte p/8, bit p%8
//   i.e. the leftmost pixel of a byte is its least significant field.  A 640-pixel line is 160 / 80 bytes.
// 3 THE HEAD'S VIEW.  vga_scan in paletted mode reads one 16-bit client word per two pixels, the even pixel in bits
//   [7:0].  Client word w is pixels 2w and 2w+1 of the virtual one-byte-a-pixel space, so buffer 1 starts at
//   client word 153,600 (pixel 307,200) for either depth.  Each pixel comes back as its index zero-extended to a
//   byte: palette entries 0..3 (2 bpp) or 0..1 (1 bpp) of the head's table.
//     2 bpp: port word w >> 3, bits [4*w[2:0] +: 4] -> {6'b0, p(2w+1), 6'b0, p(2w)}
//     1 bpp: port word w >> 4, bits [2*w[3:0] +: 2] -> {7'b0, p(2w+1), 7'b0, p(2w)}
//   Client words beyond the buffers read 0.
// 4 CLIENT TIMING.  rdy is high whenever `en' is (no queue: every request is answered); a request presented at one
//   edge is returned three edges later, in order, one per clock.
// 5 CPU TIMING.  One wait state: fb_ack follows fb_cyc by one clock and carries the read data; fb_sel are byte
//   enables.  Addresses beyond the buffers are acknowledged, read 0 and ignore writes.
`timescale 1ns / 1ps
`default_nettype none
module vga_bram_fb #(
    parameter integer BPP = 2                      // 1 or 2
) (
    // ---- CPU window (clk) ----
    input  wire        clk,
    input  wire        fb_cyc,                     // cycle and strobe
    input  wire        fb_we,
    input  wire [15:0] fb_adr,                     // 32-bit word inside the window
    input  wire [3:0]  fb_sel,
    input  wire [31:0] fb_dat_w,
    output wire [31:0] fb_dat_r,
    output reg         fb_ack,

    // ---- vga_scan's client port (vclk) ----
    input  wire        vclk,
    input  wire        en,
    input  wire [3:0]  req,
    input  wire [47:0] req_row,
    input  wire [31:0] req_col,
    output reg  [3:0]  rdy,
    output reg  [15:0] rdata,
    output reg         rvalid,
    output reg  [1:0]  rbank
);
    localparam integer WORDS = 2 * 640 * 480 * BPP / 32;
    localparam integer AW    = 16;

    // ---- CPU side ----
    wire          a_in  = ({16'd0, fb_adr} < WORDS);
    wire          a_acc = fb_cyc && !fb_ack;
    wire [AW-1:0] a_adr = fb_adr;
    reg           a_in_q = 1'b0;
    initial fb_ack = 1'b0;
    always @(posedge clk) begin
        fb_ack <= a_acc;
        a_in_q <= a_in;
    end

    // ---- head side: request -> address (E1) -> RAM (E2) -> field (E3) ----
    reg  [1:0]  in_bank;
    reg  [21:0] in_addr;
    integer     b;
    always @(*) begin
        in_bank = 2'd0;
        for (b = 3; b >= 0; b = b - 1) if (req[b]) in_bank = b[1:0];
        in_addr = {req_row[in_bank*12 +: 12], req_col[in_bank*8 +: 8], in_bank};
    end
    wire [21:0] b_word = (BPP == 1) ? {4'd0, in_addr[21:4]} : {3'd0, in_addr[21:3]};
    reg  [AW-1:0] b_adr = {AW{1'b0}};
    reg  [3:0]  fld1 = 4'd0, fld2 = 4'd0;
    reg  [1:0]  bk1 = 2'd0, bk2 = 2'd0;
    reg         v1 = 1'b0, v2 = 1'b0, in1 = 1'b0, in2 = 1'b0;
    wire [31:0] b_q;
    initial begin rdy = 4'd0; rdata = 16'd0; rvalid = 1'b0; rbank = 2'd0; end

    always @(posedge vclk) begin
        rdy    <= en ? 4'b1111 : 4'b0000;
        v1     <= |req;
        bk1    <= in_bank;
        fld1   <= (BPP == 1) ? in_addr[3:0] : {1'b0, in_addr[2:0]};
        in1    <= ({10'd0, b_word} < WORDS);
        b_adr  <= b_word[AW-1:0];
        v2 <= v1; bk2 <= bk1; fld2 <= fld1; in2 <= in1;
        rvalid <= v2;
        rbank  <= bk2;
        if (!in2)            rdata <= 16'd0;
        else if (BPP == 1) begin : one
            reg [1:0] f; f = b_q[{fld2, 1'b0} +: 2];
            rdata <= {7'd0, f[1], 7'd0, f[0]};
        end else begin : two
            reg [3:0] f; f = b_q[{fld2[2:0], 2'b00} +: 4];
            rdata <= {6'd0, f[3:2], 6'd0, f[1:0]};
        end
    end

    // ---- storage: four byte lanes, port A read/write on clk, port B read on vclk ----
    wire [31:0] a_q;
    genvar k;
    generate for (k = 0; k < 4; k = k + 1) begin : lane
        reg [7:0] m [0:WORDS-1];
        reg [7:0] qa = 8'd0, qb = 8'd0;
        integer   i;
        initial for (i = 0; i < WORDS; i = i + 1) m[i] = 8'd0;
        always @(posedge clk) begin
            if (a_acc && fb_we && fb_sel[k] && a_in) m[a_adr] <= fb_dat_w[8*k +: 8];
            qa <= m[a_adr];
        end
        always @(posedge vclk) qb <= m[b_adr];
        assign a_q[8*k +: 8] = qa;
        assign b_q[8*k +: 8] = qb;
    end endgenerate
    assign fb_dat_r = a_in_q ? a_q : 32'd0;
endmodule
`default_nettype wire
