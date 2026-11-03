/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_ddr_port.v -- DDR2 chip A (MCB1) behind the SoC's DDR port: g2_ddr_mcb (MIG 3.92 memc_wrapper) with port P0 on
// mcb_wb, the Wishbone bridge for the CPU's line bursts (LINE_BYTES 64, the VexiiRiscv line).  P1 (copy engine, USB
// DMA) is brought out idle-tied here for the workstreams that use it; P2 (read) serves the RasterIX display-list
// reader (rtl/g2/g2_rix_dl.v, through pano_g2_top); P3..P5 are unused.
//
// Clocks: the MCB clocks come from g2_mcb_clk in the top; P0 (mcb_wb) and P1 (exerciser) run on clk_cpu, P2 on clk_sys;
// under G2_DDRA_P1_EXT, P1 is the external client's (fp_*) on clk_sys.
// -DG2_SIM_MCB_MODEL replaces g2_ddr_mcb (hard MCB, not simulated outside ISim) with rtl/g2/sim/mcb_port_model.v on
// P0: the same port protocol, calibration after CAL_CYCLES clocks, random read latency.  The DRAM pins are then idle.
`timescale 1ns / 1ps
`default_nettype none

module g2_ddr_port #(
    parameter integer MEMCLK_PERIOD = 8000
) (
    input  wire        clk,                // clk_sys: port P2 (the display-list reader)
    input  wire        rst,                // clk_sys domain, active high
    input  wire        clk_cpu,            // the CPU domain: mcb_wb on P0, the exerciser on P1
    input  wire        rst_cpu,            // clk_cpu domain, active high
    // from g2_mcb_clk
    input  wire        sysclk_2x,
    input  wire        sysclk_2x_180,
    input  wire        pll_ce_0,
    input  wire        pll_ce_90,
    input  wire        pll_lock,
    input  wire        mcb_drp_clk,
    input  wire        async_rst,
    // the SoC's DDR port (pano_soc_g2 ddr_*): word address inside the 64 MB of chip A
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [23:0] wb_adr,
    input  wire [3:0]  wb_sel,
    input  wire [31:0] wb_dat_w,
    input  wire [2:0]  wb_cti,
    input  wire [1:0]  wb_bte,
    output wire [31:0] wb_dat_r,
    output wire        wb_ack,
    output wire        wb_err,
    output wire        calib_done,         // clk_sys domain
    output wire [7:0]  status,             // mcb_wb status: [0] calib_done [1] nocal access [2] wr fault [3] rd fault [4] busy
    input  wire        st_clear,
    input  wire        x_en,               // run the port-P1 whole-chip exerciser
    output wire [15:0] x_passes,
    output wire [15:0] x_errs,
    // port P2 (read, 32-bit): the RasterIX display-list reader (rtl/g2/g2_rix_dl.v)
    input  wire        dl_cmd_en,
    input  wire [2:0]  dl_cmd_instr,
    input  wire [5:0]  dl_cmd_bl,
    input  wire [29:0] dl_cmd_byte_addr,
    output wire        dl_cmd_full,
    input  wire        dl_rd_en,
    output wire [31:0] dl_rd_data,
    output wire        dl_rd_empty,
    // port P1 for an external client on clk (G2_DDRA_P1_EXT: the Amiga fabrics' fast-RAM cache, amiga/rtl/g2/amiga_g2.v;
    // the exerciser is then absent and x_passes, x_errs read 0); without the define these outputs are idle
    input  wire        fp_cmd_en,
    input  wire [2:0]  fp_cmd_instr,
    input  wire [5:0]  fp_cmd_bl,
    input  wire [29:0] fp_cmd_byte_addr,
    output wire        fp_cmd_full,
    input  wire        fp_wr_en,
    input  wire [3:0]  fp_wr_mask,
    input  wire [31:0] fp_wr_data,
    output wire        fp_wr_empty,
    input  wire        fp_rd_en,
    output wire [31:0] fp_rd_data,
    output wire        fp_rd_empty,
    // MCB1 pins
    output wire [12:0] mcb1_dram_a,
    output wire [1:0]  mcb1_dram_ba,
    output wire        mcb1_dram_ras_n,
    output wire        mcb1_dram_cas_n,
    output wire        mcb1_dram_we_n,
    output wire        mcb1_dram_cke,
    output wire        mcb1_dram_odt,
    output wire        mcb1_dram_ck,
    output wire        mcb1_dram_ck_n,
    inout  wire [15:0] mcb1_dram_dq,
    inout  wire        mcb1_dram_dqs,
    inout  wire        mcb1_dram_dqs_n,
    inout  wire        mcb1_dram_udqs,
    inout  wire        mcb1_dram_udqs_n,
    output wire        mcb1_dram_dm,
    output wire        mcb1_dram_udm,
    inout  wire        mcb1_rzq
);
    wire        calib_async;
    wire        p0_cmd_en, p0_cmd_full, p0_cmd_empty; wire [2:0] p0_cmd_instr; wire [5:0] p0_cmd_bl; wire [29:0] p0_cmd_byte_addr;
    wire        p0_wr_en, p0_wr_full, p0_wr_empty, p0_wr_underrun, p0_wr_error; wire [3:0] p0_wr_mask; wire [31:0] p0_wr_data;
    wire [6:0]  p0_wr_count;
    wire        p0_rd_en, p0_rd_full, p0_rd_empty, p0_rd_overflow, p0_rd_error; wire [31:0] p0_rd_data; wire [6:0] p0_rd_count;

    // Port P1 whole-chip self-test (g2_ddr_exerciser) while x_en: status[5] running, [6] at least one full pass,
    // [7] sticky mismatch.  status[4:0] are mcb_wb's.
    wire [7:0]  wb_status;
    wire        x_cmd_en, x_cmd_full, x_wr_en, x_wr_full, x_rd_en, x_rd_empty, x_bad;
    wire [2:0]  x_cmd_instr; wire [5:0] x_cmd_bl; wire [29:0] x_cmd_addr; wire [31:0] x_wr_data, x_rd_data; wire x_run;
    // P1 as the MCB sees it: the external client on clk, or the exerciser on clk_cpu
    wire        q1_clk, q1_cmd_en, q1_wr_en, q1_rd_en; wire [2:0] q1_cmd_instr; wire [5:0] q1_cmd_bl;
    wire [29:0] q1_cmd_addr; wire [3:0] q1_wr_mask; wire [31:0] q1_wr_data;
    wire        q1_cmd_full, q1_wr_full, q1_wr_empty, q1_rd_empty; wire [31:0] q1_rd_data;
