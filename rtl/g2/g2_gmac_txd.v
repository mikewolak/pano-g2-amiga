/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_gmac_txd.v -- the transmit side of g2_gmac.v that lives in the 125 MHz transmit clock: the crossings in, the MII
// TX_CLK sampler, the transmit engine, the output registers in the IOBs and the done toggle back.  One module so that
// ise/pano_g2.ucf can group every register of this clock by instance (TNM "etx_regs") for the DATAPATHONLY crossings.
// Mike Wolak, EPROM Foundry Inc.
//
// MII TIMING WITHOUT THE PHY'S CLOCK.  At 10/100 Mb/s the 88E1119R samples TXD on the rising edge of its own TX_CLK
// (25 or 2.5 MHz, from the same 25 MHz reference as CLK125).  TX_CLK is sampled here as data (s1, an IOB register), and
// every rising edge found produces one `step', so that the output registers change a fixed number of 125 MHz clocks
// after the edge.  The chain, for the edge at t0 first seen by s1 at the clock edge E1 (E1 - t0 in 0..8 ns): s2 at E1 + 8,
// the decoded rise registered into rise_d[0] at E1 + 16, rise_d[MII_STEP_DELAY - 1] is the step during the clock that
// ends at E1 + 8 x (MII_STEP_DELAY + 2), the engine's slot register takes the nibble at that edge, its output stage one
// clock later (rtl/g2/g2_gmac_tx.v) and the IOB register one more.  With MII_STEP_DELAY 1: TXD changes on E1 + 5 x 8 ns
// = t0 + 40..48 ns plus
// the clock-to-pad delay (3.9 to 9.4 ns, ise/pano_g2.ucf TXD OFFSET OUT), so 3.6 to 18.7 ns after the NEXT TX_CLK rising
// edge at 100 Mb/s: inside IEEE 802.3 clause 22.3.1's 0 to 25 ns.  At 10 Mb/s (400 ns period) the change lands 43.6 to
// 58.7 ns after the edge, 341 ns before the next.  The PHY samples the new nibble one TX_CLK later than it would from a
// MAC clocked by TX_CLK; the stream is the same.
`timescale 1ns / 1ps
`default_nettype none

module g2_gmac_txd #(
    parameter integer MII_STEP_DELAY = 1
) (
    input  wire        clk,            // 125 MHz transmit clock
    input  wire        tx_en_a,        // CTRL.TX_EN, clk_cpu domain level
    input  wire        gmii_a,         // GMII selected, clk_cpu domain level
    input  wire        start_tog_a,    // toggles once per frame, after the buffer holds it
    input  wire [10:0] len_a,          // static from the toggle until done
    input  wire        mii_txc,        // the TX_CLK pad (Y11), sampled here
    output wire [8:0]  rd_adr,         // transmit buffer read port (block RAM in g2_gmac.v, this clock)
    input  wire [31:0] rd_dat,
    output reg         done_tog = 1'b0,
    output wire        eth_txen,
    output wire        eth_txer,
    output wire [7:0]  eth_txd,
    output reg         gtx_on = 1'b0   // GTX_CLK runs (the ODDR2's D1) while GMII is selected
);
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [1:0] en_s = 2'b00, gm_s = 2'b00;
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [2:0] st_s = 3'b000;
    always @(posedge clk) begin en_s <= {en_s[0], tx_en_a}; gm_s <= {gm_s[0], gmii_a}; st_s <= {st_s[1:0], start_tog_a}; end
    wire t_on  = en_s[1];
    wire gmii  = gm_s[1];
    wire start = st_s[2] ^ st_s[1];
    always @(posedge clk) gtx_on <= gmii;

    // TX_CLK: an IOB register, two more for the edge, then the fixed delay
    (* IOB = "TRUE" *) reg s1 = 1'b0;
    (* shreg_extract = "no" *) reg s2 = 1'b0, s3 = 1'b0;
    reg [7:0] rise_d = 8'd0;
    always @(posedge clk) begin s1 <= mii_txc; s2 <= s1; s3 <= s2; rise_d <= {rise_d[6:0], s2 & ~s3}; end
`ifdef G2GMAC_FAULT_9
    wire mii_step = s2 & ~s3;                                    /* planted: no delay, the nibble changes near the edge */
`else
    wire mii_step = rise_d[MII_STEP_DELAY - 1];
`endif
    wire step = gmii ? 1'b1 : mii_step;

    // the length, taken in this clock the clock after the start toggle is seen (it has been static since GO), so the
    // only clk_cpu path into this group is register to register
    reg        start_q = 1'b0;
    reg [10:0] len_l = 11'd0;
    always @(posedge clk) begin start_q <= start; if (start) len_l <= len_a; end

    wire       busy, c_txen;
    wire [7:0] c_txd;
    g2_gmac_tx u_eng (
        .clk(clk), .rst(!t_on), .gmii(gmii), .step(step), .start(start_q), .len(len_l), .busy(busy),
        .rd_adr(rd_adr), .rd_dat(rd_dat), .txen(c_txen), .txd(c_txd));

    // done: the toggle goes back once the frame and its gap have left (busy falls); a start while disabled is answered
    reg busy_q = 1'b0, owe = 1'b0;
    always @(posedge clk) begin
        busy_q <= busy;
        if (!t_on) begin
            if (start_q) done_tog <= ~done_tog;                 // nothing will be sent: release the waiting side
            owe <= 1'b0;
        end else begin
            if (start_q) owe <= 1'b1;
            if (busy_q && !busy && owe) begin done_tog <= ~done_tog; owe <= 1'b0; end
        end
    end

    // the outputs in the IOBs; TX_ER is never raised (the buffer holds the whole frame before the wire starts)
    (* IOB = "TRUE" *) reg       txen_q = 1'b0, txer_q = 1'b0;
    (* IOB = "TRUE" *) reg [7:0] txd_q = 8'd0;
    always @(posedge clk) begin txen_q <= c_txen; txd_q <= c_txd; txer_q <= 1'b0; end
    assign eth_txen = txen_q;
    assign eth_txd  = txd_q;
    assign eth_txer = txer_q;
endmodule
`default_nettype wire
