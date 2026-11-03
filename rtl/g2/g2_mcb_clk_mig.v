/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_mcb_clk_mig -- the MCB clocks and reset from Xilinx MIG 3.92's own infrastructure module (rtl/g2/mig/common/
// infrastructure.v as g2_mig_infrastructure.v, with the board IBUFG as its input as in crwulff/panologic-g2
// spinal/coregen/remove_clkbuffer.patch), at 3200 ps: 125 MHz in, PLL multiplier 5 (fix_clocking.patch), 2x clocks
// 625 MHz, DRP clock 78.125 MHz, user clock clk0 = 625 / 16 = 39.0625 MHz, which is clk_sys.  Port list of g2_mcb_clk.
`default_nettype none
module g2_mcb_clk_mig (
    input  wire clkin, input wire rst,
    output wire sysclk_2x, output wire sysclk_2x_180, output wire pll_ce_0, output wire pll_ce_90, output wire pll_lock,
    output wire mcb_drp_clk, output wire async_rst, output wire clk_sys, output wire clk_125, output wire pll_locked,
    output wire clk_icap
);
    wire rst0;
    g2_mig_infrastructure #(
        .C_INCLK_PERIOD(8000), .C_RST_ACT_LOW(0), .C_INPUT_CLK_TYPE("SINGLE_ENDED"),
        .C_CLKOUT0_DIVIDE(1), .C_CLKOUT1_DIVIDE(1), .C_CLKOUT2_DIVIDE(16), .C_CLKOUT3_DIVIDE(8),
        .C_CLKFBOUT_MULT(5), .C_DIVCLK_DIVIDE(1)
    ) u_infra (
        .sys_clk_p(1'b0), .sys_clk_n(1'b0), .sys_clk(clkin), .sys_rst_i(rst),
        .clk0(clk_sys), .rst0(rst0), .async_rst(async_rst), .sysclk_2x(sysclk_2x), .sysclk_2x_180(sysclk_2x_180),
        .mcb_drp_clk(mcb_drp_clk), .pll_ce_0(pll_ce_0), .pll_ce_90(pll_ce_90), .pll_lock(pll_lock));
    assign pll_locked = pll_lock;
    assign clk_125    = clk_sys;                  // only the USB host uses clk_125; not built here
    // ICAP clock: clk_sys / 4 = 9.77 MHz (12 MHz maximum at -2)
    reg [1:0] icap_div = 2'd0;
    always @(posedge clk_sys) icap_div <= icap_div + 1'b1;
    BUFG u_bufg_icap (.I(icap_div[1]), .O(clk_icap));
    wire _unused = &{1'b0, rst0, 1'b0};
endmodule
`default_nettype wire