`ifdef G2_DDRA_P1_EXT
    assign q1_clk = clk; assign q1_cmd_en = fp_cmd_en; assign q1_cmd_instr = fp_cmd_instr; assign q1_cmd_bl = fp_cmd_bl;
    assign q1_cmd_addr = fp_cmd_byte_addr; assign q1_wr_en = fp_wr_en; assign q1_wr_mask = fp_wr_mask;
    assign q1_wr_data = fp_wr_data; assign q1_rd_en = fp_rd_en;
    assign fp_cmd_full = q1_cmd_full; assign fp_wr_empty = q1_wr_empty; assign fp_rd_data = q1_rd_data;
    assign fp_rd_empty = q1_rd_empty;
    assign x_cmd_en = 1'b0; assign x_cmd_instr = 3'd0; assign x_cmd_bl = 6'd0; assign x_cmd_addr = 30'd0;
    assign x_wr_en = 1'b0; assign x_wr_data = 32'd0; assign x_rd_en = 1'b0; assign x_bad = 1'b0; assign x_run = 1'b0;
    assign x_passes = 16'd0; assign x_errs = 16'd0;
    wire _unused_x = &{1'b0, x_en, x_cmd_full, x_wr_full, x_rd_empty, x_rd_data, 1'b0};
`else
    assign q1_clk = clk_cpu; assign q1_cmd_en = x_cmd_en; assign q1_cmd_instr = x_cmd_instr; assign q1_cmd_bl = x_cmd_bl;
    assign q1_cmd_addr = x_cmd_addr; assign q1_wr_en = x_wr_en; assign q1_wr_mask = 4'b0; assign q1_wr_data = x_wr_data;
    assign q1_rd_en = x_rd_en;
    assign x_cmd_full = q1_cmd_full; assign x_wr_full = q1_wr_full; assign x_rd_data = q1_rd_data; assign x_rd_empty = q1_rd_empty;
    assign fp_cmd_full = 1'b1; assign fp_wr_empty = 1'b1; assign fp_rd_data = 32'd0; assign fp_rd_empty = 1'b1;
    wire _unused_fp = &{1'b0, fp_cmd_en, fp_cmd_instr, fp_cmd_bl, fp_cmd_byte_addr, fp_wr_en, fp_wr_mask, fp_wr_data, fp_rd_en, 1'b0};
    g2_ddr_exerciser u_x (
        .clk(clk_cpu), .rst(rst_cpu), .go(calib_done), .en(x_en),
        .cmd_en(x_cmd_en), .cmd_instr(x_cmd_instr), .cmd_bl(x_cmd_bl), .cmd_byte_addr(x_cmd_addr),
        .cmd_full(x_cmd_full), .wr_en(x_wr_en), .wr_data(x_wr_data), .wr_full(x_wr_full),
        .rd_en(x_rd_en), .rd_data(x_rd_data), .rd_empty(x_rd_empty), .bad(x_bad),
        .passes(x_passes), .errs(x_errs), .running(x_run));
