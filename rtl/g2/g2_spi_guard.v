/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// g2_spi_guard.v -- hardware write guard between every SPI source and the configuration flash pins (M25P64 on the
// G2 revision C, M25P128 on revision B with G2_REVB).
//
// FUNCTION.  The requested levels {cs_n, sck, mosi} of whichever source owns the flash (the byte engine of
// g2_spi.v or the board-page bit-bang path) pass through one register stage to the pins.  The guard counts the
// SCK rising edges it has delivered since the chip select fell and holds the bits delivered so far.  Before it
// delivers the rising edge that would complete a byte the flash acts on, it decides whether the command may
// proceed:
//
//   edge 8  (opcode complete)   opcode in READ_OK            -> pass, no further checks in this transaction
//                               opcode in WRITE_ADDR         -> pass, decided again at edge 16
//                               any other opcode             -> REFUSE
//   edge 16 (address [23:16])   addr[23:16] in 0x38..0x6B or 0x6D..0x7F -> pass  (G2_REVB: 0x48..0x8B or 0x90..0xFF)
//                               otherwise                    -> REFUSE
//
// A refusal withholds that rising edge and drives CS_N high in the same clock.  The flash has then received 7 or
// 15 bits: CS_N rising at a position that is not a byte boundary after the complete command aborts it (M25P64 data
// sheet: SE, PP, BE and WRSR execute only when S is driven high after the eighth bit of the last byte).  CS_N stays
// high, and SCK low, until the source itself raises CS_N, so the rest of the refused transaction reaches nothing.
//
// Protected regions of the revision C flash map (M25P64, 8 MB): 0x000000-0x37FFFF (multiboot header and golden
// image) and 0x6C0000-0x6CFFFF (configuration table, MAC).  Addresses 0x800000 and above alias the array on an 8 MB
// part (address bit 23 is not decoded) and are refused.
// With G2_REVB defined, the revision B map (M25P128, 16 MB, 256 KB sectors): protected 0x000000-0x47FFFF (multiboot
// header and golden image) and 0x8C0000-0x8FFFFF (configuration table, MAC); writable 0x480000-0x8BFFFF and
// 0x900000-0xFFFFFF.  Every boundary is a multiple of 256 KB, so the check on address bits 23:16 decides whole
// sectors.
//
// SETUP.  A rising edge is delivered only when MOSI at the pin already equals the requested MOSI; a source that
// changes MOSI and SCK in the same clock gets MOSI one clock ahead of the edge.  The bit the guard records is
// therefore exactly the bit the flash samples.
//
// SPI mode 0 and mode 3 are both counted correctly: only rising edges while CS_N is low are counted, and CS_N
// falling is delivered with SCK at its requested level.
`timescale 1ns / 1ps
`default_nettype none

module g2_spi_guard (
    input  wire        clk,
    input  wire        rst,
    // requested levels, registered by the source
    input  wire        req_cs_n,
    input  wire        req_sck,
    input  wire        req_mosi,
    // pin levels (IOB registers)
    output reg         pin_cs_n = 1'b1,
    output reg         pin_sck  = 1'b0,
    output reg         pin_mosi = 1'b0,
    // status
    output wire        blocked,            // the current transaction was refused (until the source raises CS_N)
    output reg  [15:0] refused_cnt = 16'd0,
    output reg  [15:0] passed_cnt  = 16'd0, // write/erase commands whose address passed at edge 16
    output reg  [16:0] last_refused = 17'd0 // {[16] refused at the address, [15:8] addr[23:16] or 0, [7:0] opcode}
);
    reg        blk = 1'b0;
    reg  [4:0] nbits = 5'd0;             // rising edges delivered in this transaction, saturates at 16
    reg [14:0] sh = 15'd0;               // bits delivered so far, newest in [0]
    reg        wr_cmd = 1'b0;            // the opcode was an addressed write/erase
    wire       rise = req_sck && !pin_sck && (pin_mosi == req_mosi);   // a rising edge to deliver, MOSI set up
    wire [7:0] op_c = {sh[6:0], pin_mosi};         // the opcode if this is edge 8
    wire [7:0] ah_c = {sh[6:0], pin_mosi};         // addr[23:16] if this is edge 16
    assign blocked = blk;

    function f_read_ok; input [7:0] op; begin
        case (op)
        8'h03, 8'h0B, 8'h3B, 8'h6B,           // READ, FAST_READ, dual/quad output read
        8'h05, 8'h70, 8'h85, 8'h65, 8'hB5,    // RDSR, flag status, volatile/enhanced/NV config reads
        8'h9F, 8'h9E, 8'hAB, 8'h5A, 8'h4B,    // RDID, RES, SFDP, unique ID
        8'h06, 8'h04, 8'hB9:                  // WREN, WRDI, DP
            f_read_ok = 1'b1;
        default: f_read_ok = 1'b0;
        endcase
    end endfunction

    function f_write_addr; input [7:0] op; begin
        case (op)
        8'h02, 8'hA2, 8'h32,                  // PP, dual/quad input page program
        8'hD8, 8'h52, 8'h20:                  // 64 KB, 32 KB, 4 KB erase
            f_write_addr = 1'b1;
        default: f_write_addr = 1'b0;
        endcase
    end endfunction

    function f_addr_ok; input [7:0] a; begin
