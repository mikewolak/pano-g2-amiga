/*
 * Edited for pano-g2-amiga by Mike Wolak, EPROM Foundry Inc. (the edits: amiga/THIRD_PARTY.md).
 * This file keeps the licence of its origin, stated below or in amiga/nanomig (GPL-3.0-or-later).
 */
//2048 words deep, 16 bits wide, fifo
//data is written into the fifo when wr=1
//reading is more or less asynchronous if you read during the rising edge of clk
//because the output data is updated at the falling edge of the clk
//when rd=1, the next data word is selected 


module paula_floppy_fifo
(
	input		  clk, // bus clock
	input		  clk7_en,
	input		  reset, // reset 
	input [15:0]	  in, // data in
	output reg [15:0] out, // data out
	input		  rd, // read from fifo
	input		  wr, // write to fifo
	output  	  empty, // fifo is empty
	output		  full  // fifo is full
);

//local signals and registers
reg 	[15:0] mem [2047:0];	// 2048 words by 16 bit wide fifo memory (for 2 MFM-encoded sectors)
reg	[11:0] in_ptr;		// fifo input pointer
reg	[11:0] out_ptr;		// fifo output pointer

// check lower 11 bits of pointer to generate equal signal
wire   equal = (in_ptr[10:0]==out_ptr[10:0]);
assign empty = (equal && (in_ptr[11] == out_ptr[11]));
assign full =  (equal && (in_ptr[11] != out_ptr[11]));

// the word after the output pointer, read on every clock so that XST maps mem to block RAM (a read under the
// clock enable and the bypass below keep it in LUTs).  clk7_en is never high on two consecutive clocks and the
// pointers and mem change only under it, so this register holds, at every clk7_en edge, the value the read
// mem[out_ptr[10:0] + 11'd1] at that edge returns.
reg	[15:0] mem_next;
always @(posedge clk) mem_next <= mem[out_ptr[10:0] + 11'd1];

always @(posedge clk) begin
   reg empty_write;
   
   if (clk7_en) begin
      empty_write <= 1'b0;
      
      if (reset) begin
  	 in_ptr <= 12'd0;
  	 out_ptr <= 12'd0;
	 empty_write <= 1'b0;
      end else begin
  	 if(rd && !empty) begin
	    if(empty_write) out <= in;
	    else            out <= mem_next;
  	    out_ptr <= out_ptr + 12'd1;
	 end
	 if(wr && !full) begin
  	    mem[in_ptr[10:0]] <= in;
  	    in_ptr <= in_ptr + 12'd1;
	    if(empty) begin
	       out <= in;
	       empty_write <= 1'b1;
	    end
	 end
      end
   end
end
   
endmodule

