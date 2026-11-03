/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// mcb_wb.v -- Wishbone B4 slave for one 32-bit bidirectional Spartan-6 MCB user port (the CPU port).
//
// Mike Wolak, EPROM Foundry Inc.
//
// The Wishbone side and the MCB port run on the same clock (clk); the MCB port FIFOs are
// asynchronous to the memory controller, so no clock crossing is needed here other than the
// calibration-done bit, which is synchronised from the mcb_drp_clk domain.
//
// WISHBONE: B4, registered feedback.  Word address wb_adr[29:0]; the low ADDR_W bits select a word
// in the 2^ADDR_W-word device (24 bits = 64 MB).  A beat transfers at a rising edge where
// wb_cyc & wb_stb & (wb_ack | wb_err).  CTI 3'b010 marks an incrementing burst whose next beat
// address follows BTE (00 linear, 01 wrap-4, 10 wrap-8, 11 wrap-16); any other CTI ends the burst
// or is a single access.  During a burst the slave keeps ack asserted on consecutive cycles, so a
// burst moves one beat per clock once its data is available.  The master may drop STB between beats
// of a burst while holding CYC; ACK and ERR are gated with CYC & STB, so an answer prepared for the
// next beat is not presented in such a gap and the beat is answered when STB returns.
//
// READS.  A single read (CTI != 010 on a fresh request) is one MCB read command of one word.  A burst
// read fetches the whole aligned line of LINE_BYTES (one MCB command of LINE_BYTES/4 words, starting
// at the line base) into a line buffer; beats are answered from the buffer as words arrive, in any
// order the BTE produces (linear from the base, critical word first with wrap).  A linear burst that
// leaves the line fetches the next line.  The buffer serves only the burst that fetched it: it is
// invalidated at the end of that burst, when CYC drops and on any write.
//
// WRITES.  Posted.  Each beat's data and mask (MCB mask bit = 1: byte not written; mask = ~wb_sel)
// enter the MCB write FIFO at its transfer edge and are acknowledged there.  Consecutive beats form
// one segment: contiguous word addresses within one aligned line.  A segment becomes one MCB write
// command of 1 to LINE_BYTES/4 words, issued after all of its data is in the FIFO (UG388: write data
// before its command), when the burst ends, the next beat is not contiguous, the next beat starts a
// new line, or CYC drops.  Byte enables go to the DDR2 DM pins through the MCB mask, so a partial
// write is one write, not a read-modify-write.
//
// ORDER.  The MCB executes one port's commands in order.  A read command is issued only when no write
// segment is open and every earlier command has been handed to the port, so a read after a write to
// the same address returns the written data.
//
// BEFORE CALIBRATION.  Every access is answered at once without touching the port: a read returns
// NOCAL_DATA, a write is dropped, and the sticky status bit nocal_access is set.  NOCAL_ERR = 1 answers
// with wb_err instead of wb_ack, for a master that honours ERR.  A cycle is never left unanswered.
//
// STATUS (status[7:0], for the board page):
//   [0] calib_done         MCB calibration complete (synchronised)
//   [1] nocal_access       sticky: an access arrived before calibration
//   [2] wr_fault           sticky: MCB port wr_underrun or wr_error seen
//   [3] rd_fault           sticky: MCB port rd_overflow or rd_error seen
//   [4] busy               a command, a write segment or a read is in flight
//   [7:5] 0
// st_clear (one clock) clears the sticky bits.
`timescale 1ns / 1ps
`default_nettype none

