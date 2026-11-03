/*
 * Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
 */
// ide_dpram.v -- 4096 x 16 true dual-port RAM with the ports of the ide_dpram instances in rtl/src/ide.v, in the
// form XST 14.7 maps to RAMB16 (one process per port, one write per port, registered read, read-first).
// cea/ceb/ocea/oceb are tied to 1 by every instance; reseta/resetb are not used (the buffer contents need no reset).
module ide_dpram (
	input             clka,
	input             reseta,
	input             cea,
	input      [11:0] ada,
	input             wrea,
	input      [15:0] dina,
	input             ocea,
	output reg [15:0] douta,
	input             clkb,
	input             resetb,
	input             ceb,
	input      [11:0] adb,
	input             wreb,
	input      [15:0] dinb,
	input             oceb,
	output reg [15:0] doutb
);

reg [15:0] mem [0:4095];

always @(posedge clka) if (cea) begin
	if (wrea) mem[ada] <= dina;
	douta <= mem[ada];
end

always @(posedge clkb) if (ceb) begin
	if (wreb) mem[adb] <= dinb;
	doutb <= mem[adb];
end

endmodule
