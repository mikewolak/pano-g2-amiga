/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// vga_scan.v -- the complete video head: timing + upscale-only DDA scaler +
// pipelined SDRAM line fetcher + atomic buffer swap.
//
// Design contract:
//   * Output is ONLY ever vga_timing's two modes (SW[0]).  Source images are
//     arbitrary WxH up to 800x600 (de2cli enforces 4:3 + upscale-only) and
//     are stretched by nearest-neighbor DDA at scan-out.
//   * ALL scan state lives coherently in the PIXEL domain and every config
//     change (display base, source dims) applies at one frame boundary via
//     the swap toggle -- an upload can never tear the visible frame.  The
//     swap command carries an arbitrary {base, w, h}, which is also the
//     whole double-buffered-animation story: upload idle buffer, swap.
//   * The fetcher is a READ-ONLY SDRAM client that receives fully computed
//     {word address, word count, target buffer} per line, so it needs no
//     configuration of its own and no multiplier: the pixel side advances
//     the line address incrementally (+2*src_w per source line).
//   * Bandwidth-by-construction: upscale-only means a source line is reused
//     for >= 1 (mode 0) / >= 2 (mode 1) output lines, so the fetch window is
//     always >= one 800x600 line time = 2640 fclk >= 1600 words.  The gate
//     (tb_vga_scan) proves the fetcher makes rate through the real
//     controller against the JEDEC-policing model.
//
// Pixel pipeline (t = pixclk stages):
//   t0 vga_timing registered outputs (x,y,active,...)
//   t1 line-buffer read address (horizontal DDA)
//   t2 line-buffer registered read data
//   t3 output registers r/g/b (gated by blank), hs/vs/blank delayed to match
// The OSD composites between t2 and t3 (rgb_t2 out, rgb_osd back in); the
// top ties rgb_osd = rgb_t2 when no OSD is present.
module vga_scan #(
    parameter AW = 22,                // frame-buffer word address width (22: 8 MB of 16-bit words; 23: 16 MB, bit 22 on req_hi)
    parameter LB_AW = 11,             // line buffer address bits: source width <= 2^(LB_AW-1); 11: 1,024 pixels, 10: 512
    /* THE NCO INCREMENTS ARE PARAMETERS: pixels per fclk cycle in Q32.32, one per mode.  They are a property of
     * the fclk the head is built on, not of the mode: the defaults are the 100 MHz values and reproduce the
     * table this file has always carried.  A build on another fclk passes its own (rtl/vga/g2_video.v: fclk
     * 80 MHz, each pixel rate an exact dyadic fraction of it, so a downstream pixel-clock FIFO never drifts). */
    parameter [63:0] INC_M0 = {32'd0, 32'h4072B021},   // 640x480:   25.175 MHz at fclk 100 MHz
    parameter [63:0] INC_M1 = {32'd0, 32'h66666666},   // 800x600:   40.000 MHz
    parameter [63:0] INC_M2 = {32'd0, 32'hA6666666},   // 1024x768:  65.000 MHz
    parameter [63:0] INC_M4 = {32'd0, 32'h1126E979},   // 320x240p:   6.700 MHz
    parameter [63:0] INC_MD = {32'd1, 32'd0},          // default arm (no mode reaches it), 1600x1200RB: one pixel per fclk
    parameter [2:0]  MODE_RESET = 3'd1                 // the mode the raster runs from reset until the first frame strobe
) (
    // ---- ONE domain: everything at fclk -----------
    // The PLL-clocked pixel domain is GONE -- the dirty CLOCK_27 reference
    // dropped the video PLL's lock >100x/s (vdbg: VS 223 Hz = frames dying
    // at line ~171) and no mux could fix a reference.  nco_raster stretches
    // pixels fractionally on the proven 100 MHz clock instead; scan rates
    // are register data (user architecture).  CDC between pixel and fetch
    // logic no longer exists.
    input  wire [2:0]  mode,          // 0=640x480 1=800x600 2=1024x768
                                      // 3=1600x1200@46  4=320x240p@15kHz
    /* PALETTED MODE.  pal_en makes a source pixel ONE BYTE looked up in vga_palette during
     * scanout, instead of two 16-bit words the CPU had to build.  It changes three things and nothing
     * else: the line is a quarter as many words to fetch, a returned word goes STRAIGHT into the line
     * buffer as two pixels rather than through the half-pairing ring, and the read side selects a byte and
     * looks it up.  The timing, the scaler, the OSD and the DAC tap are all untouched.
     *
     * The CPU stops expanding 64,000 bytes into colours (41 ms a frame) and points the head at the byte
     * frame it already drew. */
    input  wire        pal_en,
    output wire [2:0]  mode_cur,      // fclk: the mode the raster applies (taken at the frame strobe)
    /* RGB565 SOURCE.  fmt565 makes a source pixel ONE 16-bit word {R[15:11], G[10:5], B[4:0]}, widened to the
     * 30-bit line-buffer entry by bit replication (R and B 5 -> 10, G 6 -> 10), written straight in as it
     * arrives like a paletted word: one word per pixel, one entry per pixel, no palette.  pal_en wins over
     * fmt565 when both are set.  The direct-colour source (neither) stays two words per pixel.  fmt565 crosses into
     * fclk and is applied at the frame strobe exactly as pal_en is (f565_ap), and the line engine latches it per line
     * command (l565). */
    input  wire        fmt565,
    input  wire        pal_we,        // palette write strobe, pclk domain
    input  wire        pclk,          // the register page's clock (palette write side)
    input  wire [7:0]  pal_wa,
    input  wire [29:0] pal_wd,
    output reg  [9:0]  vr, vg, vb,   // DAC-side video
    output reg         v_hs, v_vs, v_blank_n,
    /* THE DAC'S CLOCK, AND IT MUST NOT STOP DURING BLANKING.
     *
     * This was `pe4 & bl4' -- gated by blanking -- and rtl/pano_g1_top.v clocks the THS8135 from it, so the
     * DAC GOT NO CLOCK EDGES WHILE BLANKED.  It never latched the zeros this module already puts on
     * vr/vg/vb when blanked, and -- the part that does the damage -- it never sampled BLANK_ either, which
     * is a synchronous input on that part.  The DAC held the LAST ACTIVE PIXEL of the line right through the
     * back porch, which is where a monitor clamps its black reference: the monitor set black to whatever was
     * at the right-hand edge of the picture and crushed everything below it.  Measured on the glass as
     * nothing visible under about 160/255, with the threshold following the picture content.
     *
     * The gate is simply REMOVED rather than a second strobe added beside it.  The first attempt added
     * `pix_stb_all <= pe4' at the same pipeline stage and left this one alone -- and that build FAILED THE
     * TIMING GATE, pushing the LPDDR controller's clk0/clk90 domains to 10.766/11.314 ns against a 10 ns
     * requirement.  At 90% slice occupancy and 36 of 36 block RAMs there is no room for extra logic, so the
     * right shape was the one that takes an AND term AWAY.  Nothing else consumes this strobe: it reaches
     * pano_soc_vex.v's vo_pix_stb and from there only vo_clk. */
    output reg         pix_stb,      // one strobe per pixel period, blanked or not: the DAC's clock
    output wire        pix_en,       // NCO pixel enable (incl blanking) = pixel clock
    output wire [11:0] osd_x, osd_y,
    output wire        osd_active,
    output wire [29:0] rgb_t2,
    input  wire [29:0] rgb_osd,
    output wire        frame_stb,

    input  wire        fclk,
    input  wire        frst_n,
    input  wire        init_done,
    input  wire        swap_tog,      // flip: apply base/dims at next vblank
    input  wire [AW-1:0] swap_base,   // word address of buffer to display
    input  wire [10:0] swap_w,        // its source width  (<= 800)
    input  wire [10:0] swap_h,        // its source height (<= 600)

    // ---- OSD (fclk domain) ------------------------------------------------
    input  wire        osd_en,        // overlay on/off
    input  wire        osd_wtog,      // toggles per host char write
    input  wire [7:0]  osd_wpos,      // cell 0..255
    input  wire [7:0]  osd_wchar,     // char code
    // hardware OSD write port (fclk-native, e.g. the line demo's lines/sec).
    // The host's synced-edge write wins on a collision; hw traffic is a few
    // chars per second, so a lost char just reappears next refresh.
    input  wire        hw_osd_we,
    input  wire [7:0]  hw_osd_pos,
    input  wire [7:0]  hw_osd_char,
    // currently-DISPLAYED buffer base -- so a drawing client (line demo) can
    // target the visible framebuffer across vblank swaps
    output wire [AW-1:0] fb_cur_base,

    // ---- SDRAM client (fclk domain), read-only ----------------------------
    output reg  [3:0]  req,
    output wire [3:0]  req_we,
    output reg  [47:0] req_row,
    output reg  [3:0]  req_hi,        // word address bit 22 per bank (AW 23); 0 with AW 22
    output reg  [31:0] req_col,
    output wire [63:0] req_wdata,
    input  wire [3:0]  rdy,
    input  wire [15:0] rdata,
    input  wire        rvalid,
    input  wire [1:0]  rbank
);
    assign req_we    = 4'd0;          // never writes
    assign req_wdata = 64'd0;

    // ---- all cross-referenced declarations up front (ModelSim rule) ------
    reg         fack_tog;             // fclk: line-fetch done toggle
    reg         ftog;                 // line-fetch request toggle (same domain
    reg  [AW-1:0] faddr;                //   now; the sync flops below are just
    reg  [11:0] fwords;               //   pipeline stages, kept for zero-risk)
    reg         fbuf;
    reg         fpal;                 // the line's pixel format (pal_ap when the command was made)
    reg         f565;                 // the line is RGB565 (f565_ap and not pal_ap when the command was made)
    reg  [10:0] lb_ra;
    reg         psel1, psel2, psel3;   // paletted byte select, walked down beside the pixel it belongs to
    wire [29:0] lb_q;
    /* ALIGNMENT, because being one pixel out here is a picture that looks almost right.
     *   N    lb_ra and psel1 are set at the pixel boundary
     *   N+1  lb_q is back from the line buffer
     *   N+2  rgb3 holds it, and psel3 (psel1 delayed twice) is the byte select FOR THAT WORD
     *   N+3  pal_q is back from the palette, beside rgb4
     * So the lookup address comes off rgb3, and the compose stage takes pal_q instead of rgb4.  The
     * palette read occupies the t3->t4 stage that already does a synchronous fontrom read, so paletted
     * mode adds NO pipeline stage and moves no existing one. */
    /* pal_ra is DOWN BESIDE rgb3's declaration, not here: it reads rgb3, and XST enforces declare-before-use
     * (HDLCompilers:28) where both simulators accept the forward reference silently. */
    wire [29:0] pal_q;
    reg         lb_we;                // fclk: line-buffer write port
    reg  [10:0] lb_wa;
    reg  [29:0] lb_wd;

    vga_linebuf #(.AW(LB_AW)) u_lb (
        .wclk(fclk), .we(lb_we), .waddr(lb_wa), .wdata(lb_wd),
        .rclk(fclk), .raddr(lb_ra), .rdata(lb_q));

    // =======================================================================
    // raster: geometry as data, one clock (rev-2 NCO)
    // =======================================================================
    /* mode arrives from the 50 MHz monitor domain; 2FF it into fclk BEFORE
     * the geometry mux.  Without this, TAN scores the crossing at -1.5 ns
     * (75 paths, the whole vc/hc fan-in) and an async change could tear one
     * frame's geometry.  Synced, the path dies at a flop and a mode change
     * is clean within 2 cycles. */
    /* MODE AND PAL_EN ARE FRAME-SYNCHRONOUS.  Both arrive from the register page's clock; each is taken
     * through two flops and then APPLIED ONLY AT THE FRAME STROBE (mode_ap, pal_ap), as the base and the geometry of
     * the source already were.  Before, pal_en reached the fetch engine with no synchroniser and at any time: a write
     * that flipped it while a direct-colour line was in flight left one 16-bit half in the pairing ring, the line's
     * end test (all words back AND the ring empty) never came true, `running' stayed set, no further line was
     * fetched and the head showed a stale line buffer until the system reset (measured on a G2 revision C unit: the MCB read
     * counter of head 2 frozen at 272,346,974).  Now a frame is scanned in one mode and one pixel format from its
     * first line to its last; the line engine also latches the format per line (lpal) and cannot be stranded by a
     * lone half (see the end-of-line test). */
    reg [2:0] mode_s1, mode_s2, mode_ap;
    reg       pal_s1, pal_s2, pal_ap;
    reg       f565_s1, f565_s2, f565_ap;                  // fmt565 on the same path as pal_en
    wire      frame_ap;                                   // the raster's frame strobe (frame0, below)
    always @(posedge fclk or negedge frst_n)
        if (!frst_n) begin mode_s1 <= MODE_RESET; mode_s2 <= MODE_RESET; mode_ap <= MODE_RESET; pal_s1 <= 1'b0; pal_s2 <= 1'b0; pal_ap <= 1'b0;
                           f565_s1 <= 1'b0; f565_s2 <= 1'b0; f565_ap <= 1'b0; end
        else begin
            mode_s1 <= mode; mode_s2 <= mode_s1; pal_s1 <= pal_en; pal_s2 <= pal_s1;
            f565_s1 <= fmt565; f565_s2 <= f565_s1;
