/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2ser_fifo.v -- the G2 serial FIFO board: a Zorro II I/O board through which a driver moves serial data in words
// instead of Paula's one byte an interrupt.  In FIFO mode rtl/g2/amiga_g2.v connects rtl/g2/amiga_serbridge.v's
// byte ports (and so the firmware and the USB serial adapter) to this board instead of to Paula's uart_port.v.
//
// Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.
//
// REGISTERS (16-bit, at the board's base; even addresses; the 68020's longword accesses arrive as two word cycles):
//   0x00 R   RXCOUNT [11:0] bytes waiting for the Amiga
//   0x02 R   TXSPACE [11:0] free places towards the firmware
//   0x04 RW  CTRL [0] FIFO mode (the serial bridge on this board instead of Paula), [1] RX interrupt enable,
//            [2] TX interrupt enable; R [8] RX interrupt pending, [9] TX interrupt pending
//   0x06 RW  RXTHRESH [11:0]: the RX interrupt is pending while RXCOUNT >= RXTHRESH, or RXCOUNT != 0 and no byte has
//            arrived for RXIDLE (512 clk7 cycles, 73 us)
//   0x08 RW  TXTHRESH [11:0]: the TX interrupt is pending while TXSPACE >= TXTHRESH
//   0x0C RW  BAUD [31:16]   0x0E RW BAUD [15:0]: the rate the firmware sets on the USB serial adapter in FIFO mode
//   0x10, 0x12 R  RXDATA2: two bytes, the first in bits 15:8; pops both (read only when RXCOUNT >= 2); a longword read
//                 at 0x10 is two word cycles and pops four
//   0x14, 0x16 W  TXDATA: a word write pushes bits 15:8 then 7:0, a byte write that byte; a longword write at 0x14
//                 pushes four
//   0x18 R   RXDATA1: one byte in bits 7:0; pops it
// A read pops at the start of the cycle (rd rising) and the data stays until the next pop.  The interrupt is the
// board's line on INT2 (shared; a driver reads CTRL to see whether it is its own).
//
// STORAGE.  Each FIFO is a g2ser_bq: a 2^AW x 8 block RAM (one write port, one registered read port) behind a two-byte
// head that holds the oldest bytes (first-word fall-through).  The head refills at one byte a clock with one clock of
// read latency, so two bytes are back in it three clocks after a pop of two; bus cycles on this board are at least
// four clocks apart (rd, hwr and lwr are taken on their rising edges).  A word write to TXDATA writes its first byte
// at once and its second byte in the next clock.  RXCOUNT and the bridge's tx_available count the RAM, the byte in
// flight and the head (tx_available is 0 while the head is empty); TXSPACE counts free RAM places less a staged byte.
//
// BYTE PORTS (towards amiga_serbridge.v, the form of uart_port.v's): tx_* carry the Amiga's bytes out, rx_* bring
// bytes in; every signal in this module's clock domain (the core's clk).
`timescale 1ns / 1ps

module g2ser_fifo #(
    parameter AW = 9                               // 2^AW bytes of block RAM a FIFO
) (
    input  wire        clk,
    input  wire        clk7_en,
    input  wire        rst,
    // the CPU bus (minimig.v's cpu_* signals)
    input  wire        sel,
    input  wire [7:1]  addr,
    input  wire [15:0] data_in,
    output reg  [15:0] data_out = 16'd0,
    input  wire        rd,
    input  wire        hwr,
    input  wire        lwr,
    output wire        irq,
    // configuration for amiga_g2.v and the firmware
    output reg         fifo_mode = 1'b0,
    output reg  [31:0] baud = 32'd0,
    // bytes from the Amiga (as uart_port.v's serial_tx_*)
    output wire [7:0]  tx_available,
    input  wire        tx_strobe,
    output wire [7:0]  tx_data,
    // bytes to the Amiga (as uart_port.v's serial_rx_*)
    output wire [7:0]  rx_available,
    input  wire        rx_strobe,
    input  wire [7:0]  rx_data
);
    localparam N = 1 << AW;

    // ---- the CPU side ---------------------------------------------------------------------------------------------
    reg rd_q = 1'b0, hwr_q = 1'b0, lwr_q = 1'b0;
    wire rd_go = sel && rd && !rd_q;
    wire hw_go = sel && hwr && !hwr_q;
    wire lw_go = sel && lwr && !lwr_q;

    // ---- the two FIFOs --------------------------------------------------------------------------------------------
    // the serial bridge's side: it offers a byte on rx_strobe (rx_data) and takes the head of TX on tx_strobe; both
    // strobes are one clock long (amiga_serbridge.v), and a strobe rising twice in a row never happens (it waits)
    reg rxs_q = 1'b0, txs_q = 1'b0;
    wire [AW:0] rx_free, tx_free;                  // free block-RAM places
    wire [11:0] rx_n, tx_n;                        // bytes held (RAM, in flight, head)
    wire [1:0]  rx_hn, tx_hn;                      // bytes in the head
    wire [7:0]  rx_h0, rx_h1, tx_h0, tx_h1;

    // TX: a CPU word write pushes data_in[15:8] now and data_in[7:0] in the next clock (tx_st)
    reg        tx_st = 1'b0;                       // a staged byte
    reg  [7:0] tx_sb = 8'd0;
    wire       tx_word = (addr == 7'h0A || addr == 7'h0B) && hw_go && lw_go;
    wire       tx_byte = (addr == 7'h0A || addr == 7'h0B) && (hw_go ^ lw_go);
    wire [AW:0] tx_space = tx_free - tx_st;
    wire       tx_wr  = tx_st ? 1'b1 : (tx_word ? tx_space >= 2 : tx_byte && tx_space >= 1);
    wire [7:0] tx_wd  = tx_st ? tx_sb : (tx_word || hw_go) ? data_in[15:8] : data_in[7:0];
    wire       tx_pop = tx_strobe && !txs_q && tx_hn != 2'd0;
    g2ser_bq #(.AW(AW)) u_tx (.clk(clk), .rst(rst), .wr(tx_wr), .wd(tx_wd), .pop({1'b0, tx_pop}),
        .free(tx_free), .n(tx_n), .hn(tx_hn), .h0(tx_h0), .h1(tx_h1));
    // the bridge takes a byte whenever tx_available is not 0 (and then waits four edges, amiga_serbridge.v): no byte
    // is offered until one is in the head
    assign tx_available = tx_hn == 2'd0 ? 8'd0 : tx_n > 12'd255 ? 8'd255 : tx_n[7:0];
    assign tx_data      = tx_h0;

    // RX: the bridge pushes; RXDATA2 pops two, RXDATA1 one
    wire       rx_wr = rx_strobe && !rxs_q && rx_free != 0;
    wire [1:0] rx_pop = !rd_go ? 2'd0 :
                        (addr == 7'h08 || addr == 7'h09) && rx_hn == 2'd2 ? 2'd2 :
                        addr == 7'h0C && rx_hn != 2'd0 ? 2'd1 : 2'd0;
    g2ser_bq #(.AW(AW)) u_rx (.clk(clk), .rst(rst), .wr(rx_wr), .wd(rx_data), .pop(rx_pop),
        .free(rx_free), .n(rx_n), .hn(rx_hn), .h0(rx_h0), .h1(rx_h1));
    assign rx_available = rx_free > 255 ? 8'd255 : rx_free[7:0];

    reg  [1:0] ien = 2'b00;
    reg  [AW:0] rx_th = 1, tx_th = N / 2;
    reg  [8:0] idle = 9'd0;                        // clk7 cycles since the last byte arrived
    wire rx_irq = ien[0] && rx_n != 0 && (rx_n >= rx_th || idle == 9'd511);
    wire tx_irq = ien[1] && tx_space >= tx_th;
    assign irq = rx_irq || tx_irq;

    always @(posedge clk) begin
        rd_q <= sel && rd; hwr_q <= sel && hwr; lwr_q <= sel && lwr;
        rxs_q <= rx_strobe; txs_q <= tx_strobe;
        if (rst) begin
            tx_st <= 1'b0;
            fifo_mode <= 1'b0; ien <= 2'b00; rx_th <= 1; tx_th <= N / 2; baud <= 32'd0; idle <= 9'd0;
        end else begin
            // reads: the register, or the popped bytes (data_out holds them until the next read)
            if (rd_go) case (addr)
                7'h00: data_out <= {4'd0, rx_n};
                7'h01: data_out <= {{(15 - AW){1'b0}}, tx_space};
                7'h02: data_out <= {6'd0, tx_irq, rx_irq, 5'd0, ien, fifo_mode};
                7'h03: data_out <= {{(15 - AW){1'b0}}, rx_th};
                7'h04: data_out <= {{(15 - AW){1'b0}}, tx_th};
                7'h06: data_out <= baud[31:16];
                7'h07: data_out <= baud[15:0];
                7'h08, 7'h09: data_out <= {rx_h0, rx_h1};
                7'h0C: data_out <= {8'd0, rx_h0};
                default: data_out <= 16'd0;
            endcase
            // register writes (word or either byte)
            if (hw_go || lw_go) case (addr)
                7'h02: begin if (lw_go) {ien, fifo_mode} <= data_in[2:0]; end
                7'h03: rx_th <= data_in[AW:0];
                7'h04: tx_th <= data_in[AW:0];
                7'h06: baud[31:16] <= data_in;
                7'h07: baud[15:0] <= data_in;
                default: ;
            endcase
            // the second byte of a word write
            if (tx_st) tx_st <= 1'b0;
            else if (tx_word && tx_space >= 2) begin tx_st <= 1'b1; tx_sb <= data_in[7:0]; end
            if (rx_wr) idle <= 9'd0; else if (clk7_en && idle != 9'd511) idle <= idle + 1'b1;
        end
    end
endmodule

// g2ser_bq -- one FIFO of the board: 2^AW bytes of block RAM (writes at wa, registered reads at ra) and a two-byte head
// (h0 the oldest).  A read is issued while the RAM holds a byte and the head, after this clock's pop, has room for it
// and for a byte already in flight; the byte joins the head in the next clock.  pop must not exceed hn.
module g2ser_bq #(
    parameter AW = 9
) (
    input  wire          clk,
    input  wire          rst,
    input  wire          wr,
    input  wire [7:0]    wd,
    input  wire [1:0]    pop,
    output wire [AW:0]   free,
    output wire [11:0]   n,
    output reg  [1:0]    hn = 2'd0,
    output reg  [7:0]    h0 = 8'd0,
    output reg  [7:0]    h1 = 8'd0
);
    localparam N = 1 << AW;
    (* ram_style = "block" *) reg [7:0] mem [0:N-1];
    reg  [AW-1:0] wa = 0, ra = 0;
    reg  [AW:0]   mn = 0;                          // bytes in the RAM
    reg           fl = 1'b0;                       // a read in flight: q joins the head this clock
    reg  [7:0]    q = 8'd0;
    wire [1:0]    hp = hn - pop;                   // head bytes left after the pop
    wire          iss = mn != 0 && ({1'b0, hp} + fl) < 3'd2;
    assign free = N - mn;
    assign n    = mn + fl + hn;

    always @(posedge clk) begin
        if (wr && mn != N) mem[wa] <= wd;
        q <= mem[ra];
    end

    always @(posedge clk) begin
        if (rst) begin
            wa <= 0; ra <= 0; mn <= 0; fl <= 1'b0; hn <= 2'd0;
        end else begin
            if (wr && mn != N) wa <= wa + 1'b1;
            if (iss) ra <= ra + 1'b1;
            mn <= mn + (wr && mn != N) - iss;
            fl <= iss;
            // the head after the pop, then the byte in flight behind it
            if (hp == 2'd1 && pop == 2'd1) h0 <= h1;
            if (fl) begin if (hp == 2'd0) h0 <= q; else h1 <= q; end
            hn <= hp + fl;
        end
    end
endmodule
