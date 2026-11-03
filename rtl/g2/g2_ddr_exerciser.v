/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_ddr_exerciser -- whole-chip DDR2 test on one MCB user port (32-bit, bidirectional), independent of mcb_wb and the
// CPU.  One pass: write every 32-word block of the chip (64 MB, 524,288 blocks), then read every block back and
// compare.  Each word carries its own word address, so aliasing between addresses is detected; odd passes write the
// complement, so every data bit is tested at both levels.  Word value = {8'hA5 ^ pass[7:0], word address[23:0]},
// inverted on odd passes.
//   en      run while high (with go); when it falls the port is drained and the state returns to idle
//   passes  completed passes (saturating); errs  mismatching words (saturating); bad  sticky mismatch
`default_nettype none
module g2_ddr_exerciser #(
    parameter integer BLK_BITS = 19                 // 2^19 blocks of 128 bytes = 64 MB
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        go,                          // calibration done
    input  wire        en,
    output reg         cmd_en,
    output reg  [2:0]  cmd_instr,
    output reg  [5:0]  cmd_bl,
    output reg  [29:0] cmd_byte_addr,
    input  wire        cmd_full,
    output reg         wr_en,
    output reg  [31:0] wr_data,
    input  wire        wr_full,
    output wire        rd_en,
    input  wire [31:0] rd_data,
    input  wire        rd_empty,
    output reg         bad,
    output reg  [15:0] passes,
    output reg  [15:0] errs,
    output wire        running
);
    localparam [2:0] S_IDLE = 3'd0, S_FILL = 3'd1, S_WCMD = 3'd2, S_RCMD = 3'd3, S_DRAIN = 3'd4, S_STOP = 3'd5;
    reg [2:0]          s;
    reg [4:0]          qn;
    reg [BLK_BITS-1:0] blk;
    reg                rphase;                      // 0: writing the chip, 1: reading it back
    wire [23:0] waddr = {blk, qn} & 24'hFFFFFF;
    wire [31:0] exp   = {8'hA5 ^ passes[7:0], waddr} ^ {32{passes[0]}};
    wire        last_blk = &blk;
    assign rd_en   = ((s == S_DRAIN) | (s == S_STOP) | (s == S_IDLE)) & ~rd_empty;
    assign running = (s != S_IDLE) & (s != S_STOP);
    always @(posedge clk) begin
        cmd_en <= 1'b0; wr_en <= 1'b0;
        if (rst) begin
            s <= S_IDLE; qn <= 5'd0; blk <= {BLK_BITS{1'b0}}; rphase <= 1'b0;
            bad <= 1'b0; passes <= 16'd0; errs <= 16'd0;
        end else case (s)
            S_IDLE:  if (go && en) begin blk <= {BLK_BITS{1'b0}}; qn <= 5'd0; rphase <= 1'b0; s <= S_FILL; end
            S_FILL:  if (!wr_full) begin                 // a started block is always completed
                         wr_en <= 1'b1; wr_data <= exp; qn <= qn + 1'b1;
                         if (qn == 5'd31) s <= S_WCMD;
                     end
            S_WCMD:  if (!cmd_full) begin
                         cmd_en <= 1'b1; cmd_instr <= 3'b000; cmd_bl <= 6'd31; cmd_byte_addr <= {blk, 7'd0};
                         blk <= blk + 1'b1;
                         if (last_blk) begin rphase <= 1'b1; s <= S_RCMD; end else s <= S_FILL;
                     end
            S_RCMD:  if (!en) s <= S_STOP;
                     else if (!cmd_full) begin
                         cmd_en <= 1'b1; cmd_instr <= 3'b001; cmd_bl <= 6'd31; cmd_byte_addr <= {blk, 7'd0}; s <= S_DRAIN;
                     end
            S_DRAIN: if (!rd_empty) begin
                         if (rd_data != exp) begin bad <= 1'b1; if (~&errs) errs <= errs + 1'b1; end
                         qn <= qn + 1'b1;
                         if (qn == 5'd31) begin
                             blk <= blk + 1'b1;
                             if (last_blk) begin
                                 if (~&passes) passes <= passes + 1'b1;
                                 rphase <= 1'b0; s <= en ? S_FILL : S_STOP;
                             end else s <= S_RCMD;
                         end
                     end
            S_STOP:  if (rd_empty && !en) s <= S_IDLE;     // drain whatever the port still returns
            default: s <= S_IDLE;
        endcase
    end
    wire _unused = &{1'b0, rphase, 1'b0};
endmodule
`default_nettype wire
