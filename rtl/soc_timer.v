/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// soc_timer.v -- the STM32-style 32-bit timer, at 0x8000_2000 on the peripheral
// page.  A port of Mike Wolak's timer peripheral (an
// iCE40 RISC-V SoC of his), not a new design: the counting shape, the register
// map and the programming model are preserved.  What
// changed is written up below, each with the reason, because a port that
// quietly differs from its source is worse than one that differs loudly.
//
// PROGRAMMING MODEL (unchanged from the source):
//   +0x00  CR   [0] ENABLE, [1] ONE_SHOT
//   +0x04  SR   [0] UIF -- set when the counter reaches zero, WRITE 1 TO CLEAR
//   +0x08  PSC  prescaler; the tick rate is clk / (PSC + 1)
//   +0x0C  ARR  auto-reload; the update rate is the tick rate / (ARR + 1)
//   +0x10  CNT  the live down-counter, READ-ONLY
//
// so the update period is (PSC + 1) * (ARR + 1) core clocks, and at 50 MHz the
// 1 kHz millisecond tick is PSC = 49, ARR = 999.  The counter counts
// DOWN and the update happens on the tick that finds it at zero, which is why
// the +1s are there and why ARR = 0 is a legal (tick-rate) update.
//
// ---- THREE DELIBERATE DEVIATIONS FROM THE SOURCE -------------------------
//
// 1. THE INTERRUPT IS A LEVEL, NOT A ONE-CLOCK PULSE.  The source drives a
//    single-cycle irq_pulse, which is right for PicoRV32's edge-latched irq
//    input and WRONG here.  VexRiscv samples the line straight into mip:
//        VexRiscv_Full.v:5141   CsrPlugin_mip_MTIP <= timerInterrupt;
//    so a one-clock pulse sets MTIP for one clock, and if mstatus.MIE or
//    mie.MTIE happens to be closed at that instant -- inside a critical
//    section, or between the two csr writes that start the timer -- the tick
//    is GONE, with nothing anywhere recording that it was lost.  That is the
//    classic lost-tick bug and it is not reliably reproducible, which is the
//    worst property a bug can have.  Here the line IS the flag:
//        assign irq = sr_uif;
//    which is also the architectural model -- mip.MTIP is defined as level,
//    held by the device until software clears it at the source.  A handler
//    that returns without writing 1 to SR therefore re-enters immediately,
//    exactly as a real machine timer does, and the gate proves it.
//
// 2. PSC IS 32 BITS WIDE, NOT 16.  The source stores 16 and reads back
//    {16'h0, psc}, so a program that writes 0x0001_0000 gets 0 and a divide by
//    one instead of by 65,537, silently.  The rule in this project is
//    absolute: never narrow a software-visible register -- a 32-bit CPU gets
//    32-bit registers, and a narrow field truncates instead of erroring.  The
//    range only grows, so every value the source accepted behaves identically;
//    nothing that worked against the original stops working.
//
// 3. ON THE UIF RACE, THE HARDWARE SET WINS.  In the source the MMIO block is
//    written after the counting block, so a write-1-to-clear that lands in the
//    same clock as an update silently swallows that update -- one tick lost
//    per collision, and a 1 kHz tick colliding with its own handler's exit is
//    not a rare shape.  Here the set is applied last and wins: the flag stays
//    raised, the level stays asserted, and the handler is re-entered for the
//    tick it would otherwise have dropped.  Losing time is worse than serving
//    an interrupt twice, and the second is at least visible.
//
// XST 14.7: one clocked block, no functions, no loops, no part-select writes.
`timescale 1ns / 1ps
`default_nettype none

