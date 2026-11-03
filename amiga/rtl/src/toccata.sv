/*
 * Edited for pano-g2-amiga by Mike Wolak, EPROM Foundry Inc. (the edits: amiga/THIRD_PARTY.md).
 * This file keeps the licence of its origin, stated below or in amiga/nanomig (GPL-3.0-or-later).
 */
// rtl/src/toccata.sv: third_party/fpga-toccata/toccata.sv (ranzbak; MiSTer copy) with
//   - input ntsc to rtl/src/toccata_playback.sv, whose sample clock is an NCO in core time
//   - AD1848 indices 4 and 5 (AUX2 left and right) reset to 0x08 (0 dB, not muted) in place of 0x80 (muted), and
//     their values as outputs aux2_left and aux2_right: rtl/g2/amiga_g2.v applies them to Paula, which a Toccata
//     passes through its AUX2 input; Paula is heard with no Toccata software loaded
//   - the output linearly interpolated between the last two samples of the volume stage, by the time since the
//     later one in units of the sample period (from the NCO's accumulator), so the 48 kHz output of
//     rtl/g2/amiga_g2.v samples a continuous signal; at 48,000 Hz (CSS 0, CFS 6: the output's own rate) and when no
//     sample has come for a whole period (FIFO empty) the latest sample is passed unchanged
/********************************************/
/* toccata.v                                */
/* Toccata sound main                       */
/*                                          */
/* 2022-2023, ranzbak@gmail.com             */
/********************************************/


module toccata #(
    parameter int CLK_FREQUENCY = 28_359_380
) (
    input wire          clk,
    input wire          rst,
    input wire          hsync,
    input wire          ntsc,       // core clock: 1 NTSC (28,636,364 Hz), 0 PAL (28,375,160 Hz)
    // Zorro II interface
    input  wire  [15:0] data_in,
    output logic [15:0] data_out,
    input  wire  [15:1] addr,
    input  wire         rd,
    input  wire         hwr,
    input  wire         lwr,
    input  wire         sel,
    output logic        toc_int, // Toccata interrupt active high
    // Audio output
    output logic [15:0] out_left,
    output logic [15:0] out_right,
    // AD1848 AUX2 input control (indices 4, 5): [7] mute, [4:0] gain, 0 = +12 dB, 8 = 0 dB, 31 = -34.5 dB
    output logic [7:0]  aux2_left,
    output logic [7:0]  aux2_right
);

// Reset values for ad1848 registers
logic [7:0] ad1848_reset_reg [0:15] = '{
    8'h00, // 0 - interrupt control
    8'h00, // 1 - Right input
    8'h00, // 2 - Left input
    8'h80, // 3 - right aux input control
    8'h08, // 4 - left aux 2 input control (0 dB, not muted: Paula)
    8'h08, // 5 - right aux 2 input control (0 dB, not muted: Paula)
    8'h80, // 6 - left DAC control (volume)
    8'h80, // 7 - right DAC control (volume)
    8'h00, // 8 - clock and data format register
    8'h10, // 9 - interface configuration register
    8'h00, // a - pin control register
    8'h00, // b - test and initialization register
    8'h0a, // c - misc control register
    8'h00, // d - mix control register
    8'h00, // e - upper dma count register
    8'h00  // f - lower dma count register
};
localparam logic [7:0] ad1848_reset_index = 8'h40;
// We do net implement byte swap
// logic       ad1848_fifo_play_byteswap = 1'b0;
// logic       ad1848_fifo_record_byteswap = 1'b0; // Not recording, but it's there

// AD1848 register index
logic [7:0] ad1848_index;          // AD1848 current index
logic [7:0]  ad1848_regs [0:15];    // AD1848 registers

// Signals decoded from the ad1848_regs
struct {
    logic        pen;
    logic        cen; // Not used
    logic [2:0]  freq_sel;
    logic        sm;
    logic        lc;
    logic        fmt;
    logic [5:0]  lda; // Left DAC output attenuation
    logic        ldm; // Left DAC mute
    logic [5:0]  rda; // Right DAC output attenuation
    logic        rdm; // Right DAC mute
    logic        css;
    logic        acal;
    logic        aci;
    logic        int_;
    logic        pul;
    logic        plr;
} ad;


