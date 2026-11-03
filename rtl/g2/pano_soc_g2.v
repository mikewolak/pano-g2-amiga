/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// pano_soc_g2.v -- the Pano Logic G2 SoC fabric, stage 1: block RAM only.
//
// Derived from rtl/pano_soc_vex.v (the G1 SoC).  Removed: the LPDDR bridge, store-merge buffer and copy engine, the
// ISP1760 PIO page and chip-memory mover, the audio engine, the G1 video head, the JTAG console mailbox and its
// loader.  Kept with their G1 register maps and addresses: the console (now a UART), the timer, the board page, the
// fast RAM's uncached pages and the MAC's register page and packet RAM (the MAC itself is rtl/g2/g2_gmac.v: GMII and
// MII behind the G1 MII MAC's register map).  New: the SPI flash master with its write guard, the recovery page
// (IPROG, watchdog, button), and three external ports for the workstreams that follow (DDR, video, USB).
//
// ADDRESS MAP
//   0x0000_0000  block RAM, 256 KB, cached; reset vector
//   0x1000_0000  DDR port, 64 MB (chip A, MCB1), cached
//   0x9000_0000  the same 64 MB, uncached (address bit 31 selects uncached in the core)
//   0x8000_1000  console (UART)            0x8000_2000  timer            0x8000_5000  board page
//   0x8000_6000  video register port       0x8000_8000  fast RAM, low 4 KB (uncached)
//   0x8000_A000  MAC packet RAM (fast RAM, upper 4 KB)                   0x8000_B000  MAC registers
//   0x8000_C000  SPI flash master          0x8000_D000  recovery
//   0x8100_0000  USB port, 16 MB window    0x8200_0000  the MAC's large RX ring, 16 KB, read only (rtl/g2/g2_gmac.v)
// Any other address is acknowledged, reads 0 and sets the sticky bus error (board page +0x20/+0x24).
//
// BUS.  The CPU's two Wishbone masters (iBus, dBus) share one bus through a round-robin arbiter; a grant is held
// until the master drops CYC.  Block RAM, the peripheral pages and bad addresses acknowledge one clock after the
// request; the fast RAM acknowledges in the cycle of the request (port A on the falling edge, rtl/soc_fastram.v).
// The external ports (DDR, video, USB) acknowledge when their slave does; an access the slave has not acknowledged
// after 4,096 clocks is acknowledged by the fabric with 0xFFFF_FFFF and counted (board page +0x28).
// CTI/BTE of the granted master are passed to the DDR port for line bursts.
`timescale 1ns / 1ps
`default_nettype none

