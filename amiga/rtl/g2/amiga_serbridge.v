/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// amiga_serbridge.v -- Paula's serial port as a byte stream on the register page: the FIFOs and register semantics of
// rtl/g2/g2_uart.v without its bit timing, joined to NanoMig's uart_port.v (nanomig/src/misc, built with
// ENABLE_AT_MODEM) in the core domain.
//
// Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
//
// uart_port.v samples Paula's txd and drives Paula's rxd on the core's own clock (clk_sys with clk7_en), so its bit
// timing and Paula's agree whatever the clock gate withholds; it holds Paula's transmitter while its FIFO is full and
// sends a byte to Paula only when Paula's receive buffer is empty (RBF clear).  Nothing on this path is lost or timed
// by the clk domain.  This module moves the bytes between uart_port's byte ports (core domain) and two FIFOs (clk
// domain), one byte at a time each way, as toggles (amiga_g2.v DOMAINS): every core-domain signal changes only at an
// amiga_clk edge, which is a clk edge, and every clk register the core samples is stable for a clk period or more.
//
//   +0 RX     R  {[8] valid, [7:0] byte from Paula}; a read pops it when valid
//   +1 TX     R  {[8] ready}           W  [7:0] a byte for Paula; a byte written while full is dropped (OVF)
//   +2 ST     R  [7:0] TX FIFO fill, [8] TX overflow seen, [13:12] {DTR, RTS} as CIA-B drives them, [19:16] {RI, CD,
//                DSR, CTS} as CIA-B reads them      W  [3:0] {RI, CD, DSR, CTS} for CIA-B port A (reset 4'b1110)
//             The RS-232 control lines at CIA-B's pins: 0 is the line asserted, as on the machine.
//   +3 SERPER R  [14:0] Paula's SERPER (the bit time is (SERPER + 1) colour clocks; 8N1); in FIFO mode (the G2 serial
//                FIFO board carries the bytes instead of Paula) {1, the board's baud rate [30:0]}
//
// FIFOs: TX 256 bytes, RX 2^RXW bytes (2^RXW - 1 used).  A full RX FIFO stops the transfer from uart_port, whose FIFO
// then holds Paula's transmitter: back pressure reaches the Amiga program instead of a byte being dropped.  irq is
// high while the RX FIFO holds a byte.
`timescale 1ns / 1ps
`default_nettype none

