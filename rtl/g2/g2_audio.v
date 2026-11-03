/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// rtl/g2/g2_audio.v -- the WM8750BL playback path of the G2: a CPU-fed sample FIFO and the I2S transmitter.
//
// Page 0x8000_9000 (the vid_* register contract of rtl/g2/pano_soc_g2.v; word index aud_adr[3:0]):
//   0x00 CTRL    RW  [0] run: the transmitter takes frames; [1] flush (write 1: both FIFOs emptied, self-clearing);
//                    [2] wide: a frame is two DATA words, left then right, each a 24-bit sample left-justified in
//                    the word ({S[23:0], 8'd0}); 0: one word, {L[15:0], R[15:0]}, 16 bits each (STATUS and COUNT
//                    count words)
//                    R also [8] MCLK DCM locked
//   0x04 STATUS  R   [12:0] frames in the sample FIFO (0..2048), [15] full, [16] empty, [17] crossing FIFO full
//   0x08 DATA    W   one stereo frame {L[31:16], R[15:0]}, 16-bit two's complement each; dropped when full
//                    (counted in DROPS)
//   0x0C COUNT   R   frames handed to the transmitter since reset (clk domain)
//   0x10 UNDER   R   [15:0] underruns: LRCK periods with no frame (silence sent); [31:16] frames dropped at DATA
//   0x14 ID      R   0x4155_4431 ("AUD1")
//   0x18 WMARK   RW  [12:0] watermark, [31] interrupt enable; reset 0.  irq is high while the enable is set, run is
//                    set and the sample FIFO holds fewer frames than the watermark: firmware refills the FIFO from its
//                    interrupt handler instead of polling STATUS.
//
// Clocks.  MCLK is the 24.000 MHz global clock of the USB wrapper (rtl/g2/g2_usb_host.v: PLL_BASE on clk_125, the
// clock the USB3300 and USB2514 take at W12; running from configuration), through an ODDR2 to W14.  The codec runs in
// its USB clock mode with the MCLK halver on (R8: USB = 1, CLKDIV2 = 1, SR = 00000: 48 kHz), the clock mode of the G1
// driver (fw/codec.c) at twice the reference.  The transmitter runs in the 24 MHz domain: BCLK = MCLK / 10 = 2.4 MHz,
// LRCK = BCLK / 50 = 48 kHz, 24-bit I2S (R7 WL = 10, FORMAT = 10, MS = 0: the codec is the slave): LRCK changes on a
// BCLK falling edge, the MSB is presented on the next falling edge (the codec takes it on the second rising edge
// after the transition), data changes on falling edges.  No global clock buffer is added: the fabric has none free
// (the placer refused a fifteenth at f4734a0).  Frames cross from clk_sys to the 24 MHz domain through the
// Gray-pointer FIFO dp2/rtl/audio_ring.v (16 deep); the 2,048-frame sample FIFO in front of it is block RAM in the
// clk_sys domain, so the CPU refills it every 40 ms at the latest (2,048 / 48 kHz = 42.7 ms).
// Pins (docs/g2/g2_pins_reference.ucf): MCLK W14 (ODDR2 from mclk), BCLK AB13, DACLRC U6, DACDAT Y14.  The ADC side
// (ADCLRC W9, ADCDAT R13) is not driven: the G2 has no audio input jack.
`timescale 1ns / 1ps
`default_nettype none