// Address decoder masks
// data->codec_reg1_mask = 0x6801; // Register 0 - we only look at the high byte
// data->codec_reg1_addr = 0x6001;
// data->codec_reg2_mask = 0x6801; // Register 1
// data->codec_reg2_addr = 0x6801;
// data->codec_fifo_mask = 0x6800;
// data->codec_fifo_addr = 0x2000;
localparam CODEC_STATUS     = 3'b000; // Address 'h0000
localparam CODEC_FIFO       = 3'b010; // FIFO    'h2000-27ff
localparam CODEC_REG_1      = 3'b110; // Address 'h6000-67ff
localparam CODEC_REG_2      = 3'b111; // Address 'h6800-68ff
// TOCC_FIFO_STAT   0x1ffe

// Status register bits:
// Halt playback :
//      (disables interrupt, let buffer drain)
//      - 0x01, 0x04, 0x10 (0, 2, 4)
// Start playback :
//      (Start playback without interrupt)
//      - 0x01 (0)
//      - 0x01, 0x10 (0, 4)
//  - Fill buffer
//      (Enable codec and enable interrupt)
//      - 0x01, 0x04, 0x10, 0x80
// localparam STATUS_ACTIVE = 0; // replaced with compare 0x01 in code, because needs to be exact match
localparam STATUS_RESET = 1;
localparam STATUS_FIFO_CODEC = 2; // In NetBSD driver TOC_MAGIC
localparam STATUS_FIFO_RECORD = 3;
localparam STATUS_FIFO_PLAY = 4;
localparam STATUS_RECORD_INTENA = 6;
localparam STATUS_PLAY_INTENA = 7;
logic [7:0]  toc_status; // Toccata status register

// IRQ register bits (CODEC_STATUS) 16'h0000:
localparam IRQ_RECORD_HALF = 2;
localparam IRQ_PLAY_HALF   = 3;
localparam IRQ_INT_IRQ     = 7;
logic       clear_irq;     // Clear interrupt after read has ended
logic [7:0] irq_reg;

// FIFO change registers
// logic [1:0] fifo_play_half_;   // Delta FIFO half
// logic [1:0] fifo_cap_half_;   // Delta FIFO half
// logic [1:0] fifo_play_half_next; // Delta FIFO half next state
// logic [1:0] fifo_cap_half_next; // Delta FIFO half next state
logic [1:0] acal_;          // ACAL state
logic [1:0] acal_next;      // ACAL next state
logic [1:0] hsync_;         // HSYNC state
logic [1:0] hsync_next;     // HSYNC next state
logic       write_second_byte; // write second byte to FIFO
logic [7:0] second_byte;    // second byte value
logic       rd_;            // READ edge detect
logic       lwr_;           // LWR edge detect
logic       hwr_;           // HWR edge detect

logic [5:0] auto_callibration; // auto callibration counter

// interrupt change registers
struct {
    logic rst;
    logic wr_en;
    logic rd_en;
    logic [7:0] data_in;
    logic full;
    logic empty;
    logic half_full;
    logic half_empty;
    logic [7:0] data_out;
    logic endata;
} fifo;

/*
 * The Toccata board consists of: GALs for ZBus AutoConfig(tm) glue, GALs
 * that interface the FIFO chips and the audio codec chip to the ZBus,
 * an AD1848 (or AD1845), and 2 Integrated Device Technology 7202LA
 * (1024x9bit FIFO) chips.
 */

// Address decoder

// Sample playback to volume
logic [15:0]  playback_left;
logic [15:0]  playback_right;
logic [31:0]  pb_acc;
logic         pb_upd;
logic [15:0]  vol_left;
logic [15:0]  vol_right;

// Contains written byte through hwr or lwr
logic [7:0] din_byte;
logic       fifo_rst_playback;

// logic       toc_int_prev;
// logic       toc_int_cur;

logic       loc_rd_en;

logic [2:0] reg_select;