module soc_timer (
    input  wire        clk,
    input  wire        rst,              // ACTIVE HIGH, like lpddr_dma's wb_rst

    // the peripheral-page register port: a one-cycle strobe decoded upstream,
    // a combinational read that the SoC registers.  Identical in shape to
    // lpddr_dma's, deliberately -- one pattern for the whole page.
    input  wire        reg_en,
    input  wire        reg_we,
    input  wire [2:0]  reg_adr,          // word offset: 0=CR 1=SR 2=PSC 3=ARR 4=CNT
    input  wire [3:0]  reg_sel,          // byte lanes, honoured on writes
    input  wire [31:0] reg_dat_w,
    output wire [31:0] reg_dat_r,

    output wire        irq               // -> VexRiscv timerInterrupt (LEVEL)
);
    localparam [2:0] A_CR = 3'd0, A_SR = 3'd1, A_PSC = 3'd2, A_ARR = 3'd3, A_CNT = 3'd4;

    reg        cr_enable;
    reg        cr_one_shot;
    reg        sr_uif;
    reg [31:0] psc_value;
    reg [31:0] arr_value;
    reg [31:0] cnt_value;
    reg [31:0] psc_counter;

    wire psc_tick = (psc_counter == 32'd0);
    wire update   = cr_enable && psc_tick && (cnt_value == 32'd0);

    // A write reaches a register only through its own lane, so a byte store to
    // a peripheral does what a byte store means instead of clobbering the
    // other three lanes.  The console page ignores the lanes; that is a wart
    // there, not a precedent.
    wire wr = reg_en && reg_we;

    // Combinational, because the SoC registers per_q on the access cycle and
    // acknowledges the cycle after -- the same contract lpddr_dma is written to.
    assign reg_dat_r = (reg_adr == A_CR)  ? {30'd0, cr_one_shot, cr_enable} :
                       (reg_adr == A_SR)  ? {31'd0, sr_uif}                 :
                       (reg_adr == A_PSC) ? psc_value                       :
                       (reg_adr == A_ARR) ? arr_value                       :
                       (reg_adr == A_CNT) ? cnt_value                       : 32'd0;

    assign irq = sr_uif;

    always @(posedge clk) begin
        if (rst) begin
            cr_enable   <= 1'b0;
            cr_one_shot <= 1'b0;
            sr_uif      <= 1'b0;
            psc_value   <= 32'd0;
            arr_value   <= 32'd0;
            cnt_value   <= 32'd0;
            psc_counter <= 32'd0;
        end else begin
            // ---- counting: every clock the timer is enabled, independent of
            // the bus.  A peripheral whose time base stops when nobody is
            // talking to it is a peripheral that measures the bus, not time.
            if (cr_enable) begin
                if (psc_tick) begin
                    psc_counter <= psc_value;
                    if (cnt_value == 32'd0) begin
                        cnt_value <= arr_value;            // reload, both modes
                        if (cr_one_shot) cr_enable <= 1'b0;
                    end else begin
                        cnt_value <= cnt_value - 32'd1;
                    end
                end else begin
                    psc_counter <= psc_counter - 32'd1;
                end
            end

            // ---- the register file.  Written after the counting so that a
            // configuration write wins over the same clock's count -- the
            // source's order, and the right one: the program just said what it
            // wants.  The UIF SET is applied after this, below, because there
            // the opposite is true.
            if (wr) begin
                case (reg_adr)
                A_CR: if (reg_sel[0]) begin
                        cr_enable   <= reg_dat_w[0];
                        cr_one_shot <= reg_dat_w[1];
                        // 0 -> 1 on ENABLE arms the timer: the counter takes
                        // ARR and the prescaler takes PSC, so a start is from
                        // a known state and not from wherever a previous run
                        // stopped.  Re-writing ENABLE while already enabled
                        // does NOT reload -- that is the source's behaviour and
                        // it is what lets CR be used to change ONE_SHOT alone.
                        if (reg_dat_w[0] && !cr_enable) begin
                            cnt_value   <= arr_value;
                            psc_counter <= psc_value;
                        end
                      end
                A_SR: if (reg_sel[0] && reg_dat_w[0]) sr_uif <= 1'b0;   // write 1 to clear
                A_PSC: begin
                        if (reg_sel[0]) psc_value[7:0]   <= reg_dat_w[7:0];
                        if (reg_sel[1]) psc_value[15:8]  <= reg_dat_w[15:8];
                        if (reg_sel[2]) psc_value[23:16] <= reg_dat_w[23:16];
                        if (reg_sel[3]) psc_value[31:24] <= reg_dat_w[31:24];
                       end
                A_ARR: begin
                        if (reg_sel[0]) arr_value[7:0]   <= reg_dat_w[7:0];
                        if (reg_sel[1]) arr_value[15:8]  <= reg_dat_w[15:8];
                        if (reg_sel[2]) arr_value[23:16] <= reg_dat_w[23:16];
                        if (reg_sel[3]) arr_value[31:24] <= reg_dat_w[31:24];
                       end
                default: ;                                  // CNT is read-only
                endcase
            end

            // ---- THE UPDATE FLAG IS SET LAST, so it beats a write-1-to-clear
            // that lands in the same clock (deviation 3 in the header).  The
            // tick is not lost; the handler is simply re-entered for it.
            if (update) sr_uif <= 1'b1;
        end
    end
endmodule

`default_nettype wire
