/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_gmac.v -- the Pano Logic G2's Ethernet MAC: GMII at 1000 Mb/s and MII at 100 and 10 Mb/s on the 88E1119R's GMII
// balls, behind the register page and packet RAM of the G1's MII MAC (rtl/soc_ethlite.v), so fw/ethmac.c, the shells
// and a Linux driver written for that MAC run unchanged.  Mike Wolak, EPROM Foundry Inc.
//
// THE SOFTWARE VIEW (identical to rtl/soc_ethlite.v except register 10):
//     fast RAM word 1024 .. 1663   RX ring, 2,560 bytes       CPU 0x8000_A000 .. 0x8000_A9FF (uncached)
//     fast RAM word 1664 .. 2047   TX buffer, 1,536 bytes     CPU 0x8000_AA00 .. 0x8000_AFFF
//     registers 0..9 as soc_ethlite, with CTRL[8] RX_BIG added (below); 10 SPEED:
//         W [1:0] 0 or 3 automatic, 1 MII (10/100), 2 GMII (1000)
//         R [1:0] as written, [4] GMII in use, [5] a measurement has completed,
//           [31:16] RX_CLK / 8 rising edges counted in 16,384 clocks of clk (clk_cpu 66.667 MHz: count x 32.55 kHz)
//     11 RING_BASE  R  the CPU address of the RX ring in use: 0x8000_A000 (fast RAM) or 0x8200_0000 (the large ring)
// THE LARGE RX RING (CTRL[8] RX_BIG, taken while CTRL[0] RX_EN is 0): a 16 KB ring of its own (4,096 x 32 block RAM,
// written by the ring writer, read by the CPU at 0x8200_0000..0x8200_3FFF, uncached, one wait state) in place of the
// 2,560 bytes in the fast RAM.  The record format, RX_WR / RX_RD (byte offsets, now up to 14 bits) and the rules are
// the same; INFO[15:0] reports the size in use (2,560 or 16,384) and RING_BASE the address.  RX_BIG is 0 at reset, so
// software written for the 2,560-byte ring (the shells of record, the Linux and U-Boot drivers) runs unchanged; the
// TX buffer stays at 0x8000_AA00 either way.
// Automatic: GMII while RX_CLK measures above GMII_MIN (75 MHz at 66.667 MHz with the default), MII otherwise.  RX_CLK is
// 125 MHz at 1000 Mb/s and 25 or 2.5 MHz at 100 and 10 Mb/s, so the MAC follows the PHY's resolved speed by itself.  The
// receive side takes the mode between frames (RX_DV low), the transmit side at the start of a frame.
//
// RX RING RECORDS, as soc_ethlite: a header word ([10:0] length, [16] multicast/broadcast), the payload packed
// little-endian and padded to a word, wrapping at the ring end, every record starting on an even word; committed only
// with a good FCS, no RX_ER, 14..1518 bytes, the filter passed and every word fitted.  RX_WR == RX_RD means empty.
//
// DATA PATHS AT 1000 Mb/s.  Receive: 125 MB/s from the wire, packed into words in the RX_CLK domain and carried across
// in a 32-entry FIFO of 37-bit entries {sof, last, bad, bytes[1:0], word}; the ring writer stores one word per clock
// of clk, and fast RAM port A is the MAC's whenever it asks (pano_soc_g2.v), so 31.25 M words/s need under half of the
// port.  Transmit: store and forward.  GO copies the frame from the fast RAM into a 512 x 32 block RAM (one word per
// clock the ring writer leaves free), then starts the wire from that block RAM in the 125 MHz transmit clock; the wire
// can never run dry, so there is no underrun and TX_ER stays low (STATUS[3] and the TX_UNDER counter read 0).
//
// OVERSAMPLED RECEIVE (RX_OS = 1), for a fabric that has no global buffer left for RX_CLK: eth_rxc is the 125 MHz
// y13_clk, and RX_CLK (eth_rxc_pad) is sampled with RXD, RX_DV and RX_ER in IOB registers on it.  A rising edge of the
// sampled RX_CLK is a receive clock enable; the data sampled one clock before the detected edge (0 to 8 ns before
// the edge itself; 0 to 16 ns with a metastable RX_CLK sample resolved late, then 0 to 8 ns after) is the nibble of
// that edge, inside the MII window of 10 ns before to 10 ns after (IEEE 802.3 22.3.2.1).  MII only: the receive core
// takes nibbles and the speed register reads MII; at 1000 Mb/s RX_CLK is 125 MHz and cannot be sampled, so the PHY
// must not advertise 1000BASE-T (fw/ethphy.c phy_g2_advertise with gig 0).  The receive registers are those of the
// RX_CLK domain below with the enable added; the crossings are the same, with eth_rxc in the place of RX_CLK.
//
// CLOCK CROSSINGS, all of them:
//   eth_rxc -> clk   words through the 32 x 37 eth_cdc_fifo; a DATA word is pushed only while two entries are free, so
//                    the end-of-frame entry always fits; a start-of-frame entry resynchronises the ring writer.
//                    The RX_CLK / 8 edge count, gray coded, two flops (changes every 64 ns at 125 MHz).
//   clk -> eth_rxc   rx_en and the GMII decision, two flops each.
//   clk -> clk_tx    tx_en, the GMII decision, the start toggle (two and three flops); the length, static from the
//                    toggle until done; the transmit buffer is written in clk and read in clk_tx, never at once.
//   clk_tx -> clk    the done toggle, three flops.
//
// PLANTED FAULTS (rule 8, `define G2GMAC_FAULT_n; each must turn make g2gmac-stress red):
//   1  the partial last word of a frame is not written          2  TX_BUSY clears when the frame is copied, not sent
//   3  no ring-space check                                      4  the filter compares five address bytes, not six
//   5  a start-of-frame entry does not resynchronise the writer 6  the interrupt line never rises
//   7  a record is committed where it ended, not rounded        8  the speed detector never selects GMII
//   9  (g2_gmac_txd.v) the MII step is not delayed: TXD changes next to the TX_CLK edge
//  10  the large ring wraps at 640 words (the fast RAM ring's size)
`timescale 1ns / 1ps
`default_nettype none

