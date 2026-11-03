/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_gmac_rx.v -- GMII / MII receive for the Pano Logic G2's 88E1119R: the PHY's receive stream -> whole frames, CRC
// checked, as a byte stream.  Mike Wolak, EPROM Foundry Inc.
//
// CLOCK.  Entirely in the PHY's RX_CLK domain: 125 MHz at 1000 Mb/s (GMII, one byte per clock on RXD[7:0]), 25 MHz at
// 100 Mb/s and 2.5 MHz at 10 Mb/s (MII, one nibble per clock on RXD[3:0], low nibble first).  `gmii' selects the width;
// it is sampled only while RX_DV is low, so a frame is received in one mode from its first nibble to its last.
//
// THE FRAME.  Preamble, the SFD 0xD5, destination/source/type/payload, a 4-octet FCS.  The SFD is hunted, not counted:
// GMII takes the first 0xD5 byte after 0x55 bytes; MII takes a 5 nibble followed by a D nibble (0xD5 low nibble first).
//
// CRC32.  Reflected, initial all ones, polynomial 0x04C11DB7, one BYTE per step in both modes (MII assembles the byte
// first).  Running it over the data and the FCS leaves the residue 0xDEBB20E3 when the FCS is right.
//
// CLOCK ENABLE.  ce gates every register: 1 for RX_CLK itself, or one clock in each RX_CLK period when the core runs
// on a faster clock that samples RX_CLK (g2_gmac.v RX_OS); the outputs then hold from one enabled clock to the next.
//
// OUTPUT.  One byte per o_valid, the FCS withheld by a four-byte delay line: o_sof with the first data byte; o_eof alone,
// the clock after RX_DV falls, with o_crc_ok and o_rxer.  A frame of four bytes or fewer gives o_eof and no data.  A
// trailing odd nibble in MII is dropped (the CRC covers whole bytes only).
`timescale 1ns / 1ps
`default_nettype none

module g2_gmac_rx (
    input  wire        rxc,
    input  wire        ce,             // clock enable
    input  wire        rst,            // synchronous to rxc
    input  wire        gmii,           // 1: GMII bytes, 0: MII nibbles; taken while RX_DV is low
    input  wire        rxdv,
    input  wire        rxer,
    input  wire [7:0]  rxd,

    output reg  [7:0]  o_data = 8'd0,
    output reg         o_valid = 1'b0,
    output reg         o_sof = 1'b0,
    output reg         o_eof = 1'b0,
    output reg         o_crc_ok = 1'b0,
    output reg         o_rxer = 1'b0
);
    localparam [1:0] ST_IDLE = 2'd0, ST_SFD = 2'd1, ST_DATA = 2'd2;

    reg [1:0]  st = ST_IDLE;
    reg        mode = 1'b0;             // the width of the frame in progress
    reg        phase = 1'b0;            // MII: 1 = the next nibble is the high half of a byte
    reg [3:0]  lo = 4'd0;
    reg [31:0] crc = 32'hFFFFFFFF;
    reg        er_seen = 1'b0, first = 1'b0;
    reg [7:0]  d0 = 8'd0, d1 = 8'd0, d2 = 8'd0, d3 = 8'd0;
    reg [2:0]  fill = 3'd0;             // data bytes held in d0..d3 (saturates at 4)

    // one byte through the reflected CRC; unrolled (XST rejects the loop form in a function, 1b9c113)
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

    // the byte completed this clock, if any
    wire       b_ok  = (st == ST_DATA) && (mode ? 1'b1 : phase);
    wire [7:0] b_now = mode ? rxd : {rxd[3:0], lo};

    always @(posedge rxc) if (ce) begin
        o_valid <= 1'b0; o_sof <= 1'b0; o_eof <= 1'b0;
        if (rst) begin
            st <= ST_IDLE; phase <= 1'b0; crc <= 32'hFFFFFFFF; fill <= 3'd0; er_seen <= 1'b0; mode <= gmii;
        end else if (!rxdv) begin
            // end of frame: d3 is the last data byte once four bytes followed it (the FCS)
            if (st == ST_DATA) begin
                o_eof    <= 1'b1;                                  // no data with it: d0..d3 are the FCS
                o_rxer   <= er_seen;
                o_crc_ok <= (crc == 32'hDEBB20E3);
            end
            st <= ST_IDLE; phase <= 1'b0; crc <= 32'hFFFFFFFF; fill <= 3'd0; er_seen <= 1'b0;
            mode <= gmii;
        end else begin
            if (rxer) er_seen <= 1'b1;
            case (st)
            ST_IDLE: if (mode) begin
                         if (rxd == 8'hD5) begin st <= ST_DATA; first <= 1'b1; end
                     end else if (rxd[3:0] == 4'h5) st <= ST_SFD;
            ST_SFD:  if (rxd[3:0] == 4'hD) begin st <= ST_DATA; phase <= 1'b0; first <= 1'b1; end
                     else if (rxd[3:0] != 4'h5) st <= ST_IDLE;
            ST_DATA: begin
                         if (!mode) begin phase <= ~phase; if (!phase) lo <= rxd[3:0]; end
                         if (b_ok) begin
                             crc <= crc8(crc, b_now);
                             d0 <= b_now; d1 <= d0; d2 <= d1; d3 <= d2;
                             if (fill != 3'd4) fill <= fill + 3'd1;
                             else begin                                      // d3 is followed by four bytes: not FCS
                                 o_data <= d3; o_valid <= 1'b1; o_sof <= first; first <= 1'b0;
                             end
                         end
                     end
            default: st <= ST_IDLE;
            endcase
        end
    end
endmodule
`default_nettype wire
