/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_video_clk.v -- the two clocks of the G2 video head, from the 125 MHz reference.  Spartan-6 primitives;
// synthesis only (the gates generate these clocks in the bench, with the DCM_CLKGEN modelled).
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// | Output | Source                                 | Frequency                    | Use                              |
// |--------|----------------------------------------|------------------------------|----------------------------------|
// | vclk   | PLL_BASE: 125 / 5 x 32 = VCO 800 MHz, / 10 | 80.000 MHz                | vga_scan fclk, MCB video port    |
// | oclk   | DCM_CLKGEN: 125 x M / D, M/D programmed | 50 / 80 / 130 MHz (modes 0/1/2) | ch7301_out, CH7301C pins       |
//
// PLL: PFD 25 MHz, VCO 800 MHz (DS162: VCO 400..1000 MHz at -2).  COMPENSATION "INTERNAL": CLKFBOUT feeds
// CLKFBIN directly; vclk has no phase requirement against the input.
// DCM_CLKGEN: static M/D = 2/5 (mode 0, the register page's reset mode); vid_dcm_prog.v reprograms it through
// PROGCLK/PROGEN/PROGDATA.  CLKFX_MD_MAX = 1.04 (26/25, 130 MHz) so timing is analysed at the fastest output.
// PROGCLK is the register clock; UG382 v1.10 requires a global buffer driving PROGCLK to be one of
// BUFGMUX_X2Y1..X2Y4, X3Y5..X3Y8 (a placement constraint for the board's clock buffer).
//
// `rst' must be held until the 125 MHz reference is present: the PHY supplies 25 MHz on Y13 while GMII_RST_N is
// low.
`timescale 1ns / 1ps
`default_nettype none
module g2_video_clk (
    input  wire clk125,          // 125 MHz from a global buffer (Y13 through IBUFG/BUFG, board top)
    input  wire rst,             // active high, held until clk125 is at 125 MHz
    input  wire progclk,         // = g2_video.clk
    input  wire progen,
    input  wire progdata,
    output wire progdone,
    output wire vclk,            // 80 MHz, BUFG
    output wire vclk_locked,
    output wire oclk,            // 2 x pixel clock, BUFG
    output wire oclk_locked
);
    wire fb, vclk_u, oclk_u;
    PLL_BASE #(
        .BANDWIDTH("OPTIMIZED"), .CLK_FEEDBACK("CLKFBOUT"), .COMPENSATION("INTERNAL"),
        .DIVCLK_DIVIDE(5), .CLKFBOUT_MULT(32), .CLKFBOUT_PHASE(0.0),
        .CLKOUT0_DIVIDE(10), .CLKOUT0_PHASE(0.0), .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKIN_PERIOD(8.0), .REF_JITTER(0.010)
    ) u_pll (
        .CLKFBOUT(fb), .CLKOUT0(vclk_u), .CLKOUT1(), .CLKOUT2(), .CLKOUT3(), .CLKOUT4(), .CLKOUT5(),
        .LOCKED(vclk_locked), .CLKFBIN(fb), .CLKIN(clk125), .RST(rst));
    BUFG u_vbuf (.I(vclk_u), .O(vclk));

    DCM_CLKGEN #(
        .CLKFX_MULTIPLY(2), .CLKFX_DIVIDE(5), .CLKFXDV_DIVIDE(2), .CLKFX_MD_MAX(1.04),
        .CLKIN_PERIOD(8.0), .SPREAD_SPECTRUM("NONE"), .STARTUP_WAIT("FALSE")
    ) u_dcm (
        .CLKFX(oclk_u), .CLKFX180(), .CLKFXDV(), .LOCKED(oclk_locked), .PROGDONE(progdone), .STATUS(),
        .CLKIN(clk125), .FREEZEDCM(1'b0), .PROGCLK(progclk), .PROGDATA(progdata), .PROGEN(progen), .RST(rst));
    BUFG u_obuf (.I(oclk_u), .O(oclk));
endmodule
`default_nettype wire