module g2_gmac #(
    parameter integer RX_OS    = 0,              // 1: RX_CLK oversampled in eth_rxc = 125 MHz, MII only (above)
    parameter integer RXF_LOG2 = 14,             // the RX_CLK measurement window: 2^RXF_LOG2 clocks of clk
    parameter [15:0]  GMII_MIN = 16'd2304        // RX_CLK / 8 edges in the window above which GMII is selected
) (
    input  wire        clk,                      // clk_cpu
    input  wire        rst,

    input  wire        reg_en, reg_we,
    input  wire [3:0]  reg_adr,
    input  wire [31:0] reg_dat_w,
    output reg  [31:0] reg_dat_r,
    output reg         irq = 1'b0,

    output reg         pa_req,
    output reg  [9:0]  pa_addr,
    output reg  [31:0] pa_wdata,
    output reg  [3:0]  pa_wstrb,
    input  wire        pa_gnt,
    input  wire [31:0] pa_rdata,

    // the PHY
    input  wire        eth_rxc,                  // RX_CLK on a BUFG; with RX_OS, the 125 MHz y13_clk
    input  wire        eth_rxc_pad,              // RX_OS: the RX_CLK pad through an IBUF; unused otherwise
    input  wire        eth_rxdv, eth_rxer,       // from the pads (through the input delay in pano_g2_top.v)
    input  wire [7:0]  eth_rxd,
    input  wire        clk_tx,                   // 125 MHz, the PHY's CLK125 on a BUFG
    input  wire        mii_txc,                  // the TX_CLK pad (MII), sampled in clk_tx
    output wire        eth_txen, eth_txer,
    output wire [7:0]  eth_txd,
    output wire        gtx_on,                   // clk_tx domain: GTX_CLK runs
    // the large RX ring's CPU read port (0x8200_0000, pano_soc_g2.v): data one clock after en
    input  wire        ring_en,
    input  wire [11:0] ring_adr,                 // word
    output reg  [31:0] ring_q = 32'd0,
    output reg         rx_act = 1'b0,            // clk: RX_DV as received (IOB register, two more flops), for the LED
    input  wire        eth_link
);
    localparam [11:0] RX_LAST_S = 12'd639;      // the last word of the ring in the fast RAM (640 words)
    localparam [11:0] RX_LAST_B = 12'd4095;     // the last word of the large ring (4,096 words)
    localparam [9:0]  TX_BASE  = 10'd640;
    localparam [10:0] TX_MAX   = 11'd1536;
    localparam [10:0] RX_MAX   = 11'd1518;
    localparam [10:0] RX_MIN   = 11'd14;

    reg        rx_en = 1'b0, tx_en = 1'b0, promisc = 1'b0, allmulti = 1'b0, rx_ie = 1'b0, tx_ie = 1'b0, tx_done = 1'b0;
    reg [47:0] mac = 48'h0;
    reg [2:0]  stat_sel = 3'd0;
    reg [1:0]  spd_sel = 2'd0;
    reg [11:0] rx_wr = 12'd0, rx_rd = 12'd0;
    reg        rx_big = 1'b0, big = 1'b0;        // CTRL[8] as written; the mode in use (taken while RX_EN is 0)
