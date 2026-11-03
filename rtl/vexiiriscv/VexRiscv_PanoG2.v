// VexRiscv_PanoG2.v -- the Pano Logic G2 CPU: VexiiRiscv (rtl/vexiiriscv/VexiiRiscv_PanoG2_core.v, module
// VexiiG2_VexiiRiscv) behind the port list of the G1 core (module VexRiscv in
// rtl/vexriscv/VexRiscv_PanoG1_16k.v), so a fabric written for that core connects without change.
// Exactly one file declaring module VexRiscv belongs in a project.
//
// CORE (tools/vexiiriscv-gen/regen.sh): RV32IMAFDC, Zicsr, Zifencei, Zicbom, Zicntr; machine, supervisor and
//   user modes, Sv32; misa 0x4014_112D.  Instruction L1: 16 KB, 4 ways, 64-byte lines.  Data L1: 16 KB,
//   4 ways, 64-byte lines, WRITE-BACK, write-allocate.  Address bit 31 set = IO region: never cached, never
//   executed (instruction access fault), one access at a time.  Bit 31 clear = cacheable and executable.
//
// DATA WIDTH.  With D the core's two LSU ports are 64 bits wide (ADR = 8-byte word address, SEL[7:0]); the
//   fetch port is 32 bits.  Each LSU port passes through g2_wb64to32 below, which turns one 64-bit beat into
//   one or two 32-bit beats on the fabric, the lower word (bits 31:0, the lower address) first.  The fabric
//   ports of this module are therefore the 32-bit ports of the earlier cores, unchanged.
//
// iBus: the instruction L1 refill port.  Read only.  Every cycle is one line burst: 16 beats of 32 bits,
//   ADR from the line base upward, CTI = 3'b010 on beats 0..14 and 3'b111 on beat 15, BTE = 2'b00 (linear),
//   SEL = 4'hF.  CYC and STB are high together for the whole burst.
//
// dBus: two sources merged here, one master on the fabric.
//   L1 source   line refills and line write-backs: 8 core beats of 64 bits, 16 fabric beats of 32 bits,
//               CTI = 3'b010 on fabric beats 0..14 and 3'b111 on beat 15, BTE = 2'b00, SEL = 4'hF.  A
//               write-back is caused by eviction of a dirty line or by cbo.clean/cbo.flush.  The response of each
//               64-bit beat reaches the core one clock after the fabric's ACK (g2_wb64to32 below).
//   IO source   loads and stores with address bit 31 set: single beats, CTI = 3'b000, SEL = the byte lanes,
//               issued one at a time in program order (the core holds its pipeline until the ACK).  An
//               access of 32 bits or less is one fabric beat on the word its byte lanes select; only a 64-bit
//               access (fld/fsd to the IO region) is two beats, lower word first.  A word whose byte lanes
//               are all clear is never read, so a read-sensitive register beside the target is not touched.
//   Arbitration: the L1 source wins when both request in the same cycle.  Once a line burst has taken
//   the bus it keeps it until the beat with CTI = 3'b111 is acknowledged; if the core pauses between beats,
//   CYC stays high with STB low (a master wait state), so a fabric that holds its grant while CYC is high
//   sees one unbroken cycle per line.  An IO beat keeps the bus until its ACK.
//
// COHERENCE WITH DMA.  The data L1 is write-back, so memory is stale until a dirty line leaves the cache:
//   before a device reads a buffer:   cbo.clean on every line of it, then fence (the fence waits until the
//                                     write-back bursts have been acknowledged), then start the device;
//   before the CPU reads a buffer a device wrote:  cbo.inval (or cbo.flush) on every line of it.
//   The uncached alias (address | 0x8000_0000) reaches the same memory without the cache.
//
// INTERRUPTS.  timerInterrupt -> mip.MTIP, softwareInterrupt -> mip.MSIP, both levels.  externalInterruptArray
//   is registered once; CSR 0xBC0 is the mask (read/write), CSR 0xFC0 reads mask AND lines; mip.MEIP is the
//   OR of 0xFC0 -- the G1 core's arrangement.
//
// externalResetVector is loaded into the PC while reset is high (synchronous, active-high reset).
// The core's time CSR reads a 64-bit count of clk cycles since reset, kept here; mcycle counts the same clock.
`timescale 1ns / 1ps
`default_nettype none

