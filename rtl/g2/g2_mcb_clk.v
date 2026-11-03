/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_mcb_clk.v -- clocking for one Spartan-6 MCB on the Pano Logic G2, from the 125 MHz CLK125 on Y13.
//
// Mike Wolak, EPROM Foundry Inc.
//
// One PLL_ADV and one BUFPLL_MCB.  The PLL is the same structure as the MIG 3.92 infrastructure
// (rtl/g2/mig/common/infrastructure.v) with three differences: the input is an already buffered
// clock (the board top owns the Y13 IBUFG), the multiplier and dividers are derived from the
// memory clock period for a 125 MHz input, and CLKOUT2/CLKOUT4/CLKOUT5 are offered to the rest of
// the design so that one PLL can serve the MCB and the system clock.  clk_icap is for ICAP_SPARTAN6,
// whose minimum CLK period at -2 is 83.333 ns (12 MHz) in trce.
//
//   Output           PLL output   MEMCLK_PERIOD = 8000 ps     MEMCLK_PERIOD = 3200 ps
//   ---------------  -----------  -------------------------   -------------------------
//   VCO                           500 MHz (M = 4)             625 MHz (M = 5)
//   sysclk_2x        CLKOUT0      250 MHz, 0 deg              625 MHz, 0 deg
//   sysclk_2x_180    CLKOUT1      250 MHz, 180 deg            625 MHz, 180 deg
//   clk_sys          CLKOUT2      50 MHz (/10)                52.083 MHz (/12)
//   mcb_drp_clk      CLKOUT3      62.5 MHz (/8)               78.125 MHz (/8)
//   clk_cpu          CLKOUT4      125 MHz (/4)                125 MHz (/5)
//   clk_icap         CLKOUT5      10 MHz (/50)                9.92 MHz (/63)
//
// The memory clock (DRAM CK) is sysclk_2x / 2: 125 MHz (250 Mb/s per pin) or 312.5 MHz
// (625 Mb/s per pin, the -2 MCB limit, DS162 Table 25).  At 3200 ps the system clock is not
// 50 MHz; a design that needs exactly 50 MHz at that rate takes it from a second PLL or DCM.
// VCO range of the Spartan-6 PLL at -2: 400-1000 MHz (DS162).
//
// CLK125 FACT: Y13 carries 25 MHz while the PHY's reset (GMII_RST_N, R11) is low and 125 MHz
// once it is high.  rst must be held high until Y13 runs at 125 MHz;
// after a change of input frequency the PLL is reset again.  pll_locked reports the PLL lock,
// pll_lock the BUFPLL_MCB lock that the MCB requires.
//
// async_rst and the power-up lock latch follow infrastructure.v: async_rst is held until the
// BUFPLL_MCB has locked once after rst.
//
// DUAL = 1 adds a second BUFPLL_MCB on the same PLL outputs, for a second MCB at the same memory
// clock (the *_b outputs); async_rst then waits for both BUFPLL_MCB locks.  On the xc6slx100-fgg484
// one PLL places and routes to the BUFPLL_MCBs of MCB1 and MCB3.
`timescale 1ns / 1ps
`default_nettype none

