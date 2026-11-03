/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// pano_g2_top.v -- Pano Logic G2 revision C (XC6SLX100-FGG484) top level, stage 1: the SoC in block RAM.
//
// CLOCKING
//   Y13 carries CLK125 of the 88E1119R PHY: 25 MHz while GMII_RST_N (R11) is low, 125 MHz once it is high
//  .  R11 is driven high from configuration onward.  g2_clk_settle.v holds the PLL in reset
//   until Y13 has measured 125 MHz against CFGMCLK (STARTUP_SPARTAN6) for 16 consecutive windows.
//     Y13 -> IBUFG -> g2_mcb_clk (one PLL_ADV, VCO 1000 MHz)
//       sysclk_2x / _180  250 MHz through BUFPLL_MCB   MCB1 (DDR2 at 125 MHz)
//       clk_sys           50.0 MHz  BUFG               the SoC and every MCB user port
//       mcb_drp_clk       62.5 MHz  BUFGCE             MCB calibration; also the USB host's PLB clock
//       clk_cpu           75.0 MHz  BUFG               the CPU domain: SoC, block RAM, mcb_wb on P0 of both MCBs
//     Y13 -> IBUFG -> BUFG (y13_clk): the settle counters, and the USB host's 24 MHz PLL input
//       clk_icap          10.0 MHz  BUFG               ICAP_SPARTAN6 (trce minimum period at -2: 83.333 ns)
//     Y13 -> IBUFG -> BUFG (y13_clk): the settle counters
//   Ethernet (rtl/g2/g2_gmac.v): RX_CLK (AB11; 125, 25 or 2.5 MHz by link speed) through IBUFG and BUFG clocks the
//   receive side; RXD[7:0], RX_DV and RX_ER pass a fixed IODELAY2 (RX_IDELAY taps) into IOB registers.  The transmit
//   side runs on y13_clk (125 MHz); GTX_CLK is an ODDR2 of y13_clk that rises mid-way between two data launches; the
//   MII TX_CLK (Y11) is an ordinary input sampled in y13_clk (no BUFG).  ULPI_CLK (C12, 60 MHz) is buffered inside the
//   USB host netlist.
// RESET
//   clk_sys is held in reset until the PLL has locked, and for 16 clocks after.  The CPU is further held for the
//   recovery block's button window (g2_recovery.v, 2^HOLD_LOG2 clocks).
// LEDS (active high)
//   red    lit while the PLL is not locked (waiting for the PHY clock); after lock, lit on a bus error
//   green  heartbeat from clk_sys, 0.75 Hz
//   blue   SPI flash selected or a received frame (stretched), or the button pressed
//   The board page (op 4) can take all three.
`timescale 1ns / 1ps
`default_nettype none

module pano_g2_top #(
    parameter RAM_INIT_HEX = "g2fw_",
    parameter HOLD_LOG2    = 22,
    parameter DEB_LOG2     = 16
) (
    input  wire        osc_clk,            // Y13, PHY CLK125
    output wire        gmii_rst_n,         // R11

    output wire        led_red,            // E12
    output wire        led_blue,           // H13
    output wire        led_green,          // F13
    input  wire        pano_button,        // H12, active low

    output wire        spi_cs_n,           // T5  CSO_B
    output wire        spi_sck,            // Y21 CCLK
    output wire        spi_mosi,           // AB20
    input  wire        spi_miso,           // AA20

    output wire        hdmi_ddc_sda,       // AB19, console UART TX
    input  wire        hdmi_ddc_scl,       // AA21, console UART RX

    output wire        gmii_mdc,
    inout  wire        gmii_mdio,
    output wire        gmii_gtx_clk,
    input  wire        gmii_tx_clk,
    output wire        gmii_tx_en,
    output wire        gmii_tx_er,
    output wire [7:0]  gmii_tx_d,
    input  wire        gmii_rx_clk,
    input  wire        gmii_rx_dv,
    input  wire        gmii_rx_er,
    input  wire [7:0]  gmii_rx_d,
    input  wire        gmii_int_n,         // AA4

    // video: CH7301C #1 on the DVI-I connector and #2 on the micro-HDMI connector (rtl/vga/g2_video.v); their shared
    // I2C bus E8/D9 is the USB wrapper's Chrontel bus
    output wire        dvi_xclk_p,
    output wire        dvi_xclk_n,
    output wire [11:0] dvi_d,
    output wire        dvi_h,
    output wire        dvi_v,
    output wire        dvi_de,
    output wire        dvi_reset_n,        // C15
    input  wire        dvi_hpint_n,        // D13
    output wire        hdmi_xclk_p,        // no XCLK_N on this encoder
    output wire [11:0] hdmi_d,
    output wire        hdmi_h,
    output wire        hdmi_v,
    output wire        hdmi_de,
    output wire        hdmi_reset_n,       // W18
    input  wire        hdmi_hpint_n,       // AB18
    // ---- WM8750BL codec, I2S playback (rtl/g2/g2_audio.v); its control I2C is i2c_a_* below ----
    output wire        codec_mclk,         // W14, 24.000 MHz (the USB reference clock)
    output wire        codec_bclk,         // AB13
    output wire        codec_daclrc,       // U6
    output wire        codec_dacdat,       // Y14

    // DDR2 chip A on MCB1
    output wire [12:0] mcb1_dram_a,
    output wire [1:0]  mcb1_dram_ba,
    output wire        mcb1_dram_ras_n,
    output wire        mcb1_dram_cas_n,
    output wire        mcb1_dram_we_n,
    output wire        mcb1_dram_cke,
    output wire        mcb1_dram_odt,
    output wire        mcb1_dram_ck,
    output wire        mcb1_dram_ck_n,
    inout  wire [15:0] mcb1_dram_dq,
    inout  wire        mcb1_dram_dqs,
    inout  wire        mcb1_dram_dqs_n,
    inout  wire        mcb1_dram_udqs,
    inout  wire        mcb1_dram_udqs_n,
    output wire        mcb1_dram_dm,
    output wire        mcb1_dram_udm,
    inout  wire        mcb1_rzq,
    // DDR2 chip B (MCB3, bank 3): the CPU window (P0), the RasterIX memory master (P1), the video heads (P2, P4)
    output wire [12:0] mcb3_dram_a,
    output wire [1:0]  mcb3_dram_ba,
    output wire        mcb3_dram_ras_n,
    output wire        mcb3_dram_cas_n,
    output wire        mcb3_dram_we_n,
    output wire        mcb3_dram_cke,
    output wire        mcb3_dram_odt,
    output wire        mcb3_dram_ck,
    output wire        mcb3_dram_ck_n,
    inout  wire [15:0] mcb3_dram_dq,
    inout  wire        mcb3_dram_dqs,
    inout  wire        mcb3_dram_dqs_n,
    inout  wire        mcb3_dram_udqs,
    inout  wire        mcb3_dram_udqs_n,
    output wire        mcb3_dram_dm,
    output wire        mcb3_dram_udm,
    inout  wire        mcb3_rzq,

    // USB: USB3300 ULPI PHY and USB2514 hub
    input  wire        ulpi_clk,           // C12
    input  wire        ulpi_dir,
    input  wire        ulpi_nxt,
    inout  wire        ulpi_stp,
    inout  wire [7:0]  ulpi_data,
    output wire        ulpi_reset,
    output wire        usb_clk,            // W12, 24 MHz
    output wire        usb_hub_reset_n,    // W11
    inout  wire        brd_reset_out_n,    // AA6
    inout  wire        brd_power_sleep,    // C10
    inout  wire        brd_sleep_req,      // C13
    inout  wire        i2c_v_scl,          // E8
    inout  wire        i2c_v_sda,          // D9
    inout  wire        i2c_a_scl,          // U17
    inout  wire        i2c_a_sda           // AB17
);
    // ---- declarations ---------------------------------------------------------------------------------------
    wire        y13_ibufg, y13_clk;
    wire        cfgmclk_raw, cfg_clk;
    wire        pll_rst, pll_locked;
    wire        sysclk_2x, sysclk_2x_180, pll_ce_0, pll_ce_90, pll_lock, mcb_drp_clk, async_rst;
    wire        clk_sys, clk_cpu, clk_icap, usb_clk125;
    wire        clk_sys_pll;                              // CLKOUT2 before the clk_sys BUFG (the slot's clock gate's input)
    wire        clk_cpu_66;                               // CLKOUT4 through its BUFG, 66.667 MHz
