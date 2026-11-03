/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_video.v -- the video head of the Pano Logic G2 SoC: rtl/vga/vga_scan.v with the G1 register page, fed from a
// block-RAM frame buffer (rtl/vga/vga_bram_fb.v) or from one MCB user port (rtl/vga/vga_mcb_shim.v), driving a
// Chrontel CH7301C (rtl/vga/ch7301_out.v).
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// 1 REGISTER PAGE (the SoC's video port: vid_cyc = cycle and strobe, vid_adr = word inside the 4 KB page, one-clock
//   acknowledge).  Offsets 0x00..0x18 are the G1 page (rtl/pano_soc_vex.v, per_idx 6; fw/vga.h) bit for bit, so
//   fw/vga.c and fw/vgacon.c run unchanged:
//     0x00 R/W  {fmt565[5], pal_en[4], mode[3:1], en[0]}   mode, pal_en and fmt565 apply at the next frame strobe
//               (vga_scan); fmt565: one 16-bit RGB565 word a pixel (RasterIX's colour buffer), pal_en wins when both
//               are set; the output clock is reprogrammed for a new mode after that strobe, and status bit 0
//               (clock_ready) reads 1 only once the raster and the clock are both at the mode written here: a CTRL
//               write is safe at any instant
//     0x04 R/W  base [22:0], 16-bit words: from BYTE_BASE in the MCB, or of the block-RAM buffers' one-byte-a-pixel
//               space (buffer 0 at 0, buffer 1 at 153,600: rtl/vga/vga_bram_fb.v section 3)
//     0x08 R/W  {h[26:16], w[10:0]} source geometry
//     0x0C W    swap: take base, w, h at the next vblank;  R  the base being scanned
//     0x10 R    frames scanned
//     0x14 R    MCB read commands issued (the G1 counts LPDDR runs here)
//     0x18 W    palette entry {index[31:24], r[23:16], g[15:8], b[7:0]};  R  capabilities:
//               bit 0 a paletted source line may be 1,024 pixels or more (G1 meaning); bit 1 this page has the
//               G2 registers 0x1C..0x24; bit 2 a block-RAM frame buffer is present
//   G2 additions (a G1 page reads 0 at 0x1C):
//     0x1C R/W  CH7301C control pins: W {hdmi_reset_n[5], dvi_reset_n[4], sda_low[1], scl_low[0]};
//               R {hdmi_reset_n[5], dvi_reset_n[4], hdmi_hpint_n[3], dvi_hpint_n[2], sda[1], scl[0]}.
//               SCL and SDA are open drain: 1 in scl_low / sda_low pulls the line low, 0 releases it.
//               Reset value 0x30: both encoders out of reset, bus released.
//     0x20 R    status {source_active[9], calib_done[8], clock_fault[6], mcb_err[5], mode_unsupported[4],
//               fifo_overflow[3], fifo_underflow[2], streaming[1], clock_ready[0]}; errors are sticky until reset.
//     0x24 R/W  source: W [0] 1 = block-RAM frame buffer, 0 = MCB;  R {HAS_MCB[17], block RAM present[16],
//               FB_BPP[9:8], source requested[0]}.  Reset value SRC_RESET.  The head changes source at a clock
//               where no read is outstanding; the frame in progress may show both.
//
// 2 FRAME-BUFFER WINDOW (fb_*): the CPU's view of the block-RAM buffers, packed FB_BPP bits a pixel
//   (rtl/vga/vga_bram_fb.v section 2): buffer 0 at byte 0, buffer 1 at byte 640 x 480 x FB_BPP / 8.
//
// 3 CLOCKS.
//     clk    register page, frame-buffer window, palette write port, DCM_CLKGEN PROGCLK.  50 MHz in the SoC contract.
//     vclk   the head and the MCB port: 80.000 MHz, exact from 125 MHz (rtl/vga/g2_video_clk.v).  The NCO
//            increments below are exact for vclk = 80 MHz: 25/80 = 5/16, 40/80 = 1/2, 65/80 = 13/16.
//     oclk   2 x the pixel clock from the DCM_CLKGEN: 50 / 80 / 130 MHz for modes 0 / 1 / 2.
//   vga_scan's `fclk' is vclk; its geometry per mode is unchanged.  640x480 runs at 25.000 MHz (0.7 % below the
//   VESA 25.175 MHz, which is not an exact fraction of 125 MHz).
//
// 4 SYNC POLARITY.  H and V reach the CH7301C active high in every mode (the VESA polarities of vga_scan's
//   table are undone here), and fw/ch7301.c sets register 1Fh HSP = VSP = 1 once, so no register depends on
//   the mode and fw/vga.c's mode changes need no encoder access.  The polarity the encoder then drives on the
//   DVI and analog outputs is untested.
//
// 5 MODE 4 (320x240 at 15.7 kHz).  6.696 MHz pixels are below the CH7301C's 25 MHz XCLK minimum, so the output stage
//   sends each pixel four times at an XCLK of 26.79 MHz (oclk 53.571 MHz = 125 x 3/7, the NCO increment 75/896 of
//   vclk matches it exactly): the line and frame rates are the raster's, the encoder sees 1,704 pixels a line.
//   Mode 3 (1600x1200 at 80 MHz pixels on this vclk, 36.8 Hz frame) is not offered: output blanked, status bit 4.
`timescale 1ns / 1ps
`default_nettype none
module g2_video #(
    parameter [29:0] BYTE_BASE = 30'd0,         // MCB byte address of head word 0
    parameter integer LB_AW    = 11,            // line buffer: direct-colour sources up to 1,024 pixels wide
    parameter integer FB_BPP   = 2,             // block-RAM frame buffer: 1 or 2 bits a pixel, 0 = none
    parameter integer HAS_MCB  = 1,             // 0: no MCB adapter; the mcb_* inputs are ignored
    parameter         SRC_RESET = 1'b1          // source after reset: 1 block RAM, 0 MCB
) (
    // ---- register page 0x8000_6000 (the SoC video port) ----
    input  wire        clk,
    input  wire        rst,                     // synchronous to clk, active high
    input  wire        vid_cyc,                 // cycle and strobe
    input  wire        vid_we,
    input  wire [9:0]  vid_adr,                 // 32-bit word inside the page
    input  wire [3:0]  vid_sel,                 // unused: every register is written whole
    input  wire [31:0] vid_dat_w,
    output reg  [31:0] vid_dat_r,
    output reg         vid_ack,
    output wire        vid_irq,                 // one clk pulse per scanned frame

    // ---- frame-buffer window (clk) ----
    input  wire        fb_cyc,                  // cycle and strobe
    input  wire        fb_we,
    input  wire [15:0] fb_adr,                  // 32-bit word inside the window
    input  wire [3:0]  fb_sel,
    input  wire [31:0] fb_dat_w,
    output wire [31:0] fb_dat_r,
    output wire        fb_ack,

    // ---- clocks from rtl/vga/g2_video_clk.v ----
    input  wire        vclk,
    input  wire        oclk,
    input  wire        dcm_locked,
    output wire        dcm_progen,
    output wire        dcm_progdata,
    input  wire        dcm_progdone,

    // ---- MCB user port (32-bit, read), all on vclk ----
    input  wire        mcb_calib_done,
    output wire        mcb_cmd_en,
    output wire [2:0]  mcb_cmd_instr,
    output wire [5:0]  mcb_cmd_bl,
    output wire [29:0] mcb_cmd_byte_addr,
    input  wire        mcb_cmd_full,
    output wire        mcb_rd_en,
    input  wire [31:0] mcb_rd_data,
    input  wire        mcb_rd_empty,
    input  wire        mcb_rd_overflow,
    input  wire        mcb_rd_error,

    // ---- CH7301C (DVI-I connector) ----
    output wire        dvi_xclk_p,
    output wire        dvi_xclk_n,
    output wire [11:0] dvi_d,
    output wire        dvi_h,
    output wire        dvi_v,
    output wire        dvi_de,
    output wire        dvi_reset_n,
    input  wire        dvi_hpint_n,
    output wire        hdmi_reset_n,
    input  wire        hdmi_hpint_n,
    output wire        ch_scl_low,              // 1: drive SCL (E8) low; 0: release
    output wire        ch_sda_low,              // 1: drive SDA (D9) low; 0: release
    input  wire        ch_scl_in,
    input  wire        ch_sda_in
);
    // ================================================================ register page, clk
    reg         vga_en = 1'b0, vga_tog = 1'b0, vga_pal_en = 1'b0, vga_fmt565 = 1'b0;
    reg  [2:0]  vga_mode = 3'd0;
    reg  [22:0] vga_base = 23'd0;
    reg  [10:0] vga_w = 11'd320, vga_h = 11'd240;
    reg         vga_pal_we = 1'b0;
    reg  [7:0]  vga_pal_wa = 8'd0;
    reg  [29:0] vga_pal_wd = 30'd0;
    reg  [5:0]  pins = 6'b110000;               // {hdmi_reset_n, dvi_reset_n, -, -, sda_low, scl_low}
    reg  [31:0] frames = 32'd0;
    wire [22:0] vga_cur_base;
    wire [31:0] runs_c;
    wire        dcm_ready, dcm_sup, dcm_fault;
    wire        st_calib, st_mcb_err, st_ovf, st_unf, st_stream;

    assign dvi_reset_n  = pins[4];
    assign hdmi_reset_n = pins[5];
    assign ch_scl_low   = pins[0];
    assign ch_sda_low   = pins[1];

    reg  [1:0] hp_s = 2'b11, bus_s = 2'b11;
    always @(posedge clk) begin hp_s <= {hdmi_hpint_n, dvi_hpint_n}; bus_s <= {ch_sda_in, ch_scl_in}; end

    reg  src_req = SRC_RESET;
    wire src_act_c;
    wire acc = vid_cyc && !vid_ack;
    always @(posedge clk) begin
        vid_ack    <= acc;
        vga_pal_we <= 1'b0;
        if (rst) begin
            vga_en <= 1'b0; vga_mode <= 3'd0; vga_pal_en <= 1'b0; vga_fmt565 <= 1'b0; pins <= 6'b110000; vid_ack <= 1'b0; src_req <= SRC_RESET;
        end else if (acc) begin
            vid_dat_r <= 32'd0;
            case ((vid_adr[9:4] == 6'd0) ? vid_adr[3:0] : 4'd15)
            4'd0: begin vid_dat_r <= {26'd0, vga_fmt565, vga_pal_en, vga_mode, vga_en};
                      if (vid_we) begin vga_en <= vid_dat_w[0]; vga_mode <= vid_dat_w[3:1]; vga_pal_en <= vid_dat_w[4]; vga_fmt565 <= vid_dat_w[5]; end end
            4'd1: begin vid_dat_r <= {9'd0, vga_base}; if (vid_we) vga_base <= vid_dat_w[22:0]; end
            4'd2: begin vid_dat_r <= {5'd0, vga_h, 5'd0, vga_w}; if (vid_we) begin vga_w <= vid_dat_w[10:0]; vga_h <= vid_dat_w[26:16]; end end
            4'd3: begin vid_dat_r <= {9'd0, vga_cur_base}; if (vid_we) vga_tog <= ~vga_tog; end
            4'd4: vid_dat_r <= frames;
            4'd5: vid_dat_r <= runs_c;
            4'd6: begin vid_dat_r <= {28'd0, 1'b1, FB_BPP != 0, 2'b11};       // [3] RGB565 source (CTRL[5]) present
                      if (vid_we) begin
                          vga_pal_we <= 1'b1;
                          vga_pal_wa <= vid_dat_w[31:24];
                          vga_pal_wd <= {vid_dat_w[23:16], 2'b00, vid_dat_w[15:8], 2'b00, vid_dat_w[7:0], 2'b00};
                      end
                  end
            4'd7: begin vid_dat_r <= {26'd0, pins[5:4], hp_s, bus_s}; if (vid_we) pins <= {vid_dat_w[5:4], 2'b00, vid_dat_w[1:0]}; end
            4'd8: vid_dat_r <= {22'd0, src_act_c, st_calib, 1'b0, dcm_fault, st_mcb_err, !dcm_sup, st_ovf, st_unf, st_stream, dcm_ready};
            4'd9: begin vid_dat_r <= {14'd0, HAS_MCB != 0, FB_BPP != 0, 6'd0, FB_BPP[1:0], 7'd0, src_req};
                      if (vid_we) src_req <= (HAS_MCB == 0) ? 1'b1 : (FB_BPP == 0) ? 1'b0 : vid_dat_w[0]; end
            default: vid_dat_r <= 32'd0;
            endcase
        end
    end

    /* the mode the raster has applied (vga_scan mode_cur, vclk), carried into clk: two flops, then taken only when
     * two successive samples agree (the three bits change once per mode write, at a frame strobe) */
    wire [2:0] scan_mode;
    reg  [2:0] smode_c1 = 3'd0, smode_c2 = 3'd0, smode_c = 3'd0;
    always @(posedge clk) begin
        smode_c1 <= scan_mode; smode_c2 <= smode_c1;
        if (smode_c2 == smode_c1) smode_c <= smode_c2;
    end
    wire dcm_ready_raw;
    assign dcm_ready = dcm_ready_raw && (smode_c == vga_mode);   /* status bit 0: the written mode is in force */
    vid_dcm_prog u_prog (
        .clk(clk), .rst(rst), .mode(smode_c),
        .progen(dcm_progen), .progdata(dcm_progdata), .progdone(dcm_progdone), .locked(dcm_locked),
        .ready(dcm_ready_raw), .supported(dcm_sup), .fault(dcm_fault));

    // ================================================================ vclk: the head and the MCB port
    reg  [2:0] vrst_s = 3'b111;
    always @(posedge vclk) vrst_s <= {vrst_s[1:0], rst};
    wire       vrst = vrst_s[2];
    reg  [1:0] en_v = 2'b00, rdy_v = 2'b00, cal_v = 2'b00;
    reg  [2:0] mode_v1 = 3'd0, mode_v = 3'd0;
    reg        rep4_v = 1'b0;                       // modes 3, 4, 6 and 7 (15 kHz): pixels repeated; registered with mode_v: the vclk-to-oclk path is one flop
    reg        rep2_v = 1'b0;                       // modes 3 and 7 (640 wide): two sends a pixel, not four; the same
    always @(posedge vclk) begin
        en_v <= {en_v[0], vga_en}; rdy_v <= {rdy_v[0], dcm_ready_raw}; cal_v <= {cal_v[0], mcb_calib_done};
        mode_v1 <= scan_mode; mode_v <= mode_v1;       // the output stage follows the raster's applied mode
        rep4_v <= (mode_v1 == 3'd3) || (mode_v1 == 3'd4) || (mode_v1 == 3'd6) || (mode_v1 == 3'd7);
        rep2_v <= (mode_v1 == 3'd3) || (mode_v1 == 3'd7);
    end
    /* vga_scan's syncs are active low in every mode but 1; the CH7301C is told active high
     * (register 1Fh HSP = VSP = 1, fw/ch7301.c) and given active high in every mode */
    wire       h_inv = (mode_v != 3'd1);
    wire       v_inv = (mode_v != 3'd1);

    wire [3:0]  v_req, v_rdy;
    wire [47:0] v_req_row;
    wire [3:0]  v_req_hi;
    wire [31:0] v_req_col;
    wire [15:0] v_rdata;
    wire        v_rvalid;
    wire [1:0]  v_rbank;
    wire [9:0]  vr, vg, vb;
    wire        vhs, vvs, vbl_n, vpix_stb, vframe_stb;
    wire [31:0] v_cmds;
    wire [29:0] rgb_t2;
    wire        err_v, ovf_v;

    /* ---- THE SOURCE.  Requests go to the source selected when they are issued; the two return streams are
     * merged.  src_v moves only while no request is outstanding, so the two can never return on one clock and no
     * request is ever answered by the wrong source (an unanswered request would stop vga_scan's fetcher). ---- */
    reg  [1:0] src_s = {SRC_RESET, SRC_RESET};
    reg        src_v = SRC_RESET;
    reg  [4:0] outst = 5'd0;
    always @(posedge vclk) begin
        src_s <= {src_s[0], src_req};
        outst <= outst + {4'd0, |v_req} - {4'd0, v_rvalid};
        if (vrst) outst <= 5'd0;
        if (outst == 5'd0 && !(|v_req)) src_v <= src_s[1];
    end
    wire [3:0]  m_rdy, f_rdy;
    wire [15:0] m_rdata, f_rdata;
    wire        m_rvalid, f_rvalid;
    wire [1:0]  m_rbank, f_rbank;
    assign v_rdy    = src_v ? f_rdy : m_rdy;
    assign v_rvalid = m_rvalid | f_rvalid;
    assign v_rdata  = f_rvalid ? f_rdata : m_rdata;
    assign v_rbank  = f_rvalid ? f_rbank : m_rbank;

    generate if (HAS_MCB != 0) begin : g_mcb
        vga_mcb_shim #(.BYTE_BASE(BYTE_BASE)) u_shim (
            .clk(vclk), .rst(vrst), .en(en_v[1]), .calib_done(cal_v[1]),
            .req(src_v ? 4'd0 : v_req), .req_row(v_req_row), .req_hi(v_req_hi), .req_col(v_req_col),
            .rdy(m_rdy), .rdata(m_rdata), .rvalid(m_rvalid), .rbank(m_rbank),
            .mcb_cmd_en(mcb_cmd_en), .mcb_cmd_instr(mcb_cmd_instr), .mcb_cmd_bl(mcb_cmd_bl),
            .mcb_cmd_byte_addr(mcb_cmd_byte_addr), .mcb_cmd_full(mcb_cmd_full),
            .mcb_rd_en(mcb_rd_en), .mcb_rd_data(mcb_rd_data), .mcb_rd_empty(mcb_rd_empty),
            .mcb_rd_overflow(mcb_rd_overflow), .mcb_rd_error(mcb_rd_error),
            .cmd_count(v_cmds), .err(err_v));
    end else begin : g_nomcb
        assign m_rdy = 4'd0; assign m_rdata = 16'd0; assign m_rvalid = 1'b0; assign m_rbank = 2'd0;
        assign mcb_cmd_en = 1'b0; assign mcb_cmd_instr = 3'd0; assign mcb_cmd_bl = 6'd0; assign mcb_cmd_byte_addr = 30'd0;
        assign mcb_rd_en = 1'b0; assign v_cmds = 32'd0; assign err_v = 1'b0;
    end endgenerate

    generate if (FB_BPP != 0) begin : g_fb
        vga_bram_fb #(.BPP(FB_BPP)) u_fb (
            .clk(clk), .fb_cyc(fb_cyc), .fb_we(fb_we), .fb_adr(fb_adr), .fb_sel(fb_sel), .fb_dat_w(fb_dat_w),
            .fb_dat_r(fb_dat_r), .fb_ack(fb_ack),
            .vclk(vclk), .en(en_v[1]), .req(src_v ? v_req : 4'd0), .req_row(v_req_row), .req_col(v_req_col),
            .rdy(f_rdy), .rdata(f_rdata), .rvalid(f_rvalid), .rbank(f_rbank));
    end else begin : g_nofb
        reg ack_n = 1'b0;
        always @(posedge clk) ack_n <= fb_cyc && !ack_n;
        assign fb_ack = ack_n; assign fb_dat_r = 32'd0;
        assign f_rdy = 4'd0; assign f_rdata = 16'd0; assign f_rvalid = 1'b0; assign f_rbank = 2'd0;
    end endgenerate

    vga_scan #(.AW(23), .LB_AW(LB_AW),
               .INC_M0({32'd0, 32'h5000_0000}),     // 25 MHz = 80 x  5/16
               .INC_M1({32'd0, 32'h8000_0000}),     // 40 MHz = 80 x  1/2
               .INC_M2({32'd0, 32'hD000_0000}),     // 65 MHz = 80 x 13/16
               .INC_M4({32'd0, 32'h156D_B6DB}),     // 6.696 MHz = 80 x 75/896 = (125 x 3/7) / 8: mode 4, four XCLK per pixel
               .INC_MD({32'd1, 32'd0}),             // the default arm (no mode reaches it)
               .MODE_RESET(3'd0)) u_scan (          // the page's reset mode: no reprogramming at start
        .mode(vga_mode), .mode_cur(scan_mode), .vr(vr), .vg(vg), .vb(vb),
        .v_hs(vhs), .v_vs(vvs), .v_blank_n(vbl_n), .pix_stb(vpix_stb), .pix_en(),
        .osd_x(), .osd_y(), .osd_active(),
        .rgb_t2(rgb_t2), .rgb_osd(rgb_t2), .frame_stb(vframe_stb),
        .fclk(vclk), .frst_n(!vrst), .init_done(src_v | cal_v[1]),
        .swap_tog(vga_tog), .swap_base(vga_base), .swap_w(vga_w), .swap_h(vga_h),
        .pal_en(vga_pal_en), .fmt565(vga_fmt565), .pal_we(vga_pal_we), .pclk(clk),
        .pal_wa(vga_pal_wa), .pal_wd(vga_pal_wd),
        .osd_en(1'b0), .osd_wtog(1'b0), .osd_wpos(8'd0), .osd_wchar(8'd0),
        .hw_osd_we(1'b0), .hw_osd_pos(8'd0), .hw_osd_char(8'd0), .fb_cur_base(vga_cur_base),
        .req(v_req), .req_we(), .req_row(v_req_row), .req_hi(v_req_hi), .req_col(v_req_col), .req_wdata(),
        .rdy(v_rdy), .rdata(v_rdata), .rvalid(v_rvalid), .rbank(v_rbank));

    // counters into clk: the frame strobe as a toggle, the command count in Gray code
    reg        ftog_v = 1'b0;
    reg [31:0] cmds_g = 32'd0;
    always @(posedge vclk) begin
        if (vframe_stb) ftog_v <= ~ftog_v;
        cmds_g <= v_cmds ^ (v_cmds >> 1);
    end
    reg [2:0]  ftog_c = 3'd0;
    reg [31:0] cg1 = 32'd0, cg2 = 32'd0;
    always @(posedge clk) begin
        ftog_c <= {ftog_c[1:0], ftog_v};
        if (rst) frames <= 32'd0; else if (ftog_c[2] ^ ftog_c[1]) frames <= frames + 32'd1;
        cg1 <= cmds_g; cg2 <= cg1;
    end
    assign vid_irq = ftog_c[2] ^ ftog_c[1];
    genvar gi;
    generate for (gi = 0; gi < 32; gi = gi + 1) begin : g_gray
        assign runs_c[gi] = ^cg2[31:gi];
    end endgenerate

    // ================================================================ output stage
    /* the output clock may stop while the DCM is reprogrammed: its reset asserts asynchronously and releases
     * two oclk edges after LOCKED */
    wire       olock_n = !dcm_locked || rst;
    (* equivalent_register_removal = "no" *) reg [1:0] orst_s = 2'b11;   // one per head (see ch7301_out.v)
    always @(posedge oclk or posedge olock_n)
        if (olock_n) orst_s <= 2'b11; else orst_s <= {orst_s[0], 1'b0};

    wire       run_v = rdy_v[1];                     // the clock is at this mode's frequency
    wire       unf_o, stream_o;
    ch7301_out u_out (
        .vclk(vclk), .vrst(vrst), .run(run_v), .rep4(rep4_v), .rep2(rep2_v), .pix_stb(vpix_stb),
        .r(en_v[1] ? vr[9:2] : 8'd0), .g(en_v[1] ? vg[9:2] : 8'd0), .b(en_v[1] ? vb[9:2] : 8'd0),
        .hs(vhs ^ h_inv), .vs(vvs ^ v_inv), .de(vbl_n),
        .oclk(oclk), .orst(orst_s[1]),
        .xclk_p(dvi_xclk_p), .xclk_n(dvi_xclk_n), .d(dvi_d), .h(dvi_h), .v(dvi_v), .de_o(dvi_de),
        .ovf(ovf_v), .unf(unf_o), .streaming(stream_o));

    reg [1:0] unf_c = 2'b00, str_c = 2'b00, calc = 2'b00, ovf_c = 2'b00, err_c = 2'b00;
    always @(posedge clk) begin
        unf_c <= {unf_c[0], unf_o}; str_c <= {str_c[0], stream_o}; calc <= {calc[0], mcb_calib_done};
        ovf_c <= {ovf_c[0], ovf_v}; err_c <= {err_c[0], err_v};
    end
    assign st_ovf     = ovf_c[1];
    assign st_mcb_err = err_c[1];
    reg       unf_sticky = 1'b0;                     /* unf itself is cleared by the output reset at a reprogramming */
    always @(posedge clk) if (rst) unf_sticky <= 1'b0; else if (unf_c[1]) unf_sticky <= 1'b1;
    assign st_unf    = unf_sticky;
    assign st_stream = str_c[1];
    assign st_calib  = calc[1];
    reg [1:0] srca_c = 2'b00;
    always @(posedge clk) srca_c <= {srca_c[0], src_v};
    assign src_act_c = srca_c[1];
endmodule
`default_nettype wire
