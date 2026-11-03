/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_uart.v -- the G2 bring-up console: a UART, 8N1, at the register address and with the register semantics of
// the G1 console (0x8000_1000), so fw/console.h and every program built on it run unchanged.
//
//   +0x00 RX  R  {[8] valid, [7:0] data}; reading pops one byte when valid
//   +0x04 TX  R  {[8] ready}                  W  [7:0] pushes one byte; a byte written while full is dropped and
//                                                  counted (OVF)
//   +0x08 ST  R  [7:0] TX FIFO fill, [8] TX overflow seen, [9] RX overflow seen, [10] framing error seen
//                (the G1 console has no register here; firmware written for the G1 never reads it)
//
// FIFOs: TX 256 bytes (the G1 console's depth), RX 2^RXW bytes, of which 2^RXW - 1 are used (RXW 4: 16 bytes).
// Bit time DIV clocks: 434 at 50 MHz = 115,207 baud,
// +0.007 % from 115,200.  The receiver samples each bit at its middle from a two-register synchroniser; a start
// bit shorter than half a bit time is rejected as noise, a stop bit read as 0 is a framing error and the byte is
// discarded.
`timescale 1ns / 1ps
`default_nettype none

module g2_uart #(
    parameter DIV = 434,
    parameter RXW = 4                      // RX FIFO address width
) (
    input  wire        clk,
    input  wire        rst,                // clears the CPU-side FIFO state, like the G1 console's run/halt
    input  wire        reg_en,
    input  wire        reg_we,
    input  wire [1:0]  reg_adr,
    input  wire [7:0]  reg_dat_w,
    output reg  [31:0] reg_dat_r,
    output wire        txd,
    input  wire        rxd
);
    localparam integer HALF = DIV / 2;

    // ---- TX FIFO and shifter ------------------------------------------------
    (* ram_style = "distributed" *) reg [7:0] txf [0:255];   // distributed: no 9K block RAM (initial contents unused)
    reg  [7:0] tx_wp = 8'd0, tx_rp = 8'd0;
    wire [7:0] tx_count = tx_wp - tx_rp;
    wire       tx_full  = (tx_count == 8'd255);
    reg        tx_ovf = 1'b0, rx_ovf = 1'b0, fr_err = 1'b0;
    reg        tx_busy = 1'b0;
    reg  [9:0] tx_sh = 10'h3FF;
    reg  [3:0] tx_n = 4'd0;
    reg [15:0] tx_cnt = 16'd0;
    reg  [7:0] tx_byte = 8'd0;
    reg        tx_load = 1'b0;
    (* IOB = "TRUE" *) reg txd_q = 1'b1;
    assign txd = txd_q;

    // ---- RX -----------------------------------------------------------------
    (* ram_style = "distributed" *) reg [7:0] rxf [0:(1 << RXW) - 1];
    reg  [RXW-1:0] rx_wp = {RXW{1'b0}}, rx_rp = {RXW{1'b0}};
    wire       rx_valid = (rx_wp != rx_rp);
    wire       rx_full  = ((rx_wp - rx_rp) == {RXW{1'b1}});
    (* IOB = "TRUE" *) reg rxd_q = 1'b1;
    reg        rxd_s = 1'b1;
    reg        rx_busy = 1'b0;
    reg [15:0] rx_cnt = 16'd0;
    reg  [3:0] rx_n = 4'd0;
    reg  [7:0] rx_sh = 8'd0;

    always @(posedge clk) begin
        rxd_q <= rxd; rxd_s <= rxd_q;
        tx_byte <= txf[tx_rp];                          // registered read of the FIFO head
        // transmitter
        if (!tx_busy) begin
            txd_q <= 1'b1;
            if (tx_load) begin
                tx_busy <= 1'b1; tx_sh <= {1'b1, tx_byte, 1'b0}; tx_n <= 4'd0; tx_cnt <= DIV[15:0] - 16'd1;
                tx_rp <= tx_rp + 8'd1; tx_load <= 1'b0;
                txd_q <= 1'b0;
            end else if (tx_count != 8'd0) tx_load <= 1'b1;   // tx_byte is valid from the next clock
        end else begin
            if (tx_cnt != 16'd0) tx_cnt <= tx_cnt - 16'd1;
            else begin
                tx_cnt <= DIV[15:0] - 16'd1;
                if (tx_n == 4'd9) begin tx_busy <= 1'b0; txd_q <= 1'b1; end
                else begin tx_n <= tx_n + 4'd1; tx_sh <= {1'b1, tx_sh[9:1]}; txd_q <= tx_sh[1]; end
            end
        end
        // receiver
        if (!rx_busy) begin
            if (!rxd_s) begin rx_busy <= 1'b1; rx_cnt <= HALF[15:0]; rx_n <= 4'd0; end
        end else begin
            if (rx_cnt != 16'd0) rx_cnt <= rx_cnt - 16'd1;
            else begin
                rx_cnt <= DIV[15:0] - 16'd1;
                if (rx_n == 4'd0) begin
                    if (rxd_s) rx_busy <= 1'b0;                 // start bit gone at mid-bit: noise
                    else rx_n <= 4'd1;
                end else if (rx_n != 4'd9) begin
                    rx_sh <= {rxd_s, rx_sh[7:1]}; rx_n <= rx_n + 4'd1;
                end else begin
                    rx_busy <= 1'b0;
                    if (!rxd_s) fr_err <= 1'b1;
                    else if (rx_full) rx_ovf <= 1'b1;
                    else begin rxf[rx_wp] <= rx_sh; rx_wp <= rx_wp + 1'b1; end
                end
            end
        end
        // register port
        if (reg_en) begin
            if (reg_adr == 2'd0 && !reg_we && rx_valid) rx_rp <= rx_rp + 1'b1;
            if (reg_adr == 2'd1 && reg_we) begin
                if (tx_full) tx_ovf <= 1'b1;
                else begin txf[tx_wp] <= reg_dat_w; tx_wp <= tx_wp + 8'd1; end
            end
        end
        if (rst) begin
            tx_wp <= 8'd0; tx_rp <= 8'd0; tx_busy <= 1'b0; tx_load <= 1'b0; txd_q <= 1'b1;
            rx_rp <= rx_wp; tx_ovf <= 1'b0; rx_ovf <= 1'b0; fr_err <= 1'b0;
        end
    end

    always @(*) begin
        case (reg_adr)
        2'd0:    reg_dat_r = {23'd0, rx_valid, rxf[rx_rp]};
        2'd1:    reg_dat_r = {23'd0, !tx_full, 8'd0};
        2'd2:    reg_dat_r = {21'd0, fr_err, rx_ovf, tx_ovf, tx_count};
        default: reg_dat_r = 32'd0;
        endcase
    end
endmodule

`default_nettype wire