module VexRiscv (
    input  wire [31:0] externalResetVector,
    input  wire        timerInterrupt,
    input  wire        softwareInterrupt,
    input  wire [31:0] externalInterruptArray,
    output wire        iBusWishbone_CYC,
    output wire        iBusWishbone_STB,
    input  wire        iBusWishbone_ACK,
    output wire        iBusWishbone_WE,
    output wire [29:0] iBusWishbone_ADR,
    input  wire [31:0] iBusWishbone_DAT_MISO,
    output wire [31:0] iBusWishbone_DAT_MOSI,
    output wire [3:0]  iBusWishbone_SEL,
    input  wire        iBusWishbone_ERR,
    output wire [2:0]  iBusWishbone_CTI,
    output wire [1:0]  iBusWishbone_BTE,
    output wire        dBusWishbone_CYC,
    output wire        dBusWishbone_STB,
    input  wire        dBusWishbone_ACK,
    output wire        dBusWishbone_WE,
    output wire [29:0] dBusWishbone_ADR,
    input  wire [31:0] dBusWishbone_DAT_MISO,
    output wire [31:0] dBusWishbone_DAT_MOSI,
    output wire [3:0]  dBusWishbone_SEL,
    input  wire        dBusWishbone_ERR,
    output wire [2:0]  dBusWishbone_CTI,
    output wire [1:0]  dBusWishbone_BTE,
    input  wire        clk,
    input  wire        reset
);

    // ---- core buses -------------------------------------------------------------------------------------
    wire        l1_cyc, l1_stb, l1_we;
    wire [29:0] l1_adr;
    wire [31:0] l1_mosi;
    wire [3:0]  l1_sel;
    wire [2:0]  l1_cti;
    wire [1:0]  l1_bte;
    wire        io_cyc, io_stb, io_we;
    wire [29:0] io_adr;
    wire [31:0] io_mosi;
    wire [3:0]  io_sel;
    wire [2:0]  io_cti;
    wire [1:0]  io_bte;
    wire        l1_ack, l1_err, io_ack, io_err;

`ifdef G2CPU_LSU32
    // a core generated without F and D (rtl/vexiiriscv/VexiiRiscv_PanoG2_core_nofd.v): its LSU ports are 32 bits and
    // connect to the merge directly, no g2_wb64to32
    localparam LW = 32, LA = 30, LS = 4;
`else
    localparam LW = 64, LA = 29, LS = 8;