`ifdef G2GMAC_FAULT_10
    wire [11:0] rx_last = RX_LAST_S;                            /* planted: the large ring wraps at 640 words */
`else
    wire [11:0] rx_last = big ? RX_LAST_B : RX_LAST_S;
`endif
    reg        tx_busy = 1'b0;
    reg [10:0] tx_len = 11'd0;
    reg [15:0] c_rx_ok = 0, c_rx_bad = 0, c_rx_space = 0, c_rx_filt = 0, c_rx_len = 0, c_rx_fifo = 0, c_tx_ok = 0;
    reg        use_gmii = 1'b0, f_done = 1'b0;
    reg [15:0] f_count = 16'd0;

    wire [36:0] rq_d; wire rq_empty, f_pop; reg rq_pop;    // rq_pop: the writer takes the entry held in e_q
    reg         tx_start_tog = 1'b0; wire tx_done_tog_c;
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [2:0] tx_done_s = 3'd0;

    // ======================================================================================================
    // 1. registers (clk)
    // ======================================================================================================
    wire go = reg_en && reg_we && (reg_adr == 4'd4) && reg_dat_w[31];
    wire [10:0] go_len = reg_dat_w[10:0];
    wire go_ok = go && tx_en && !tx_busy && (go_len != 11'd0) && (go_len <= TX_MAX);

    always @(posedge clk) begin
        if (rst) begin
            rx_en <= 1'b0; tx_en <= 1'b0; promisc <= 1'b0; allmulti <= 1'b0; rx_ie <= 1'b0; tx_ie <= 1'b0;
            mac <= 48'h0; stat_sel <= 3'd0; rx_rd <= 12'd0; spd_sel <= 2'd0; rx_big <= 1'b0;
        end else if (reg_en && reg_we) case (reg_adr)
            4'd0: begin rx_en <= reg_dat_w[0]; tx_en <= reg_dat_w[1]; promisc <= reg_dat_w[2]; allmulti <= reg_dat_w[3];
                        rx_ie <= reg_dat_w[4]; tx_ie <= reg_dat_w[5]; rx_big <= reg_dat_w[8];
                        if (reg_dat_w[8] != rx_big) rx_rd <= 12'd0; end                 // a new ring starts empty at 0
            4'd3: rx_rd <= (reg_dat_w[13:2] <= rx_last) ? reg_dat_w[13:2] : 12'd0;
            4'd5: mac[31:0]  <= reg_dat_w;
            4'd6: mac[47:32] <= reg_dat_w[15:0];
            4'd7: stat_sel <= reg_dat_w[2:0];
            4'd10: spd_sel <= reg_dat_w[1:0];
            default: ;
        endcase
    end

    always @(*) begin
        reg_dat_r = 32'd0;
        case (reg_adr)
        4'd0: reg_dat_r = {23'd0, rx_big, 2'd0, tx_ie, rx_ie, allmulti, promisc, tx_en, rx_en};
        4'd1: reg_dat_r = {27'd0, tx_done, 1'b0, eth_link, (rx_wr != rx_rd), tx_busy};
        4'd2: reg_dat_r = {18'd0, rx_wr, 2'b00};
        4'd3: reg_dat_r = {18'd0, rx_rd, 2'b00};
        4'd4: reg_dat_r = {21'd0, tx_len};
        4'd5: reg_dat_r = mac[31:0];
        4'd6: reg_dat_r = {16'd0, mac[47:32]};
        4'd7: reg_dat_r = {29'd0, stat_sel};
        4'd8: case (stat_sel)
                3'd0: reg_dat_r = {16'd0, c_rx_ok};    3'd1: reg_dat_r = {16'd0, c_rx_bad};
                3'd2: reg_dat_r = {16'd0, c_rx_space}; 3'd3: reg_dat_r = {16'd0, c_rx_filt};
                3'd4: reg_dat_r = {16'd0, c_rx_len};   3'd5: reg_dat_r = {16'd0, c_rx_fifo};
                3'd6: reg_dat_r = {16'd0, c_tx_ok};    3'd7: reg_dat_r = 32'd0;   // TX_UNDER: store and forward
              endcase
        4'd9:  reg_dat_r = {1'b1, 15'd1536, big ? 16'd16384 : 16'd2560};
        4'd11: reg_dat_r = big ? 32'h8200_0000 : 32'h8000_A000;
        4'd10: reg_dat_r = {f_count, 10'd0, f_done, use_gmii, 2'd0, spd_sel};
        default: reg_dat_r = 32'd0;
        endcase
    end

    // ======================================================================================================
    // 2. the speed: RX_CLK / 8 counted in gray code, measured over 2^RXF_LOG2 clocks of clk
    // ======================================================================================================
    reg [2:0]  rxc_pre = 3'd0;
    reg [15:0] rxc_bin = 16'd0, rxc_gray = 16'd0;
    wire       rx_ce;                            // the receive clock enable: 1, or the sampled RX_CLK edge (RX_OS)
    always @(posedge eth_rxc) if (rx_ce) begin
        rxc_pre <= rxc_pre + 3'd1;
        if (rxc_pre == 3'd7) begin rxc_bin <= rxc_bin + 16'd1; rxc_gray <= (rxc_bin + 16'd1) ^ ((rxc_bin + 16'd1) >> 1); end
    end
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [15:0] rg_s1 = 16'd0, rg_s2 = 16'd0;
    reg [15:0] rg_bin, f_prev = 16'd0;
    integer    gi;
    always @(*) begin
        rg_bin[15] = rg_s2[15];
        for (gi = 14; gi >= 0; gi = gi - 1) rg_bin[gi] = rg_bin[gi + 1] ^ rg_s2[gi];
    end
    reg [RXF_LOG2-1:0] f_tmr = 0;
    wire [15:0] f_delta = rg_bin - f_prev;
    always @(posedge clk) begin
        rg_s1 <= rxc_gray; rg_s2 <= rg_s1;
        f_tmr <= f_tmr + 1'b1;
        if (&f_tmr) begin f_prev <= rg_bin; f_count <= f_delta; f_done <= 1'b1; end
        if (rst) begin use_gmii <= 1'b0; f_done <= 1'b0; end
        else case (spd_sel)
            2'd1: use_gmii <= 1'b0;
            2'd2: use_gmii <= 1'b1;
`ifdef G2GMAC_FAULT_8
            default: use_gmii <= 1'b0;                           /* planted: automatic never selects GMII */