// Toccata FIFO to 2-complement 16-bit audio output
toccata_playback #(
    .CLK_FREQUENCY(CLK_FREQUENCY)
) my_playback (
    .clk(clk),
    .rst(rst),
    .pen(ad.pen),
    .freq_sel(ad.freq_sel),
    .sm(ad.sm),
    .lc(ad.lc),
    .fmt(ad.fmt),
    .css(ad.css),
    .ntsc(ntsc),

    .rst_fifo(fifo_rst_playback),
    .rd_en(fifo.rd_en),
    .data_in(fifo.data_out),
    .empty(fifo.empty),

    .ldata(playback_left),
    .rdata(playback_right),
    .endata(fifo.endata),
    .nco_acc(pb_acc),
    .upd(pb_upd)
);

// Dummy Toccata audio capture module
struct {
    logic [7:0] data_out;
    logic       rd;
    logic       empty;
    logic       half_full;
    logic       full;
    logic       endata;
} cap;
toccata_capture #(
    .CLK_FREQUENCY(CLK_FREQUENCY)
) my_capture (
    .clk(clk),
    .rst(rst),

    .cen(ad.cen),
    .freq_sel(ad.freq_sel),
    .sm(ad.sm),
    .fmt(ad.fmt),
    .css(ad.css),

    .data_out(cap.data_out),
    .rd(cap.rd),
    .empty(cap.empty),
    .half_full(cap.half_full),
    .full(cap.full),

    .endata(cap.endata)
);


// Volume playback
toccata_volume my_toccata_volume (
    .clk(clk),
    .rst(rst),
    .audio_in_left(playback_left),
    .audio_in_right(playback_right),
    .attenuation_left(ad.lda),
    .mute_left(ad.ldm),
    .attenuation_right(ad.rda),
    .mute_right(ad.rdm),
    .audio_out_left(vol_left),
    .audio_out_right(vol_right)
);

assign aux2_left  = ad1848_regs[4];
assign aux2_right = ad1848_regs[5];

