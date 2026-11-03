/*
 * Edited for pano-g2-amiga by Mike Wolak, EPROM Foundry Inc. (the edits: amiga/THIRD_PARTY.md).
 * This file keeps the licence of its origin, stated below or in amiga/nanomig (GPL-3.0-or-later).
 */
// rtl/src/toccata_playback.sv: third_party/fpga-toccata/toccata_playback.sv with each pb_state.next() written as the
// state that follows in pbStateType (sv2v does not lower enum methods), and the sample clock a 32-bit phase
// accumulator (NCO) in place of the counter of CLK_FREQUENCY / rate:
//   - every clock the accumulator adds rate x 2^32 / f_core; a carry starts a sample, so the mean rate is exact in
//     core time (f_core 28,636,364 Hz NTSC, 28,375,160 Hz PAL: the core clock fw/amiga/amiga_regs.h AMI_NCO_NTSC and
//     AMI_NCO_PAL produce; the same formula gives the firmware's 48 kHz output increment, so 48 kHz here is that
//     NCO's rate exactly)
//   - the 16 rates of the AD1848 data sheet (CSS 0: 24.576 MHz / 3072, 1536, 896, 768, 448, 384, 512, 2560; CSS 1:
//     16.9344 MHz / the same); code 3 is 32,000 Hz, where the upstream table had 31,270
//   - outputs acc (the accumulator) and upd (one clock: ldata and rdata were written) for the interpolation of
//     rtl/src/toccata.sv
// CLK_FREQUENCY is kept as a parameter and no longer used.
/********************************************/
/* toccata_playback.v                       */
/* Toccata sound playback                   */
/*                                          */
/* 2022-2023, ranzbak@gmail.com             */
/********************************************/


module toccata_playback #(
    parameter int CLK_FREQUENCY = 28_359_380
) (
    input  logic        clk,
    input  logic        rst,

    // Control parameters
    input  wire         pen,        // Playback, disable - 0, enable - 1
    input  wire  [2:0]  freq_sel,   // See table below
    input  wire         sm,         // 0 - Mono, 1 - Stereo
    input  wire         lc,         // 0 - Linear, 1 - companded (not supported)
    input  wire         fmt,        // 0 - 8 bit unsigned, 1 - 16 bit 2 complement
    input  wire         css,        // 0 - 24.576, 1 - 16.9344
    input  wire         ntsc,       // 1 - the core clock is the NTSC one (28,636,364 Hz), 0 - PAL (28,375,160 Hz)

    // FIFO interface
    output logic        rst_fifo,   // Reset FIFO if sm, lc, fmt change
    output logic        rd_en,      // Enable read from FIFO
    input  wire  [7:0]  data_in,    // Data from the FIFO
    input  wire         empty,      // 1 - FIFO is empty

    // Audio interface
    (* KEEP = "true" *)
    output logic [15:0] ldata,      // Left DAC data
    (* KEEP = "true" *)
    output logic [15:0] rdata,      // Right DAC data
    (* KEEP = "true" *)
    output logic        endata,     // Strobe on new sample data
    output logic [31:0] nco_acc,    // the sample clock's accumulator
    output logic        upd         // one clock: ldata and rdata were written
);
assign nco_acc = acc;