`ifdef G2_SNES
    // The subsystem-slot fabric runs the CPU on clk_sys (50 MHz): one domain for the SoC, its bridges and the subsystem block; CLKOUT4
    // and its BUFG are unused and removed.
    assign clk_cpu = clk_sys;
    wire _unused_cpu66 = clk_cpu_66;
`else
    assign clk_cpu = clk_cpu_66;
`endif
    wire        erxc_ibufg, eth_rxc, gtx_on;
    wire [9:0]  erx_pad, erx_dly;                        // {RX_ER, RX_DV, RXD[7:0]} at the pads and after the IODELAY2
    wire [7:0]  settle_count;
    wire [4:0]  settle_wins;
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [3:0] lock_s = 4'b0000;
    reg  [4:0]  rst_cnt = 5'd0;
    reg         sys_rst = 1'b1;
    reg  [25:0] beat = 26'd0;
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] lock_cfg = 2'b00;
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] err_cfg = 2'b00;
    reg         red_q = 1'b1;
    wire        mdc, mdio_o, mdio_oe, mdio_i;
    wire        led_ovr; wire [2:0] led_val;
    wire        activity, bus_err, cpu_trap;
    wire        vid_cyc, vid_we, vid_ack, vid_irq, vid2_cyc, vid2_we, vid2_ack, vid2_irq;
    wire        clk24_aud, pll24_locked_aud, aud_irq;
    wire        aud_cyc, aud_we, aud_ack; wire [9:0] aud_adr; wire [3:0] aud_sel; wire [31:0] aud_dat_w, aud_dat_r;
    // RasterIX page 0x8000_E000 (rtl/g2/g2_rix.v): display lists from chip A port P2, memory on chip B port P1
    wire        rix_cyc, rix_we, rix_ack, rix_irq; wire [9:0] rix_adr; wire [3:0] rix_sel; wire [31:0] rix_dat_w, rix_dat_r;
    wire        dl_cmd_en, dl_cmd_full, dl_rd_en, dl_rd_empty; wire [2:0] dl_cmd_instr; wire [5:0] dl_cmd_bl;
    wire [29:0] dl_cmd_addr; wire [31:0] dl_rd_data; wire [3:0] q3_wr_mask;
    wire [9:0]  vid_adr, vid2_adr; wire [3:0] vid_sel, vid2_sel; wire [31:0] vid_dat_w, vid_dat_r, vid2_dat_w, vid2_dat_r;
    wire        ddrb_cyc, ddrb_stb, ddrb_we, ddrb_ack, ddrb_err; wire [23:0] ddrb_adr; wire [3:0] ddrb_sel;
    wire [31:0] ddrb_dat_w, ddrb_dat_r; wire [2:0] ddrb_cti; wire [1:0] ddrb_bte;
    wire        ch_scl_low, ch_sda_low, ch_scl_in, ch_sda_in;
    wire        usb_cyc, usb_we, usb_ack, usb_irq; wire [21:0] usb_adr; wire [3:0] usb_sel; wire [31:0] usb_dat_w, usb_dat_r;
    wire        ddr_cyc, ddr_stb, ddr_we, ddr_ack, ddr_err, ddr_ready, ddr_st_clear; wire [23:0] ddr_adr;
    // chip A port P1 for the Amiga fabrics' fast-RAM cache (g2_ddr_port G2_DDRA_P1_EXT); idle in every other fabric
    wire        fp_cmd_en, fp_cmd_full, fp_wr_en, fp_wr_empty, fp_rd_en, fp_rd_empty;
    wire [2:0]  fp_cmd_instr; wire [5:0] fp_cmd_bl; wire [29:0] fp_cmd_byte_addr; wire [3:0] fp_wr_mask;
    wire [31:0] fp_wr_data, fp_rd_data;
`ifndef G2_AMIGA
    assign fp_cmd_en = 1'b0; assign fp_cmd_instr = 3'd0; assign fp_cmd_bl = 6'd0; assign fp_cmd_byte_addr = 30'd0;
    assign fp_wr_en = 1'b0; assign fp_wr_mask = 4'd0; assign fp_wr_data = 32'd0; assign fp_rd_en = 1'b0;
`endif
    wire [3:0]  ddr_sel; wire [31:0] ddr_dat_w, ddr_dat_r; wire [2:0] ddr_cti; wire [1:0] ddr_bte; wire [7:0] ddr_status;
    reg  [1:0]  btn_s = 2'b00;

    // ---- the PHY out of reset -----------------------------------------------------------------------------------
    assign gmii_rst_n   = 1'b1;

    // ---- clocks ---------------------------------------------------------------------------------------------
    IBUFG u_y13_ibufg (.I(osc_clk), .O(y13_ibufg));
    BUFG  u_y13_bufg  (.I(y13_ibufg), .O(y13_clk));

    STARTUP_SPARTAN6 u_startup (
        .CFGCLK(), .CFGMCLK(cfgmclk_raw), .EOS(),
        .CLK(1'b0), .GSR(1'b0), .GTS(1'b0), .KEYCLEARB(1'b1));
    BUFG u_cfg_bufg (.I(cfgmclk_raw), .O(cfg_clk));

    g2_clk_settle u_settle (
        .ref_clk(cfg_clk), .y13_clk(y13_clk),
        .pll_rst(pll_rst), .last_count(settle_count), .good_wins(settle_wins));

    // DRAM CK period in ps: 8000 (125 MHz, clk_sys 50.000 MHz) or 3200 (312.5 MHz, clk_sys 52.083 MHz); XST define G2_MEMCLK_3200.
`ifdef G2_MEMCLK_3200
    localparam integer MEMCLK = 3200, UART_DIV = 339;    // 39.0625 MHz (MIG clk0) / 339 = 115,228 baud
`else
`ifdef G2_SNES
    localparam integer MEMCLK = 8000, UART_DIV = 434;    // the subsystem-slot fabric's CPU on clk_sys 50 MHz / 434 = 115,207 baud (+0.01 %)
`else
    localparam integer MEMCLK = 8000, UART_DIV = 579;    // clk_cpu 66.667 MHz / 579 = 115,141 baud (-0.05 %)
`endif
    localparam [31:0]  WDT_DEFAULT = 32'd2_000_000_000;  // 30.0 s at 66.667 MHz
`endif
`ifdef G2_MEMCLK_3200
    g2_mcb_clk_mig u_clk (
        .clkin(y13_ibufg), .rst(pll_rst),
        .sysclk_2x(sysclk_2x), .sysclk_2x_180(sysclk_2x_180), .pll_ce_0(pll_ce_0), .pll_ce_90(pll_ce_90),
        .pll_lock(pll_lock), .mcb_drp_clk(mcb_drp_clk), .async_rst(async_rst),
        .clk_sys(clk_sys), .clk_125(usb_clk125), .pll_locked(pll_locked), .clk_icap(clk_icap));
    assign clk_cpu = clk_sys;
    assign clk_sys_pll = 1'b0;                            // no gated clk_sys in this variant (G2_SNES needs MEMCLK 8000)                             // the 3200 ps variant: one domain (not maintained)
`else
    wire        sysclk_2x_b, sysclk_2x_180_b, pll_ce_0_b, pll_ce_90_b, pll_lock_b;
    g2_mcb_clk #(.MEMCLK_PERIOD(MEMCLK), .DUAL(1)) u_clk (
        .clkin(y13_ibufg), .rst(pll_rst),
        .sysclk_2x(sysclk_2x), .sysclk_2x_180(sysclk_2x_180), .pll_ce_0(pll_ce_0), .pll_ce_90(pll_ce_90),
        .pll_lock(pll_lock), .mcb_drp_clk(mcb_drp_clk), .async_rst(async_rst),
        .clk_sys(clk_sys), .clk_sys_pll_o(clk_sys_pll), .clk_cpu(clk_cpu_66), .pll_locked(pll_locked), .clk_icap(clk_icap),
        .sysclk_2x_b(sysclk_2x_b), .sysclk_2x_180_b(sysclk_2x_180_b), .pll_ce_0_b(pll_ce_0_b), .pll_ce_90_b(pll_ce_90_b), .pll_lock_b(pll_lock_b));
    assign usb_clk125 = y13_clk;                           // the USB host's 24 MHz PLL runs from Y13 itself
`endif

