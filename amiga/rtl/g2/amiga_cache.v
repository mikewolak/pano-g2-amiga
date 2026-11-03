/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// amiga_cache.v -- two-way set-associative write-back cache between the 16-bit chip-memory port of the Amiga core and
// a 32-bit Spartan-6 MCB user port (DDR2).  the subsystem slot's byte cache with a 16-bit core port and two byte enables;
// the line engine, tags, maintenance and MCB side are that module's, unchanged.
//
// WORD LAYOUT
//   The core's 16-bit word at byte address A (A[0] = 0) is half A[1] of the MCB word at A[AW-1:2]: bits 15:0 for
//   A[1] = 0, bits 31:16 for A[1] = 1, the core's high byte (D15..D8) in the upper byte of the half.  A big-endian
//   image (Kickstart) is therefore stored with the two bytes of every word exchanged.
//
// ORGANISATION
//   2 ways x 2^SET_LOG2 sets x 64-byte lines.  SET_LOG2 8: 2 x 16 KB = 32 KB.
//   Each way's data is four byte lanes of block RAM (lane = address bits 1:0); port A serves the core one 16-bit
//   word (two lanes) at a time, port B moves whole 32-bit words for line fills and write-backs.  Tags, valid, dirty
//   and the LRU bit are LUT RAM, one word a set (the state section below); invalidate-all is a scan of one set a
//   clock, during which no lookup completes (no hit, no miss), so the core waits on its first access.
//
// CORE SIDE (clk, every signal synchronous to clk; the core's gated clock has its edges on clk edges)
//   a_addr, a_req, a_we, a_be, a_wdata describe the access the core is making in its current bus cycle (a_be[1]
//   writes the high byte, a_be[0] the low byte).
//   The data RAM reads a_addr on every clk edge.  a_conf is high when the tags have been compared for exactly the
//   current a_addr and the line is present: a_q is then the word at a_addr.  a_commit is the clock-gate's pulse for
//   a core edge issued while the access is current (amiga_g2.v); with a_we it writes the enabled bytes of a_wdata and
//   marks the line dirty,
//   and it updates the set's LRU bit.  The gate must not issue the consuming edge while a_req && !a_conf.
//   A lookup that misses for the current a_addr requests the line at once (before the consuming edge), so DDR2
//   latency overlaps the rest of the bus cycle.
//
// LINE ENGINE (MCB port, owned for one whole transaction through busreq/busgnt)
//   victim = an invalid way, else the LRU way.  A dirty victim is read from port B into the port's write FIFO
//   (16 words) and written with one 16-word command; then one 16-word read command fetches the line, which is
//   written through port B; then the tag, valid, dirty and LRU state are updated.  Commands on one MCB port
//   execute in order, so a line written back is never read back stale.
//
// MAINTENANCE
//   inv (pulse) and reset: every valid bit cleared by the scan (2^SET_LOG2 clocks).  flush (pulse): every valid dirty line written back and cleaned, then
//   flush_busy falls.  Both are issued by firmware with the core's clock stopped.

`timescale 1ns / 1ps
`default_nettype none