`endif
    // the core's LSU ports
    wire        c_l1_cyc, c_l1_stb, c_l1_we, c_l1_ack, c_l1_err;
    wire [LA-1:0] c_l1_adr;
    wire [LW-1:0] c_l1_mosi, c_l1_miso;
    wire [LS-1:0] c_l1_sel;
    wire [2:0]  c_l1_cti;
    wire [1:0]  c_l1_bte;
    wire        c_io_cyc, c_io_stb, c_io_we, c_io_ack, c_io_err;
    wire [LA-1:0] c_io_adr;
    wire [LW-1:0] c_io_mosi, c_io_miso;
    wire [LS-1:0] c_io_sel;
    wire [2:0]  c_io_cti;
    wire [1:0]  c_io_bte;
    wire [31:0] i_mosi_core;
    wire        i_we_core;

    reg  [63:0] rdtime;
    always @(posedge clk)
        if (reset) rdtime <= 64'd0;
        else       rdtime <= rdtime + 64'd1;

    VexiiG2_VexiiRiscv core (
        .PrivilegedPlugin_logic_rdtime                    (rdtime),
        .PrivilegedPlugin_logic_harts_0_int_m_timer       (timerInterrupt),
        .PrivilegedPlugin_logic_harts_0_int_m_software    (softwareInterrupt),
        .PanoG2Plugin_logic_externalResetVector           (externalResetVector),
        .PanoG2Plugin_logic_externalInterruptArray        (externalInterruptArray),
        .LsuL1WishbonePlugin_logic_bus_CYC                (c_l1_cyc),
        .LsuL1WishbonePlugin_logic_bus_STB                (c_l1_stb),
        .LsuL1WishbonePlugin_logic_bus_ACK                (c_l1_ack),
        .LsuL1WishbonePlugin_logic_bus_WE                 (c_l1_we),
        .LsuL1WishbonePlugin_logic_bus_ADR                (c_l1_adr),
        .LsuL1WishbonePlugin_logic_bus_DAT_MISO           (c_l1_miso),
        .LsuL1WishbonePlugin_logic_bus_DAT_MOSI           (c_l1_mosi),
        .LsuL1WishbonePlugin_logic_bus_SEL                (c_l1_sel),
        .LsuL1WishbonePlugin_logic_bus_ERR                (c_l1_err),
        .LsuL1WishbonePlugin_logic_bus_CTI                (c_l1_cti),
        .LsuL1WishbonePlugin_logic_bus_BTE                (c_l1_bte),
        .FetchL1WishbonePlugin_logic_bus_CYC              (iBusWishbone_CYC),
        .FetchL1WishbonePlugin_logic_bus_STB              (iBusWishbone_STB),
        .FetchL1WishbonePlugin_logic_bus_ACK              (iBusWishbone_ACK),
        .FetchL1WishbonePlugin_logic_bus_WE               (i_we_core),
        .FetchL1WishbonePlugin_logic_bus_ADR              (iBusWishbone_ADR),
        .FetchL1WishbonePlugin_logic_bus_DAT_MISO         (iBusWishbone_DAT_MISO),
        .FetchL1WishbonePlugin_logic_bus_DAT_MOSI         (i_mosi_core),
        .FetchL1WishbonePlugin_logic_bus_SEL              (iBusWishbone_SEL),
        .FetchL1WishbonePlugin_logic_bus_ERR              (iBusWishbone_ERR),
        .FetchL1WishbonePlugin_logic_bus_CTI              (iBusWishbone_CTI),
        .FetchL1WishbonePlugin_logic_bus_BTE              (iBusWishbone_BTE),
        .LsuCachelessWishbonePlugin_logic_bridge_down_CYC (c_io_cyc),
        .LsuCachelessWishbonePlugin_logic_bridge_down_STB (c_io_stb),
        .LsuCachelessWishbonePlugin_logic_bridge_down_ACK (c_io_ack),
        .LsuCachelessWishbonePlugin_logic_bridge_down_WE  (c_io_we),
        .LsuCachelessWishbonePlugin_logic_bridge_down_ADR (c_io_adr),
        .LsuCachelessWishbonePlugin_logic_bridge_down_DAT_MISO (c_io_miso),
        .LsuCachelessWishbonePlugin_logic_bridge_down_DAT_MOSI (c_io_mosi),
        .LsuCachelessWishbonePlugin_logic_bridge_down_SEL (c_io_sel),
        .LsuCachelessWishbonePlugin_logic_bridge_down_ERR (c_io_err),
        .LsuCachelessWishbonePlugin_logic_bridge_down_CTI (c_io_cti),
        .LsuCachelessWishbonePlugin_logic_bridge_down_BTE (c_io_bte),
        .clk   (clk),
        .reset (reset)
    );

`ifdef G2CPU_LSU32
    assign {l1_cyc, l1_stb, l1_we, l1_adr, l1_mosi, l1_sel, l1_cti, l1_bte} =
           {c_l1_cyc, c_l1_stb, c_l1_we, c_l1_adr, c_l1_mosi, c_l1_sel, c_l1_cti, c_l1_bte};
    assign {io_cyc, io_stb, io_we, io_adr, io_mosi, io_sel, io_cti, io_bte} =
           {c_io_cyc, c_io_stb, c_io_we, c_io_adr, c_io_mosi, c_io_sel, c_io_cti, c_io_bte};
    assign c_l1_ack = l1_ack;  assign c_l1_err = l1_err;  assign c_l1_miso = dBusWishbone_DAT_MISO;
    assign c_io_ack = io_ack;  assign c_io_err = io_err;  assign c_io_miso = dBusWishbone_DAT_MISO;