`ifdef G2_REVB
`ifdef G2_FAULT_CFGTBL
        f_addr_ok = (a >= 8'h48);                                              // planted: configuration table not protected
`else
        f_addr_ok = ((a >= 8'h48) && (a <= 8'h8B)) || (a >= 8'h90);
`endif
`else
`ifdef G2_FAULT_CFGTBL
        f_addr_ok = (a >= 8'h38) && (a <= 8'h7F);                              // planted: configuration table not protected
`else
        f_addr_ok = ((a >= 8'h38) && (a <= 8'h6B)) || ((a >= 8'h6D) && (a <= 8'h7F));
`endif
`endif
    end endfunction

    // refuse this edge?
    wire refuse_op   = (nbits == 5'd7)  && !f_read_ok(op_c) && !f_write_addr(op_c);
    wire refuse_addr = (nbits == 5'd15) && wr_cmd && !f_addr_ok(ah_c);
`ifdef G2_FAULT_NOGUARD
    wire refuse = 1'b0;                                                        // planted: guard removed
`elsif G2_FAULT_OPCODE
    wire refuse = refuse_addr;                                                 // planted: opcode check removed
`else
    wire refuse = refuse_op || refuse_addr;
`endif

    always @(posedge clk) begin
        if (rst) begin
            pin_cs_n <= 1'b1; pin_sck <= 1'b0; pin_mosi <= 1'b0;
            blk <= 1'b0; nbits <= 5'd0; sh <= 15'd0; wr_cmd <= 1'b0;
        end else if (req_cs_n) begin
            // the source has released the flash: deselect, reset the transaction
            pin_cs_n <= 1'b1; pin_sck <= req_sck; pin_mosi <= req_mosi;
            blk <= 1'b0; nbits <= 5'd0; wr_cmd <= 1'b0;
        end else if (blk) begin
            pin_cs_n <= 1'b1; pin_sck <= 1'b0;
        end else if (pin_cs_n) begin
            // chip select falls; SCK keeps its requested level, no edge is counted
            pin_cs_n <= 1'b0; pin_sck <= req_sck; pin_mosi <= req_mosi;
            nbits <= 5'd0; wr_cmd <= 1'b0;
        end else if (rise) begin
            if (refuse) begin
                blk <= 1'b1; pin_cs_n <= 1'b1;
                refused_cnt  <= refused_cnt + 16'd1;
                last_refused <= (nbits == 5'd7) ? {1'b0, 8'd0, op_c} : {1'b1, ah_c, sh[14:7]};
            end else begin
                pin_sck <= 1'b1;
                sh <= {sh[13:0], pin_mosi};
                if (nbits != 5'd16) nbits <= nbits + 5'd1;
                if (nbits == 5'd7)  wr_cmd <= f_write_addr(op_c);
                if (nbits == 5'd15 && wr_cmd) passed_cnt <= passed_cnt + 16'd1;
            end
        end else if (req_sck && !pin_sck) begin
            pin_mosi <= req_mosi;             // MOSI first; the edge follows in the next clock and is checked there
        end else begin
            pin_sck  <= req_sck;              // falling edges and steady levels
            pin_mosi <= req_mosi;
        end
    end
endmodule

`default_nettype wire
