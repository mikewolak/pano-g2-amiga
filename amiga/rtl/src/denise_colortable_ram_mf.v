/*
 * Edited for pano-g2-amiga by Mike Wolak, EPROM Foundry Inc. (the edits: amiga/THIRD_PARTY.md).
 * This file keeps the licence of its origin, stated below or in amiga/nanomig (GPL-3.0-or-later).
 */
// colortable ram

module denise_colortable_ram_mf (
        input [1:0]	  ena_a,
	input		  clock,
	input [11:0]	  data,
	input [7:0]	  rdaddress,
	input [7:0]	  wraddress,
	input		  wren,
	output reg [23:0] q
);

reg [11:0] ram0[255:0];
reg [11:0] ram1[255:0];

// the write registered one clock: the chip bus (custom_data_in) reaches the RAM's write port through a flop
reg        wren_r = 1'b0;
reg [1:0]  ena_r;
reg [7:0]  wa_r;
reg [11:0] data_r;
   
always @(posedge clock) begin
   wren_r <= wren; ena_r <= ena_a; wa_r <= wraddress; data_r <= data;
   if(wren_r) begin
      if(ena_r[0]) ram0[wa_r] <= data_r;
      if(ena_r[1]) ram1[wa_r] <= data_r;
   end

   q <= { ram1[rdaddress], ram0[rdaddress] }; 
end
   
endmodule