`endif
    assign status = {x_bad, |x_passes, x_run, wb_status[4:0]};

    mcb_wb #(.LINE_BYTES(64), .ADDR_W(24)) u_wb (
        .clk(clk_cpu), .rst(rst_cpu),
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we), .wb_adr({6'd0, wb_adr}), .wb_sel(wb_sel), .wb_dat_w(wb_dat_w),
        .wb_cti(wb_cti), .wb_bte(wb_bte), .wb_dat_r(wb_dat_r), .wb_ack(wb_ack), .wb_err(wb_err),
        .p_cmd_en(p0_cmd_en), .p_cmd_instr(p0_cmd_instr), .p_cmd_bl(p0_cmd_bl), .p_cmd_byte_addr(p0_cmd_byte_addr),
        .p_cmd_full(p0_cmd_full),
        .p_wr_en(p0_wr_en), .p_wr_mask(p0_wr_mask), .p_wr_data(p0_wr_data), .p_wr_full(p0_wr_full),
        .p_wr_count(p0_wr_count), .p_wr_underrun(p0_wr_underrun), .p_wr_error(p0_wr_error),
        .p_rd_en(p0_rd_en), .p_rd_data(p0_rd_data), .p_rd_empty(p0_rd_empty), .p_rd_overflow(p0_rd_overflow),
        .p_rd_error(p0_rd_error),
        .calib_done_async(calib_async), .st_clear(st_clear), .calib_done(calib_done), .status(wb_status));

`ifdef G2_SIM_MCB_MODEL
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(3)) u_model (
        .clk(clk_cpu), .rst(rst_cpu), .calib_done(calib_async),
        .cmd_en(p0_cmd_en), .cmd_instr(p0_cmd_instr), .cmd_bl(p0_cmd_bl), .cmd_byte_addr(p0_cmd_byte_addr),
        .cmd_full(p0_cmd_full), .cmd_empty(p0_cmd_empty),
        .wr_en(p0_wr_en), .wr_mask(p0_wr_mask), .wr_data(p0_wr_data), .wr_full(p0_wr_full), .wr_empty(p0_wr_empty),
        .wr_count(p0_wr_count), .wr_underrun(p0_wr_underrun), .wr_error(p0_wr_error),
        .rd_en(p0_rd_en), .rd_data(p0_rd_data), .rd_full(p0_rd_full), .rd_empty(p0_rd_empty), .rd_count(p0_rd_count),
        .rd_overflow(p0_rd_overflow), .rd_error(p0_rd_error),
        .b_clk(clk), .b_cmd_en(dl_cmd_en), .b_cmd_instr(dl_cmd_instr), .b_cmd_bl(dl_cmd_bl), .b_cmd_byte_addr(dl_cmd_byte_addr),
        .b_cmd_full(dl_cmd_full), .b_rd_en(dl_rd_en), .b_rd_data(dl_rd_data), .b_rd_empty(dl_rd_empty));
    assign mcb1_dram_a = 13'd0; assign mcb1_dram_ba = 2'd0; assign mcb1_dram_ras_n = 1'b1; assign mcb1_dram_cas_n = 1'b1;
    assign mcb1_dram_we_n = 1'b1; assign mcb1_dram_cke = 1'b0; assign mcb1_dram_odt = 1'b0; assign mcb1_dram_ck = 1'b0;
    assign mcb1_dram_ck_n = 1'b1; assign mcb1_dram_dm = 1'b0; assign mcb1_dram_udm = 1'b0;