module mcb_wb #(
    parameter integer LINE_BYTES = 32,              // 32 or 64: the CPU data-cache line
    parameter integer ADDR_W     = 24,              // word-address bits of the device (24: 64 MB)
    parameter [31:0]  NOCAL_DATA = 32'hDEAD_CA1B,
    parameter integer NOCAL_ERR  = 0
) (
    input  wire        clk,
    input  wire        rst,                         // synchronous, active high

    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [29:0] wb_adr,
    input  wire [3:0]  wb_sel,
    input  wire [31:0] wb_dat_w,
    input  wire [2:0]  wb_cti,
    input  wire [1:0]  wb_bte,
    output reg  [31:0] wb_dat_r,
    output wire        wb_ack,
    output wire        wb_err,

    output wire        p_cmd_en,
    output wire [2:0]  p_cmd_instr,
    output wire [5:0]  p_cmd_bl,
    output wire [29:0] p_cmd_byte_addr,
    input  wire        p_cmd_full,
    output wire        p_wr_en,
    output wire [3:0]  p_wr_mask,
    output wire [31:0] p_wr_data,
    input  wire        p_wr_full,
    input  wire [6:0]  p_wr_count,
    input  wire        p_wr_underrun,
    input  wire        p_wr_error,
    output wire        p_rd_en,
    input  wire [31:0] p_rd_data,
    input  wire        p_rd_empty,
    input  wire        p_rd_overflow,
    input  wire        p_rd_error,

    input  wire        calib_done_async,            // memc_wrapper calib_done (mcb_drp_clk domain)
    input  wire        st_clear,
    output wire        calib_done,
    output wire [7:0]  status
);
    localparam integer LW = LINE_BYTES / 4;         // words per line
    localparam integer LB = (LW == 16) ? 4 : 3;     // log2(LW)
    localparam [2:0] I_WRITE = 3'b000, I_READ = 3'b001;
    localparam [1:0] R_IDLE = 2'd0, R_SINGLE = 2'd1, R_FILL = 2'd2;
    // MCB write FIFO: 64 words.  A grant is given only while the FIFO has room for the beat it
    // admits plus the beats already admitted but not yet counted (the count lags a push by up to
    // two clocks).
    localparam [6:0] WR_ROOM = 7'd56;

    // ---- calibration-done synchroniser ---------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg cal_s0, cal_s1;
    always @(posedge clk) begin cal_s0 <= calib_done_async; cal_s1 <= cal_s0; end
`ifdef MCBWB_F8
    wire cal = 1'b1;                                // fault: calibration ignored
`else
    wire cal = cal_s1;