// Sample clock: rate x 2^32 / f_core, NTSC and PAL (see the header)
logic [31:0] nco_inc;
logic [31:0] acc = 32'd0;
always_ff @(posedge clk) begin
    case ({css, freq_sel})
        4'h0: nco_inc <= ntsc ? 32'h00124EF8 : 32'h00127A1D;    //  8,000 Hz
        4'h1: nco_inc <= ntsc ? 32'h00249DF0 : 32'h0024F43A;    // 16,000 Hz
        4'h2: nco_inc <= ntsc ? 32'h003EC59B : 32'h003F5988;    // 27,428.57 Hz
        4'h3: nco_inc <= ntsc ? 32'h00493BDF : 32'h0049E874;    // 32,000 Hz
        4'h4: nco_inc <= ntsc ? 32'h007D8B36 : 32'h007EB310;    // 54,857.14 Hz (beyond the data sheet)
        4'h5: nco_inc <= ntsc ? 32'h009277BF : 32'h0093D0E8;    // 64,000 Hz (beyond the data sheet)
        4'h6: nco_inc <= ntsc ? 32'h006DD9CF : 32'h006EDCAE;    // 48,000 Hz
        4'h7: nco_inc <= ntsc ? 32'h0015F85D : 32'h00162C23;    //  9,600 Hz
        4'h8: nco_inc <= ntsc ? 32'h000C9D9D : 32'h000CBB58;    //  5,512.5 Hz
        4'h9: nco_inc <= ntsc ? 32'h00193B3A : 32'h001976B0;    // 11,025 Hz
        4'ha: nco_inc <= ntsc ? 32'h002B40F6 : 32'h002BA6E5;    // 18,900 Hz
        4'hb: nco_inc <= ntsc ? 32'h00327675 : 32'h0032ED60;    // 22,050 Hz
        4'hc: nco_inc <= ntsc ? 32'h005681ED : 32'h00574DC9;    // 37,800 Hz
        4'hd: nco_inc <= ntsc ? 32'h0064ECEA : 32'h0065DAC0;    // 44,100 Hz
        4'he: nco_inc <= ntsc ? 32'h004BB1AF : 32'h004C6410;    // 33,075 Hz
        4'hf: nco_inc <= ntsc ? 32'h000F2389 : 32'h000F4736;    //  6,615 Hz
    endcase
end
wire [32:0] acc_n = {1'b0, acc} + {1'b0, nco_inc};

// Byte ordering in playback modes
// 8bit  - mono   : 0 - n bytes in order
// 8bit  - stereo : sample 1 - left, sample 1 - right ...n
// 16bit - mona   : sample 1 - byte 1, 2 ...n
// 16bit - stereo : sample 1 - byte 1, 2 left byte 3, 4 right byte ...n
logic signed [15:0] tmp_16bit_left;
logic signed [15:0] tmp_16bit_right;
logic signed [7:0]  tmp_8bit_left;
logic signed [7:0]  tmp_8bit_right;
logic               pb_en;            // Playback enable strobe
// Change detection registers
logic               _sm;
logic               _lc;
logic               _fmt;

