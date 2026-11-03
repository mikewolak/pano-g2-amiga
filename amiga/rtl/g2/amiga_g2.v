/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// amiga_g2.v -- the Amiga subsystem of the Pano Logic G2 Amiga fabric: the NanoMig core (amiga/rtl/gen/nanomig.v)
// on a gated clock, its memory in DDR2 behind a cache, its companion modules (hid.v, sysctrl.v, osd_u8g2.v) and its
// sector port served by the SoC firmware, its picture captured into frame buffers in DDR2.  The ports are those of
// the subsystem slot (G2_SNES), so rtl/g2/pano_g2_top.v takes this module where it takes the first core of the subsystem slot: the register
// page at the RasterIX page, the cache on chip B port P1, the frame writer on chip B port P3.
//
// CLOCK GATE .  amiga_clk is clk_gate_src (the PLL output that clk's BUFG buffers) through a BUFGCE, so
// every amiga_clk edge is a clk edge.  An NCO on clk adds NCO_INC every clock; each carry owes the core one edge.  An
// owed edge is issued when run is set and the core is not waiting on memory: while a chip-memory access is current
// and its cache has not confirmed it, no edge is issued.  Owed edges accumulate during a hold (up to OWE_MAX) and are
// repaid at up to one edge per clk, so the long-term rate is the NCO rate and the machine's own timing is unchanged:
// the 68000, Agnus, Denise, Paula, the CIAs and the companion modules stop together.  Unlike the slot's first clock gate, edges may
// be consecutive (the core meets 20 ns on the XC6SLX100-2).
//   NCO_INC = f_amiga / f_clk x 2^32: NTSC 28.636364 MHz over 50 MHz = 0x929E_4149, PAL 28.375160 MHz = 0x9147_E3C4.
//
// DOMAINS.  Every signal of the core domain (amiga_clk) changes only at an amiga_clk edge, which is a clk edge; every
// signal this module passes from clk into the core domain is a register of clk that the core samples at its next edge,
// one or more clk periods later.  Events cross as toggles: the receiving side compares the toggle with its own copy.
//
// CHIP MEMORY.  The core's chip-memory port (chip, slow and Kickstart RAM, word address ram_address[23:1]) is one
// 16 MB byte space at DDR_BASE behind one amiga_cache.  Requests are taken as top.sv of the Tang Nano 20K build does:
// an access while _ram_oe or _ram_we is low; Kickstart (word address bits 22:19 all 1) is not written.  The cache
// takes the request and its write data registered one clk late (the slot's first core registers the request only; its edges are
// at least two clocks apart), so no path runs from the core's address logic into the cache's block RAM within one
// clock; the edge after a request starts is issued against the previous request's confirmation, which is safe because
// the core consumes read data three edges after its request starts (minimig_sram_bridge.v: the access spans Q1 to Q3
// of the 7 MHz cycle), and a commit of the previous write in that clock rewrites the previous data.  Turbo modes are
// forced off: they route the 68000 to chip memory through the fast-RAM port, which this cache would not see.  The
// fast-RAM port is served by its own cache (FAST RAM below).  Firmware loads Kickstart into DDR2 at
// DDR_BASE + 0x78_0000 with the two bytes of every word exchanged (amiga_cache.v WORD LAYOUT) and invalidates the
// cache, with the core stopped.
//
// COMPANION.  NanoMig's companion MCU talks to hid.v (keyboard, mouse, joysticks), sysctrl.v (machine settings and
// interrupts) and osd_u8g2.v (the menu overlay) over mcu_spi.v; here the firmware writes the same byte stream into
// MCU_TX: {start[16], target[9:8] (0 sys, 1 hid, 2 osd), byte[7:0]}.  Each write becomes one data_in_strobe of that
// target in the core domain, with data_in_start = start; MCU_TX reads {busy[31], data_out of the last target[7:0]}.
// Firmware writes the next byte when busy reads 0.  sysctrl's interrupt (int_out_n low) is IRQ bit 2.
//
// SECTOR PORT.  The core's floppy (and Gayle IDE) requests sectors on its sdc_* port (rtl/src/paula_floppy_ebr.v);
// sim/sdc_model.v serves it in simulation.  Here: a request (sdc_rd or sdc_wr: bits 3:0 floppy drives 0 to 3, bits 5:4
// IDE drives 0 and 1) raises busy at once and
// SDC_REQ reads {pending[31] (until SDC_GO is written), done[30], write[16], drive mask[7:0]} and SDC_SECTOR the
// sector number; firmware fills
// the 512-byte buffer (SDC_DATA writes, one byte each, from index 0) for a read and writes SDC_GO; the core domain
// then presents the buffer one byte every 4 core edges (byte_in_strobe) or, for a write, takes byte_out_data into
// the buffer, lowers busy and sets done; firmware reads a written sector back through SDC_DATA (index reset by
// SDC_GO).  SDC_MOUNT {drive mask[7:0]} with SDC_SIZE gives the core a one-edge img_mounted pulse, from core edge
// 100 after reset on.  With sdc_hold
// (CTRL bit 2, set at reset) the gate also withholds core edges while a request waits for SDC_GO, so a sector
// arrives at once in the core's time (as from sim/sdc_model.v); each such wait costs owed edges (OWE_MAX).
//
// VIDEO.  The native picture before the scan doubler (nanomig.v vid_*) passes osd_u8g2.v and is sampled at every
// second core edge (14 MHz: hires pixels once, lowres pixels twice) on 15 kHz lines and at every core edge on 31 kHz
// lines (the line rate measured per line), outside hblank and vblank, from sample HOFS (VID, or VID_FAST on 31 kHz
// lines) of each line, W samples a line (VID), RGB565, into a two-line buffer; each finished line is written to DDR2 chip B
// through P3 at line pitch 2 x W bytes in one of three frame buffers (FB_BASE + n x 0x10_0000), and also 16 MB higher
// when the mirror bit is set.  The buffer rule and FRAME_INFO are the subsystem slot's.  Interlaced frames place line y at
// 2y + field.
//
// AUDIO.  An NCO in core time (AUD_INC per core edge) takes {left, right} (Paula's 15-bit outputs, doubled to 16 bits)
// into a 1,024-entry FIFO that firmware reads at AUD_DATA (the subsystem slot's FIFO); NTSC 48 kHz is AUD_INC 0x006D_D9CF.
//
// REGISTER PAGE (4 KB, word offsets; clk domain; ack one clock after cyc)
//   0x000 R   ID 0x414D_4947 ("AMIG")
//   0x001 RW  CTRL {aud_irq_en[7], frm_irq_en[6], mirror[5], ser_irq_en[3], sdc_hold[2], core_reset[1], run[0]};
//             reset 0x0000_0006
//   0x002 R   STATUS {head_sel[16], lace[9], field[8], chip_flush_busy[0]}
//   0x003 W   CACHE {audio_clear[3], flush[1], invalidate[0]} (flush and invalidate with run 0)
//   0x007 RW  HEAD_SEL[0]
//   0x008 RW  NCO_INC, reset 0x929E_4149 (NTSC)
//   0x009 RW  AUD_INC, reset 0x006D_D9CF (48 kHz in NTSC core time)
//   0x00B RW  FB_BASE[24:0], byte offset in chip B, reset 0x0080_0000
//   0x00C RW  VID {hofs[26:16], w[10:0]} (w even, up to 1,024), reset w 768, hofs 0 (the NTSC picture with its
//             border is about 754 samples wide)
//   0x00D R   FRAMES completed
//   0x00E R   FRAME_INFO {fast[26], lines[25:16], lace[2], buffer[1:0]} (fast: the frame's lines are 31 kHz lines)
//   0x00F RW  IRQ pending {serial[3] (a byte from Paula waits), mcu[2], audio[1], frame[0]}; write 1 to bit 0 to
//             clear the frame bit
//   0x010 R   TS_NOW (clk cycles)   0x011 R TS_FRAME   0x012 R TS_HEAD
//   0x013 R   AUD_DATA {L[31:16], R[15:0]}, the 18-bit samples saturated to 16; each read pops one sample
//   0x017 R   AUD_WL: the left 18-bit sample, sign-extended (no pop)   0x019 R AUD_WR: the right one; pops
//   0x014 R   AUD_LEVEL {overflow[16], level[10:0]}   0x015 RW AUD_WMARK[10:0], reset 256
//   0x016 R   chip misses   0x018 R chip write-backs   0x01A R held clocks   0x01B R {lost[31:16], owed[15:0]}
//   0x01C RW  DISP[1:0]: the buffer the heads scan
//   0x01D R   fast misses   0x01E R fast write-backs (the fast-RAM cache, FAST RAM below)
//   0x01F R   CAPS {[0] 18-bit audio samples at AUD_WL and AUD_WR}; a fabric without it reads 0
//   0x020 RW  MCU_TX (COMPANION)
//   0x024 R   SDC_REQ {pending[31], done[30], write[16], drive mask[7:0]}   0x025 R SDC_SECTOR
//   0x026 W   SDC_GO (any value)   0x027 RW SDC_DATA (byte; auto-increment index)
//   0x028 W   SDC_MOUNT {drive mask[7:0]}   0x029 RW SDC_SIZE[31:0] (image bytes, for the next mount)
//   0x02E RW  VID_FAST hofs[10:0]: VID's hofs for 31 kHz lines (in 28 MHz samples), reset 0
//   0x02F R   LINE_DROPS[15:0]: captured lines lost because the writer still held the previous one
//   0x030-0x033 SERIAL: Paula's serial port as bytes (amiga_serbridge.v and the core's uart_port.v, built with
//             ENABLE_AT_MODEM): Paula's bits are sampled and generated on the core's own clock, its transmitter is
//             held while the bytes have nowhere to go and a byte is sent to it only when its receive buffer is empty,
//             so nothing is lost or timed by the clock gate.  0x030 R RX {[8] valid, [7:0] byte from Paula}, a read
//             pops it (FIFO 1,023 bytes); 0x031 R {[8] ready} W [7:0] a byte for Paula (FIFO 255 bytes); 0x032 R
//             {[8] TX overflow, [7:0] TX FIFO fill}; 0x033 R Paula's SERPER[14:0].  IRQ bit 3 while a byte from Paula
//             waits.  The firmware moves the bytes to and from a USB-MIDI or USB serial device
`timescale 1ns / 1ps
`default_nettype none