module g2_audio (
    input  wire        clk,                 // clk_sys, 50 MHz
    input  wire        rst,                 // sys_rst, active high
    // register port, page 0x8000_9000
    input  wire        aud_cyc,
    input  wire        aud_we,
    input  wire [9:0]  aud_adr,
    input  wire [3:0]  aud_sel,
    input  wire [31:0] aud_dat_w,
    output reg  [31:0] aud_dat_r,
    output reg         aud_ack,
    output wire        irq,                 // FIFO level below WMARK (level, clk domain)
    // codec pins
    input  wire        mclk,                // 24 MHz global clock (u_usb clk24), running from configuration
    input  wire        mclk_locked,         // its PLL lock
    output wire        codec_mclk,
    output wire        codec_bclk,
    output wire        codec_daclrc,
    output wire        codec_dacdat
);
    // ------------------------------------------------------------------ MCLK to the pin
    wire dcm_locked = mclk_locked;
    ODDR2 #(.DDR_ALIGNMENT("NONE"), .INIT(1'b0), .SRTYPE("SYNC")) u_mclk_oddr (
        .Q(codec_mclk), .C0(mclk), .C1(~mclk), .CE(1'b1), .D0(1'b1), .D1(1'b0), .R(1'b0), .S(1'b0));

    // ------------------------------------------------------------------ registers (clk_sys)
    reg         run = 1'b0, flush = 1'b0;
    reg  [31:0] count = 32'd0;
    reg  [15:0] drops = 16'd0;
    reg  [1:0]  lock_s = 2'b00;
    reg  [15:0] under_s0 = 16'd0, under_s = 16'd0;   // the mclk-domain underrun count, two flops into clk
    always @(posedge clk) lock_s <= {lock_s[0], dcm_locked};

    // the sample FIFO: 2,048 frames of 32 bits, block RAM
    reg  [31:0] fifo [0:2047];
    reg  [11:0] wptr = 12'd0, rptr = 12'd0;
    reg  [31:0] fifo_q;
    wire [11:0] level = wptr - rptr;
    wire        full  = (level == 12'd2048);
    wire        empty = (level == 12'd0);
    wire        xfull;                      // the crossing FIFO cannot take a frame this clock
    reg         xwr = 1'b0;                 // push into the crossing FIFO
    reg         pop_q = 1'b0;

    wire wr_data = aud_cyc && aud_we && !aud_ack && (aud_adr[3:0] == 4'd2);
    wire wr_ctrl = aud_cyc && aud_we && !aud_ack && (aud_adr[3:0] == 4'd0);
    wire pop = run && !empty && !xfull && !pop_q && !flush;
    reg         wide = 1'b0;                // CTRL[2]: two DATA words a frame (left, right), 24-bit left-justified
    reg         flush_t = 1'b0;             // toggles on every flush: the mclk domain takes the edge
    reg  [12:0] wmark = 13'd0;              // WMARK[12:0]
    reg         wm_en = 1'b0;               // WMARK[31]
    wire wr_wmark = aud_cyc && aud_we && !aud_ack && (aud_adr[3:0] == 4'd6);
    assign irq = wm_en && run && ({1'b0, level} < wmark);

    always @(posedge clk) begin
        aud_ack <= aud_cyc && !aud_ack;
        flush   <= 1'b0;
        xwr     <= 1'b0;
        pop_q   <= pop;
        if (wr_ctrl) begin
            if (aud_sel[0]) begin run <= aud_dat_w[0]; flush <= aud_dat_w[1]; wide <= aud_dat_w[2]; end
        end
        if (wr_data) begin
            if (!full) begin fifo[wptr[10:0]] <= aud_dat_w; wptr <= wptr + 12'd1; end
            else drops <= drops + 16'd1;
        end
        if (pop) begin
            fifo_q <= fifo[rptr[10:0]];
            rptr   <= rptr + 12'd1;
            xwr    <= 1'b1;
            count  <= count + 32'd1;        // words (two a frame in the wide mode)
        end
        if (flush) begin wptr <= 12'd0; rptr <= 12'd0; flush_t <= ~flush_t; end
        if (wr_wmark) begin wmark <= aud_dat_w[12:0]; wm_en <= aud_dat_w[31]; end
        if (rst)   begin run <= 1'b0; wide <= 1'b0; wptr <= 12'd0; rptr <= 12'd0; count <= 32'd0; drops <= 16'd0;
                         wmark <= 13'd0; wm_en <= 1'b0; end
        case (aud_adr[3:0])
        4'd0: aud_dat_r <= {23'd0, lock_s[1], 5'd0, wide, 1'b0, run};
        4'd1: aud_dat_r <= {14'd0, xfull, empty, full, 2'd0, level};
        4'd3: aud_dat_r <= count;
        4'd4: aud_dat_r <= {drops, under_s};
        4'd5: aud_dat_r <= 32'h41554431;
        4'd6: aud_dat_r <= {wm_en, 18'd0, wmark};
        default: aud_dat_r <= 32'd0;
        endcase
    end

    // ------------------------------------------------------------------ the crossing FIFO (clk_sys -> mclk)
    /* the transmitter's reset: released four MCLK cycles after the PLL lock */
    reg  [3:0]  mrst_sh = 4'b0000;
    always @(posedge mclk) mrst_sh <= {mrst_sh[2:0], dcm_locked & ~rst};
    wire        mrst_n = mrst_sh[3];
    wire        rempty, rd;
    wire [31:0] rdata;
    reg         flush_m = 1'b0;             // flush seen in the mclk domain: drop what is queued there
    audio_ring #(.WIDTH(32), .DEPTH(16), .AW(4)) u_x (
        .wclk(clk),  .wrst_n(~rst & ~flush), .wr(xwr), .wdata(fifo_q), .wfull(xfull),
        .rclk(mclk), .rrst_n(mrst_n & ~flush_m), .rd(rd), .rdata(rdata), .rempty(rempty));

    // ------------------------------------------------------------------ the transmitter (mclk, 24 MHz)
    reg  [3:0]  div = 4'd0;                 // 0..9: one BCLK period
    reg         bclk = 1'b0, lrck = 1'b0, dacdat = 1'b0;
    reg  [5:0]  bcnt = 6'd0;                // 0..49: BCLK periods in one LRCK period, advanced on falling edges
    reg  [23:0] tx_sh = 24'd0, rhold = 24'd0;
    reg         dac_req = 1'b0;             // one clock: the frame in head goes out now
    reg  [31:0] head = 32'd0;               // the frame presented at the next dac_req ({L16, R16}; wide: the right word)
    reg  [23:0] head_l = 24'd0;             // wide: the left sample, taken one word ahead of head
    reg         half = 1'b0;                // wide: head_l holds the left word, the right one is next
    reg  [1:0]  wide_m = 2'b00;             // CTRL[2] in the mclk domain
    always @(posedge mclk) wide_m <= {wide_m[0], wide};
    reg         head_ok = 1'b0;             // head holds an unplayed frame
    reg  [15:0] under = 16'd0;
    /* the flush reaches the mclk domain as a toggle: a one-clock pulse at 50 MHz is shorter than an mclk period
     * (41.7 ns) and can be missed by a level synchroniser */
    reg  [2:0]  flush_s = 3'b000;
    always @(posedge mclk) begin flush_s <= {flush_s[1:0], flush_t}; flush_m <= flush_s[2] ^ flush_s[1]; end
    wire [23:0] dac_l = !head_ok ? 24'd0 : wide_m[1] ? head_l : {head[31:16], 8'd0};
    wire [23:0] dac_r = !head_ok ? 24'd0 : wide_m[1] ? head[31:8] : {head[15:0], 8'd0};
    wire        fe = (div == 4'd4);         // the clock before BCLK falls
    wire        re = (div == 4'd9);         // the clock before BCLK rises

    always @(posedge mclk or negedge mrst_n) begin
        if (!mrst_n) begin
            div <= 4'd0; bclk <= 1'b0; lrck <= 1'b0; dacdat <= 1'b0; bcnt <= 6'd0; tx_sh <= 24'd0; rhold <= 24'd0;
            dac_req <= 1'b0; under <= 16'd0;
        end else begin
            dac_req <= 1'b0;
            div <= re ? 4'd0 : div + 4'd1;
            if (re) bclk <= 1'b1;
            if (fe) begin
                bclk <= 1'b0;
                bcnt <= (bcnt == 6'd49) ? 6'd0 : bcnt + 6'd1;
                case (bcnt)
                6'd49: begin                              /* next: slot 0, LRCK low: the frame is taken */
                    lrck <= 1'b0; tx_sh <= dac_l; rhold <= dac_r; dac_req <= 1'b1; dacdat <= 1'b0;
                    if (!head_ok) under <= under + 16'd1;
                end
                6'd24: begin lrck <= 1'b1; tx_sh <= rhold; dacdat <= 1'b0; end   /* next: slot 25, LRCK high */
                default: begin dacdat <= tx_sh[23]; tx_sh <= {tx_sh[22:0], 1'b0}; end
                endcase
            end
        end
    end
    assign codec_bclk   = bclk;
    assign codec_daclrc = lrck;
    assign codec_dacdat = dacdat;

    /* the crossing FIFO shows its head on rdata before the pop (dp2/rtl/audio_ring.v): the frame is taken on the
     * same clock as rd */
    assign rd = !rempty && !head_ok;
    always @(posedge mclk or negedge mrst_n) begin
        if (!mrst_n) begin head <= 32'd0; head_l <= 24'd0; half <= 1'b0; head_ok <= 1'b0; end
        else begin
            if (dac_req) head_ok <= 1'b0;                      /* the frame in head went out on this pulse */
            if (rd) begin                                      /* last: a frame taken on a dac_req cycle is kept */
                if (wide_m[1] && !half) begin head_l <= rdata[31:8]; half <= 1'b1; end   /* the left word */
                else begin head <= rdata; head_ok <= 1'b1; half <= 1'b0; end            /* the frame is complete */
            end
            if (flush_m) begin head_ok <= 1'b0; half <= 1'b0; end
        end
    end

    always @(posedge clk) begin under_s0 <= under; under_s <= under_s0; end   // diagnostic: read as-is
endmodule
`default_nettype wire