`ifdef G2_DDRA_P1_EXT                                 // P1: its own model memory on clk (nothing else addresses that range)
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(5)) u_model1 (
        .clk(q1_clk), .rst(rst), .calib_done(),
        .cmd_en(q1_cmd_en), .cmd_instr(q1_cmd_instr), .cmd_bl(q1_cmd_bl), .cmd_byte_addr(q1_cmd_addr),
        .cmd_full(q1_cmd_full), .cmd_empty(),
        .wr_en(q1_wr_en), .wr_mask(q1_wr_mask), .wr_data(q1_wr_data), .wr_full(q1_wr_full), .wr_empty(q1_wr_empty),
        .wr_count(), .wr_underrun(), .wr_error(),
        .rd_en(q1_rd_en), .rd_data(q1_rd_data), .rd_full(), .rd_empty(q1_rd_empty), .rd_count(),
        .rd_overflow(), .rd_error(),
        .b_clk(q1_clk), .b_cmd_en(1'b0), .b_cmd_instr(3'd0), .b_cmd_bl(6'd0), .b_cmd_byte_addr(30'd0),
        .b_cmd_full(), .b_rd_en(1'b0), .b_rd_data(), .b_rd_empty());
`else
    assign q1_cmd_full = 1'b1; assign q1_wr_full = 1'b1; assign q1_wr_empty = 1'b1; assign q1_rd_empty = 1'b1;
    assign q1_rd_data = 32'd0;                         // P1 idle in the port model