`ifdef G2_AMIGA
    // The Amiga fabric gives the receive clock's global buffer to the Amiga clock gate (amiga_g2.v BUFGCE) as the subsystem-slot
    // fabric does; the MAC samples RX_CLK as data in y13_clk instead (g2_gmac.v RX_OS, MII at 10 and 100 Mb/s only).
    localparam integer ETH_RX_OS = 1;
    IBUF  u_erxc_ibuf  (.I(gmii_rx_clk), .O(erxc_ibufg));
    assign eth_rxc = y13_clk;
`elsif G2_SNES
    localparam integer ETH_RX_OS = 0;
    IBUFG u_erxc_ibufg (.I(gmii_rx_clk), .O(erxc_ibufg));
    // The subsystem-slot fabric gives the receive clock's global buffer to the slot's clock gate (the slot's BUFGCE, which takes a
    // BUFGMUX pair); the MAC's receive side is not clocked there and the network is not used.
    assign eth_rxc = 1'b0;
    wire _unused_erxc = erxc_ibufg;
`else
    localparam integer ETH_RX_OS = 0;
    IBUFG u_erxc_ibufg (.I(gmii_rx_clk), .O(erxc_ibufg));
    BUFG  u_erxc_bufg  (.I(erxc_ibufg), .O(eth_rxc));
`endif

    // ---- GMII receive input delay ----------------------------------------------------------------------------
    // RX_IDELAY taps of fixed delay on every receive data line, so that the IOB register on the BUFG'd RX_CLK samples
    // inside the 88E1119R's window: the constraint of ise/pano_g2.ucf (valid 2.5 ns before to 0.5 ns after the RX_CLK
    // rising edge at the pad) is met with 0 taps by neither edge and with 40 taps.
    // The XC6SLX150 (G2_REVB) delivers RX_CLK to the IOB registers later than the XC6SLX100: with 40 taps its build
    // reported setup +0.558 ns and hold -0.186 ns, so 10 more taps (about 0.036 ns each at the window centre, from the
    // 32/48-tap points of that table) centre the window again.
`ifdef G2_REVB
    localparam integer RX_IDELAY = 50;
`else
    localparam integer RX_IDELAY = 40;