`else
            default: if (&f_tmr) use_gmii <= (RX_OS == 0) && (f_delta > GMII_MIN);
`endif
        endcase
    end

    // ======================================================================================================
    // 3. RX, eth_rxc side: IOB registers, the receive core, the word packer
    // ======================================================================================================
    (* IOB = "TRUE" *) reg [7:0] rxd_q = 8'd0;
    (* IOB = "TRUE" *) reg       rxdv_q = 1'b0, rxer_q = 1'b0;
    always @(posedge eth_rxc) begin rxd_q <= eth_rxd; rxdv_q <= eth_rxdv; rxer_q <= eth_rxer; end
    // the activity level for the board LED, taken from the IOB register: the delayed RX_DV (IODELAY2 DATAOUT) can
    // reach only its ILOGIC, so no fabric logic may read the pad net itself (it left erx_dly<8> unroutable)
    wire [7:0] rx_d; wire rx_dv, rx_er;              // the receive core's inputs
    generate if (RX_OS) begin : g_os
        (* IOB = "TRUE" *) reg c_q = 1'b0;           // RX_CLK, sampled beside rxd_q, rxdv_q, rxer_q
        (* shreg_extract = "no", equivalent_register_removal = "no" *) reg c_m = 1'b0, c_p = 1'b0;
        reg [9:0] d_m = 10'd0, d_p = 10'd0, d_o = 10'd0;
        reg       ce = 1'b0;
        always @(posedge eth_rxc) begin
            c_q <= eth_rxc_pad; c_m <= c_q; c_p <= c_m;
            d_m <= {rxer_q, rxdv_q, rxd_q}; d_p <= d_m;
            ce  <= c_m && !c_p;                          // RX_CLK rose between the samples in c_p and c_m
            if (c_m && !c_p) d_o <= d_p;                 // the sample one clock before c_m's
        end
        assign rx_ce = ce;
        assign {rx_er, rx_dv, rx_d} = d_o;
    end else begin : g_rxc
        assign rx_ce = 1'b1;
        assign {rx_er, rx_dv, rx_d} = {rxer_q, rxdv_q, rxd_q};
        wire _unused_pad = eth_rxc_pad;
    end endgenerate
    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg rx_act_m = 1'b0;
    always @(posedge clk) begin rx_act_m <= rxdv_q; rx_act <= rx_act_m; end

    (* shreg_extract = "no", equivalent_register_removal = "no" *) reg [1:0] rx_en_r = 2'b00, rx_gm_r = 2'b00;
    always @(posedge eth_rxc) begin rx_en_r <= {rx_en_r[0], rx_en}; rx_gm_r <= {rx_gm_r[0], use_gmii}; end
    wire rx_on = rx_en_r[1];

    wire [7:0] m_data; wire m_valid, m_sof, m_eof, m_crc_ok, m_rxer;
    g2_gmac_rx u_rx (
        .rxc(eth_rxc), .ce(rx_ce), .rst(!rx_on), .gmii(rx_gm_r[1] && (RX_OS == 0)), .rxdv(rx_dv), .rxer(rx_er), .rxd(rx_d),
        .o_data(m_data), .o_valid(m_valid), .o_sof(m_sof), .o_eof(m_eof), .o_crc_ok(m_crc_ok), .o_rxer(m_rxer));

    // packing: acc holds p_n bytes of the current word; a full word is pushed as the fourth byte arrives; the end entry
    // carries the partial word (0..3 bytes) and the verdict
    wire       rq_full; wire [5:0] rq_free;
    reg [23:0] acc = 24'd0;
    reg [1:0]  p_n = 2'd0;
    reg        p_sof = 1'b0, dropped = 1'b0;
    reg        eof_pend = 1'b0, eof_bad = 1'b0, eof_sof = 1'b0;
    reg [1:0]  eof_n = 2'd0;
    reg [23:0] eof_w = 24'd0;
    reg        rq_we; reg [36:0] rq_wd;
    wire       room2  = (rq_free >= 6'd2);
    wire       w_full = m_valid && (m_sof ? 1'b0 : (p_n == 2'd3));        // this byte completes a word
    wire       w_sof  = m_sof ? 1'b1 : p_sof;
    always @(*) begin
        rq_we = 1'b0; rq_wd = {1'b0, 1'b0, 1'b0, 2'd0, m_data, acc};
        if (eof_pend)                         begin rq_we = 1'b1; rq_wd = {eof_sof, 1'b1, eof_bad, eof_n, 8'd0, eof_w}; end
        else if (w_full && room2)             begin rq_we = 1'b1; rq_wd = {p_sof, 1'b0, 1'b0, 2'd0, m_data, acc}; end
    end
    always @(posedge eth_rxc) if (rx_ce) begin
        if (!rx_on) begin eof_pend <= 1'b0; dropped <= 1'b0; p_n <= 2'd0; p_sof <= 1'b0; end
        else begin
            if (eof_pend && !rq_full) eof_pend <= 1'b0;
            if (m_valid) begin
                if (m_sof) begin acc <= {16'd0, m_data}; p_n <= 2'd1; p_sof <= 1'b1; dropped <= 1'b0; end
                else begin
                    case (p_n)
                    2'd0: acc[7:0] <= m_data;  2'd1: acc[15:8] <= m_data;  2'd2: acc[23:16] <= m_data;
                    default: ;
                    endcase
                    p_n <= p_n + 2'd1;
                    if (p_n == 2'd3) begin
                        p_sof <= 1'b0; acc <= 24'd0;
                        if (eof_pend || !room2) dropped <= 1'b1;         // a word that did not go in
                    end
                end
            end
            if (m_eof) begin
                eof_pend <= 1'b1; eof_sof <= p_sof; eof_n <= p_n; eof_w <= acc;
                eof_bad  <= !m_crc_ok || m_rxer || dropped;
                p_n <= 2'd0; p_sof <= 1'b0;
            end
        end
    end

    eth_cdc_fifo #(.DW(37), .AW(5)) u_rq (
        .wr_clk(eth_rxc), .wr_rst(rst), .wr_data(rq_wd), .wr_en(rq_we && rx_ce), .full(rq_full), .wr_free(rq_free),
        .rd_clk(clk), .rd_rst(rst), .rd_data(rq_d), .rd_en(f_pop), .empty(rq_empty));

    // ======================================================================================================
    // 4. RX, clk side: the ring writer, one word per clock
    // ======================================================================================================
    localparam [2:0] R_IDLE = 3'd0, R_DATA = 3'd1, R_DROP = 3'd2, R_HDR = 3'd4, R_COMMIT = 3'd5;
    reg [2:0]  rs = R_IDLE;
    reg [11:0] fs = 12'd0, wp = 12'd0;
    reg [10:0] nb = 11'd0;
    reg        m_uc = 1'b0, m_bc = 1'b0, is_mc = 1'b0;
    reg        rw_pend = 1'b0;
    reg [11:0] rw_addr = 12'd0; reg [31:0] rw_data = 32'd0;
    reg [1:0]  drop_why = 2'd0;              // 0 fifo/resync, 1 space, 3 length

    // the last word is an argument, not a free reference: a simulator re-evaluates a continuous assignment only on its
    // arguments' changes
    function [11:0] nxt; input [11:0] a; input [11:0] last; begin nxt = (a == last) ? 12'd0 : a + 12'd1; end endfunction

    // the FIFO's head entry is registered first (e_q, valid e_v): the crossing ends in a register and the writer's
    // compares start from one, so no clk path hangs off the RX_CLK-written distributed RAM.  An entry is refilled in the
    // clock the writer takes one: one entry per clock sustained.
    reg         e_v = 1'b0;
    reg  [36:0] e_q = 37'd0;
    assign      f_pop  = !rq_empty && (!e_v || rq_pop);
    always @(posedge clk) begin
        if (rst) e_v <= 1'b0;
        else if (f_pop) begin e_q <= rq_d; e_v <= 1'b1; end
        else if (rq_pop) e_v <= 1'b0;
    end
    wire        e_none = !e_v;
    wire        e_sof  = e_q[36], e_last = e_q[35], e_bad = e_q[34];
    wire [1:0]  e_n    = e_q[33:32];
    wire [31:0] e_w    = e_q[31:0];
    wire        r_sof  = !e_none && e_sof;
`ifdef G2GMAC_FAULT_5
    wire        r_resync = 1'b0;
`else
    wire        r_resync = r_sof;
`endif
    wire        rx_gnt  = rw_pend && (big || pa_gnt);     // the large ring is the writer's alone: every clock
    wire        rw_free = !rw_pend || rx_gnt;            // a new ring write may be loaded this clock
    // where the word popped this clock goes: the first word of a frame follows its header slot
    wire [11:0] wpx     = (rs == R_IDLE) ? nxt(rx_wr, rx_last) : wp;
`ifdef G2GMAC_FAULT_3
    wire        room_x  = 1'b1, room_fs = 1'b1;
`else
    wire        room_x  = (nxt(wpx, rx_last) != rx_rd);
    wire        room_fs = (nxt(rx_wr, rx_last) != rx_rd);
`endif
    // the destination address: bytes 0..3 in the first word, 4..5 in the second
    wire        uc0 = (e_w == mac[31:0]);
`ifdef G2GMAC_FAULT_4
    wire        uc1 = (e_w[7:0] == mac[39:32]);          // planted: byte 5 not compared
`else
    wire        uc1 = (e_w[15:0] == mac[47:32]);
`endif
    // the second word (nb == 4) completes the six address bytes; the verdict includes it when it is the last entry
    wire        upd2    = (nb == 11'd4);
    wire        m_uc_n  = upd2 ? (m_uc && uc1) : m_uc;
    wire        m_bc_n  = upd2 ? (m_bc && (e_w[15:0] == 16'hFFFF)) : m_bc;
    wire        filt_ok = promisc || m_uc_n || m_bc_n || (is_mc && allmulti);
    wire [10:0] nb_last = nb + {9'd0, e_n};

    always @(*) begin
        rq_pop = 1'b0;
        case (rs)
        R_IDLE:  rq_pop = !e_none && !(r_sof && rx_en && !rw_free);     // a start waits for the port; the rest is taken
        R_DATA:  rq_pop = !e_none && rw_free && !r_resync;               // a start here is left for IDLE (resync)
        R_DROP:  rq_pop = !e_none && !r_resync;
        default: rq_pop = 1'b0;
        endcase
    end

    always @(posedge clk) begin
        if (rst) begin
            rs <= R_IDLE; rx_wr <= 12'd0; rw_pend <= 1'b0; big <= 1'b0;
            c_rx_ok <= 0; c_rx_bad <= 0; c_rx_space <= 0; c_rx_filt <= 0; c_rx_len <= 0; c_rx_fifo <= 0;
        end else begin
            if (rx_gnt) rw_pend <= 1'b0;
            case (rs)
            R_IDLE: if (r_sof && rx_en && rw_free) begin
                        fs <= rx_wr; nb <= 11'd4;
                        is_mc <= e_w[0]; m_bc <= (e_w == 32'hFFFF_FFFF); m_uc <= uc0;
                        if (e_last) begin c_rx_len <= c_rx_len + 16'd1; end              // under four bytes
                        else if (!room_fs || !room_x) begin drop_why <= 2'd1; rs <= R_DROP; end
                        else begin
                            rw_pend <= 1'b1; rw_addr <= wpx; rw_data <= e_w; wp <= nxt(wpx, rx_last); rs <= R_DATA;
                        end
                    end
            R_DATA: if (!e_none && rw_free) begin
                        if (r_resync) begin
                            c_rx_fifo <= c_rx_fifo + 16'd1; rs <= R_IDLE;                  // this frame's end was lost
                        end else if (e_last) begin
                            if (e_bad)                                     begin c_rx_bad  <= c_rx_bad  + 16'd1; rs <= R_IDLE; end
                            else if (nb_last < RX_MIN || nb_last > RX_MAX) begin c_rx_len  <= c_rx_len  + 16'd1; rs <= R_IDLE; end
                            else if (!filt_ok)                             begin c_rx_filt <= c_rx_filt + 16'd1; rs <= R_IDLE; end
                            else begin
                                nb <= nb_last;
`ifdef G2GMAC_FAULT_1
                                rs <= R_HDR;
`else
                                if (e_n == 2'd0) rs <= R_HDR;
                                else if (!room_x) begin c_rx_space <= c_rx_space + 16'd1; rs <= R_IDLE; end
                                else begin rw_pend <= 1'b1; rw_addr <= wpx; rw_data <= e_w; wp <= nxt(wpx, rx_last); rs <= R_HDR; end
`endif
                            end
                        end else begin
                            m_uc <= m_uc_n; m_bc <= m_bc_n;
                            nb <= nb + 11'd4;
                            if (nb + 11'd4 > RX_MAX) begin drop_why <= 2'd3; rs <= R_DROP; end
                            else if (!room_x) begin drop_why <= 2'd1; rs <= R_DROP; end
                            else begin rw_pend <= 1'b1; rw_addr <= wpx; rw_data <= e_w; wp <= nxt(wpx, rx_last); end
                        end
                    end
            R_HDR:  if (rw_free) begin
                        rw_pend <= 1'b1; rw_addr <= fs; rw_data <= {15'd0, is_mc, 5'd0, nb}; rs <= R_COMMIT;
                    end
`ifdef G2GMAC_FAULT_7
            R_COMMIT: if (!rw_pend) begin rx_wr <= wp; c_rx_ok <= c_rx_ok + 16'd1; rs <= R_IDLE; end
`else
            R_COMMIT: if (!rw_pend) begin
                        if (wp[0] && (nxt(wp, rx_last) == rx_rd)) begin c_rx_space <= c_rx_space + 16'd1; rs <= R_IDLE; end
                        else begin rx_wr <= wp[0] ? nxt(wp, rx_last) : wp; c_rx_ok <= c_rx_ok + 16'd1; rs <= R_IDLE; end
                      end
`endif
            R_DROP: if (!e_none) begin
                        if (r_resync) begin c_rx_fifo <= c_rx_fifo + 16'd1; rs <= R_IDLE; end
                        else if (e_last) begin
                            case (drop_why)
                            2'd1:    c_rx_space <= c_rx_space + 16'd1;
                            2'd3:    c_rx_len   <= c_rx_len   + 16'd1;
                            default: c_rx_fifo  <= c_rx_fifo  + 16'd1;
                            endcase
                            rs <= R_IDLE;
                        end
                    end
            default: rs <= R_IDLE;
            endcase
            if (!rx_en) begin rs <= R_IDLE; rx_wr <= rx_rd; if (!rw_pend) big <= rx_big; end
        end
    end

    // the large ring: written by the ring writer (the clock its write is granted), read by the CPU
    (* ram_style = "block" *) reg [31:0] rbuf [0:4095];
    always @(posedge clk) begin
        if (rw_pend && big) rbuf[rw_addr] <= rw_data;
        if (ring_en) ring_q <= rbuf[ring_adr];
    end

    // ======================================================================================================
    // 5. TX, clk side: GO, the copy into the transmit buffer, TX_BUSY
    // ======================================================================================================
    localparam [1:0] T_IDLE = 2'd0, T_COPY = 2'd1, T_WAIT = 2'd2;
    reg [1:0]  tst = T_IDLE;
    reg [9:0]  t_addr = 10'd0;
    reg [8:0]  t_left = 9'd0, t_widx = 9'd0;
    reg        t_rd = 1'b0, t_wv = 1'b0;
    reg [31:0] t_wd = 32'd0;
    wire       tx_gnt = pa_gnt && t_rd && !(rw_pend && !big);
    wire       tx_done_pulse = tx_done_s[2] ^ tx_done_s[1];

    (* ram_style = "block" *) reg [31:0] tbuf [0:511];
    always @(posedge clk) if (t_wv) tbuf[t_widx] <= t_wd;
    wire [8:0] tb_ra; reg [31:0] tb_rd = 32'd0;
    always @(posedge clk_tx) tb_rd <= tbuf[tb_ra];

    always @(posedge clk) begin
        tx_done_s <= {tx_done_s[1:0], tx_done_tog_c};
        if (rst) begin
            tst <= T_IDLE; tx_busy <= 1'b0; t_rd <= 1'b0; t_wv <= 1'b0; c_tx_ok <= 0; tx_done <= 1'b0;
        end else begin
            if (reg_en && reg_we && reg_adr == 4'd1 && reg_dat_w[4]) tx_done <= 1'b0;
            // the word read on a granted cycle is registered, then written into the buffer the clock after
            t_wv <= 1'b0;
            if (t_wv) t_widx <= t_widx + 9'd1;
            if (tx_gnt) begin t_wd <= pa_rdata; t_wv <= 1'b1; t_addr <= t_addr + 10'd1; t_left <= t_left - 9'd1; end
            case (tst)
            T_IDLE: if (go_ok) begin
                        tx_busy <= 1'b1; tx_len <= go_len; t_addr <= TX_BASE; t_widx <= 9'd0;
                        t_left <= go_len[10:2] + {8'd0, (go_len[1:0] != 2'd0)};
                        t_rd <= 1'b1; tst <= T_COPY;
                    end
            T_COPY: begin
                        if (tx_gnt && t_left == 9'd1) t_rd <= 1'b0;
                        if (!t_rd && !t_wv) begin
                            tx_start_tog <= ~tx_start_tog; tst <= T_WAIT;
`ifdef G2GMAC_FAULT_2
                            tx_busy <= 1'b0;
`endif
                        end
                    end
            T_WAIT: if (tx_done_pulse) begin
                        tx_busy <= 1'b0; tst <= T_IDLE; tx_done <= 1'b1; c_tx_ok <= c_tx_ok + 16'd1;
                    end
            default: tst <= T_IDLE;
            endcase
        end
    end

    always @(posedge clk) begin
        if (rst) irq <= 1'b0;
        else begin
`ifdef G2GMAC_FAULT_6
            irq <= 1'b0;
`else
            irq <= (rx_ie && rx_wr != rx_rd) || (tx_ie && tx_done);
`endif
        end
    end

    // ======================================================================================================
    // 6. the port A requester: one access in flight, RX writes first
    // ======================================================================================================
    always @(*) begin
        pa_req = 1'b0; pa_addr = 10'd0; pa_wdata = 32'd0; pa_wstrb = 4'd0;
        if (rw_pend && !big) begin pa_req = 1'b1; pa_addr = rw_addr[9:0]; pa_wdata = rw_data; pa_wstrb = 4'hF; end
        else if (t_rd) begin pa_req = 1'b1; pa_addr = t_addr; end
    end

    // ======================================================================================================
    // 7. TX, clk_tx side
    // ======================================================================================================
    g2_gmac_txd u_txd (
        .clk(clk_tx), .tx_en_a(tx_en), .gmii_a(use_gmii), .start_tog_a(tx_start_tog), .len_a(tx_len),
        .mii_txc(mii_txc), .rd_adr(tb_ra), .rd_dat(tb_rd), .done_tog(tx_done_tog_c),
        .eth_txen(eth_txen), .eth_txer(eth_txer), .eth_txd(eth_txd), .gtx_on(gtx_on));

    wire _unused = ^{rq_full, e_bad};
endmodule
`default_nettype wire
