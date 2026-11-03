module hid (
	clk,
	reset,
	data_in_strobe,
	data_in_start,
	data_in,
	data_out,
	db9_port,
	irq,
	iack,
	mouse_buttons,
	kbd_mouse_level,
	kbd_mouse_type,
	kbd_mouse_data,
	vsync_n,
	keyboard_ack,
	kbd_reset,
	joystick0,
	joystick1
);
	parameter KEYBOARD_FIFO_BITS = 3;
	input clk;
	input reset;
	input data_in_strobe;
	input data_in_start;
	input [7:0] data_in;
	output reg [7:0] data_out;
	input [5:0] db9_port;
	output reg irq;
	input iack;
	output reg [2:0] mouse_buttons;
	output reg kbd_mouse_level;
	output reg [1:0] kbd_mouse_type;
	output reg [7:0] kbd_mouse_data;
	input vsync_n;
	input keyboard_ack;
	output reg kbd_reset;
	output reg [7:0] joystick0;
	output reg [7:0] joystick1;
	localparam [KEYBOARD_FIFO_BITS:0] KEYBOARD_FIFO_DEPTH = {1'b1, {KEYBOARD_FIFO_BITS {1'b0}}};
	(* ram_style = "distributed", syn_ramstyle = "distributed" *) reg [7:0] keyboard_fifo [0:KEYBOARD_FIFO_DEPTH - 1];
	reg [KEYBOARD_FIFO_BITS - 1:0] keyboard_wr_ptr;
	reg [KEYBOARD_FIFO_BITS - 1:0] keyboard_rd_ptr;
	reg [KEYBOARD_FIFO_BITS:0] keyboard_count;
	reg [1:0] keyboard_vsync_d;
	reg keyboard_busy;
	reg keyboard_aged;
	reg [7:0] command;
	reg [3:0] state;
	wire mouse_event = (((data_in_strobe && !data_in_start) && (command == 8'd2)) && (state >= 4'd1)) && (state <= 4'd3);
	wire keyboard_timeout = (keyboard_aged && keyboard_vsync_d[1]) && !keyboard_vsync_d[0];
	wire keyboard_pop = (keyboard_ack || keyboard_timeout) && keyboard_busy;
	wire [6:0] amiga_keycode;
	wire keyboard_push = ((((data_in_strobe && !data_in_start) && (command == 8'd1)) && (state == 4'd0)) && (amiga_keycode != 7'h7f)) && ((keyboard_count != KEYBOARD_FIFO_DEPTH) || keyboard_pop);
	wire keyboard_send = (((keyboard_count != 0) && !keyboard_busy) && !keyboard_ack) && !mouse_event;
	wire [7:0] keyboard_head = keyboard_fifo[keyboard_rd_ptr];
	always @(posedge clk)
		if (!reset && keyboard_push)
			keyboard_fifo[keyboard_wr_ptr] <= {data_in[7], amiga_keycode};
	always @(posedge clk)
		if (reset) begin
			keyboard_wr_ptr <= 0;
			keyboard_rd_ptr <= 0;
			keyboard_count <= 0;
			keyboard_busy <= 0;
			keyboard_aged <= 0;
			keyboard_vsync_d <= 0;
		end
		else begin
			keyboard_vsync_d <= {keyboard_vsync_d[0], vsync_n};
			if (keyboard_push)
				keyboard_wr_ptr <= keyboard_wr_ptr + 1;
			if (keyboard_pop)
				keyboard_rd_ptr <= keyboard_rd_ptr + 1;
			case ({keyboard_push, keyboard_pop})
				2'b10: keyboard_count <= keyboard_count + 1;
				2'b01: keyboard_count <= keyboard_count - 1;
				default:
					;
			endcase
			if ((keyboard_busy && keyboard_vsync_d[1]) && !keyboard_vsync_d[0])
				keyboard_aged <= 1;
			if (keyboard_pop) begin
				keyboard_busy <= 0;
				keyboard_aged <= 0;
			end
			if (keyboard_send) begin
				keyboard_busy <= 1;
				keyboard_aged <= 0;
			end
		end
	reg [7:0] device;
	reg irq_enable;
	reg [5:0] db9_portD;
	reg [5:0] db9_portD2;
	reg lctrl_down;
	reg lamiga_down;
	reg ramiga_down;
	reg kbd_combo_prev;
	wire kbd_combo_now;
	keymap keymap(
		.code(data_in[6:0]),
		.amiga(amiga_keycode)
	);
	always @(posedge clk)
		if (reset) begin
			state <= 4'd0;
			irq <= 1'b0;
			irq_enable <= 1'b0;
			kbd_mouse_level <= 1'b0;
			lctrl_down <= 1'b0;
			lamiga_down <= 1'b0;
			ramiga_down <= 1'b0;
		end
		else begin
			db9_portD <= db9_port;
			db9_portD2 <= db9_portD;
			if (irq_enable) begin
				if (db9_portD2 != db9_portD) begin
					irq <= 1'b1;
					irq_enable <= 1'b0;
				end
			end
			if (iack)
				irq <= 1'b0;
			if (keyboard_send) begin
				kbd_mouse_level <= !kbd_mouse_level;
				kbd_mouse_type <= 2'd2;
				kbd_mouse_data <= keyboard_head;
			end
			if (data_in_strobe) begin
				if (data_in_start) begin
					state <= 4'd0;
					command <= data_in;
				end
				else begin
					if (state != 4'd15)
						state <= state + 4'd1;
					if (command == 8'd0) begin
						if (state == 4'd0)
							data_out <= 8'h01;
						if (state == 4'd1)
							data_out <= 8'h00;
					end
					if (command == 8'd1) begin
						if ((state == 4'd0) && (amiga_keycode != 7'h7f)) begin
							if (amiga_keycode == 7'h63)
								lctrl_down <= ~data_in[7];
							if (amiga_keycode == 7'h66)
								lamiga_down <= ~data_in[7];
							if (amiga_keycode == 7'h67)
								ramiga_down <= ~data_in[7];
						end
					end
					if (command == 8'd2) begin
						if (state == 4'd0)
							mouse_buttons <= data_in[2:0];
						if (state == 4'd1) begin
							kbd_mouse_level <= !kbd_mouse_level;
							kbd_mouse_type <= 2'd0;
							kbd_mouse_data <= data_in;
						end
						if (state == 4'd2) begin
							kbd_mouse_level <= !kbd_mouse_level;
							kbd_mouse_type <= 2'd1;
							kbd_mouse_data <= data_in;
						end
						if (state == 4'd3) begin
							kbd_mouse_level <= !kbd_mouse_level;
							kbd_mouse_data <= data_in;
						end
					end
					if (command == 8'd3) begin
						if (state == 4'd0)
							device <= data_in;
						if (state == 4'd1) begin
							if (device == 8'd0)
								joystick0 <= data_in;
							if (device == 8'd1)
								joystick1 <= data_in;
						end
					end
					if (command == 8'd4) begin
						if (state == 4'd0)
							irq_enable <= 1'b1;
						data_out <= {2'b00, db9_portD};
					end
				end
			end
		end
	assign kbd_combo_now = (lctrl_down & lamiga_down) & ramiga_down;
	always @(posedge clk)
		if (reset) begin
			kbd_combo_prev <= 1'b0;
			kbd_reset <= 1'b0;
		end
		else begin
			kbd_reset <= kbd_combo_now & ~kbd_combo_prev;
			kbd_combo_prev <= kbd_combo_now;
		end
endmodule
module keymap (
	code,
	amiga
);
	input [6:0] code;
	output wire [6:0] amiga;
	assign amiga = (code == 7'h04 ? 7'h20 : (code == 7'h05 ? 7'h35 : (code == 7'h06 ? 7'h33 : (code == 7'h07 ? 7'h22 : (code == 7'h08 ? 7'h12 : (code == 7'h09 ? 7'h23 : (code == 7'h0a ? 7'h24 : (code == 7'h0b ? 7'h25 : (code == 7'h0c ? 7'h17 : (code == 7'h0d ? 7'h26 : (code == 7'h0e ? 7'h27 : (code == 7'h0f ? 7'h28 : (code == 7'h10 ? 7'h37 : (code == 7'h11 ? 7'h36 : (code == 7'h12 ? 7'h18 : (code == 7'h13 ? 7'h19 : (code == 7'h14 ? 7'h10 : (code == 7'h15 ? 7'h13 : (code == 7'h16 ? 7'h21 : (code == 7'h17 ? 7'h14 : (code == 7'h18 ? 7'h16 : (code == 7'h19 ? 7'h34 : (code == 7'h1a ? 7'h11 : (code == 7'h1b ? 7'h32 : (code == 7'h1c ? 7'h15 : (code == 7'h1d ? 7'h31 : (code == 7'h1e ? 7'h01 : (code == 7'h1f ? 7'h02 : (code == 7'h20 ? 7'h03 : (code == 7'h21 ? 7'h04 : (code == 7'h22 ? 7'h05 : (code == 7'h23 ? 7'h06 : (code == 7'h24 ? 7'h07 : (code == 7'h25 ? 7'h08 : (code == 7'h26 ? 7'h09 : (code == 7'h27 ? 7'h0a : (code == 7'h28 ? 7'h44 : (code == 7'h29 ? 7'h45 : (code == 7'h2a ? 7'h41 : (code == 7'h2b ? 7'h42 : (code == 7'h2c ? 7'h40 : (code == 7'h2d ? 7'h0b : (code == 7'h2e ? 7'h0c : (code == 7'h2f ? 7'h1a : (code == 7'h30 ? 7'h1b : (code == 7'h31 ? 7'h0d : (code == 7'h32 ? 7'h2b : (code == 7'h33 ? 7'h29 : (code == 7'h34 ? 7'h2a : (code == 7'h35 ? 7'h00 : (code == 7'h36 ? 7'h38 : (code == 7'h37 ? 7'h39 : (code == 7'h38 ? 7'h3a : (code == 7'h39 ? 7'h62 : (code == 7'h3a ? 7'h50 : (code == 7'h3b ? 7'h51 : (code == 7'h3c ? 7'h52 : (code == 7'h3d ? 7'h53 : (code == 7'h3e ? 7'h54 : (code == 7'h3f ? 7'h55 : (code == 7'h40 ? 7'h56 : (code == 7'h41 ? 7'h57 : (code == 7'h42 ? 7'h58 : (code == 7'h43 ? 7'h59 : (code == 7'h49 ? 7'h5f : (code == 7'h4a ? 7'h5a : (code == 7'h4b ? 7'h5b : (code == 7'h4c ? 7'h46 : (code == 7'h4d ? 7'h5f : (code == 7'h4e ? 7'h67 : (code == 7'h4f ? 7'h4e : (code == 7'h50 ? 7'h4f : (code == 7'h51 ? 7'h4d : (code == 7'h52 ? 7'h4c : (code == 7'h54 ? 7'h5c : (code == 7'h55 ? 7'h5d : (code == 7'h56 ? 7'h4a : (code == 7'h57 ? 7'h5e : (code == 7'h58 ? 7'h43 : (code == 7'h59 ? 7'h1d : (code == 7'h5a ? 7'h1e : (code == 7'h5b ? 7'h1f : (code == 7'h5c ? 7'h2d : (code == 7'h5d ? 7'h2e : (code == 7'h5e ? 7'h2f : (code == 7'h5f ? 7'h3d : (code == 7'h60 ? 7'h3e : (code == 7'h61 ? 7'h3f : (code == 7'h62 ? 7'h0f : (code == 7'h63 ? 7'h3c : (code == 7'h64 ? 7'h2b : (code == 7'h68 ? 7'h63 : (code == 7'h69 ? 7'h60 : (code == 7'h6a ? 7'h64 : (code == 7'h6b ? 7'h66 : (code == 7'h6d ? 7'h61 : (code == 7'h6e ? 7'h65 : (code == 7'h6f ? 7'h67 : 7'h7f))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
endmodule
module sysctrl (
	clk,
	reset,
	data_in_strobe,
	data_in_start,
	data_in,
	data_out,
	int_out_n,
	int_in,
	int_ack,
	buttons,
	leds,
	color,
	jtagsel,
	rtc,
	system_reset,
	system_floppy_drives,
	system_floppy_wrprot,
	system_floppy_turbo,
	system_cpu,
	system_chipset,
	system_video_mode,
	system_video_screen,
	system_ide_enable,
	system_video_filter,
	system_video_scanlines,
	system_chipmem,
	system_slowmem,
	system_fastmem,
	system_turbo,
	system_joy_swap,
	system_volume,
	system_stereo_mix,
	system_kickstart,
	system_drive_sounds,
	system_lcd_v_pos
);
	parameter AGA = 0;
	input clk;
	input reset;
	input data_in_strobe;
	input data_in_start;
	input [7:0] data_in;
	output reg [7:0] data_out;
	output wire int_out_n;
	input [7:0] int_in;
	output reg [7:0] int_ack;
	input [1:0] buttons;
	output reg [1:0] leds;
	output reg [23:0] color;
	output reg jtagsel;
	output reg [11:0] rtc;
	output wire system_reset;
	output reg [1:0] system_floppy_drives;
	output reg system_floppy_wrprot;
	output reg system_floppy_turbo;
	output reg [1:0] system_cpu;
	output reg [2:0] system_chipset;
	output reg system_video_mode;
	output reg [1:0] system_video_screen;
	output reg system_ide_enable;
	output reg [1:0] system_video_filter;
	output reg [1:0] system_video_scanlines;
	output reg [1:0] system_chipmem;
	output reg [1:0] system_slowmem;
	output reg [1:0] system_fastmem;
	output reg [2:0] system_turbo;
	output reg system_joy_swap;
	output reg [2:0] system_volume;
	output reg system_stereo_mix;
	output reg [1:0] system_kickstart;
	output reg system_drive_sounds;
	output reg [7:0] system_lcd_v_pos;
	reg [3:0] state;
	reg [7:0] command;
	reg [7:0] id;
	wire [7:0] data_in_rev = {data_in[0], data_in[1], data_in[2], data_in[3], data_in[4], data_in[5], data_in[6], data_in[7]};
	reg coldboot = 1'b1;
	reg sys_int = 1'b1;
	reg [1:0] buttonsD;
	reg [1:0] buttonsD2;
	reg buttons_irq_enable;
	assign int_out_n = ((int_in != 8'h00) || sys_int ? 1'b0 : 1'b1);
	reg main_reset = 1'b1;
	assign system_reset = main_reset;
	reg [26:0] main_reset_timeout;
	reg [11:0] menu_rom_addr;
	reg [7:0] menu_rom_data;
	reg [7:0] amiga_xml [0:1535];
	initial $readmemh("amiga_xml.hex", amiga_xml);
	reg [7:0] rtc_cmd;
	always @(posedge clk) menu_rom_data <= amiga_xml[menu_rom_addr];
	always @(posedge clk)
		if (reset) begin
			state <= 4'd0;
			leds <= 2'b00;
			color <= 24'h000000;
			main_reset <= 1'b1;
			main_reset_timeout <= 27'd84000000;
			buttons_irq_enable <= 1'b1;
			int_ack <= 8'h00;
			coldboot <= 1'b1;
			sys_int <= 1'b1;
			jtagsel <= 1'b0;
			rtc <= 12'h000;
			system_floppy_drives <= 2'd0;
			system_floppy_turbo <= 1'b1;
			system_floppy_wrprot <= 1'b1;
			system_cpu <= 2'b11;
			system_chipset <= (AGA ? 3'd6 : 3'd2);
			system_video_mode <= 1'b0;
			system_video_screen <= 2'd0;
			system_video_filter <= 2'd0;
			system_video_scanlines <= 2'd0;
			system_chipmem <= (AGA ? 2'd2 : 2'd0);
			system_slowmem <= 2'd1;
			system_fastmem <= 2'd0;
			system_turbo <= 3'b011;
			system_ide_enable <= 1'b0;
			system_joy_swap <= 1'b1;
			system_volume <= 3'b010;
			system_stereo_mix <= 1'b1;
			system_kickstart <= 2'b01;
			system_drive_sounds <= 1'b0;
		end
		else begin
			buttonsD <= buttons;
			buttonsD2 <= buttonsD;
			if (main_reset_timeout) begin
				main_reset_timeout <= main_reset_timeout - 27'd1;
				if (main_reset_timeout == 27'd1) begin
					main_reset <= 1'b0;
					color <= 24'h000202;
				end
			end
			int_ack <= 8'h00;
			if (int_ack[0])
				sys_int <= 1'b0;
			if (buttons_irq_enable) begin
				if (buttonsD2 != buttonsD) begin
					sys_int <= 1'b1;
					buttons_irq_enable <= 1'b0;
				end
			end
			if (data_in_strobe) begin
				if (data_in_start) begin
					state <= 4'd0;
					command <= data_in;
					menu_rom_addr <= 12'h000;
					data_out <= 8'h00;
				end
				else begin
					if (state != 4'd15)
						state <= state + 4'd1;
					if (command == 8'd0) begin
						if (state == 4'd0)
							data_out <= 8'h5c;
						if (state == 4'd1)
							data_out <= 8'h42;
						if (state == 4'd2)
							data_out <= 8'h00;
					end
					if (command == 8'd1) begin
						if (state == 4'd0)
							leds <= data_in[1:0];
					end
					if (command == 8'd2) begin
						if (state == 4'd0)
							color[15:8] <= data_in_rev;
						if (state == 4'd1)
							color[7:0] <= data_in_rev;
						if (state == 4'd2)
							color[23:16] <= data_in_rev;
					end
					if (command == 8'd3) begin
						data_out <= {6'b000000, buttons};
						buttons_irq_enable <= 1'b1;
					end
					if (command == 8'd4) begin
						if (state == 4'd0)
							id <= data_in;
						if (state == 4'd1) begin
							if (id == "R") begin
								main_reset <= data_in[0];
								main_reset_timeout <= 27'd0;
							end
							if (id == "D")
								system_floppy_drives <= data_in[1:0];
							if (id == "S")
								system_floppy_turbo <= data_in[0];
							if (id == "P")
								system_floppy_wrprot <= data_in[0];
							if (id == "C")
								system_chipset <= (AGA && (data_in[1:0] == 2'd3) ? 3'd6 : {1'b0, data_in[1:0]});
							if (id == "T")
								system_cpu <= (data_in[1:0] == 2'b10 ? 2'b11 : data_in[1:0]);
							if (id == "F")
								system_video_filter <= data_in[1:0];
							if (id == "V")
								system_video_mode <= data_in[0];
							if (id == "W")
								system_video_screen <= data_in[1:0];
							if (id == "L")
								system_video_scanlines <= data_in[1:0];
							if (id == "Y")
								system_chipmem <= data_in[1:0];
							if (id == "X")
								system_slowmem <= data_in[1:0];
							if (id == "Q")
								system_fastmem <= data_in[1:0];
							if (id == "Z")
								system_turbo[1:0] <= data_in[1:0];
							if (id == "U")
								system_turbo[2] <= data_in[0];
							if (id == "I")
								system_ide_enable <= data_in[0];
							if (id == "J")
								system_joy_swap <= data_in[0];
							if (id == "A")
								system_volume <= data_in[2:0];
							if (id == "M")
								system_stereo_mix <= data_in[0];
							if (id == "K")
								system_kickstart <= data_in[1:0];
							if (id == "E")
								system_drive_sounds <= data_in[0];
							if (id == "B")
								system_lcd_v_pos <= data_in;
						end
					end
					if (command == 8'd5) begin
						if (state == 4'd0)
							int_ack <= data_in;
						data_out <= {int_in[7:1], sys_int};
					end
					if (command == 8'd6) begin
						data_out <= {5'b00000, !buttons_irq_enable, 1'b0, coldboot};
						if (state == 4'd0)
							coldboot <= 1'b0;
					end
					if (command == 8'd8) begin
						data_out <= menu_rom_data;
						menu_rom_addr <= menu_rom_addr + 12'd1;
					end
					if (command == 8'd9) begin
						if (state == 4'd0)
							jtagsel <= data_in[0];
					end
					if (command == 8'd10) begin
						if (state == 4'd0)
							rtc_cmd <= data_in;
						else if (rtc_cmd == 8'd0) begin
							if (state >= 4'd2)
								rtc <= {!rtc[11], state[2:0] - 3'd2, data_in};
						end
					end
				end
			end
		end
endmodule
module osd_u8g2 (
	clk,
	reset,
	data_in_strobe,
	data_in_start,
	data_in,
	hs,
	vs,
	r_in,
	g_in,
	b_in,
	r_out,
	g_out,
	b_out
);
	input clk;
	input reset;
	input data_in_strobe;
	input data_in_start;
	input [7:0] data_in;
	input hs;
	input vs;
	input [5:0] r_in;
	input [5:0] g_in;
	input [5:0] b_in;
	output wire [5:0] r_out;
	output wire [5:0] g_out;
	output wire [5:0] b_out;
	localparam HCNT_BITS = 11;
	localparam VCNT_BITS = 10;
	reg enabled;
	reg vsD;
	reg hsD;
	reg [10:0] hcnt;
	reg [10:0] scr_width;
	reg [9:0] vcnt;
	reg [9:0] scr_height;
	reg active;
	reg tactive;
	reg sactive;
	wire osd_pix;
	wire [5:0] osd_pix_col;
	wire [5:0] osd_r = (tactive && osd_pix ? osd_pix_col : (sactive ? {4'b0000, r_in[5:4]} : {3'b000, r_in[5:3]}));
	wire [5:0] osd_g = (tactive && osd_pix ? osd_pix_col : (sactive ? {4'b0000, g_in[5:4]} : {3'b000, g_in[5:3]}));
	wire [5:0] osd_b = (tactive && osd_pix ? osd_pix_col : (sactive ? {4'b0100, b_in[5:4]} : {3'b010, b_in[5:3]}));
	assign r_out = (!enabled ? r_in : (active ? osd_r : (sactive ? {1'b0, r_in[5:1]} : r_in)));
	assign g_out = (!enabled ? g_in : (active ? osd_g : (sactive ? {1'b0, g_in[5:1]} : g_in)));
	assign b_out = (!enabled ? b_in : (active ? osd_b : (sactive ? {1'b0, b_in[5:1]} : b_in)));
	wire [10:0] hstart = {1'b0, scr_width[10:1]} - 128;
	wire [9:0] vstart = {1'b0, scr_height[9:1]} - 64;
	wire signed [HCNT_BITS:0] hpos = $signed({1'b0, hcnt}) - $signed({1'b0, hstart});
	wire signed [VCNT_BITS:0] vpos = $signed({1'b0, vcnt}) - $signed({1'b0, vstart});
	always @(posedge clk) begin
		active <= (((hpos >= -5) && (hpos < 259)) && (vpos >= -4)) && (vpos < 132);
		tactive <= (((hpos >= -1) && (hpos < 255)) && (vpos >= 0)) && (vpos < 128);
		sactive <= (((hpos >= 3) && (hpos < 267)) && (vpos >= 4)) && (vpos < 140);
	end
	reg [7:0] buffer [0:1023];
	reg [9:0] data_cnt;
	reg [7:0] command;
	reg data_addr_state;
	always @(posedge clk)
		if (reset)
			enabled <= 1'b0;
		else if (data_in_strobe) begin
			if (data_in_start) begin
				command <= data_in;
				data_addr_state <= 1'b1;
				data_cnt <= 10'd0;
			end
			else begin
				data_addr_state <= 1'b0;
				if ((command == 8'd1) && data_addr_state)
					enabled <= data_in[0];
				if (command == 8'd2) begin
					if (data_addr_state)
						data_cnt <= {data_in[6:0], 3'b000};
					else begin
						buffer[data_cnt] <= data_in;
						data_cnt <= data_cnt + 10'd1;
					end
				end
			end
		end
	wire [7:0] hpix = hpos[7:0];
	wire [7:0] hpixD = hpix + 1;
	wire [6:0] vpix = vpos[6:0];
	reg [7:0] buffer_byte;
	assign osd_pix = buffer_byte[vpix[3:1]];
	always @(posedge clk) buffer_byte <= buffer[{vpix[6:4], hpixD[7:1]}];
	assign osd_pix_col = 6'd63;
	always @(posedge clk) begin
		hsD <= hs;
		if (hs && !hsD) begin
			scr_width <= hcnt;
			hcnt <= 0;
		end
		else
			hcnt <= hcnt + 12'd1;
		if (hs && !hsD) begin
			vsD <= vs;
			if (!vs && vsD) begin
				scr_height <= vcnt;
				vcnt <= 0;
			end
			else
				vcnt <= vcnt + 10'd1;
		end
	end
endmodule