/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_gmac_tx.v -- GMII / MII transmit for the Pano Logic G2's 88E1119R: one frame from the transmit buffer -> preamble,
// SFD, data, pad, FCS, interframe gap.  Mike Wolak, EPROM Foundry Inc.
//
// CLOCK.  Entirely in the 125 MHz transmit clock (the PHY's CLK125 on Y13, the clock GTX_CLK is made from).  `step'
// advances the wire by one symbol: every clock in GMII (one byte on TXD[7:0]); once per MII TX_CLK period in MII (one
// nibble on TXD[3:0], low nibble first), the strobe coming from g2_gmac.v's TX_CLK sampler.  `gmii' is taken when a
// frame starts, so a frame goes out in one mode.
//
// THE BYTE SOURCE.  A 32-bit block RAM read port with one clock of latency (rd_adr -> rd_dat).  The next word is always
// read ahead: a word lasts at least four clocks, the read two.  Past `len' the bytes are zero: the pad to the 60-byte
// minimum.  The buffer holds the whole frame before `start' (g2_gmac.v copies it first), so the wire cannot run dry and
// TX_ER is never needed.
//
//   7 x 0x55, 0xD5    preamble and SFD
//   N bytes            the frame, unmodified, padded with zeros to 60
//   4 bytes            FCS: the complement of the CRC32 of the data and pad, low byte first
//   12 bytes           interframe gap with TX_EN low
`timescale 1ns / 1ps
`default_nettype none

