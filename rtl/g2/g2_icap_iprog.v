/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_icap_iprog.v -- IPROG through ICAP_SPARTAN6: reconfigure the FPGA from a flash address, with the golden image
// at 0x040000 as the fallback.
//
// SEQUENCE (UG380 v2.11, Table 7-1, 16-bit words, written with CE = 0 and WRITE = 0):
//   FFFF  dummy          AA99 5566  sync
//   3261  GENERAL1  <- addr[15:0]                 3281  GENERAL2  <- {READ opcode 0x03, addr[23:16]}
//   32A1  GENERAL3  <- 0x0000                     32C1  GENERAL4  <- {0x03, 0x04}   (golden 0x040000)
//   30A1  CMD       <- 000E (IPROG)               2000  NOOP x 4
// The ICAP data bus takes SelectMAP x16 bit order (UG380 Table 7-1 note 1, Table 2-5): the bits of each byte are
// reversed.  The words above are written in register order and reversed per byte at the ICAP input.
//
// The clock of this module must not exceed the ICAP_SPARTAN6 limit: trce reports a minimum CLK period of 83.333 ns
// (12 MHz) for the -2 speed grade.  The top supplies 10 MHz.  `req_tog' is a toggle from another clock domain, synchronised here; each change starts one sequence.
// `addr' is held static by the requester from before `req_tog' changes until `done_tog' changes.
`timescale 1ns / 1ps
`default_nettype none

module g2_icap_iprog (
    input  wire        clk,                // 10 MHz; 12 MHz maximum at -2
    input  wire        req_tog,            // toggle, any domain
    input  wire [23:0] addr,               // quasi-static
    output reg         done_tog = 1'b0     // toggles when a complete sequence has been written
);
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [2:0] req_s = 3'b000;
    reg  [4:0]  st = 5'd0;
    reg         run = 1'b0;
    reg  [23:0] a = 24'd0;
    reg  [15:0] w;
    reg         icap_ce_n = 1'b1, icap_wr_n = 1'b1;
    reg  [15:0] icap_d = 16'hFFFF;

    always @(*) begin
        case (st)
        5'd0:  w = 16'hFFFF;
        5'd1:  w = 16'hAA99;
        5'd2:  w = 16'h5566;
        5'd3:  w = 16'h3261;
        5'd4:  w = a[15:0];
        5'd5:  w = 16'h3281;
        5'd6:  w = {8'h03, a[23:16]};
        5'd7:  w = 16'h32A1;
        5'd8:  w = 16'h0000;
        5'd9:  w = 16'h32C1;
        5'd10: w = 16'h0304;
        5'd11: w = 16'h30A1;
        5'd12: w = 16'h000E;
        default: w = 16'h2000;             // 13..16: NOOP
        endcase
    end

    function [15:0] swap8; input [15:0] v; integer k; begin
        for (k = 0; k < 8; k = k + 1) begin swap8[k] = v[7-k]; swap8[8+k] = v[15-k]; end
    end endfunction

    always @(posedge clk) begin
        req_s <= {req_s[1:0], req_tog};
        if (!run) begin
            icap_ce_n <= 1'b1; icap_wr_n <= 1'b1; icap_d <= 16'hFFFF; st <= 5'd0;
            if (req_s[2] ^ req_s[1]) begin run <= 1'b1; a <= addr; end
        end else begin
`ifdef G2_FAULT_NOSWAP
            icap_ce_n <= 1'b0; icap_wr_n <= 1'b0; icap_d <= w;                 // planted: SelectMAP bit order ignored
`else
            icap_ce_n <= 1'b0; icap_wr_n <= 1'b0; icap_d <= swap8(w);
`endif
            if (st == 5'd16) begin run <= 1'b0; done_tog <= ~done_tog; end
            else st <= st + 5'd1;
        end
    end

    ICAP_SPARTAN6 #(.DEVICE_ID(32'h04011093), .SIM_CFG_FILE_NAME("NONE")) u_icap (
        .BUSY(), .O(),
        .CE(icap_ce_n), .CLK(clk), .I(icap_d), .WRITE(icap_wr_n));
endmodule

`default_nettype wire
