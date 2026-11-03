/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// nco_raster.v -- rev-2 raster timing: ONE fixed clock, every scan rate a
// number: a fractional phase accumulator with 64 bits of fixed-point
// precision for arbitrary scan rates.
//
// The engine runs entirely in one fast domain (fclk -- the 100 MHz SDRAM
// clock, or any fixed clock).  A 64-bit phase accumulator stretches each
// PIXEL over a fractional number of fclk cycles: pix_inc is a Q32.32
// cycles-per-pixel reciprocal (pixels-per-cycle), added every fclk; each
// integer overflow emits one pixel strobe (pix_en).  Horizontal structure
// (active/fp/sync/bp) counts in PIXELS, vertical counts in LINES, so the
// average line and frame rates are exact to the accumulator's precision
// (2^-32 pixels/cycle ~ micro-hertz) while every EDGE lands on the fclk
// grid (+/- one period -- invisible to a CRT's line-rate PLL, see the
// bench doctrine note in the 4x artifact).
//
// All geometry is a REGISTER FILE loaded over the monitor (quasi-static:
// change only while blanked/held in reset by the loader).  No PLL, no
// clock mux, no CLOCK_27, no TD_RESET -- the whole no-signal saga's
// dramatis personae are simply gone.
//
//   pix_inc  Q32.32 pixels per fclk cycle (e.g. 40 MHz pixel rate at
//            fclk=100 MHz -> 0.4 * 2^32 = 0x0000_0000_6666_6666)
//   h_*      pixels;  v_* lines;  *_pol: 1 = active-low sync
//
// The DAC clock in this scheme is fclk itself (VGA_CLK = fclk); RGB holds
// each pixel's value for every fclk cycle of that pixel's duration, so the
// DAC just oversamples the staircase -- the analog waveform is identical.
module nco_raster (
    input  wire        fclk,
    input  wire        rst_n,

    // geometry registers (quasi-static)
    input  wire [63:0] pix_inc,       // Q32.32 pixels advanced per fclk
    input  wire [11:0] h_act, h_fp, h_sy, h_bp,     // pixels
    input  wire [11:0] v_act, v_fp, v_sy, v_bp,     // lines
    input  wire        hs_pol, vs_pol,              // 1 = active low

    output reg         pix_en,        // 1 fclk strobe per new pixel
    output reg         hs, vs, blank_n,
    output reg  [11:0] x, y,          // active-area coordinates
    output reg         frame,         // strobe entering vblank
    output reg         line           // strobe at end of each active line
);
    /* THE GEOMETRY'S DERIVED CONSTANTS ARE REGISTERS: summed combinationally every clock,
     * h_act+h_fp+h_sy+h_bp sat in front of the counter compares and TRCE measured g_hact -> vc at 14.1 ns over 8
     * levels against the 10 ns clock (498 endpoints).  The inputs are static while a frame scans (vga_scan registers
     * them from the mode), so one clock of latency on these sums changes nothing; every compare below is now a
     * 12-bit test against a register. */
    reg  [11:0] h_tot_m1, v_tot_m1, h_act_m1, v_act_m1, hs_b, hs_e, vs_b, vs_e;
    always @(posedge fclk) begin
        h_tot_m1 <= h_act + h_fp + h_sy + h_bp - 12'd1;
        v_tot_m1 <= v_act + v_fp + v_sy + v_bp - 12'd1;
        h_act_m1 <= h_act - 12'd1;
        v_act_m1 <= v_act - 12'd1;
        hs_b     <= h_act + h_fp;
        hs_e     <= h_act + h_fp + h_sy;
        vs_b     <= v_act + v_fp;
        vs_e     <= v_act + v_fp + v_sy;
    end

    reg  [31:0] acc;                  // fractional pixel phase
    reg  [11:0] hc, vc;

    wire [32:0] acc_n = {1'b0, acc} + {1'b0, pix_inc[31:0]};
    // integer pixel steps this cycle: pix_inc integer part is 0 for any
    // pixel rate below fclk (the only supported regime: fclk >= pixel rate)
    wire        step  = acc_n[32] | (|pix_inc[63:32]);

    /* >= and not ==: the geometry may shrink while a frame scans (a mode change from 1024x768 to 320x240 is applied
     * three fclk after the register write, not at a frame boundary), and a counter already past the new total must
     * wrap at the next pixel.  With == it ran to 4095 first: 3,290 lines of 426 pixels at 6.696 MHz, 209 ms of no
     * frame at the pins after the switch into mode 4 (g2-vga-timing +seq, timeout at 400 ms). */
    wire h_last  = (hc >= h_tot_m1);
    wire v_last  = (vc >= v_tot_m1);
    wire h_active = (hc < h_act);
    wire v_active = (vc < v_act);
    wire hs_raw = (hc >= hs_b) && (hc < hs_e);
    wire vs_raw = (vc >= vs_b) && (vc < vs_e);

    always @(posedge fclk or negedge rst_n) begin
        if (!rst_n) begin
            acc <= 32'd0; hc <= 12'd0; vc <= 12'd0;
            pix_en <= 1'b0; hs <= 1'b0; vs <= 1'b0; blank_n <= 1'b0;
            x <= 12'd0; y <= 12'd0; frame <= 1'b0; line <= 1'b0;
        end else begin
            acc    <= acc_n[31:0];
            pix_en <= step;
            frame  <= 1'b0;
            line   <= 1'b0;
            if (step) begin
                if (h_last) begin
                    hc <= 12'd0;
                    if (v_last) vc <= 12'd0;
                    else        vc <= vc + 12'd1;
                    frame <= (vc == v_act_m1);
                end else
                    hc <= hc + 12'd1;
                line <= (hc == h_act_m1) && v_active;
            end
            // outputs re-evaluated every fclk (they only move on pixel
            // boundaries anyway; registering here keeps them glitch-free)
            hs      <= hs_pol ? ~hs_raw : hs_raw;
            vs      <= vs_pol ? ~vs_raw : vs_raw;
            blank_n <= h_active && v_active;
            x       <= h_active ? hc : 12'd0;
            y       <= v_active ? vc : 12'd0;
        end
    end
endmodule