// Interpolation.  The volume stage registers one clock after playback writes a sample (pb_upd), so the pair
// moves at upd_d: s0 <= s1, s1 <= the new sample, acc_u <= the accumulator then.  frac = (acc - acc_u) / 2^16
// runs from 0 to just under 2^16 over one sample period.  A second carry of the accumulator without a new sample
// (FIFO empty) holds s1.  Three pipeline stages: difference and fraction, product (DSP), sum.
logic               upd_d = 1'b0;
logic signed [15:0] s0_l = 16'sd0, s1_l = 16'sd0, s0_r = 16'sd0, s1_r = 16'sd0;
logic [31:0]        acc_u = 32'd0;
logic               acc_msb = 1'b0;
logic [1:0]         carries = 2'd0;
logic               pass_1 = 1'b1, pass_2 = 1'b1;
logic [15:0]        frac_1 = 16'd0;
logic signed [16:0] dl_1 = 17'sd0, dr_1 = 17'sd0;
logic signed [15:0] b0l_1 = 16'sd0, b0r_1 = 16'sd0, b0l_2 = 16'sd0, b0r_2 = 16'sd0;
logic signed [15:0] b1l_1 = 16'sd0, b1r_1 = 16'sd0, b1l_2 = 16'sd0, b1r_2 = 16'sd0;
logic signed [33:0] pl_2 = 34'sd0, pr_2 = 34'sd0;
wire  [31:0]        acc_d = pb_acc - acc_u;
wire                carry = acc_msb && !pb_acc[31];
wire signed [16:0]  ql = pl_2[32:16], qr = pr_2[32:16];
wire signed [16:0]  il = {b0l_2[15], b0l_2} + ql, ir = {b0r_2[15], b0r_2} + qr;
always_ff @(posedge clk) begin
    upd_d   <= pb_upd;
    acc_msb <= pb_acc[31];
    if (upd_d) begin
        s0_l <= s1_l; s1_l <= vol_left; s0_r <= s1_r; s1_r <= vol_right;
        acc_u <= pb_acc; carries <= 2'd0;
    end else if (carry && carries != 2'd2) begin
        carries <= carries + 2'd1;
    end
    // stage 1
    pass_1 <= ({ad.css, ad.freq_sel} == 4'h6) || carries == 2'd2 || !ad.pen;
    frac_1 <= acc_d[31:16];
    dl_1   <= {s1_l[15], s1_l} - {s0_l[15], s0_l};
    dr_1   <= {s1_r[15], s1_r} - {s0_r[15], s0_r};
    b0l_1  <= s0_l; b0r_1 <= s0_r; b1l_1 <= s1_l; b1r_1 <= s1_r;
    // stage 2
    pass_2 <= pass_1;
    pl_2   <= dl_1 * $signed({1'b0, frac_1});
    pr_2   <= dr_1 * $signed({1'b0, frac_1});
    b0l_2  <= b0l_1; b0r_2 <= b0r_1; b1l_2 <= b1l_1; b1r_2 <= b1r_1;
    // stage 3: between two 16-bit samples, so within 16 bits
    out_left  <= pass_2 ? b1l_2 : il[15:0];
    out_right <= pass_2 ? b1r_2 : ir[15:0];
end


// Sound FIFO, used to hold the ad1848 sound data
toccata_fifo #(
    .DATA_WIDTH(8),
    .FIFO_DEPTH(1024)
) myfifo (
    .clk(clk),
    .rst(fifo.rst | fifo_rst_playback),
    .wr_en(fifo.wr_en),
    .rd_en(fifo.rd_en || loc_rd_en),
    .data_in(fifo.data_in),
    .full(fifo.full),
    .empty(fifo.empty),
    .half_full(fifo.half_full),
    .half_empty(fifo.half_empty),
    .data_out(fifo.data_out)
);

always_comb begin


    // Generate reg select pattern
    reg_select = {addr[14], addr[13], addr[11]};

    // Current interrupt register value
    // toc_int_cur = !irq_reg[IRQ_INT_IRQ];

    // Decode AD1848 audio registers
    ad.int_ = ad1848_regs[2][0];
    ad.lda = ad1848_regs[6][5:0];
    ad.ldm = ad1848_regs[6][7];
    ad.rda = ad1848_regs[7][5:0];
    ad.rdm = ad1848_regs[7][7];
    ad.css = ad1848_regs[8][0];         // Crystal select
    ad.freq_sel = ad1848_regs[8][3:1];
    ad.sm = ad1848_regs[8][4];
    ad.lc = ad1848_regs[8][5];
    ad.fmt = ad1848_regs[8][6];
    ad.pen = ad1848_regs[9][0];
    ad.cen = ad1848_regs[9][1];
    ad.acal = ad1848_regs[9][3];
    ad.aci = ad1848_regs[11][5];

    // Detect changes in the signals that generate interrupts
    // fifo_play_half_next = {fifo_play_half_[0], fifo.half_empty};
    // fifo_cap_half_next = {fifo_cap_half_[0], cap.half_full};
    acal_next = {acal_[0], ad.acal};
    hsync_next = {hsync_[0], hsync};

    // Get the byte written from the high or low byte
    // High byte write has priority
    din_byte = lwr ? data_in[7:0] : hwr ? data_in[15:8] : 8'h00;
end


always_ff @(posedge clk) begin
    fifo.wr_en <= 1'b0;
    loc_rd_en <= 1'b0;
    fifo.rst <= 1'b0;
    write_second_byte <= 1'b0;

    // Edge detect registers write
    rd_ <= rd;
    lwr_ <= lwr;
    hwr_ <= hwr;

    // generate interrupt pulses
    // Set the interrupt until cleared
    if (irq_reg[IRQ_INT_IRQ] == 1'b1) begin
        irq_reg[IRQ_INT_IRQ] <= !(|irq_reg[6:0]); // Active low
    end
    toc_int <= !irq_reg[IRQ_INT_IRQ];

    // toc_int_prev <= toc_int_cur;

    if (rst == 1) begin
        // Reset the ad1848 registers
        for(int i = 0; i < 16; i++) begin
            ad1848_regs[i] <= ad1848_reset_reg[i];
        end
        // Reset the FIFO
        fifo.rst <= 1'b1;
        ad1848_index <= ad1848_reset_index;
        data_out <= 16'h0000;
        irq_reg <= 8'h80;
        auto_callibration <= 0;
        toc_status <= 0; // Initial status set
        clear_irq <= 0;
    end else begin
        // Update int trigger records
        // fifo_play_half_ <= fifo_play_half_next;
        // fifo_cap_half_ <= fifo_cap_half_next;
        acal_ <= acal_next;
        hsync_ <= hsync_next;

        // Play half empty interrupt
        if (fifo.half_empty && toc_status[STATUS_FIFO_PLAY] == 1'b1 && toc_status[STATUS_PLAY_INTENA] == 1'b1) begin
            irq_reg[IRQ_PLAY_HALF] <= 1'b1; // Half empty interrupt
        end
        // Capture half full interrupt
        if (cap.half_full && toc_status[STATUS_FIFO_RECORD] == 1'b1 && toc_status[STATUS_RECORD_INTENA] == 1'b1) begin
            irq_reg[IRQ_RECORD_HALF] <= 1'b1; // Half full interrupt
        end

        // Trigger on acal bit change
        if (acal_next == 2'b01) begin
            auto_callibration <= 50;
        end

        // Set Playback Underrun bit when FIFO is empty
        ad1848_regs[11][6] <= fifo.empty;
        // Set Capture overrun bit when FIFO is full
        ad1848_regs[11][7] <= cap.full;

        // Simulate auto callibration cycle
        if (hsync_next == 2'b01 && auto_callibration > 0) begin
            auto_callibration <= auto_callibration - 1'd1;
        end
        if (auto_callibration > 10 && auto_callibration < 30) begin
            // Auto callibration in progress
            ad1848_regs[11][5] <= 1'b1; // Set ACAL bit to indicate callbiration in progress
        end else begin
            // Auto callibration done
            ad1848_regs[11][5] <= 1'b0; // Reset ACAL bit
        // ad1848_regs[9][3] <= 1'b0;  // Reset ACI bit
        end

        // Trigger interrupt if the FIFO state changes

        // Handle second byte to FIFO first
        // This is to handle 16-bit writes
        if (write_second_byte == 1'b1) begin
            // Write second byte to the FIFO
            fifo.wr_en <= 1'b1;
            fifo.data_in <= second_byte;
            write_second_byte <= 1'b0;
        end else if (sel == 1'b1) begin
            // When selected start answering

            // =================================================================
            // WRITE data into the registers
            // =================================================================
            if (lwr || hwr) begin // Might need to trigger on both

                case (reg_select)
                    CODEC_STATUS: begin // 'h00xx - status register
                        // If the reset bit is set, stop codec, reset fifo
                        if (din_byte[STATUS_RESET] == 1'b1) begin
                            // Reset the card, and stop playback
                            fifo.rst <= 1'b1;
                            irq_reg <= 8'h80; // Clear all interrupts

                            // Reset registers
                            ad1848_regs[9][0] <= 1'b0;
                            ad1848_regs[9][1] <= 1'b0;
                            ad1848_regs[9][6] <= 1'b0;
                            ad1848_regs[9][7] <= 1'b0;
                            ad1848_regs[10][1] <= 1'b0;

                            // Status register clear
                            toc_status <= 8'h00; // Inactivate card
                        end else begin
                            // When not resetting, store the status
                            toc_status <= din_byte;

                            // When only Status active is set and nothing else,
                            // Reset the buffer
                            if (din_byte == 8'h01) begin // STATUS_ACTIVE
                                // Activate card
                                fifo.rst <= 1'b1; // Start with a clean FIFO
                                irq_reg <= 8'h80; // Clear all interrupts
                            end

                            // Store bytes in the AD1848 registers
                            ad1848_regs[9][0] <= din_byte[STATUS_FIFO_CODEC] ? din_byte[STATUS_FIFO_PLAY] : 1'b0;
                            ad1848_regs[9][1] <= din_byte[STATUS_FIFO_CODEC] ? din_byte[STATUS_FIFO_RECORD] : 1'b0;
                            ad1848_regs[9][6] <= din_byte[STATUS_PLAY_INTENA];
                            ad1848_regs[9][7] <= din_byte[STATUS_RECORD_INTENA];
                            ad1848_regs[10][1] <= din_byte[STATUS_PLAY_INTENA] | din_byte[STATUS_RECORD_INTENA];
                            // Unmute channels
                            ad1848_regs[6][7] <= din_byte[STATUS_FIFO_CODEC] ? !din_byte[STATUS_FIFO_PLAY] : 1'b1;
                            ad1848_regs[7][7] <= din_byte[STATUS_FIFO_CODEC] ? !din_byte[STATUS_FIFO_PLAY] : 1'b1;
                        end
                    end
                    CODEC_FIFO: begin // 'h20xx - FIFO register
                        // TODO: Evaluate later, STATUS_FIFO_PLAY should be taken into account
                        // but driver initialization seems to indicate not??
                        // if (toc_status[STATUS_FIFO_PLAY] == 1'b1 && fifo.full == 1'b0) begin
                        // if (fifo.full) begin
                        // Write value only when presented in the lower byte
                        if (lwr == 1'b1 && lwr_ == 1'b0) begin
                            // Write byte to the FIFO
                            fifo.wr_en <= 1'b1;
                            fifo.data_in <= data_in[7:0];
                        end

                        if (hwr == 1'b1 && hwr_ == 1'b0) begin
                            // Write high byte to FIFO as well
                            write_second_byte <= 1'b1;
                            second_byte <= data_in[15:8];
                        end

                        // On write to FIFO clear FIFO half empty flag
                        irq_reg[IRQ_PLAY_HALF] <= 1'b0;
                    // end
                    end
                    CODEC_REG_1: begin // 'h60xx - Index register
                        if (lwr == 1'b1) begin
                            ad1848_index <= data_in[7:0]; // mod 16
                            `ifdef DEBUG
                            $display("Set index: %1h", data_in[3:0]);
                            `endif
                        end
                    end
                    CODEC_REG_2: begin // 'h68xx - AD1848 register
                        if (lwr == 1'b1) begin
                            `ifdef DEBUG
                            $display("write - index: %1h data: %2h", ad1848_index, data_in[15:8]);
                            `endif
                            case (ad1848_index[3:0])
                                4'h3: begin
                                    // PIO register
                                    fifo.wr_en <= 1'b1;
                                    fifo.data_in <= data_in[7:0];
                                end
                                4'hc: begin
                                // Ignore
                                end
                                default: begin
                                    // Only write to the first 16 register, there is no more
                                    // Store the values in the registers
                                    ad1848_regs[ad1848_index[3:0]] <= data_in[7:0];
                                end
                            endcase
                        end
                    end
                endcase
            end

            // =================================================================
            // READ data from the registers
            // =================================================================
            else if (rd == 1'b1) begin
                case ({addr[14], addr[13], addr[11]})
                    CODEC_STATUS: begin // 'h00xx - status register
                        // Clear the interrupt register after reading
                        // IRQ_INT_IRQ is active low, so reset
                        clear_irq <= 1'b1;

                        // Return the current interrupt status register
                        data_out <= {irq_reg, irq_reg};
                    end
                    CODEC_FIFO: begin // 20xx - FIFO register
                        if (ad.cen == 0) begin
                            // When not recording, return current byte in the playback FIFO
                            if (fifo.empty == 1'b0 && rd_ == 1'b0) begin
                                loc_rd_en <= 1'b1;
                            end
                            data_out <= {8'h00, fifo.data_out};
                        end else begin
                            // Return dummy value from record module
                            data_out <= {8'h00, cap.data_out};
                        end
                    end
                    CODEC_REG_1: begin // 60xx - INDEX REGISTER
                        // Return the current index address
                        // Is the high byte addre xxx0 and the low byte addre xxx1 ?
                        data_out <= {8'h00, ad1848_index};
                    end
                    CODEC_REG_2: begin // 68xx - AD1848 register
                        case (ad1848_index)
                            8'h03: begin
                                data_out <= 16'h0000;
                            end
                            default: begin
                                // Return the current value 16 registers anything else wrap
                                data_out <= {8'h000, ad1848_regs[ad1848_index[3:0]]};
                            end
                        endcase
                    end
                    default: begin
                    // Do nothing
                    end
                endcase
            end
        end else begin
            // Make sure no data is on the output when not selected
            data_out <= 16'h0000;
            // Clear irq if requested
            if (clear_irq) begin
                irq_reg <= 8'h80;
                clear_irq <= 0;
            end
        end

// Paula sound forward
// register 4 for left channel value
// register 5 for right channel value

    end
end

endmodule