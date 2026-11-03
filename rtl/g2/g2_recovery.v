/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_recovery.v -- the three ways back to the golden (stock) image, page 0x8000_D000.
//
//   1. IPROG on command: a keyed register write reconfigures from IPROG_ADDR (default 0x040000, the golden image).
//   2. Fabric watchdog: enabled from reset with a 30 s timeout; on expiry it reconfigures from 0x040000.
//   3. Button at reset: the CPU is held in reset for 2^HOLD_LOG2 clocks (83.9 ms at 50 MHz); if the button (H12,
//      active low) reads pressed for 2^DEB_LOG2 consecutive clocks (1.31 ms) inside that window, the CPU stays
//      held and the design reconfigures from 0x040000.
// Every reconfiguration uses GENERAL3/4 = 0x040000 as the fallback (g2_icap_iprog.v).
//
// REGISTERS (word offsets from 0x8000_D000)
//   +0x00 WDT_CTRL   R  [0] EN  [1] EXPIRED
//                    W  0x5A5A_000e: EN <= e.  Any other value is ignored.
//   +0x04 WDT_LOAD   R/W timeout in clocks (values below 1024 act as 1024).  A write also restarts the count.
//   +0x08 WDT_KICK   W  0x4B49_434B restarts the count; any other value is ignored.   R  kicks accepted
//   +0x0C WDT_COUNT  R  clocks left
//   +0x10 IPROG      W  0x4950_5247 reconfigures from IPROG_ADDR.  Any other value is ignored.   R  requests issued
//   +0x14 IPROG_ADDR R/W [23:0] flash byte address of the image; reset 0x040000
//   +0x18 STATUS     R  [0] button pressed now (debounced)  [1] CPU held  [2] button seen in the reset window
//                       [4:3] cause of the last request: 1 command, 2 watchdog, 3 button
//                       [5] a request is in flight (ICAP sequence not yet complete)
//                       [31:16] ICAP sequences completed
//   +0x1C ID         R  0x4732_5243 ("G2RC")
// A request made while one is in flight is ignored.  On silicon the first completed sequence reconfigures the FPGA.
`timescale 1ns / 1ps
`default_nettype none

module g2_recovery #(
    parameter HOLD_LOG2   = 22,
    parameter DEB_LOG2    = 16,
    parameter [31:0] WDT_DEFAULT = 32'd1_500_000_000
) (
    input  wire        clk,
    input  wire        rst,                // system reset (PLL not locked)
    input  wire        reg_en,
    input  wire        reg_we,
    input  wire [2:0]  reg_adr,
    input  wire [31:0] reg_dat_w,
    output reg  [31:0] reg_dat_r,
    input  wire        button_n,           // pin level, active low, asynchronous
    output wire        cpu_hold,           // hold the CPU in reset
    output wire        button,             // debounced, active high
    // to g2_icap_iprog (clk_icap domain)
    output reg         icap_req_tog = 1'b0,
    output reg  [23:0] icap_addr = 24'h040000,
    input  wire        icap_done_tog
);
    localparam [31:0] K_WDT   = 32'h5A5A_0000;
    localparam [31:0] K_KICK  = 32'h4B49_434B;
    localparam [31:0] K_IPROG = 32'h4950_5247;

    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] btn_s = 2'b00;
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [2:0] done_s = 3'b000;
    reg [DEB_LOG2:0]  deb = 0;
    reg               btn = 1'b0;
    reg [HOLD_LOG2:0] hold = 0;
    reg               held = 1'b1;
    reg               btn_seen = 1'b0;
    reg               wdt_en = 1'b1, wdt_exp = 1'b0;
    reg [31:0]        wdt_load = WDT_DEFAULT, wdt_cnt = WDT_DEFAULT;
    reg [31:0]        kicks = 32'd0, reqs = 32'd0;
    reg [23:0]        iprog_addr = 24'h040000;
    reg [1:0]         cause = 2'd0;
    reg               busy = 1'b0;
    reg [15:0]        done_cnt = 16'd0;
    wire [31:0]       load_e = (wdt_load < 32'd1024) ? 32'd1024 : wdt_load;

    assign cpu_hold = held;
    assign button   = btn;

    always @(posedge clk) begin
        btn_s  <= {btn_s[0], !button_n};
        done_s <= {done_s[1:0], icap_done_tog};
        if (done_s[2] ^ done_s[1]) begin busy <= 1'b0; done_cnt <= done_cnt + 16'd1; end
        // debounce: the level must be stable for 2^DEB_LOG2 clocks
        if (btn_s[1] == btn) deb <= 0;
        else if (deb[DEB_LOG2]) begin btn <= btn_s[1]; deb <= 0; end
        else deb <= deb + 1'b1;

        if (rst) begin
            hold <= 0; held <= 1'b1; btn_seen <= 1'b0;
            wdt_en <= 1'b1; wdt_exp <= 1'b0; wdt_load <= WDT_DEFAULT; wdt_cnt <= WDT_DEFAULT;
            iprog_addr <= 24'h040000;
        end else begin
            // the reset window and the button
            if (held && !btn_seen) begin
                if (btn) begin
                    btn_seen <= 1'b1;
                    if (!busy) begin busy <= 1'b1; cause <= 2'd3; icap_addr <= 24'h040000; icap_req_tog <= ~icap_req_tog; reqs <= reqs + 32'd1; end
                end else if (hold[HOLD_LOG2]) held <= 1'b0;
                else hold <= hold + 1'b1;
            end
            // the watchdog
`ifdef G2_FAULT_WDT
            if (1'b0) begin
`else
            if (wdt_en && !wdt_exp) begin
`endif
                if (wdt_cnt != 32'd0) wdt_cnt <= wdt_cnt - 32'd1;
                else begin
                    wdt_exp <= 1'b1;
                    if (!busy) begin busy <= 1'b1; cause <= 2'd2; icap_addr <= 24'h040000; icap_req_tog <= ~icap_req_tog; reqs <= reqs + 32'd1; end
                end
            end
            // the register port
            if (reg_en && reg_we) begin
                case (reg_adr)
                3'd0: if (reg_dat_w[31:16] == K_WDT[31:16]) begin wdt_en <= reg_dat_w[0]; wdt_cnt <= load_e; end
                3'd1: begin wdt_load <= reg_dat_w; wdt_cnt <= (reg_dat_w < 32'd1024) ? 32'd1024 : reg_dat_w; end
                3'd2: if (reg_dat_w == K_KICK && !wdt_exp) begin wdt_cnt <= load_e; kicks <= kicks + 32'd1; end
                3'd4: if (reg_dat_w == K_IPROG && !busy) begin
                          busy <= 1'b1; cause <= 2'd1; icap_addr <= iprog_addr; icap_req_tog <= ~icap_req_tog; reqs <= reqs + 32'd1;
                      end
                3'd5: iprog_addr <= reg_dat_w[23:0];
                default: ;
                endcase
            end
        end
    end

    always @(*) begin
        case (reg_adr)
        3'd0:    reg_dat_r = {30'd0, wdt_exp, wdt_en};
        3'd1:    reg_dat_r = wdt_load;
        3'd2:    reg_dat_r = kicks;
        3'd3:    reg_dat_r = wdt_cnt;
        3'd4:    reg_dat_r = reqs;
        3'd5:    reg_dat_r = {8'd0, iprog_addr};
        3'd6:    reg_dat_r = {done_cnt, 10'd0, busy, cause, btn_seen, held, btn};
        default: reg_dat_r = 32'h4732_5243;
        endcase
    end
endmodule

`default_nettype wire
