/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_ddr_mcb.v -- one Spartan-6 MCB (DDR2, MT47H32M16, 64 MB, x16) with six 32-bit user ports.
//
// Mike Wolak, EPROM Foundry Inc.
//
// Instantiates the MIG 3.92 memc_wrapper (rtl/g2/mig/common, vendored unmodified) with the parameters
// that MIG 3.92 generated for this board (rtl/g2/mig/g2_ddr2_c1/g2_ddr2_c1.v, g2_ddr2_c3.v): DDR2
// MT47H32M16XX-25E-IT timing, burst length 4, ODT 50 ohm, full drive strength, ROW_BANK_COLUMN,
// port configuration B32_B32_R32_W32_R32_W32, round-robin arbitration over 12 time slots, soft
// calibration with input-termination calibration skipped (uncalibrated input termination:
// UNTUNED_SPLIT_50 in the UCF, no ZIO ball).  The same module serves MCB1 and MCB3: which MCB is
// used is decided by the pin LOC constraints (rtl/g2/mig/g2_ddr2_c1/g2_ddr2_c1.ucf, g2_ddr2_c3.ucf).
//
// The clocks come from rtl/g2/g2_mcb_clk.v.  MEMCLK_PERIOD sets the DRAM CK period in ps and the
// CAS latency with it (MT47H32M16 -25E: CL3 to 200 MHz, CL4 to 266 MHz, CL5 to 400 MHz); MIG 3.92
// chose CL3 at 8000 ps and CL5 at 3200 ps (rtl/g2/mig/g2_ddr2_c1_3200/g2_ddr2_c1_3200.v).
// The MCB timing parameters are in ps and are converted to cycles by the MCB wrapper.
//
// Port semantics (UG388): a command is {instr, bl, byte_addr}; instr 000 write, 001 read, 010 write
// with auto precharge, 011 read with auto precharge, 100 refresh; bl is the word count minus one
// (1 to 64 words); byte_addr is aligned to the port width (bits [1:0] = 0).  Write data (with mask:
// 1 = byte NOT written) enters the port's write FIFO before its command is issued.  Read data leaves
// the read FIFO first-word-fall-through: rd_data is valid while rd_empty is low, rd_en pops.  The
// port FIFOs are asynchronous: each port runs on its own pN_clk.
//
// calib_done is in the mcb_drp_clk domain.
`timescale 1ns / 1ps
`default_nettype none