`endif
    assign erx_pad = {gmii_rx_er, gmii_rx_dv, gmii_rx_d};
    genvar gi;
    generate for (gi = 0; gi < 10; gi = gi + 1) begin : g_rxdly
        IODELAY2 #(.IDELAY_TYPE("FIXED"), .IDELAY_VALUE(RX_IDELAY), .DELAY_SRC("IDATAIN"), .DATA_RATE("SDR"),
                   .IDELAY_MODE("NORMAL"), .SIM_TAPDELAY_VALUE(75)) u_dly (
            .IDATAIN(erx_pad[gi]), .DATAOUT(erx_dly[gi]), .T(1'b1), .ODATAIN(1'b0), .CAL(1'b0), .CE(1'b0), .INC(1'b0),
            .CLK(1'b0), .IOCLK0(1'b0), .IOCLK1(1'b0), .RST(1'b0), .BUSY(), .DATAOUT2(), .DOUT(), .TOUT());
    end endgenerate

    // ---- GTX_CLK: 125 MHz from y13_clk, low for the first half of each data period (the data launches on the rising
    // edge of y13_clk, GTX_CLK rises on its falling edge: 4 ns of setup and of hold at the PHY); held low in MII
    ODDR2 #(.DDR_ALIGNMENT("NONE"), .INIT(1'b0), .SRTYPE("SYNC")) u_gtx (
        .Q(gmii_gtx_clk), .C0(y13_clk), .C1(~y13_clk), .CE(1'b1), .D0(1'b0), .D1(gtx_on), .R(1'b0), .S(1'b0));

    // ---- system reset: PLL locked, then 16 clocks ---------------------------------------------------------
    always @(posedge clk_sys) begin
        lock_s <= {lock_s[2:0], pll_locked};
        if (!lock_s[3]) begin rst_cnt <= 5'd0; sys_rst <= 1'b1; end
        else if (!rst_cnt[4]) rst_cnt <= rst_cnt + 5'd1;
        else sys_rst <= 1'b0;
        beat <= beat + 26'd1;
        btn_s <= {btn_s[0], !pano_button};
    end

    // ---- LEDs -----------------------------------------------------------------------------------------------
    // red is produced in the CFGMCLK domain, which runs before the PLL locks
    always @(posedge cfg_clk) begin
        lock_cfg <= {lock_cfg[0], pll_locked};
        err_cfg  <= {err_cfg[0], bus_err};
        red_q    <= !lock_cfg[1] || err_cfg[1];
    end
    assign led_red   = led_ovr ? led_val[2] : red_q;
    assign led_blue  = led_ovr ? led_val[1] : (activity || btn_s[1]);
    assign led_green = led_ovr ? led_val[0] : beat[25];

    // ---- MDIO -----------------------------------------------------------------------------------------------
    IOBUF u_mdio (.I(mdio_o), .T(!mdio_oe), .O(mdio_i), .IO(gmii_mdio));
    assign gmii_mdc = mdc;

    // ---- DDR2 chip A (MCB1): g2_ddr_mcb + mcb_wb -------------------------------------------------------------
    g2_ddr_port #(.MEMCLK_PERIOD(MEMCLK)) u_ddr (
        .clk(clk_sys), .rst(sys_rst), .clk_cpu(clk_cpu), .rst_cpu(rst_cpu),
        .sysclk_2x(sysclk_2x), .sysclk_2x_180(sysclk_2x_180), .pll_ce_0(pll_ce_0), .pll_ce_90(pll_ce_90),
        .pll_lock(pll_lock), .mcb_drp_clk(mcb_drp_clk), .async_rst(async_rst),
        .wb_cyc(ddr_cyc), .wb_stb(ddr_stb), .wb_we(ddr_we), .wb_adr(ddr_adr), .wb_sel(ddr_sel), .wb_dat_w(ddr_dat_w),
        .wb_cti(ddr_cti), .wb_bte(ddr_bte), .wb_dat_r(ddr_dat_r), .wb_ack(ddr_ack), .wb_err(ddr_err),
        .calib_done(ddr_ready), .status(ddr_status), .st_clear(ddr_st_clear),
        .x_en(ddr_x_en[0]), .x_passes(x_passes_a), .x_errs(x_errs_a),
        .dl_cmd_en(dl_cmd_en), .dl_cmd_instr(dl_cmd_instr), .dl_cmd_bl(dl_cmd_bl), .dl_cmd_byte_addr(dl_cmd_addr),
        .dl_cmd_full(dl_cmd_full), .dl_rd_en(dl_rd_en), .dl_rd_data(dl_rd_data), .dl_rd_empty(dl_rd_empty),
        .fp_cmd_en(fp_cmd_en), .fp_cmd_instr(fp_cmd_instr), .fp_cmd_bl(fp_cmd_bl), .fp_cmd_byte_addr(fp_cmd_byte_addr),
        .fp_cmd_full(fp_cmd_full), .fp_wr_en(fp_wr_en), .fp_wr_mask(fp_wr_mask), .fp_wr_data(fp_wr_data),
        .fp_wr_empty(fp_wr_empty), .fp_rd_en(fp_rd_en), .fp_rd_data(fp_rd_data), .fp_rd_empty(fp_rd_empty),
        .mcb1_dram_a(mcb1_dram_a), .mcb1_dram_ba(mcb1_dram_ba), .mcb1_dram_ras_n(mcb1_dram_ras_n),
        .mcb1_dram_cas_n(mcb1_dram_cas_n), .mcb1_dram_we_n(mcb1_dram_we_n), .mcb1_dram_cke(mcb1_dram_cke),
        .mcb1_dram_odt(mcb1_dram_odt), .mcb1_dram_ck(mcb1_dram_ck), .mcb1_dram_ck_n(mcb1_dram_ck_n),
        .mcb1_dram_dq(mcb1_dram_dq), .mcb1_dram_dqs(mcb1_dram_dqs), .mcb1_dram_dqs_n(mcb1_dram_dqs_n),
        .mcb1_dram_udqs(mcb1_dram_udqs), .mcb1_dram_udqs_n(mcb1_dram_udqs_n),
        .mcb1_dram_dm(mcb1_dram_dm), .mcb1_dram_udm(mcb1_dram_udm), .mcb1_rzq(mcb1_rzq));

    // ---- USB host: the module g2_usb_host (EHCI controller, USB3300 ULPI PHY, USB2514 hub, the codec's 24 MHz reference
    // and the Chrontel I2C lines) is not part of this source release.  In its place the window reads 0, the USB3300 is
    // held in reset (RESET high, STP high, data released), the hub in reset, and no 24 MHz reference is made
    reg usb_ack_q = 1'b0;
    always @(posedge clk_sys) usb_ack_q <= usb_cyc && !usb_ack_q;
    assign usb_ack = usb_ack_q;
    assign usb_dat_r = 32'd0;
    assign usb_irq = 1'b0;
    assign ulpi_stp = 1'b1;
    assign ulpi_data = 8'bzzzzzzzz;
    assign ulpi_reset = 1'b1;
    assign usb_clk = 1'b0;
    assign usb_hub_reset_n = 1'b0;
    assign i2c_v_scl = 1'bz; assign i2c_v_sda = 1'bz; assign i2c_a_scl = 1'bz; assign i2c_a_sda = 1'bz;
    assign ch_scl_in = 1'b1; assign ch_sda_in = 1'b1;
    wire _unused_ch = &{1'b0, ch_scl_low, ch_sda_low, 1'b0};
    wire _unused_usb = &{1'b0, ulpi_clk, ulpi_dir, ulpi_nxt, usb_we, usb_adr, usb_sel, usb_dat_w, usb_clk125, 1'b0};

    // ---- the SoC --------------------------------------------------------------------------------------------
    // ---- DDR2 chip B (MCB3): g2_ddr_mcb; P1 the RasterIX memory master (below); board status [3:1] read 0 ---------
    wire        calib3_async;
    // P0: the CPU's window into chip B (mcb_wb); P2 and P4: the two video heads' read ports (rtl/vga/vga_mcb_shim.v)
    wire        b0_cmd_en, b0_cmd_full, b0_cmd_empty, b0_wr_en, b0_wr_full, b0_wr_empty, b0_wr_underrun, b0_wr_error;
    wire        b0_rd_en, b0_rd_full, b0_rd_empty, b0_rd_overflow, b0_rd_error;
    wire [2:0]  b0_cmd_instr; wire [5:0] b0_cmd_bl; wire [29:0] b0_cmd_byte_addr; wire [3:0] b0_wr_mask;
    wire [31:0] b0_wr_data, b0_rd_data; wire [6:0] b0_wr_count, b0_rd_count;
    wire        v1_cmd_en, v1_cmd_full, v1_rd_en, v1_rd_empty, v1_rd_overflow, v1_rd_error;
    wire [2:0]  v1_cmd_instr; wire [5:0] v1_cmd_bl; wire [29:0] v1_cmd_byte_addr; wire [31:0] v1_rd_data;
    wire        v2_cmd_en, v2_cmd_full, v2_rd_en, v2_rd_empty, v2_rd_overflow, v2_rd_error;
    wire [2:0]  v2_cmd_instr; wire [5:0] v2_cmd_bl; wire [29:0] v2_cmd_byte_addr; wire [31:0] v2_rd_data;
    wire        q3_cmd_en, q3_cmd_full, q3_wr_en, q3_wr_full, q3_rd_en, q3_rd_empty, q3_rd_overflow, q3_wr_underrun;
    wire        q3_wr_empty;
    // chip B P3 (write): the frame writer in the G2_SNES fabric, idle otherwise
    wire        s3_cmd_en, s3_cmd_full, s3_wr_en, s3_wr_empty; wire [6:0] s3_wr_count; wire [2:0] s3_cmd_instr; wire [5:0] s3_cmd_bl;
    wire [29:0] s3_cmd_byte_addr; wire [3:0] s3_wr_mask; wire [31:0] s3_wr_data;
    wire [2:0]  q3_cmd_instr; wire [5:0] q3_cmd_bl; wire [29:0] q3_cmd_byte_addr; wire [31:0] q3_wr_data, q3_rd_data;
    wire        bad3, run3; wire [15:0] passes3, errs3;
    wire [1:0]  ddr_x_en; wire [15:0] x_passes_a, x_errs_a;
`ifdef G2_SIM_MCB_MODEL
    // simulation: the port model of rtl/g2/sim/mcb_port_model.v on P1 for the exerciser, as g2_ddr_port.v does for
    // chip A; the hard MCB is simulated only by the encrypted secureip model
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(6)) u_model3_p0 (
        .clk(clk_cpu), .rst(rst_cpu), .calib_done(),
        .cmd_en(b0_cmd_en), .cmd_instr(b0_cmd_instr), .cmd_bl(b0_cmd_bl), .cmd_byte_addr(b0_cmd_byte_addr),
        .cmd_full(b0_cmd_full), .cmd_empty(b0_cmd_empty),
        .wr_en(b0_wr_en), .wr_mask(b0_wr_mask), .wr_data(b0_wr_data), .wr_full(b0_wr_full), .wr_empty(b0_wr_empty),
        .wr_count(b0_wr_count), .wr_underrun(b0_wr_underrun), .wr_error(b0_wr_error),
        .rd_en(b0_rd_en), .rd_data(b0_rd_data), .rd_full(b0_rd_full), .rd_empty(b0_rd_empty), .rd_count(b0_rd_count),
        .rd_overflow(b0_rd_overflow), .rd_error(b0_rd_error));
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(7)) u_model3_p2 (
        .clk(vclk), .rst(sys_rst), .calib_done(),
        .cmd_en(v1_cmd_en), .cmd_instr(v1_cmd_instr), .cmd_bl(v1_cmd_bl), .cmd_byte_addr(v1_cmd_byte_addr),
        .cmd_full(v1_cmd_full), .cmd_empty(),
        .wr_en(1'b0), .wr_mask(4'b0), .wr_data(32'b0), .wr_full(), .wr_empty(), .wr_count(), .wr_underrun(), .wr_error(),
        .rd_en(v1_rd_en), .rd_data(v1_rd_data), .rd_full(), .rd_empty(v1_rd_empty), .rd_count(),
        .rd_overflow(v1_rd_overflow), .rd_error(v1_rd_error));
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(8)) u_model3_p4 (
        .clk(vclk), .rst(sys_rst), .calib_done(),
        .cmd_en(v2_cmd_en), .cmd_instr(v2_cmd_instr), .cmd_bl(v2_cmd_bl), .cmd_byte_addr(v2_cmd_byte_addr),
        .cmd_full(v2_cmd_full), .cmd_empty(),
        .wr_en(1'b0), .wr_mask(4'b0), .wr_data(32'b0), .wr_full(), .wr_empty(), .wr_count(), .wr_underrun(), .wr_error(),
        .rd_en(v2_rd_en), .rd_data(v2_rd_data), .rd_full(), .rd_empty(v2_rd_empty), .rd_count(),
        .rd_overflow(v2_rd_overflow), .rd_error(v2_rd_error));
    mcb_port_model #(.AW(24), .CAL_CYCLES(400), .LAT_MAX(12), .SEED(5)) u_model3 (
        .clk(clk_sys), .rst(sys_rst), .calib_done(calib3_async),
        .cmd_en(q3_cmd_en), .cmd_instr(q3_cmd_instr), .cmd_bl(q3_cmd_bl), .cmd_byte_addr(q3_cmd_byte_addr),
        .cmd_full(q3_cmd_full), .cmd_empty(),
        .wr_en(q3_wr_en), .wr_mask(q3_wr_mask), .wr_data(q3_wr_data), .wr_full(q3_wr_full), .wr_empty(q3_wr_empty),
        .wr_count(), .wr_underrun(q3_wr_underrun), .wr_error(),
        .rd_en(q3_rd_en), .rd_data(q3_rd_data), .rd_full(), .rd_empty(q3_rd_empty), .rd_count(),
        .rd_overflow(q3_rd_overflow), .rd_error());
    assign mcb3_dram_a = 13'd0; assign mcb3_dram_ba = 2'd0; assign mcb3_dram_ras_n = 1'b1; assign mcb3_dram_cas_n = 1'b1;
    assign mcb3_dram_we_n = 1'b1; assign mcb3_dram_cke = 1'b0; assign mcb3_dram_odt = 1'b0; assign mcb3_dram_ck = 1'b0;
    assign mcb3_dram_ck_n = 1'b1; assign mcb3_dram_dm = 1'b0; assign mcb3_dram_udm = 1'b0;
    wire _unused_sim3 = &{1'b0, sysclk_2x_b, sysclk_2x_180_b, pll_ce_0_b, pll_ce_90_b, pll_lock_b, 1'b0};
