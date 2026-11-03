/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// ch7301_out.v -- the video head's pixel stream to one Chrontel CH7301C, 12-bit data on both XCLK edges.
//
// Mike Wolak, EPROM Foundry Inc. -- Pano Logic G2 video output.
//
// 1 INPUT FORMAT (CH7301C data sheet 201-0000-056 rev 1.32, section 3.3, figure 4, table 4; register 1Ch = 00h:
//   XCM = 0, XCLK at the pixel rate, data latched on both edges; MCP = 0; register 1Fh IDF = 0):
//       first half of pixel n,  P0a = D[11:0] = {G[3:0], B[7:0]}   latched by the XCLK rising edge
//       second half,            P0b = D[11:0] = {R[7:0], G[7:4]}   latched by the XCLK falling edge
//   H, V and DE are sampled with the data (setup 0.5 ns, hold 0.5 ns to either XCLK edge, table 17).
//   XCLK range 25..165 MHz (table 17).  Blanked pixels carry RGB = 0 (section 3.3).
//
// 2 CLOCKING.  oclk runs at TWICE the pixel rate.  Every output goes through an ODDR2 (DDR_ALIGNMENT "C0",
//   C1 = the local inversion of oclk):
//     - D[11:0], H, V, DE: D0 = D1, so each pin changes only at oclk rising edges and holds for one oclk period;
//       D carries P0a for one oclk period, then P0b for one; H, V, DE hold for both.
//     - XCLK: D0/D1 = (0,1) in the P0a period and (1,0) in the P0b period (0,0 in reset), so XCLK changes only at oclk FALLING
//       edges -- it rises in the middle of P0a and falls in the middle of P0b.  Setup and hold at the pins are
//       each half an oclk period less the pin-to-pin skew: 3.85 ns at 130 MHz (65 MHz pixels), 10 ns at 50 MHz.
//     XCLK_N is the complement.  This is the same phase relation as a 90-degree XCLK against a pixel-rate data
//     clock, obtained from one clock.
//
// 3 RATE MATCHING.  vga_scan emits one pixel per pix_stb on vclk; its NCO increment is an exact dyadic fraction
//   and vclk and oclk are exact rational multiples of one 125 MHz reference, so the long-term pixel rates on the
//   two sides are equal and the FIFO level is bounded.  The FIFO (16 entries, Gray-coded pointers) starts
//   reading once it holds HALF entries after `run' rises; underflow and overflow are counted, never expected.
//   `run' low (unsupported mode, clock not locked, clock being reprogrammed) empties the FIFO and drives
//   DE = 0, RGB = 0; XCLK keeps running whenever oclk does.
`timescale 1ns / 1ps
`default_nettype none
// KEEP_HIERARCHY: the two heads' output stages are identical and run on one oclk; kept apart, XST does not merge their
// registers and logic, so each stage stays next to its own pins (ise/pano_g2.ucf AG_out1, AG_out2).
(* KEEP_HIERARCHY = "YES" *)
module ch7301_out #(
    parameter integer HALF = 8
) (
    // ---- head side (vclk) ----
    input  wire        vclk,
    input  wire        vrst,                       // synchronous to vclk
    input  wire        run,                        // synchronous to vclk
    input  wire        rep4,                       // synchronous to vclk: each pixel is repeated (modes 3, 4, 6 and 7, 15 kHz), four sends
    input  wire        rep2,                       // synchronous to vclk, with rep4: two sends instead of four (modes 3 and 7)
    input  wire        pix_stb,
    input  wire [7:0]  r, g, b,
    input  wire        hs, vs, de,

    // ---- output side (oclk = 2 x pixel clock) ----
    input  wire        oclk,
    input  wire        orst,                       // asynchronous assert, synchronous release (to oclk)

    // ---- CH7301C pins ----
    output wire        xclk_p,
    output wire        xclk_n,
    output wire [11:0] d,
    output wire        h,
    output wire        v,
    output wire        de_o,

    // ---- status ----
    output reg         ovf,                        // vclk: sticky, a pixel arrived with the FIFO full
    output reg         unf,                        // oclk: sticky, a pixel was due with the FIFO empty
    output reg         streaming                   // oclk
);
    // ---- FIFO storage, 16 x 27 (distributed RAM) ----
    reg  [26:0] mem [0:15];
    reg  [4:0]  wp = 5'd0, rp = 5'd0;              // binary, one wrap bit
    reg  [4:0]  wg = 5'd0, rg = 5'd0;              // Gray
    reg  [4:0]  rg_v1 = 5'd0, rg_v2 = 5'd0;        // read pointer in vclk
    reg  [4:0]  wg_o1 = 5'd0, wg_o2 = 5'd0;        // write pointer in oclk
    reg         run_o1 = 1'b0, run_o2 = 1'b0;
    integer     i;
    initial begin
        for (i = 0; i < 16; i = i + 1) mem[i] = 27'd0;
        ovf = 1'b0; unf = 1'b0; streaming = 1'b0;
    end

    function [4:0] g2b; input [4:0] gv; begin
        g2b[4] = gv[4];
        g2b[3] = g2b[4] ^ gv[3];
        g2b[2] = g2b[3] ^ gv[2];
        g2b[1] = g2b[2] ^ gv[1];
        g2b[0] = g2b[1] ^ gv[0];
    end endfunction

    // ---- write side ----
    wire [4:0] rp_v   = g2b(rg_v2);
    wire       w_full = (wp[3:0] == rp_v[3:0]) && (wp[4] != rp_v[4]);
    wire [4:0] wp1    = wp + 5'd1;
    always @(posedge vclk) begin
        rg_v1 <= rg; rg_v2 <= rg_v1;
        if (vrst || !run) begin
            wp <= 5'd0; wg <= 5'd0;
            if (vrst) ovf <= 1'b0;
        end else if (pix_stb) begin
            if (w_full) ovf <= 1'b1;
            else begin
                mem[wp[3:0]] <= {r, g, b, hs, vs, de};
                wp <= wp1;
                wg <= wp1 ^ (wp1 >> 1);
            end
        end
    end

    // ---- read side ----
    wire [4:0]  wp_o   = g2b(wg_o2);
    wire [4:0]  level  = wp_o - rp;
    wire        r_empty = (level == 5'd0) || (level > 5'd16);   /* > 16: the write side was just emptied by run falling */
    wire [26:0] rq     = mem[rp[3:0]];
    wire [4:0]  rp1    = rp + 5'd1;

    reg         ph = 1'b0;                          // 0: the next edge starts a pixel (P0a)
    reg  [11:0] d_q = 12'd0;
    reg         h_q = 1'b0, v_q = 1'b0, de_q = 1'b0;
    reg         xd0 = 1'b0, xd1 = 1'b0;             // XCLK for the two halves of this oclk period; both 0 in reset
    reg  [11:0] p0b = 12'd0;                        // the pending second half
    reg  [11:0] p0a_h = 12'd0;                      // the first half of the pixel being repeated (rep4)
    reg  [1:0]  rc = 2'd0;                          // repeats sent of the current pixel (rep4)
    reg         rep_o1 = 1'b0, rep_o2 = 1'b0, r2_o1 = 1'b0, r2_o2 = 1'b0;
    reg         h_l = 1'b0, v_l = 1'b0;             // last syncs, held through an underflow

    always @(posedge oclk or posedge orst) begin
        if (orst) begin
            wg_o1 <= 5'd0; wg_o2 <= 5'd0; run_o1 <= 1'b0; run_o2 <= 1'b0;
            rp <= 5'd0; rg <= 5'd0; ph <= 1'b0; streaming <= 1'b0;
            d_q <= 12'd0; h_q <= 1'b0; v_q <= 1'b0; de_q <= 1'b0; p0b <= 12'd0; xd0 <= 1'b0; xd1 <= 1'b0;
            h_l <= 1'b0; v_l <= 1'b0; unf <= 1'b0;
        end else begin
            wg_o1 <= wg; wg_o2 <= wg_o1;
            run_o1 <= run; run_o2 <= run_o1;
            rep_o1 <= rep4; rep_o2 <= rep_o1; r2_o1 <= rep2; r2_o2 <= r2_o1;   /* each crossing one flop, no logic */
            ph <= ~ph;
            if (!run_o2) begin
                rp <= 5'd0; rg <= 5'd0; streaming <= 1'b0; rc <= 2'd0;
            end else if (!streaming && ph == 1'b1 && level >= HALF[4:0]) begin
                streaming <= 1'b1;                  // the first pop lands on the next (ph == 0) edge
            end
            if (ph == 1'b0) begin
                xd0 <= 1'b0; xd1 <= 1'b1;   // XCLK rises mid-P0a
                if (streaming && run_o2 && rep_o2 && rc != 2'd0) begin
                    /* mode 4: the pixel popped rc periods ago is sent again; XCLK runs at four times the pixel
                     * rate (26.8 MHz, above the CH7301C's 25 MHz floor) and the raster is unchanged */
                    d_q <= p0a_h;
                    rc  <= (r2_o2 && rc == 2'd1) ? 2'd0 : rc + 2'd1;   /* rep2: one repeat, rep4: three */
                end else if (streaming && run_o2 && !r_empty) begin
                    /* entry = {R[26:19], G[18:11], B[10:3], hs, vs, de} */
                    d_q  <= rq[14:3];                         // P0a = {G[3:0], B[7:0]}
                    p0a_h <= rq[14:3];
                    p0b  <= rq[26:15];                        // P0b = {R[7:0], G[7:4]}
                    h_q  <= rq[2]; v_q <= rq[1]; de_q <= rq[0];
                    h_l  <= rq[2]; v_l <= rq[1];
                    rp   <= rp1; rg <= rp1 ^ (rp1 >> 1);
                    rc   <= rep_o2 ? 2'd1 : 2'd0;
                end else begin
                    if (streaming && run_o2) unf <= 1'b1;
                    d_q <= 12'd0; p0b <= 12'd0; de_q <= 1'b0; h_q <= h_l; v_q <= v_l; rc <= 2'd0;
                end
            end else begin
                xd0 <= 1'b1; xd1 <= 1'b0;   // XCLK falls mid-P0b
                d_q   <= p0b;
            end
        end
    end

    // ---- pins ----
    genvar k;
    generate for (k = 0; k < 12; k = k + 1) begin : g_d
        ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b0), .SRTYPE("ASYNC")) u_d (
            .Q(d[k]), .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(d_q[k]), .D1(d_q[k]), .R(1'b0), .S(1'b0));
    end endgenerate
    ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b0), .SRTYPE("ASYNC")) u_h  (.Q(h),    .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(h_q),  .D1(h_q),  .R(1'b0), .S(1'b0));
    ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b0), .SRTYPE("ASYNC")) u_v  (.Q(v),    .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(v_q),  .D1(v_q),  .R(1'b0), .S(1'b0));
    ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b0), .SRTYPE("ASYNC")) u_de (.Q(de_o), .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(de_q), .D1(de_q), .R(1'b0), .S(1'b0));
    ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b0), .SRTYPE("ASYNC")) u_xp (.Q(xclk_p), .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(xd0),  .D1(xd1),  .R(1'b0), .S(1'b0));
    ODDR2 #(.DDR_ALIGNMENT("C0"), .INIT(1'b1), .SRTYPE("ASYNC")) u_xn (.Q(xclk_n), .C0(oclk), .C1(~oclk), .CE(1'b1), .D0(~xd0), .D1(~xd1), .R(1'b0), .S(1'b0));
endmodule
`default_nettype wire