module g2_gmac_tx (
    input  wire        clk,            // 125 MHz
    input  wire        rst,            // synchronous
    input  wire        gmii,
    input  wire        step,
    input  wire        start,          // one-clock pulse: send the frame in the buffer
    input  wire [10:0] len,            // data octets, static from start to !busy
    output reg         busy = 1'b0,
    output wire [8:0]  rd_adr,
    input  wire [31:0] rd_dat,
    output reg         txen = 1'b0,
    output reg  [7:0]  txd = 8'd0
);
    localparam [2:0] ST_IDLE = 3'd0, ST_PRE = 3'd1, ST_DATA = 3'd2, ST_FCS = 3'd3, ST_IFG = 3'd4;
    localparam [10:0] MIN_DATA = 11'd60;

    reg [2:0]  st = ST_IDLE;
    reg        mode = 1'b1, ph = 1'b0, pend = 1'b0;
    reg [3:0]  k = 4'd0;               // preamble byte, FCS byte, IFG byte
    reg [10:0] i = 11'd0, n = 11'd0;   // data byte index, padded length
    reg [31:0] crc = 32'hFFFFFFFF;
    reg [31:0] wcur = 32'd0, wnext = 32'd0;
    reg [7:0]  cb = 8'd0;              // MII: the byte whose high nibble goes next
    reg [8:0]  wa = 9'd0;              // the word being read ahead
    reg        pf_valid = 1'b0, pf_wait = 1'b0;
    // the output stage, one clock behind the slot logic: the slot's byte, its kind and the step that produced it
    reg        o_slot = 1'b0, o_nib = 1'b0, o_idle = 1'b0, o_data = 1'b0, o_fcs = 1'b0, o_en = 1'b0;
    reg  [1:0] o_k = 2'd0;
    reg  [7:0] o_byte = 8'd0;

    function [31:0] crc8;
        input [31:0] c;
        input [7:0]  d;
        reg   [31:0] t;
        begin
            t = c;
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[0])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[1])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[2])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[3])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[4])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[5])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[6])}});
            t = (t >> 1) ^ (32'hEDB88320 & {32{(t[0] ^ d[7])}});
            crc8 = t;
        end
    endfunction

    assign rd_adr = wa;

    // the slot boundaries: a slot is one byte; GMII sends it in one step, MII in two (ph 0 low nibble, ph 1 high)
    wire slot_start = step && (mode || !ph);
    wire slot_end   = step && (mode || ph);

    // the data byte of slot i: byte 0 of a word comes from the word read ahead (taken at that slot's start)
    reg [7:0] dbyte;
    always @(*) begin
        case (i[1:0])
        2'd0: dbyte = wnext[7:0];
        2'd1: dbyte = wcur[15:8];
        2'd2: dbyte = wcur[23:16];
        default: dbyte = wcur[31:24];
        endcase
        if (i >= len) dbyte = 8'd0;                              // the pad
    end
    wire [31:0] fcs = ~crc;
    reg  [7:0]  fbyte;
    always @(*) case (o_k)
        2'd0: fbyte = fcs[7:0]; 2'd1: fbyte = fcs[15:8]; 2'd2: fbyte = fcs[23:16]; default: fbyte = fcs[31:24];
    endcase
    wire [7:0]  obyte = o_fcs ? fbyte : o_byte;    // the byte the output stage sends

    // the byte of the slot that starts now
    reg [7:0] sbyte;
    always @(*) case (st)
        ST_PRE:  sbyte = (k == 4'd7) ? 8'hD5 : 8'h55;
        ST_DATA: sbyte = dbyte;
        ST_FCS:  sbyte = 8'd0;                                // the FCS byte is chosen in the output stage
        default: sbyte = 8'd0;
    endcase

    always @(posedge clk) begin
        // read-ahead: one word in flight at a time; rd_dat is valid the clock after wa was presented
        if (pf_wait) begin wnext <= rd_dat; pf_valid <= 1'b1; pf_wait <= 1'b0; wa <= wa + 9'd1; end
        else if (!pf_valid && st != ST_IDLE) pf_wait <= 1'b1;

        // the output stage: the slot logic below registers each slot's byte and kind (o_*); one clock later the byte goes
        // to TXD (an FCS byte from the CRC, which by then includes the last data byte) and a data byte enters the CRC.
        // The CRC therefore starts from registers, and the frame on the wire is the same, one clock later.
        o_slot <= 1'b0; o_nib <= 1'b0; o_idle <= 1'b0;
        if (o_idle) begin txen <= 1'b0; txd <= 8'd0; end
        if (o_slot) begin
            txen <= o_en;
            txd  <= mode ? obyte : {4'd0, obyte[3:0]};
            cb   <= obyte;
            if (o_data) crc <= crc8(crc, o_byte);
        end else if (o_nib) begin
            txd <= {4'd0, cb[7:4]};                              // MII, second nibble of the slot
        end
        if (rst) begin
            st <= ST_IDLE; busy <= 1'b0; pend <= 1'b0; txen <= 1'b0; txd <= 8'd0; ph <= 1'b0;
            pf_valid <= 1'b0; pf_wait <= 1'b0; wa <= 9'd0; o_slot <= 1'b0; o_nib <= 1'b0; o_idle <= 1'b0;
        end else begin
            if (start) begin pend <= 1'b1; busy <= 1'b1; end
            case (st)
            ST_IDLE: begin
                if (step) o_idle <= 1'b1;
                if (pend && step) begin
                    pend <= 1'b0; mode <= gmii; ph <= 1'b0; k <= 4'd0; i <= 11'd0; crc <= 32'hFFFFFFFF;
                    n <= (len < MIN_DATA) ? MIN_DATA : len;
                    wa <= 9'd0; pf_valid <= 1'b0; pf_wait <= 1'b0;
                    st <= ST_PRE;
                end
            end
            default: begin
                if (slot_start) begin
                    o_slot <= 1'b1; o_en <= (st != ST_IFG); o_byte <= sbyte; o_data <= (st == ST_DATA);
                    o_fcs <= (st == ST_FCS); o_k <= k[1:0];
                    if (st == ST_DATA && i[1:0] == 2'd0) begin wcur <= wnext; pf_valid <= 1'b0; end
                end else if (step) begin
                    o_nib <= 1'b1;
                end
                if (slot_end) begin
                    ph <= 1'b0;
                    case (st)
                    ST_PRE:  if (k == 4'd7) begin st <= ST_DATA; k <= 4'd0; end else k <= k + 4'd1;
                    ST_DATA: if (i + 11'd1 == n) begin st <= ST_FCS; k <= 4'd0; end else i <= i + 11'd1;
                    ST_FCS:  if (k == 4'd3) begin st <= ST_IFG; k <= 4'd0; end else k <= k + 4'd1;
                    ST_IFG:  if (k == 4'd11) begin st <= ST_IDLE; k <= 4'd0; busy <= pend; end else k <= k + 4'd1;
                    default: st <= ST_IDLE;
                    endcase
                end else if (step) ph <= 1'b1;
            end
            endcase
        end
    end
endmodule
`default_nettype wire