module amiga_g2 #(
    parameter integer SET_LOG2 = 9,                // chip cache: 2 ways x 2^SET_LOG2 x 64 bytes (9: 64 KB)
    parameter [29:0]  DDR_BASE = 30'h0200_0000,    // MCB byte address of chip-memory byte address 0 (chip B)
    parameter integer FSET_LOG2 = 8,               // fast-RAM cache: 2 ways x 2^FSET_LOG2 x 64 bytes (8: 32 KB)
    parameter [29:0]  FAST_BASE = 30'h0300_0000,   // MCB byte address of fast-RAM byte address 0 (chip A)
    parameter [11:0]  FAST_LINE = 12'd1362         // core clocks: a shorter line is a 31 kHz line (VIDEO)
) (
    input  wire        clk,
    input  wire        clk_gate_src,
    input  wire        rst,

    // register page (the subsystem slot's port)
    input  wire        cyc,
    input  wire        we,
    input  wire [9:0]  adr,
    input  wire [3:0]  sel,
    input  wire [31:0] dat_w,
    output reg  [31:0] dat_r,
    output reg         ack,
    output wire        irq,
    input  wire        head1_frame,
    input  wire        head2_frame,

    // MCB chip B port P1: the chip cache
    output wire        c_cmd_en,
    output wire [2:0]  c_cmd_instr,
    output wire [5:0]  c_cmd_bl,
    output wire [29:0] c_cmd_byte_addr,
    input  wire        c_cmd_full,
    output wire        c_wr_en,
    output wire [3:0]  c_wr_mask,
    output wire [31:0] c_wr_data,
    input  wire        c_wr_empty,
    output wire        c_rd_en,
    input  wire [31:0] c_rd_data,
    input  wire        c_rd_empty,
    input  wire        mcb_ready,                  // chip B calibrated (any domain; synchronised here): the cache may issue commands

    // MCB chip B port P3: the frame writer
    output reg         v_cmd_en,
    output wire [2:0]  v_cmd_instr,
    output wire [5:0]  v_cmd_bl,
    output reg  [29:0] v_cmd_byte_addr,
    input  wire        v_cmd_full,
    output reg         v_wr_en,
    output wire [3:0]  v_wr_mask,
    output wire [31:0] v_wr_data,
    input  wire        v_wr_empty,
    input  wire [6:0]  v_wr_count,

    // MCB chip A port P1: the fast-RAM cache
    output wire        f_cmd_en,
    output wire [2:0]  f_cmd_instr,
    output wire [5:0]  f_cmd_bl,
    output wire [29:0] f_cmd_byte_addr,
    input  wire        f_cmd_full,
    output wire        f_wr_en,
    output wire [3:0]  f_wr_mask,
    output wire [31:0] f_wr_data,
    input  wire        f_wr_empty,
    output wire        f_rd_en,
    input  wire [31:0] f_rd_data,
    input  wire        f_rd_empty,
    input  wire        f_mcb_ready,                // chip A calibrated (any domain; synchronised here)

    // simulation and test: the core clock and the scan-doubled picture of nanomig.v
    output wire        dbg_amiga_clk,
    output wire        dbg_hs, dbg_vs,
    output wire [3:0]  dbg_r, dbg_g, dbg_b
);
    // ================================================================================================================
    // registers (written below)
    // ================================================================================================================
    reg         run, core_reset, mirror, frm_irq_en, aud_irq_en, head_sel, sdc_hold, ser_irq_en;
    reg  [31:0] nco_inc, aud_inc;
    reg  [24:0] fb_base;
    reg  [10:0] vid_w, vid_hofs, vid_hofs_fast;
    reg  [10:0] aud_wmark;
    reg  [1:0]  disp;
    reg         c_inv, c_flush;
    reg  [31:0] frames, frame_info, ts_now, ts_frame, ts_head;
    reg         irq_frame;

    // ================================================================================================================
    // clock gate 
    // ================================================================================================================
    localparam [15:0] OWE_MAX = 16'd8192;
    reg  [31:0] nco;
    reg  [15:0] owed, lost;
    reg  [31:0] held;
    reg         ce_r;
    wire        amiga_clk;
    wire [32:0] nco_n = {1'b0, nco} + {1'b0, nco_inc};
    wire        carry = nco_n[32] & run;
    wire        hold;
    wire        can   = run & ((owed != 16'd0) | carry);
    wire        emit  = can & ~hold;
    always @(posedge clk) begin
        if (rst) begin nco <= 32'd0; owed <= 16'd0; lost <= 16'd0; ce_r <= 1'b0; held <= 32'd0; end
        else begin
            nco  <= nco_n[31:0];
            ce_r <= emit;
            if (can & hold) held <= held + 32'd1;
            case ({carry, emit})
            2'b10: if (owed != OWE_MAX) owed <= owed + 16'd1; else if (lost != 16'hFFFF) lost <= lost + 16'd1;
            2'b01: owed <= owed - 16'd1;
            default: ;
            endcase
            if (!run) owed <= 16'd0;
        end
    end
    BUFGCE u_bgce (.I(clk_gate_src), .CE(ce_r), .O(amiga_clk));
    // gap_r: the next core edge (ce_r now) comes two or more clk periods after the previous one (ce_r was clear a clk
    // ago).  A clk register the core samples one clk later (DOMAINS); the TG68K's pacing uses it (cpu_wrapper.v).
    reg         gap_r = 1'b0;
    always @(posedge clk) gap_r <= rst ? 1'b0 : ~ce_r;
    assign dbg_amiga_clk = amiga_clk;

    // ================================================================================================================
    // reset and the companion modules with their byte bridge
    // ================================================================================================================
    // after core_reset falls, por is released at core edge 16 and reset at core edge 2000 (sim/tb_amiga_boot.v)
    reg  [11:0] rcnt = 12'd0;
    reg         por_r = 1'b1, reset_r = 1'b1;
    wire        sys_reset;                         // sysctrl.v system_reset: the menu's reset
    wire        kbd_reset;                         // hid.v: Ctrl-Amiga-Amiga
    always @(posedge amiga_clk) begin
        if (core_reset) begin rcnt <= 12'd0; por_r <= 1'b1; reset_r <= 1'b1; end
        else begin
            if (rcnt != 12'hFFF) rcnt <= rcnt + 12'd1;
            if (rcnt == 12'd16)   por_r <= 1'b0;
            if (rcnt == 12'd2000) reset_r <= 1'b0;
        end
    end
    wire        core_rst = reset_r | sys_reset | kbd_reset;

    // MCU_TX (clk) -> one strobe in the core domain
    reg         tx_tog = 1'b0, tx_tog_c = 1'b0;
    reg  [1:0]  tx_target;  reg tx_start;  reg [7:0] tx_byte;
    wire        tx_busy = tx_tog ^ tx_tog_c;
    reg         mcu_strobe_c = 1'b0;  reg [1:0] mcu_target_c = 2'd0;
    always @(posedge amiga_clk) begin
        mcu_strobe_c <= 1'b0;
        if (tx_tog != tx_tog_c) begin mcu_strobe_c <= 1'b1; mcu_target_c <= tx_target; tx_tog_c <= tx_tog; end
    end
    wire        mcu_sys_strobe = mcu_strobe_c & (mcu_target_c == 2'd0);
    wire        mcu_hid_strobe = mcu_strobe_c & (mcu_target_c == 2'd1);
    wire        mcu_osd_strobe = mcu_strobe_c & (mcu_target_c == 2'd2);
    wire [7:0]  sys_data_out, hid_data_out;
    wire [7:0]  mcu_rx = (tx_target == 2'd0) ? sys_data_out : (tx_target == 2'd1) ? hid_data_out : 8'h55;

    wire [7:0]  int_ack;
    wire        hid_int, sys_int_n;
    wire [2:0]  mouse_buttons;
    wire        kbd_mouse_level;
    wire [1:0]  kbd_mouse_type;
    wire [7:0]  kbd_mouse_data, hid_joy0, hid_joy1;
    wire        keyboard_ack;
    hid u_hid (
        .clk(amiga_clk), .reset(por_r),
        .data_in_strobe(mcu_hid_strobe), .data_in_start(tx_start), .data_in(tx_byte), .data_out(hid_data_out),
        .db9_port(6'd0), .irq(hid_int), .iack(int_ack[1]),
        .mouse_buttons(mouse_buttons), .kbd_mouse_level(kbd_mouse_level), .kbd_mouse_type(kbd_mouse_type),
        .kbd_mouse_data(kbd_mouse_data), .keyboard_ack(keyboard_ack), .kbd_reset(kbd_reset), .vsync_n(dbg_vs),
        .joystick0(hid_joy0), .joystick1(hid_joy1));

    wire [1:0]  s_drives, s_cpu, s_screen, s_filter, s_scan, s_chipmem, s_slowmem, s_fastmem, s_kick;
    wire        s_wrprot, s_fturbo, s_vmode, s_ide, s_swap, s_stereo, s_dsounds;
    wire [2:0]  s_chipset, s_turbo, s_volume;
    wire [11:0] rtc;                               // sysctrl.v command 10 to minimig.v's MSM6242 registers ($DC0000)
    // G2_AMIGA_AGA: sysctrl.v's AGA parameter lets the chipset setting ("C" 3) select AGA (chipset_config 6) and makes
    // AGA the setting's reset value; without it the chipset is OCS or ECS and the AGA logic is constant-disabled
`ifdef G2_AMIGA_AGA
    sysctrl #(.AGA(1)) u_sys (
`else
    sysctrl u_sys (
`endif
        .clk(amiga_clk), .reset(por_r),
        .data_in_strobe(mcu_sys_strobe), .data_in_start(tx_start), .data_in(tx_byte), .data_out(sys_data_out),
        .int_out_n(sys_int_n), .int_in({6'd0, hid_int, 1'b0}), .int_ack(int_ack),
        .buttons(2'b00), .leds(), .color(), .jtagsel(), .rtc(rtc),
        .system_reset(sys_reset), .system_floppy_drives(s_drives), .system_floppy_wrprot(s_wrprot),
        .system_floppy_turbo(s_fturbo), .system_cpu(s_cpu), .system_chipset(s_chipset), .system_video_mode(s_vmode),
        .system_video_screen(s_screen), .system_ide_enable(s_ide), .system_video_filter(s_filter),
        .system_video_scanlines(s_scan), .system_chipmem(s_chipmem), .system_slowmem(s_slowmem),
        .system_fastmem(s_fastmem), .system_turbo(s_turbo), .system_joy_swap(s_swap), .system_volume(s_volume),
        .system_stereo_mix(s_stereo), .system_kickstart(s_kick), .system_drive_sounds(s_dsounds),
        .system_lcd_v_pos());

    // ================================================================================================================
    // the core (configuration packed as top.sv packs it).  Turbo: the G2_AMIGA_AGA core has the CPU cache
    // (ENABLE_CACHE, FAST RAM below), and of sysctrl.v's turbo setting ("Z") only turbo kick is passed: the CPU then
    // fetches Kickstart instructions through the fast-RAM port, from the copy firmware places at fast-RAM byte address
    // 0x78_0000 (rtl/src/cpu_wrapper.v maps $F8_0000-$FF_FFFF there, outside the Zorro II boards' 0x20_0000-0x5F_FFFF
    // and 0x80_0000-0xBF_FFFF).  Turbo chip and turbo data stay off: they would route chip and slow memory through the fast-RAM port,
    // which reaches chip A, not the chip memory in chip B.
    // ================================================================================================================
    wire [15:0] ram_dout, ram_din;
    wire [47:0] chip48;                            // the rest of the 8-byte group of the word read (AGA wide fetch)
    wire [23:1] ram_a;
    wire        ram_bhe_n, ram_ble_n, ram_we_n, ram_oe_n, ram_refresh;
    wire [7:0]  joystick0 = s_swap ? hid_joy0 : hid_joy1;
    wire [7:0]  joystick1 = s_swap ? hid_joy1 : hid_joy0;
    wire [7:0]  sdc_img_mounted;  wire [63:0] sdc_img_size;
    wire [7:0]  sdc_rd, sdc_wr;   wire [31:0] sdc_sector;
    wire        sdc_busy, sdc_done, sdc_byte_in_strobe;
    wire [8:0]  sdc_byte_addr;    wire [7:0] sdc_byte_in_data, sdc_byte_out_data;
    wire [7:0]  vr, vg, vb;
    wire        vhs, vvs, vhbl, vvbl, vce, vfield, vlace;
    wire [1:0]  vres;
    wire [14:0] audio_left, audio_right;
`ifdef G2_AMIGA_AGA
    wire [15:0] toc_l, toc_r;                      // the Toccata card (rtl/gen/nanomig_020.v, ENABLE_TOCCATA)
    wire [7:0]  aux2_l, aux2_r;                    // its AD1848 AUX2 control: the Paula level (rtl/src/toccata.sv)
`endif
    wire        fr_sel, fr_lds, fr_uds, fr_wr;     // the core's fast-RAM port (FAST RAM below)
    wire [23:1] fr_addr;  wire [15:0] fr_din;
`ifdef G2_AMIGA_AGA
    // the X-Surf 100 board's host mailbox (xsurf_mbx.v, X-SURF MAILBOX below)
    wire        xm_req, xm_write, xm_wide, xm_uds, xm_lds, xm_ready;
    wire [15:1] xm_addr;  wire [15:0] xm_wdata, xm_rdata;  wire [63:0] xm_wdata64, xm_rdata64;
    reg  [15:1] mbx_a = 15'd0;
    reg  [47:0] xs_mac = 48'h5254_0504_0302;      // XS_MAC_HI/LO: the board's station address, written before a start
    reg         mbx_we = 1'b0;  reg [15:0] mbx_wd = 16'd0;
    wire [15:0] mbx_q;
`endif
    reg  [15:0] fr_q = 16'd0;
    reg  [47:0] fr_q48 = 48'd0;                    // the other three words of fr_q's aligned 8-byte group
    reg         fast_tog = 1'b0;

    // SERIAL: Paula's serial port as bytes on the register page (0x030-0x033)
    wire [31:0] midi_q;
    wire        irq_ser;
    wire [7:0]  ser_tx_av, ser_tx_dat, ser_rx_av, ser_rx_dat;
    wire        ser_tx_stb, ser_rx_stb;
    wire [14:0] ser_serper;
    wire [31:0] ser_status;
    wire       ser_rts_n, ser_dtr_n;               // CIA-B's RS-232 outputs (0: asserted)
    wire [3:0] ser_ctl_n;                          // {RI, CD, DSR, CTS} for CIA-B
    // the bridge's core-side byte ports: Paula's uart_port.v, or in FIFO mode the G2 serial FIFO board
    // (rtl/src/g2ser_fifo.v, G2_AMIGA_AGA), whose driver then sets the rate (g2s_baud) instead of Paula's SERPER
    wire [7:0]  br_tx_av, br_tx_dat, br_rx_av;
    wire        br_tx_stb, br_rx_stb;
`ifdef G2_AMIGA_AGA
    wire        g2s_mode;
    wire [31:0] g2s_baud;
    wire [7:0]  g2s_tx_av, g2s_tx_dat, g2s_rx_av;
`else
    wire        g2s_mode = 1'b0;
    wire [31:0] g2s_baud = 32'd0;
    wire [7:0]  g2s_tx_av = 8'd0, g2s_tx_dat = 8'd0, g2s_rx_av = 8'd0;
`endif
    assign br_tx_av  = g2s_mode ? g2s_tx_av  : ser_tx_av;
    assign br_tx_dat = g2s_mode ? g2s_tx_dat : ser_tx_dat;
    assign br_rx_av  = g2s_mode ? g2s_rx_av  : ser_rx_av;
    assign ser_tx_stb = br_tx_stb && !g2s_mode;
    assign ser_rx_stb = br_rx_stb && !g2s_mode;
    wire   g2s_tx_stb = br_tx_stb && g2s_mode;
    wire   g2s_rx_stb = br_rx_stb && g2s_mode;
    amiga_serbridge #(.RXW(10)) u_midi (
        .clk(clk), .rst(rst), .reg_en(cyc && !ack && adr[9:2] == 8'h0C), .reg_we(we), .reg_adr(adr[1:0]),
        .reg_dat_w(dat_w[7:0]), .reg_dat_r(midi_q), .irq(irq_ser),
        .aclk(amiga_clk), .tx_available(br_tx_av), .tx_strobe(br_tx_stb), .tx_data(br_tx_dat),
        .rx_available(br_rx_av), .rx_strobe(br_rx_stb), .rx_data(ser_rx_dat), .serper(ser_serper),
        .fifo_mode(g2s_mode), .baud(g2s_baud),
        .rts_n(ser_rts_n), .dtr_n(ser_dtr_n), .ctl_n(ser_ctl_n)
    );

    nanomig u_core (
        .clk_sys(amiga_clk), .reset(core_rst), .por(por_r), .rtc(rtc),
        .clk7_en(), .clk7n_en(), .cpu_nrst_out(),
        .pwr_led(), .fdd_led(), .fdd_step_n(),
        .hs(dbg_hs), .vs(dbg_vs), .r(dbg_r), .g(dbg_g), .b(dbg_b),
        .vid_r(vr), .vid_g(vg), .vid_b(vb), .vid_hs(vhs), .vid_vs(vvs), .vid_hbl(vhbl), .vid_vbl(vvbl), .vid_ce(vce),
        .vid_res(vres), .vid_field(vfield), .vid_lace(vlace),
        .memory_config({4'b0000, s_slowmem, s_chipmem}), .fastram_config({1'b0, s_fastmem}), .cpu_config(s_cpu),
        .chipset_config({1'b0, s_chipset, s_vmode, 1'b0}),
        .floppy_config({s_drives, s_wrprot, s_fturbo}), .video_config({s_filter, s_scan}),
`ifdef G2_AMIGA_AGA
        .turbo_config(s_turbo & 3'b010),
`else
        .turbo_config(3'b000),
`endif
        .ide_config({5'b10000, s_ide}), .hdd_led(),
        .audio_left(audio_left), .audio_right(audio_right),
        .mouse_buttons(mouse_buttons), .kbd_mouse_level(kbd_mouse_level), .kbd_mouse_type(kbd_mouse_type),
        .kbd_mouse_data(kbd_mouse_data), .joystick0(joystick0), .joystick1(joystick1), .keyboard_ack(keyboard_ack),
        .uart_tx(), .uart_rx(1'b1),
        .serial_status(ser_status), .serial_tx_available(ser_tx_av), .serial_tx_strobe(ser_tx_stb),
        .serial_tx_data(ser_tx_dat), .serial_rx_available(ser_rx_av), .serial_rx_strobe(ser_rx_stb),
        .serial_rx_data(ser_rx_dat), .serial_serper(ser_serper),
        .ser_rts_n(ser_rts_n), .ser_dtr_n(ser_dtr_n), .ser_ri_n(ser_ctl_n[3]), .ser_cd_n(ser_ctl_n[2]),
        .ser_dsr_n(ser_ctl_n[1]), .ser_cts_n(ser_ctl_n[0]),
        .sdc_img_mounted(sdc_img_mounted), .sdc_img_size(sdc_img_size), .sdc_rd(sdc_rd), .sdc_wr(sdc_wr),
        .sdc_sector(sdc_sector), .sdc_busy(sdc_busy), .sdc_done(sdc_done), .sdc_byte_in_strobe(sdc_byte_in_strobe),
        .sdc_byte_addr(sdc_byte_addr), .sdc_byte_in_data(sdc_byte_in_data), .sdc_byte_out_data(sdc_byte_out_data),
        .ram_data(ram_dout), .ramdata_in(ram_din), .chip48(chip48), .refresh(ram_refresh),
        .ram_address(ram_a), ._ram_bhe(ram_bhe_n), ._ram_ble(ram_ble_n), ._ram_we(ram_we_n), ._ram_oe(ram_oe_n),
        .fastram_sel(fr_sel), .fastram_addr(fr_addr), .fastram_lds(fr_lds), .fastram_uds(fr_uds),
        .fastram_dout(fr_q), .fastram_dout48(fr_q48), .fastram_din(fr_din), .fastram_wr(fr_wr), .fastram_ready(fast_tog), .cpu_gap(gap_r)
`ifdef G2_AMIGA_AGA
        , .eth_dma_req(xm_req), .eth_dma_write(xm_write), .eth_dma_addr(xm_addr), .eth_dma_wdata(xm_wdata),
        .eth_dma_wide(xm_wide), .eth_dma_wdata64(xm_wdata64), .eth_dma_uds(xm_uds), .eth_dma_lds(xm_lds),
        .eth_dma_ready(xm_ready), .eth_dma_rdata(xm_rdata), .eth_dma_rdata64(xm_rdata64), .xsurf_mac(xs_mac),
        .toccata_left(toc_l), .toccata_right(toc_r), .toccata_aux2_left(aux2_l), .toccata_aux2_right(aux2_r),
        .g2ser_mode(g2s_mode), .g2ser_baud(g2s_baud), .g2ser_tx_available(g2s_tx_av), .g2ser_tx_strobe(g2s_tx_stb),
        .g2ser_tx_data(g2s_tx_dat), .g2ser_rx_available(g2s_rx_av), .g2ser_rx_strobe(g2s_rx_stb),
        .g2ser_rx_data(ser_rx_dat)
`endif
    );

    // ================================================================================================================
    // chip cache
    // ================================================================================================================
    wire        chip_req = ~ram_oe_n | ~ram_we_n;
    wire        is_rom   = ram_a[22:19] == 4'b1111;
    wire        chip_we  = ~ram_we_n & ~is_rom;
    // the write data is registered with the address: an edge may be issued in the clock after a bus cycle changes,
    // while the registered request still names the previous write, and that commit must write the previous data
    reg  [23:0] chip_a_r;  reg chip_req_r, chip_we_r;  reg [1:0] chip_be_r;  reg [15:0] chip_wd_r;
    always @(posedge clk) begin
        chip_a_r <= {ram_a, 1'b0}; chip_req_r <= chip_req; chip_we_r <= chip_we;
        chip_be_r <= {~ram_bhe_n, ~ram_ble_n}; chip_wd_r <= ram_dout;
    end
    wire        chip_conf;
    // a sector request waiting for the firmware (sector port) holds the core until SDC_GO toggles go_tog; the next
    // core edge then sees the toggle
    reg         q_pend = 1'b0;
    reg         go_tog = 1'b0, go_tog_c = 1'b0;
    // no sector hold while the core is in reset: the queue is cleared at the core's power-on reset (below), which needs
    // core edges, so a request left waiting by a machine that was reset cannot keep the core from leaving reset
    // FAST RAM.  The core's fast-RAM port: fastram_sel is high while an access is outstanding; the memory
    // acknowledges by toggling fastram_ready.  Without ENABLE_CACHE (the 68000 core) the CPU drives the port and takes
    // the toggle (and fastram_dout) at its next cpu_ph1 edge, retrying in the next 7 MHz cycle when it has not come.
    // With ENABLE_CACHE (the G2_AMIGA_AGA core: MiSTer's cpu_cache_new.v in nanomig.v, 2 x 4 KB, as NanoMig's
    // top_020.sv builds it) the CPU is served by that cache at its own clock; the port carries its 8-byte line fills
    // (an aligned read whose other three words are fastram_dout48, from u_fast's a_q48) and its write-through
    // writes, and drops fastram_sel at the edge that sees the toggle.  fastram_addr is a 16 MB byte space (ENABLE_RAM32: the 8 MB Zorro II board at $200000-$9FFFFF lies at
    // 0x20_0000-0x5F_FFFF and 0x80_0000-0xBF_FFFF of it), cached by u_fast at FAST_BASE of chip A.  The request is
    // registered as the chip request is and taken once a data strobe is set; a miss holds the gate like a chip miss.  At the first edge issued with the
    // access confirmed, a write is committed once, a read's word is latched in fr_q, and fast_tog toggles; fast_served
    // keeps that access from being taken again until the core drops fastram_sel, which it does from the toggle until
    // its next cpu_ph1 edge (at least one core edge, so at least one clk).
    reg  [23:0] fast_a_r;  reg fast_req_r = 1'b0, fast_we_r = 1'b0, fast_served = 1'b0;
    reg  [1:0]  fast_be_r; reg [15:0] fast_wd_r;
    always @(posedge clk) begin
        fast_a_r <= {fr_addr, 1'b0}; fast_req_r <= fr_sel; fast_we_r <= fr_wr;
        fast_be_r <= {~fr_uds, ~fr_lds}; fast_wd_r <= fr_din;   // fastram_uds/lds are the CPU's active-low strobes
    end
    // an access is taken once a data strobe is set: the 68000 raises UDS/LDS of a write one clock after AS, with
    // the data, so a commit at the first edge of the select would write no byte
    wire        fast_act = fast_req_r & ~fast_served & (|fast_be_r);
    wire        fast_conf;
    wire [15:0] fast_q;
    wire [47:0] fast_q48;

    assign hold = (chip_req_r & ~chip_conf) | (fast_act & ~fast_conf) |
                  (sdc_hold & ~core_reset & q_pend & (go_tog == go_tog_c));
    wire        commit = emit & chip_req_r;
    wire        fcommit = emit & fast_act;
    always @(posedge clk) begin
        if (rst | core_reset) fast_served <= 1'b0;
        else if (fcommit) begin fast_served <= 1'b1; fast_tog <= ~fast_tog; fr_q <= fast_q; fr_q48 <= fast_q48; end
        else if (!fast_req_r) fast_served <= 1'b0;
    end
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] mcb_ready_s = 2'b00;
    always @(posedge clk) mcb_ready_s <= {mcb_ready_s[0], mcb_ready};
    wire [31:0] chip_miss, chip_wb;
    wire        chip_fb, busreq;
    amiga_cache #(.AW(24), .SET_LOG2(SET_LOG2), .DDR_BASE(DDR_BASE)) u_chip (
        .clk(clk), .rst(rst),
        .a_addr(chip_a_r), .a_req(chip_req_r), .a_we(chip_we_r), .a_be(chip_be_r), .a_wdata(chip_wd_r),
        .a_q(ram_din), .a_q48(chip48), .a_conf(chip_conf), .a_commit(commit),
        .inv(c_inv), .flush(c_flush), .flush_busy(chip_fb),
        .misses(chip_miss), .writebacks(chip_wb),
        .busreq(busreq), .busgnt(mcb_ready_s[1]),
        .p_cmd_en(c_cmd_en), .p_cmd_instr(c_cmd_instr), .p_cmd_bl(c_cmd_bl), .p_cmd_byte_addr(c_cmd_byte_addr),
        .p_cmd_full(c_cmd_full), .p_wr_en(c_wr_en), .p_wr_mask(c_wr_mask), .p_wr_data(c_wr_data),
        .p_wr_empty(c_wr_empty), .p_rd_en(c_rd_en), .p_rd_data(c_rd_data), .p_rd_empty(c_rd_empty));

`ifdef G2_AMIGA_AGA
    // X-SURF MAILBOX.  The device model's 64 KB host mailbox (rtl/src/ethernet.v) in block
    // RAM: its master on port A in the core domain, the firmware on port B through MBX_ADDR (0x02A, the byte offset,
    // bit 0 ignored) and MBX_DATA (0x02B, one 16-bit word in the model's order; each read and each write moves
    // MBX_ADDR on by 2).  A read of MBX_DATA returns the word MBX_ADDR named at least two clocks before.
    // XS_MAC_HI (0x02C, bytes 0-1 in bits 15:0) and XS_MAC_LO (0x02D, bytes 2-5): the station address the model loads
    // into its PAR registers and PROM at the core's reset (byte 0 first on the wire).
    xsurf_mbx u_xmbx (
        .clk_a(amiga_clk), .eth_dma_req(xm_req), .eth_dma_write(xm_write), .eth_dma_addr(xm_addr),
        .eth_dma_wdata(xm_wdata), .eth_dma_wide(xm_wide), .eth_dma_wdata64(xm_wdata64), .eth_dma_uds(xm_uds),
        .eth_dma_lds(xm_lds), .eth_dma_ready(xm_ready), .eth_dma_rdata(xm_rdata), .eth_dma_rdata64(xm_rdata64),
        .clk_b(clk), .b_addr(mbx_a), .b_we(mbx_we), .b_wdata(mbx_wd), .b_q(mbx_q));
`endif
    (* ASYNC_REG = "TRUE", shreg_extract = "no" *) reg [1:0] f_ready_s = 2'b00;
    always @(posedge clk) f_ready_s <= {f_ready_s[0], f_mcb_ready};
    wire [31:0] fast_miss, fast_wb;
    wire        fast_fb, f_busreq;
    amiga_cache #(.AW(24), .SET_LOG2(FSET_LOG2), .DDR_BASE(FAST_BASE)) u_fast (
        .clk(clk), .rst(rst),
        .a_addr(fast_a_r), .a_req(fast_act), .a_we(fast_we_r), .a_be(fast_be_r), .a_wdata(fast_wd_r),
        .a_q(fast_q), .a_q48(fast_q48), .a_conf(fast_conf), .a_commit(fcommit),
        .inv(c_inv), .flush(1'b0), .flush_busy(fast_fb),
        .misses(fast_miss), .writebacks(fast_wb),
        .busreq(f_busreq), .busgnt(f_ready_s[1]),
        .p_cmd_en(f_cmd_en), .p_cmd_instr(f_cmd_instr), .p_cmd_bl(f_cmd_bl), .p_cmd_byte_addr(f_cmd_byte_addr),
        .p_cmd_full(f_cmd_full), .p_wr_en(f_wr_en), .p_wr_mask(f_wr_mask), .p_wr_data(f_wr_data),
        .p_wr_empty(f_wr_empty), .p_rd_en(f_rd_en), .p_rd_data(f_rd_data), .p_rd_empty(f_rd_empty));

    // ================================================================================================================
    // sector port
    // ================================================================================================================
    reg  [7:0]  sd_buf [0:511];                    // written on clk (firmware, or a byte the core wrote), read on both
    reg  [8:0]  sd_fidx;                           // firmware index
    reg  [7:0]  sd_fq;
    reg         mnt_tog = 1'b0;
    reg  [7:0]  mnt_mask;  reg [31:0] sd_size;
    localparam [1:0] Q_IDLE = 2'd0, Q_WAIT = 2'd1, Q_XFER = 2'd2, Q_END = 2'd3;
    reg  [1:0]  qst = Q_IDLE;
    reg         q_busy = 1'b0, q_done = 1'b0, q_we = 1'b0;
    reg  [7:0]  q_mask = 8'd0;  reg [31:0] q_sector = 32'd0;
    reg         mnt_tog_c = 1'b0;
    reg  [9:0]  q_i;  reg [1:0] q_gap;
    reg         q_stb = 1'b0;  reg [8:0] q_addr = 9'd0;  reg [7:0] q_data = 8'd0;
    reg         q_take = 1'b0, q_take_d = 1'b0, q_take_dd = 1'b0;  reg [8:0] q_take_a, q_take_ad, q_take_add;
    reg  [7:0]  mnt_pulse = 8'd0;
    reg         done_pulse = 1'b0;
    wire [7:0]  q_rdbyte = sd_buf[q_i[8:0]];
    always @(posedge amiga_clk) begin
        q_stb <= 1'b0; done_pulse <= 1'b0; mnt_pulse <= 8'd0; q_take <= 1'b0;
        q_take_d <= q_take; q_take_ad <= q_take_a;    // the byte for an address is registered by the core one edge on
        q_take_dd <= q_take_d; q_take_add <= q_take_ad;
        // a mount applies from core edge 100 after reset on (sim/sdc_model.v MOUNT_AT), so one written before run
        // reaches the floppy at the same core edge in every run
        if (mnt_tog != mnt_tog_c && rcnt >= 12'd100) begin mnt_tog_c <= mnt_tog; mnt_pulse <= mnt_mask; end
        if (por_r) begin                           // the core's power-on reset: no request waiting or in transfer, a
            qst <= Q_IDLE; q_busy <= 1'b0;         // GO written before it is not taken for the next request
            q_pend <= 1'b0; q_done <= 1'b0; go_tog_c <= go_tog;
        end else
        case (qst)
        Q_IDLE: if (|sdc_rd || |sdc_wr) begin
            q_busy <= 1'b1; q_pend <= 1'b1; q_done <= 1'b0; q_we <= |sdc_wr;
            q_mask <= sdc_rd | sdc_wr; q_sector <= sdc_sector; qst <= Q_WAIT;
        end
        Q_WAIT: if (go_tog != go_tog_c) begin go_tog_c <= go_tog; q_pend <= 1'b0; q_i <= 10'd0; q_gap <= 2'd0; qst <= Q_XFER; end
        Q_XFER: if (q_gap != 2'd0) q_gap <= q_gap - 2'd1;
            else if (q_i == 10'd512) qst <= Q_END;
            else begin
                q_addr <= q_i[8:0]; q_gap <= 2'd3;
                if (!q_we) begin q_stb <= 1'b1; q_data <= q_rdbyte; end
                else begin q_take <= 1'b1; q_take_a <= q_i[8:0]; end
                q_i <= q_i + 10'd1;
            end
        Q_END: begin q_busy <= 1'b0; q_done <= 1'b1; done_pulse <= 1'b1; qst <= Q_IDLE; end
        endcase
    end
    assign sdc_busy = q_busy;
    assign sdc_done = done_pulse;
    assign sdc_byte_in_strobe = q_stb;
    assign sdc_byte_addr = q_addr;
    assign sdc_byte_in_data = q_data;
    assign sdc_img_mounted = mnt_pulse;
    assign sdc_img_size = {32'd0, sd_size};
    wire        fw_sd_wr = cyc && we && !ack && adr == 10'h027;
    // the core's byte of a write, taken at the edge after the one its address was set (q_take_d) for an IDE drive,
    // whose byte follows the address combinationally (nanomig.v sdc_byte_out_data); one edge later (q_take_dd) for a
    // floppy, whose byte comes from a block-RAM read at the first edge and a register at the second
    // (paula_floppy_ebr.v sdc_byte_out_data): at the second edge that register still holds the previous address's
    // word, which is the right word only for an odd byte.  The address stays for four edges (q_gap).
    wire        q_cap   = (|q_mask[3:0]) ? q_take_dd  : q_take_d;
    wire [8:0]  q_cap_a = (|q_mask[3:0]) ? q_take_add : q_take_ad;
    always @(posedge clk) begin
        if (fw_sd_wr) sd_buf[sd_fidx] <= dat_w[7:0];
        else if (ce_r && q_cap) sd_buf[q_cap_a] <= sdc_byte_out_data;
        sd_fq <= sd_buf[sd_fidx];
    end

    // ================================================================================================================
    // video capture (osd_u8g2.v on the native picture) and frame writer 
    // ================================================================================================================
    wire [5:0]  or6, og6, ob6;
    osd_u8g2 u_osd (
        .clk(amiga_clk), .reset(por_r),
        .data_in_strobe(mcu_osd_strobe), .data_in_start(tx_start), .data_in(tx_byte),
        .hs(vhs), .vs(vvs), .r_in(vr[7:2]), .g_in(vg[7:2]), .b_in(vb[7:2]), .r_out(or6), .g_out(og6), .b_out(ob6));
    wire [15:0] px565 = {or6[5:1], og6, ob6[5:1]};
    // core side: sample phase, line buffer write port, line and frame toggles
    // LINE RATE.  lc counts core clocks from one hblank start to the next: a 15 kHz line is 1,816 (PAL, 227 colour
    // clocks of 8) or 1,820 (NTSC), a 31 kHz line (DblPAL, DblNTSC, Multiscan, productivity) about half.  A line
    // shorter than FAST_LINE is sampled on every core clock (its pixels are 35 ns), others on every second one
    // (the 14 MHz hires grid), from hofs_fast or vid_hofs samples after hblank.  The flag is the previous line's.
    reg         ph = 1'b0, hbl_d = 1'b1, vbl_d = 1'b1;
    reg  [10:0] hx = 11'd0, sx = 11'd0;            // samples since hblank ended; samples stored in this line
    reg  [11:0] lc = 12'd0;
    reg         fast_c = 1'b0, fr_fast_c = 1'b0;
    reg         lbank_c = 1'b0, ln_tog = 1'b0, fr_tog = 1'b0, fs_tog = 1'b0;
    reg         ln_bank_c = 1'b0, ln_field_c = 1'b0, ln_lace_c = 1'b0, fr_lace_c = 1'b0;
    reg  [9:0]  vy_c = 10'd0, ln_y_c = 10'd0, fr_lines_c = 10'd0;
    reg         lb_we_e = 1'b0, lb_we_o = 1'b0;  reg [9:0] lb_wa;  reg [15:0] lb_wd;
    wire [10:0] hofs_c = fast_c ? vid_hofs_fast : vid_hofs;
    always @(posedge amiga_clk) begin
        lb_we_e <= 1'b0; lb_we_o <= 1'b0;
        hbl_d <= vhbl; vbl_d <= vvbl;
        ph <= ~ph;
        if (lc != 12'hFFF) lc <= lc + 12'd1;
        if (!vhbl && !vvbl && (ph || fast_c)) begin
            if (hx >= hofs_c && sx < vid_w) begin
                lb_we_e <= ~sx[0]; lb_we_o <= sx[0]; lb_wa <= {lbank_c, sx[9:1]}; lb_wd <= px565; sx <= sx + 11'd1;
            end
            hx <= hx + 11'd1;
        end
        if (vhbl && !hbl_d) begin                  // line end
            if (!vvbl) begin ln_tog <= ~ln_tog; ln_bank_c <= lbank_c; ln_y_c <= vy_c; ln_field_c <= vfield; ln_lace_c <= vlace;
                             lbank_c <= ~lbank_c; if (vy_c != 10'h3FF) vy_c <= vy_c + 10'd1; end
            hx <= 11'd0; sx <= 11'd0; ph <= 1'b0; lc <= 12'd0; fast_c <= lc < FAST_LINE;
        end
        if (!vvbl && vbl_d) begin vy_c <= 10'd0; fs_tog <= ~fs_tog; end                              // frame start
        if (vvbl && !vbl_d) begin fr_tog <= ~fr_tog; fr_lines_c <= vy_c; fr_lace_c <= vlace; fr_fast_c <= fast_c; end   // frame end
    end
    // the line buffer in two halves, even and odd samples: the writer takes one 32-bit word {odd, even} a clock
    reg  [9:0]  lb_ra;  wire [15:0] lb_qe, lb_qo;
    amiga_lbuf u_lbuf_e (.wclk(amiga_clk), .we(lb_we_e), .wa(lb_wa), .wd(lb_wd), .rclk(clk), .ra(lb_ra), .q(lb_qe));
    amiga_lbuf u_lbuf_o (.wclk(amiga_clk), .we(lb_we_o), .wa(lb_wa), .wd(lb_wd), .rclk(clk), .ra(lb_ra), .q(lb_qo));

    // clk side: frame buffer choice and FRAME_INFO (the subsystem slot's rule), line queue
    localparam [1:0] W_IDLE = 2'd0, W_FILL = 2'd1, W_CMD = 2'd2, W_ROOM = 2'd3;
    reg  [1:0]  wst;
    reg         ln_tog_s = 1'b0, fr_tog_s = 1'b0, fs_tog_s = 1'b0;
    reg  [1:0]  wbuf;
    wire [1:0]  pend = frame_info[1:0];
    wire [1:0]  wnext = (disp != 2'd0 && pend != 2'd0) ? 2'd0 : (disp != 2'd1 && pend != 2'd1) ? 2'd1 : 2'd2;
    reg         ln_pend;  reg ln_bank;  reg [9:0] ln_y;  reg ln_field, ln_lace;
    reg  [15:0] ln_drop;                           // lines that arrived while the previous one was still being written
    always @(posedge clk) begin
        if (rst) begin wbuf <= 2'd0; frames <= 32'd0; irq_frame <= 1'b0; frame_info <= 32'd0; ln_pend <= 1'b0; ln_drop <= 16'd0;
                       ln_tog_s <= ln_tog; fr_tog_s <= fr_tog; fs_tog_s <= fs_tog; end
        else begin
            if (ln_pend && wst == W_IDLE) ln_pend <= 1'b0;
            if (ln_tog != ln_tog_s) begin ln_tog_s <= ln_tog; ln_pend <= 1'b1; ln_bank <= ln_bank_c; ln_y <= ln_y_c;
                                          if (ln_pend && wst != W_IDLE) ln_drop <= ln_drop + 16'd1;
                                          ln_field <= ln_field_c; ln_lace <= ln_lace_c; end
            if (fs_tog != fs_tog_s) begin fs_tog_s <= fs_tog; wbuf <= wnext; end
            if (fr_tog != fr_tog_s) begin fr_tog_s <= fr_tog; frames <= frames + 32'd1; ts_frame <= ts_now; irq_frame <= 1'b1;
                                          frame_info <= {5'd0, fr_fast_c, fr_lines_c, 13'd0, fr_lace_c, wbuf}; end
            if (cyc && we && !ack && adr == 10'h00F && dat_w[0]) irq_frame <= 1'b0;
        end
    end

    // writer: W samples = W/2 words a line, in commands of up to 32 words (64 samples), one word read from the two
    // line-buffer halves a clock; again at +16 MB when mirrored.  A piece is filled while the previous one drains: a
    // fill starts once the write FIFO (64 words) holds 32 or fewer (v_wr_count, read W_SETTLE clocks after the last
    // push, as the count lags it), so a line costs about one clock a word rather than a fill and a drain per piece.
    // Rows from WLINES on are not written: they would pass the 1 MB buffer at 640 samples a line.
    localparam [9:0] WLINES = 10'd768;
    reg  [3:0]  wchunk;                            // 64-sample piece of the line
    reg  [5:0]  wi;                                // words read in this piece
    reg  [5:0]  wcount;                            // words pushed in this piece
    reg  [1:0]  wsettle;
    reg         rd_v, rd_v2, wpass;
    reg  [20:0] wyb;  reg wbank;  reg [1:0] wb_idx;
    reg  [31:0] wdata_r;
    wire [9:0]  wline = ln_lace ? {ln_y[8:0], ln_field} : ln_y;
    wire [10:0] left  = vid_w - {wchunk, 6'd0};    // samples of the line from this piece on
    wire [6:0]  piece = (left >= 11'd64) ? 7'd64 : left[6:0];
    wire [5:0]  pw    = piece[6:1];                // words of this piece
    wire        room  = (v_wr_count <= 7'd32) && (wsettle == 2'd0);
    assign v_cmd_instr = 3'b000;
    assign v_cmd_bl    = pw - 6'd1;                // words - 1 (64 samples: 32 words, bl 31)
    assign v_wr_mask   = 4'b0000;
    assign v_wr_data   = wdata_r;
    always @(posedge clk) begin
        v_cmd_en <= 1'b0; v_wr_en <= 1'b0;
        rd_v <= 1'b0; rd_v2 <= rd_v;
        if (wsettle != 2'd0) wsettle <= wsettle - 2'd1;
        if (rd_v2) begin wdata_r <= {lb_qo, lb_qe}; v_wr_en <= 1'b1; wcount <= wcount + 6'd1; wsettle <= 2'd3; end
        if (rst) begin wst <= W_IDLE; wsettle <= 2'd0; end
        else case (wst)
        W_IDLE: if (ln_pend && wline < WLINES) begin
            wyb <= wline * {vid_w, 1'b0}; wbank <= ln_bank; wb_idx <= wbuf; wpass <= 1'b0; wchunk <= 4'd0;
            wi <= 6'd0; wcount <= 6'd0; wst <= W_ROOM;
        end
        W_ROOM: if (room) wst <= W_FILL;
        W_FILL: begin
            if (wi != pw) begin lb_ra <= {wbank, wchunk, wi[4:0]}; rd_v <= 1'b1; wi <= wi + 6'd1; end
            if (wcount == pw && wi == pw) wst <= W_CMD;
        end
        W_CMD: if (!v_cmd_full) begin
            v_cmd_en <= 1'b1;
            v_cmd_byte_addr <= {5'd0, fb_base} + {5'd0, wpass, 24'd0} + {8'd0, wb_idx, 20'd0} + {9'd0, wyb}
                               + {19'd0, wchunk, 7'd0};
            wi <= 6'd0; wcount <= 6'd0;
            if (left > 11'd64) begin wchunk <= wchunk + 4'd1; wst <= W_ROOM; end
            else if (mirror && !wpass) begin wpass <= 1'b1; wchunk <= 4'd0; wst <= W_ROOM; end
            else wst <= W_IDLE;
        end
        endcase
    end

    // ================================================================================================================
    // audio FIFO , sampled by an NCO in core time
    // ================================================================================================================
    reg  [31:0] anco = 32'd0;
    reg         asmp_tog = 1'b0, asmp_tog_s = 1'b0;
    reg  [35:0] asmp_c = 36'd0;
    wire [32:0] anco_n = {1'b0, anco} + {1'b0, aud_inc};
    // Paula's 15 bits as 16 in 18 bits (a quarter of the range); the firmware takes the 18 bits to the codec's 24
    // (AUD_WL, AUD_WR).  G2_AMIGA_AGA: Paula scaled by the Toccata's AUX2 control as an AD1848 scales its AUX2 input
    // (gain 0 = +12 dB to 31 = -34.5 dB in 1.5 dB steps, 8 = 0 dB: the level without the card; bit 7 mutes), plus the
    // Toccata card's 16-bit output at the full 18-bit range, the sum saturated to 18 bits.
    wire [15:0] pau_l = {audio_left, audio_left[14]}, pau_r = {audio_right, audio_right[14]};
`ifdef G2_AMIGA_AGA
    // 10^((12 - 1.5 code) / 20) x 2^14
    function [15:0] aux2_gain;
        input [4:0] code;
        case (code)
            5'd0: aux2_gain = 16'd65226;
            5'd1: aux2_gain = 16'd54881;
            5'd2: aux2_gain = 16'd46176;
            5'd3: aux2_gain = 16'd38853;
            5'd4: aux2_gain = 16'd32690;
            5'd5: aux2_gain = 16'd27506;
            5'd6: aux2_gain = 16'd23143;
            5'd7: aux2_gain = 16'd19472;
            5'd8: aux2_gain = 16'd16384;
            5'd9: aux2_gain = 16'd13785;
            5'd10: aux2_gain = 16'd11599;
            5'd11: aux2_gain = 16'd9759;
            5'd12: aux2_gain = 16'd8211;
            5'd13: aux2_gain = 16'd6909;
            5'd14: aux2_gain = 16'd5813;
            5'd15: aux2_gain = 16'd4891;
            5'd16: aux2_gain = 16'd4115;
            5'd17: aux2_gain = 16'd3463;
            5'd18: aux2_gain = 16'd2914;
            5'd19: aux2_gain = 16'd2451;
            5'd20: aux2_gain = 16'd2063;
            5'd21: aux2_gain = 16'd1735;
            5'd22: aux2_gain = 16'd1460;
            5'd23: aux2_gain = 16'd1229;
            5'd24: aux2_gain = 16'd1034;
            5'd25: aux2_gain = 16'd870;
            5'd26: aux2_gain = 16'd732;
            5'd27: aux2_gain = 16'd616;
            5'd28: aux2_gain = 16'd518;
            5'd29: aux2_gain = 16'd436;
            5'd30: aux2_gain = 16'd367;
            5'd31: aux2_gain = 16'd309;
        endcase
    endfunction
    function [17:0] sat18;
        input [18:0] v;
        sat18 = (v[18] == v[17]) ? v[17:0] : {v[18], {17{~v[18]}}};
    endfunction
    reg  [15:0] ga_l = 16'd16384, ga_r = 16'd16384;
    reg  signed [32:0] pp_l = 33'sd0, pp_r = 33'sd0;
    reg  [17:0] out_l = 18'd0, out_r = 18'd0;
    // two registered stages: the gains and the products (DSP), then the saturated sum; the 48 kHz NCO below samples
    // the result
    wire signed [18:0] sum_l = {pp_l[32], pp_l[31:14]} + {toc_l[15], toc_l, 2'b00};
    wire signed [18:0] sum_r = {pp_r[32], pp_r[31:14]} + {toc_r[15], toc_r, 2'b00};
    always @(posedge amiga_clk) begin
        ga_l  <= aux2_l[7] ? 16'd0 : aux2_gain(aux2_l[4:0]);
        ga_r  <= aux2_r[7] ? 16'd0 : aux2_gain(aux2_r[4:0]);
        pp_l  <= $signed(pau_l) * $signed({1'b0, ga_l});
        pp_r  <= $signed(pau_r) * $signed({1'b0, ga_r});
        out_l <= sat18(sum_l);
        out_r <= sat18(sum_r);
    end
`else
    wire [17:0] out_l = {{2{pau_l[15]}}, pau_l}, out_r = {{2{pau_r[15]}}, pau_r};
`endif
    always @(posedge amiga_clk) begin
        anco <= anco_n[31:0];
        if (anco_n[32]) begin asmp_tog <= ~asmp_tog; asmp_c <= {out_l, out_r}; end
    end
    reg  [9:0]  aw_p, ar_p;
    reg  [10:0] alevel;
    reg         aovf;
    wire        apush = (asmp_tog != asmp_tog_s);
    wire        apop  = cyc && !we && !ack && (adr == 10'h013 || adr == 10'h019) && alevel != 11'd0;
    wire        aflush = cyc && we && !ack && adr == 10'h003 && dat_w[3];
    wire [35:0] aq;
    // an 18-bit sample saturated to 16 (AUD_DATA)
    function [15:0] sat16;
        input [17:0] v;
        sat16 = (v[17:15] == 3'b000 || v[17:15] == 3'b111) ? v[15:0] : {v[17], {15{~v[17]}}};
    endfunction
    amiga_afifo u_afifo (.clk(clk), .we(apush && alevel != 11'd1024), .wa(aw_p), .wd(asmp_c),
                         .ra(ar_p + (apop ? 10'd1 : 10'd0)), .q(aq));
    always @(posedge clk) begin
        asmp_tog_s <= asmp_tog;
        if (rst || aflush) begin aw_p <= 10'd0; ar_p <= 10'd0; alevel <= 11'd0; aovf <= 1'b0; end
        else begin
            if (apush) begin
                if (alevel != 11'd1024) aw_p <= aw_p + 10'd1; else aovf <= 1'b1;
            end
            if (apop) ar_p <= ar_p + 10'd1;
            alevel <= alevel + ((apush && alevel != 11'd1024) ? 11'd1 : 11'd0) - (apop ? 11'd1 : 11'd0);
        end
    end
    wire irq_audio = (alevel >= aud_wmark);
    wire irq_mcu   = ~sys_int_n;
    assign irq = (irq_frame & frm_irq_en) | (irq_audio & aud_irq_en) | irq_mcu | (irq_ser & ser_irq_en);

    // head frame strobes
    always @(posedge clk) begin
        if (rst) begin ts_now <= 32'd0; ts_head <= 32'd0; end
        else begin
            ts_now <= ts_now + 32'd1;
            if (head_sel ? head2_frame : head1_frame) ts_head <= ts_now;
        end
    end

    // ================================================================================================================
    // register page
    // ================================================================================================================
    always @(posedge clk) begin
        ack <= cyc & ~ack;
        c_inv <= 1'b0; c_flush <= 1'b0;
`ifdef G2_AMIGA_AGA
        mbx_we <= 1'b0;                              // MBX_DATA: one write a strobe; the address moves on after it, and
        if (mbx_we || (cyc && !we && !ack && adr == 10'h02B)) mbx_a <= mbx_a + 15'd1;   // after a read takes the word
`endif
        if (rst) begin
            run <= 1'b0; core_reset <= 1'b1; sdc_hold <= 1'b1; mirror <= 1'b0; frm_irq_en <= 1'b0; aud_irq_en <= 1'b0; head_sel <= 1'b1;
            ser_irq_en <= 1'b0;
            nco_inc <= 32'h929E_4149; aud_inc <= 32'h006D_D9CF; fb_base <= 25'h080_0000;
            vid_w <= 11'd768; vid_hofs <= 11'd0; vid_hofs_fast <= 11'd0; aud_wmark <= 11'd256; disp <= 2'd3;
            tx_target <= 2'd0; tx_start <= 1'b0; tx_byte <= 8'd0; sd_fidx <= 9'd0; mnt_mask <= 8'd0; sd_size <= 32'd0;
        end else if (cyc && we && !ack) begin
            case (adr)
            10'h001: {aud_irq_en, frm_irq_en, mirror, ser_irq_en, sdc_hold, core_reset, run} <= {dat_w[7:5], dat_w[3:0]};
            10'h003: begin c_inv <= dat_w[0]; c_flush <= dat_w[1]; end
            10'h007: head_sel  <= dat_w[0];
            10'h008: nco_inc   <= dat_w;
            10'h009: aud_inc   <= dat_w;
            10'h00B: fb_base   <= dat_w[24:0];
            10'h00C: begin vid_w <= {dat_w[10:1], 1'b0}; vid_hofs <= dat_w[26:16]; end
            10'h015: aud_wmark <= dat_w[10:0];
            10'h01C: disp      <= dat_w[1:0];
            10'h020: if (!tx_busy) begin tx_target <= dat_w[9:8]; tx_start <= dat_w[16]; tx_byte <= dat_w[7:0]; tx_tog <= ~tx_tog; end
            10'h026: begin go_tog <= ~go_tog; sd_fidx <= 9'd0; end
            10'h027: sd_fidx   <= sd_fidx + 9'd1;
            10'h028: begin mnt_mask <= dat_w[7:0]; mnt_tog <= ~mnt_tog; end
            10'h029: sd_size   <= dat_w;
`ifdef G2_AMIGA_AGA
            10'h02A: mbx_a     <= dat_w[15:1];
            10'h02B: begin mbx_wd <= dat_w[15:0]; mbx_we <= 1'b1; end
            10'h02C: xs_mac[47:32] <= dat_w[15:0];
            10'h02D: xs_mac[31:0]  <= dat_w;
            10'h02E: vid_hofs_fast <= dat_w[10:0];
`endif
            default: ;
            endcase
        end else if (cyc && !we && !ack && adr == 10'h027) sd_fidx <= sd_fidx + 9'd1;
        case (adr)
        10'h000: dat_r <= 32'h414D_4947;
        10'h001: dat_r <= {24'd0, aud_irq_en, frm_irq_en, mirror, 1'b0, ser_irq_en, sdc_hold, core_reset, run};
        10'h002: dat_r <= {15'd0, head_sel, 6'd0, vlace, vfield, 7'd0, chip_fb};
        10'h007: dat_r <= {31'd0, head_sel};
        10'h008: dat_r <= nco_inc;
        10'h009: dat_r <= aud_inc;
        10'h00B: dat_r <= {7'd0, fb_base};
        10'h00C: dat_r <= {5'd0, vid_hofs, 5'd0, vid_w};
        10'h00D: dat_r <= frames;
        10'h00E: dat_r <= frame_info;
        10'h00F: dat_r <= {28'd0, irq_ser, irq_mcu, irq_audio, irq_frame};
        10'h010: dat_r <= ts_now;
        10'h011: dat_r <= ts_frame;
        10'h012: dat_r <= ts_head;
        10'h013: dat_r <= {sat16(aq[35:18]), sat16(aq[17:0])};
        10'h017: dat_r <= {{14{aq[35]}}, aq[35:18]};
        10'h019: dat_r <= {{14{aq[17]}}, aq[17:0]};
        10'h014: dat_r <= {15'd0, aovf, 5'd0, alevel};
        10'h015: dat_r <= {21'd0, aud_wmark};
        10'h016: dat_r <= chip_miss;
        10'h018: dat_r <= chip_wb;
        10'h01A: dat_r <= held;
        10'h01B: dat_r <= {lost, owed};
        10'h01C: dat_r <= {30'd0, disp};
        10'h01D: dat_r <= fast_miss;
        10'h01E: dat_r <= fast_wb;
        10'h01F: dat_r <= 32'h0000_0001;            // CAPS: [0] 18-bit audio samples (AUD_WL, AUD_WR)
        10'h020: dat_r <= {tx_busy, 23'd0, mcu_rx};
        // pending: waiting and its SDC_GO not yet written.  q_pend falls only when the core domain takes the toggle
        // (Q_WAIT); a read between the SDC_GO write and that edge would otherwise report the served request again, and
        // the second SDC_GO would release the next request before its buffer is filled
        10'h024: dat_r <= {q_pend & (go_tog == go_tog_c), q_done, 13'd0, q_we, 8'd0, q_mask};
        10'h025: dat_r <= q_sector;
        10'h027: dat_r <= {24'd0, sd_fq};
        10'h029: dat_r <= sd_size;
        10'h030, 10'h031, 10'h032, 10'h033: dat_r <= midi_q;
`ifdef G2_AMIGA_AGA
        10'h02A: dat_r <= {16'd0, mbx_a, 1'b0};
        10'h02B: dat_r <= {16'd0, mbx_q};
        10'h02C: dat_r <= {16'd0, xs_mac[47:32]};
        10'h02D: dat_r <= xs_mac[31:0];
        10'h02E: dat_r <= {21'd0, vid_hofs_fast};
        10'h02F: dat_r <= {16'd0, ln_drop};
`endif
        default: dat_r <= 32'd0;
        endcase
    end
    wire _unused = &{1'b0, sel, vce, vres, ram_refresh, s_screen, s_turbo, s_volume, s_stereo, s_kick,
                     s_dsounds, busreq, fast_fb, f_busreq, 1'b0};
endmodule

// two-line buffer, 2 x 1,024 x 16, write port on the core clock, read port on clk
module amiga_lbuf (
    input  wire wclk, input wire we, input wire [9:0] wa, input wire [15:0] wd,
    input  wire rclk, input wire [9:0] ra, output reg [15:0] q
);
    reg [15:0] mem [0:1023];
    always @(posedge wclk) if (we) mem[wa] <= wd;
    always @(posedge rclk) q <= mem[ra];
endmodule

// audio FIFO storage, 1,024 x 36 ({L[17:0], R[17:0]}), read address presented one clock ahead (q follows ra)
module amiga_afifo (
    input  wire clk, input wire we, input wire [9:0] wa, input wire [35:0] wd,
    input  wire [9:0] ra, output reg [35:0] q
);
    reg [35:0] mem [0:1023];
    always @(posedge clk) begin if (we) mem[wa] <= wd; q <= mem[ra]; end
endmodule

`default_nettype wire
