/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// wb_cdc.v -- a single-beat Wishbone bridge between two clock domains: the CPU side (clk_m) and the peripheral side
// (clk_s), any frequency ratio, by a four-phase toggle handshake through two-flop synchronisers.
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2.
//
// Master side: a cycle (m_cyc & m_stb) is captured with its address, data, byte enables and direction, and req_t
// toggles; the cycle is held (no ack) until ack_t comes back toggled, then m_ack (or m_err) is one clock with the
// read data.  A new cycle is accepted only after the previous acknowledge.  Slave side: the toggled req_t, seen
// through two flops, starts one cycle on the s_* bus and holds it until s_ack or s_err; the data and the error flag
// are captured and ack_t toggles.  The address, data and control registers are written on the master side only
// while no request is outstanding and read on the slave side only after the request toggle has been seen, so they
// are stable across the crossing; the slave side takes them into its own registers in the clock that starts the cycle,
// so each crossing path is register to register, bounded by the FROM/TO constraints of ise/pano_g2.ucf.  Latency: two
// synchroniser flops each way plus the slave cycle, about 5 clk_m + 3 clk_s periods.  Bursts (CTI) are not carried:
// every beat is one cycle; the SoC routes only single-beat IO accesses through this bridge.
`timescale 1ns / 1ps
`default_nettype none
module wb_cdc #(
    parameter integer AW = 30
) (
    // master side
    input  wire          clk_m,
    input  wire          rst_m,
    input  wire          m_cyc,
    input  wire          m_stb,
    input  wire          m_we,
    input  wire [AW-1:0] m_adr,
    input  wire [3:0]    m_sel,
    input  wire [31:0]   m_dat_w,
    output reg  [31:0]   m_dat_r,
    output reg           m_ack,
    output reg           m_err,
    // slave side
    input  wire          clk_s,
    input  wire          rst_s,
    output reg           s_cyc,
    output reg           s_stb,
    output wire          s_we,
    output wire [AW-1:0] s_adr,
    output wire [3:0]    s_sel,
    output wire [31:0]   s_dat_w,
    input  wire [31:0]   s_dat_r,
    input  wire          s_ack,
    input  wire          s_err
);
    // ---- master side ----
    reg          req_t = 1'b0;            // toggles per request
    reg          busy = 1'b0;
    reg          we_x = 1'b0; reg [AW-1:0] adr_x = 0; reg [3:0] sel_x = 0; reg [31:0] dat_w_x = 0;   // stable while busy
    reg  [31:0]  dat_r_x = 0; reg err_x = 1'b0;                                                       // written by the slave side
    reg          ack_t = 1'b0;            // toggles per completion (slave side)
    reg  [1:0]   ack_s = 2'b00;           // ack_t synchronised into clk_m
    reg          ack_seen = 1'b0;
    wire         take = m_cyc && m_stb && !busy && !m_ack && !m_err;
    always @(posedge clk_m) begin
        ack_s <= {ack_s[0], ack_t};
        m_ack <= 1'b0; m_err <= 1'b0;
        if (rst_m) begin
            req_t <= 1'b0; busy <= 1'b0; ack_seen <= 1'b0; ack_s <= 2'b00;
        end else begin
            if (take) begin
                we_x <= m_we; adr_x <= m_adr; sel_x <= m_sel; dat_w_x <= m_dat_w;
                req_t <= ~req_t; busy <= 1'b1;
            end
            if (busy && (ack_s[1] != ack_seen)) begin
                ack_seen <= ack_s[1]; busy <= 1'b0;
                m_dat_r <= dat_r_x; m_ack <= !err_x; m_err <= err_x;
            end
        end
    end
    // ---- slave side ----
    reg  [1:0]   req_s = 2'b00;
    reg          req_seen = 1'b0;
    // the cycle's address, data and control taken into clk_s in the clock that raises s_cyc (they have been stable since
    // the toggle): every crossing path ends on one register, and the slave's decode and RAM fan-out start in clk_s
    reg          we_s = 1'b0; reg [AW-1:0] adr_s = 0; reg [3:0] sel_s = 0; reg [31:0] dat_w_s = 0;
    assign s_we = we_s; assign s_adr = adr_s; assign s_sel = sel_s; assign s_dat_w = dat_w_s;
    always @(posedge clk_s) begin
        req_s <= {req_s[0], req_t};
        if (rst_s) begin
            s_cyc <= 1'b0; s_stb <= 1'b0; req_seen <= 1'b0; req_s <= 2'b00; ack_t <= 1'b0;
        end else begin
            if (!s_cyc && (req_s[1] != req_seen)) begin
                req_seen <= req_s[1]; s_cyc <= 1'b1; s_stb <= 1'b1;
                we_s <= we_x; adr_s <= adr_x; sel_s <= sel_x; dat_w_s <= dat_w_x;
            end
            if (s_cyc && (s_ack || s_err)) begin
                s_cyc <= 1'b0; s_stb <= 1'b0; dat_r_x <= s_dat_r; err_x <= s_err; ack_t <= ~ack_t;
            end
        end
    end
endmodule
`default_nettype wire