module amiga_cache #(
    parameter integer AW       = 24,          // byte address width of the cached space
    parameter integer SET_LOG2 = 8,           // sets per way
    parameter [29:0]  DDR_BASE = 30'h0200_0000 // MCB byte address of cached-space address 0
) (
    input  wire          clk,
    input  wire          rst,

    input  wire [AW-1:0] a_addr,
    input  wire          a_req,
    input  wire          a_we,
    input  wire [1:0]    a_be,
    input  wire [15:0]   a_wdata,
    output wire [15:0]   a_q,
    output wire [47:0]   a_q48,       // the other three words of a_addr's aligned 8-byte group (AGA wide fetch)
    output wire          a_conf,
    input  wire          a_commit,

    input  wire          inv,
    input  wire          flush,
    output wire          flush_busy,

    output reg  [31:0]   misses,
    output reg  [31:0]   writebacks,

    output reg           busreq,
    input  wire          busgnt,
    output reg           p_cmd_en,
    output reg  [2:0]    p_cmd_instr,
    output wire [5:0]    p_cmd_bl,
    output reg  [29:0]   p_cmd_byte_addr,
    input  wire          p_cmd_full,
    output reg           p_wr_en,
    output wire [3:0]    p_wr_mask,
    output wire [31:0]   p_wr_data,
    input  wire          p_wr_empty,
    output wire          p_rd_en,
    input  wire [31:0]   p_rd_data,
    input  wire          p_rd_empty
);
    localparam integer LINE_LOG2 = 6;                      // 64-byte line, 16 words
    localparam integer SETS      = 1 << SET_LOG2;
    localparam integer WAW       = SET_LOG2 + LINE_LOG2 - 2; // word address inside a way
    localparam integer TW        = AW - SET_LOG2 - LINE_LOG2; // tag width

    function [SET_LOG2-1:0] set_of(input [AW-1:0] a); set_of = a[SET_LOG2+LINE_LOG2-1:LINE_LOG2]; endfunction
    function [TW-1:0]       tag_of(input [AW-1:0] a); tag_of = a[AW-1:SET_LOG2+LINE_LOG2];        endfunction

    // ---- state ----------------------------------------------------------------------------------------------------
    // Every per-set state bit is in distributed RAM, one word a set, not in flip-flops: {valid, tag} for each way (one
    // RAM a way, written by the line engine only, at m_set), dirty for each way and the LRU bit (written by the engine
    // at m_set, and by the core's commit through the one-entry register c_* below).  Two read addresses: la_set (the
    // lookup) and m_set (the engine).  XST 14.7 maps a RAM written at an address it also reads asynchronously into its
    // address register when inferred as block RAM, so every array is ram_style distributed.
    (* ram_style = "distributed" *) reg  [TW:0]     tv0 [0:SETS-1];   // {valid, tag} of way 0
    (* ram_style = "distributed" *) reg  [TW:0]     tv1 [0:SETS-1];
    (* ram_style = "distributed" *) reg             dr0 [0:SETS-1];   // dirty of way 0
    (* ram_style = "distributed" *) reg             dr1 [0:SETS-1];
    (* ram_style = "distributed" *) reg             lr  [0:SETS-1];   // 1: way 1 is the least recently used
    integer k;
    initial for (k = 0; k < SETS; k = k + 1) begin tv0[k] = {(TW+1){1'b0}}; tv1[k] = {(TW+1){1'b0}}; dr0[k] = 1'b0; dr1[k] = 1'b0; lr[k] = 1'b0; end

    // ---- lookup ---------------------------------------------------------------------------------------------------
    reg  [AW-1:0]   la;                                    // address the data RAM is reading this cycle
    reg  [AW-1:0]   hit_addr;
    reg             hit_r, hit_way, cmp_ok;
    reg             lv0, lv1, llru;                        // valid bits and LRU of the looked-up set, for the victim
    reg             inv_busy;                              // invalidate-all scan pending or running
    always @(posedge clk) la <= a_addr;
    wire [SET_LOG2-1:0] la_set = set_of(la);
    wire [SET_LOG2-1:0] a_set  = set_of(a_addr);           // the set of the current access
    wire [TW:0] lt0 = tv0[la_set], lt1 = tv1[la_set];
    wire h0 = lt0[TW] && (lt0[TW-1:0] == tag_of(la));
    wire h1 = lt1[TW] && (lt1[TW-1:0] == tag_of(la));
    reg  tag_wr;                                           // a tag or valid update this clock invalidates the compare below
    always @(posedge clk) begin
        if (rst) begin hit_r <= 1'b0; cmp_ok <= 1'b0; hit_way <= 1'b0; hit_addr <= {AW{1'b0}}; end
        else begin
            hit_r    <= h0 | h1;
            cmp_ok   <= ~tag_wr & ~inv & ~inv_busy;  // a compare that saw a state update in flight is not used
            hit_way  <= h1;
            hit_addr <= la;
        end
        lv0 <= lt0[TW]; lv1 <= lt1[TW]; llru <= lr[la_set];
    end
    wire same_addr = (hit_addr == a_addr);
    assign a_conf = hit_r & cmp_ok & same_addr;
    wire looked_miss = ~hit_r & cmp_ok & same_addr & a_req;         // compared, absent, wanted

    // ---- data RAM: 2 ways x 2 banks x 4 lanes, port A byte, port B word ----------------------------------------------
    // Each way's 32-bit words are split by word-address bit 0 into an even bank and an odd bank (each half the way's
    // depth), so port A reads the two words of an 8-byte group in one clock: a_q is the word at a_addr, a_q48 the other
    // three words of its aligned group {word 1, word 2, word 3} (the AGA chipset's wide fetch, minimig chip48).  Port
    // B moves one 32-bit word a clock into or out of the bank its address selects.
    wire [WAW-1:0] wa_a = a_addr[SET_LOG2+LINE_LOG2-1:2];
    reg            half_r, bank_r;
    always @(posedge clk) begin half_r <= a_addr[1]; bank_r <= a_addr[2]; end
    wire           wr_a   = a_commit & a_we & a_conf;
    reg            pb_we;  reg pb_way; reg [WAW-1:0] pb_addr; reg [31:0] pb_wdata;
    reg            pb_bank_r;
    always @(posedge clk) pb_bank_r <= pb_addr[0];
    wire [31:0]    pb_e0, pb_o0, pb_e1, pb_o1;          // port B words: even / odd bank, way 0 / 1
    wire [31:0]    pa_e0, pa_o0, pa_e1, pa_o1;          // port A words
    wire [31:0]    pb_q0 = pb_bank_r ? pb_o0 : pb_e0;
    wire [31:0]    pb_q1 = pb_bank_r ? pb_o1 : pb_e1;
    genvar gl;
    generate for (gl = 0; gl < 4; gl = gl + 1) begin : g_lane
        // lane gl holds the low byte (gl even) or the high byte (gl odd) of half gl / 2 of a 32-bit word
        wire lane_we = (a_addr[1] == (gl / 2)) & ((gl % 2) ? a_be[1] : a_be[0]);
        wire [7:0] lane_d = (gl % 2) ? a_wdata[15:8] : a_wdata[7:0];
        wire       aw_e = wr_a & lane_we & ~a_addr[2], aw_o = wr_a & lane_we & a_addr[2];
        wire       bw_e = pb_we & ~pb_addr[0],         bw_o = pb_we & pb_addr[0];
        amiga_tdp8 #(.AW(WAW-1)) u_e0 (.clk(clk),
            .a_we(aw_e & ~hit_way), .a_addr(wa_a[WAW-1:1]), .a_d(lane_d), .a_q(pa_e0[8*gl+7:8*gl]),
            .b_we(bw_e & ~pb_way), .b_addr(pb_addr[WAW-1:1]), .b_d(pb_wdata[8*gl+7:8*gl]), .b_q(pb_e0[8*gl+7:8*gl]));
        amiga_tdp8 #(.AW(WAW-1)) u_o0 (.clk(clk),
            .a_we(aw_o & ~hit_way), .a_addr(wa_a[WAW-1:1]), .a_d(lane_d), .a_q(pa_o0[8*gl+7:8*gl]),
            .b_we(bw_o & ~pb_way), .b_addr(pb_addr[WAW-1:1]), .b_d(pb_wdata[8*gl+7:8*gl]), .b_q(pb_o0[8*gl+7:8*gl]));
        amiga_tdp8 #(.AW(WAW-1)) u_e1 (.clk(clk),
            .a_we(aw_e & hit_way), .a_addr(wa_a[WAW-1:1]), .a_d(lane_d), .a_q(pa_e1[8*gl+7:8*gl]),
            .b_we(bw_e & pb_way), .b_addr(pb_addr[WAW-1:1]), .b_d(pb_wdata[8*gl+7:8*gl]), .b_q(pb_e1[8*gl+7:8*gl]));
        amiga_tdp8 #(.AW(WAW-1)) u_o1 (.clk(clk),
            .a_we(aw_o & hit_way), .a_addr(wa_a[WAW-1:1]), .a_d(lane_d), .a_q(pa_o1[8*gl+7:8*gl]),
            .b_we(bw_o & pb_way), .b_addr(pb_addr[WAW-1:1]), .b_d(pb_wdata[8*gl+7:8*gl]), .b_q(pb_o1[8*gl+7:8*gl]));
    end endgenerate
    wire [31:0] pa_e = hit_way ? pa_e1 : pa_e0, pa_o = hit_way ? pa_o1 : pa_o0;
    wire [31:0] pa_w = bank_r ? pa_o : pa_e;
    assign a_q   = half_r ? pa_w[31:16] : pa_w[15:0];
    assign a_q48 = {pa_e[31:16], pa_o[15:0], pa_o[31:16]};

    // ---- line engine ----------------------------------------------------------------------------------------------
    localparam [3:0] S_IDLE = 4'd0, S_REQ = 4'd1, S_WBRD = 4'd2, S_WBCMD = 4'd3, S_RDCMD = 4'd4, S_FILL = 4'd5,
                     S_TAG = 4'd6, S_SETTLE = 4'd7, S_FSCAN = 4'd8, S_FCHK = 4'd9, S_INV = 4'd10;
    reg  [3:0]          st;
    reg  [AW-1:0]       m_addr;                            // line being fetched; the set being scanned
    reg                 v_way, v_dirty;
    reg  [TW-1:0]       v_tag;
    reg  [4:0]          wcnt;                              // words issued (write-back read) or received (fill)
    reg                 wb_pipe;                           // a port-B read issued last clock
    reg                 flushing;
    reg  [SET_LOG2:0]   fidx;                              // flush scan: {way, set}
    reg                 fill_needed;
    reg                 inv_req;                           // an invalidate-all is owed (from inv or reset)
    wire [SET_LOG2-1:0] m_set = set_of(m_addr);
    wire [TW:0]         mt0 = tv0[m_set], mt1 = tv1[m_set];
    wire                md0 = dr0[m_set], md1 = dr1[m_set];

    assign p_cmd_bl   = 6'd15;
    assign p_wr_mask  = 4'b0000;
    assign p_wr_data  = v_way ? pb_q1 : pb_q0;
    assign p_rd_en    = (st == S_FILL) & ~p_rd_empty;
    assign flush_busy = flushing;

    // the core's commit: LRU of its set, and dirty for a write; held one clock when the engine writes the same RAMs
    reg                 c_pend, c_way, c_we;
    reg  [SET_LOG2-1:0] c_set;
    reg                 e_dw, e_dval, e_dway, e_lw, e_lval;   // the engine's dirty and LRU writes this clock
    always @(*) begin
        e_dw = 1'b0; e_dval = 1'b0; e_dway = v_way; e_lw = 1'b0; e_lval = ~v_way;
        if (!rst) case (st)
            S_WBCMD: if (!p_cmd_full) begin e_dw = 1'b1; e_dval = 1'b0; end
            S_TAG:   if (fill_needed) begin e_dw = 1'b1; e_dval = 1'b0; e_lw = 1'b1; end
            default: ;
        endcase
    end
    // one write a clock per array, its address, data and enable multiplexed here: XST maps a distributed RAM with one
    // write statement; two writes at different addresses in the process gave flip-flops and read multiplexers
    wire                c_go = c_pend & ~e_dw & ~e_lw;   // the core's commit is written this clock
    wire                dw   = e_dw | (c_go & c_we);
    wire [SET_LOG2-1:0] dwa  = e_dw ? m_set : c_set;
    wire                dwy  = e_dw ? e_dway : c_way;
    wire                dwv  = e_dw ? e_dval : 1'b1;
    wire                lw   = e_lw | c_go;
    wire [SET_LOG2-1:0] lwa  = e_lw ? m_set : c_set;
    wire                lwv  = e_lw ? e_lval : ~c_way;
    always @(posedge clk) if (dw & ~dwy) dr0[dwa] <= dwv;
    always @(posedge clk) if (dw &  dwy) dr1[dwa] <= dwv;
    always @(posedge clk) if (lw)        lr[lwa]  <= lwv;
    always @(posedge clk) begin
        if (rst) c_pend <= 1'b0;
        else if (a_commit & a_conf & a_req) begin c_pend <= 1'b1; c_set <= a_set; c_way <= hit_way; c_we <= a_we; end
        else if (!e_dw && !e_lw) c_pend <= 1'b0;
    end

    always @(posedge clk) begin
        tag_wr   <= 1'b0;
        p_cmd_en <= 1'b0;
        p_wr_en  <= wb_pipe;                               // the word read from port B last clock
        wb_pipe  <= 1'b0;
        pb_we    <= 1'b0;
        if (inv) inv_req <= 1'b1;

        if (rst) begin
            st <= S_IDLE; busreq <= 1'b0; flushing <= 1'b0; misses <= 32'd0; writebacks <= 32'd0;
            p_wr_en <= 1'b0; wb_pipe <= 1'b0; inv_req <= 1'b1; inv_busy <= 1'b1;
        end else begin
            inv_busy <= inv_req | inv | (st == S_INV);
            case (st)
            S_IDLE: begin
                if (inv_req | inv) begin                   // every valid bit cleared, one set a clock
                    inv_req <= 1'b0; m_addr <= {AW{1'b0}}; st <= S_INV;
                end else if (flush) begin
                    flushing <= 1'b1; fidx <= {(SET_LOG2+1){1'b0}}; st <= S_FSCAN;
                end else if (looked_miss) begin
                    m_addr <= a_addr;
                    misses <= misses + 32'd1;
                    fill_needed <= 1'b1;
                    // victim: an invalid way, else the least recently used (the looked-up set: la is a_addr here)
                    if (!lv0)      v_way <= 1'b0;
                    else if (!lv1) v_way <= 1'b1;
                    else           v_way <= llru;
                    st <= S_REQ;
                end
            end
            S_INV: begin
                tv0[m_set] <= {(TW+1){1'b0}}; tv1[m_set] <= {(TW+1){1'b0}};
                tag_wr <= 1'b1;
                if (&m_set) st <= S_IDLE;
                m_addr <= m_addr + (1 << LINE_LOG2);
            end
            S_FSCAN: begin                                 // flush: the next {way, set} to look at
                m_addr <= {{TW{1'b0}}, fidx[SET_LOG2-1:0], {LINE_LOG2{1'b0}}};
                st <= S_FCHK;
            end
            S_FCHK: begin                                  // m_set is fidx's set: is that way's line valid and dirty
                if (fidx[SET_LOG2] ? (mt1[TW] & md1) : (mt0[TW] & md0)) begin
                    v_way <= fidx[SET_LOG2];
                    fill_needed <= 1'b0;
                    st <= S_REQ;
                end else if (&fidx) begin
                    flushing <= 1'b0; st <= S_IDLE;
                end else begin fidx <= fidx + 1'b1; st <= S_FSCAN; end
            end
            S_REQ: begin
                // victim state, latched once the set is known
                v_dirty <= v_way ? (mt1[TW] & md1) : (mt0[TW] & md0);
                v_tag   <= v_way ? mt1[TW-1:0] : mt0[TW-1:0];
                busreq  <= 1'b1;
                if (busgnt & busreq) begin
                    wcnt <= 5'd0;
                    if (v_dirty) begin
                        writebacks <= writebacks + 32'd1;
                        st <= S_WBRD;
                    end else st <= fill_needed ? S_RDCMD : S_TAG;
                end
            end
            S_WBRD: begin                                  // 16 port-B reads, pushed into the write FIFO a clock later
                pb_way  <= v_way;
                pb_addr <= {m_set, wcnt[3:0]};
                if (wcnt != 5'd16) begin wb_pipe <= 1'b1; wcnt <= wcnt + 5'd1; end
                else if (!wb_pipe && !p_wr_en) st <= S_WBCMD;
            end
            S_WBCMD: if (!p_cmd_full) begin                // dirty cleared by e_dw
                p_cmd_en <= 1'b1; p_cmd_instr <= 3'b000;
                p_cmd_byte_addr <= DDR_BASE + {{(30-AW){1'b0}}, v_tag, m_set, {LINE_LOG2{1'b0}}};
                wcnt <= 5'd0;
                st <= fill_needed ? S_RDCMD : S_SETTLE;
            end
            S_RDCMD: if (!p_cmd_full && !p_cmd_en) begin
                p_cmd_en <= 1'b1; p_cmd_instr <= 3'b001;
                p_cmd_byte_addr <= DDR_BASE + {{(30-AW){1'b0}}, m_addr[AW-1:LINE_LOG2], {LINE_LOG2{1'b0}}};
                // the line is about to be overwritten: no longer valid until the tag update
                if (v_way) tv1[m_set] <= {1'b0, v_tag}; else tv0[m_set] <= {1'b0, v_tag};
                tag_wr <= 1'b1;
                wcnt <= 5'd0;
                st <= S_FILL;
            end
            S_FILL: begin
                if (!p_rd_empty) begin
                    pb_we <= 1'b1; pb_way <= v_way; pb_addr <= {m_set, wcnt[3:0]}; pb_wdata <= p_rd_data;
                    wcnt <= wcnt + 5'd1;
                    if (wcnt == 5'd15) st <= S_TAG;
                end
            end
            S_TAG: begin                                   // dirty and LRU by e_dw, e_lw
                if (fill_needed) begin
                    if (v_way) tv1[m_set] <= {1'b1, tag_of(m_addr)}; else tv0[m_set] <= {1'b1, tag_of(m_addr)};
                end
                tag_wr <= 1'b1;
                st <= S_SETTLE;
            end
            S_SETTLE: begin                                // wait for the write data to leave the port
                if (p_wr_empty) begin
                    busreq <= 1'b0;
                    if (flushing) begin
                        if (&fidx) begin flushing <= 1'b0; st <= S_IDLE; end
                        else begin fidx <= fidx + 1'b1; st <= S_FSCAN; end
                    end else st <= S_IDLE;
                end
            end
            default: st <= S_IDLE;
            endcase
        end
    end
endmodule

// One byte-wide true dual-port block RAM, one clock, read-first on both ports.
module amiga_tdp8 #(parameter integer AW = 12) (
    input  wire          clk,
    input  wire          a_we,
    input  wire [AW-1:0] a_addr,
    input  wire [7:0]    a_d,
    output reg  [7:0]    a_q,
    input  wire          b_we,
    input  wire [AW-1:0] b_addr,
    input  wire [7:0]    b_d,
    output reg  [7:0]    b_q
);
    reg [7:0] mem [0:(1<<AW)-1];
    always @(posedge clk) begin
        if (a_we) mem[a_addr] <= a_d;
        a_q <= mem[a_addr];
    end
    always @(posedge clk) begin
        if (b_we) mem[b_addr] <= b_d;
        b_q <= mem[b_addr];
    end
endmodule

`default_nettype wire