`ifdef VGA_SCAN_FAULT_ASYNC
            mode_ap <= mode_s2; pal_ap <= pal_en; f565_ap <= fmt565;   /* planted: the unsynchronised, immediate path */
`else
            if (frame_ap) begin mode_ap <= mode_s2; pal_ap <= pal_s2; f565_ap <= f565_s2; end
`endif
        end

    reg  [63:0] pix_inc; reg [11:0] g_hact,g_hfp,g_hsy,g_hbp,g_vact,g_vfp,g_vsy,g_vbp;
    reg         g_hspol, g_vspol;
    /* THE GEOMETRY IS REGISTERED: as a combinational case on mode_s2 it sat in front of
     * the NCO's 32-bit add and every counter compare, and TRCE measured mode_s2 -> u_t/hc at 17.0 ns over 14 levels
     * against the 10 ns clock (874 endpoints).  One more clock of mode latency (three instead of two), nothing else
     * changes: the mode is static while a frame scans. */
    always @(posedge fclk) begin
        case (mode_ap)
        3'd0: begin pix_inc=INC_M0;             // 25.175 MHz avg: VESA exact
              g_hact=12'd640;  g_hfp=12'd16; g_hsy=12'd96;  g_hbp=12'd48;
              g_vact=12'd480;  g_vfp=12'd10; g_vsy=12'd2;   g_vbp=12'd33;
              g_hspol=1'b1; g_vspol=1'b1; end
        3'd1: begin pix_inc=INC_M1;             // 40.000 MHz avg
              g_hact=12'd800;  g_hfp=12'd40; g_hsy=12'd128; g_hbp=12'd88;
              g_vact=12'd600;  g_vfp=12'd1;  g_vsy=12'd4;   g_vbp=12'd23;
              g_hspol=1'b0; g_vspol=1'b0; end
        3'd2: begin pix_inc=INC_M2;             // 65.000 MHz avg
              g_hact=12'd1024; g_hfp=12'd24; g_hsy=12'd136; g_hbp=12'd160;
              g_vact=12'd768;  g_vfp=12'd3;  g_vsy=12'd6;   g_vbp=12'd29;
              g_hspol=1'b1; g_vspol=1'b1; end
        3'd4: begin pix_inc=INC_M4;             // 6.700 MHz avg: 240p @15.7kHz
              g_hact=12'd320;  g_hfp=12'd8;  g_hsy=12'd30;  g_hbp=12'd68;
              g_vact=12'd240;  g_vfp=12'd3;  g_vsy=12'd3;   g_vbp=12'd16;
              g_hspol=1'b1; g_vspol=1'b1; end     // 15 kHz arcade/240p
        3'd5: begin pix_inc=INC_M0;             // mode 0's clock, 800 x 520: 60.10 Hz at 25.000 MHz (60.1 Hz frame rate)
              g_hact=12'd640;  g_hfp=12'd16; g_hsy=12'd96;  g_hbp=12'd48;
              g_vact=12'd480;  g_vfp=12'd5;  g_vsy=12'd2;   g_vbp=12'd33;
              g_hspol=1'b1; g_vspol=1'b1; end
        3'd6: begin pix_inc=INC_M4;             // mode 4's clock, 427 x 261: 60.09 Hz at 6.696 MHz (60.1 Hz frame rate)
              g_hact=12'd320;  g_hfp=12'd8;  g_hsy=12'd30;  g_hbp=12'd69;
              g_vact=12'd240;  g_vfp=12'd3;  g_vsy=12'd3;   g_vbp=12'd15;
              g_hspol=1'b1; g_vspol=1'b1; end
        3'd3: begin pix_inc=INC_M4 << 1;        // twice mode 4's clock, 856 x 262: 15.646 kHz, 59.72 Hz at 13.393 MHz
              g_hact=12'd640;  g_hfp=12'd16; g_hsy=12'd60;  g_hbp=12'd140;   // (640x240, 15 kHz NTSC)
              g_vact=12'd240;  g_vfp=12'd3;  g_vsy=12'd3;   g_vbp=12'd16;
              g_hspol=1'b1; g_vspol=1'b1; end
        3'd7: begin pix_inc=INC_M4 << 1;        // twice mode 4's clock, 856 x 312: 15.646 kHz, 50.15 Hz at 13.393 MHz
              g_hact=12'd640;  g_hfp=12'd16; g_hsy=12'd60;  g_hbp=12'd140;   // (640x288, 15 kHz PAL)
              g_vact=12'd288;  g_vfp=12'd6;  g_vsy=12'd3;   g_vbp=12'd15;
              g_hspol=1'b1; g_vspol=1'b1; end
        default: begin pix_inc=INC_MD;           // 100 MHz: 1600x1200RB @46
              g_hact=12'd1600; g_hfp=12'd48; g_hsy=12'd32;  g_hbp=12'd80;
              g_vact=12'd1200; g_vfp=12'd3;  g_vsy=12'd4;   g_vbp=12'd30;
              g_hspol=1'b0; g_vspol=1'b1; end
        endcase
    end

    wire        hs0, vs0, blank0, frame0, line0, pen0;
    wire [11:0] x0, y0;
    nco_raster u_t (
        .fclk(fclk), .rst_n(frst_n),
        .pix_inc(pix_inc),
        .h_act(g_hact), .h_fp(g_hfp), .h_sy(g_hsy), .h_bp(g_hbp),
        .v_act(g_vact), .v_fp(g_vfp), .v_sy(g_vsy), .v_bp(g_vbp),
        .hs_pol(g_hspol), .vs_pol(g_vspol),
        .pix_en(pen0), .hs(hs0), .vs(vs0), .blank_n(blank0),
        .x(x0), .y(y0), .frame(frame0), .line(line0));
    wire act0 = blank0;
    assign frame_ap  = frame0;
    assign mode_cur  = mode_ap;
    assign frame_stb = frame0;
    assign pix_en = pen0;

    wire [11:0] out_w = g_hact;
    wire [11:0] out_h = g_vact;

    // ---- config, applied ONLY at the frame strobe -------------------------
    reg        stog1, stog2, stog_ap;
    reg [AW-1:0] cur_base;
    assign fb_cur_base = cur_base;
    reg [10:0] src_w, src_h;
    wire       swap_pend = (stog2 != stog_ap);
    always @(posedge fclk or negedge frst_n) begin
        if (!frst_n) begin
            stog1<=0; stog2<=0; stog_ap<=0;
            cur_base<={AW{1'b0}}; src_w<=11'd800; src_h<=11'd600;
        end else begin
            stog1 <= swap_tog; stog2 <= stog1;
            if (frame0 && swap_pend) begin
                stog_ap  <= stog2;
                cur_base <= swap_base;         // stable while tog pending
                src_w    <= swap_w;
                src_h    <= swap_h;
            end
        end
    end

    // ---- line sequencing + vertical DDA -----------------------------------
    // dbuf is displayed, ~dbuf is the spare; stale counts buffers needing a
    // fetch (2 right after a frame strobe).  Fetches always target alternate
    // buffers starting at 0 each frame, in source-line order 0,1,2,...
    reg        dbuf, tbuf, fpend;
    reg [1:0]  stale;
    reg [10:0] nl;                    // next source line to fetch
    reg [11:0] vacc;
    reg [AW-1:0] facc;                  // word address of line nl (incremental)
    reg        atog1, atog2, aseen;
    reg [2:0]  fdly;

    always @(posedge fclk or negedge frst_n) begin
        if (!frst_n) begin
            dbuf<=0; tbuf<=0; fpend<=0; stale<=2'd0; nl<=11'd0;
            vacc<=12'd0; facc<={AW{1'b0}}; ftog<=0; faddr<={AW{1'b0}}; fwords<=12'd0;
            fbuf<=0; fpal<=0; f565<=0; atog1<=0; atog2<=0; aseen<=0; fdly<=3'd0;
        end else begin
            atog1 <= fack_tog; atog2 <= atog1;
            if (atog2 != aseen) begin aseen <= atog2; fpend <= 1'b0; end

            if (frame0) begin
                dbuf  <= 1'b0; tbuf <= 1'b0;
                stale <= 2'd2; nl <= 11'd0; vacc <= 12'd0;
                facc  <= swap_pend ? swap_base : cur_base;
                fdly  <= 3'd7;
            end else if (fdly != 3'd0) begin
                fdly <= fdly - 3'd1;
            end else begin
                if (stale != 2'd0 && !fpend && nl < src_h) begin
                    faddr  <= facc;
                    /* 2 words per pixel, or -- paletted -- one word per TWO pixels, which is a QUARTER of
                     * the fetches and a quarter of the LPDDR traffic for the same picture.  ceil, not
                     * floor: an odd width still needs the word holding its last pixel. */
                    fwords <= pal_ap ? {2'd0, src_w[10:1]} + {11'd0, src_w[0]} : f565_ap ? {1'b0, src_w} : {src_w, 1'b0};
                    fpal   <= pal_ap;
                    f565   <= f565_ap & ~pal_ap;
                    fbuf   <= tbuf;
                    ftog   <= ~ftog;
                    fpend  <= 1'b1;
                    tbuf   <= ~tbuf;
                    stale  <= stale - 2'd1;
                    nl     <= nl + 11'd1;
                    facc   <= facc + (pal_ap ? {12'd0, src_w[10:1]} + {21'd0, src_w[0]}
                                             : f565_ap ? {11'd0, src_w} : {src_w, 1'b0});
                end
                if (line0) begin                   // end of each active line
                    if (vacc + {1'b0,src_h} >= out_h) begin
                        vacc  <= vacc + {1'b0,src_h} - out_h;
                        dbuf  <= ~dbuf;
                        stale <= stale + 2'd1;
                    end else
                        vacc <= vacc + {1'b0,src_h};
                end
            end
        end
    end

    // ---- OSD: 8x14 EGA-font text overlay, top-left, host-written ----------
    // A 64x4 character cell grid (top-left), composited over the scaled image
    // in a +1 fclk stage.  osd_en=0 makes the whole overlay a bit-exact
    // pass-through (the gate proves it).  Text is host-written into tbuf via
    // a toggle-synced port; the font ROM inits from egafont14 (mif on Altera parts,
    // hex in sim).  White text on a dimmed box so it reads over any picture.
    localparam OSD_COLS = 7'd64, OSD_ROWS = 3'd4;
    reg  [7:0] osdbuf [0:255];               // 64*4 char cells
    (* ram_init_file = "egafont14.mif" *)
    reg  [7:0] fontrom [0:3583];             // 256 glyphs x 14 rows, MSB=left
    integer gi;
    initial begin for (gi=0;gi<256;gi=gi+1) begin
        osdbuf[gi]=8'h20; end                // spaces
    end
    /* THE FONT ROM'S INITIALISATION.  For an Altera part the ram_init_file
     * attribute above carries the ROM and Quartus honours it.  XST HONOURS NEITHER: the attribute is Altera's,
     * and __ICARUS__ is not defined during synthesis -- so ported unchanged, this
     * block RAM would have synthesised BLANK and the OSD would have rendered
     * nothing on the board, with no error at any stage to say so.
     *
     * `initial $readmemh' IS the Xilinx idiom for initialising an inferred block
     * RAM, so it is unconditional here and it is now the only mechanism that
     * matters on this part.  The attribute is left in place: XST ignores it, and
     * leaving it keeps the file usable on Altera parts.
     *
     * The path is a define because the three flows have three working directories
     * (rtl/sim for iverilog, ise/ for XST, quartus/ upstream).  tools/egafont.c
     * regenerates the file from tools/ibm-ega.rom and reproduces the shipped
     * artifact byte for byte, so the ROM is checkable rather than merely carried. */
