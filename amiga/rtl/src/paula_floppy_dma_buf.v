/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// paula_floppy_dma_buf.v -- 256 x 16 true dual-port RAM with byte selects, the ports and behaviour of
// nanomig/src/minimig-aga/paula_floppy_dma_buf.v (registered read, read-first; bs[1] writes [15:8], bs[0] [7:0]).
// Written as two 256 x 8 lanes, each with one process and one write per port, the form XST 14.7 maps to RAMB16.
module paula_floppy_dma_buf (
	input  wire [15:0] dina,
	input  wire [15:0] dinb,
	input  wire [1:0]  bsa,
	input  wire [1:0]  bsb,
	input  wire [7:0]  addra,
	input  wire [7:0]  addrb,
	input  wire        clk,
	input  wire        wrena,
	input  wire        wrenb,
	output wire [15:0] douta,
	output wire [15:0] doutb
);

reg [7:0] hi [0:255];
reg [7:0] lo [0:255];
reg [7:0] hi_a, hi_b, lo_a, lo_b;
assign douta = {hi_a, lo_a};
assign doutb = {hi_b, lo_b};

always @(posedge clk) begin
	if (wrena && bsa[1]) hi[addra] <= dina[15:8];
	hi_a <= hi[addra];
end
always @(posedge clk) begin
	if (wrenb && bsb[1]) hi[addrb] <= dinb[15:8];
	hi_b <= hi[addrb];
end
always @(posedge clk) begin
	if (wrena && bsa[0]) lo[addra] <= dina[7:0];
	lo_a <= lo[addra];
end
always @(posedge clk) begin
	if (wrenb && bsb[0]) lo[addrb] <= dinb[7:0];
	lo_b <= lo[addrb];
end

endmodule