typedef enum { idle,
    STEP_0_MONO,
    STEP_1_MONO,
    STEP_2_MONO,
    STEP_0_STEREO,
    STEP_1_STEREO,
    STEP_2_STEREO,
    STEP_3_STEREO,
    STEP_4_STEREO
} pbStateType;
pbStateType pb_state = idle;
always_ff @(posedge clk) begin
    pb_en <= 1'b0;
    rd_en <= 1'b0;
    endata <= 1'b0;
    rst_fifo <= 1'b0;
    upd <= 1'b0;

    // Sync prev regs
    _sm <= sm;
    _lc <= lc;
    _fmt <= fmt;

    if (rst) begin
        rst_fifo <= 1'b1;
        pb_state <= idle;
        tmp_8bit_left <= 8'sh80;
        tmp_8bit_right <= 8'sh80;
        tmp_16bit_left <= 16'h0000;
        tmp_16bit_right <= 16'h0000;
        ldata <= 16'h0000;
        rdata <= 16'h0000;
    end else begin
        // Detect changes between previous and current
        if (sm != _sm || lc != _lc || _fmt != _fmt) begin
            // Reset FIFO
            rst_fifo <= 1'b1;
            // Reset FSM
            pb_state <= idle;
        end

        // Trigger sample playback at each carry of the sample clock
        if (pen == 1'b0) begin
            // Just keep the accumulator ready
            acc <= 32'd0;
        end else begin
            acc <= acc_n[31:0];
            pb_en <= acc_n[32];
        end

        // FSM to handle different sample modes
        //
        // LC == 1'b1 is for Companded audio, because we don't implement it
        // we treat it like a normal 8-bit audio stream.
        case (pb_state)
            idle: begin
                // Don't start playing if the buffer is empty
                if (pb_en == 1'b1 && empty == 1'b0) begin
                    endata <= 1'b1; // signal new data is available
                    if (sm == 1'b0) begin
                        pb_state <= STEP_0_MONO;
                    end else begin
                        pb_state <= STEP_0_STEREO;
                    end
                end
            end
            // Handle mono decoding
            STEP_0_MONO: begin
                // Pulse read enable to get new byte
                rd_en <= 1'b1;
                if (rd_en == 1'b1) begin
                    pb_state <= STEP_1_MONO;
                    rd_en <= 1'b0;
                end
            end
            STEP_1_MONO: begin
                if (fmt == 1'b0 || lc == 1'b1) begin // 8-bit
                    // Make 8-bit unsigned to 8 bits signed
                    tmp_8bit_left <= data_in - 8'sh80;
                    pb_state <= STEP_2_MONO;
                end else begin // 16 bit
                    // Store the LSB first
                    tmp_8bit_left <= data_in;
                    rd_en <= 1'b1; // Get the MSB
                    if (rd_en == 1'b1) begin
                        pb_state <= STEP_2_MONO;
                        rd_en <= 1'b0;
                    end
                end
            end
            STEP_2_MONO: begin
                if (fmt == 1'b0 || lc == 1'b1) begin // 8-bit
                    // Output the 8-bit signed data on both left and right channels
                    ldata <= {tmp_8bit_left, 8'h00};
                    rdata <= {tmp_8bit_left, 8'h00};
                end else begin // 16 bit
                    // Output the 16-bit mono on both left and right channels
                    ldata <= {data_in, tmp_8bit_left};
                    rdata <= {data_in, tmp_8bit_left};
                end
                upd <= 1'b1;
                pb_state <= idle;
            end
            // Handle stereo decoding
            STEP_0_STEREO: begin
                // Start by initiating the first read
                rd_en <= 1'b1;
                if (rd_en == 1'b1) begin
                    pb_state <= STEP_1_STEREO;
                    rd_en <= 1'b0;
                end
            end
            STEP_1_STEREO: begin
                if (fmt == 1'b0 || lc == 1'b1) begin // 8-bit
                    // Make 8-bit unsigned to 8 bits signed
                    tmp_8bit_left <= data_in - 8'sh80;
                end else begin // 16 bit
                    // Get left channel lsb first
                    tmp_16bit_left[7:0] <= data_in;
                end
                rd_en <= 1'b1; // read next byte
                if (rd_en == 1'b1) begin
                    pb_state <= STEP_2_STEREO;
                    rd_en <= 1'b0;
                end
            end
            STEP_2_STEREO: begin
                if (fmt == 1'b0 || lc == 1'b1) begin // 8-bit
                    // Make 8-bit unsigned to 8 bits signed
                    tmp_8bit_right <= data_in - 8'sh80;
                    pb_state <= STEP_3_STEREO;
                end else begin // 16-bit
                    // Get left chonnel msb
                    tmp_16bit_left[15:8] <= data_in;
                    rd_en <= 1'b1; // read next byte
                    if (rd_en == 1'b1) begin
                        pb_state <= STEP_3_STEREO;
                        rd_en <= 1'b0;
                    end
                end
            end
            STEP_3_STEREO: begin
                if (fmt == 1'b0 || lc == 1'b1) begin // 8-bit
                    // Output received data
                    ldata <= {tmp_8bit_left, 8'h00};
                    rdata <= {tmp_8bit_right, 8'h00};
                    upd <= 1'b1;
                    pb_state <= idle;
                end else begin // 16-bit
                    // Get right channel lsb first
                    tmp_16bit_right[7:0] <= data_in;
                    rd_en <= 1'b1; // read next byte
                    if (rd_en == 1'b1) begin
                        pb_state <= STEP_4_STEREO;
                        rd_en <= 1'b0;
                    end
                end
            end
            STEP_4_STEREO: begin
                // Output received data on both channels 16-bit
                ldata <= tmp_16bit_left;
                rdata <= {data_in, tmp_16bit_right[7:0]};
                upd <= 1'b1;
                pb_state <= idle;
            end
        endcase
    end
end
endmodule