`ifndef VGA_FONT_HEX
 `define VGA_FONT_HEX "../vga/egafont14.hex"
`endif
    initial $readmemh(`VGA_FONT_HEX, fontrom);
    // host write CDC (toggle -> fclk edge)
    reg ow1, ow2, ow3;
    always @(posedge fclk or negedge frst_n) begin
        if (!frst_n) begin ow1<=0; ow2<=0; ow3<=0; end
        else begin ow1<=osd_wtog; ow2<=ow1; ow3<=ow2;
            if (ow3 ^ ow2)       osdbuf[osd_wpos]    <= osd_wchar;
            else if (hw_osd_we)  osdbuf[hw_osd_pos]  <= hw_osd_char; end
    end

    reg [11:0] hacc;
    reg [10:0] sx;
    reg        act1, act2, hs1, hs2, vs1, vs2, bl1, bl2, pe1, pe2;
    reg [11:0] x1, x2, y1, y2;
    // OSD pipeline stages t3/t4/t5 -- each does ONE synchronous memory read
    // so neither the 256-cell text buffer nor the 3584-entry font ROM ever
    // becomes a combinational mux (that failed 100 MHz by -2.4 ns).
    reg [7:0]  osd_char;                 // t3: osdbuf sync-read result
    reg [7:0]  osd_glyph;                // t4: fontrom sync-read result
    reg [3:0]  sub3;
    reg        inbox3, inbox4; reg [2:0] xbit3, xbit4;
    reg [29:0] rgb3, rgb4;
    /* the byte this output pixel is: see the alignment note beside pal_q's declaration above */
    wire [7:0] pal_ra = psel3 ? rgb3[15:8] : rgb3[7:0];

    /* THE TABLE IS INSTANTIATED HERE, below rgb3 and pal_ra, for the same reason pal_ra moved: XST wants
     * every identifier declared before it is used, a port connection included, and it says so as
     * HDLCompilers:28 -- which both simulators accept in silence and XST does not. */
    vga_palette u_pal (
        .wclk(pclk), .we(pal_we), .waddr(pal_wa), .wdata(pal_wd),
        .rclk(fclk), .raddr(pal_ra), .rdata(pal_q));
    reg        bl3, hs3, vs3, pe3, bl4, hs4, vs4, pe4;

    // t2 combinational cell address (cheap: compares + slice)
    wire [1:0]  o_row = (y2 < 12'd14) ? 2'd0 : (y2 < 12'd28) ? 2'd1 :
                        (y2 < 12'd42) ? 2'd2 : 2'd3;
    wire [3:0]  o_sub = y2[3:0] - {2'd0, o_row}*4'd14;   // y2 - row*14 (y2<56)
    wire [7:0]  o_cell = {o_row, x2[8:3]};               // row*64 + col
    wire        o_inbox = (x2 < 12'd512) && (y2 < 12'd56);
    // t3 combinational font address from the REGISTERED char (small adder)
    wire [11:0] o_faddr = {osd_char,3'd0} + {osd_char,2'd0} + {osd_char,1'd0}
                          + {8'd0, sub3};                // char*14 + sub

    always @(posedge fclk or negedge frst_n) begin
        if (!frst_n) begin
            hacc<=12'd0; sx<=11'd0; lb_ra<=11'd0;
            act1<=0; act2<=0; hs1<=0; hs2<=0; vs1<=0; vs2<=1; bl1<=0; bl2<=0;
            pe1<=0; pe2<=0;
            x1<=0; x2<=0; y1<=0; y2<=0;
            osd_char<=8'h20; osd_glyph<=0; sub3<=0;
            psel1<=0; psel2<=0; psel3<=0;
            inbox3<=0; inbox4<=0; xbit3<=0; xbit4<=0; rgb3<=0; rgb4<=0;
            bl3<=0; hs3<=0; vs3<=1; pe3<=0; bl4<=0; hs4<=0; vs4<=1; pe4<=0;
            vr<=10'd0; vg<=10'd0; vb<=10'd0; v_hs<=0; v_vs<=1; v_blank_n<=0;
            pix_stb<=1'b0;
        end else begin
            if (pen0) begin
                if (act0) begin                    // pixel boundary in active
                    /* paletted: two pixels live in one entry, so the entry is sx>>1 and sx[0] picks the
                     * byte.  psel1 rides the pipeline beside the data it selects from -- one stage, the
                     * same stage lb_q takes to come back. */
                    /* PALETTED USES THE WHOLE LINE BUFFER.  This was
                     * {dbuf, 1'b0, sx[LB_AW-2:1]}: a forced zero above the entry index, so a paletted line was
                     * capped at 2^(LB_AW-2) entries = 512 pixels in the Pano's LB_AW = 10 build although the
                     * buffer holds 2^(LB_AW-1) entries a line and the write side fills them.  For any source of
                     * 512 pixels or fewer sx[LB_AW-1] is 0 and the address is bit for bit what it was. */
                    lb_ra <= pal_ap ? {dbuf, sx[LB_AW-1:1]} : {dbuf, sx[LB_AW-2:0]};
                    psel1 <= sx[0];
                    if (hacc + {1'b0,src_w} >= out_w) begin
                        hacc <= hacc + {1'b0,src_w} - out_w;
                        sx   <= sx + 11'd1;
                    end else
                        hacc <= hacc + {1'b0,src_w};
                end else begin
                    sx <= 11'd0; hacc <= 12'd0;
                end
            end
            pe1<=pen0; pe2<=pe1;
            act1<=act0; hs1<=hs0; vs1<=vs0; bl1<=blank0; x1<=x0; y1<=y0;
            act2<=act1; hs2<=hs1; vs2<=vs1; bl2<=bl1; x2<=x1; y2<=y1;
            // t2->t3: osdbuf sync read + carry
            osd_char <= osdbuf[o_cell];
            sub3<=o_sub; inbox3<=o_inbox; xbit3<=x2[2:0];
            rgb3<=lb_q; bl3<=bl2; hs3<=hs2; vs3<=vs2; pe3<=pe2;
            psel2<=psel1; psel3<=psel2;
            // t3->t4: fontrom sync read + carry
            osd_glyph <= fontrom[o_faddr];
            inbox4<=inbox3; xbit4<=xbit3;
            rgb4<=rgb3; bl4<=bl3; hs4<=hs3; vs4<=vs3; pe4<=pe3;
            // t4->t5: composite + output
            begin : compose
                reg obit; reg [29:0] pix;
                obit = osd_en && inbox4 && osd_glyph[3'd7 - xbit4];
                pix  = obit ? 30'h3FFFFFFF : (pal_ap ? pal_q : rgb4);   // white text
                vr <= bl4 ? pix[29:20] : 10'd0;
                vg <= bl4 ? pix[19:10] : 10'd0;
                vb <= bl4 ? pix[9:0]   : 10'd0;
            end
            v_hs <= hs4; v_vs <= vs4; v_blank_n <= bl4;
            pix_stb <= pe4;
        end
    end

    assign osd_x = x2; assign osd_y = y2; assign osd_active = act2;
    assign rgb_t2 = lb_q;

    // =======================================================================
    // fclk domain: the line fetcher
    // =======================================================================
    // Issue: one sequential word per cycle when the word's bank has queue
    // room (rdy masked against our own last-cycle req -- the documented
    // rdy-staleness race).  Per-bank 4-deep index FIFOs recover each
    // return's word index (the controller retires per-bank in order), an
    // 8-pixel assembly ring pairs the two 16-bit halves of each 10:10:10
    // pixel, completed pixels stream into the line buffer.  Outstanding
    // word-index span is bounded by the bank FIFOs at 12, so the 8-pixel
    // ring can never collide.
    reg        ft1, ft2, ft3;
    reg [AW-1:0] la;
    reg [11:0] ln;
    reg        lbuf, lpal, l565;          // lpal, l565: this line's format, taken with its command
    reg [11:0] wi, rc;
    reg        running;
    reg [3:0]  req_r;

    reg [11:0] bfifo0 [0:3];  reg [11:0] bfifo1 [0:3];
    reg [11:0] bfifo2 [0:3];  reg [11:0] bfifo3 [0:3];
    reg [1:0]  bwp [0:3], brp [0:3];
    reg [2:0]  bcnt [0:3];
    reg [3:0]  bfull;          // bfull[k] == (bcnt[k] == 4), kept as a flop: see "THE ISSUE LOOP, UNROLLED" below

    reg [15:0] alo [0:7];     // pixel-half assembly ring
    reg [13:0] ahi [0:7];
    reg [7:0]  vlo, vhi;
    reg [9:0]  pix_of [0:7];  // full pixel index per ring slot

    /* THE ISSUE ADDRESS IS A RUNNING REGISTER: as `la + wi', a 22-bit add sat in front of
     * the bank select, the per-bank count compare and the ready test every clock -- TRCE measured wi -> bcnt at
     * 11.9 ns over 9 levels against the 10 ns clock (429 endpoints).  cw_addr_r is loaded with the line address
     * at the line command and steps by one per issue, which is exactly la + wi at every clock the old wire was
     * read.  init_done is the memory controller's flag on the OTHER edge of this clock: one flop here, as
     * everywhere else in the SoC. */
    reg  [AW-1:0] cw_addr_r;
    reg         init_q;
    always @(posedge fclk) init_q <= init_done;
    wire [AW-1:0] cw_addr = cw_addr_r;
    wire [23:0] cw_x     = cw_addr_r;   // zero-extended: bit 22 is the AW 23 address bit
    wire [1:0]  cw_bank  = cw_addr_r[1:0];
    /* THE ISSUE LOOP, UNROLLED (bcnt -> bcnt at -0.696 ns over 8 levels,
     * 31 of the 39 clk0 errors).  The loop was bcnt[cw_bank] (a 3-bit 4:1 mux) -> != 4 -> AND with the rdy[] and
     * req_r[] muxes -> do_issue -> an array-indexed +1 / -1 / reconcile write -> bcnt.  Now:
     *   - bfull[k] is a FLOP equal to (bcnt[k] == 4) at every clock; its next value is chosen by that bank's own
     *     inc/dec from b3[k] = (bcnt[k] == 3), which comes straight off the registers, so no compare follows do_issue;
     *   - can[k] = !bfull[k] && rdy[k] && !req_r[k] for all four banks at once, so the loop sees ONE 1-bit 4:1 mux;
     *   - each bank's counter is its own up/down counter on inc[k] / dec[k] (one-hot per bank), no indexed write.
     * Cycle for cycle what it was: the simulation checks bfull against the count on every clock (below). */
    wire [3:0]  can      = ~bfull & rdy & ~req_r;
    wire        do_issue = running && init_q && (wi < ln) && can[cw_bank];
    wire        do_ret   = running && rvalid;
    wire [3:0]  b_inc    = do_issue ? (4'b0001 << cw_bank) : 4'b0000;
    wire [3:0]  b_dec    = do_ret   ? (4'b0001 << rbank)   : 4'b0000;
    wire [3:0]  b3       = {bcnt[3] == 3'd3, bcnt[2] == 3'd3, bcnt[1] == 3'd3, bcnt[0] == 3'd3};
`ifndef SYNTHESIS
    // the unrolled loop's one invariant: every clock, the flag IS the compare it replaced
    always @(posedge fclk)
        if (bfull !== {bcnt[3] == 3'd4, bcnt[2] == 3'd4, bcnt[1] == 3'd4, bcnt[0] == 3'd4})
            $display("VGA_SCAN BFULL MISMATCH at %0t: bfull=%b bcnt=%0d/%0d/%0d/%0d -- FAIL", $time, bfull, bcnt[3], bcnt[2], bcnt[1], bcnt[0]);
`endif

    // the returning word's index: head of that bank's FIFO
    wire [11:0] ret_q0 = bfifo0[brp[0]];
    wire [11:0] ret_q1 = bfifo1[brp[1]];
    wire [11:0] ret_q2 = bfifo2[brp[2]];
    wire [11:0] ret_q3 = bfifo3[brp[3]];
    wire [11:0] ret_wi = (rbank==2'd0) ? ret_q0 : (rbank==2'd1) ? ret_q1 :
                         (rbank==2'd2) ? ret_q2 : ret_q3;
    wire [2:0]  ret_pix = ret_wi[3:1];

    integer k;
    wire [7:0] px_done = vlo & vhi;   // slots with both halves home
    reg  [2:0] px_sel;                // the lowest of them
    reg        px_any;
    always @* begin
        px_any = |px_done;
        casez (px_done)
        8'b???????1: px_sel = 3'd0;
        8'b??????10: px_sel = 3'd1;
        8'b?????100: px_sel = 3'd2;
        8'b????1000: px_sel = 3'd3;
        8'b???10000: px_sel = 3'd4;
        8'b??100000: px_sel = 3'd5;
        8'b?1000000: px_sel = 3'd6;
        8'b10000000: px_sel = 3'd7;
        default:     px_sel = 3'd0;
        endcase
    end

    always @(posedge fclk or negedge frst_n) begin
        if (!frst_n) begin
            ft1<=0; ft2<=0; ft3<=0; fack_tog<=0;
            la<={AW{1'b0}}; ln<=12'd0; lbuf<=0; lpal<=0; l565<=0; wi<=12'd0; rc<=12'd0; running<=0; cw_addr_r<={AW{1'b0}};
            req<=4'd0; req_r<=4'd0; req_row<=48'd0; req_col<=32'd0; req_hi<=4'd0;
            vlo<=8'd0; vhi<=8'd0; lb_we<=0; lb_wa<=11'd0; lb_wd<=30'd0;
            for (k=0;k<4;k=k+1) begin bwp[k]<=2'd0; brp[k]<=2'd0; bcnt[k]<=3'd0; end
            bfull <= 4'd0;
        end else begin
            ft1<=ftog; ft2<=ft1; ft3<=ft2;
            req   <= 4'd0;
            req_r <= req;
            lb_we <= 1'b0;

            if (!running && ft3 != ft2) begin      // new line command
                la <= faddr; ln <= fwords; lbuf <= fbuf; cw_addr_r <= faddr; lpal <= fpal; l565 <= f565;
                wi <= 12'd0; rc <= 12'd0; running <= 1'b1;
                vlo <= 8'd0; vhi <= 8'd0;
                for (k=0;k<4;k=k+1) begin bwp[k]<=2'd0; brp[k]<=2'd0; bcnt[k]<=3'd0; end
                bfull <= 4'd0;
            end

            if (do_issue) begin
                req[cw_bank]              <= 1'b1;
                /* one bank's row/col slot; a case, not [cw_bank*12 +: 12], because XST 14.7 refuses a
                 * variable part-select on the left-hand side */
                case (cw_bank)
                2'd0: begin req_row[11:0]  <= cw_addr[21:10]; req_col[7:0]   <= cw_addr[9:2]; req_hi[0] <= cw_x[22]; end
                2'd1: begin req_row[23:12] <= cw_addr[21:10]; req_col[15:8]  <= cw_addr[9:2]; req_hi[1] <= cw_x[22]; end
                2'd2: begin req_row[35:24] <= cw_addr[21:10]; req_col[23:16] <= cw_addr[9:2]; req_hi[2] <= cw_x[22]; end
                2'd3: begin req_row[47:36] <= cw_addr[21:10]; req_col[31:24] <= cw_addr[9:2]; req_hi[3] <= cw_x[22]; end
                endcase
                case (cw_bank)
                2'd0: bfifo0[bwp[0]] <= wi;  2'd1: bfifo1[bwp[1]] <= wi;
                2'd2: bfifo2[bwp[2]] <= wi;  2'd3: bfifo3[bwp[3]] <= wi;
                endcase
                bwp[cw_bank] <= bwp[cw_bank] + 2'd1;
                cw_addr_r    <= cw_addr_r + 1'b1;
                wi           <= wi + 12'd1;
            end
            if (do_ret) begin
                brp[rbank] <= brp[rbank] + 2'd1;
                rc         <= rc + 12'd1;
`ifdef VGA_SCAN_FAULT_ASYNC
                if (pal_en) begin
`else
                if (lpal) begin
`endif
                    /* STRAIGHT IN.  A paletted word is TWO PIXELS and needs no assembly, so it is written
                     * to the line buffer as it arrives -- ret_wi is already the entry index.  The ring
                     * below (alo/ahi/vlo/vhi/pix_of) is simply not used: vlo and vhi stay zero, so px_any
                     * never fires and cannot fight this write, and the end-of-line test that waits for
                     * them to drain is satisfied by construction.
                     *
                     * The 14 high bits are unused here.  Widening the line buffer down to 16 bits for this
                     * mode would save two RAMB16 and cost a second shape to verify; the head already fits,
                     * so it keeps one shape. */
                    lb_we <= 1'b1;
                    lb_wd <= {14'd0, rdata};
                    lb_wa <= {lbuf, ret_wi[LB_AW-2:0]};
`ifdef VGA_SCAN_FAULT_ASYNC
                end else if (fmt565) begin
`else
                end else if (l565) begin
`endif
                    /* RGB565: one word is one pixel, widened by replication; straight in as the paletted word */
                    lb_we <= 1'b1;
                    lb_wd <= {rdata[15:11], rdata[15:11],                    // R 5 -> 10
                              rdata[10:5], rdata[10:7],                      // G 6 -> 10
                              rdata[4:0], rdata[4:0]};                       // B 5 -> 10
                    lb_wa <= {lbuf, ret_wi[LB_AW-2:0]};
                end else begin
                    pix_of[ret_pix] <= ret_wi[10:1];
                    if (ret_wi[0]) begin
                        ahi[ret_pix] <= rdata[13:0]; vhi[ret_pix] <= 1'b1;
                    end else begin
                        alo[ret_pix] <= rdata;       vlo[ret_pix] <= 1'b1;
                    end
                end
            end
            // bcnt / bfull: each bank its own up/down counter (an issue and a return on one bank cancel)
            for (k=0;k<4;k=k+1)
                case ({b_inc[k], b_dec[k]})
                2'b10:   begin bcnt[k] <= bcnt[k] + 3'd1; bfull[k] <= b3[k]; end
                2'b01:   begin bcnt[k] <= bcnt[k] - 3'd1; bfull[k] <= 1'b0;  end
                default: ;
                endcase

            // stream one completed pixel per cycle into the line buffer
            // (returns arrive one per cycle, so one completion per cycle).
            // The lowest completed slot wins -- px_sel below is the priority
            // encoder the original for-loop-with-a-wrote-flag described; XST 14.7
            // refused that loop.  Same
            // ordering: this sits after do_ret, so a clear here still beats a
            // set there on the same slot, as the loop's did.
            if (px_any) begin
                lb_we <= 1'b1;
                lb_wd <= {ahi[px_sel][13:4],                       // R
                          ahi[px_sel][3:0], alo[px_sel][15:10],    // G
                          alo[px_sel][9:0]};                       // B
                lb_wa <= {lbuf, pix_of[px_sel][LB_AW-2:0]};
                vlo[px_sel] <= 1'b0; vhi[px_sel] <= 1'b0;
            end

`ifdef VGA_SCAN_FAULT_ASYNC
            if (running && rc == ln && vlo == 8'd0 && vhi == 8'd0) begin
`else
            /* THE LINE ENDS when every word is back and no COMPLETE pixel waits in the ring.  A lone half (one of vlo /
             * vhi without the other) can no longer arise -- the format is latched per line -- but if one did, it is
             * dropped here instead of holding `running' for ever. */
            if (running && rc == ln && px_done == 8'd0) begin
                vlo <= 8'd0; vhi <= 8'd0;
`endif
                running  <= 1'b0;
                fack_tog <= ~fack_tog;
            end
        end
    end
endmodule