`else
    g2_ddr_mcb #(.MEMCLK_PERIOD(MEMCLK), .SIMULATION("FALSE")) u_mcb3 (
        .sysclk_2x(sysclk_2x_b), .sysclk_2x_180(sysclk_2x_180_b), .pll_ce_0(pll_ce_0_b), .pll_ce_90(pll_ce_90_b),
        .pll_lock(pll_lock_b), .mcb_drp_clk(mcb_drp_clk), .async_rst(async_rst), .calib_done(calib3_async),
        .mcbx_dram_a(mcb3_dram_a), .mcbx_dram_ba(mcb3_dram_ba), .mcbx_dram_ras_n(mcb3_dram_ras_n),
        .mcbx_dram_cas_n(mcb3_dram_cas_n), .mcbx_dram_we_n(mcb3_dram_we_n), .mcbx_dram_cke(mcb3_dram_cke),
        .mcbx_dram_odt(mcb3_dram_odt), .mcbx_dram_ck(mcb3_dram_ck), .mcbx_dram_ck_n(mcb3_dram_ck_n),
        .mcbx_dram_dq(mcb3_dram_dq), .mcbx_dram_dqs(mcb3_dram_dqs), .mcbx_dram_dqs_n(mcb3_dram_dqs_n),
        .mcbx_dram_udqs(mcb3_dram_udqs), .mcbx_dram_udqs_n(mcb3_dram_udqs_n),
        .mcbx_dram_dm(mcb3_dram_dm), .mcbx_dram_udm(mcb3_dram_udm), .mcbx_rzq(mcb3_rzq),
        .p0_clk(clk_cpu), .p0_cmd_en(b0_cmd_en), .p0_cmd_instr(b0_cmd_instr), .p0_cmd_bl(b0_cmd_bl), .p0_cmd_byte_addr(b0_cmd_byte_addr),
        .p0_cmd_full(b0_cmd_full), .p0_cmd_empty(b0_cmd_empty), .p0_wr_en(b0_wr_en), .p0_wr_mask(b0_wr_mask), .p0_wr_data(b0_wr_data), .p0_wr_full(b0_wr_full),
        .p0_wr_empty(b0_wr_empty), .p0_wr_count(b0_wr_count), .p0_wr_underrun(b0_wr_underrun), .p0_wr_error(b0_wr_error), .p0_rd_en(b0_rd_en), .p0_rd_data(b0_rd_data),
        .p0_rd_full(b0_rd_full), .p0_rd_empty(b0_rd_empty), .p0_rd_count(b0_rd_count), .p0_rd_overflow(b0_rd_overflow), .p0_rd_error(b0_rd_error),
        .p1_clk(clk_sys), .p1_cmd_en(q3_cmd_en), .p1_cmd_instr(q3_cmd_instr), .p1_cmd_bl(q3_cmd_bl),
        .p1_cmd_byte_addr(q3_cmd_byte_addr), .p1_cmd_full(q3_cmd_full), .p1_cmd_empty(),
        .p1_wr_en(q3_wr_en), .p1_wr_mask(q3_wr_mask), .p1_wr_data(q3_wr_data), .p1_wr_full(q3_wr_full),
        .p1_wr_empty(q3_wr_empty), .p1_wr_count(), .p1_wr_underrun(q3_wr_underrun), .p1_wr_error(),
        .p1_rd_en(q3_rd_en), .p1_rd_data(q3_rd_data), .p1_rd_full(), .p1_rd_empty(q3_rd_empty),
        .p1_rd_count(), .p1_rd_overflow(q3_rd_overflow), .p1_rd_error(),
        .p2_clk(vclk), .p2_cmd_en(v1_cmd_en), .p2_cmd_instr(v1_cmd_instr), .p2_cmd_bl(v1_cmd_bl), .p2_cmd_byte_addr(v1_cmd_byte_addr),
        .p2_cmd_full(v1_cmd_full), .p2_cmd_empty(), .p2_rd_en(v1_rd_en), .p2_rd_data(v1_rd_data), .p2_rd_full(), .p2_rd_empty(v1_rd_empty),
        .p2_rd_count(), .p2_rd_overflow(v1_rd_overflow), .p2_rd_error(v1_rd_error),
        .p3_clk(clk_sys), .p3_cmd_en(s3_cmd_en), .p3_cmd_instr(s3_cmd_instr), .p3_cmd_bl(s3_cmd_bl),
        .p3_cmd_byte_addr(s3_cmd_byte_addr), .p3_cmd_full(s3_cmd_full), .p3_cmd_empty(), .p3_wr_en(s3_wr_en),
        .p3_wr_mask(s3_wr_mask), .p3_wr_data(s3_wr_data), .p3_wr_full(),
        .p3_wr_empty(s3_wr_empty), .p3_wr_count(s3_wr_count), .p3_wr_underrun(), .p3_wr_error(),
        .p4_clk(vclk), .p4_cmd_en(v2_cmd_en), .p4_cmd_instr(v2_cmd_instr), .p4_cmd_bl(v2_cmd_bl), .p4_cmd_byte_addr(v2_cmd_byte_addr),
        .p4_cmd_full(v2_cmd_full), .p4_cmd_empty(), .p4_rd_en(v2_rd_en), .p4_rd_data(v2_rd_data), .p4_rd_full(), .p4_rd_empty(v2_rd_empty),
        .p4_rd_count(), .p4_rd_overflow(v2_rd_overflow), .p4_rd_error(v2_rd_error),
        .p5_clk(clk_sys), .p5_cmd_en(1'b0), .p5_cmd_instr(3'b0), .p5_cmd_bl(6'b0), .p5_cmd_byte_addr(30'b0),
        .p5_cmd_full(), .p5_cmd_empty(), .p5_wr_en(1'b0), .p5_wr_mask(4'b0), .p5_wr_data(32'b0), .p5_wr_full(),
        .p5_wr_empty(), .p5_wr_count(), .p5_wr_underrun(), .p5_wr_error());
`endif
    reg cal3_s0 = 1'b0, cal3_s1 = 1'b0;
    always @(posedge clk_sys) begin cal3_s0 <= calib3_async; cal3_s1 <= cal3_s0; end
    // the CPU's window into chip B: 0x1400_0000 cached, 0x9400_0000 uncached, through mcb_wb on P0
    mcb_wb #(.LINE_BYTES(64), .ADDR_W(24)) u_wbb (
        .clk(clk_cpu), .rst(rst_cpu),
        .wb_cyc(ddrb_cyc), .wb_stb(ddrb_stb), .wb_we(ddrb_we), .wb_adr({6'd0, ddrb_adr}), .wb_sel(ddrb_sel), .wb_dat_w(ddrb_dat_w),
        .wb_cti(ddrb_cti), .wb_bte(ddrb_bte), .wb_dat_r(ddrb_dat_r), .wb_ack(ddrb_ack), .wb_err(ddrb_err),
        .p_cmd_en(b0_cmd_en), .p_cmd_instr(b0_cmd_instr), .p_cmd_bl(b0_cmd_bl), .p_cmd_byte_addr(b0_cmd_byte_addr),
        .p_cmd_full(b0_cmd_full),
        .p_wr_en(b0_wr_en), .p_wr_mask(b0_wr_mask), .p_wr_data(b0_wr_data), .p_wr_full(b0_wr_full),
        .p_wr_count(b0_wr_count), .p_wr_underrun(b0_wr_underrun), .p_wr_error(b0_wr_error),
        .p_rd_en(b0_rd_en), .p_rd_data(b0_rd_data), .p_rd_empty(b0_rd_empty), .p_rd_overflow(b0_rd_overflow),
        .p_rd_error(b0_rd_error),
        .calib_done_async(calib3_async), .st_clear(1'b0), .calib_done(), .status());
    wire _unused_b0 = &{1'b0, b0_cmd_empty, b0_wr_empty, b0_rd_full, b0_rd_count, 1'b0};

    // ---- video: two heads on chip B (P2, P4), 16 MB each (two 8 MB buffers), clocks from the 125 MHz reference ------
    /* one output clock for both heads: the device has 16 global clock buffers and this fabric uses 14 (the MCB DRP
     * clock's BUFGCE is a BUFGMUX whose paired site must stay empty, ise/pano_g2.ucf), so head 2 runs on head 1's
     * oclk and both heads must be set to the same mode (fw/bootsh.c cmd_vid refuses another) */
    wire vclk, vclk_locked, oclk1, oclk1_locked;
    wire p1_progen, p1_progdata, p1_progdone;
    g2_video_clk u_vclk (
        .clk125(usb_clk125), .rst(sys_rst), .progclk(clk_sys), .progen(p1_progen), .progdata(p1_progdata), .progdone(p1_progdone),
        .vclk(vclk), .vclk_locked(vclk_locked), .oclk(oclk1), .oclk_locked(oclk1_locked));
    g2_video #(.BYTE_BASE(30'h0000_0000), .LB_AW(11), .FB_BPP(0), .HAS_MCB(1), .SRC_RESET(1'b0)) u_vid1 (
        .clk(clk_sys), .rst(sys_rst),
        .vid_cyc(vid_cyc), .vid_we(vid_we), .vid_adr(vid_adr), .vid_sel(vid_sel), .vid_dat_w(vid_dat_w),
        .vid_dat_r(vid_dat_r), .vid_ack(vid_ack), .vid_irq(vid_irq),
        .fb_cyc(1'b0), .fb_we(1'b0), .fb_adr(16'd0), .fb_sel(4'd0), .fb_dat_w(32'd0), .fb_dat_r(), .fb_ack(),
        .vclk(vclk), .oclk(oclk1), .dcm_locked(oclk1_locked & vclk_locked),
        .dcm_progen(p1_progen), .dcm_progdata(p1_progdata), .dcm_progdone(p1_progdone),
        .mcb_calib_done(calib3_async), .mcb_cmd_en(v1_cmd_en), .mcb_cmd_instr(v1_cmd_instr), .mcb_cmd_bl(v1_cmd_bl),
        .mcb_cmd_byte_addr(v1_cmd_byte_addr), .mcb_cmd_full(v1_cmd_full), .mcb_rd_en(v1_rd_en), .mcb_rd_data(v1_rd_data),
        .mcb_rd_empty(v1_rd_empty), .mcb_rd_overflow(v1_rd_overflow), .mcb_rd_error(v1_rd_error),
        .dvi_xclk_p(dvi_xclk_p), .dvi_xclk_n(dvi_xclk_n), .dvi_d(dvi_d), .dvi_h(dvi_h), .dvi_v(dvi_v), .dvi_de(dvi_de),
        .dvi_reset_n(dvi_reset_n), .dvi_hpint_n(dvi_hpint_n), .hdmi_reset_n(hdmi_reset_n), .hdmi_hpint_n(hdmi_hpint_n),
        .ch_scl_low(ch_scl_low), .ch_sda_low(ch_sda_low), .ch_scl_in(ch_scl_in), .ch_sda_in(ch_sda_in));
    wire hdmi_xclk_n_unused, v2_dvi_reset_n, v2_hdmi_reset_n, v2_scl_low, v2_sda_low;
    g2_video #(.BYTE_BASE(30'h0100_0000), .LB_AW(11), .FB_BPP(0), .HAS_MCB(1), .SRC_RESET(1'b0)) u_vid2 (
        .clk(clk_sys), .rst(sys_rst),
        .vid_cyc(vid2_cyc), .vid_we(vid2_we), .vid_adr(vid2_adr), .vid_sel(vid2_sel), .vid_dat_w(vid2_dat_w),
        .vid_dat_r(vid2_dat_r), .vid_ack(vid2_ack), .vid_irq(vid2_irq),
        .fb_cyc(1'b0), .fb_we(1'b0), .fb_adr(16'd0), .fb_sel(4'd0), .fb_dat_w(32'd0), .fb_dat_r(), .fb_ack(),
        .vclk(vclk), .oclk(oclk1), .dcm_locked(oclk1_locked & vclk_locked),
        .dcm_progen(), .dcm_progdata(), .dcm_progdone(1'b1),
        .mcb_calib_done(calib3_async), .mcb_cmd_en(v2_cmd_en), .mcb_cmd_instr(v2_cmd_instr), .mcb_cmd_bl(v2_cmd_bl),
        .mcb_cmd_byte_addr(v2_cmd_byte_addr), .mcb_cmd_full(v2_cmd_full), .mcb_rd_en(v2_rd_en), .mcb_rd_data(v2_rd_data),
        .mcb_rd_empty(v2_rd_empty), .mcb_rd_overflow(v2_rd_overflow), .mcb_rd_error(v2_rd_error),
        .dvi_xclk_p(hdmi_xclk_p), .dvi_xclk_n(hdmi_xclk_n_unused), .dvi_d(hdmi_d), .dvi_h(hdmi_h), .dvi_v(hdmi_v), .dvi_de(hdmi_de),
        .dvi_reset_n(v2_dvi_reset_n), .dvi_hpint_n(1'b1), .hdmi_reset_n(v2_hdmi_reset_n), .hdmi_hpint_n(1'b1),
        .ch_scl_low(v2_scl_low), .ch_sda_low(v2_sda_low), .ch_scl_in(1'b1), .ch_sda_in(1'b1));
    wire _unused_v2 = &{1'b0, hdmi_xclk_n_unused, v2_dvi_reset_n, v2_hdmi_reset_n, v2_scl_low, v2_sda_low, 1'b0};
    // ---- audio: the WM8750BL playback path, page 0x8000_9000 (rtl/g2/g2_audio.v) ----------------------------------
    g2_audio u_aud (
        .clk(clk_sys), .rst(sys_rst),
        .aud_cyc(aud_cyc), .aud_we(aud_we), .aud_adr(aud_adr), .aud_sel(aud_sel), .aud_dat_w(aud_dat_w),
        .aud_dat_r(aud_dat_r), .aud_ack(aud_ack), .irq(aud_irq), .mclk(clk24_aud), .mclk_locked(pll24_locked_aud),
        .codec_mclk(codec_mclk), .codec_bclk(codec_bclk), .codec_daclrc(codec_daclrc), .codec_dacdat(codec_dacdat));
`ifdef G2_SNES
    // ---- the subsystem slot (G2_SNES) on the RasterIX page 0x8000_E000: its two caches on chip B
    // port P1, its frame writer on chip B port P3; chip A port P2 is idle.  The page's interrupt line carries the
    // The subsystem interrupt ORed with the audio FIFO watermark interrupt (each has its own status register).
    wire snes_irq;
`ifdef G2_AMIGA
    // ---- the Amiga fabric (amiga/rtl/g2/amiga_g2.v) in the first core of the subsystem slot's place: the same page,
    // interrupt, cache port (P1) and frame-writer port (P3); its cache waits for chip B's calibration.  Its fast-RAM
    // cache is on chip A port P1 (fp_*) and waits for chip A's calibration.
    amiga_g2 u_amiga (
        .clk(clk_sys), .clk_gate_src(clk_sys_pll), .rst(sys_rst),
        .cyc(rix_cyc), .we(rix_we), .adr(rix_adr), .sel(rix_sel), .dat_w(rix_dat_w), .dat_r(rix_dat_r),
        .ack(rix_ack), .irq(snes_irq),
        .head1_frame(vid_irq), .head2_frame(vid2_irq),
        .c_cmd_en(q3_cmd_en), .c_cmd_instr(q3_cmd_instr), .c_cmd_bl(q3_cmd_bl), .c_cmd_byte_addr(q3_cmd_byte_addr),
        .c_cmd_full(q3_cmd_full), .c_wr_en(q3_wr_en), .c_wr_mask(q3_wr_mask), .c_wr_data(q3_wr_data),
        .c_wr_empty(q3_wr_empty), .c_rd_en(q3_rd_en), .c_rd_data(q3_rd_data), .c_rd_empty(q3_rd_empty),
        .mcb_ready(calib3_async),
        .v_cmd_en(s3_cmd_en), .v_cmd_instr(s3_cmd_instr), .v_cmd_bl(s3_cmd_bl), .v_cmd_byte_addr(s3_cmd_byte_addr),
        .v_cmd_full(s3_cmd_full), .v_wr_en(s3_wr_en), .v_wr_mask(s3_wr_mask), .v_wr_data(s3_wr_data),
        .v_wr_empty(s3_wr_empty), .v_wr_count(s3_wr_count),
        .f_cmd_en(fp_cmd_en), .f_cmd_instr(fp_cmd_instr), .f_cmd_bl(fp_cmd_bl), .f_cmd_byte_addr(fp_cmd_byte_addr),
        .f_cmd_full(fp_cmd_full), .f_wr_en(fp_wr_en), .f_wr_mask(fp_wr_mask), .f_wr_data(fp_wr_data),
        .f_wr_empty(fp_wr_empty), .f_rd_en(fp_rd_en), .f_rd_data(fp_rd_data), .f_rd_empty(fp_rd_empty),
        .f_mcb_ready(ddr_ready),
        .dbg_amiga_clk(), .dbg_hs(), .dbg_vs(), .dbg_r(), .dbg_g(), .dbg_b());
`else
    snes_g2 u_snes (
        .clk(clk_sys), .clk_gate_src(clk_sys_pll), .rst(sys_rst),
        .cyc(rix_cyc), .we(rix_we), .adr(rix_adr), .sel(rix_sel), .dat_w(rix_dat_w), .dat_r(rix_dat_r),
        .ack(rix_ack), .irq(snes_irq),
        .head1_frame(vid_irq), .head2_frame(vid2_irq),
        .c_cmd_en(q3_cmd_en), .c_cmd_instr(q3_cmd_instr), .c_cmd_bl(q3_cmd_bl), .c_cmd_byte_addr(q3_cmd_byte_addr),
        .c_cmd_full(q3_cmd_full), .c_wr_en(q3_wr_en), .c_wr_mask(q3_wr_mask), .c_wr_data(q3_wr_data),
        .c_wr_empty(q3_wr_empty), .c_rd_en(q3_rd_en), .c_rd_data(q3_rd_data), .c_rd_empty(q3_rd_empty),
        .v_cmd_en(s3_cmd_en), .v_cmd_instr(s3_cmd_instr), .v_cmd_bl(s3_cmd_bl), .v_cmd_byte_addr(s3_cmd_byte_addr),
        .v_cmd_full(s3_cmd_full), .v_wr_en(s3_wr_en), .v_wr_mask(s3_wr_mask), .v_wr_data(s3_wr_data),
        .v_wr_empty(s3_wr_empty));
`endif
    assign rix_irq = snes_irq | aud_irq;
    assign dl_cmd_en = 1'b0; assign dl_cmd_instr = 3'b001; assign dl_cmd_bl = 6'd0; assign dl_cmd_addr = 30'd0;
    assign dl_rd_en = 1'b0;
    wire _unused_snes = &{1'b0, dl_cmd_full, dl_rd_data, dl_rd_empty, q3_wr_full, q3_rd_overflow, q3_wr_underrun, 1'b0};
`else
    // ---- RasterIX (rtl/g2/g2_rix.v) on chip B port P1 (in place of the chip B exerciser) and chip A port P2 ------
    g2_rix #(.TMU_COUNT(1), .MAX_TEXTURE_SIZE(128), .ENABLE_MIPMAPPING(1), .ENABLE_STENCIL_BUFFER(0), .ENABLE_DEPTH_BUFFER(1),
             .ENABLE_FOG(1), .ENABLE_TEXTURE_FILTERING(1), .TEXTURE_PAGE_SIZE(2048), .RASTERIZER_FIXPOINT_PRECISION(25)) u_rix (
        .clk(clk_sys), .rst(sys_rst),
        .rix_cyc(rix_cyc), .rix_we(rix_we), .rix_adr(rix_adr), .rix_sel(rix_sel), .rix_dat_w(rix_dat_w),
        .rix_dat_r(rix_dat_r), .rix_ack(rix_ack), .rix_irq(rix_irq),
        .a_cmd_en(dl_cmd_en), .a_cmd_instr(dl_cmd_instr), .a_cmd_bl(dl_cmd_bl), .a_cmd_byte_addr(dl_cmd_addr),
        .a_cmd_full(dl_cmd_full), .a_rd_en(dl_rd_en), .a_rd_data(dl_rd_data), .a_rd_empty(dl_rd_empty),
        .b_cmd_en(q3_cmd_en), .b_cmd_instr(q3_cmd_instr), .b_cmd_bl(q3_cmd_bl), .b_cmd_byte_addr(q3_cmd_byte_addr),
        .b_cmd_full(q3_cmd_full), .b_wr_en(q3_wr_en), .b_wr_mask(q3_wr_mask), .b_wr_data(q3_wr_data), .b_wr_full(q3_wr_full),
        .b_rd_en(q3_rd_en), .b_rd_data(q3_rd_data), .b_rd_empty(q3_rd_empty));
    assign s3_cmd_en = 1'b0; assign s3_cmd_instr = 3'b000; assign s3_cmd_bl = 6'd0; assign s3_cmd_byte_addr = 30'd0;
    assign s3_wr_en = 1'b0; assign s3_wr_mask = 4'b0000; assign s3_wr_data = 32'd0;
    wire _unused_s3 = &{1'b0, s3_cmd_full, s3_wr_empty, q3_wr_empty, aud_irq, clk_sys_pll, 1'b0};
`endif
    assign bad3 = 1'b0; assign run3 = 1'b0; assign passes3 = 16'd0; assign errs3 = 16'd0;   // no chip B exerciser
    // board status +0x04: [0] chip A calibrated, [1] chip B calibrated, [2] chip B >= 1 full pass, [3] chip B mismatch,
    // [4] chip B exerciser running, [5] chip A exerciser running, [6] chip A >= 1 full pass, [7] chip A mismatch
    wire [7:0]  ddr_status_ab = {ddr_status[7:5], run3, bad3, |passes3, cal3c_s1, ddr_status[0]};

    // ---- board lines AA6 (RESET_OUT_N), C10 (POWER_SLEEP), C13 (SLEEP_REQ): board page +0x40 (rtl/g2/pano_soc_g2.v)
    wire [2:0] pin_oe, pin_o, pin_i;
    IOBUF u_pin_aa6 (.IO(brd_reset_out_n), .I(pin_o[0]), .T(~pin_oe[0]), .O(pin_i[0]));
    IOBUF u_pin_c10 (.IO(brd_power_sleep), .I(pin_o[1]), .T(~pin_oe[1]), .O(pin_i[1]));
    IOBUF u_pin_c13 (.IO(brd_sleep_req),   .I(pin_o[2]), .T(~pin_oe[2]), .O(pin_i[2]));


    // ---- the CPU domain (clk_cpu, 75 MHz): the SoC, its block RAM and both DDR bridges.  The pages of the 50 MHz
    // fabric (heads, audio, RasterIX, USB window) are reached through one wb_cdc bridge each; their interrupt
    // levels are synchronised into clk_cpu; the reset is sys_rst synchronised.
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] rst_cpu_s = 2'b11;
    always @(posedge clk_cpu) rst_cpu_s <= {rst_cpu_s[0], sys_rst};
    wire        rst_cpu = rst_cpu_s[1];
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [3:0] irq_s0 = 4'd0, irq_s1 = 4'd0;
    always @(posedge clk_cpu) begin irq_s0 <= {rix_irq, usb_irq, vid2_irq, vid_irq}; irq_s1 <= irq_s0; end
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg cal3c_s0 = 1'b0, cal3c_s1 = 1'b0;
    always @(posedge clk_cpu) begin cal3c_s0 <= calib3_async; cal3c_s1 <= cal3c_s0; end
    wire        vid_cyc_c, vid_we_c, vid_ack_c, vid2_cyc_c, vid2_we_c, vid2_ack_c, aud_cyc_c, aud_we_c, aud_ack_c;
    wire        rix_cyc_c, rix_we_c, rix_ack_c, usb_cyc_c, usb_we_c, usb_ack_c;
    wire [9:0]  vid_adr_c, vid2_adr_c, aud_adr_c, rix_adr_c; wire [21:0] usb_adr_c;
    wire [3:0]  vid_sel_c, vid2_sel_c, aud_sel_c, rix_sel_c, usb_sel_c;
    wire [31:0] vid_dat_w_c, vid_dat_r_c, vid2_dat_w_c, vid2_dat_r_c, aud_dat_w_c, aud_dat_r_c, rix_dat_w_c, rix_dat_r_c, usb_dat_w_c, usb_dat_r_c;
    wire        vid_stb_s, vid2_stb_s, aud_stb_s, rix_stb_s, usb_stb_s;
    wb_cdc #(.AW(10)) u_cdc_vid (
        .clk_m(clk_cpu), .rst_m(rst_cpu), .m_cyc(vid_cyc_c), .m_stb(vid_cyc_c), .m_we(vid_we_c), .m_adr(vid_adr_c), .m_sel(vid_sel_c),
        .m_dat_w(vid_dat_w_c), .m_dat_r(vid_dat_r_c), .m_ack(vid_ack_c), .m_err(),
        .clk_s(clk_sys), .rst_s(sys_rst), .s_cyc(vid_cyc), .s_stb(vid_stb_s), .s_we(vid_we), .s_adr(vid_adr), .s_sel(vid_sel),
        .s_dat_w(vid_dat_w), .s_dat_r(vid_dat_r), .s_ack(vid_ack), .s_err(1'b0));
    wb_cdc #(.AW(10)) u_cdc_vid2 (
        .clk_m(clk_cpu), .rst_m(rst_cpu), .m_cyc(vid2_cyc_c), .m_stb(vid2_cyc_c), .m_we(vid2_we_c), .m_adr(vid2_adr_c), .m_sel(vid2_sel_c),
        .m_dat_w(vid2_dat_w_c), .m_dat_r(vid2_dat_r_c), .m_ack(vid2_ack_c), .m_err(),
        .clk_s(clk_sys), .rst_s(sys_rst), .s_cyc(vid2_cyc), .s_stb(vid2_stb_s), .s_we(vid2_we), .s_adr(vid2_adr), .s_sel(vid2_sel),
        .s_dat_w(vid2_dat_w), .s_dat_r(vid2_dat_r), .s_ack(vid2_ack), .s_err(1'b0));
    wb_cdc #(.AW(10)) u_cdc_aud (
        .clk_m(clk_cpu), .rst_m(rst_cpu), .m_cyc(aud_cyc_c), .m_stb(aud_cyc_c), .m_we(aud_we_c), .m_adr(aud_adr_c), .m_sel(aud_sel_c),
        .m_dat_w(aud_dat_w_c), .m_dat_r(aud_dat_r_c), .m_ack(aud_ack_c), .m_err(),
        .clk_s(clk_sys), .rst_s(sys_rst), .s_cyc(aud_cyc), .s_stb(aud_stb_s), .s_we(aud_we), .s_adr(aud_adr), .s_sel(aud_sel),
        .s_dat_w(aud_dat_w), .s_dat_r(aud_dat_r), .s_ack(aud_ack), .s_err(1'b0));
    wb_cdc #(.AW(10)) u_cdc_rix (
        .clk_m(clk_cpu), .rst_m(rst_cpu), .m_cyc(rix_cyc_c), .m_stb(rix_cyc_c), .m_we(rix_we_c), .m_adr(rix_adr_c), .m_sel(rix_sel_c),
        .m_dat_w(rix_dat_w_c), .m_dat_r(rix_dat_r_c), .m_ack(rix_ack_c), .m_err(),
        .clk_s(clk_sys), .rst_s(sys_rst), .s_cyc(rix_cyc), .s_stb(rix_stb_s), .s_we(rix_we), .s_adr(rix_adr), .s_sel(rix_sel),
        .s_dat_w(rix_dat_w), .s_dat_r(rix_dat_r), .s_ack(rix_ack), .s_err(1'b0));
    wb_cdc #(.AW(22)) u_cdc_usb (
        .clk_m(clk_cpu), .rst_m(rst_cpu), .m_cyc(usb_cyc_c), .m_stb(usb_cyc_c), .m_we(usb_we_c), .m_adr(usb_adr_c), .m_sel(usb_sel_c),
        .m_dat_w(usb_dat_w_c), .m_dat_r(usb_dat_r_c), .m_ack(usb_ack_c), .m_err(),
        .clk_s(clk_sys), .rst_s(sys_rst), .s_cyc(usb_cyc), .s_stb(usb_stb_s), .s_we(usb_we), .s_adr(usb_adr), .s_sel(usb_sel),
        .s_dat_w(usb_dat_w), .s_dat_r(usb_dat_r), .s_ack(usb_ack), .s_err(1'b0));
    wire _unused_cdc = &{1'b0, vid_stb_s, vid2_stb_s, aud_stb_s, rix_stb_s, usb_stb_s, 1'b0};

    // SoC block RAM: 256 KB; 32 KB in the G2_SNES fabric, whose firmware runs from DDR2 (fw/snes/, the loader
    // and the stack are all that live in block RAM there).
`ifdef G2_SNES
    localparam integer SOC_RAM_AW = 13;
`else
    localparam integer SOC_RAM_AW = 16;
`endif
    pano_soc_g2 #(.RAM_INIT_HEX(RAM_INIT_HEX), .RAM_AW(SOC_RAM_AW), .HOLD_LOG2(HOLD_LOG2), .DEB_LOG2(DEB_LOG2), .UART_DIV(UART_DIV), .WDT_DEFAULT(WDT_DEFAULT), .ETH_RX_OS(ETH_RX_OS)) u_soc (
        .clk(clk_cpu), .rst(rst_cpu), .clk_icap(clk_icap),
        .pin_oe(pin_oe), .pin_o(pin_o), .pin_i(pin_i),
        .uart_txd(hdmi_ddc_sda), .uart_rxd(hdmi_ddc_scl),
        .spi_cs_n(spi_cs_n), .spi_sck(spi_sck), .spi_mosi(spi_mosi), .spi_miso(spi_miso),
        .eth_rxc(eth_rxc), .eth_rxc_pad(erxc_ibufg), .eth_rxdv(erx_dly[8]), .eth_rxer(erx_dly[9]), .eth_rxd(erx_dly[7:0]),
        .clk_tx(y13_clk), .mii_txc(gmii_tx_clk), .eth_txen(gmii_tx_en), .eth_txer(gmii_tx_er), .eth_txd(gmii_tx_d),
        .gtx_on(gtx_on),
        .eth_link(gmii_int_n), .mdc(mdc), .mdio_o(mdio_o), .mdio_oe(mdio_oe), .mdio_i(mdio_i),
        .button_n(pano_button), .led_ovr(led_ovr), .led_val(led_val), .activity(activity), .bus_err(bus_err),
        .clk_status({settle_count, 3'd0, settle_wins, 14'd0, !pll_rst, lock_s[3]}),
        .ddr_cyc(ddr_cyc), .ddr_stb(ddr_stb), .ddr_we(ddr_we), .ddr_adr(ddr_adr), .ddr_uncached(), .ddr_sel(ddr_sel),
        .ddr_dat_w(ddr_dat_w), .ddr_cti(ddr_cti), .ddr_bte(ddr_bte),
        .ddr_dat_r(ddr_dat_r), .ddr_ack(ddr_ack), .ddr_err(ddr_err), .ddr_ready(ddr_ready), .ddr_status(ddr_status_ab), .ddr_x_en(ddr_x_en), .ddr_x_passes({x_passes_a, passes3}), .ddr_x_errs({x_errs_a, errs3}),
        .ddr_st_clear(ddr_st_clear), .ddr_irq(1'b0),
        .vid_cyc(vid_cyc_c), .vid_we(vid_we_c), .vid_adr(vid_adr_c), .vid_sel(vid_sel_c), .vid_dat_w(vid_dat_w_c),
        .vid_dat_r(vid_dat_r_c), .vid_ack(vid_ack_c), .vid_irq(irq_s1[0]),
        .vid2_cyc(vid2_cyc_c), .vid2_we(vid2_we_c), .vid2_adr(vid2_adr_c), .vid2_sel(vid2_sel_c), .vid2_dat_w(vid2_dat_w_c),
        .vid2_dat_r(vid2_dat_r_c), .vid2_ack(vid2_ack_c), .vid2_irq(irq_s1[1]),
        .aud_cyc(aud_cyc_c), .aud_we(aud_we_c), .aud_adr(aud_adr_c), .aud_sel(aud_sel_c), .aud_dat_w(aud_dat_w_c),
        .aud_dat_r(aud_dat_r_c), .aud_ack(aud_ack_c),
        .rix_cyc(rix_cyc_c), .rix_we(rix_we_c), .rix_adr(rix_adr_c), .rix_sel(rix_sel_c), .rix_dat_w(rix_dat_w_c),
        .rix_dat_r(rix_dat_r_c), .rix_ack(rix_ack_c), .rix_irq(irq_s1[3]),
        .ddrb_cyc(ddrb_cyc), .ddrb_stb(ddrb_stb), .ddrb_we(ddrb_we), .ddrb_adr(ddrb_adr), .ddrb_sel(ddrb_sel),
        .ddrb_dat_w(ddrb_dat_w), .ddrb_cti(ddrb_cti), .ddrb_bte(ddrb_bte),
        .ddrb_dat_r(ddrb_dat_r), .ddrb_ack(ddrb_ack), .ddrb_err(ddrb_err), .ddrb_ready(cal3c_s1),
        .usb_cyc(usb_cyc_c), .usb_we(usb_we_c), .usb_adr(usb_adr_c), .usb_sel(usb_sel_c), .usb_dat_w(usb_dat_w_c),
        .usb_dat_r(usb_dat_r_c), .usb_ack(usb_ack_c), .usb_irq(irq_s1[2]),
        .cpu_trap(cpu_trap));

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, cpu_trap, 1'b0};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule

`default_nettype wire