`endif
    wire _unused_sim = &{1'b0, sysclk_2x, sysclk_2x_180, pll_ce_0, pll_ce_90, pll_lock, mcb_drp_clk, async_rst, 1'b0};
`else
    g2_ddr_mcb #(.MEMCLK_PERIOD(MEMCLK_PERIOD), .SIMULATION("FALSE")) u_mcb (
        .sysclk_2x(sysclk_2x), .sysclk_2x_180(sysclk_2x_180), .pll_ce_0(pll_ce_0), .pll_ce_90(pll_ce_90),
        .pll_lock(pll_lock), .mcb_drp_clk(mcb_drp_clk), .async_rst(async_rst), .calib_done(calib_async),
        .mcbx_dram_a(mcb1_dram_a), .mcbx_dram_ba(mcb1_dram_ba), .mcbx_dram_ras_n(mcb1_dram_ras_n),
        .mcbx_dram_cas_n(mcb1_dram_cas_n), .mcbx_dram_we_n(mcb1_dram_we_n), .mcbx_dram_cke(mcb1_dram_cke),
        .mcbx_dram_odt(mcb1_dram_odt), .mcbx_dram_ck(mcb1_dram_ck), .mcbx_dram_ck_n(mcb1_dram_ck_n),
        .mcbx_dram_dq(mcb1_dram_dq), .mcbx_dram_dqs(mcb1_dram_dqs), .mcbx_dram_dqs_n(mcb1_dram_dqs_n),
        .mcbx_dram_udqs(mcb1_dram_udqs), .mcbx_dram_udqs_n(mcb1_dram_udqs_n),
        .mcbx_dram_dm(mcb1_dram_dm), .mcbx_dram_udm(mcb1_dram_udm), .mcbx_rzq(mcb1_rzq),
        .p0_clk(clk_cpu), .p0_cmd_en(p0_cmd_en), .p0_cmd_instr(p0_cmd_instr), .p0_cmd_bl(p0_cmd_bl),
        .p0_cmd_byte_addr(p0_cmd_byte_addr), .p0_cmd_full(p0_cmd_full), .p0_cmd_empty(p0_cmd_empty),
        .p0_wr_en(p0_wr_en), .p0_wr_mask(p0_wr_mask), .p0_wr_data(p0_wr_data), .p0_wr_full(p0_wr_full),
        .p0_wr_empty(p0_wr_empty), .p0_wr_count(p0_wr_count), .p0_wr_underrun(p0_wr_underrun), .p0_wr_error(p0_wr_error),
        .p0_rd_en(p0_rd_en), .p0_rd_data(p0_rd_data), .p0_rd_full(p0_rd_full), .p0_rd_empty(p0_rd_empty),
        .p0_rd_count(p0_rd_count), .p0_rd_overflow(p0_rd_overflow), .p0_rd_error(p0_rd_error),
        .p1_clk(q1_clk), .p1_cmd_en(q1_cmd_en), .p1_cmd_instr(q1_cmd_instr), .p1_cmd_bl(q1_cmd_bl), .p1_cmd_byte_addr(q1_cmd_addr),
        .p1_cmd_full(q1_cmd_full), .p1_cmd_empty(), .p1_wr_en(q1_wr_en), .p1_wr_mask(q1_wr_mask), .p1_wr_data(q1_wr_data),
        .p1_wr_full(q1_wr_full), .p1_wr_empty(q1_wr_empty), .p1_wr_count(), .p1_wr_underrun(), .p1_wr_error(),
        .p1_rd_en(q1_rd_en), .p1_rd_data(q1_rd_data), .p1_rd_full(), .p1_rd_empty(q1_rd_empty), .p1_rd_count(), .p1_rd_overflow(), .p1_rd_error(),
        .p2_clk(clk), .p2_cmd_en(dl_cmd_en), .p2_cmd_instr(dl_cmd_instr), .p2_cmd_bl(dl_cmd_bl), .p2_cmd_byte_addr(dl_cmd_byte_addr),
        .p2_cmd_full(dl_cmd_full), .p2_cmd_empty(),
        .p2_rd_en(dl_rd_en), .p2_rd_data(dl_rd_data), .p2_rd_full(), .p2_rd_empty(dl_rd_empty), .p2_rd_count(), .p2_rd_overflow(), .p2_rd_error(),
        .p3_clk(clk), .p3_cmd_en(1'b0), .p3_cmd_instr(3'b0), .p3_cmd_bl(6'b0), .p3_cmd_byte_addr(30'b0),
        .p3_cmd_full(), .p3_cmd_empty(), .p3_wr_en(1'b0), .p3_wr_mask(4'b0), .p3_wr_data(32'b0), .p3_wr_full(),
        .p3_wr_empty(), .p3_wr_count(), .p3_wr_underrun(), .p3_wr_error(),
        .p4_clk(clk), .p4_cmd_en(1'b0), .p4_cmd_instr(3'b0), .p4_cmd_bl(6'b0), .p4_cmd_byte_addr(30'b0),
        .p4_cmd_full(), .p4_cmd_empty(), .p4_rd_en(1'b0), .p4_rd_data(), .p4_rd_full(), .p4_rd_empty(),
        .p4_rd_count(), .p4_rd_overflow(), .p4_rd_error(),
        .p5_clk(clk), .p5_cmd_en(1'b0), .p5_cmd_instr(3'b0), .p5_cmd_bl(6'b0), .p5_cmd_byte_addr(30'b0),
        .p5_cmd_full(), .p5_cmd_empty(), .p5_wr_en(1'b0), .p5_wr_mask(4'b0), .p5_wr_data(32'b0), .p5_wr_full(),
        .p5_wr_empty(), .p5_wr_count(), .p5_wr_underrun(), .p5_wr_error());
`endif
endmodule

`default_nettype wire