module amiga_serbridge #(
    parameter RXW = 10
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        reg_en,
    input  wire        reg_we,
    input  wire [1:0]  reg_adr,
    input  wire [7:0]  reg_dat_w,
    output reg  [31:0] reg_dat_r,
    output wire        irq,

    // uart_port's byte ports and Paula's SERPER, core domain
    input  wire        aclk,
    input  wire [7:0]  tx_available,       // bytes from Paula waiting in uart_port
    output reg         tx_strobe = 1'b0,   // takes one
    input  wire [7:0]  tx_data,            // the byte at the head
    input  wire [7:0]  rx_available,       // free places in uart_port's FIFO to Paula
    output reg         rx_strobe = 1'b0,   // puts rx_data in
    output reg  [7:0]  rx_data = 8'd0,
    input  wire [14:0] serper,
    input  wire        fifo_mode,          // the G2 serial FIFO board in Paula's place (core domain, changes rarely)
    input  wire [31:0] baud,
    // CIA-B port A's RS-232 control lines (0: asserted), core domain
    input  wire        rts_n,
    input  wire        dtr_n,
    output reg  [3:0]  ctl_n = 4'b1110      // {RI, CD, DSR, CTS}: CTS asserted, the others not
);
    reg  [1:0] mo_s1 = 2'b11, mo_s2 = 2'b11;     // {DTR, RTS} into the clk domain
    // ---- clk domain: the FIFOs and the register port -------------------------
    (* ram_style = "distributed" *) reg [7:0] txf [0:255];
    reg  [7:0] tx_wp = 8'd0, tx_rp = 8'd0;
    wire [7:0] tx_count = tx_wp - tx_rp;
    wire       tx_full  = (tx_count == 8'd255);
    reg        tx_ovf = 1'b0;
    (* ram_style = "distributed" *) reg [7:0] rxf [0:(1 << RXW) - 1];
    reg  [RXW-1:0] rx_wp = {RXW{1'b0}}, rx_rp = {RXW{1'b0}};
    wire       rx_valid = (rx_wp != rx_rp);
    wire       rx_full  = ((rx_wp - rx_rp) == {RXW{1'b1}});
    assign irq = rx_valid;

    // Paula to the RX FIFO: the core domain offers a byte (up_byte, up_tog); this side takes it into the FIFO when
    // there is room and answers with up_ack
    reg  [7:0] up_byte = 8'd0;             // core domain
    reg        up_tog = 1'b0;              // core domain
    reg        up_ack = 1'b0;              // clk domain
    // the TX FIFO to Paula: this side offers the head (dn_byte, dn_tog); the core domain puts it into uart_port when
    // there is room and answers with dn_ack
    reg  [7:0] dn_byte = 8'd0;             // clk domain
    reg        dn_tog = 1'b0;              // clk domain
    reg        dn_ack = 1'b0;              // core domain
    reg [14:0] serper_c = 15'd0;           // clk domain copy
    reg        fmode_c = 1'b0;
    reg [31:0] baud_c = 32'd0;

    always @(posedge clk) begin
        serper_c <= serper; fmode_c <= fifo_mode; baud_c <= baud;
        if (up_tog != up_ack && !rx_full) begin
            rxf[rx_wp] <= up_byte; rx_wp <= rx_wp + 1'b1; up_ack <= up_tog;
        end
        if (dn_tog == dn_ack && tx_count != 8'd0) begin
            dn_byte <= txf[tx_rp]; tx_rp <= tx_rp + 8'd1; dn_tog <= ~dn_tog;
        end
        mo_s1 <= {dtr_n, rts_n}; mo_s2 <= mo_s1;
        if (reg_en) begin
            if (reg_adr == 2'd2 && reg_we) ctl_n <= reg_dat_w[3:0];
            if (reg_adr == 2'd0 && !reg_we && rx_valid) rx_rp <= rx_rp + 1'b1;
            if (reg_adr == 2'd1 && reg_we) begin
                if (tx_full) tx_ovf <= 1'b1;
                else begin txf[tx_wp] <= reg_dat_w; tx_wp <= tx_wp + 8'd1; end
            end
        end
        if (rst) begin
            tx_wp <= 8'd0; tx_rp <= 8'd0; rx_rp <= rx_wp; tx_ovf <= 1'b0;
        end
    end

    always @(*) begin
        case (reg_adr)
        2'd0:    reg_dat_r = {23'd0, rx_valid, rxf[rx_rp]};
        2'd1:    reg_dat_r = {23'd0, !tx_full, 8'd0};
        2'd2:    reg_dat_r = {12'd0, ctl_n, 2'd0, mo_s2, 3'd0, tx_ovf, tx_count};
        default: reg_dat_r = fmode_c ? {1'b1, baud_c[30:0]} : {17'd0, serper_c};
        endcase
    end

    // ---- core domain: one byte each way at a time ----------------------------
    // tx_strobe and rx_strobe are high for one core edge.  uart_port's FIFOs (io_fifo.v) take a strobe through two
    // registers and act on its rising edge, so a pointer and its count move two to three edges later; each side
    // waits four edges after a strobe before it looks at the count again, and rx_data stays until then.
    reg [2:0] up_wait = 3'd0, dn_wait = 3'd0;
    always @(posedge aclk) begin
        tx_strobe <= 1'b0; rx_strobe <= 1'b0;
        if (up_wait != 3'd0) up_wait <= up_wait - 3'd1;
        else if (up_tog == up_ack && tx_available != 8'd0) begin
            up_byte <= tx_data; tx_strobe <= 1'b1; up_tog <= ~up_tog; up_wait <= 3'd4;
        end
        if (dn_wait != 3'd0) dn_wait <= dn_wait - 3'd1;
        else if (dn_tog != dn_ack && rx_available != 8'd0) begin
            rx_data <= dn_byte; rx_strobe <= 1'b1; dn_ack <= dn_tog; dn_wait <= 3'd4;
        end
    end
endmodule

`default_nettype wire