module g2_mcb_clk #(
    parameter integer MEMCLK_PERIOD = 8000,     // ps, DRAM CK period: 8000 (125 MHz) or 3200 (312.5 MHz)
    parameter integer CLKIN_PERIOD  = 8000,     // ps, the Y13 clock (125 MHz)
    parameter integer DUAL          = 0         // 1: second BUFPLL_MCB for a second MCB
) (
    input  wire clkin,                          // Y13 through the board's IBUFG (not through a BUFG)
    input  wire rst,                            // asynchronous, active high: PLL reset
    output wire sysclk_2x,
    output wire sysclk_2x_180,
    output wire pll_ce_0,
    output wire pll_ce_90,
    output wire pll_lock,                       // BUFPLL_MCB LOCK
    output wire mcb_drp_clk,                    // BUFGCE, runs once the PLL has locked
    output wire async_rst,                      // to memc_wrapper.async_rst
    output wire clk_sys,                        // BUFG of CLKOUT2
    output wire clk_sys_pll_o,                  // CLKOUT2 before its BUFG: a second, gated buffer of clk_sys
    output wire clk_cpu,                        // BUFG of CLKOUT4: the CPU domain, 75 MHz
    output wire pll_locked,                     // PLL LOCKED
    output wire clk_icap,                       // BUFG of CLKOUT5, <= 10 MHz
    output wire sysclk_2x_b,                    // DUAL = 1: the second MCB's clocks
    output wire sysclk_2x_180_b,
    output wire pll_ce_0_b,
    output wire pll_ce_90_b,
    output wire pll_lock_b
);
    // MEMCLK_PERIOD 8000: VCO 1000 MHz (125 x 8): 2x 250 (/4), clk_sys 50 (/20), drp 62.5 (/16), clk_cpu 66.67 (/15), icap 10 (/100).
    // (A 750 MHz VCO with clk_cpu 75 MHz, /10, missed timing in the full fabric: build 41fd52f, -0.45 and -1.72 ns.)
    // MEMCLK_PERIOD 3200: VCO 625 MHz (125 x 5): 2x 625 (/1), clk_sys 52.08 (/12), drp 78.1 (/8), clk_cpu 78.1 (/8), icap 9.9 (/63).
    localparam integer MULT    = (MEMCLK_PERIOD == 3200) ? 5 : 8;
    localparam integer DIV_2X  = (MEMCLK_PERIOD == 3200) ? 1 : 4;
    localparam integer DIV_SYS = (MEMCLK_PERIOD == 3200) ? 12 : 20;
    localparam integer DIV_DRP = (MEMCLK_PERIOD == 3200) ? 8 : 16;
    localparam integer DIV_CPU = (MEMCLK_PERIOD == 3200) ? 8 : 15;
    localparam integer DIV_ICAP = (MEMCLK_PERIOD == 3200) ? 63 : 100;
    localparam real    CLKIN_NS = CLKIN_PERIOD / 1000.0;

    wire clk_2x_0, clk_2x_180, clk_sys_pll, clk_drp_pll, clk_cpu_pll, clk_icap_pll, fb;
    wire locked;

    PLL_ADV #(
        .BANDWIDTH          ("OPTIMIZED"),
        .CLKIN1_PERIOD      (CLKIN_NS),
        .CLKIN2_PERIOD      (CLKIN_NS),
        .CLKOUT0_DIVIDE     (DIV_2X),
        .CLKOUT1_DIVIDE     (DIV_2X),
        .CLKOUT2_DIVIDE     (DIV_SYS),
        .CLKOUT3_DIVIDE     (DIV_DRP),
        .CLKOUT4_DIVIDE     (DIV_CPU),
        .CLKOUT5_DIVIDE     (DIV_ICAP),
        .CLKOUT0_PHASE      (0.000),
        .CLKOUT1_PHASE      (180.000),
        .CLKOUT2_PHASE      (0.000),
        .CLKOUT3_PHASE      (0.000),
        .CLKOUT4_PHASE      (0.000),
        .CLKOUT5_PHASE      (0.000),
        .CLKOUT0_DUTY_CYCLE (0.500),
        .CLKOUT1_DUTY_CYCLE (0.500),
        .CLKOUT2_DUTY_CYCLE (0.500),
        .CLKOUT3_DUTY_CYCLE (0.500),
        .CLKOUT4_DUTY_CYCLE (0.500),
        .CLKOUT5_DUTY_CYCLE (0.500),
        .SIM_DEVICE         ("SPARTAN6"),
        .COMPENSATION       ("INTERNAL"),
        .DIVCLK_DIVIDE      (1),
        .CLKFBOUT_MULT      (MULT),
        .CLKFBOUT_PHASE     (0.0),
        .REF_JITTER         (0.005000)
    ) u_pll (
        .CLKFBIN    (fb),
        .CLKINSEL   (1'b1),
        .CLKIN1     (clkin),
        .CLKIN2     (1'b0),
        .DADDR      (5'b0),
        .DCLK       (1'b0),
        .DEN        (1'b0),
        .DI         (16'b0),
        .DWE        (1'b0),
        .REL        (1'b0),
        .RST        (rst),
        .CLKFBDCM   (),
        .CLKFBOUT   (fb),
        .CLKOUTDCM0 (), .CLKOUTDCM1 (), .CLKOUTDCM2 (), .CLKOUTDCM3 (), .CLKOUTDCM4 (), .CLKOUTDCM5 (),
        .CLKOUT0    (clk_2x_0),
        .CLKOUT1    (clk_2x_180),
        .CLKOUT2    (clk_sys_pll),
        .CLKOUT3    (clk_drp_pll),
        .CLKOUT4    (clk_cpu_pll),
        .CLKOUT5    (clk_icap_pll),
        .DO         (),
        .DRDY       (),
        .LOCKED     (locked)
    );

    BUFG   u_bufg_sys (.I(clk_sys_pll), .O(clk_sys));
    assign clk_sys_pll_o = clk_sys_pll;
    BUFG   u_bufg_cpu (.I(clk_cpu_pll), .O(clk_cpu));
    BUFG   u_bufg_icap (.I(clk_icap_pll), .O(clk_icap));
    BUFGCE u_bufg_drp (.I(clk_drp_pll), .CE(locked), .O(mcb_drp_clk));

    BUFPLL_MCB u_bufpll_mcb (
        .IOCLK0        (sysclk_2x),
        .IOCLK1        (sysclk_2x_180),
        .LOCKED        (locked),
        .GCLK          (mcb_drp_clk),
        .SERDESSTROBE0 (pll_ce_0),
        .SERDESSTROBE1 (pll_ce_90),
        .PLLIN0        (clk_2x_0),
        .PLLIN1        (clk_2x_180),
        .LOCK          (pll_lock)
    );

    generate if (DUAL != 0) begin : g_dual
        BUFPLL_MCB u_bufpll_mcb_b (
            .IOCLK0        (sysclk_2x_b),
            .IOCLK1        (sysclk_2x_180_b),
            .LOCKED        (locked),
            .GCLK          (mcb_drp_clk),
            .SERDESSTROBE0 (pll_ce_0_b),
            .SERDESSTROBE1 (pll_ce_90_b),
            .PLLIN0        (clk_2x_0),
            .PLLIN1        (clk_2x_180),
            .LOCK          (pll_lock_b)
        );
    end else begin : g_single
        assign sysclk_2x_b = 1'b0; assign sysclk_2x_180_b = 1'b0;
        assign pll_ce_0_b = 1'b0;  assign pll_ce_90_b = 1'b0;   assign pll_lock_b = 1'b1;
    end endgenerate

    reg powerup_locked;
    always @(posedge mcb_drp_clk or posedge rst)
        if (rst)                        powerup_locked <= 1'b0;
        else if (pll_lock & pll_lock_b) powerup_locked <= 1'b1;

    assign async_rst  = rst | ~powerup_locked;
    assign pll_locked = locked;
endmodule

`default_nettype wire
