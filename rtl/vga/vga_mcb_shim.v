/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vga_mcb_shim.v -- rtl/vga/vga_scan.v's four-bank read client, served from one Spartan-6 MCB user port.
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// 1 CLIENT SIDE.  Identical to rtl/vga/vga_lpddr_shim.v, so vga_scan.v is instantiated unchanged above it:
//     vga_scan issues one sequential 16-bit word per clock, bank = word address [1:0],
//     req_row[bank*12 +: 12] = word address [21:10], req_col[bank*8 +: 8] = word address [9:2];
//     rdy is sampled one clock before the issue it permits; returns come back in issue order (which satisfies
//     vga_scan's per-bank in-order rule), one per clock at most, tagged with the bank.
//     vga_scan can hold 16 words outstanding (4 per bank); the request queue holds 32 and advertises room only
//     while two or more entries are free (rdy is registered, so it must promise one more slot than it needs).
//
// 2 MEMORY SIDE.  One MCB user port configured 32 bits wide, read commands only (UG388 v2.3 table 2-5, 2-7):
//     pX_cmd_instr = 3'b001 (read), pX_cmd_bl = words - 1, pX_cmd_byte_addr 4-byte aligned;
//     pX_rd_data is the head of the read-data FIFO and is valid while pX_rd_empty is low; a clock with
//     pX_rd_en = 1 and pX_rd_empty = 0 removes it.  The read-data FIFO holds 64 words, and a read command whose
//     data does not fit overflows it (UG388 chapter 4, "Simple Read"), so this block never has more than 64
//     words issued and not yet removed.  The MCB converts the byte address to bank/row/column and crosses row
//     boundaries itself (UG388 chapter 4, "Byte Address to Memory Address Conversion").
//     All MCB port signals are on `clk' (pX_cmd_clk = pX_rd_clk = clk).
//
// 3 FETCH STRUCTURE.  Two slots of one 128-byte block each (32 port words = 64 client words), in distributed
//     RAM (64 x 32 bits).  A block is fetched with one 32-word read command, 128-byte aligned.
//     - the queue head is a HIT when a slot holds its block and has received the port word it lies in; a hit
//       is returned the same clock it is found, one per clock.
//     - the head's block not held by either slot: a read for it is issued into a slot that has no read in
//       flight (the slot that holds the block after it is kept when both are free).
//     - the head's block held: the block after it is read into the other slot (sequential prefetch).  A line
//       walks its blocks in order, so while one slot is being returned to the client the next is in flight.
//     Two slots and 32-word reads bound the words in flight at 64, the read-data FIFO's depth.  The slot the
//     queue head is in is never chosen for a new read, so a returned word can never be overwritten before it
//     is returned.
//     A prefetch past the end of a line reads one block the client does not ask for: one extra 128-byte read
//     per scanned source line.
//
// 4 ADDRESS.  Client word address w (23 bits, 16-bit words: req_row/req_col and req_hi = bit 22) is MCB byte address
//     BYTE_BASE + 2*w: a 16 MB window
//     from BYTE_BASE.  The 32-bit port word at byte address 4*n holds client word 2n in bits [15:0] and 2n+1 in
//     bits [31:16] (the MCB's byte lanes are little-endian: pX_rd_data[7:0] is the byte at the lowest address).
//
// 5 ENABLE.  `en' low stops new read commands; requests are still accepted into the queue, as vga_lpddr_shim
//     accepts them while the head holds no grant.  Reads already issued complete.
`timescale 1ns / 1ps
`default_nettype none
module vga_mcb_shim #(
    parameter        QD        = 5,               // request queue: 1 << QD entries
    parameter [29:0] BYTE_BASE = 30'd0            // MCB byte address of client word 0
) (
    input  wire        clk,
    input  wire        rst,                        // synchronous to clk, active high
    input  wire        en,                         // synchronous to clk
    input  wire        calib_done,                 // MCB calibration complete (any domain; registered here)

    // ---- vga_scan's client port ----
    input  wire [3:0]  req,
    input  wire [47:0] req_row,
    input  wire [3:0]  req_hi,                     // word address bit 22 per bank
    input  wire [31:0] req_col,
    output reg  [3:0]  rdy,
    output reg  [15:0] rdata,
    output reg         rvalid,
    output reg  [1:0]  rbank,

    // ---- MCB user port, 32-bit, read only ----
    output reg         mcb_cmd_en,
    output wire [2:0]  mcb_cmd_instr,
    output wire [5:0]  mcb_cmd_bl,
    output reg  [29:0] mcb_cmd_byte_addr,
    input  wire        mcb_cmd_full,
    output wire        mcb_rd_en,
    input  wire [31:0] mcb_rd_data,
    input  wire        mcb_rd_empty,
    input  wire        mcb_rd_overflow,
    input  wire        mcb_rd_error,

    // ---- status ----
    output reg  [31:0] cmd_count,                  // read commands issued
    output reg         err                         // sticky: command into a full FIFO, a word with no read in flight, rd_overflow, rd_error
);
    localparam integer QN = (1 << QD);

    assign mcb_cmd_instr = 3'b001;                 // read
    assign mcb_cmd_bl    = 6'd31;                  // 32 words = 128 bytes

    // ---- declarations (XST requires declare-before-use) ----
    reg  [22:0] q_addr [0:QN-1];
    reg  [QD:0] q_wp, q_rp;
    reg  [22:0] head_q;
    reg  [31:0] bufm [0:63];                       // {slot, word}: distributed RAM
    reg  [16:0] tag0, tag1;
    reg         tv0, tv1;                          // tag valid
    reg  [5:0]  fc0, fc1;                          // words received, 0..32
    reg         pd0, pd1;                          // read in flight
    reg         old;                               // with both in flight: the slot whose read was issued first
    reg         cal_q, cal_qq;
    integer     i;

    initial begin
        rdy = 4'd0; rdata = 16'd0; rvalid = 1'b0; rbank = 2'd0;
        mcb_cmd_en = 1'b0; mcb_cmd_byte_addr = 30'd0; cmd_count = 32'd0; err = 1'b0;
        q_wp = 0; q_rp = 0; head_q = 23'd0;
        tag0 = 17'd0; tag1 = 17'd0; tv0 = 1'b0; tv1 = 1'b0; fc0 = 6'd0; fc1 = 6'd0; pd0 = 1'b0; pd1 = 1'b0; old = 1'b0;
        cal_q = 1'b0; cal_qq = 1'b0;
        for (i = 0; i < QN; i = i + 1) q_addr[i] = 23'd0;
        for (i = 0; i < 64; i = i + 1) bufm[i] = 32'd0;
    end

    always @(posedge clk) begin cal_q <= calib_done; cal_qq <= cal_q; end

    // ---- request queue ----
    wire        q_empty = (q_wp == q_rp);
    wire        q_full  = (q_wp[QD-1:0] == q_rp[QD-1:0]) && (q_wp[QD] != q_rp[QD]);
    wire [QD:0] q_cnt   = q_wp - q_rp;
    wire        q_room  = (q_cnt <= (QN[QD:0] - 2));
    wire        q_push  = |req && !q_full && cal_qq;
    wire [QD:0] q_rp1   = q_rp + 1'b1;

    reg  [1:0]  in_bank;
    reg  [22:0] in_addr;
    integer     b;
    always @(*) begin
        in_bank = 2'd0;
        for (b = 3; b >= 0; b = b - 1) if (req[b]) in_bank = b[1:0];
        in_addr = {req_hi[in_bank], req_row[in_bank*12 +: 12], req_col[in_bank*8 +: 8], in_bank};
    end

    // ---- head decode ----
    wire [16:0] hb   = head_q[22:6];              // 128-byte block
    wire [4:0]  hdw  = head_q[5:1];               // port word within the block
    wire        hhi  = head_q[0];                 // upper half of the port word
    wire        pr0  = tv0 && (tag0 == hb);
    wire        pr1  = tv1 && (tag1 == hb);
    wire        hit0 = pr0 && (fc0 > {1'b0, hdw});
    wire        hit1 = pr1 && (fc1 > {1'b0, hdw});
    wire        hit  = hit0 | hit1;
    wire        q_pop = !q_empty && hit;
    wire [31:0] hword = bufm[{hit1, hdw}];
    wire [16:0] nb   = hb + 17'd1;
    wire        nx0  = tv0 && (tag0 == nb);
    wire        nx1  = tv1 && (tag1 == nb);

    // ---- read-data FIFO: every word is taken the clock it is presented ----
    wire        fsl     = (pd0 && pd1) ? old : pd1;   // the slot the next returned word belongs to
    wire        any_pd  = pd0 | pd1;
    assign      mcb_rd_en = !mcb_rd_empty;
    wire        rd_take = !mcb_rd_empty;
    wire [5:0]  fc_f    = fsl ? fc1 : fc0;

    // ---- issue decision ----
    reg         iss;
    reg         iss_slot;
    reg  [16:0] iss_blk;
    always @(*) begin
        iss = 1'b0; iss_slot = 1'b0; iss_blk = hb;
        if (en && cal_qq && !q_empty && !mcb_cmd_en) begin
            if (!pr0 && !pr1) begin
                iss_blk = hb;
                if (!pd0 && !pd1)  begin iss = 1'b1; iss_slot = nx0 ? 1'b1 : 1'b0; end
                else if (!pd0)     begin iss = 1'b1; iss_slot = 1'b0; end
                else if (!pd1)     begin iss = 1'b1; iss_slot = 1'b1; end
            end else if (!nx0 && !nx1) begin
                iss_blk  = nb;
                iss_slot = pr0 ? 1'b1 : 1'b0;         // the slot the head is NOT in
                iss      = pr0 ? !pd1 : !pd0;
            end
        end
    end

    always @(posedge clk) begin
        rvalid     <= 1'b0;
        mcb_cmd_en <= 1'b0;
        rdy        <= (q_room && cal_qq) ? 4'b1111 : 4'b0000;

        if (rst) begin
            q_wp <= 0; q_rp <= 0; head_q <= 23'd0; rdy <= 4'd0;
            tv0 <= 1'b0; tv1 <= 1'b0; pd0 <= 1'b0; pd1 <= 1'b0; fc0 <= 6'd0; fc1 <= 6'd0; old <= 1'b0;
            cmd_count <= 32'd0; err <= 1'b0;
        end else begin
            if (q_push) begin
                q_addr[q_wp[QD-1:0]] <= in_addr;
                q_wp <= q_wp + 1'b1;
            end
            if (q_pop) begin
                rdata  <= hhi ? hword[31:16] : hword[15:0];
                rbank  <= head_q[1:0];
                rvalid <= 1'b1;
                q_rp   <= q_rp1;
            end
            if (q_pop)                  head_q <= (q_push && q_wp == q_rp1) ? in_addr : q_addr[q_rp1[QD-1:0]];
            else if (q_push && q_empty) head_q <= in_addr;

            // a returned word: into the slot whose read is oldest
            if (rd_take) begin
                if (!any_pd) err <= 1'b1;
                else begin
                    bufm[{fsl, fc_f[4:0]}] <= mcb_rd_data;
                    if (fsl) begin fc1 <= fc1 + 6'd1; if (fc1 == 6'd31) pd1 <= 1'b0; end
                    else     begin fc0 <= fc0 + 6'd1; if (fc0 == 6'd31) pd0 <= 1'b0; end
                end
            end

            if (iss) begin
                mcb_cmd_en        <= 1'b1;
                mcb_cmd_byte_addr <= BYTE_BASE + {6'd0, iss_blk, 7'd0};
                cmd_count         <= cmd_count + 32'd1;
                old               <= (iss_slot ? pd0 : pd1) ? ~iss_slot : iss_slot;
                if (iss_slot) begin tag1 <= iss_blk; tv1 <= 1'b1; fc1 <= 6'd0; pd1 <= 1'b1; end
                else          begin tag0 <= iss_blk; tv0 <= 1'b1; fc0 <= 6'd0; pd0 <= 1'b1; end
            end

            if ((mcb_cmd_en && mcb_cmd_full) || mcb_rd_overflow || mcb_rd_error) err <= 1'b1;
        end
    end
endmodule
`default_nettype wire