module pano_soc_g2 #(
    parameter RAM_INIT_HEX = "",              // prefix of the four byte-lane hex files; "" = uninitialised
    parameter RAM_AW       = 16,              // words: 16 -> 65,536 words -> 256 KB
    parameter UART_DIV     = 434,             // 50 MHz / 434 = 115,207 baud
    parameter HOLD_LOG2    = 22,              // reset window for the button (g2_recovery.v)
    parameter [31:0] WDT_DEFAULT = 32'd1_500_000_000,   // watchdog reset value in clk cycles (g2_recovery.v)
    parameter DEB_LOG2     = 16,
    parameter ETH_RX_OS    = 0                // 1: RX_CLK oversampled in eth_rxc = y13_clk (g2_gmac.v RX_OS)
) (
    input  wire        clk,                   // the CPU domain clock (clk_cpu, 75 MHz; clk_sys in the single-domain top)
    input  wire        rst,                   // active high, synchronous to clk
    input  wire        clk_icap,              // 10 MHz, for ICAP_SPARTAN6 (12 MHz maximum at -2)

    // console UART
    output wire        uart_txd,
    input  wire        uart_rxd,

    // SPI configuration flash
    output wire        spi_cs_n,
    output wire        spi_sck,
    output wire        spi_mosi,
    input  wire        spi_miso,

    // Ethernet PHY, GMII (1000 Mb/s) and MII (100/10 Mb/s) on the same balls (rtl/g2/g2_gmac.v)
    input  wire        eth_rxc,               // PHY RX_CLK on a BUFG; with ETH_RX_OS, y13_clk
    input  wire        eth_rxc_pad,           // ETH_RX_OS: the RX_CLK pad through an IBUF
    input  wire        eth_rxdv, eth_rxer,
    input  wire [7:0]  eth_rxd,
    input  wire        clk_tx,                // 125 MHz, the PHY's CLK125 on a BUFG: GMII transmit and the MII TX_CLK sampler
    input  wire        mii_txc,               // the PHY's TX_CLK pad (MII), sampled in clk_tx
    output wire        eth_txen, eth_txer,
    output wire [7:0]  eth_txd,
    output wire        gtx_on,                // clk_tx domain: GTX_CLK runs
    input  wire        eth_link,              // raw pin level of AA4 (function unconfirmed)
    output reg         mdc = 1'b0,               // the MDIO levels are re-registered once (mdc_r ...) so the pad flops
    output reg         mdio_o = 1'b0,            // see only a local flop, not the bus write path (a 14 ns path at 75 MHz)
    output reg         mdio_oe = 1'b0,
    input  wire        mdio_i,

    // front panel
    input  wire        button_n,              // H12, active low
    output reg         led_ovr = 1'b0,        // board page op 4: the CPU owns the LEDs
    output reg  [2:0]  led_val = 3'b000,      // {red, blue, green} when led_ovr
    output wire        activity,              // flash selected or a frame on the wire, for the blue LED
    // three board lines of untraced function, pads in the top: [0] AA6 RESET_OUT_N, [1] C10 POWER_SLEEP, [2] C13 SLEEP_REQ
    output reg  [2:0]  pin_oe = 3'b001,       // output enables; reset: AA6 driven, C10 and C13 released
    output reg  [2:0]  pin_o  = 3'b001,       // output levels; reset: AA6 high
    input  wire [2:0]  pin_i,                 // pad levels (asynchronous)
    output wire        bus_err,               // sticky bus error, for the red LED
    input  wire [31:0] clk_status,            // board page +0x2C, from the top

    // ---- DDR port (Wishbone B4 classic with CTI/BTE; the fabric is the master) -------------------------------
    output wire        ddr_cyc,
    output wire        ddr_stb,
    output wire        ddr_we,
    output wire [23:0] ddr_adr,               // word address inside the 64 MB of chip A (byte address [25:2])
    output wire        ddr_uncached,          // the access came through the 0x9000_0000 alias
    output wire [3:0]  ddr_sel,
    output wire [31:0] ddr_dat_w,
    output wire [2:0]  ddr_cti,
    output wire [1:0]  ddr_bte,
    input  wire [31:0] ddr_dat_r,
    input  wire        ddr_ack,
    input  wire        ddr_err,               // acknowledges the beat as a bus error (counted at board page +0x20)
    input  wire        ddr_ready,             // calibration done; while low every DDR access is a bus error
    input  wire [7:0]  ddr_status,            // board page +0x04
    output reg         ddr_st_clear = 1'b0,   // board page op 7: one clock
    output reg  [1:0]  ddr_x_en = 2'b00,      // board page op 7 operand [1:0]: run the chip A / chip B whole-chip exercisers (off from reset: they overwrite the whole chip)
    input  wire [31:0] ddr_x_passes,          // board page +0x0C: {chip A passes, chip B passes}
    input  wire [31:0] ddr_x_errs,            // board page +0x14: {chip A mismatching words, chip B mismatching words}
    input  wire        ddr_irq,

    // ---- video register port (page 0x8000_6000) ----------------------------------------------------------------
    output wire        vid_cyc,
    output wire        vid_we,
    output wire [9:0]  vid_adr,               // word inside the 4 KB page
    output wire [3:0]  vid_sel,
    output wire [31:0] vid_dat_w,
    input  wire [31:0] vid_dat_r,
    input  wire        vid_ack,
    input  wire        vid_irq,
    // ---- second video register port (page 0x8000_7000): the same contract ---------------------------------------
    output wire        vid2_cyc,
    output wire        vid2_we,
    output wire [9:0]  vid2_adr,
    output wire [3:0]  vid2_sel,
    output wire [31:0] vid2_dat_w,
    input  wire [31:0] vid2_dat_r,
    input  wire        vid2_ack,
    input  wire        vid2_irq,
    // ---- audio register port (page 0x8000_9000, rtl/g2/g2_audio.v): the same contract, no interrupt ------------
    output wire        aud_cyc,
    output wire        aud_we,
    output wire [9:0]  aud_adr,
    output wire [3:0]  aud_sel,
    output wire [31:0] aud_dat_w,
    input  wire [31:0] aud_dat_r,
    input  wire        aud_ack,
    // ---- RasterIX register port (page 0x8000_E000, rtl/g2/g2_rix.v): the same contract; interrupt line 6 -----------
    output wire        rix_cyc,
    output wire        rix_we,
    output wire [9:0]  rix_adr,
    output wire [3:0]  rix_sel,
    output wire [31:0] rix_dat_w,
    input  wire [31:0] rix_dat_r,
    input  wire        rix_ack,
    input  wire        rix_irq,
    // ---- DDR2 chip B port (0x1400_0000 cached, 0x9400_0000 uncached; the same Wishbone contract as chip A) ------
    output wire        ddrb_cyc,
    output wire        ddrb_stb,
    output wire        ddrb_we,
    output wire [23:0] ddrb_adr,
    output wire [3:0]  ddrb_sel,
    output wire [31:0] ddrb_dat_w,
    output wire [2:0]  ddrb_cti,
    output wire [1:0]  ddrb_bte,
    input  wire [31:0] ddrb_dat_r,
    input  wire        ddrb_ack,
    input  wire        ddrb_err,
    input  wire        ddrb_ready,

    // ---- USB port (0x8100_0000, 16 MB) ----------------------------------------------------------------------
    output wire        usb_cyc,
    output wire        usb_we,
    output wire [21:0] usb_adr,               // word inside the window
    output wire [3:0]  usb_sel,
    output wire [31:0] usb_dat_w,
    input  wire [31:0] usb_dat_r,
    input  wire        usb_ack,
    input  wire        usb_irq,

    output wire        cpu_trap               // = bus_err
);
    localparam [1:0] G_IDLE = 2'd0, G_D = 2'd1, G_I = 2'd2;

    // ---- declarations (XST requires declaration before use) -----------------------------------------------
    wire        cpu_hold;
    wire        cpu_rst = rst | cpu_hold;
    wire        i_cyc, i_stb, i_we; wire [29:0] i_adr; wire [31:0] i_mosi; wire [3:0] i_sel; wire [2:0] i_cti; wire [1:0] i_bte;
    wire        d_cyc, d_stb, d_we; wire [29:0] d_adr; wire [31:0] d_mosi; wire [3:0] d_sel; wire [2:0] d_cti; wire [1:0] d_bte;
    (* fsm_extract = "no" *)
    reg  [1:0]  gst = G_IDLE;
    reg         last_gnt_d = 1'b0;
    wire        m_cyc, m_stb, m_we; wire [29:0] m_adr; wire [31:0] m_mosi; wire [3:0] m_sel; wire [2:0] m_cti; wire [1:0] m_bte;
    wire        pg_per, sel_ram, sel_fast, sel_per, sel_ddr, sel_usb, sel_vid, sel_vid2, sel_ddrb, sel_aud, sel_ring, sel_rix;
    wire [3:0]  per_idx;
    wire        per_int;                      // an internal peripheral page
    wire        ram_en, per_en, bad_en, fast_en, fast_hold;
    reg         ram_ack = 1'b0, per_ack = 1'b0, bad_ack = 1'b0, ring_ack = 1'b0;
    wire        ring_en;
    wire [31:0] ring_q;
    reg  [31:0] per_q = 32'd0;
    wire [31:0] ram_q, fast_q;
    wire        ack_out;
    wire [31:0] s_miso;
    reg         err_sticky = 1'b0;
    reg  [31:0] bad_cnt = 32'd0, bad_addr = 32'd0, to_cnt = 32'd0;
    // external ports
    wire        ext_sel, ext_ack;
    wire [31:0] ext_dat;
    reg  [12:0] to_timer = 13'd0;
    reg         to_ack = 1'b0;
    reg  [31:0] usb_to_cnt = 32'd0, usb_to_addr = 32'd0;
    // peripherals
    wire        con_en, tmr_en, brd_en, eth_en, spi_en, rec_en;
    wire [31:0] con_dat_r, tmr_dat_r, eth_dat_r, spi_dat_r, rec_dat_r;
    wire        tmr_irq, eth_irq, eth_rx_act;
    wire        mac_req, mac_gnt; wire [9:0] mac_addr; wire [31:0] mac_wdata; wire [3:0] mac_wstrb;
    wire        fa_en; wire [10:0] fa_addr; wire [31:0] fa_wdata; wire [3:0] fa_wstrb;
    wire        spi_miso_lvl, spi_cs_act;
    reg         bb_claim = 1'b0, bb_cs_n = 1'b1, bb_sck = 1'b0, bb_mosi = 1'b0;
    reg  [27:0] cal_status = 28'd0;
    wire        btn;
    wire        icap_req_tog, icap_done_tog; wire [23:0] icap_addr;
    reg  [19:0] act_str = 20'd0;
