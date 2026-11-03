/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vga_timing.v -- the two output timings the ADV7123 will ever see.
//
// The scan-out design deliberately supports exactly TWO signal modes, both
// from one PLL (VCO 840 MHz: c/21 = 40.000, c/6 = 140.000 -- both exact):
//
//   mode 0: 800x600@60   VESA DMT, 40.000 MHz pixel clock
//           h: 800 act / 40 fp / 128 sync / 88 bp   = 1056 total, HS positive
//           v: 600 act /  1 fp /   4 sync / 23 bp   =  628 total, VS positive
//   mode 1: 1600x1200 CVT reduced-blanking at 140.000 MHz  (~64.3 Hz)
//           h: 1600 act / 48 fp / 32 sync / 80 bp   = 1760 total, HS positive
//           v: 1200 act /  3 fp /  4 sync / 30 bp   = 1237 total, VS NEGATIVE
//           (CVT-RB sync polarity convention: +h/-v)
//
// Source images of any size upscale to these in the scan-out DDA; the CRT
// never has to lock anything else.  `mode` is sampled only during vertical
// blanking so a mid-frame SW[0] flip cannot tear a frame in progress.
//
// All outputs are registered in the pixel-clock domain.  blank_n is the
// ADV7123's active-low composite blank; the DAC latches RGB on clock rising
// edges only while blank_n is high.
module vga_timing (
    input  wire        pixclk,
    input  wire        rst_n,
    input  wire        mode,          // 0 = 800x600, 1 = 1600x1200 (SW[0])

    output reg         hs,
    output reg         vs,
    output reg         blank_n,
    output reg         active,        // combinational-timing equivalent of
                                      // blank_n, unregistered name kept off
    output reg  [10:0] x,             // active-area pixel coordinate
    output reg  [10:0] y,             // active-area line coordinate
    output reg         frame,         // 1-clk strobe at start of v-blank
    output reg         line,          // 1-clk strobe at end of each active line
    output reg         mode_q         // the latched mode actually being scanned
);
    // horizontal/vertical structure per mode (act, fp, sync, bp)
`ifdef VGA_FORCE_640
    // diagnostic build: industry-standard 640x480@60 from CLOCK_50/2 --
    // no PLL, no clock mux; if THIS syncs, the fault is in the clock path
    wire [11:0] h_act = 12'd640;
    wire [11:0] h_fp  = 12'd16;
    wire [11:0] h_sy  = 12'd96;
    wire [11:0] h_tot = 12'd800;
    wire [11:0] v_act = 12'd480;
    wire [11:0] v_fp  = 12'd10;
    wire [11:0] v_sy  = 12'd2;
    wire [11:0] v_tot = 12'd525;
    wire        hs_neg = 1'b1;              // 640x480 DMT: BOTH syncs negative
    wire        vs_neg = 1'b1;
`else
    wire [11:0] h_act = mode_q ? 12'd1600 : 12'd800;
    wire [11:0] h_fp  = mode_q ? 12'd48   : 12'd40;
    wire [11:0] h_sy  = mode_q ? 12'd32   : 12'd128;
    wire [11:0] h_tot = mode_q ? 12'd1760 : 12'd1056;
    wire [11:0] v_act = mode_q ? 12'd1200 : 12'd600;
    wire [11:0] v_fp  = mode_q ? 12'd3    : 12'd1;
    wire [11:0] v_sy  = mode_q ? 12'd4    : 12'd4;
    wire [11:0] v_tot = mode_q ? 12'd1237 : 12'd628;
    wire        hs_neg = 1'b0;
    wire        vs_neg = mode_q;            // CVT-RB: vsync negative
`endif

    reg [11:0] hc, vc;
    wire h_last = (hc == h_tot - 12'd1);
    wire v_last = (vc == v_tot - 12'd1);

    wire h_active = (hc < h_act);
    wire v_active = (vc < v_act);
    wire hs_raw = (hc >= h_act + h_fp) && (hc < h_act + h_fp + h_sy);
    wire vs_raw = (vc >= v_act + v_fp) && (vc < v_act + v_fp + v_sy);

    always @(posedge pixclk or negedge rst_n) begin
        if (!rst_n) begin
            hc <= 12'd0; vc <= 12'd0; mode_q <= 1'b0;
            hs <= 1'b0; vs <= 1'b1; blank_n <= 1'b0; active <= 1'b0;
            x <= 11'd0; y <= 11'd0; frame <= 1'b0; line <= 1'b0;
        end else begin
            if (h_last) begin
                hc <= 12'd0;
                if (v_last) begin
                    vc <= 12'd0;
                    mode_q <= mode;        // resample only at frame boundary
                end else
                    vc <= vc + 12'd1;
            end else
                hc <= hc + 12'd1;

            hs      <= hs_neg ? ~hs_raw : hs_raw;
            vs      <= vs_neg ? ~vs_raw : vs_raw;
            blank_n <= h_active && v_active;
            active  <= h_active && v_active;
            x       <= h_active ? hc[10:0] : 11'd0;
            y       <= v_active ? vc[10:0] : 11'd0;
            frame   <= h_last && (vc == v_act - 12'd1);       // entering vblank
            line    <= (hc == h_act - 12'd1) && v_active;     // active line done
        end
    end
endmodule