`else
    // ---- 64-bit LSU ports to 32-bit beats ---------------------------------------------------------------
    // L1: every beat is a whole-line transfer, both words always (SEL = 4'hF on the fabric for reads).
    // IO: the words whose byte lanes are set, lower first.
    g2_wb64to32 #(.BOTH(1)) u_l1_split (
        .clk(clk), .reset(reset),
        .u_cyc(c_l1_cyc), .u_stb(c_l1_stb), .u_we(c_l1_we), .u_adr(c_l1_adr), .u_mosi(c_l1_mosi), .u_sel(c_l1_sel),
        .u_cti(c_l1_cti), .u_bte(c_l1_bte), .u_ack(c_l1_ack), .u_err(c_l1_err), .u_miso(c_l1_miso),
        .d_cyc(l1_cyc), .d_stb(l1_stb), .d_we(l1_we), .d_adr(l1_adr), .d_mosi(l1_mosi), .d_sel(l1_sel),
        .d_cti(l1_cti), .d_bte(l1_bte), .d_ack(l1_ack), .d_err(l1_err), .d_miso(dBusWishbone_DAT_MISO));
    g2_wb64to32 #(.BOTH(0)) u_io_split (
        .clk(clk), .reset(reset),
        .u_cyc(c_io_cyc), .u_stb(c_io_stb), .u_we(c_io_we), .u_adr(c_io_adr), .u_mosi(c_io_mosi), .u_sel(c_io_sel),
        .u_cti(c_io_cti), .u_bte(c_io_bte), .u_ack(c_io_ack), .u_err(c_io_err), .u_miso(c_io_miso),
        .d_cyc(io_cyc), .d_stb(io_stb), .d_we(io_we), .d_adr(io_adr), .d_mosi(io_mosi), .d_sel(io_sel),
        .d_cti(io_cti), .d_bte(io_bte), .d_ack(io_ack), .d_err(io_err), .d_miso(dBusWishbone_DAT_MISO));

`endif

    // The instruction bus never writes; its write data output is undefined in the core.
    assign iBusWishbone_WE      = 1'b0;
    assign iBusWishbone_DAT_MOSI = 32'd0;

    // ---- dBus merge -------------------------------------------------------------------------------------
    // own_l1: a line burst has presented a beat and its CTI = 3'b111 beat has not been acknowledged.  It is set by a
    // presented beat (STB), not by CYC alone: after the last beat the L1 port keeps CYC high, STB low, for the clock
    // in which g2_wb64to32 gives the core its registered response, and that clock must not start a new ownership.
    // own_io: an IO beat is on the bus and not yet acknowledged.
    reg  own_l1, own_io;
    wire free   = !own_l1 && !own_io;
    wire g_l1   = own_l1 || (free && l1_cyc);
    wire g_io   = own_io || (free && !l1_cyc && io_cyc);
    wire d_done = dBusWishbone_ACK || dBusWishbone_ERR;

    always @(posedge clk)
        if (reset) begin
            own_l1 <= 1'b0;
            own_io <= 1'b0;
        end else begin
            own_l1 <= g_l1 && (own_l1 || (l1_cyc && l1_stb)) && !(d_done && l1_stb && (l1_cti == 3'b111));
            own_io <= g_io && io_cyc && !d_done;
        end

    assign dBusWishbone_CYC     = g_l1 ? (l1_cyc || own_l1) : g_io ? io_cyc : 1'b0;
    assign dBusWishbone_STB     = g_l1 ? l1_stb  : g_io ? io_stb : 1'b0;
    assign dBusWishbone_WE      = g_l1 ? l1_we   : io_we;
`ifdef G2CPU_FAULT_BEAT
    assign dBusWishbone_ADR     = g_l1 ? {l1_adr[29:4], 4'd0} : io_adr;     // every beat to the line base
`else
    assign dBusWishbone_ADR     = g_l1 ? l1_adr  : io_adr;
`endif
    assign dBusWishbone_DAT_MOSI = g_l1 ? l1_mosi : io_mosi;
    assign dBusWishbone_SEL     = g_l1 ? l1_sel  : io_sel;
`ifdef G2CPU_FAULT_IOBURST
    assign dBusWishbone_CTI     = g_l1 ? l1_cti  : 3'b010;                   // IO beats marked as bursts
`else
    assign dBusWishbone_CTI     = g_l1 ? l1_cti  : io_cti;
`endif
    assign dBusWishbone_BTE     = g_l1 ? l1_bte  : io_bte;

    assign l1_ack = g_l1 && l1_stb && dBusWishbone_ACK;
    assign l1_err = g_l1 && l1_stb && dBusWishbone_ERR;
    assign io_ack = g_io && io_stb && dBusWishbone_ACK;
    assign io_err = g_io && io_stb && dBusWishbone_ERR;

endmodule

`default_nettype wire

// g2_wb64to32 -- one 64-bit Wishbone master port (8-byte word address, SEL[7:0]) to 32-bit beats.
//   Each up beat becomes one or two down beats, the lower word (bits 31:0, down ADR = {u_adr, 0}) first and the
//   upper word (bits 63:32, down ADR = {u_adr, 1}) second.  BOTH = 1 transfers both words of every beat (line
//   refills and write-backs; down SEL = 4'hF on reads); BOTH = 0 transfers only the words whose byte lanes are
//   set, and the lower word alone when no lane is set.  CTI = 3'b111 is passed on the last down beat of an up beat
//   that carries it and becomes 3'b010 on the down beat before it; other CTI values and BTE pass unchanged.
//   REGISTERED RESPONSE: the up ACK (or ERR) and the read data {upper, lower} (a single word copied into both
//   halves) are registered from the last down beat and given to the core in the next clock; in that clock the
//   down STB is low (a master wait state, CYC stays high).  The core's response path therefore starts at a flop
//   here instead of running through the dBus merge and the fabric's ACK, at one clock per up beat.  An ERR on
//   either down beat ends the up beat with ERR.  Up to down (the request) is combinational.
module g2_wb64to32 #(parameter BOTH = 0) (
    input  wire        clk,
    input  wire        reset,
    input  wire        u_cyc,
    input  wire        u_stb,
    input  wire        u_we,
    input  wire [28:0] u_adr,
    input  wire [63:0] u_mosi,
    input  wire [7:0]  u_sel,
    input  wire [2:0]  u_cti,
    input  wire [1:0]  u_bte,
    output wire        u_ack,
    output wire        u_err,
    output wire [63:0] u_miso,
    output wire        d_cyc,
    output wire        d_stb,
    output wire        d_we,
    output wire [29:0] d_adr,
    output wire [31:0] d_mosi,
    output wire [3:0]  d_sel,
    output wire [2:0]  d_cti,
    output wire [1:0]  d_bte,
    input  wire        d_ack,
    input  wire        d_err,
    input  wire [31:0] d_miso
);
    reg        hi_now = 1'b0;           // the lower word of this up beat has been acknowledged
    reg [31:0] lo_data = 32'd0;
    reg        ack_r = 1'b0, err_r = 1'b0;
    reg [63:0] miso_r = 64'd0;
    wire need_lo = BOTH ? 1'b1 : (|u_sel[3:0] || !(|u_sel[7:4]));
    wire need_hi = BOTH ? 1'b1 : |u_sel[7:4];
    wire word    = hi_now || !need_lo;  // 0 lower, 1 upper
    wire last    = word || !need_hi;
    wire hold    = ack_r || err_r;      // the clock in which the core takes the registered response
    wire go      = u_stb && !hold;

    always @(posedge clk) begin
        ack_r <= 1'b0;
        err_r <= 1'b0;
        if (reset || !u_cyc) hi_now <= 1'b0;
        else if (go && d_err) begin
            hi_now <= 1'b0;
            err_r  <= 1'b1;
        end else if (go && d_ack) begin
            if (!last) begin
                hi_now  <= 1'b1;
                lo_data <= d_miso;
            end else begin
                hi_now <= 1'b0;
                ack_r  <= 1'b1;
                miso_r <= word ? {d_miso, (hi_now ? lo_data : d_miso)} : {d_miso, d_miso};
            end
        end
    end

    assign d_cyc  = u_cyc;
    assign d_stb  = go;
    assign d_we   = u_we;
    assign d_adr  = {u_adr, word};
    assign d_mosi = word ? u_mosi[63:32] : u_mosi[31:0];
    assign d_sel  = (BOTH && !u_we) ? 4'hF : (word ? u_sel[7:4] : u_sel[3:0]);
    assign d_cti  = (u_cti == 3'b111 && !last) ? 3'b010 : u_cti;
    assign d_bte  = u_bte;
    assign u_ack  = ack_r;
    assign u_err  = err_r;
    assign u_miso = miso_r;
endmodule