`ifdef BUILD_ID
    localparam [31:0] BUILD_ID = `BUILD_ID;
`else
    localparam [31:0] BUILD_ID = 32'h0000_0000;
`endif

    // ---- arbiter: round robin between dBus and iBus, grant held until CYC drops -------------------------------
    always @(posedge clk) begin
        if (cpu_rst) begin gst <= G_IDLE; last_gnt_d <= 1'b0; end
        else case (gst)
            G_IDLE: begin
                if (d_cyc && (!i_cyc || !last_gnt_d)) begin gst <= G_D; last_gnt_d <= 1'b1; end
                else if (i_cyc)                       begin gst <= G_I; last_gnt_d <= 1'b0; end
            end
            G_D:     if (!d_cyc) gst <= G_IDLE;
            G_I:     if (!i_cyc) gst <= G_IDLE;
            default: gst <= G_IDLE;
        endcase
    end
    assign m_cyc  = (gst == G_D) ? d_cyc : (gst == G_I) ? i_cyc : 1'b0;
    assign m_stb  = (gst == G_D) ? d_stb : (gst == G_I) ? i_stb : 1'b0;
    assign m_we   = (gst == G_D) ? d_we  : 1'b0;
    assign m_adr  = (gst == G_D) ? d_adr : i_adr;
    assign m_mosi = d_mosi;
    assign m_sel  = (gst == G_D) ? d_sel : 4'hF;
    assign m_cti  = (gst == G_D) ? d_cti : i_cti;
    assign m_bte  = (gst == G_D) ? d_bte : i_bte;

    // ---- decode (m_adr is a word address: byte address bit n is m_adr[n-2]) ---------------------------------
    assign sel_ram  = (m_adr[29:RAM_AW] == {(30-RAM_AW){1'b0}});
    assign sel_ddr  = (m_adr[28:24] == 5'b00100);                    // byte [30:26] = 00100, either value of bit 31
    assign sel_ddrb = (m_adr[28:24] == 5'b00101);                    // byte [30:26] = 00101: chip B, 0x1400_0000 / 0x9400_0000
    assign pg_per   = (m_adr[29:14] == 16'h8000);                    // 0x8000_0000 .. 0x8000_FFFF
    assign per_idx  = m_adr[13:10];
    // the fast RAM's uncached page is reached by data accesses only (no code is fetched from 0x8000_xxxx): the decode
    // uses the data port's address, and the grant is the last gate -- the bus address multiplexer is off the path
    // into the block RAM's enable (a half-cycle path, port A on the falling edge)
    wire   d_fast   = (d_adr[29:14] == 16'h8000) && ((d_adr[13:10] == 4'd8) || (d_adr[13:10] == 4'd10));
    assign sel_fast = (gst == G_D) && d_fast;
    assign sel_vid  = pg_per && (per_idx == 4'd6);
    assign sel_vid2 = pg_per && (per_idx == 4'd7);
    assign sel_aud  = pg_per && (per_idx == 4'd9);
    assign sel_rix  = pg_per && (per_idx == 4'd14);
    assign per_int  = (per_idx == 4'd1) || (per_idx == 4'd2) || (per_idx == 4'd5) || (per_idx == 4'd11) ||
                      (per_idx == 4'd12) || (per_idx == 4'd13);
    assign sel_per  = pg_per && per_int;
    assign sel_usb  = (m_adr[29:22] == 8'h81);                       // 0x8100_0000 .. 0x81FF_FFFF
    assign sel_ring = (m_adr[29:12] == 18'h20800);                   // 0x8200_0000 .. 0x8200_3FFF (uncached only)

    wire   req     = m_cyc && m_stb;
    wire   ddr_live  = sel_ddr && ddr_ready;
    wire   ddrb_live = sel_ddrb && ddrb_ready;
    assign ext_sel = ddr_live || ddrb_live || sel_vid || sel_vid2 || sel_usb || sel_aud || sel_rix;
    assign ram_en  = req && sel_ram && !ram_ack;
    assign ring_en = req && sel_ring && !ring_ack;                   // reads; a write is acknowledged and ignored
    assign per_en  = req && sel_per && !per_ack;
    assign fast_en = req && sel_fast;
    assign bad_en  = req && !sel_ram && !sel_per && !sel_fast && !sel_ring && !ext_sel && !bad_ack;

    assign con_en = per_en && (per_idx == 4'd1);
    assign tmr_en = per_en && (per_idx == 4'd2);
    assign brd_en = per_en && (per_idx == 4'd5);
    reg         mdc_r = 1'b0, mdio_o_r = 1'b0, mdio_oe_r = 1'b0;
    always @(posedge clk) begin mdc <= mdc_r; mdio_o <= mdio_o_r; mdio_oe <= mdio_oe_r; end
    assign eth_en = per_en && (per_idx == 4'd11);
    assign spi_en = per_en && (per_idx == 4'd12);
    assign rec_en = per_en && (per_idx == 4'd13);

    // ---- external ports -----------------------------------------------------------------------------------
    assign ddr_cyc      = m_cyc && ddr_live;
    assign ddr_stb      = req && ddr_live && !to_ack;
    assign ddr_we       = m_we;
    assign ddr_adr      = m_adr[23:0];
    assign ddr_uncached = m_adr[29];
    assign ddr_sel      = m_sel;
    assign ddr_dat_w    = m_mosi;
    assign ddr_cti      = m_cti;
    assign ddr_bte      = m_bte;
    assign aud_cyc      = req && sel_aud && !to_ack;
    assign aud_we       = m_we;
    assign aud_adr      = m_adr[9:0];
    assign aud_sel      = m_sel;
    assign aud_dat_w    = m_mosi;
    assign rix_cyc      = req && sel_rix && !to_ack;
    assign rix_we       = m_we;
    assign rix_adr      = m_adr[9:0];
    assign rix_sel      = m_sel;
    assign rix_dat_w    = m_mosi;
    assign vid_cyc      = req && sel_vid && !to_ack;
    assign vid_we       = m_we;
    assign vid_adr      = m_adr[9:0];
    assign vid_sel      = m_sel;
    assign vid_dat_w    = m_mosi;
    assign vid2_cyc     = req && sel_vid2 && !to_ack;
    assign vid2_we      = m_we;
    assign vid2_adr     = m_adr[9:0];
    assign vid2_sel     = m_sel;
    assign vid2_dat_w   = m_mosi;
    assign ddrb_cyc     = m_cyc && ddrb_live;
    assign ddrb_stb     = req && ddrb_live && !to_ack;
    assign ddrb_we      = m_we;
    assign ddrb_adr     = m_adr[23:0];
    assign ddrb_sel     = m_sel;
    assign ddrb_dat_w   = m_mosi;
    assign ddrb_cti     = m_cti;
    assign ddrb_bte     = m_bte;
    assign usb_cyc      = req && sel_usb && !to_ack;
    assign usb_we       = m_we;
    assign usb_adr      = m_adr[21:0];
    assign usb_sel      = m_sel;
    assign usb_dat_w    = m_mosi;
    wire   ddr_ack_g = (ddr_ack || ddr_err) && req && ddr_live;
    wire   ddr_err_g = ddr_err && req && ddr_live;
    wire   ddrb_ack_g = (ddrb_ack || ddrb_err) && req && ddrb_live;
    wire   ddrb_err_g = ddrb_err && req && ddrb_live;
    /* The pages behind wb_cdc (video heads, audio, RasterIX, USB) are gated with REGISTERED selects: psel_q holds the
     * page decode of the previous clock and cont_q says that the previous clock carried the same request unfinished
     * (req and no acknowledge).  A crossing never acknowledges in the first clock of a request, so no acknowledge is
     * lost; an acknowledge left over from a timed-out request is still refused in the first clock of the next one,
     * as the combinational gate refused it.  The address decode (m_adr -> pg_per -> sel_*) is off the path into
     * the CPU's acknowledge. */
    reg    [4:0] psel_q = 5'd0;
    reg          cont_q = 1'b0;
    always @(posedge clk) begin
        psel_q <= {sel_rix, sel_usb, sel_aud, sel_vid2, sel_vid};
        cont_q <= req && !ack_out;
    end
    wire   preq      = req && cont_q;
    wire   vid_ack_g = vid_ack && preq && psel_q[0];
    wire   vid2_ack_g = vid2_ack && preq && psel_q[1];
    wire   aud_ack_g = aud_ack && preq && psel_q[2];
    wire   usb_ack_g = usb_ack && preq && psel_q[3];
    wire   rix_ack_g = rix_ack && preq && psel_q[4];
    assign ext_ack = ddr_ack_g | ddrb_ack_g | vid_ack_g | vid2_ack_g | usb_ack_g | aud_ack_g | rix_ack_g;
    assign ext_dat = ddr_ack_g ? ddr_dat_r : ddrb_ack_g ? ddrb_dat_r : vid_ack_g ? vid_dat_r : vid2_ack_g ? vid2_dat_r : aud_ack_g ? aud_dat_r : rix_ack_g ? rix_dat_r : usb_dat_r;
    // the bus timeout: 4,096 clocks without an acknowledge from an external slave; 1,024 clocks in the USB window
    // (20.5 us at 50 MHz: the host's slowest regular completion, an EHCI register access that the core does not
    // acknowledge, is 255 clk_plb cycles = 4.1 us plus the synchronisers).  A USB-window timeout reads
    // 0xDEAD_0000 and is counted, with its address, at board page +0x34 / +0x38; any other reads 0xFFFF_FFFF.
    wire   to_lim = sel_usb ? to_timer[10] : to_timer[12];
    always @(posedge clk) begin
        to_ack <= 1'b0;
        if (!(req && ext_sel) || ext_ack || to_ack) to_timer <= 13'd0;
        else if (to_lim) begin
            to_ack <= 1'b1; to_timer <= 13'd0; to_cnt <= to_cnt + 32'd1;
            if (sel_usb) begin usb_to_cnt <= usb_to_cnt + 32'd1; usb_to_addr <= {m_adr, 2'b00}; end
        end
        else to_timer <= to_timer + 13'd1;
    end

    // ---- block RAM ----------------------------------------------------------------------------------------
    soc_ram #(.AW(RAM_AW), .INIT_HEX(RAM_INIT_HEX)) u_ram (
        .clk(clk), .en(ram_en), .addr(m_adr[RAM_AW-1:0]),
        .wdata(m_mosi), .wstrb(m_we ? m_sel : 4'd0), .rdata(ram_q));

    // ---- fast RAM: port A shared by the CPU and the MAC (the MAC wins and a colliding CPU access is held) ----
    assign mac_gnt   = mac_req;
    // A CPU write is acknowledged in the cycle it is made.  A CPU read takes two cycles: port A (falling edge)
    // reads in the first, fast_q_r holds the word from the rising edge that ends it, and the second cycle
    // acknowledges with fast_q_r, so the read data reaches the CPU from a register and not through the RAM's
    // half-cycle output path.  The port is not enabled for the CPU in the acknowledge cycle.
    reg         fast_rd_ack = 1'b0;
    reg  [31:0] fast_q_r = 32'd0;
    assign fast_hold = fast_en && mac_req;
    assign fa_en     = mac_req ? 1'b1 : (fast_en && !fast_rd_ack);
    always @(posedge clk) begin
        fast_rd_ack <= fast_en && !m_we && !fast_hold && !fast_rd_ack;
        fast_q_r    <= fast_q;
    end
    assign fa_addr   = mac_req ? {1'b1, mac_addr} : {d_adr[11], d_adr[9:0]};
    assign fa_wdata  = mac_req ? mac_wdata : m_mosi;
    assign fa_wstrb  = mac_req ? mac_wstrb : (m_we ? m_sel : 4'd0);
    soc_fastram #(.AW(11)) u_fast (
        .clk(clk), .en(fa_en), .addr(fa_addr), .wdata(fa_wdata), .wstrb(fa_wstrb), .rdata(fast_q),
        .clkb(clk), .enb(1'b0), .addrb(11'd0), .wdatab(32'd0), .wstrbb(4'd0), .rdatab());

    // ---- console, timer, MAC, SPI flash, recovery -----------------------------------------------------------
    g2_uart #(.DIV(UART_DIV)) u_uart (
        .clk(clk), .rst(cpu_rst),
        .reg_en(con_en), .reg_we(m_we), .reg_adr(m_adr[1:0]), .reg_dat_w(m_mosi[7:0]), .reg_dat_r(con_dat_r),
        .txd(uart_txd), .rxd(uart_rxd));

    soc_timer u_timer (
        .clk(clk), .rst(cpu_rst),
        .reg_en(tmr_en), .reg_we(m_we), .reg_adr(m_adr[2:0]), .reg_sel(m_sel),
        .reg_dat_w(m_mosi), .reg_dat_r(tmr_dat_r), .irq(tmr_irq));

    g2_gmac #(.RX_OS(ETH_RX_OS)) u_eth (
        .clk(clk), .rst(cpu_rst),
        .reg_en(eth_en), .reg_we(m_we), .reg_adr(m_adr[3:0]), .reg_dat_w(m_mosi), .reg_dat_r(eth_dat_r), .irq(eth_irq),
        .pa_req(mac_req), .pa_addr(mac_addr), .pa_wdata(mac_wdata), .pa_wstrb(mac_wstrb), .pa_gnt(mac_gnt), .pa_rdata(fast_q),
        .eth_rxc(eth_rxc), .eth_rxc_pad(eth_rxc_pad), .eth_rxdv(eth_rxdv), .eth_rxer(eth_rxer), .eth_rxd(eth_rxd),
        .clk_tx(clk_tx), .mii_txc(mii_txc), .eth_txen(eth_txen), .eth_txer(eth_txer), .eth_txd(eth_txd), .gtx_on(gtx_on),
        .ring_en(ring_en), .ring_adr(m_adr[11:0]), .ring_q(ring_q),
        .rx_act(eth_rx_act), .eth_link(eth_link));

    g2_spi u_spi (
        .clk(clk), .rst(rst),
        .reg_en(spi_en), .reg_we(m_we), .reg_adr(m_adr[2:0]), .reg_dat_w(m_mosi), .reg_dat_r(spi_dat_r),
        .bb_claim(bb_claim), .bb_cs_n(bb_cs_n), .bb_sck(bb_sck), .bb_mosi(bb_mosi),
        .miso_level(spi_miso_lvl), .cs_active(spi_cs_act),
        .spi_cs_n(spi_cs_n), .spi_sck(spi_sck), .spi_mosi(spi_mosi), .spi_miso(spi_miso));

    g2_recovery #(.HOLD_LOG2(HOLD_LOG2), .DEB_LOG2(DEB_LOG2), .WDT_DEFAULT(WDT_DEFAULT)) u_rec (
        .clk(clk), .rst(rst),
        .reg_en(rec_en), .reg_we(m_we), .reg_adr(m_adr[2:0]), .reg_dat_w(m_mosi), .reg_dat_r(rec_dat_r),
        .button_n(button_n), .cpu_hold(cpu_hold), .button(btn),
        .icap_req_tog(icap_req_tog), .icap_addr(icap_addr), .icap_done_tog(icap_done_tog));

    g2_icap_iprog u_iprog (
        .clk(clk_icap), .req_tog(icap_req_tog), .addr(icap_addr), .done_tog(icap_done_tog));

    // ---- bus completion, the board page, the bit-bang levels (one clocked block) ----------------------------
    always @(posedge clk) begin
        ram_ack <= ram_en;
        ring_ack <= ring_en;
        ddr_st_clear <= 1'b0;
        per_ack <= per_en;
        bad_ack <= bad_en;
        if (per_en) begin
            case (per_idx)
            4'd1:  per_q <= con_dat_r;
            4'd2:  per_q <= tmr_dat_r;
            4'd11: per_q <= eth_dat_r;
            4'd12: per_q <= spi_dat_r;
            4'd13: per_q <= rec_dat_r;
            4'd5: begin
                case (m_adr[4:0])
                5'd0:  per_q <= 32'd0;
                5'd1:  per_q <= {24'd0, ddr_status};
                5'd2:  per_q <= {4'd0, cal_status};
                5'd3:  per_q <= ddr_x_passes;
                5'd5:  per_q <= ddr_x_errs;
                5'd4:  per_q <= BUILD_ID;
                5'd7:  per_q <= {27'd0, btn, 1'b0, eth_link, mdio_i, spi_miso_lvl};
                5'd8:  per_q <= bad_cnt;
                5'd9:  per_q <= bad_addr;
                5'd10: per_q <= to_cnt;
                5'd11: per_q <= clk_status;
                5'd12: per_q <= 32'h4732_0001;                    // board identifier: "G2", fabric revision 1
                5'd13: per_q <= usb_to_cnt;
                5'd14: per_q <= usb_to_addr;
                5'd16: per_q <= {21'd0, pin_s, 1'b0, pin_o, 1'b0, pin_oe};
                default: per_q <= 32'd0;
                endcase
                // +0x00 write: {[31:28] op, operands}
                if (m_we && m_adr[4:0] == 5'd16) begin pin_oe <= m_mosi[2:0]; pin_o <= m_mosi[6:4]; end
                if (m_we && m_adr[4:0] == 5'd0) begin
                    case (m_mosi[31:28])
                    4'd1: cal_status <= m_mosi[27:0];
                    4'd4: begin led_ovr <= m_mosi[8]; led_val <= {m_mosi[2], m_mosi[1], m_mosi[0]}; end
                    4'd5: begin bb_claim <= m_mosi[8]; bb_cs_n <= m_mosi[2]; bb_sck <= m_mosi[1]; bb_mosi <= m_mosi[0]; end
                    4'd6: begin mdio_o_r <= m_mosi[0]; mdio_oe_r <= m_mosi[1]; mdc_r <= m_mosi[2]; end
                    4'd7: begin ddr_st_clear <= 1'b1; ddr_x_en <= m_mosi[1:0]; end
                    default: ;
                    endcase
                end
            end
            default: per_q <= 32'd0;
            endcase
        end
        if (bad_en || ddr_err_g || ddrb_err_g) begin
            err_sticky <= 1'b1; bad_cnt <= bad_cnt + 32'd1; bad_addr <= {m_adr, 2'b00};
        end
        if (rst) begin
            bb_claim <= 1'b0; bb_cs_n <= 1'b1; bb_sck <= 1'b0; bb_mosi <= 1'b0;
            mdio_oe_r <= 1'b0; mdc_r <= 1'b0; led_ovr <= 1'b0; err_sticky <= 1'b0;
        end
        // activity: flash selected or a received frame (RX_DV from the MAC's input register), stretched to 2^20 clocks
        if (spi_cs_act || eth_rx_act) act_str <= 20'hFFFFF;
        else if (act_str != 20'd0) act_str <= act_str - 20'd1;
    end
    assign activity = (act_str != 20'd0);
    reg  [2:0] pin_m = 3'd0, pin_s = 3'd0;
    always @(posedge clk) begin pin_m <= pin_i; pin_s <= pin_m; end
    assign bus_err  = err_sticky;
    assign cpu_trap = err_sticky;

    assign ack_out = ram_ack | per_ack | bad_ack | ring_ack | ext_ack | to_ack | (fast_en && m_we && !fast_hold) | fast_rd_ack;
    assign s_miso  = per_ack ? per_q : ext_ack ? ext_dat : to_ack ? (sel_usb ? 32'hDEAD_0000 : 32'hFFFF_FFFF) : bad_ack ? 32'd0 :
                     ring_ack ? ring_q : fast_rd_ack ? fast_q_r : ram_q;

    // ---- the CPU (module VexRiscv: rtl/vexiiriscv/VexRiscv_PanoG2.v, VexiiRiscv): reset vector 0x0000_0000 -----
    // externalInterruptArray: [2] MAC (as on the G1), [3] USB, [4] video, [5] DDR, [6] RasterIX.  timerInterrupt: the timer, a level.
    VexRiscv u_cpu (
        .externalResetVector   (32'h0000_0000),
        .timerInterrupt        (tmr_irq),
        .softwareInterrupt     (1'b0),
        .externalInterruptArray({25'd0, rix_irq, ddr_irq, vid_irq | vid2_irq, usb_irq, eth_irq, 2'b00}),
        .iBusWishbone_CYC(i_cyc), .iBusWishbone_STB(i_stb), .iBusWishbone_ACK((gst == G_I) && ack_out),
        .iBusWishbone_WE(i_we),   .iBusWishbone_ADR(i_adr),
        .iBusWishbone_DAT_MISO(s_miso), .iBusWishbone_DAT_MOSI(i_mosi),
        .iBusWishbone_SEL(i_sel), .iBusWishbone_ERR(1'b0),
        .iBusWishbone_CTI(i_cti), .iBusWishbone_BTE(i_bte),
        .dBusWishbone_CYC(d_cyc), .dBusWishbone_STB(d_stb), .dBusWishbone_ACK((gst == G_D) && ack_out),
        .dBusWishbone_WE(d_we),   .dBusWishbone_ADR(d_adr),
        .dBusWishbone_DAT_MISO(s_miso), .dBusWishbone_DAT_MOSI(d_mosi),
        .dBusWishbone_SEL(d_sel), .dBusWishbone_ERR(1'b0),
        .dBusWishbone_CTI(d_cti), .dBusWishbone_BTE(d_bte),
        .clk(clk), .reset(cpu_rst));

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, i_we, i_mosi, i_sel, 1'b0};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule

`default_nettype wire
