/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vid_dcm_prog.v -- programs the DCM_CLKGEN that makes the CH7301C output clock (2 x the pixel clock) for the
// video mode in force.
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// 1 PROTOCOL (Spartan-6 FPGA Clocking Resources, UG382 v1.10, "DCM_CLKGEN ... Dynamic Frequency Synthesis",
//   figure 2-14), all on PROGCLK = clk:
//     LoadD  PROGEN high for exactly 10 clocks, PROGDATA = 1, 0, then D-1 LSB first
//     gap    PROGEN low, at least 2 clocks (3 here)
//     LoadM  PROGEN high for exactly 10 clocks, PROGDATA = 1, 1, then M-1 LSB first
//     gap    PROGEN low, at least 1 clock (2 here)
//     GO     PROGEN high for exactly 1 clock, PROGDATA = 0
//     then PROGDONE high, then LOCKED high: CLKFX is at the new frequency.
//   PROGCLK on a global buffer must come from one of BUFGMUX_X2Y1..X2Y4, X3Y5..X3Y8 (UG382, DCM_CLKGEN ports).
//
// 2 FREQUENCIES.  CLKIN = 125 MHz; CLKFX = 125 x M / D, exact:
//     mode 0  640x480    M  2 / D  5   50 MHz  = 2 x 25.000 MHz
//     mode 1  800x600    M 16 / D 25   80 MHz  = 2 x 40.000 MHz
//     mode 2  1024x768   M 26 / D 25  130 MHz  = 2 x 65.000 MHz
//     mode 4  320x240p   M  3 / D  7  53.571 MHz = 8 x 6.696 MHz: four XCLK cycles per pixel (ch7301_out rep4), XCLK
//             26.79 MHz inside the CH7301C's 25..165 MHz range; the raster keeps its 15.7 kHz line rate
//     mode 5  640x480 at 60.10 Hz (520 lines): mode 0's frequency
//     mode 6  320x240p at 60.09 Hz (427 x 261): mode 4's frequency and repetition
//     modes 3 and 7  640x240p at 59.72 Hz (856 x 262) and 640x288p at 50.15 Hz (856 x 312): mode 4's frequency,
//             13.393 MHz pixels sent twice each (ch7301_out rep2), the same XCLK
//   The static attributes of the DCM_CLKGEN (rtl/vga/g2_video_clk.v) are mode 0's, the mode the register page
//   comes out of reset in, so no programming is needed to reach it.
//
// 3 `ready' is high while the DCM is locked at the frequency of `mode' and no programming is in progress.  A
//   programming sequence that does not see PROGDONE and LOCKED within TIMEOUT clocks sets `fault' and is
//   started again.
`timescale 1ns / 1ps
`default_nettype none
module vid_dcm_prog #(
    parameter integer TIMEOUT = 1 << 21        // clocks: 41.9 ms at 50 MHz
) (
    input  wire       clk,                     // PROGCLK
    input  wire       rst,
    input  wire [2:0] mode,
    output reg        progen,
    output reg        progdata,
    input  wire       progdone,
    input  wire       locked,                  // DCM LOCKED, any domain
    output wire       ready,
    output wire       supported,
    output reg        fault
);
    function sup; input [2:0] m; sup = 1'b1; endfunction
    function [7:0] mm1; input [2:0] m; case (m) 3'd1: mm1 = 8'd15; 3'd2: mm1 = 8'd25; 3'd3, 3'd4, 3'd6, 3'd7: mm1 = 8'd2; default: mm1 = 8'd1;  endcase endfunction
    function [7:0] dm1; input [2:0] m; case (m) 3'd1: dm1 = 8'd24; 3'd2: dm1 = 8'd24; 3'd3, 3'd4, 3'd6, 3'd7: dm1 = 8'd6; default: dm1 = 8'd4;  endcase endfunction

    localparam [2:0] S_IDLE = 3'd0, S_LD = 3'd1, S_GAP1 = 3'd2, S_LM = 3'd3, S_GAP2 = 3'd4, S_GO = 3'd5, S_WAIT = 3'd6;

    reg  [2:0]  st = S_IDLE;
    reg  [2:0]  pmode = 3'd0;                  // the mode the DCM is programmed for
    reg  [2:0]  tmode = 3'd0;                  // the mode being programmed
    reg  [9:0]  sh = 10'd0;
    reg  [3:0]  n = 4'd0;
    reg  [21:0] to = 22'd0;
    reg         lk1 = 1'b0, lk2 = 1'b0, pd1 = 1'b0;
    initial begin progen = 1'b0; progdata = 1'b0; fault = 1'b0; end

    assign supported = sup(mode);
    assign ready     = (st == S_IDLE) && (pmode == mode) && sup(mode) && lk2;

    always @(posedge clk) begin
        lk1 <= locked; lk2 <= lk1; pd1 <= progdone;
        progen <= 1'b0; progdata <= 1'b0;
        if (rst) begin
            st <= S_IDLE; pmode <= 3'd0; fault <= 1'b0;
        end else case (st)
        S_IDLE: if (sup(mode) && mode != pmode) begin
                    tmode <= mode;
                    sh    <= {dm1(mode), 2'b01};           // shifted out LSB first: 1, 0, D-1[0..7]
                    n     <= 4'd9;
                    pmode <= 3'd7;                          // nothing valid until LOCKED at the new frequency
                    st    <= S_LD;
                end
        S_LD:   begin
                    progen <= 1'b1; progdata <= sh[0]; sh <= sh >> 1;
                    if (n == 4'd0) begin n <= 4'd2; st <= S_GAP1; end else n <= n - 4'd1;
                end
        S_GAP1: if (n == 4'd0) begin sh <= {mm1(tmode), 2'b11}; n <= 4'd9; st <= S_LM; end
                else n <= n - 4'd1;
        S_LM:   begin
                    progen <= 1'b1; progdata <= sh[0]; sh <= sh >> 1;
                    if (n == 4'd0) begin n <= 4'd1; st <= S_GAP2; end else n <= n - 4'd1;
                end
        S_GAP2: if (n == 4'd0) st <= S_GO; else n <= n - 4'd1;
        S_GO:   begin progen <= 1'b1; progdata <= 1'b0; to <= 22'd0; st <= S_WAIT; end
        S_WAIT: begin
                    to <= to + 22'd1;
                    if (to > 22'd4 && pd1 && lk2) begin pmode <= tmode; st <= S_IDLE; end
                    else if (to == TIMEOUT[21:0]) begin fault <= 1'b1; pmode <= 3'd7; st <= S_IDLE; end
                end
        default: st <= S_IDLE;
        endcase
    end
endmodule
`default_nettype wire