`endif
    assign calib_done = cal_s1;

    // ---- state ---------------------------------------------------------------------------
    reg                 pc_v;                       // one pending MCB command
    reg  [2:0]          pc_instr;
    reg  [5:0]          pc_bl;
    reg  [ADDR_W-1:0]   pc_addr;

    reg                 seg_open;                   // write segment being filled
    reg                 seg_close_req;
    reg  [ADDR_W-1:0]   seg_start;
    reg  [ADDR_W-1:0]   seg_next;
    reg  [5:0]          seg_n;                      // words in the segment

    reg  [1:0]          rstate;
    reg  [31:0]         lb [0:LW-1];                // line buffer
    reg  [LW-1:0]       lb_vld;
    reg  [ADDR_W-LB-1:0] lb_tag;
    reg                 lb_live;
    reg  [LB-1:0]       fill_idx;

    reg                 ack_r, err_r;               // registered answer for the current cycle
    reg                 ack_nocal;                  // the ack/err now asserted answers a pre-calibration access
    reg                 st_nocal, st_wr, st_rd;

    // ---- this edge -----------------------------------------------------------------------
    wire [ADDR_W-1:0] a     = wb_adr[ADDR_W-1:0];
    wire              req   = wb_cyc & wb_stb;
    wire              xfer  = req & (ack_r | err_r);
    wire              xfer_w = xfer & wb_we & ~ack_nocal;
    wire              cont  = xfer & (wb_cti == 3'b010);

    function [ADDR_W-1:0] f_next;
        input [ADDR_W-1:0] x; input [1:0] bte;
        begin
`ifdef MCBWB_F4
            f_next = {x[ADDR_W-1:2], x[1:0] + 2'd1};  // fault: every BTE decoded as wrap-4
`else
            case (bte)
                2'b01:   f_next = {x[ADDR_W-1:2], x[1:0] + 2'd1};
                2'b10:   f_next = {x[ADDR_W-1:3], x[2:0] + 3'd1};
                2'b11:   f_next = {x[ADDR_W-1:4], x[3:0] + 4'd1};
                default: f_next = x + 1'b1;
            endcase
`endif
        end
    endfunction

    // candidate for the ack of the next cycle: the predicted next beat of a continuing burst,
    // or the request on the bus when no ack is asserted
    wire              cand_v     = cont | (~ack_r & ~err_r & req);
    wire              cand_fresh = ~cont & cand_v;
    wire [ADDR_W-1:0] cand_a     = cont ? f_next(a, wb_bte) : a;
    wire              cand_burst = cont | (wb_cti == 3'b010);
    wire [LB-1:0]     cand_i     = cand_a[LB-1:0];
    wire              cand_tag   = (cand_a[ADDR_W-1:LB] == lb_tag);

    wire pc_issue = pc_v & ~p_cmd_full;

    // write segment decisions at this edge
`ifdef MCBWB_F6
    wire contig = seg_open;                         // fault: address continuity not checked
`else
    wire contig = seg_open & (a == seg_next) & (a[LB-1:0] != {LB{1'b0}});
`endif
    wire w_last      = xfer_w & (wb_cti != 3'b010);
    wire w_close_old = xfer_w & seg_open & ~contig;
    wire w_close_new = w_last & ~w_close_old;       // this beat ends the segment it belongs to
    wire w_close_idle = ~xfer_w & seg_open & (seg_close_req | ~wb_cyc) & (~pc_v | pc_issue);
    wire [ADDR_W-1:0] w_new_start = (seg_open & contig) ? seg_start : a;
    wire [5:0]        w_new_n     = (seg_open & contig) ? seg_n + 6'd1 : 6'd1;

    // read side
    wire fill_now  = (rstate == R_FILL) & ~p_rd_empty;
    wire rd_hit    = lb_live & cand_tag & lb_vld[cand_i];
    wire rd_bypass = lb_live & cand_tag & fill_now & (fill_idx == cand_i);
`ifdef MCBWB_F9
    wire rd_order_ok = 1'b1;                        // fault: a read may pass an open write segment
`else
    wire rd_order_ok = ~seg_open;
`endif
    wire rd_issue  = cand_fresh & ~wb_we & cal & (rstate == R_IDLE) & rd_order_ok &
                     (~pc_v | pc_issue) & ~(lb_live & cand_tag);
    wire single_done = (rstate == R_SINGLE) & ~p_rd_empty;

    // command register loads (at most one per edge by construction)
    wire pc_load = w_close_old | w_close_new | w_close_idle | rd_issue;
    wire pc_free_next = (~pc_v | pc_issue) & ~pc_load;
    wire seg_close_req_next = w_close_old & w_last;

    // grants
    wire g_nocal = cand_v & ~cal;
    wire g_rd    = cand_v & ~wb_we & cal & (rd_hit | rd_bypass);
    wire g_wr    = cand_v &  wb_we & cal & pc_free_next & ~seg_close_req_next & ~seg_close_req &
                   (p_wr_count < WR_ROOM) & ~p_wr_full;
    wire g_single = single_done & req & ~wb_we & ~ack_r & ~err_r;

    // ---- MCB port outputs ----------------------------------------------------------------
`ifdef MCBWB_F2
    // fault: the command that closes a burst goes out at the edge that pushes its last word
    wire f2_direct = w_close_new & ~p_cmd_full;
    assign p_cmd_en        = f2_direct | pc_issue;
    assign p_cmd_instr     = f2_direct ? I_WRITE : pc_instr;
    assign p_cmd_bl        = f2_direct ? (w_new_n - 6'd1) : pc_bl;
    assign p_cmd_byte_addr = f2_direct ? {{(28-ADDR_W){1'b0}}, w_new_start, 2'b00}
                                       : {{(28-ADDR_W){1'b0}}, pc_addr, 2'b00};
`elsif MCBWB_F5
    // fault: the top address bit is dropped
    assign p_cmd_en        = pc_issue;
    assign p_cmd_instr     = pc_instr;
    assign p_cmd_bl        = pc_bl;
    assign p_cmd_byte_addr = {{(29-ADDR_W){1'b0}}, pc_addr[ADDR_W-2:0], 2'b00};
`else
    assign p_cmd_en        = pc_issue;
    assign p_cmd_instr     = pc_instr;
    assign p_cmd_bl        = pc_bl;
    assign p_cmd_byte_addr = {{(28-ADDR_W){1'b0}}, pc_addr, 2'b00};
`endif
    assign p_wr_en   = xfer_w;
`ifdef MCBWB_F1
    assign p_wr_mask = wb_sel;                      // fault: mask polarity
`else
    assign p_wr_mask = ~wb_sel;
`endif
    assign p_wr_data = wb_dat_w;
    assign p_rd_en   = ((rstate == R_SINGLE) | (rstate == R_FILL)) & ~p_rd_empty;

    // ---- registers -------------------------------------------------------------------------
    integer k;
    always @(posedge clk) begin
        if (rst) begin
            pc_v <= 1'b0; pc_instr <= I_WRITE; pc_bl <= 6'd0; pc_addr <= {ADDR_W{1'b0}};
            seg_open <= 1'b0; seg_close_req <= 1'b0; seg_start <= {ADDR_W{1'b0}};
            seg_next <= {ADDR_W{1'b0}}; seg_n <= 6'd0;
            rstate <= R_IDLE; lb_vld <= {LW{1'b0}}; lb_tag <= {(ADDR_W-LB){1'b0}}; lb_live <= 1'b0;
            fill_idx <= {LB{1'b0}};
            ack_r <= 1'b0; err_r <= 1'b0; wb_dat_r <= 32'd0; ack_nocal <= 1'b0;
            st_nocal <= 1'b0; st_wr <= 1'b0; st_rd <= 1'b0;
        end else begin
            // pending command
            if (pc_issue) pc_v <= 1'b0;
            if (w_close_old | w_close_idle) begin
                pc_v <= 1'b1; pc_instr <= I_WRITE; pc_bl <= seg_n - 6'd1; pc_addr <= seg_start;
            end else if (w_close_new) begin
`ifndef MCBWB_F2
                pc_v <= 1'b1;
`endif
                pc_instr <= I_WRITE; pc_bl <= w_new_n - 6'd1; pc_addr <= w_new_start;
            end else if (rd_issue) begin
                pc_v <= 1'b1; pc_instr <= I_READ;
                if (cand_burst) begin
                    pc_bl <= LW - 1;
`ifdef MCBWB_F3
                    pc_addr <= cand_a;              // fault: line read not from the line base
`else
                    pc_addr <= {cand_a[ADDR_W-1:LB], {LB{1'b0}}};
`endif
                end else begin
                    pc_bl <= 6'd0; pc_addr <= cand_a;
                end
            end
`ifdef MCBWB_F2
            if (f2_direct) pc_v <= 1'b0;
`endif

            // write segment
            if (w_close_old) begin
                seg_open <= 1'b1; seg_start <= a; seg_n <= 6'd1; seg_next <= a + 1'b1;
                seg_close_req <= w_last;
            end else if (w_close_new) begin
                seg_open <= 1'b0; seg_close_req <= 1'b0;
            end else if (xfer_w) begin
                seg_open <= 1'b1; seg_start <= w_new_start; seg_n <= w_new_n; seg_next <= a + 1'b1;
            end else if (w_close_idle) begin
                seg_open <= 1'b0; seg_close_req <= 1'b0;
            end

            // read engine
            case (rstate)
                R_IDLE: if (rd_issue) begin
                    if (cand_burst) begin
                        rstate <= R_FILL; fill_idx <= {LB{1'b0}}; lb_vld <= {LW{1'b0}};
                        lb_tag <= cand_a[ADDR_W-1:LB];
                    end else
                        rstate <= R_SINGLE;
                end
                R_SINGLE: if (single_done) rstate <= R_IDLE;
                R_FILL: if (fill_now) begin
                    lb[fill_idx] <= p_rd_data;
                    lb_vld[fill_idx] <= 1'b1;
                    fill_idx <= fill_idx + 1'b1;
                    if (fill_idx == LW - 1) rstate <= R_IDLE;
                end
                default: rstate <= R_IDLE;
            endcase

            // line buffer lifetime: the burst that fetched it
`ifdef MCBWB_F7
            if (rd_issue & cand_burst) lb_live <= 1'b1;   // fault: never invalidated
`else
            if (rd_issue & cand_burst)                              lb_live <= 1'b1;
            else if (~wb_cyc | xfer_w | (xfer & ~wb_we & ~cont))    lb_live <= 1'b0;
`endif

            // answer
            ack_r <= 1'b0; err_r <= 1'b0; ack_nocal <= 1'b0;
            if (g_nocal) begin
                ack_nocal <= 1'b1; wb_dat_r <= NOCAL_DATA;
                if (NOCAL_ERR != 0) err_r <= 1'b1; else ack_r <= 1'b1;
            end else if (g_rd) begin
                ack_r <= 1'b1;
                wb_dat_r <= rd_hit ? lb[cand_i] : p_rd_data;
            end else if (g_wr) begin
                ack_r <= 1'b1;
            end else if (g_single) begin
                ack_r <= 1'b1; wb_dat_r <= p_rd_data;
            end

            // status
            if (st_clear) begin
                st_nocal <= 1'b0; st_wr <= 1'b0; st_rd <= 1'b0;
            end else begin
                if (g_nocal)                        st_nocal <= 1'b1;
                if (p_wr_underrun | p_wr_error)     st_wr <= 1'b1;
                if (p_rd_overflow | p_rd_error)     st_rd <= 1'b1;
            end
        end
    end

    // The answer is visible only while the master strobes: a burst master may drop STB between
    // beats (CYC held), and a predicted ack must not appear in such a gap.
`ifdef MCBWB_F10
    assign wb_ack = ack_r;                          // fault: ack not gated by STB
    assign wb_err = err_r;
`else
    assign wb_ack = ack_r & wb_cyc & wb_stb;
    assign wb_err = err_r & wb_cyc & wb_stb;
`endif

    assign status = {3'b000, (pc_v | seg_open | (rstate != R_IDLE)), st_rd, st_wr, st_nocal, cal_s1};
endmodule

`default_nettype wire
