/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_clk_settle.v -- hold the PLL in reset until the clock on Y13 has settled at 125 MHz.
//
// Y13 carries the 88E1119R's CLK125 output: 25 MHz while GMII_RST_N (R11) is low, 125 MHz once it is high
//.  The design drives R11 high from configuration onward, so the clock changes rate at an
// unknown time after the design starts.  A PLL released on the 25 MHz clock, or during the change, would lock to the
// wrong rate or lose lock.  This block measures Y13 against an independent clock and releases the PLL only after
// the rate has been 125 MHz for SETTLE_WIN consecutive measurement windows.
//
// REFERENCE.  CFGMCLK of STARTUP_SPARTAN6: the configuration oscillator, nominally 50 MHz, kept running after
// configuration because it is connected (UG380 v2.11 p. 16; Table 4-3, CFGMCLK).
// Its tolerance is taken as +/-50 % (UG380 p. 15, the FMCCKTOL example).
//
// MEASUREMENT.  Y13 divided by 16 in its own domain; the divided clock's rising edges counted in a window of 1024
// CFGMCLK cycles.  Expected counts: 125 MHz -> 160 at 50 MHz reference, 107..320 over the reference tolerance;
// 25 MHz -> 32, 21..64.  THRESH = 80 separates the two with margin on both sides.
//
// FAIL-SAFE.  If the reference does not run, a counter in the Y13 domain releases the PLL after 2^TIMEOUT_LOG2 Y13
// cycles (2^24: 134 ms at 125 MHz, 671 ms at 25 MHz).  Until then, a window that fails after release re-asserts
// the PLL reset.
`timescale 1ns / 1ps
`default_nettype none

module g2_clk_settle #(
    parameter WIN_LOG2     = 10,
    parameter THRESH       = 80,
    parameter SETTLE_WIN   = 16,
    parameter TIMEOUT_LOG2 = 24
) (
    input  wire        ref_clk,            // CFGMCLK
    input  wire        y13_clk,            // Y13 through IBUFG/BUFIO2/BUFG
    output reg         pll_rst = 1'b1,     // ref_clk domain
    output reg  [7:0]  last_count = 8'd0,  // the last window's count (saturating at 255), ref_clk domain
    output reg  [4:0]  good_wins = 5'd0
);
    // Y13 domain
    reg [3:0]              ydiv = 4'd0;
    reg [TIMEOUT_LOG2:0]   yto  = 0;
    always @(posedge y13_clk) begin
        ydiv <= ydiv + 4'd1;
        if (!yto[TIMEOUT_LOG2]) yto <= yto + 1'b1;
    end

    // reference domain
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [2:0] d_s  = 3'b000;
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] to_s = 2'b00;
    reg [WIN_LOG2-1:0] win = 0;
    reg [8:0]          cnt = 9'd0;
    always @(posedge ref_clk) begin
        d_s  <= {d_s[1:0], ydiv[3]};
        to_s <= {to_s[0], yto[TIMEOUT_LOG2]};
        win  <= win + 1'b1;
        if (&win) begin
            last_count <= cnt[8] ? 8'hFF : cnt[7:0];
            cnt <= {8'd0, d_s[2:1] == 2'b01};
            if (cnt >= THRESH) begin if (good_wins != 5'd31) good_wins <= good_wins + 5'd1; end
            else good_wins <= 5'd0;
        end else if (d_s[2:1] == 2'b01 && !cnt[8]) cnt <= cnt + 9'd1;
`ifdef G2_FAULT_NOSETTLE
        pll_rst <= 1'b0;                                           // planted: PLL released at once
`else
        pll_rst <= !((good_wins >= SETTLE_WIN) || to_s[1]);
`endif
    end
endmodule

`default_nettype wire