module g2_ddr_mcb #(
    parameter integer MEMCLK_PERIOD = 8000,     // ps: 8000 (125 MHz, 250 Mb/s) or 3200 (312.5 MHz, 625 Mb/s)
    parameter         SIMULATION    = "FALSE"   // "TRUE" shortens the soft calibration for simulation
) (
    // clocks and reset from g2_mcb_clk
    input  wire        sysclk_2x,
    input  wire        sysclk_2x_180,
    input  wire        pll_ce_0,
    input  wire        pll_ce_90,
    input  wire        pll_lock,
    input  wire        mcb_drp_clk,
    input  wire        async_rst,
    output wire        calib_done,

    // DDR2 device
    output wire [12:0] mcbx_dram_a,
    output wire [1:0]  mcbx_dram_ba,
    output wire        mcbx_dram_ras_n,
    output wire        mcbx_dram_cas_n,
    output wire        mcbx_dram_we_n,
    output wire        mcbx_dram_cke,
    output wire        mcbx_dram_odt,
    output wire        mcbx_dram_ck,
    output wire        mcbx_dram_ck_n,
    inout  wire [15:0] mcbx_dram_dq,
    inout  wire        mcbx_dram_dqs,           // LDQS
    inout  wire        mcbx_dram_dqs_n,
    inout  wire        mcbx_dram_udqs,
    inout  wire        mcbx_dram_udqs_n,
    output wire        mcbx_dram_dm,            // LDM
    output wire        mcbx_dram_udm,
    inout  wire        mcbx_rzq,

    // P0: bidirectional, 32-bit -- CPU (rtl/g2/mcb_wb.v)
    input  wire        p0_clk,
    input  wire        p0_cmd_en,
    input  wire [2:0]  p0_cmd_instr,
    input  wire [5:0]  p0_cmd_bl,
    input  wire [29:0] p0_cmd_byte_addr,
    output wire        p0_cmd_full,
    output wire        p0_cmd_empty,
    input  wire        p0_wr_en,
    input  wire [3:0]  p0_wr_mask,
    input  wire [31:0] p0_wr_data,
    output wire        p0_wr_full,
    output wire        p0_wr_empty,
    output wire [6:0]  p0_wr_count,
    output wire        p0_wr_underrun,
    output wire        p0_wr_error,
    input  wire        p0_rd_en,
    output wire [31:0] p0_rd_data,
    output wire        p0_rd_full,
    output wire        p0_rd_empty,
    output wire [6:0]  p0_rd_count,
    output wire        p0_rd_overflow,
    output wire        p0_rd_error,
    // P1: bidirectional, 32-bit -- copy engine / USB DMA
    input  wire        p1_clk,
    input  wire        p1_cmd_en,
    input  wire [2:0]  p1_cmd_instr,
    input  wire [5:0]  p1_cmd_bl,
    input  wire [29:0] p1_cmd_byte_addr,
    output wire        p1_cmd_full,
    output wire        p1_cmd_empty,
    input  wire        p1_wr_en,
    input  wire [3:0]  p1_wr_mask,
    input  wire [31:0] p1_wr_data,
    output wire        p1_wr_full,
    output wire        p1_wr_empty,
    output wire [6:0]  p1_wr_count,
    output wire        p1_wr_underrun,
    output wire        p1_wr_error,
    input  wire        p1_rd_en,
    output wire [31:0] p1_rd_data,
    output wire        p1_rd_full,
    output wire        p1_rd_empty,
    output wire [6:0]  p1_rd_count,
    output wire        p1_rd_overflow,
    output wire        p1_rd_error,
    // P2: read, 32-bit -- video scan-out
    input  wire        p2_clk,
    input  wire        p2_cmd_en,
    input  wire [2:0]  p2_cmd_instr,
    input  wire [5:0]  p2_cmd_bl,
    input  wire [29:0] p2_cmd_byte_addr,
    output wire        p2_cmd_full,
    output wire        p2_cmd_empty,
    input  wire        p2_rd_en,
    output wire [31:0] p2_rd_data,
    output wire        p2_rd_full,
    output wire        p2_rd_empty,
    output wire [6:0]  p2_rd_count,
    output wire        p2_rd_overflow,
    output wire        p2_rd_error,
    // P3: write, 32-bit -- spare write
    input  wire        p3_clk,
    input  wire        p3_cmd_en,
    input  wire [2:0]  p3_cmd_instr,
    input  wire [5:0]  p3_cmd_bl,
    input  wire [29:0] p3_cmd_byte_addr,
    output wire        p3_cmd_full,
    output wire        p3_cmd_empty,
    input  wire        p3_wr_en,
    input  wire [3:0]  p3_wr_mask,
    input  wire [31:0] p3_wr_data,
    output wire        p3_wr_full,
    output wire        p3_wr_empty,
    output wire [6:0]  p3_wr_count,
    output wire        p3_wr_underrun,
    output wire        p3_wr_error,
    // P4: read, 32-bit -- spare read
    input  wire        p4_clk,
    input  wire        p4_cmd_en,
    input  wire [2:0]  p4_cmd_instr,
    input  wire [5:0]  p4_cmd_bl,
    input  wire [29:0] p4_cmd_byte_addr,
    output wire        p4_cmd_full,
    output wire        p4_cmd_empty,
    input  wire        p4_rd_en,
    output wire [31:0] p4_rd_data,
    output wire        p4_rd_full,
    output wire        p4_rd_empty,
    output wire [6:0]  p4_rd_count,
    output wire        p4_rd_overflow,
    output wire        p4_rd_error,
    // P5: write, 32-bit -- spare write
    input  wire        p5_clk,
    input  wire        p5_cmd_en,
    input  wire [2:0]  p5_cmd_instr,
    input  wire [5:0]  p5_cmd_bl,
    input  wire [29:0] p5_cmd_byte_addr,
    output wire        p5_cmd_full,
    output wire        p5_cmd_empty,
    input  wire        p5_wr_en,
    input  wire [3:0]  p5_wr_mask,
    input  wire [31:0] p5_wr_data,
    output wire        p5_wr_full,
    output wire        p5_wr_empty,
    output wire [6:0]  p5_wr_count,
    output wire        p5_wr_underrun,
    output wire        p5_wr_error
);
    localparam integer CAS_LATENCY = (MEMCLK_PERIOD >= 5000) ? 3 : (MEMCLK_PERIOD >= 3750) ? 4 : 5;

    // ZIO is used only by input-termination calibration, which this configuration skips.
    wire zio_nc;

    memc_wrapper #(
        .C_MEMCLK_PERIOD            (MEMCLK_PERIOD),
        .C_CALIB_SOFT_IP            ("TRUE"),
        .C_SIMULATION               (SIMULATION),
        .C_ARB_NUM_TIME_SLOTS       (12),
        .C_ARB_TIME_SLOT_0          (18'o012345),
        .C_ARB_TIME_SLOT_1          (18'o123450),
        .C_ARB_TIME_SLOT_2          (18'o234501),
        .C_ARB_TIME_SLOT_3          (18'o345012),
        .C_ARB_TIME_SLOT_4          (18'o450123),
        .C_ARB_TIME_SLOT_5          (18'o501234),
        .C_ARB_TIME_SLOT_6          (18'o012345),
        .C_ARB_TIME_SLOT_7          (18'o123450),
        .C_ARB_TIME_SLOT_8          (18'o234501),
        .C_ARB_TIME_SLOT_9          (18'o345012),
        .C_ARB_TIME_SLOT_10         (18'o450123),
        .C_ARB_TIME_SLOT_11         (18'o501234),
        .C_ARB_ALGORITHM            (0),
        .C_PORT_ENABLE              (6'b111111),
        .C_PORT_CONFIG              ("B32_B32_R32_W32_R32_W32"),
        .C_MEM_TRAS                 (42500),
        .C_MEM_TRCD                 (12500),
        .C_MEM_TREFI                (3900000),
        .C_MEM_TRFC                 (105000),
        .C_MEM_TRP                  (12500),
        .C_MEM_TWR                  (15000),
        .C_MEM_TRTP                 (7500),
        .C_MEM_TWTR                 (7500),
        .C_MEM_ADDR_ORDER           ("ROW_BANK_COLUMN"),
        .C_NUM_DQ_PINS              (16),
        .C_MEM_TYPE                 ("DDR2"),
        .C_MEM_DENSITY              ("512Mb"),
        .C_MEM_BURST_LEN            (4),
        .C_MEM_CAS_LATENCY          (CAS_LATENCY),
        .C_MEM_ADDR_WIDTH           (13),
        .C_MEM_BANKADDR_WIDTH       (2),
        .C_MEM_NUM_COL_BITS         (10),
        .C_MEM_DDR1_2_ODS           ("REDUCED"),
        .C_MEM_DDR2_RTT             ("50OHMS"),
        .C_MEM_DDR2_DIFF_DQS_EN     ("YES"),
        .C_MEM_DDR2_3_PA_SR         ("FULL"),
        .C_MEM_DDR2_3_HIGH_TEMP_SR  ("NORMAL"),
        .C_MEM_DDR3_CAS_LATENCY     (6),
        .C_MEM_DDR3_ODS             ("DIV6"),
        .C_MEM_DDR3_RTT             ("DIV2"),
        .C_MEM_DDR3_CAS_WR_LATENCY  (5),
        .C_MEM_DDR3_AUTO_SR         ("ENABLED"),
        .C_MEM_MOBILE_PA_SR         ("FULL"),
        .C_MEM_MDDR_ODS             ("FULL"),
        .C_MC_CALIB_BYPASS          ("NO"),
        .C_MC_CALIBRATION_MODE      ("CALIBRATION"),
        .C_MC_CALIBRATION_DELAY     ("HALF"),
        .C_SKIP_IN_TERM_CAL         (1),
        .C_SKIP_DYNAMIC_CAL         (0),
        .LDQSP_TAP_DELAY_VAL (0), .UDQSP_TAP_DELAY_VAL (0), .LDQSN_TAP_DELAY_VAL (0), .UDQSN_TAP_DELAY_VAL (0),
        .DQ0_TAP_DELAY_VAL  (0), .DQ1_TAP_DELAY_VAL  (0), .DQ2_TAP_DELAY_VAL  (0), .DQ3_TAP_DELAY_VAL  (0),
        .DQ4_TAP_DELAY_VAL  (0), .DQ5_TAP_DELAY_VAL  (0), .DQ6_TAP_DELAY_VAL  (0), .DQ7_TAP_DELAY_VAL  (0),
        .DQ8_TAP_DELAY_VAL  (0), .DQ9_TAP_DELAY_VAL  (0), .DQ10_TAP_DELAY_VAL (0), .DQ11_TAP_DELAY_VAL (0),
        .DQ12_TAP_DELAY_VAL (0), .DQ13_TAP_DELAY_VAL (0), .DQ14_TAP_DELAY_VAL (0), .DQ15_TAP_DELAY_VAL (0),
        .C_P0_MASK_SIZE             (4),
        .C_P0_DATA_PORT_SIZE        (32),
        .C_P1_MASK_SIZE             (4),
        .C_P1_DATA_PORT_SIZE        (32)
    ) memc_wrapper_inst (
        .mcbx_dram_addr     (mcbx_dram_a),
        .mcbx_dram_ba       (mcbx_dram_ba),
        .mcbx_dram_ras_n    (mcbx_dram_ras_n),
        .mcbx_dram_cas_n    (mcbx_dram_cas_n),
        .mcbx_dram_we_n     (mcbx_dram_we_n),
        .mcbx_dram_cke      (mcbx_dram_cke),
        .mcbx_dram_clk      (mcbx_dram_ck),
        .mcbx_dram_clk_n    (mcbx_dram_ck_n),
        .mcbx_dram_dq       (mcbx_dram_dq),
        .mcbx_dram_dqs      (mcbx_dram_dqs),
        .mcbx_dram_dqs_n    (mcbx_dram_dqs_n),
        .mcbx_dram_udqs     (mcbx_dram_udqs),
        .mcbx_dram_udqs_n   (mcbx_dram_udqs_n),
        .mcbx_dram_udm      (mcbx_dram_udm),
        .mcbx_dram_ldm      (mcbx_dram_dm),
        .mcbx_dram_odt      (mcbx_dram_odt),
        .mcbx_dram_ddr3_rst (),
        .mcbx_rzq           (mcbx_rzq),
        .mcbx_zio           (zio_nc),
        .calib_done         (calib_done),
        .async_rst          (async_rst),
        .sysclk_2x          (sysclk_2x),
        .sysclk_2x_180      (sysclk_2x_180),
        .pll_ce_0           (pll_ce_0),
        .pll_ce_90          (pll_ce_90),
        .pll_lock           (pll_lock),
        .mcb_drp_clk        (mcb_drp_clk),
        .selfrefresh_enter  (1'b0),
        .selfrefresh_mode   (),
        .p0_cmd_clk(p0_clk), .p0_cmd_en(p0_cmd_en), .p0_cmd_instr(p0_cmd_instr), .p0_cmd_bl(p0_cmd_bl),
        .p0_cmd_byte_addr(p0_cmd_byte_addr), .p0_cmd_full(p0_cmd_full), .p0_cmd_empty(p0_cmd_empty),
        .p0_wr_clk(p0_clk), .p0_wr_en(p0_wr_en), .p0_wr_mask(p0_wr_mask), .p0_wr_data(p0_wr_data),
        .p0_wr_full(p0_wr_full), .p0_wr_empty(p0_wr_empty), .p0_wr_count(p0_wr_count),
        .p0_wr_underrun(p0_wr_underrun), .p0_wr_error(p0_wr_error),
        .p0_rd_clk(p0_clk), .p0_rd_en(p0_rd_en), .p0_rd_data(p0_rd_data), .p0_rd_full(p0_rd_full),
        .p0_rd_empty(p0_rd_empty), .p0_rd_count(p0_rd_count), .p0_rd_overflow(p0_rd_overflow), .p0_rd_error(p0_rd_error),
        .p1_cmd_clk(p1_clk), .p1_cmd_en(p1_cmd_en), .p1_cmd_instr(p1_cmd_instr), .p1_cmd_bl(p1_cmd_bl),
        .p1_cmd_byte_addr(p1_cmd_byte_addr), .p1_cmd_full(p1_cmd_full), .p1_cmd_empty(p1_cmd_empty),
        .p1_wr_clk(p1_clk), .p1_wr_en(p1_wr_en), .p1_wr_mask(p1_wr_mask), .p1_wr_data(p1_wr_data),
        .p1_wr_full(p1_wr_full), .p1_wr_empty(p1_wr_empty), .p1_wr_count(p1_wr_count),
        .p1_wr_underrun(p1_wr_underrun), .p1_wr_error(p1_wr_error),
        .p1_rd_clk(p1_clk), .p1_rd_en(p1_rd_en), .p1_rd_data(p1_rd_data), .p1_rd_full(p1_rd_full),
        .p1_rd_empty(p1_rd_empty), .p1_rd_count(p1_rd_count), .p1_rd_overflow(p1_rd_overflow), .p1_rd_error(p1_rd_error),
        .p2_cmd_clk(p2_clk), .p2_cmd_en(p2_cmd_en), .p2_cmd_instr(p2_cmd_instr), .p2_cmd_bl(p2_cmd_bl),
        .p2_cmd_byte_addr(p2_cmd_byte_addr), .p2_cmd_full(p2_cmd_full), .p2_cmd_empty(p2_cmd_empty),
        .p2_wr_clk(p2_clk), .p2_wr_en(1'b0), .p2_wr_mask(4'b0), .p2_wr_data(32'b0),
        .p2_wr_full(), .p2_wr_empty(), .p2_wr_count(), .p2_wr_underrun(), .p2_wr_error(),
        .p2_rd_clk(p2_clk), .p2_rd_en(p2_rd_en), .p2_rd_data(p2_rd_data), .p2_rd_full(p2_rd_full),
        .p2_rd_empty(p2_rd_empty), .p2_rd_count(p2_rd_count), .p2_rd_overflow(p2_rd_overflow), .p2_rd_error(p2_rd_error),
        .p3_cmd_clk(p3_clk), .p3_cmd_en(p3_cmd_en), .p3_cmd_instr(p3_cmd_instr), .p3_cmd_bl(p3_cmd_bl),
        .p3_cmd_byte_addr(p3_cmd_byte_addr), .p3_cmd_full(p3_cmd_full), .p3_cmd_empty(p3_cmd_empty),
        .p3_wr_clk(p3_clk), .p3_wr_en(p3_wr_en), .p3_wr_mask(p3_wr_mask), .p3_wr_data(p3_wr_data),
        .p3_wr_full(p3_wr_full), .p3_wr_empty(p3_wr_empty), .p3_wr_count(p3_wr_count),
        .p3_wr_underrun(p3_wr_underrun), .p3_wr_error(p3_wr_error),
        .p3_rd_clk(p3_clk), .p3_rd_en(1'b0), .p3_rd_data(), .p3_rd_full(), .p3_rd_empty(), .p3_rd_count(),
        .p3_rd_overflow(), .p3_rd_error(),
        .p4_cmd_clk(p4_clk), .p4_cmd_en(p4_cmd_en), .p4_cmd_instr(p4_cmd_instr), .p4_cmd_bl(p4_cmd_bl),
        .p4_cmd_byte_addr(p4_cmd_byte_addr), .p4_cmd_full(p4_cmd_full), .p4_cmd_empty(p4_cmd_empty),
        .p4_wr_clk(p4_clk), .p4_wr_en(1'b0), .p4_wr_mask(4'b0), .p4_wr_data(32'b0),
        .p4_wr_full(), .p4_wr_empty(), .p4_wr_count(), .p4_wr_underrun(), .p4_wr_error(),
        .p4_rd_clk(p4_clk), .p4_rd_en(p4_rd_en), .p4_rd_data(p4_rd_data), .p4_rd_full(p4_rd_full),
        .p4_rd_empty(p4_rd_empty), .p4_rd_count(p4_rd_count), .p4_rd_overflow(p4_rd_overflow), .p4_rd_error(p4_rd_error),
        .p5_cmd_clk(p5_clk), .p5_cmd_en(p5_cmd_en), .p5_cmd_instr(p5_cmd_instr), .p5_cmd_bl(p5_cmd_bl),
        .p5_cmd_byte_addr(p5_cmd_byte_addr), .p5_cmd_full(p5_cmd_full), .p5_cmd_empty(p5_cmd_empty),
        .p5_wr_clk(p5_clk), .p5_wr_en(p5_wr_en), .p5_wr_mask(p5_wr_mask), .p5_wr_data(p5_wr_data),
        .p5_wr_full(p5_wr_full), .p5_wr_empty(p5_wr_empty), .p5_wr_count(p5_wr_count),
        .p5_wr_underrun(p5_wr_underrun), .p5_wr_error(p5_wr_error),
        .p5_rd_clk(p5_clk), .p5_rd_en(1'b0), .p5_rd_data(), .p5_rd_full(), .p5_rd_empty(), .p5_rd_count(),
        .p5_rd_overflow(), .p5_rd_error()
    );
endmodule

`default_nettype wire
