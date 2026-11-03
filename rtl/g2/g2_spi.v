/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_spi.v -- the SPI flash master of the G2 SoC: a byte engine (page 0x8000_C000), the board-page bit-bang path
// kept from the G1 (op 5, read back at 0x8000_501C bit 0), and the write guard (g2_spi_guard.v) that both pass
// through on their way to the pins.
//
// REGISTERS (word offsets from 0x8000_C000)
//   +0x00 CTRL    R/W [0] CS (1 = chip select asserted)   [15:8] DIV: SCK half period in clocks, 0 and 1 read as 2
//   +0x04 DATA    W   [7:0] a byte to shift out; 8 SCK periods, MSB first, SPI mode 0
//                 R   [7:0] the byte shifted in by the last transfer, [8] BUSY
//   +0x08 STATUS  R   [0] BUSY  [1] BLOCKED (the current transaction was refused)  [2] BITBANG (board page owns the pins)
//                     [31:16] refused-command count
//   +0x0C GUARD   R   the last refusal: [7:0] opcode, [15:8] address [23:16], [16] refused at the address
//   +0x10 PASSED  R   [15:0] write/erase commands whose address passed the guard
// A DATA write while BUSY is ignored.  Writes to CTRL while BUSY are ignored.
//
// OWNERSHIP.  bb_claim (board page op 5 bit 8) gives the pins to the bit-bang levels; otherwise the engine owns them.
// Both reach the pins only through the guard.
//
// TIMING.  At DIV = 2 and a 50 MHz clock SCK is 12.5 MHz.  MISO is sampled at the end of each SCK high phase from
// an input register; the M25P64 changes MISO at most 8 ns after the falling edge that precedes that high phase.
`timescale 1ns / 1ps
`default_nettype none

module g2_spi (
    input  wire        clk,
    input  wire        rst,
    // register port
    input  wire        reg_en,
    input  wire        reg_we,
    input  wire [2:0]  reg_adr,
    input  wire [31:0] reg_dat_w,
    output reg  [31:0] reg_dat_r,
    // board-page bit-bang levels
    input  wire        bb_claim,
    input  wire        bb_cs_n,
    input  wire        bb_sck,
    input  wire        bb_mosi,
    output wire        miso_level,         // the registered MISO pin level, for 0x8000_501C bit 0
    output wire        cs_active,          // the flash is selected (for the activity LED)
    // pins
    output wire        spi_cs_n,
    output wire        spi_sck,
    output wire        spi_mosi,
    input  wire        spi_miso
);
    reg        cs = 1'b0;
    reg  [7:0] div = 8'd2;
    reg        busy = 1'b0;
    reg  [7:0] txs = 8'd0, rxs = 8'd0, rxb = 8'd0;
    reg  [3:0] bitn = 4'd0;
    reg        ph = 1'b0;                  // 0 = SCK low phase, 1 = high phase
    reg  [7:0] cnt = 8'd0;
    reg        e_sck = 1'b0, e_mosi = 1'b0;
    (* IOB = "TRUE" *) reg miso_q = 1'b0;
    wire [7:0] div_e = (div < 8'd2) ? 8'd2 : div;

    wire        blocked;
    wire [15:0] refused_cnt, passed_cnt;
    wire [16:0] last_refused;

    // the requested levels, registered: the guard sees one source at a time
    reg r_cs_n = 1'b1, r_sck = 1'b0, r_mosi = 1'b0;
    always @(posedge clk) begin
        miso_q <= spi_miso;
        r_cs_n <= bb_claim ? bb_cs_n : !cs;
        r_sck  <= bb_claim ? bb_sck  : e_sck;
        r_mosi <= bb_claim ? bb_mosi : e_mosi;
    end
    assign miso_level = miso_q;

    always @(posedge clk) begin
        if (rst) begin
            cs <= 1'b0; div <= 8'd2; busy <= 1'b0; e_sck <= 1'b0; e_mosi <= 1'b0; bitn <= 4'd0; ph <= 1'b0; cnt <= 8'd0;
        end else begin
            if (busy) begin
                if (cnt != 8'd1) cnt <= cnt - 8'd1;
                else begin
                    cnt <= div_e;
                    if (!ph) begin
                        ph <= 1'b1; e_sck <= 1'b1;                 // rising edge requested
                    end else begin
                        ph <= 1'b0; e_sck <= 1'b0;                 // falling edge: sample first, then next bit
                        rxs <= {rxs[6:0], miso_q};
                        if (bitn == 4'd7) begin
                            busy <= 1'b0; rxb <= {rxs[6:0], miso_q};
                        end else begin
                            bitn <= bitn + 4'd1;
                            e_mosi <= txs[6];
                            txs <= {txs[6:0], 1'b0};
                        end
                    end
                end
            end
            if (reg_en && reg_we && !busy) begin
                case (reg_adr)
                3'd0: begin cs <= reg_dat_w[0]; div <= reg_dat_w[15:8]; end
                3'd1: begin
                    busy <= 1'b1; bitn <= 4'd0; ph <= 1'b0; cnt <= div_e;
                    txs <= reg_dat_w[7:0]; e_mosi <= reg_dat_w[7]; e_sck <= 1'b0;
                end
                default: ;
                endcase
            end
        end
    end

    always @(*) begin
        case (reg_adr)
        3'd0:    reg_dat_r = {16'd0, div, 7'd0, cs};
        3'd1:    reg_dat_r = {23'd0, busy, rxb};
        3'd2:    reg_dat_r = {refused_cnt, 13'd0, bb_claim, blocked, busy};
        3'd3:    reg_dat_r = {15'd0, last_refused};
        3'd4:    reg_dat_r = {16'd0, passed_cnt};
        default: reg_dat_r = 32'd0;
        endcase
    end

    g2_spi_guard u_guard (
        .clk(clk), .rst(rst),
        .req_cs_n(r_cs_n), .req_sck(r_sck), .req_mosi(r_mosi),
        .pin_cs_n(spi_cs_n), .pin_sck(spi_sck), .pin_mosi(spi_mosi),
        .blocked(blocked), .refused_cnt(refused_cnt), .passed_cnt(passed_cnt), .last_refused(last_refused));

    assign cs_active = !spi_cs_n;
endmodule

`default_nettype wire
