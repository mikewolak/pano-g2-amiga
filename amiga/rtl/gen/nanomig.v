module fx68k (
	clk,
	HALTn,
	extReset,
	pwrUp,
	enPhi1,
	enPhi2,
	eRWn,
	ASn,
	LDSn,
	UDSn,
	E,
	VMAn,
	FC0,
	FC1,
	FC2,
	BGn,
	oRESETn,
	oHALTEDn,
	DTACKn,
	VPAn,
	BERRn,
	BRn,
	BGACKn,
	IPL0n,
	IPL1n,
	IPL2n,
	iEdb,
	oEdb,
	eab
);
	reg _sv2v_0;
	input clk;
	input HALTn;
	input extReset;
	input pwrUp;
	input enPhi1;
	input enPhi2;
	output wire eRWn;
	output wire ASn;
	output wire LDSn;
	output wire UDSn;
	output reg E;
	output wire VMAn;
	output wire FC0;
	output wire FC1;
	output wire FC2;
	output wire BGn;
	output wire oRESETn;
	output wire oHALTEDn;
	input DTACKn;
	input VPAn;
	input BERRn;
	input BRn;
	input BGACKn;
	input IPL0n;
	input IPL1n;
	input IPL2n;
	input [15:0] iEdb;
	output wire [15:0] oEdb;
	output wire [23:1] eab;
	wire [4:0] Clks;
	assign Clks[4] = clk;
	assign Clks[3] = extReset;
	assign Clks[2] = pwrUp;
	assign Clks[1] = enPhi1;
	assign Clks[0] = enPhi2;
	wire wClk;
	reg [31:0] tState;
	wire enT1 = (Clks[1] & (tState == 32'd4)) & ~wClk;
	wire enT2 = Clks[0] & (tState == 32'd1);
	wire enT3 = Clks[1] & (tState == 32'd2);
	wire enT4 = Clks[0] & ((tState == 32'd0) | (tState == 32'd3));
	always @(posedge Clks[4])
		if (Clks[2])
			tState <= 32'd0;
		else
			case (tState)
				32'd0:
					if (Clks[0])
						tState <= 32'd4;
				32'd1:
					if (Clks[0])
						tState <= 32'd2;
				32'd2:
					if (Clks[1])
						tState <= 32'd3;
				32'd3:
					if (Clks[0])
						tState <= 32'd4;
				32'd4:
					if (Clks[1])
						tState <= (wClk ? 32'd0 : 32'd1);
			endcase
	reg rDtack;
	reg rBerr;
	reg [2:0] rIpl;
	reg [2:0] iIpl;
	reg Vpai;
	reg BeI;
	reg Halti;
	reg BRi;
	reg BgackI;
	reg BeiDelay;
	wire BeDebounced = ~(BeI | BeiDelay);
	always @(posedge Clks[4])
		if (Clks[2]) begin
			rBerr <= 1'b0;
			BeI <= 1'b0;
		end
		else if (Clks[0]) begin
			rDtack <= DTACKn;
			rBerr <= BERRn;
			rIpl <= ~{IPL2n, IPL1n, IPL0n};
			iIpl <= rIpl;
		end
		else if (Clks[1]) begin
			Vpai <= VPAn;
			BeI <= rBerr;
			BeiDelay <= BeI;
			BgackI <= BGACKn;
			BRi <= BRn;
			Halti <= HALTn;
		end
	localparam NANO_WIDTH = 68;
	reg [67:0] nanoLatch;
	wire [67:0] nanoOutput;
	localparam UROM_WIDTH = 17;
	reg [16:0] microLatch;
	wire [16:0] microOutput;
	localparam UADDR_WIDTH = 10;
	reg [9:0] microAddr;
	wire [9:0] nma;
	localparam NADDR_WIDTH = 9;
	reg [8:0] nanoAddr;
	wire [8:0] orgAddr;
	wire rstUrom;
	microToNanoAddr microToNanoAddr(
		.uAddr(nma),
		.orgAddr(orgAddr)
	);
	nanoRom nanoRom(
		.clk(Clks[4]),
		.nanoAddr(nanoAddr),
		.nanoOutput(nanoOutput)
	);
	uRom uRom(
		.clk(Clks[4]),
		.microAddr(microAddr),
		.microOutput(microOutput)
	);
	localparam RSTP0_NMA = 'h2;
	always @(posedge Clks[4]) begin
		if (Clks[2]) begin
			microAddr <= RSTP0_NMA;
			nanoAddr <= RSTP0_NMA;
		end
		else if (enT1) begin
			microAddr <= nma;
			nanoAddr <= orgAddr;
		end
		if (Clks[3]) begin
			microLatch <= 1'sb0;
			nanoLatch <= 1'sb0;
		end
		else if (rstUrom) begin
			{microLatch[16], microLatch[15], microLatch[0]} <= 1'sb0;
			nanoLatch <= 1'sb0;
		end
		else if (enT3) begin
			microLatch <= microOutput;
			nanoLatch <= nanoOutput;
		end
	end
	wire [104:0] Nanod;
	wire [41:0] Irdecod;
	reg Tpend;
	reg intPend;
	reg pswT;
	reg pswS;
	reg [2:0] pswI;
	wire [7:0] ccr;
	wire [15:0] psw = {pswT, 1'b0, pswS, 2'b00, pswI, ccr};
	reg [15:0] ftu;
	reg [15:0] Irc;
	reg [15:0] Ir;
	reg [15:0] Ird;
	wire [15:0] alue;
	wire [15:0] Abl;
	wire prenEmpty;
	wire au05z;
	wire dcr4;
	wire ze;
	wire [9:0] a1;
	wire [9:0] a2;
	wire [9:0] a3;
	wire isPriv;
	wire isIllegal;
	wire isLineA;
	wire isLineF;
	always @(posedge Clks[4])
		if (enT1) begin
			if (Nanod[81])
				Ird <= Ir;
			else if (microLatch[0])
				Ir <= Irc;
		end
	wire [3:0] tvn;
	wire waitBusCycle;
	wire busStarting;
	wire BusRetry = 1'b0;
	wire busAddrErr;
	wire bciWrite;
	wire bgBlock;
	wire busAvail;
	wire addrOe;
	wire busIsByte = Nanod[101] & (Irdecod[32] | Irdecod[31]);
	wire aob0;
	reg iStop;
	reg A0Err;
	reg excRst;
	reg BerrA;
	reg Spuria;
	reg Avia;
	wire Iac;
	reg rAddrErr;
	reg iBusErr;
	reg Err6591;
	wire iAddrErr = rAddrErr & addrOe;
	wire enErrClk;
	assign rstUrom = Clks[1] & enErrClk;
	uaddrDecode uaddrDecode(
		.opcode(Ir),
		.a1(a1),
		.a2(a2),
		.a3(a3),
		.isPriv(isPriv),
		.isIllegal(isIllegal),
		.isLineA(isLineA),
		.isLineF(isLineF),
		.lineBmap()
	);
	sequencer sequencer(
		.Clks(Clks),
		.enT3(enT3),
		.microLatch(microLatch),
		.Ird(Ird),
		.A0Err(A0Err),
		.excRst(excRst),
		.BerrA(BerrA),
		.busAddrErr(busAddrErr),
		.Spuria(Spuria),
		.Avia(Avia),
		.Tpend(Tpend),
		.intPend(intPend),
		.isIllegal(isIllegal),
		.isPriv(isPriv),
		.isLineA(isLineA),
		.isLineF(isLineF),
		.nma(nma),
		.a1(a1),
		.a2(a2),
		.a3(a3),
		.tvn(tvn),
		.psw(psw),
		.prenEmpty(prenEmpty),
		.au05z(au05z),
		.dcr4(dcr4),
		.ze(ze),
		.alue01(alue[1:0]),
		.i11(Irc[11])
	);
	wire [16:1] sv2v_tmp_excUnit_Irc;
	always @(*) Irc = sv2v_tmp_excUnit_Irc;
	excUnit excUnit(
		.Clks(Clks),
		.Nanod(Nanod),
		.Irdecod(Irdecod),
		.enT1(enT1),
		.enT2(enT2),
		.enT3(enT3),
		.enT4(enT4),
		.Ird(Ird),
		.ftu(ftu),
		.iEdb(iEdb),
		.pswS(pswS),
		.prenEmpty(prenEmpty),
		.au05z(au05z),
		.dcr4(dcr4),
		.ze(ze),
		.AblOut(Abl),
		.eab(eab),
		.aob0(aob0),
		.Irc(sv2v_tmp_excUnit_Irc),
		.oEdb(oEdb),
		.alue(alue),
		.ccr(ccr)
	);
	nDecoder3 nDecoder(
		.Clks(Clks),
		.Nanod(Nanod),
		.Irdecod(Irdecod),
		.enT2(enT2),
		.enT4(enT4),
		.microLatch(microLatch),
		.nanoLatch(nanoLatch)
	);
	irdDecode irdDecode(
		.ird(Ird),
		.Irdecod(Irdecod)
	);
	busControl busControl(
		.Clks(Clks),
		.enT1(enT1),
		.enT4(enT4),
		.permStart(Nanod[104]),
		.permStop(Nanod[103]),
		.iStop(iStop),
		.aob0(aob0),
		.isWrite(Nanod[102]),
		.isRmc(Nanod[100]),
		.isByte(busIsByte),
		.busAvail(busAvail),
		.bciWrite(bciWrite),
		.addrOe(addrOe),
		.bgBlock(bgBlock),
		.waitBusCycle(waitBusCycle),
		.busStarting(busStarting),
		.busAddrErr(busAddrErr),
		.rDtack(rDtack),
		.BeDebounced(BeDebounced),
		.Vpai(Vpai),
		.ASn(ASn),
		.LDSn(LDSn),
		.UDSn(UDSn),
		.eRWn(eRWn)
	);
	busArbiter busArbiter(
		.Clks(Clks),
		.BRi(BRi),
		.BgackI(BgackI),
		.Halti(Halti),
		.bgBlock(bgBlock),
		.busAvail(busAvail),
		.BGn(BGn)
	);
	wire [1:0] uFc = microLatch[16:15];
	reg oReset;
	reg oHalted;
	assign oRESETn = !oReset;
	assign oHALTEDn = !oHalted;
	always @(posedge Clks[4])
		if (Clks[2]) begin
			oReset <= 1'b0;
			oHalted <= 1'b0;
		end
		else if (enT1) begin
			oReset <= (uFc == 2'b01) & !Nanod[104];
			oHalted <= (uFc == 2'b10) & !Nanod[104];
		end
	reg [2:0] rFC;
	assign {FC2, FC1, FC0} = rFC;
	assign Iac = {rFC == 3'b111};
	always @(posedge Clks[4])
		if (Clks[3])
			rFC <= 1'sb0;
		else if (enT1 & Nanod[104]) begin
			rFC[2] <= pswS;
			rFC[1] <= microLatch[16] | (~microLatch[15] & Irdecod[41]);
			rFC[0] <= microLatch[15] | (~microLatch[16] & ~Irdecod[41]);
		end
	reg [2:0] inl;
	reg updIll;
	reg prevNmi;
	wire nmi = iIpl == 3'b111;
	wire iplStable = iIpl == rIpl;
	wire iplComp = iIpl > pswI;
	always @(posedge Clks[4]) begin
		if (Clks[3]) begin
			intPend <= 1'b0;
			prevNmi <= 1'b0;
		end
		else begin
			if (Clks[0])
				prevNmi <= nmi;
			if (Clks[0]) begin
				if (iplStable & ((nmi & ~prevNmi) | iplComp))
					intPend <= 1'b1;
				else if (((inl == 3'b111) & Iac) | ((iplStable & !nmi) & !iplComp))
					intPend <= 1'b0;
			end
		end
		if (Clks[3]) begin
			inl <= 1'sb1;
			updIll <= 1'b0;
		end
		else if (enT4)
			updIll <= microLatch[0];
		else if (enT1 & updIll)
			inl <= iIpl;
		if (enT4) begin
			Spuria <= ~BeiDelay & Iac;
			Avia <= ~Vpai & Iac;
		end
	end
	assign enErrClk = iAddrErr | iBusErr;
	assign wClk = ((waitBusCycle | ~BeI) | iAddrErr) | Err6591;
	reg [3:0] eCntr;
	reg rVma;
	assign VMAn = rVma;
	wire xVma = ~rVma & (eCntr == 8);
	always @(posedge Clks[4]) begin
		if (Clks[2]) begin
			E <= 1'b0;
			eCntr <= 1'sb0;
			rVma <= 1'b1;
		end
		if (Clks[0]) begin
			if (eCntr == 9)
				E <= 1'b0;
			else if (eCntr == 5)
				E <= 1'b1;
			if (eCntr == 9)
				eCntr <= 1'sb0;
			else
				eCntr <= eCntr + 1'b1;
		end
		if (((Clks[0] & addrOe) & ~Vpai) & (eCntr == 3))
			rVma <= 1'b0;
		else if (Clks[1] & (eCntr == {4 {1'sb0}}))
			rVma <= 1'b1;
	end
	always @(posedge Clks[4]) begin
		if (Clks[3])
			rAddrErr <= 1'b0;
		else if (Clks[1]) begin
			if (busAddrErr & addrOe)
				rAddrErr <= 1'b1;
			else if (~addrOe)
				rAddrErr <= 1'b0;
		end
		if (Clks[3])
			iBusErr <= 1'b0;
		else if (Clks[1])
			iBusErr <= ((BerrA & ~BeI) & ~Iac) & !BusRetry;
		if (Clks[3])
			BerrA <= 1'b0;
		else if (Clks[0]) begin
			if ((~BeI & ~Iac) & addrOe)
				BerrA <= 1'b1;
			else if (BeI & busStarting)
				BerrA <= 1'b0;
		end
		if (Clks[3])
			excRst <= 1'b1;
		else if (enT2 & Nanod[104])
			excRst <= 1'b0;
		if (Clks[3])
			A0Err <= 1'b1;
		else if (enT3)
			A0Err <= 1'b0;
		else if ((Clks[1] & enErrClk) & (busAddrErr | BerrA))
			A0Err <= 1'b1;
		if (Clks[3]) begin
			iStop <= 1'b0;
			Err6591 <= 1'b0;
		end
		else if (Clks[1])
			Err6591 <= enErrClk;
		else if (Clks[0])
			iStop <= xVma | (Vpai & (iAddrErr | ~rBerr));
	end
	reg irdToCcr_t4;
	always @(posedge Clks[4])
		if (Clks[2]) begin
			Tpend <= 1'b0;
			{pswT, pswS, pswI} <= 1'sb0;
			irdToCcr_t4 <= 1'sb0;
		end
		else if (enT4)
			irdToCcr_t4 <= Irdecod[38];
		else if (enT3) begin
			if (Nanod[97])
				Tpend <= pswT;
			else if (Nanod[96])
				Tpend <= 1'b0;
			if (Nanod[88] & !irdToCcr_t4)
				{pswT, pswS, pswI} <= {ftu[15], ftu[13], ftu[10:8]};
			else begin
				if (Nanod[82]) begin
					pswS <= 1'b1;
					pswT <= 1'b0;
				end
				if (Nanod[89])
					pswI <= inl;
			end
		end
	reg [4:0] ssw;
	reg [3:0] tvnLatch;
	reg [15:0] tvnMux;
	reg inExcept01;
	always @(posedge Clks[4]) begin
		if (Nanod[61] & enT3)
			ssw <= {~bciWrite, inExcept01, rFC};
		if (enT1 & Nanod[81]) begin
			tvnLatch <= tvn;
			inExcept01 <= tvn != 1;
		end
		if (Clks[2])
			ftu <= 1'sb0;
		else if (enT3)
			(* full_case, parallel_case *)
			case (1'b1)
				Nanod[95]: ftu <= tvnMux;
				Nanod[87]: ftu <= {pswT, 1'b0, pswS, 2'b00, pswI, 3'b000, ccr[4:0]};
				Nanod[84]: ftu <= Ird;
				Nanod[83]: ftu[4:0] <= ssw;
				Nanod[85]: ftu <= {12'hfff, pswI, 1'b0};
				Nanod[94]: ftu <= Irdecod[22-:16];
				Nanod[91]: ftu <= Abl;
				default: ftu <= ftu;
			endcase
	end
	localparam TVN_AUTOVEC = 13;
	localparam TVN_INTERRUPT = 15;
	localparam TVN_SPURIOUS = 12;
	always @(*) begin
		if (_sv2v_0)
			;
		if (inExcept01) begin
			if (tvnLatch == TVN_SPURIOUS)
				tvnMux = 16'h0060;
			else if (tvnLatch == TVN_AUTOVEC)
				tvnMux = {11'b00000000011, pswI, 2'b00};
			else if (tvnLatch == TVN_INTERRUPT)
				tvnMux = {6'b000000, Ird[7:0], 2'b00};
			else
				tvnMux = {10'b0000000000, tvnLatch, 2'b00};
		end
		else
			tvnMux = {8'h00, Irdecod[6-:6], 2'b00};
	end
	initial _sv2v_0 = 0;
endmodule
module nDecoder3 (
	Clks,
	Irdecod,
	Nanod,
	enT2,
	enT4,
	microLatch,
	nanoLatch
);
	input wire [4:0] Clks;
	input wire [41:0] Irdecod;
	output reg [104:0] Nanod;
	input enT2;
	input enT4;
	localparam UROM_WIDTH = 17;
	input [16:0] microLatch;
	localparam NANO_WIDTH = 68;
	input [67:0] nanoLatch;
	localparam NANO_IR2IRD = 67;
	localparam NANO_TOIRC = 66;
	localparam NANO_ALU_COL = 63;
	localparam NANO_ALU_FI = 61;
	localparam NANO_TODBIN = 60;
	localparam NANO_ALUE = 57;
	localparam NANO_DCR = 57;
	localparam NANO_DOBCTRL_1 = 56;
	localparam NANO_LOWBYTE = 55;
	localparam NANO_HIGHBYTE = 54;
	localparam NANO_DOBCTRL_0 = 53;
	localparam NANO_ALU_DCTRL = 51;
	localparam NANO_ALU_ACTRL = 50;
	localparam NANO_DBD2ALUB = 49;
	localparam NANO_ABD2ALUB = 48;
	localparam NANO_DBIN2DBD = 47;
	localparam NANO_DBIN2ABD = 46;
	localparam NANO_ALU2ABD = 45;
	localparam NANO_ALU2DBD = 44;
	localparam NANO_RZ = 43;
	localparam NANO_BUSBYTE = 42;
	localparam NANO_PCLABL = 41;
	localparam NANO_RXL_DBL = 40;
	localparam NANO_PCLDBL = 39;
	localparam NANO_ABDHRECHARGE = 38;
	localparam NANO_REG2ABL = 37;
	localparam NANO_ABL2REG = 36;
	localparam NANO_ABLABD = 35;
	localparam NANO_DBLDBD = 34;
	localparam NANO_DBL2REG = 33;
	localparam NANO_REG2DBL = 32;
	localparam NANO_ATLCTRL = 29;
	localparam NANO_FTUCONTROL = 25;
	localparam NANO_SSP = 24;
	localparam NANO_RXH_DBH = 22;
	localparam NANO_AUOUT = 20;
	localparam NANO_AUCLKEN = 19;
	localparam NANO_AUCTRL = 16;
	localparam NANO_DBLDBH = 15;
	localparam NANO_ABLABH = 14;
	localparam NANO_EXT_ABH = 13;
	localparam NANO_EXT_DBH = 12;
	localparam NANO_ATHCTRL = 9;
	localparam NANO_REG2ABH = 8;
	localparam NANO_ABH2REG = 7;
	localparam NANO_REG2DBH = 6;
	localparam NANO_DBH2REG = 5;
	localparam NANO_AOBCTRL = 3;
	localparam NANO_PCH = 0;
	localparam NANO_NO_SP_ALGN = 0;
	localparam NANO_FTU_UPDTPEND = 1;
	localparam NANO_FTU_INIT_ST = 15;
	localparam NANO_FTU_CLRTPEND = 14;
	localparam NANO_FTU_TVN = 13;
	localparam NANO_FTU_ABL2PREN = 12;
	localparam NANO_FTU_SSW = 11;
	localparam NANO_FTU_RSTPREN = 10;
	localparam NANO_FTU_IRD = 9;
	localparam NANO_FTU_2ABL = 8;
	localparam NANO_FTU_RDSR = 7;
	localparam NANO_FTU_INL = 6;
	localparam NANO_FTU_PSWI = 5;
	localparam NANO_FTU_DBL = 4;
	localparam NANO_FTU_2SR = 2;
	localparam NANO_FTU_CONST = 1;
	reg [3:0] ftuCtrl;
	wire [2:0] athCtrl;
	wire [2:0] atlCtrl;
	assign athCtrl = nanoLatch[11:NANO_ATHCTRL];
	assign atlCtrl = nanoLatch[31:NANO_ATLCTRL];
	wire [1:0] aobCtrl = nanoLatch[4:NANO_AOBCTRL];
	wire [1:0] dobCtrl = {nanoLatch[NANO_DOBCTRL_1], nanoLatch[NANO_DOBCTRL_0]};
	always @(posedge Clks[4])
		if (enT4) begin
			ftuCtrl <= {nanoLatch[25], nanoLatch[26], nanoLatch[27], nanoLatch[28]};
			Nanod[80] <= !nanoLatch[NANO_AUCLKEN];
			Nanod[78-:3] <= nanoLatch[18:16];
			Nanod[79] <= nanoLatch[1:NANO_NO_SP_ALGN] == 2'b11;
			Nanod[6] <= nanoLatch[NANO_EXT_DBH];
			Nanod[5] <= nanoLatch[NANO_EXT_ABH];
			Nanod[75] <= nanoLatch[NANO_TODBIN];
			Nanod[74] <= nanoLatch[NANO_TOIRC];
			Nanod[4] <= nanoLatch[NANO_ABLABD];
			Nanod[3] <= nanoLatch[NANO_ABLABH];
			Nanod[2] <= nanoLatch[NANO_DBLDBD];
			Nanod[1] <= nanoLatch[NANO_DBLDBH];
			Nanod[73] <= atlCtrl == 3'b010;
			Nanod[70] <= atlCtrl == 3'b011;
			Nanod[72] <= atlCtrl == 3'b100;
			Nanod[71] <= atlCtrl == 3'b101;
			Nanod[62] <= athCtrl == 3'b101;
			Nanod[69] <= (athCtrl == 3'b001) | (athCtrl == 3'b101);
			Nanod[68] <= athCtrl == 3'b100;
			Nanod[67] <= athCtrl == 3'b110;
			Nanod[66] <= athCtrl == 3'b011;
			Nanod[13] <= nanoLatch[NANO_ALU2DBD];
			Nanod[12] <= nanoLatch[NANO_ALU2ABD];
			Nanod[19] <= nanoLatch[58:NANO_DCR] == 2'b11;
			Nanod[18] <= nanoLatch[59:58] == 2'b11;
			Nanod[17] <= nanoLatch[59:58] == 2'b10;
			Nanod[16] <= nanoLatch[58:NANO_ALUE] == 2'b01;
			Nanod[15] <= nanoLatch[NANO_DBD2ALUB];
			Nanod[14] <= nanoLatch[NANO_ABD2ALUB];
			Nanod[60-:2] <= dobCtrl;
		end
	wire [1:1] sv2v_tmp_747F9;
	assign sv2v_tmp_747F9 = Nanod[62];
	always @(*) Nanod[61] = sv2v_tmp_747F9;
	wire [1:1] sv2v_tmp_04070;
	assign sv2v_tmp_04070 = ftuCtrl == NANO_FTU_UPDTPEND;
	always @(*) Nanod[97] = sv2v_tmp_04070;
	wire [1:1] sv2v_tmp_0B1AE;
	assign sv2v_tmp_0B1AE = ftuCtrl == NANO_FTU_CLRTPEND;
	always @(*) Nanod[96] = sv2v_tmp_0B1AE;
	wire [1:1] sv2v_tmp_1A1EB;
	assign sv2v_tmp_1A1EB = ftuCtrl == NANO_FTU_TVN;
	always @(*) Nanod[95] = sv2v_tmp_1A1EB;
	wire [1:1] sv2v_tmp_530E0;
	assign sv2v_tmp_530E0 = ftuCtrl == NANO_FTU_CONST;
	always @(*) Nanod[94] = sv2v_tmp_530E0;
	wire [1:1] sv2v_tmp_D720A;
	assign sv2v_tmp_D720A = (ftuCtrl == NANO_FTU_DBL) | (ftuCtrl == NANO_FTU_INL);
	always @(*) Nanod[93] = sv2v_tmp_D720A;
	wire [1:1] sv2v_tmp_F34AF;
	assign sv2v_tmp_F34AF = ftuCtrl == NANO_FTU_2ABL;
	always @(*) Nanod[92] = sv2v_tmp_F34AF;
	wire [1:1] sv2v_tmp_D37EE;
	assign sv2v_tmp_D37EE = ftuCtrl == NANO_FTU_INL;
	always @(*) Nanod[89] = sv2v_tmp_D37EE;
	wire [1:1] sv2v_tmp_3961F;
	assign sv2v_tmp_3961F = ftuCtrl == NANO_FTU_PSWI;
	always @(*) Nanod[85] = sv2v_tmp_3961F;
	wire [1:1] sv2v_tmp_5924E;
	assign sv2v_tmp_5924E = ftuCtrl == NANO_FTU_2SR;
	always @(*) Nanod[88] = sv2v_tmp_5924E;
	wire [1:1] sv2v_tmp_E1571;
	assign sv2v_tmp_E1571 = ftuCtrl == NANO_FTU_RDSR;
	always @(*) Nanod[87] = sv2v_tmp_E1571;
	wire [1:1] sv2v_tmp_0EBEF;
	assign sv2v_tmp_0EBEF = ftuCtrl == NANO_FTU_IRD;
	always @(*) Nanod[84] = sv2v_tmp_0EBEF;
	wire [1:1] sv2v_tmp_F6F07;
	assign sv2v_tmp_F6F07 = ftuCtrl == NANO_FTU_SSW;
	always @(*) Nanod[83] = sv2v_tmp_F6F07;
	wire [1:1] sv2v_tmp_ACA7A;
	assign sv2v_tmp_ACA7A = ((ftuCtrl == NANO_FTU_INL) | (ftuCtrl == NANO_FTU_CLRTPEND)) | (ftuCtrl == NANO_FTU_INIT_ST);
	always @(*) Nanod[82] = sv2v_tmp_ACA7A;
	wire [1:1] sv2v_tmp_E19FE;
	assign sv2v_tmp_E19FE = ftuCtrl == NANO_FTU_ABL2PREN;
	always @(*) Nanod[91] = sv2v_tmp_E19FE;
	wire [1:1] sv2v_tmp_567CC;
	assign sv2v_tmp_567CC = ftuCtrl == NANO_FTU_RSTPREN;
	always @(*) Nanod[90] = sv2v_tmp_567CC;
	wire [1:1] sv2v_tmp_E9496;
	assign sv2v_tmp_E9496 = nanoLatch[NANO_IR2IRD];
	always @(*) Nanod[81] = sv2v_tmp_E9496;
	wire [2:1] sv2v_tmp_3117C;
	assign sv2v_tmp_3117C = nanoLatch[52:NANO_ALU_DCTRL];
	always @(*) Nanod[24-:2] = sv2v_tmp_3117C;
	wire [1:1] sv2v_tmp_E767E;
	assign sv2v_tmp_E767E = nanoLatch[NANO_ALU_ACTRL];
	always @(*) Nanod[22] = sv2v_tmp_E767E;
	wire [3:1] sv2v_tmp_56582;
	assign sv2v_tmp_56582 = {nanoLatch[NANO_ALU_COL], nanoLatch[64], nanoLatch[65]};
	always @(*) Nanod[27-:3] = sv2v_tmp_56582;
	wire [1:0] aluFinInit = nanoLatch[62:NANO_ALU_FI];
	wire [1:1] sv2v_tmp_D8ECF;
	assign sv2v_tmp_D8ECF = aluFinInit == 2'b10;
	always @(*) Nanod[20] = sv2v_tmp_D8ECF;
	wire [1:1] sv2v_tmp_10612;
	assign sv2v_tmp_10612 = aluFinInit == 2'b01;
	always @(*) Nanod[21] = sv2v_tmp_10612;
	wire [1:1] sv2v_tmp_1422B;
	assign sv2v_tmp_1422B = aluFinInit == 2'b11;
	always @(*) Nanod[86] = sv2v_tmp_1422B;
	wire [1:1] sv2v_tmp_40621;
	assign sv2v_tmp_40621 = nanoLatch[NANO_ABDHRECHARGE];
	always @(*) Nanod[0] = sv2v_tmp_40621;
	wire [1:1] sv2v_tmp_11986;
	assign sv2v_tmp_11986 = nanoLatch[21:NANO_AUOUT] == 2'b01;
	always @(*) Nanod[11] = sv2v_tmp_11986;
	wire [1:1] sv2v_tmp_87A4B;
	assign sv2v_tmp_87A4B = nanoLatch[21:NANO_AUOUT] == 2'b10;
	always @(*) Nanod[10] = sv2v_tmp_87A4B;
	wire [1:1] sv2v_tmp_0185C;
	assign sv2v_tmp_0185C = nanoLatch[21:NANO_AUOUT] == 2'b11;
	always @(*) Nanod[9] = sv2v_tmp_0185C;
	wire [1:1] sv2v_tmp_51E64;
	assign sv2v_tmp_51E64 = aobCtrl == 2'b10;
	always @(*) Nanod[65] = sv2v_tmp_51E64;
	wire [1:1] sv2v_tmp_3A15B;
	assign sv2v_tmp_3A15B = aobCtrl == 2'b01;
	always @(*) Nanod[64] = sv2v_tmp_3A15B;
	wire [1:1] sv2v_tmp_1E9C6;
	assign sv2v_tmp_1E9C6 = aobCtrl == 2'b11;
	always @(*) Nanod[63] = sv2v_tmp_1E9C6;
	wire [1:1] sv2v_tmp_A10B9;
	assign sv2v_tmp_A10B9 = nanoLatch[NANO_DBIN2ABD];
	always @(*) Nanod[8] = sv2v_tmp_A10B9;
	wire [1:1] sv2v_tmp_E3EEB;
	assign sv2v_tmp_E3EEB = nanoLatch[NANO_DBIN2DBD];
	always @(*) Nanod[7] = sv2v_tmp_E3EEB;
	wire [1:1] sv2v_tmp_E660A;
	assign sv2v_tmp_E660A = |aobCtrl;
	always @(*) Nanod[104] = sv2v_tmp_E660A;
	wire [1:1] sv2v_tmp_05508;
	assign sv2v_tmp_05508 = |dobCtrl;
	always @(*) Nanod[102] = sv2v_tmp_05508;
	wire [1:1] sv2v_tmp_FD641;
	assign sv2v_tmp_FD641 = (nanoLatch[NANO_TOIRC] | nanoLatch[NANO_TODBIN]) | Nanod[102];
	always @(*) Nanod[103] = sv2v_tmp_FD641;
	wire [1:1] sv2v_tmp_0A255;
	assign sv2v_tmp_0A255 = nanoLatch[NANO_BUSBYTE];
	always @(*) Nanod[101] = sv2v_tmp_0A255;
	wire [1:1] sv2v_tmp_28AEA;
	assign sv2v_tmp_28AEA = nanoLatch[NANO_LOWBYTE];
	always @(*) Nanod[99] = sv2v_tmp_28AEA;
	wire [1:1] sv2v_tmp_F8B80;
	assign sv2v_tmp_F8B80 = nanoLatch[NANO_HIGHBYTE];
	always @(*) Nanod[98] = sv2v_tmp_F8B80;
	wire [1:1] sv2v_tmp_5A0C4;
	assign sv2v_tmp_5A0C4 = nanoLatch[NANO_ABL2REG];
	always @(*) Nanod[57] = sv2v_tmp_5A0C4;
	wire [1:1] sv2v_tmp_4E723;
	assign sv2v_tmp_4E723 = nanoLatch[NANO_ABH2REG];
	always @(*) Nanod[58] = sv2v_tmp_4E723;
	wire [1:1] sv2v_tmp_15A44;
	assign sv2v_tmp_15A44 = nanoLatch[NANO_DBL2REG];
	always @(*) Nanod[53] = sv2v_tmp_15A44;
	wire [1:1] sv2v_tmp_2968D;
	assign sv2v_tmp_2968D = nanoLatch[NANO_DBH2REG];
	always @(*) Nanod[54] = sv2v_tmp_2968D;
	wire [1:1] sv2v_tmp_EF529;
	assign sv2v_tmp_EF529 = nanoLatch[NANO_REG2DBL];
	always @(*) Nanod[52] = sv2v_tmp_EF529;
	wire [1:1] sv2v_tmp_FDE24;
	assign sv2v_tmp_FDE24 = nanoLatch[NANO_REG2DBH];
	always @(*) Nanod[51] = sv2v_tmp_FDE24;
	wire [1:1] sv2v_tmp_9299E;
	assign sv2v_tmp_9299E = nanoLatch[NANO_REG2ABL];
	always @(*) Nanod[56] = sv2v_tmp_9299E;
	wire [1:1] sv2v_tmp_997F3;
	assign sv2v_tmp_997F3 = nanoLatch[NANO_REG2ABH];
	always @(*) Nanod[55] = sv2v_tmp_997F3;
	wire [1:1] sv2v_tmp_AD37F;
	assign sv2v_tmp_AD37F = nanoLatch[NANO_SSP];
	always @(*) Nanod[50] = sv2v_tmp_AD37F;
	wire [1:1] sv2v_tmp_50BCA;
	assign sv2v_tmp_50BCA = nanoLatch[NANO_RZ];
	always @(*) Nanod[29] = sv2v_tmp_50BCA;
	wire dtldbd = 1'b0;
	wire dthdbh = 1'b0;
	wire dtlabd = 1'b0;
	wire dthabh = 1'b0;
	wire dblSpecial = Nanod[48] | dtldbd;
	wire dbhSpecial = Nanod[49] | dthdbh;
	wire ablSpecial = Nanod[47] | dtlabd;
	wire abhSpecial = Nanod[46] | dthabh;
	wire [1:1] sv2v_tmp_CBE41;
	assign sv2v_tmp_CBE41 = nanoLatch[NANO_RXL_DBL];
	always @(*) Nanod[28] = sv2v_tmp_CBE41;
	wire isPcRel = Irdecod[41] & !Nanod[29];
	wire pcRelDbl = isPcRel & !nanoLatch[NANO_RXL_DBL];
	wire pcRelDbh = isPcRel & !nanoLatch[NANO_RXH_DBH];
	wire pcRelAbl = isPcRel & nanoLatch[NANO_RXL_DBL];
	wire pcRelAbh = isPcRel & nanoLatch[NANO_RXH_DBH];
	wire [1:1] sv2v_tmp_C1AFC;
	assign sv2v_tmp_C1AFC = nanoLatch[NANO_PCLDBL] | pcRelDbl;
	always @(*) Nanod[48] = sv2v_tmp_C1AFC;
	wire [1:1] sv2v_tmp_F63C2;
	assign sv2v_tmp_F63C2 = (nanoLatch[1:NANO_PCH] == 2'b01) | pcRelDbh;
	always @(*) Nanod[49] = sv2v_tmp_F63C2;
	wire [1:1] sv2v_tmp_3DD4A;
	assign sv2v_tmp_3DD4A = nanoLatch[NANO_PCLABL] | pcRelAbl;
	always @(*) Nanod[47] = sv2v_tmp_3DD4A;
	wire [1:1] sv2v_tmp_7CBE9;
	assign sv2v_tmp_7CBE9 = (nanoLatch[1:NANO_PCH] == 2'b10) | pcRelAbh;
	always @(*) Nanod[46] = sv2v_tmp_7CBE9;
	always @(posedge Clks[4]) begin
		if (enT4) begin
			Nanod[41] <= (Nanod[52] & !dblSpecial) & nanoLatch[NANO_RXL_DBL];
			Nanod[40] <= (Nanod[56] & !ablSpecial) & !nanoLatch[NANO_RXL_DBL];
			Nanod[43] <= (Nanod[53] & !dblSpecial) & nanoLatch[NANO_RXL_DBL];
			Nanod[39] <= (Nanod[57] & !ablSpecial) & !nanoLatch[NANO_RXL_DBL];
			Nanod[45] <= (Nanod[51] & !dbhSpecial) & nanoLatch[NANO_RXH_DBH];
			Nanod[44] <= (Nanod[55] & !abhSpecial) & !nanoLatch[NANO_RXH_DBH];
			Nanod[42] <= (Nanod[54] & !dbhSpecial) & nanoLatch[NANO_RXH_DBH];
			Nanod[38] <= (Nanod[58] & !abhSpecial) & !nanoLatch[NANO_RXH_DBH];
			Nanod[37] <= (Nanod[54] & !dbhSpecial) & !nanoLatch[NANO_RXH_DBH];
			Nanod[36] <= (Nanod[58] & !abhSpecial) & nanoLatch[NANO_RXH_DBH];
			Nanod[31] <= (Nanod[53] & !dblSpecial) & !nanoLatch[NANO_RXL_DBL];
			Nanod[30] <= (Nanod[57] & !ablSpecial) & nanoLatch[NANO_RXL_DBL];
			Nanod[35] <= (Nanod[52] & !dblSpecial) & !nanoLatch[NANO_RXL_DBL];
			Nanod[34] <= (Nanod[56] & !ablSpecial) & nanoLatch[NANO_RXL_DBL];
			Nanod[33] <= (Nanod[51] & !dbhSpecial) & !nanoLatch[NANO_RXH_DBH];
			Nanod[32] <= (Nanod[55] & !abhSpecial) & nanoLatch[NANO_RXH_DBH];
		end
		if (enT4)
			Nanod[100] <= Irdecod[40] & nanoLatch[NANO_BUSBYTE];
	end
endmodule
module irdDecode (
	ird,
	Irdecod
);
	reg _sv2v_0;
	input [15:0] ird;
	output reg [41:0] Irdecod;
	wire [3:0] line = ird[15:12];
	wire [15:0] lineOnehot;
	onehotEncoder4 irdLines(
		.bin(line),
		.bitMap(lineOnehot)
	);
	wire isRegShift = lineOnehot['he] & (ird[7:6] != 2'b11);
	wire isDynShift = isRegShift & ird[5];
	wire [1:1] sv2v_tmp_8A32C;
	assign sv2v_tmp_8A32C = ((&ird[5:3] & ~isDynShift) & !ird[2]) & ird[1];
	always @(*) Irdecod[41] = sv2v_tmp_8A32C;
	wire [1:1] sv2v_tmp_6B5A5;
	assign sv2v_tmp_6B5A5 = lineOnehot[4] & (ird[11:6] == 6'b101011);
	always @(*) Irdecod[40] = sv2v_tmp_6B5A5;
	wire [3:1] sv2v_tmp_3A1E9;
	assign sv2v_tmp_3A1E9 = ird[11:9];
	always @(*) Irdecod[30-:3] = sv2v_tmp_3A1E9;
	wire [3:1] sv2v_tmp_E6B0F;
	assign sv2v_tmp_E6B0F = ird[2:0];
	always @(*) Irdecod[27-:3] = sv2v_tmp_E6B0F;
	wire isPreDecr = ird[5:3] == 3'b100;
	wire eaAreg = ird[5:3] == 3'b001;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (1'b1)
			lineOnehot[1], lineOnehot[2], lineOnehot[3]: Irdecod[24] = |ird[8:6];
			lineOnehot[4]: Irdecod[24] = &ird[8:6];
			lineOnehot['h8]: Irdecod[24] = (eaAreg & ird[8]) & ~ird[7];
			lineOnehot['hc]: Irdecod[24] = (eaAreg & ird[8]) & ~ird[7];
			lineOnehot['h9], lineOnehot['hb], lineOnehot['hd]: Irdecod[24] = (ird[7] & ird[6]) | ((eaAreg & ird[8]) & (ird[7:6] != 2'b11));
			default: Irdecod[24] = Irdecod[39];
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		Irdecod[34] = (lineOnehot[4] & ~ird[8]) & ~Irdecod[39];
	end
	wire [1:1] sv2v_tmp_83E94;
	assign sv2v_tmp_83E94 = Irdecod[34] & isPreDecr;
	always @(*) Irdecod[33] = sv2v_tmp_83E94;
	wire [1:1] sv2v_tmp_0D1C9;
	assign sv2v_tmp_0D1C9 = lineOnehot[5] | (lineOnehot[0] & ~ird[8]);
	always @(*) Irdecod[37] = sv2v_tmp_0D1C9;
	wire [1:1] sv2v_tmp_4B7C1;
	assign sv2v_tmp_4B7C1 = lineOnehot[4] & (ird[11:4] == 8'he6);
	always @(*) Irdecod[35] = sv2v_tmp_4B7C1;
	wire eaImmOrAbs = (ird[5:3] == 3'b111) & ~ird[1];
	wire [1:1] sv2v_tmp_BEF0B;
	assign sv2v_tmp_BEF0B = eaImmOrAbs & ~isRegShift;
	always @(*) Irdecod[36] = sv2v_tmp_BEF0B;
	always @(*) begin : sv2v_autoblock_1
		reg eaIsAreg;
		if (_sv2v_0)
			;
		eaIsAreg = (ird[5:3] != 3'b000) & (ird[5:3] != 3'b111);
		(* full_case, parallel_case *)
		case (1'b1)
			default: Irdecod[23] = eaIsAreg;
			lineOnehot[5]: Irdecod[23] = eaIsAreg & (ird[7:3] != 5'b11001);
			lineOnehot[6], lineOnehot[7]: Irdecod[23] = 1'b0;
			lineOnehot['he]: Irdecod[23] = ~isRegShift;
		endcase
	end
	wire xIsScc = (ird[7:6] == 2'b11) & (ird[5:3] != 3'b001);
	wire xStaticMem = (ird[11:8] == 4'b1000) & (ird[5:4] == 2'b00);
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (1'b1)
			lineOnehot[0]: Irdecod[32] = (((ird[8] & (ird[5:4] != 2'b00)) | ((ird[11:8] == 4'b1000) & (ird[5:4] != 2'b00))) | ((ird[8:7] == 2'b10) & (ird[5:3] == 3'b001))) | ((ird[8:6] == 3'b000) & !xStaticMem);
			lineOnehot[1]: Irdecod[32] = 1'b1;
			lineOnehot[4]: Irdecod[32] = (ird[7:6] == 2'b00) | Irdecod[40];
			lineOnehot[5]: Irdecod[32] = (ird[7:6] == 2'b00) | xIsScc;
			lineOnehot[8], lineOnehot[9], lineOnehot['hb], lineOnehot['hc], lineOnehot['hd], lineOnehot['he]: Irdecod[32] = ird[7:6] == 2'b00;
			default: Irdecod[32] = 1'b0;
		endcase
	end
	wire [1:1] sv2v_tmp_93976;
	assign sv2v_tmp_93976 = (lineOnehot[0] & ird[8]) & eaAreg;
	always @(*) Irdecod[31] = sv2v_tmp_93976;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (1'b1)
			lineOnehot[6]: Irdecod[39] = ird[11:8] == 4'b0001;
			lineOnehot[4]: Irdecod[39] = (ird[11:8] == 4'b1110) | (ird[11:6] == 6'b100001);
			default: Irdecod[39] = 1'b0;
		endcase
	end
	wire [1:1] sv2v_tmp_90692;
	assign sv2v_tmp_90692 = (lineOnehot[4] & ((ird[11:0] == 12'he77) | (ird[11:6] == 6'b010011))) | (lineOnehot[0] & (ird[8:6] == 3'b000));
	always @(*) Irdecod[38] = sv2v_tmp_90692;
	reg [15:0] ftuConst;
	wire [3:0] zero28 = (ird[11:9] == 0 ? 4'h8 : {1'b0, ird[11:9]});
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (1'b1)
			lineOnehot[6], lineOnehot[7]: ftuConst = {{8 {ird[7]}}, ird[7:0]};
			lineOnehot['h5], lineOnehot['he]: ftuConst = {12'b000000000000, zero28};
			lineOnehot['h8], lineOnehot['hc]: ftuConst = 16'h000f;
			lineOnehot[4]: ftuConst = 16'h0080;
			default: ftuConst = 1'sb0;
		endcase
	end
	wire [16:1] sv2v_tmp_5A76A;
	assign sv2v_tmp_5A76A = ftuConst;
	always @(*) Irdecod[22-:16] = sv2v_tmp_5A76A;
	always @(*) begin
		if (_sv2v_0)
			;
		if (lineOnehot[4])
			case (ird[6:5])
				2'b00, 2'b01: Irdecod[6-:6] = 6;
				2'b11: Irdecod[6-:6] = 7;
				2'b10: Irdecod[6-:6] = {2'b10, ird[3:0]};
			endcase
		else
			Irdecod[6-:6] = 5;
	end
	wire eaAdir = ird[5:3] == 3'b001;
	wire size11 = ird[7] & ird[6];
	wire [1:1] sv2v_tmp_C4C1A;
	assign sv2v_tmp_C4C1A = (((lineOnehot[9] | lineOnehot['hd]) & size11) | (lineOnehot[5] & eaAdir)) | ((lineOnehot[2] | lineOnehot[3]) & (ird[8:6] == 3'b001));
	always @(*) Irdecod[0] = sv2v_tmp_C4C1A;
	initial _sv2v_0 = 0;
endmodule
module excUnit (
	Clks,
	enT1,
	enT2,
	enT3,
	enT4,
	Nanod,
	Irdecod,
	Ird,
	pswS,
	ftu,
	iEdb,
	ccr,
	alue,
	prenEmpty,
	au05z,
	dcr4,
	ze,
	aob0,
	AblOut,
	Irc,
	oEdb,
	eab
);
	reg _sv2v_0;
	input wire [4:0] Clks;
	input enT1;
	input enT2;
	input enT3;
	input enT4;
	input wire [104:0] Nanod;
	input wire [41:0] Irdecod;
	input [15:0] Ird;
	input pswS;
	input [15:0] ftu;
	input [15:0] iEdb;
	output wire [7:0] ccr;
	output wire [15:0] alue;
	output wire prenEmpty;
	output wire au05z;
	output reg dcr4;
	output wire ze;
	output wire aob0;
	output wire [15:0] AblOut;
	output wire [15:0] Irc;
	output wire [15:0] oEdb;
	output wire [23:1] eab;
	localparam REG_USP = 15;
	localparam REG_SSP = 16;
	localparam REG_DT = 17;
	reg [15:0] regs68L [0:17];
	reg [15:0] regs68H [0:17];
	initial begin : sv2v_autoblock_1
		reg signed [31:0] i;
		for (i = 0; i < 18; i = i + 1)
			begin
				regs68L[i] <= 1'sb0;
				regs68H[i] <= 1'sb0;
			end
	end
	wire [31:0] SSP = {regs68H[REG_SSP], regs68L[REG_SSP]};
	wire [15:0] aluOut;
	wire [15:0] dbin;
	reg [15:0] dcrOutput;
	reg [15:0] PcL;
	reg [15:0] PcH;
	reg [31:0] auReg;
	reg [31:0] aob;
	reg [15:0] Ath;
	reg [15:0] Atl;
	reg [15:0] Dbl;
	reg [15:0] Dbh;
	reg [15:0] Abh;
	reg [15:0] Abl;
	reg [15:0] Abd;
	reg [15:0] Dbd;
	assign AblOut = Abl;
	assign au05z = ~|auReg[5:0];
	reg [15:0] dblMux;
	reg [15:0] dbhMux;
	reg [15:0] abhMux;
	reg [15:0] ablMux;
	reg [15:0] abdMux;
	reg [15:0] dbdMux;
	reg abdIsByte;
	reg Pcl2Dbl;
	reg Pch2Dbh;
	reg Pcl2Abl;
	reg Pch2Abh;
	reg [4:0] actualRx;
	reg [4:0] actualRy;
	wire [15:0] regs68L_rx = regs68L[actualRx];
	wire [15:0] regs68L_ry = regs68L[actualRy];
	wire [15:0] regs68H_rx = regs68H[actualRx];
	wire [15:0] regs68H_ry = regs68H[actualRy];
	reg [3:0] movemRx;
	reg byteNotSpAlign;
	reg [4:0] rxMux;
	reg [4:0] ryMux;
	reg [3:0] rxReg;
	reg [3:0] ryReg;
	reg rxIsSp;
	reg ryIsSp;
	reg rxIsAreg;
	reg ryIsAreg;
	always @(*) begin
		if (_sv2v_0)
			;
		if (Nanod[50]) begin
			rxMux = REG_SSP;
			rxIsSp = 1'b1;
			rxReg = 1'bx;
		end
		else if (Irdecod[35]) begin
			rxMux = REG_USP;
			rxIsSp = 1'b1;
			rxReg = 1'bx;
		end
		else if (Irdecod[37] & !Irdecod[39]) begin
			rxMux = REG_DT;
			rxIsSp = 1'b0;
			rxReg = 1'bx;
		end
		else begin
			if (Irdecod[39])
				rxReg = 15;
			else if (Irdecod[34])
				rxReg = movemRx;
			else
				rxReg = {Irdecod[24], Irdecod[30-:3]};
			if (&rxReg) begin
				rxMux = (pswS ? REG_SSP : 15);
				rxIsSp = 1'b1;
			end
			else begin
				rxMux = {1'b0, rxReg};
				rxIsSp = 1'b0;
			end
		end
		if (Irdecod[36] & !Nanod[29]) begin
			ryMux = REG_DT;
			ryIsSp = 1'b0;
			ryReg = 1'sbx;
		end
		else begin
			ryReg = (Nanod[29] ? Irc[15:12] : {Irdecod[23], Irdecod[27-:3]});
			ryIsSp = &ryReg;
			if (ryIsSp & pswS)
				ryMux = REG_SSP;
			else
				ryMux = {1'b0, ryReg};
		end
	end
	always @(posedge Clks[4]) begin
		if (enT4) begin
			byteNotSpAlign <= Irdecod[32] & ~(Nanod[28] ? rxIsSp : ryIsSp);
			actualRx <= rxMux;
			actualRy <= ryMux;
			rxIsAreg <= rxIsSp | rxMux[3];
			ryIsAreg <= ryIsSp | ryMux[3];
		end
		if (enT4)
			abdIsByte <= Nanod[0] & Irdecod[32];
	end
	wire ryl2Abl = Nanod[34] & (ryIsAreg | Nanod[4]);
	wire ryl2Abd = Nanod[34] & (~ryIsAreg | Nanod[4]);
	wire ryl2Dbl = Nanod[35] & (ryIsAreg | Nanod[2]);
	wire ryl2Dbd = Nanod[35] & (~ryIsAreg | Nanod[2]);
	wire rxl2Abl = Nanod[40] & (rxIsAreg | Nanod[4]);
	wire rxl2Abd = Nanod[40] & (~rxIsAreg | Nanod[4]);
	wire rxl2Dbl = Nanod[41] & (rxIsAreg | Nanod[2]);
	wire rxl2Dbd = Nanod[41] & (~rxIsAreg | Nanod[2]);
	reg abhIdle;
	reg ablIdle;
	reg abdIdle;
	reg dbhIdle;
	reg dblIdle;
	reg dbdIdle;
	always @(*) begin
		if (_sv2v_0)
			;
		{abhIdle, ablIdle, abdIdle} = 1'sb0;
		{dbhIdle, dblIdle, dbdIdle} = 1'sb0;
		(* full_case, parallel_case *)
		case (1'b1)
			ryl2Dbd: dbdMux = regs68L_ry;
			rxl2Dbd: dbdMux = regs68L_rx;
			Nanod[16]: dbdMux = alue;
			Nanod[7]: dbdMux = dbin;
			Nanod[13]: dbdMux = aluOut;
			Nanod[18]: dbdMux = dcrOutput;
			default: begin
				dbdMux = 1'sbx;
				dbdIdle = 1'b1;
			end
		endcase
		(* full_case, parallel_case *)
		case (1'b1)
			rxl2Dbl: dblMux = regs68L_rx;
			ryl2Dbl: dblMux = regs68L_ry;
			Nanod[93]: dblMux = ftu;
			Nanod[11]: dblMux = auReg[15:0];
			Nanod[70]: dblMux = Atl;
			Pcl2Dbl: dblMux = PcL;
			default: begin
				dblMux = 1'sbx;
				dblIdle = 1'b1;
			end
		endcase
		(* full_case, parallel_case *)
		case (1'b1)
			Nanod[45]: dbhMux = regs68H_rx;
			Nanod[33]: dbhMux = regs68H_ry;
			Nanod[11]: dbhMux = auReg[31:16];
			Nanod[67]: dbhMux = Ath;
			Pch2Dbh: dbhMux = PcH;
			default: begin
				dbhMux = 1'sbx;
				dbhIdle = 1'b1;
			end
		endcase
		(* full_case, parallel_case *)
		case (1'b1)
			ryl2Abd: abdMux = regs68L_ry;
			rxl2Abd: abdMux = regs68L_rx;
			Nanod[8]: abdMux = dbin;
			Nanod[12]: abdMux = aluOut;
			default: begin
				abdMux = 1'sbx;
				abdIdle = 1'b1;
			end
		endcase
		(* full_case, parallel_case *)
		case (1'b1)
			Pcl2Abl: ablMux = PcL;
			rxl2Abl: ablMux = regs68L_rx;
			ryl2Abl: ablMux = regs68L_ry;
			Nanod[92]: ablMux = ftu;
			Nanod[10]: ablMux = auReg[15:0];
			Nanod[62]: ablMux = aob[15:0];
			Nanod[71]: ablMux = Atl;
			default: begin
				ablMux = 1'sbx;
				ablIdle = 1'b1;
			end
		endcase
		(* full_case, parallel_case *)
		case (1'b1)
			Pch2Abh: abhMux = PcH;
			Nanod[44]: abhMux = regs68H_rx;
			Nanod[32]: abhMux = regs68H_ry;
			Nanod[10]: abhMux = auReg[31:16];
			Nanod[62]: abhMux = aob[31:16];
			Nanod[66]: abhMux = Ath;
			default: begin
				abhMux = 1'sbx;
				abhIdle = 1'b1;
			end
		endcase
	end
	reg [15:0] preAbh;
	reg [15:0] preAbl;
	reg [15:0] preAbd;
	reg [15:0] preDbh;
	reg [15:0] preDbl;
	reg [15:0] preDbd;
	always @(posedge Clks[4]) begin
		if (enT1) begin
			{preAbh, preAbl, preAbd} <= {abhMux, ablMux, abdMux};
			{preDbh, preDbl, preDbd} <= {dbhMux, dblMux, dbdMux};
		end
		if (enT2) begin
			if (Nanod[5])
				Abh <= {16 {(ablIdle ? preAbd[15] : preAbl[15])}};
			else if (abhIdle)
				Abh <= (ablIdle ? preAbd : preAbl);
			else
				Abh <= preAbh;
			if (~ablIdle)
				Abl <= preAbl;
			else
				Abl <= (Nanod[3] ? preAbh : preAbd);
			Abd <= (~abdIdle ? preAbd : (ablIdle ? preAbh : preAbl));
			if (Nanod[6])
				Dbh <= {16 {(dblIdle ? preDbd[15] : preDbl[15])}};
			else if (dbhIdle)
				Dbh <= (dblIdle ? preDbd : preDbl);
			else
				Dbh <= preDbh;
			if (~dblIdle)
				Dbl <= preDbl;
			else
				Dbl <= (Nanod[1] ? preDbh : preDbd);
			Dbd <= (~dbdIdle ? preDbd : (dblIdle ? preDbh : preDbl));
		end
	end
	wire au2Aob = Nanod[63] | (Nanod[11] & Nanod[65]);
	always @(posedge Clks[4])
		if (enT1 & au2Aob)
			aob <= auReg;
		else if (enT2) begin
			if (Nanod[65])
				aob <= {preDbh, (~dblIdle ? preDbl : preDbd)};
			else if (Nanod[64])
				aob <= {preAbh, (~ablIdle ? preAbl : preAbd)};
		end
	assign eab = aob[23:1];
	assign aob0 = aob[0];
	reg [31:0] auInpMux;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (Nanod[78-:3])
			3'b000: auInpMux = 0;
			3'b001: auInpMux = (byteNotSpAlign | Nanod[79] ? 1 : 2);
			3'b010: auInpMux = -4;
			3'b011: auInpMux = {Abh, Abl};
			3'b100: auInpMux = 2;
			3'b101: auInpMux = 4;
			3'b110: auInpMux = -2;
			3'b111: auInpMux = (byteNotSpAlign | Nanod[79] ? -1 : -2);
			default: auInpMux = 1'sbx;
		endcase
	end
	wire [16:0] aulow = Dbl + auInpMux[15:0];
	wire [31:0] auResult = {(Dbh + auInpMux[31:16]) + aulow[16], aulow[15:0]};
	always @(posedge Clks[4])
		if (Clks[2])
			auReg <= 1'sb0;
		else if (enT3 & Nanod[80])
			auReg <= auResult;
	always @(posedge Clks[4])
		if (enT3) begin
			if (Nanod[43] | Nanod[39]) begin
				if (~rxIsAreg) begin
					if (Nanod[43])
						regs68L[actualRx] <= Dbd;
					else if (abdIsByte)
						regs68L[actualRx][7:0] <= Abd[7:0];
					else
						regs68L[actualRx] <= Abd;
				end
				else
					regs68L[actualRx] <= (Nanod[43] ? Dbl : Abl);
			end
			if (Nanod[31] | Nanod[30]) begin
				if (~ryIsAreg) begin
					if (Nanod[31])
						regs68L[actualRy] <= Dbd;
					else if (abdIsByte)
						regs68L[actualRy][7:0] <= Abd[7:0];
					else
						regs68L[actualRy] <= Abd;
				end
				else
					regs68L[actualRy] <= (Nanod[31] ? Dbl : Abl);
			end
			if (Nanod[42] | Nanod[38])
				regs68H[actualRx] <= (Nanod[42] ? Dbh : Abh);
			if (Nanod[37] | Nanod[36])
				regs68H[actualRy] <= (Nanod[37] ? Dbh : Abh);
		end
	reg dbl2Pcl;
	reg dbh2Pch;
	reg abh2Pch;
	reg abl2Pcl;
	always @(posedge Clks[4]) begin
		if (Clks[3]) begin
			{dbl2Pcl, dbh2Pch, abh2Pch, abl2Pcl} <= 1'sb0;
			Pcl2Dbl <= 1'b0;
			Pch2Dbh <= 1'b0;
			Pcl2Abl <= 1'b0;
			Pch2Abh <= 1'b0;
		end
		else if (enT4) begin
			dbl2Pcl <= Nanod[53] & Nanod[48];
			dbh2Pch <= Nanod[54] & Nanod[49];
			abh2Pch <= Nanod[58] & Nanod[46];
			abl2Pcl <= Nanod[57] & Nanod[47];
			Pcl2Dbl <= Nanod[52] & Nanod[48];
			Pch2Dbh <= Nanod[51] & Nanod[49];
			Pcl2Abl <= Nanod[56] & Nanod[47];
			Pch2Abh <= Nanod[55] & Nanod[46];
		end
		if (enT1 & Nanod[9])
			PcL <= auReg[15:0];
		else if (enT3) begin
			if (dbl2Pcl)
				PcL <= Dbl;
			else if (abl2Pcl)
				PcL <= Abl;
		end
		if (enT1 & Nanod[9])
			PcH <= auReg[31:16];
		else if (enT3) begin
			if (dbh2Pch)
				PcH <= Dbh;
			else if (abh2Pch)
				PcH <= Abh;
		end
		if (enT3) begin
			if (Nanod[73])
				Atl <= Dbl;
			else if (Nanod[72])
				Atl <= Abl;
		end
		if (enT3) begin
			if (Nanod[69])
				Ath <= Abh;
			else if (Nanod[68])
				Ath <= Dbh;
		end
	end
	wire rmIdle;
	wire [3:0] prHbit;
	reg [15:0] prenLatch;
	assign prenEmpty = ~|prenLatch;
	pren rmPren(
		.mask(prenLatch),
		.hbit(prHbit)
	);
	always @(posedge Clks[4])
		if (enT1 & Nanod[91])
			prenLatch <= dbin;
		else if (enT3 & Nanod[90]) begin
			prenLatch[prHbit] <= 1'b0;
			movemRx <= (Irdecod[33] ? ~prHbit : prHbit);
		end
	wire [15:0] dcrCode;
	wire [3:0] dcrInput = (abdIsByte ? {1'b0, Abd[2:0]} : Abd[3:0]);
	onehotEncoder4 dcrDecoder(
		.bin(dcrInput),
		.bitMap(dcrCode)
	);
	always @(posedge Clks[4])
		if (Clks[2])
			dcr4 <= 1'sb0;
		else if (enT3 & Nanod[19]) begin
			dcrOutput <= dcrCode;
			dcr4 <= Abd[4];
		end
	reg [15:0] alub;
	always @(posedge Clks[4])
		if (enT3) begin
			if (Nanod[15])
				alub <= Dbd;
			else if (Nanod[14])
				alub <= Abd;
		end
	wire alueClkEn = enT3 & Nanod[17];
	reg [15:0] dobInput;
	wire dobIdle = ~|Nanod[60-:2];
	localparam NANO_DOB_ADB = 2'b10;
	localparam NANO_DOB_ALU = 2'b11;
	localparam NANO_DOB_DBD = 2'b01;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (Nanod[60-:2])
			NANO_DOB_ADB: dobInput = Abd;
			NANO_DOB_DBD: dobInput = Dbd;
			NANO_DOB_ALU: dobInput = aluOut;
			default: dobInput = 1'sbx;
		endcase
	end
	dataIo dataIo(
		.Clks(Clks),
		.enT1(enT1),
		.enT2(enT2),
		.enT3(enT3),
		.enT4(enT4),
		.Nanod(Nanod),
		.Irdecod(Irdecod),
		.iEdb(iEdb),
		.dobIdle(dobIdle),
		.dobInput(dobInput),
		.aob0(aob0),
		.Irc(Irc),
		.dbin(dbin),
		.oEdb(oEdb)
	);
	fx68kAlu alu(
		.clk(Clks[4]),
		.pwrUp(Clks[2]),
		.enT1(enT1),
		.enT3(enT3),
		.enT4(enT4),
		.ird(Ird),
		.aluColumn(Nanod[27-:3]),
		.aluAddrCtrl(Nanod[22]),
		.init(Nanod[21]),
		.finish(Nanod[20]),
		.aluIsByte(Irdecod[32]),
		.ftu2Ccr(Nanod[86]),
		.alub(alub),
		.ftu(ftu),
		.alueClkEn(alueClkEn),
		.alue(alue),
		.aluDataCtrl(Nanod[24-:2]),
		.iDataBus(Dbd),
		.iAddrBus(Abd),
		.ze(ze),
		.aluOut(aluOut),
		.ccr(ccr)
	);
	initial _sv2v_0 = 0;
endmodule
module dataIo (
	Clks,
	enT1,
	enT2,
	enT3,
	enT4,
	Nanod,
	Irdecod,
	iEdb,
	aob0,
	dobIdle,
	dobInput,
	Irc,
	dbin,
	oEdb
);
	input wire [4:0] Clks;
	input enT1;
	input enT2;
	input enT3;
	input enT4;
	input wire [104:0] Nanod;
	input wire [41:0] Irdecod;
	input [15:0] iEdb;
	input aob0;
	input dobIdle;
	input [15:0] dobInput;
	output reg [15:0] Irc;
	output reg [15:0] dbin;
	output wire [15:0] oEdb;
	reg [15:0] dob;
	reg xToDbin;
	reg xToIrc;
	reg dbinNoLow;
	reg dbinNoHigh;
	reg byteMux;
	reg isByte_T4;
	always @(posedge Clks[4]) begin
		if (enT4)
			isByte_T4 <= Irdecod[32];
		if (enT3) begin
			dbinNoHigh <= Nanod[98];
			dbinNoLow <= Nanod[99];
			byteMux <= (Nanod[101] & isByte_T4) & ~aob0;
		end
		if (enT1) begin
			xToDbin <= 1'b0;
			xToIrc <= 1'b0;
		end
		else if (enT3) begin
			xToDbin <= Nanod[75];
			xToIrc <= Nanod[74];
		end
		if (xToIrc & Clks[0])
			Irc <= iEdb;
		if (xToDbin & Clks[0]) begin
			if (~dbinNoLow)
				dbin[7:0] <= (byteMux ? iEdb[15:8] : iEdb[7:0]);
			if (~dbinNoHigh)
				dbin[15:8] <= (~byteMux & dbinNoLow ? iEdb[7:0] : iEdb[15:8]);
		end
	end
	reg byteCycle;
	always @(posedge Clks[4]) begin
		if (enT4)
			byteCycle <= Nanod[101] & Irdecod[32];
		if (enT3 & ~dobIdle) begin
			dob[7:0] <= (Nanod[99] ? dobInput[15:8] : dobInput[7:0]);
			dob[15:8] <= (byteCycle | Nanod[98] ? dobInput[7:0] : dobInput[15:8]);
		end
	end
	assign oEdb = dob;
endmodule
module uaddrDecode (
	opcode,
	a1,
	a2,
	a3,
	isPriv,
	isIllegal,
	isLineA,
	isLineF,
	lineBmap
);
	reg _sv2v_0;
	input [15:0] opcode;
	localparam UADDR_WIDTH = 10;
	output wire [9:0] a1;
	output wire [9:0] a2;
	output wire [9:0] a3;
	output reg isPriv;
	output wire isIllegal;
	output wire isLineA;
	output wire isLineF;
	output wire [15:0] lineBmap;
	wire [3:0] line = opcode[15:12];
	wire [3:0] eaCol;
	wire [3:0] movEa;
	onehotEncoder4 irLineDecod(
		.bin(line),
		.bitMap(lineBmap)
	);
	assign isLineA = lineBmap['ha];
	assign isLineF = lineBmap['hf];
	pla_lined pla_lined(
		.movEa(movEa),
		.col(eaCol),
		.opcode(opcode),
		.lineBmap(lineBmap),
		.palIll(isIllegal),
		.plaA1(a1),
		.plaA2(a2),
		.plaA3(a3)
	);
	function [3:0] eaDecode;
		input [5:0] eaBits;
		(* full_case, parallel_case *)
		case (eaBits[5:3])
			3'b111:
				case (eaBits[2:0])
					3'b000: eaDecode = 7;
					3'b001: eaDecode = 8;
					3'b010: eaDecode = 9;
					3'b011: eaDecode = 10;
					3'b100: eaDecode = 11;
					default: eaDecode = 12;
				endcase
			default: eaDecode = eaBits[5:3];
		endcase
	endfunction
	assign eaCol = eaDecode(opcode[5:0]);
	assign movEa = eaDecode({opcode[8:6], opcode[11:9]});
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (lineBmap)
			'h1: isPriv = (opcode & 16'hf5ff) == 16'h007c;
			'h10:
				if ((opcode & 16'hffc0) == 16'h46c0)
					isPriv = 1'b1;
				else if ((opcode & 16'hfff0) == 16'h4e60)
					isPriv = 1'b1;
				else if (((opcode == 16'h4e70) || (opcode == 16'h4e73)) || (opcode == 16'h4e72))
					isPriv = 1'b1;
				else
					isPriv = 1'b0;
			default: isPriv = 1'b0;
		endcase
	end
	initial _sv2v_0 = 0;
endmodule
module onehotEncoder4 (
	bin,
	bitMap
);
	reg _sv2v_0;
	input [3:0] bin;
	output reg [15:0] bitMap;
	always @(*) begin
		if (_sv2v_0)
			;
		case (bin)
			'b0: bitMap = 16'h0001;
			'b1: bitMap = 16'h0002;
			'b10: bitMap = 16'h0004;
			'b11: bitMap = 16'h0008;
			'b100: bitMap = 16'h0010;
			'b101: bitMap = 16'h0020;
			'b110: bitMap = 16'h0040;
			'b111: bitMap = 16'h0080;
			'b1000: bitMap = 16'h0100;
			'b1001: bitMap = 16'h0200;
			'b1010: bitMap = 16'h0400;
			'b1011: bitMap = 16'h0800;
			'b1100: bitMap = 16'h1000;
			'b1101: bitMap = 16'h2000;
			'b1110: bitMap = 16'h4000;
			'b1111: bitMap = 16'h8000;
		endcase
	end
	initial _sv2v_0 = 0;
endmodule
module pren (
	mask,
	hbit
);
	parameter size = 16;
	parameter outbits = 4;
	input [size - 1:0] mask;
	output reg [outbits - 1:0] hbit;
	always @(mask) begin : sv2v_autoblock_1
		integer i;
		hbit = 0;
		for (i = size - 1; i >= 0; i = i - 1)
			if (mask[i])
				hbit = i;
	end
endmodule
module sequencer (
	Clks,
	enT3,
	microLatch,
	A0Err,
	BerrA,
	busAddrErr,
	Spuria,
	Avia,
	Tpend,
	intPend,
	isIllegal,
	isPriv,
	excRst,
	isLineA,
	isLineF,
	psw,
	prenEmpty,
	au05z,
	dcr4,
	ze,
	i11,
	alue01,
	Ird,
	a1,
	a2,
	a3,
	tvn,
	nma
);
	reg _sv2v_0;
	input wire [4:0] Clks;
	input enT3;
	localparam UROM_WIDTH = 17;
	input [16:0] microLatch;
	input A0Err;
	input BerrA;
	input busAddrErr;
	input Spuria;
	input Avia;
	input Tpend;
	input intPend;
	input isIllegal;
	input isPriv;
	input excRst;
	input isLineA;
	input isLineF;
	input [15:0] psw;
	input prenEmpty;
	input au05z;
	input dcr4;
	input ze;
	input i11;
	input [1:0] alue01;
	input [15:0] Ird;
	localparam UADDR_WIDTH = 10;
	input [9:0] a1;
	input [9:0] a2;
	input [9:0] a3;
	output reg [3:0] tvn;
	output reg [9:0] nma;
	reg [9:0] uNma;
	reg [9:0] grp1Nma;
	reg [1:0] c0c1;
	reg a0Rst;
	wire A0Sel;
	wire inGrp0Exc;
	wire [9:0] dbNma = {microLatch[14:13], microLatch[6:5], microLatch[10:7], microLatch[12:11]};
	localparam BSER1_NMA = 'h3;
	localparam HALT1_NMA = 'h1;
	localparam RSTP0_NMA = 'h2;
	always @(*) begin
		if (_sv2v_0)
			;
		if (A0Err) begin
			if (a0Rst)
				nma = RSTP0_NMA;
			else if (inGrp0Exc)
				nma = HALT1_NMA;
			else
				nma = BSER1_NMA;
		end
		else
			nma = uNma;
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if (microLatch[1])
			uNma = {microLatch[14:13], c0c1, microLatch[10:7], microLatch[12:11]};
		else
			case (microLatch[3:2])
				0: uNma = dbNma;
				1: uNma = (A0Sel ? grp1Nma : a1);
				2: uNma = a2;
				3: uNma = a3;
			endcase
	end
	wire [1:0] enl = {Ird[6], prenEmpty};
	wire [1:0] ms0 = {Ird[8], alue01[0]};
	wire [3:0] m01 = {au05z, Ird[8], alue01};
	localparam NF = 3;
	localparam ZF = 2;
	wire [1:0] nz1 = {psw[NF], psw[ZF]};
	localparam VF = 1;
	wire [1:0] nv = {psw[NF], psw[VF]};
	reg ccTest;
	wire [4:0] cbc = microLatch[6:2];
	localparam CF = 0;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (cbc)
			'h0: c0c1 = {i11, i11};
			'h1: c0c1 = (au05z ? 2'b01 : 2'b11);
			'h11: c0c1 = (au05z ? 2'b00 : 2'b11);
			'h2: c0c1 = {1'b0, ~psw[CF]};
			'h12: c0c1 = {1'b1, ~psw[CF]};
			'h3: c0c1 = {psw[ZF], psw[ZF]};
			'h4:
				case (nz1)
					'b0: c0c1 = 2'b10;
					'b10: c0c1 = 2'b01;
					'b1, 'b11: c0c1 = 2'b11;
				endcase
			'h5: c0c1 = {psw[NF], 1'b1};
			'h15: c0c1 = {1'b1, psw[NF]};
			'h6: c0c1 = {~nz1[1] & ~nz1[0], 1'b1};
			'h7:
				case (ms0)
					'b10, 'b0: c0c1 = 2'b11;
					'b1: c0c1 = 2'b01;
					'b11: c0c1 = 2'b10;
				endcase
			'h8:
				case (m01)
					'b0, 'b1, 'b100, 'b111: c0c1 = 2'b11;
					'b10, 'b11, 'b101: c0c1 = 2'b01;
					'b110: c0c1 = 2'b10;
					default: c0c1 = 2'b00;
				endcase
			'h9: c0c1 = (ccTest ? 2'b11 : 2'b01);
			'h19: c0c1 = (ccTest ? 2'b11 : 2'b10);
			'hc: c0c1 = (dcr4 ? 2'b01 : 2'b11);
			'h1c: c0c1 = (dcr4 ? 2'b10 : 2'b11);
			'ha: c0c1 = (ze ? 2'b11 : 2'b00);
			'hb: c0c1 = (nv == 2'b00 ? 2'b00 : 2'b11);
			'hd: c0c1 = {~psw[VF], ~psw[VF]};
			'he, 'h1e:
				case (enl)
					2'b00: c0c1 = 'b10;
					2'b10: c0c1 = 'b11;
					2'b01, 2'b11: c0c1 = {1'b0, microLatch[6]};
				endcase
			default: c0c1 = 1'sbx;
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (Ird[11:8])
			'h0: ccTest = 1'b1;
			'h1: ccTest = 1'b0;
			'h2: ccTest = ~psw[CF] & ~psw[ZF];
			'h3: ccTest = psw[CF] | psw[ZF];
			'h4: ccTest = ~psw[CF];
			'h5: ccTest = psw[CF];
			'h6: ccTest = ~psw[ZF];
			'h7: ccTest = psw[ZF];
			'h8: ccTest = ~psw[VF];
			'h9: ccTest = psw[VF];
			'ha: ccTest = ~psw[NF];
			'hb: ccTest = psw[NF];
			'hc: ccTest = (psw[NF] & psw[VF]) | (~psw[NF] & ~psw[VF]);
			'hd: ccTest = (psw[NF] & ~psw[VF]) | (~psw[NF] & psw[VF]);
			'he: ccTest = ((psw[NF] & psw[VF]) & ~psw[ZF]) | ((~psw[NF] & ~psw[VF]) & ~psw[ZF]);
			'hf: ccTest = (psw[ZF] | (psw[NF] & ~psw[VF])) | (~psw[NF] & psw[VF]);
		endcase
	end
	reg rTrace;
	reg rInterrupt;
	reg rIllegal;
	reg rPriv;
	reg rLineA;
	reg rLineF;
	reg rExcRst;
	reg rExcAdrErr;
	reg rExcBusErr;
	reg rSpurious;
	reg rAutovec;
	wire grp1LatchEn;
	wire grp0LatchEn;
	assign grp1LatchEn = microLatch[0] & (microLatch[1] | !microLatch[4]);
	assign grp0LatchEn = microLatch[4] & !microLatch[1];
	assign inGrp0Exc = (rExcRst | rExcBusErr) | rExcAdrErr;
	localparam SF = 13;
	always @(posedge Clks[4]) begin
		if (grp0LatchEn & enT3) begin
			rExcRst <= excRst;
			rExcBusErr <= BerrA;
			rExcAdrErr <= busAddrErr;
			rSpurious <= Spuria;
			rAutovec <= Avia;
		end
		if (grp1LatchEn & enT3) begin
			rTrace <= Tpend;
			rInterrupt <= intPend;
			rIllegal <= (isIllegal & ~isLineA) & ~isLineF;
			rLineA <= isLineA;
			rLineF <= isLineF;
			rPriv <= isPriv & !psw[SF];
		end
	end
	localparam ITLX1_NMA = 'h1c4;
	localparam TRAC1_NMA = 'h1c0;
	localparam TVN_AUTOVEC = 13;
	localparam TVN_INTERRUPT = 15;
	localparam TVN_SPURIOUS = 12;
	always @(*) begin
		if (_sv2v_0)
			;
		grp1Nma = TRAC1_NMA;
		if (rExcRst)
			tvn = 1'sb0;
		else if (rExcBusErr | rExcAdrErr)
			tvn = {1'b1, rExcAdrErr};
		else if (rSpurious | rAutovec)
			tvn = (rSpurious ? TVN_SPURIOUS : TVN_AUTOVEC);
		else if (rTrace)
			tvn = 9;
		else if (rInterrupt) begin
			tvn = TVN_INTERRUPT;
			grp1Nma = ITLX1_NMA;
		end
		else
			(* full_case, parallel_case *)
			case (1'b1)
				rIllegal: tvn = 4;
				rPriv: tvn = 8;
				rLineA: tvn = 10;
				rLineF: tvn = 11;
				default: tvn = 1;
			endcase
	end
	assign A0Sel = ((((rIllegal | rLineF) | rLineA) | rPriv) | rTrace) | rInterrupt;
	always @(posedge Clks[4])
		if (Clks[3])
			a0Rst <= 1'b1;
		else if (enT3)
			a0Rst <= 1'b0;
	initial _sv2v_0 = 0;
endmodule
module busArbiter (
	Clks,
	BRi,
	BgackI,
	Halti,
	bgBlock,
	busAvail,
	BGn
);
	reg _sv2v_0;
	input wire [4:0] Clks;
	input BRi;
	input BgackI;
	input Halti;
	input bgBlock;
	output wire busAvail;
	output reg BGn;
	reg [31:0] dmaPhase;
	reg [31:0] next;
	always @(*) begin
		if (_sv2v_0)
			;
		case (dmaPhase)
			32'd0: next = 32'd1;
			32'd1:
				if (bgBlock)
					next = 32'd1;
				else if (~BgackI)
					next = 32'd4;
				else if (~BRi)
					next = 32'd2;
				else
					next = 32'd1;
			32'd4:
				if (~BRi & !bgBlock)
					next = 32'd6;
				else if (~BgackI & !bgBlock)
					next = 32'd4;
				else
					next = 32'd1;
			32'd2: next = 32'd3;
			32'd3: next = (~BRi & BgackI ? 32'd3 : 32'd4);
			32'd6: next = 32'd5;
			32'd5:
				case ({BgackI, BRi})
					2'b11: next = 32'd1;
					2'b10: next = 32'd3;
					2'b01: next = 32'd7;
					2'b00: next = 32'd5;
				endcase
			32'd7: next = 32'd4;
			default: next = 32'd1;
		endcase
	end
	reg granting;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (next)
			32'd2, 32'd6, 32'd3, 32'd5: granting = 1'b1;
			default: granting = 1'b0;
		endcase
	end
	reg rGranted;
	assign busAvail = ((Halti & BRi) & BgackI) & ~rGranted;
	always @(posedge Clks[4]) begin
		if (Clks[3]) begin
			dmaPhase <= 32'd0;
			rGranted <= 1'b0;
		end
		else if (Clks[0]) begin
			dmaPhase <= next;
			rGranted <= granting;
		end
		if (Clks[3])
			BGn <= 1'b1;
		else if (Clks[1])
			BGn <= ~rGranted;
	end
	initial _sv2v_0 = 0;
endmodule
module busControl (
	Clks,
	enT1,
	enT4,
	permStart,
	permStop,
	iStop,
	aob0,
	isWrite,
	isByte,
	isRmc,
	busAvail,
	bgBlock,
	busAddrErr,
	waitBusCycle,
	busStarting,
	addrOe,
	bciWrite,
	rDtack,
	BeDebounced,
	Vpai,
	ASn,
	LDSn,
	UDSn,
	eRWn
);
	reg _sv2v_0;
	input wire [4:0] Clks;
	input enT1;
	input enT4;
	input permStart;
	input permStop;
	input iStop;
	input aob0;
	input isWrite;
	input isByte;
	input isRmc;
	input busAvail;
	output wire bgBlock;
	output wire busAddrErr;
	output wire waitBusCycle;
	output wire busStarting;
	output reg addrOe;
	output wire bciWrite;
	input rDtack;
	input BeDebounced;
	input Vpai;
	output wire ASn;
	output wire LDSn;
	output wire UDSn;
	output wire eRWn;
	reg rAS;
	reg rLDS;
	reg rUDS;
	reg rRWn;
	assign ASn = rAS;
	assign LDSn = rLDS;
	assign UDSn = rUDS;
	assign eRWn = rRWn;
	reg dataOe;
	reg bcPend;
	reg isWriteReg;
	reg bciByte;
	reg isRmcReg;
	reg wendReg;
	assign bciWrite = isWriteReg;
	reg addrOeDelay;
	reg isByteT4;
	wire canStart;
	wire busEnd;
	wire bcComplete;
	wire bcReset;
	wire isRcmReset = (bcComplete & bcReset) & isRmcReg;
	assign busAddrErr = aob0 & ~bciByte;
	wire busRetry = ~busAddrErr & 1'b0;
	reg [31:0] busPhase;
	reg [31:0] next;
	always @(posedge Clks[4])
		if (Clks[3])
			busPhase <= 32'd0;
		else if (Clks[1])
			busPhase <= next;
	always @(*) begin
		if (_sv2v_0)
			;
		case (busPhase)
			32'd0: next = 32'd1;
			32'd6: next = 32'd1;
			32'd2: next = 32'd3;
			32'd3: next = 32'd4;
			32'd4: next = (busEnd ? 32'd5 : 32'd4);
			32'd5: next = (isRcmReset ? 32'd6 : (canStart ? 32'd2 : 32'd1));
			32'd1: next = (canStart ? 32'd2 : 32'd1);
			default: next = 32'd1;
		endcase
	end
	wire rmcIdle = ((busPhase == 32'd1) & ~ASn) & isRmcReg;
	assign canStart = (((busAvail | rmcIdle) & (bcPend | permStart)) & !busRetry) & !bcReset;
	wire busEnding = (next == 32'd1) | (next == 32'd2);
	assign busStarting = busPhase == 32'd2;
	assign busEnd = ~rDtack | iStop;
	assign bcComplete = busPhase == 32'd5;
	wire bciClear = bcComplete & ~busRetry;
	assign bcReset = Clks[3] | ((addrOeDelay & BeDebounced) & Vpai);
	assign waitBusCycle = wendReg & !bcComplete;
	assign bgBlock = ((busPhase == 32'd2) & ASn) | (busPhase == 32'd6);
	always @(posedge Clks[4]) begin
		if (Clks[3])
			addrOe <= 1'b0;
		else if (Clks[0] & (busPhase == 32'd2))
			addrOe <= 1'b1;
		else if (Clks[1] & (busPhase == 32'd6))
			addrOe <= 1'b0;
		else if ((Clks[1] & ~isRmcReg) & busEnding)
			addrOe <= 1'b0;
		if (Clks[1])
			addrOeDelay <= addrOe;
		if (Clks[3]) begin
			rAS <= 1'b1;
			rUDS <= 1'b1;
			rLDS <= 1'b1;
			rRWn <= 1'b1;
			dataOe <= 1'sb0;
		end
		else begin
			if ((Clks[0] & isWriteReg) & (busPhase == 32'd3))
				dataOe <= 1'b1;
			else if (Clks[1] & (busEnding | (busPhase == 32'd1)))
				dataOe <= 1'b0;
			if (Clks[1] & busEnding)
				rRWn <= 1'b1;
			else if (Clks[1] & isWriteReg) begin
				if ((busPhase == 32'd2) & isWriteReg)
					rRWn <= 1'b0;
			end
			if (Clks[1] & (busPhase == 32'd2))
				rAS <= 1'b0;
			else if (Clks[0] & (busPhase == 32'd6))
				rAS <= 1'b1;
			else if ((Clks[0] & bcComplete) & ~32'd6) begin
				if (~isRmcReg)
					rAS <= 1'b1;
			end
			if (Clks[1] & (busPhase == 32'd2)) begin
				if (~isWriteReg & !busAddrErr) begin
					rUDS <= ~(~bciByte | ~aob0);
					rLDS <= ~(~bciByte | aob0);
				end
			end
			else if (((Clks[1] & isWriteReg) & (busPhase == 32'd3)) & !busAddrErr) begin
				rUDS <= ~(~bciByte | ~aob0);
				rLDS <= ~(~bciByte | aob0);
			end
			else if (Clks[0] & bcComplete) begin
				rUDS <= 1'b1;
				rLDS <= 1'b1;
			end
		end
	end
	always @(posedge Clks[4])
		if (enT4)
			isByteT4 <= isByte;
	always @(posedge Clks[4])
		if (Clks[2]) begin
			bcPend <= 1'b0;
			wendReg <= 1'b0;
			isWriteReg <= 1'b0;
			bciByte <= 1'b0;
			isRmcReg <= 1'b0;
		end
		else if (Clks[0] & (bciClear | bcReset)) begin
			bcPend <= 1'b0;
			wendReg <= 1'b0;
		end
		else begin
			if (enT1 & permStart) begin
				isWriteReg <= isWrite;
				bciByte <= isByteT4;
				isRmcReg <= isRmc & ~isWrite;
				bcPend <= 1'b1;
			end
			if (enT1)
				wendReg <= permStop;
		end
	initial _sv2v_0 = 0;
endmodule
module uRom (
	clk,
	microAddr,
	microOutput
);
	input clk;
	localparam UADDR_WIDTH = 10;
	input [9:0] microAddr;
	localparam UROM_WIDTH = 17;
	output reg [16:0] microOutput;
	localparam UROM_DEPTH = 1024;
	reg [16:0] uRam [0:1023];
	initial $readmemb("microrom.mem", uRam);
	always @(posedge clk) microOutput <= uRam[microAddr];
endmodule
module nanoRom (
	clk,
	nanoAddr,
	nanoOutput
);
	input clk;
	localparam NADDR_WIDTH = 9;
	input [8:0] nanoAddr;
	localparam NANO_WIDTH = 68;
	output reg [67:0] nanoOutput;
	localparam NANO_DEPTH = 336;
	reg [67:0] nRam [0:335];
	initial $readmemb("nanorom.mem", nRam);
	always @(posedge clk) nanoOutput <= nRam[nanoAddr];
endmodule
module microToNanoAddr (
	uAddr,
	orgAddr
);
	localparam UADDR_WIDTH = 10;
	input [9:0] uAddr;
	localparam NADDR_WIDTH = 9;
	output wire [8:0] orgAddr;
	wire [9:2] baseAddr = uAddr[9:2];
	reg [8:2] orgBase;
	assign orgAddr = {orgBase, uAddr[1:0]};
	always @(baseAddr)
		case (baseAddr)
			'h0: orgBase = 7'h00;
			'h1: orgBase = 7'h01;
			'h2: orgBase = 7'h02;
			'h3: orgBase = 7'h02;
			'h8: orgBase = 7'h03;
			'h9: orgBase = 7'h04;
			'ha: orgBase = 7'h05;
			'hb: orgBase = 7'h05;
			'h10: orgBase = 7'h06;
			'h11: orgBase = 7'h07;
			'h12: orgBase = 7'h08;
			'h13: orgBase = 7'h08;
			'h18: orgBase = 7'h09;
			'h19: orgBase = 7'h0a;
			'h1a: orgBase = 7'h0b;
			'h1b: orgBase = 7'h0b;
			'h20: orgBase = 7'h0c;
			'h21: orgBase = 7'h0d;
			'h22: orgBase = 7'h0e;
			'h23: orgBase = 7'h0d;
			'h28: orgBase = 7'h0f;
			'h29: orgBase = 7'h10;
			'h2a: orgBase = 7'h11;
			'h2b: orgBase = 7'h10;
			'h30: orgBase = 7'h12;
			'h31: orgBase = 7'h13;
			'h32: orgBase = 7'h14;
			'h33: orgBase = 7'h14;
			'h38: orgBase = 7'h15;
			'h39: orgBase = 7'h16;
			'h3a: orgBase = 7'h17;
			'h3b: orgBase = 7'h17;
			'h40: orgBase = 7'h18;
			'h41: orgBase = 7'h18;
			'h42: orgBase = 7'h18;
			'h43: orgBase = 7'h18;
			'h44: orgBase = 7'h19;
			'h45: orgBase = 7'h19;
			'h46: orgBase = 7'h19;
			'h47: orgBase = 7'h19;
			'h48: orgBase = 7'h1a;
			'h49: orgBase = 7'h1a;
			'h4a: orgBase = 7'h1a;
			'h4b: orgBase = 7'h1a;
			'h4c: orgBase = 7'h1b;
			'h4d: orgBase = 7'h1b;
			'h4e: orgBase = 7'h1b;
			'h4f: orgBase = 7'h1b;
			'h54: orgBase = 7'h1c;
			'h55: orgBase = 7'h1d;
			'h56: orgBase = 7'h1e;
			'h57: orgBase = 7'h1f;
			'h5c: orgBase = 7'h20;
			'h5d: orgBase = 7'h21;
			'h5e: orgBase = 7'h22;
			'h5f: orgBase = 7'h23;
			'h70: orgBase = 7'h24;
			'h71: orgBase = 7'h24;
			'h72: orgBase = 7'h24;
			'h73: orgBase = 7'h24;
			'h74: orgBase = 7'h24;
			'h75: orgBase = 7'h24;
			'h76: orgBase = 7'h24;
			'h77: orgBase = 7'h24;
			'h78: orgBase = 7'h25;
			'h79: orgBase = 7'h25;
			'h7a: orgBase = 7'h25;
			'h7b: orgBase = 7'h25;
			'h7c: orgBase = 7'h25;
			'h7d: orgBase = 7'h25;
			'h7e: orgBase = 7'h25;
			'h7f: orgBase = 7'h25;
			'h84: orgBase = 7'h26;
			'h85: orgBase = 7'h27;
			'h86: orgBase = 7'h28;
			'h87: orgBase = 7'h29;
			'h8c: orgBase = 7'h2a;
			'h8d: orgBase = 7'h2b;
			'h8e: orgBase = 7'h2c;
			'h8f: orgBase = 7'h2d;
			'h94: orgBase = 7'h2e;
			'h95: orgBase = 7'h2f;
			'h96: orgBase = 7'h30;
			'h97: orgBase = 7'h31;
			'h9c: orgBase = 7'h32;
			'h9d: orgBase = 7'h33;
			'h9e: orgBase = 7'h34;
			'h9f: orgBase = 7'h35;
			'ha4: orgBase = 7'h36;
			'ha5: orgBase = 7'h36;
			'ha6: orgBase = 7'h37;
			'ha7: orgBase = 7'h37;
			'hac: orgBase = 7'h38;
			'had: orgBase = 7'h38;
			'hae: orgBase = 7'h39;
			'haf: orgBase = 7'h39;
			'hb4: orgBase = 7'h3a;
			'hb5: orgBase = 7'h3a;
			'hb6: orgBase = 7'h3b;
			'hb7: orgBase = 7'h3b;
			'hbc: orgBase = 7'h3c;
			'hbd: orgBase = 7'h3c;
			'hbe: orgBase = 7'h3d;
			'hbf: orgBase = 7'h3d;
			'hc0: orgBase = 7'h3e;
			'hc1: orgBase = 7'h3f;
			'hc2: orgBase = 7'h40;
			'hc3: orgBase = 7'h41;
			'hc8: orgBase = 7'h42;
			'hc9: orgBase = 7'h43;
			'hca: orgBase = 7'h44;
			'hcb: orgBase = 7'h45;
			'hd0: orgBase = 7'h46;
			'hd1: orgBase = 7'h47;
			'hd2: orgBase = 7'h48;
			'hd3: orgBase = 7'h49;
			'hd8: orgBase = 7'h4a;
			'hd9: orgBase = 7'h4b;
			'hda: orgBase = 7'h4c;
			'hdb: orgBase = 7'h4d;
			'he0: orgBase = 7'h4e;
			'he1: orgBase = 7'h4e;
			'he2: orgBase = 7'h4f;
			'he3: orgBase = 7'h4f;
			'he8: orgBase = 7'h50;
			'he9: orgBase = 7'h50;
			'hea: orgBase = 7'h51;
			'heb: orgBase = 7'h51;
			'hf0: orgBase = 7'h52;
			'hf1: orgBase = 7'h52;
			'hf2: orgBase = 7'h52;
			'hf3: orgBase = 7'h52;
			'hf8: orgBase = 7'h53;
			'hf9: orgBase = 7'h53;
			'hfa: orgBase = 7'h53;
			'hfb: orgBase = 7'h53;
			default: orgBase = 1'sbx;
		endcase
endmodule
module fx68kAlu (
	clk,
	pwrUp,
	enT1,
	enT3,
	enT4,
	ird,
	aluColumn,
	aluDataCtrl,
	aluAddrCtrl,
	alueClkEn,
	ftu2Ccr,
	init,
	finish,
	aluIsByte,
	ftu,
	alub,
	iDataBus,
	iAddrBus,
	ze,
	alue,
	ccr,
	aluOut
);
	reg _sv2v_0;
	input clk;
	input pwrUp;
	input enT1;
	input enT3;
	input enT4;
	input [15:0] ird;
	input [2:0] aluColumn;
	input [1:0] aluDataCtrl;
	input aluAddrCtrl;
	input alueClkEn;
	input ftu2Ccr;
	input init;
	input finish;
	input aluIsByte;
	input [15:0] ftu;
	input [15:0] alub;
	input [15:0] iDataBus;
	input [15:0] iAddrBus;
	output wire ze;
	output reg [15:0] alue;
	output reg [7:0] ccr;
	output wire [15:0] aluOut;
	localparam CF = 0;
	localparam VF = 1;
	localparam ZF = 2;
	localparam NF = 3;
	localparam XF = 4;
	reg [15:0] aluLatch;
	reg [4:0] pswCcr;
	reg [4:0] ccrCore;
	reg [15:0] result;
	reg [4:0] ccrTemp;
	reg coreH;
	reg [15:0] subResult;
	reg subHcarry;
	reg subCout;
	reg subOv;
	assign aluOut = aluLatch;
	assign ze = ~ccrCore[ZF];
	reg [15:0] row;
	reg isArX;
	reg noCcrEn;
	reg isByte;
	reg [4:0] ccrMask;
	reg [4:0] oper;
	reg [15:0] aOperand;
	reg [15:0] dOperand;
	wire isCorf = aluDataCtrl == 2'b10;
	wire [15:0] cRow;
	wire cIsArX;
	wire cNoCcrEn;
	rowDecoder rowDecoder(
		.ird(ird),
		.row(cRow),
		.noCcrEn(cNoCcrEn),
		.isArX(cIsArX)
	);
	wire [4:0] cMask;
	wire [4:0] aluOp;
	aluGetOp aluGetOp(
		.row(row),
		.col(aluColumn),
		.isCorf(isCorf),
		.aluOp(aluOp)
	);
	ccrTable ccrTable(
		.col(aluColumn),
		.row(row),
		.finish(finish),
		.ccrMask(cMask)
	);
	wire shftIsMul = row[7];
	wire shftIsDiv = row[1];
	wire [31:0] shftResult;
	reg [7:0] bcdLatch;
	reg bcdCarry;
	reg bcdOverf;
	reg isLong;
	reg rIrd8;
	reg isShift;
	reg shftCin;
	reg shftRight;
	reg addCin;
	always @(posedge clk) begin
		if (enT3) begin
			row <= cRow;
			isArX <= cIsArX;
			noCcrEn <= cNoCcrEn;
			rIrd8 <= ird[8];
			isByte <= aluIsByte;
		end
		if (enT4) begin
			isLong <= ((ird[7] & ~ird[6]) | shftIsMul) | shftIsDiv;
			ccrMask <= cMask;
			oper <= aluOp;
		end
	end
	always @(*) begin
		if (_sv2v_0)
			;
		aOperand = (aluAddrCtrl ? alub : iAddrBus);
		case (aluDataCtrl)
			2'b00: dOperand = iDataBus;
			2'b01: dOperand = 'h0;
			2'b11: dOperand = 'hffff;
			2'b10: dOperand = 1'sbx;
		endcase
	end
	wire shftMsb = (isLong ? alue[15] : (isByte ? aOperand[7] : aOperand[15]));
	aluShifter shifter(
		.data({alue, aOperand}),
		.swapWords(shftIsMul | shftIsDiv),
		.cin(shftCin),
		.dir(shftRight),
		.isByte(isByte),
		.isLong(isLong),
		.result(shftResult)
	);
	wire [7:0] bcdResult;
	wire bcdC;
	wire bcdV;
	localparam OP_SBCD = 6;
	aluCorf aluCorf(
		.binResult(aluLatch[7:0]),
		.hCarry(coreH),
		.bAdd(oper != OP_SBCD),
		.cin(pswCcr[XF]),
		.bcdResult(bcdResult),
		.dC(bcdC),
		.ov(bcdV)
	);
	always @(posedge clk)
		if (enT1) begin
			bcdLatch <= bcdResult;
			bcdCarry <= bcdC;
			bcdOverf <= bcdV;
		end
	localparam OP_ADD = 4;
	localparam OP_ADDC = 11;
	localparam OP_ADDX = 12;
	localparam OP_SUB = 2;
	localparam OP_SUB0 = 7;
	localparam OP_SUBC = 10;
	localparam OP_SUBX = 3;
	always @(*) begin
		if (_sv2v_0)
			;
		case (oper)
			OP_ADD, OP_SUB: addCin = 1'b0;
			OP_SUB0: addCin = 1'b1;
			OP_ADDC, OP_SUBC: addCin = ccrCore[CF];
			OP_ADDX, OP_SUBX: addCin = pswCcr[XF];
			default: addCin = 1'bx;
		endcase
	end
	localparam OP_ASL = 13;
	localparam OP_ASR = 14;
	localparam OP_LSL = 15;
	localparam OP_LSR = 16;
	localparam OP_ROL = 17;
	localparam OP_ROR = 18;
	localparam OP_ROXL = 19;
	localparam OP_ROXR = 20;
	localparam OP_SLAA = 21;
	always @(*) begin
		if (_sv2v_0)
			;
		case (oper)
			OP_LSL, OP_ASL, OP_ROL, OP_ROXL, OP_SLAA: shftRight = 1'b0;
			OP_LSR, OP_ASR, OP_ROR, OP_ROXR: shftRight = 1'b1;
			default: shftRight = 1'bx;
		endcase
		case (oper)
			OP_LSR, OP_ASL, OP_LSL: shftCin = 1'b0;
			OP_ROL, OP_ASR: shftCin = shftMsb;
			OP_ROR: shftCin = aOperand[0];
			OP_ROXL, OP_ROXR:
				if (shftIsMul)
					shftCin = (rIrd8 ? pswCcr[NF] ^ pswCcr[VF] : pswCcr[CF]);
				else
					shftCin = pswCcr[XF];
			OP_SLAA: shftCin = aluColumn[1];
			default: shftCin = 1'sbx;
		endcase
	end
	task mySubber;
		input [15:0] inpa;
		input [15:0] inpb;
		input cin;
		input bAdd;
		input isByte;
		output reg [15:0] result;
		output reg cout;
		output reg ov;
		reg [16:0] rtemp;
		reg rm;
		reg sm;
		reg dm;
		reg tsm;
		begin
			if (isByte) begin
				rtemp = (bAdd ? ({1'b0, inpb[7:0]} + {1'b0, inpa[7:0]}) + cin : ({1'b0, inpb[7:0]} - {1'b0, inpa[7:0]}) - cin);
				result = {{8 {rtemp[7]}}, rtemp[7:0]};
				cout = rtemp[8];
			end
			else begin
				rtemp = (bAdd ? ({1'b0, inpb} + {1'b0, inpa}) + cin : ({1'b0, inpb} - {1'b0, inpa}) - cin);
				result = rtemp[15:0];
				cout = rtemp[16];
			end
			rm = (isByte ? rtemp[7] : rtemp[15]);
			dm = (isByte ? inpb[7] : inpb[15]);
			tsm = (isByte ? inpa[7] : inpa[15]);
			sm = (bAdd ? tsm : ~tsm);
			ov = ((sm & dm) & ~rm) | ((~sm & ~dm) & rm);
			subHcarry = (inpa[4] ^ inpb[4]) ^ rtemp[4];
		end
	endtask
	localparam OP_ABCD = 22;
	localparam OP_AND = 1;
	localparam OP_EOR = 9;
	localparam OP_EXT = 5;
	localparam OP_OR = 8;
	always @(*) begin
		if (_sv2v_0)
			;
		mySubber(aOperand, dOperand, addCin, ((oper == OP_ADD) | (oper == OP_ADDC)) | (oper == OP_ADDX), isByte, subResult, subCout, subOv);
		isShift = 1'b0;
		case (oper)
			OP_AND: result = aOperand & dOperand;
			OP_OR: result = aOperand | dOperand;
			OP_EOR: result = aOperand ^ dOperand;
			OP_EXT: result = {{8 {aOperand[7]}}, aOperand[7:0]};
			OP_SLAA, OP_ASL, OP_ASR, OP_LSL, OP_LSR, OP_ROL, OP_ROR, OP_ROXL, OP_ROXR: begin
				result = shftResult[15:0];
				isShift = 1'b1;
			end
			OP_ADD, OP_ADDC, OP_ADDX, OP_SUB, OP_SUBC, OP_SUB0, OP_SUBX: result = subResult;
			OP_ABCD, OP_SBCD: result = {8'hxx, bcdLatch};
			default: result = 1'sbx;
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		ccrTemp[XF] = pswCcr[XF];
		ccrTemp[CF] = 0;
		ccrTemp[VF] = 0;
		ccrTemp[ZF] = (isByte ? ~(|result[7:0]) : ~(|result));
		ccrTemp[NF] = (isByte ? result[7] : result[15]);
		(* full_case, parallel_case *)
		case (oper)
			OP_EXT:
				if (aluColumn == 5) begin
					ccrTemp[VF] = 1'b1;
					ccrTemp[NF] = 1'b1;
					ccrTemp[ZF] = 1'b0;
				end
			OP_SUB0, OP_OR, OP_EOR: begin
				ccrTemp[CF] = 0;
				ccrTemp[VF] = 0;
			end
			OP_AND: begin
				if ((aluColumn == 1) & (row[11] | row[8]))
					ccrTemp[CF] = pswCcr[XF];
				else
					ccrTemp[CF] = 0;
				ccrTemp[VF] = 0;
			end
			OP_SLAA: ccrTemp[CF] = aOperand[15];
			OP_LSL, OP_ROXL: begin
				ccrTemp[CF] = shftMsb;
				ccrTemp[XF] = shftMsb;
				ccrTemp[VF] = 1'b0;
			end
			OP_LSR, OP_ROXR: begin
				ccrTemp[CF] = (shftIsMul ? 1'b0 : aOperand[0]);
				ccrTemp[XF] = aOperand[0];
				ccrTemp[VF] = 0;
			end
			OP_ASL: begin
				ccrTemp[XF] = shftMsb;
				ccrTemp[CF] = shftMsb;
				ccrTemp[VF] = pswCcr[VF] | (shftMsb ^ (isLong ? alue[14] : (isByte ? aOperand[6] : aOperand[14])));
			end
			OP_ASR: begin
				ccrTemp[XF] = aOperand[0];
				ccrTemp[CF] = aOperand[0];
				ccrTemp[VF] = 0;
			end
			OP_ROL: ccrTemp[CF] = shftMsb;
			OP_ROR: ccrTemp[CF] = aOperand[0];
			OP_ADD, OP_ADDC, OP_ADDX, OP_SUB, OP_SUBC, OP_SUBX: begin
				ccrTemp[CF] = subCout;
				ccrTemp[XF] = subCout;
				ccrTemp[VF] = subOv;
			end
			OP_ABCD, OP_SBCD: begin
				ccrTemp[XF] = bcdCarry;
				ccrTemp[CF] = bcdCarry;
				ccrTemp[VF] = bcdOverf;
			end
		endcase
	end
	reg [4:0] ccrMasked;
	always @(*) begin
		if (_sv2v_0)
			;
		ccrMasked = (ccrTemp & ccrMask) | (pswCcr & ~ccrMask);
		if (finish | isArX)
			ccrMasked[ZF] = ccrTemp[ZF] & pswCcr[ZF];
	end
	always @(posedge clk) begin
		if (enT3) begin
			if (|aluColumn) begin
				aluLatch <= result;
				coreH <= subHcarry;
				if (|aluColumn)
					ccrCore <= ccrTemp;
			end
			if (alueClkEn)
				alue <= iDataBus;
			else if (isShift & |aluColumn)
				alue <= shftResult[31:16];
		end
		if (pwrUp)
			pswCcr <= 1'sb0;
		else if (enT3 & ftu2Ccr)
			pswCcr <= ftu[4:0];
		else if ((enT3 & ~noCcrEn) & (finish | init))
			pswCcr <= ccrMasked;
	end
	wire [8:1] sv2v_tmp_A86A0;
	assign sv2v_tmp_A86A0 = {3'b000, pswCcr};
	always @(*) ccr = sv2v_tmp_A86A0;
	initial _sv2v_0 = 0;
endmodule
module aluCorf (
	binResult,
	bAdd,
	cin,
	hCarry,
	bcdResult,
	dC,
	ov
);
	reg _sv2v_0;
	input [7:0] binResult;
	input bAdd;
	input cin;
	input hCarry;
	output wire [7:0] bcdResult;
	output wire dC;
	output reg ov;
	reg [8:0] htemp;
	reg [4:0] hNib;
	function gt9;
		input [3:0] nib;
		gt9 = nib[3] & (nib[2] | nib[1]);
	endfunction
	wire lowC = hCarry | (bAdd ? gt9(binResult[3:0]) : 1'b0);
	wire highC = cin | (bAdd ? gt9(htemp[7:4]) | htemp[8] : 1'b0);
	always @(*) begin
		if (_sv2v_0)
			;
		if (bAdd) begin
			htemp = {1'b0, binResult} + (lowC ? 4'h6 : 4'h0);
			hNib = htemp[8:4] + (highC ? 4'h6 : 4'h0);
			ov = hNib[3] & ~binResult[7];
		end
		else begin
			htemp = {1'b0, binResult} - (lowC ? 4'h6 : 4'h0);
			hNib = htemp[8:4] - (highC ? 4'h6 : 4'h0);
			ov = ~hNib[3] & binResult[7];
		end
	end
	assign bcdResult = {hNib[3:0], htemp[3:0]};
	assign dC = hNib[4] | cin;
	initial _sv2v_0 = 0;
endmodule
module aluShifter (
	data,
	isByte,
	isLong,
	swapWords,
	dir,
	cin,
	result
);
	reg _sv2v_0;
	input [31:0] data;
	input isByte;
	input isLong;
	input swapWords;
	input dir;
	input cin;
	output reg [31:0] result;
	reg [31:0] tdata;
	always @(*) begin
		if (_sv2v_0)
			;
		tdata = data;
		if (isByte & dir)
			tdata[8] = cin;
		else if (!isLong & dir)
			tdata[16] = cin;
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if (swapWords & dir)
			result = {tdata[0], tdata[31:17], cin, tdata[15:1]};
		else if (swapWords)
			result = {tdata[30:16], cin, tdata[14:0], tdata[31]};
		else if (dir)
			result = {cin, tdata[31:1]};
		else
			result = {tdata[30:0], cin};
	end
	initial _sv2v_0 = 0;
endmodule
module aluGetOp (
	row,
	col,
	isCorf,
	aluOp
);
	reg _sv2v_0;
	input [15:0] row;
	input [2:0] col;
	input isCorf;
	output reg [4:0] aluOp;
	localparam OP_ABCD = 22;
	localparam OP_ADD = 4;
	localparam OP_ADDC = 11;
	localparam OP_ADDX = 12;
	localparam OP_AND = 1;
	localparam OP_ASL = 13;
	localparam OP_ASR = 14;
	localparam OP_EOR = 9;
	localparam OP_EXT = 5;
	localparam OP_LSL = 15;
	localparam OP_LSR = 16;
	localparam OP_OR = 8;
	localparam OP_ROL = 17;
	localparam OP_ROR = 18;
	localparam OP_ROXL = 19;
	localparam OP_ROXR = 20;
	localparam OP_SBCD = 6;
	localparam OP_SLAA = 21;
	localparam OP_SUB = 2;
	localparam OP_SUB0 = 7;
	localparam OP_SUBC = 10;
	localparam OP_SUBX = 3;
	always @(*) begin
		if (_sv2v_0)
			;
		aluOp = 1'sbx;
		(* full_case, parallel_case *)
		case (col)
			1: aluOp = OP_AND;
			5: aluOp = OP_EXT;
			default:
				(* full_case, parallel_case *)
				case (1'b1)
					row[1]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUB;
							3: aluOp = OP_SUBC;
							4, 6: aluOp = OP_SLAA;
						endcase
					row[2]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_ADD;
							3: aluOp = OP_ADDC;
							4: aluOp = OP_ASR;
						endcase
					row[3]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_ADDX;
							3: aluOp = (isCorf ? OP_ABCD : OP_ADD);
							4: aluOp = OP_ASL;
						endcase
					row[4]: aluOp = (col == 4 ? OP_LSL : OP_AND);
					row[5], row[6]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUB;
							3: aluOp = OP_SUBC;
							4: aluOp = OP_LSR;
						endcase
					row[7]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUB;
							3: aluOp = OP_ADD;
							4: aluOp = OP_ROXR;
						endcase
					row[8]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_EXT;
							3: aluOp = OP_AND;
							4: aluOp = OP_ROXR;
						endcase
					row[9]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUBX;
							3: aluOp = OP_SBCD;
							4: aluOp = OP_ROL;
						endcase
					row[10]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUBX;
							3: aluOp = OP_SUBC;
							4: aluOp = OP_ROR;
						endcase
					row[11]:
						(* full_case, parallel_case *)
						case (col)
							2: aluOp = OP_SUB0;
							3: aluOp = OP_SUB0;
							4: aluOp = OP_ROXL;
						endcase
					row[12]: aluOp = OP_ADDX;
					row[13]: aluOp = OP_EOR;
					row[14]: aluOp = (col == 4 ? OP_EOR : OP_OR);
					row[15]: aluOp = (col == 3 ? OP_ADD : OP_OR);
				endcase
		endcase
	end
	initial _sv2v_0 = 0;
endmodule
module rowDecoder (
	ird,
	row,
	noCcrEn,
	isArX
);
	reg _sv2v_0;
	input [15:0] ird;
	output reg [15:0] row;
	output wire noCcrEn;
	output reg isArX;
	wire eaRdir = ird[5:4] == 2'b00;
	wire eaAdir = ird[5:3] == 3'b001;
	wire size11 = ird[7] & ird[6];
	always @(*) begin
		if (_sv2v_0)
			;
		case (ird[15:12])
			'h4, 'h9, 'hd: isArX = row[10] | row[12];
			default: isArX = 1'b0;
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (ird[15:12])
			'h4:
				if (ird[8])
					row = 16'h0040;
				else
					case (ird[11:9])
						'b0: row = 16'h0400;
						'b1: row = 16'h0010;
						'b10: row = 16'h0020;
						'b11: row = 16'h0800;
						'b100: row = (ird[7] ? 16'h0100 : 16'h0200);
						'b101: row = 16'h8000;
						default: row = 0;
					endcase
			'h0:
				if (ird[8])
					row = (ird[7] ? 16'h4000 : 16'h2000);
				else
					case (ird[11:9])
						'b0: row = 16'h4000;
						'b1: row = 16'h0010;
						'b10: row = 16'h0020;
						'b11: row = 16'h0004;
						'b100: row = (ird[7] ? 16'h4000 : 16'h2000);
						'b101: row = 16'h2000;
						'b110: row = 16'h0040;
						default: row = 0;
					endcase
			'h1, 'h2, 'h3: row = 16'h0004;
			'h5:
				if (size11)
					row = 16'h8000;
				else
					row = (ird[8] ? 16'h0020 : 16'h0004);
			'h6: row = 0;
			'h7: row = 16'h0004;
			'h8:
				if (size11)
					row = 16'h0002;
				else if (ird[8] & eaRdir)
					row = 16'h0200;
				else
					row = 16'h4000;
			'h9:
				if ((ird[8] & ~size11) & eaRdir)
					row = 16'h0400;
				else
					row = 16'h0020;
			'hb:
				if ((ird[8] & ~size11) & ~eaAdir)
					row = 16'h2000;
				else
					row = 16'h0040;
			'hc:
				if (size11)
					row = 16'h0080;
				else if (ird[8] & eaRdir)
					row = 16'h0008;
				else
					row = 16'h0010;
			'hd:
				if ((ird[8] & ~size11) & eaRdir)
					row = 16'h1000;
				else
					row = 16'h0004;
			'he:
				case ({(size11 ? ird[10:9] : ird[4:3]), ird[8]})
					0: row = 16'h0004;
					1: row = 16'h0008;
					2: row = 16'h0020;
					3: row = 16'h0010;
					4: row = 16'h0100;
					5: row = 16'h0800;
					6: row = 16'h0400;
					7: row = 16'h0200;
				endcase
			default: row = 0;
		endcase
	end
	assign noCcrEn = ((((ird[15] & ~ird[13]) & ird[12]) & size11) | ((ird[15:12] == 4'h5) & eaAdir)) | (((~ird[15] & ~ird[14]) & ird[13]) & (ird[8:6] == 3'b001));
	initial _sv2v_0 = 0;
endmodule
module ccrTable (
	col,
	row,
	finish,
	ccrMask
);
	reg _sv2v_0;
	input [2:0] col;
	input [15:0] row;
	input finish;
	localparam MASK_NBITS = 5;
	output reg [4:0] ccrMask;
	localparam KNZ00 = 5'b01111;
	localparam KKZKK = 5'b00100;
	localparam KNZKK = 5'b01100;
	localparam KNZ10 = 5'b01111;
	localparam KNZ0C = 5'b01111;
	localparam KNZVC = 5'b01111;
	localparam XNKVC = 5'b11011;
	localparam CUPDALL = 5'b11111;
	localparam CUNUSED = 5'bxxxxx;
	reg [4:0] ccrMask1;
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (col)
			1: ccrMask = ccrMask1;
			2, 3:
				(* full_case, parallel_case *)
				case (1'b1)
					row[1]: ccrMask = KNZ0C;
					row[3], row[9]: ccrMask = (col == 2 ? XNKVC : CUPDALL);
					row[2], row[5], row[10], row[12]: ccrMask = CUPDALL;
					row[6], row[7], row[11]: ccrMask = KNZVC;
					row[4], row[8], row[13], row[14]: ccrMask = KNZ00;
					row[15]: ccrMask = 5'b00000;
					default: ccrMask = CUNUSED;
				endcase
			4:
				(* full_case, parallel_case *)
				case (row)
					16'h0004, 16'h0008, 16'h0010, 16'h0020: ccrMask = CUPDALL;
					16'h0080: ccrMask = KNZ00;
					16'h0200, 16'h0400: ccrMask = KNZ00;
					16'h0100, 16'h0800: ccrMask = CUPDALL;
					default: ccrMask = CUNUSED;
				endcase
			5: ccrMask = (row[1] ? KNZ10 : 5'b00000);
			default: ccrMask = CUNUSED;
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if (finish)
			ccrMask1 = (row[7] ? KNZ00 : KNZKK);
		else
			ccrMask1 = (row[13] | row[14] ? KKZKK : KNZ00);
	end
	initial _sv2v_0 = 0;
endmodule
module pla_lined (
	movEa,
	col,
	opcode,
	lineBmap,
	palIll,
	plaA1,
	plaA2,
	plaA3
);
	reg _sv2v_0;
	input [3:0] movEa;
	input [3:0] col;
	input [15:0] opcode;
	input [15:0] lineBmap;
	output wire palIll;
	output wire [9:0] plaA1;
	output wire [9:0] plaA2;
	output wire [9:0] plaA3;
	wire [3:0] line = opcode[15:12];
	wire [2:0] row86 = opcode[8:6];
	reg [15:0] arIll;
	reg [159:0] arA1;
	reg [159:0] arA23;
	reg [9:0] scA3;
	reg illMisc;
	reg [9:0] a1Misc;
	assign palIll = |(arIll & lineBmap);
	assign plaA1 = arA1[line*10 +: 10];
	assign plaA2 = arA23[line*10 +: 10];
	assign plaA3 = (lineBmap[0] ? scA3 : arA23[line*10 +: 10]);
	always @(*) begin
		if (_sv2v_0)
			;
		arIll['h6] = 1'b0;
		arA23[69:60] = 1'sbx;
		if (opcode[11:8] == 4'h1)
			arA1[69:60] = (|opcode[7:0] ? 'h89 : 'ha9);
		else
			arA1[69:60] = (|opcode[7:0] ? 'h308 : 'h68);
		arIll['h7] = opcode[8];
		arA23[79:70] = 1'sbx;
		arA1[79:70] = 'h23b;
		arIll['ha] = 1'b1;
		arIll['hf] = 1'b1;
		arA1[109:100] = 1'sbx;
		arA1[159:150] = 1'sbx;
		arA23[109:100] = 1'sbx;
		arA23[159:150] = 1'sbx;
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if ((~opcode[11] & opcode[7]) & opcode[6]) begin
			arA23[149:140] = 'h3c7;
			(* full_case, parallel_case *)
			case (col)
				2: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h6;
				end
				3: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h21c;
				end
				4: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h103;
				end
				5: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h1c2;
				end
				6: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h1e3;
				end
				7: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'ha;
				end
				8: begin
					arIll['he] = 1'b0;
					arA1[149:140] = 'h1e2;
				end
				default: begin
					arIll['he] = 1'b1;
					arA1[149:140] = 1'sbx;
				end
			endcase
		end
		else begin
			arA23[149:140] = 1'sbx;
			(* full_case, parallel_case *)
			case (opcode[7:6])
				2'b00, 2'b01: begin
					arIll['he] = 1'b0;
					arA1[149:140] = (opcode[5] ? 'h382 : 'h381);
				end
				2'b10: begin
					arIll['he] = 1'b0;
					arA1[149:140] = (opcode[5] ? 'h386 : 'h385);
				end
				2'b11: begin
					arIll['he] = 1'b1;
					arA1[149:140] = 1'sbx;
				end
			endcase
		end
	end
	always @(*) begin
		if (_sv2v_0)
			;
		illMisc = 1'b0;
		case (opcode[5:3])
			3'b000, 3'b001: a1Misc = 'h1d0;
			3'b010: a1Misc = 'h30b;
			3'b011: a1Misc = 'h119;
			3'b100: a1Misc = 'h2f5;
			3'b101: a1Misc = 'h230;
			3'b110:
				case (opcode[2:0])
					3'b110: a1Misc = 'h6d;
					3'b000: a1Misc = 'h3a6;
					3'b001: a1Misc = 'h363;
					3'b010: a1Misc = 'h3a2;
					3'b011: a1Misc = 'h12a;
					3'b111: a1Misc = 'h12a;
					3'b101: a1Misc = 'h126;
					default: begin
						illMisc = 1'b1;
						a1Misc = 1'sbx;
					end
				endcase
			default: begin
				illMisc = 1'b1;
				a1Misc = 1'sbx;
			end
		endcase
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if ((opcode[11:6] & 'h1f) == 'h8)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1cc;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h0)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1cc;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h1f) == 'h9)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1cc;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h1)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1cc;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h1f) == 'ha)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h10c;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hb;
					scA3 = 'h29d;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hf;
					scA3 = 'h29d;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h179;
					scA3 = 'h29d;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1c6;
					scA3 = 'h29d;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e7;
					scA3 = 'h29d;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'he;
					scA3 = 'h29d;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e6;
					scA3 = 'h29d;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h2)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h10c;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hb;
					scA3 = 'h29d;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hf;
					scA3 = 'h29d;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h179;
					scA3 = 'h29d;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1c6;
					scA3 = 'h29d;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e7;
					scA3 = 'h29d;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'he;
					scA3 = 'h29d;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e6;
					scA3 = 'h29d;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h10)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h11)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h100;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h299;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h299;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h299;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h299;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h299;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h299;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h299;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h37) == 'h12)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h10c;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hb;
					scA3 = 'h29d;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hf;
					scA3 = 'h29d;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h179;
					scA3 = 'h29d;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1c6;
					scA3 = 'h29d;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e7;
					scA3 = 'h29d;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'he;
					scA3 = 'h29d;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e6;
					scA3 = 'h29d;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h4)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e7;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1d2;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h6;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h21c;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h103;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1c2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e3;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'ha;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				9: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1c2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				10: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e3;
					arA23[9:0] = 1'sbx;
					scA3 = 'h215;
				end
				11: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'hea;
					arA23[9:0] = 'hab;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h5)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3ef;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1d6;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h6;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h21c;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h103;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1c2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e3;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'ha;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h7)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3ef;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1ce;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h6;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h21c;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h103;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1c2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e3;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'ha;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h81;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h6)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3eb;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1ca;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h6;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h21c;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h103;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1c2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e3;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'ha;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h1e2;
					arA23[9:0] = 1'sbx;
					scA3 = 'h69;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h20)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h3e7;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h215;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h215;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h215;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h215;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h215;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h215;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h215;
				end
				9: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h215;
				end
				10: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h215;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h21)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h3ef;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h81;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h81;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h81;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h81;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h81;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h81;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h81;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h23)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h3ef;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h81;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h81;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h81;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h81;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h81;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h81;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h81;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h22)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h3eb;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h69;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h69;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h69;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h69;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h69;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h69;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h69;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h30)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h108;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h87;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h87;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h87;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h87;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h87;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h87;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h87;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h31)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h108;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h6;
					scA3 = 'h87;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h21c;
					scA3 = 'h87;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h103;
					scA3 = 'h87;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1c2;
					scA3 = 'h87;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e3;
					scA3 = 'h87;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'ha;
					scA3 = 'h87;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h2b9;
					arA23[9:0] = 'h1e2;
					scA3 = 'h87;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h32)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h104;
					scA3 = 1'sbx;
				end
				1: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				2: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hb;
					scA3 = 'h8f;
				end
				3: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'hf;
					scA3 = 'h8f;
				end
				4: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h179;
					scA3 = 'h8f;
				end
				5: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1c6;
					scA3 = 'h8f;
				end
				6: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e7;
					scA3 = 'h8f;
				end
				7: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'he;
					scA3 = 'h8f;
				end
				8: begin
					arIll['h0] = 1'b0;
					arA1[9:0] = 'h3e0;
					arA23[9:0] = 'h1e6;
					scA3 = 'h8f;
				end
				9: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				10: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				11: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
				default: begin
					arIll['h0] = 1'b1;
					arA1[9:0] = 1'sbx;
					arA23[9:0] = 1'sbx;
					scA3 = 1'sbx;
				end
			endcase
		else begin
			arIll['h0] = 1'b1;
			arA1[9:0] = 1'sbx;
			arA23[9:0] = 1'sbx;
			scA3 = 1'sbx;
		end
	end
	always @(*) begin
		if (_sv2v_0)
			;
		if ((opcode[11:6] & 'h27) == 'h0)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h133;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h2b8;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h2b8;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h2b8;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h2b8;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h2b8;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h2b8;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h2b8;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h27) == 'h1)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h133;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h2b8;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h2b8;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h2b8;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h2b8;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h2b8;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h2b8;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h2b8;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h27) == 'h2)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h137;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hb;
					arA23[49:40] = 'h2bc;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hf;
					arA23[49:40] = 'h2bc;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h179;
					arA23[49:40] = 'h2bc;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c6;
					arA23[49:40] = 'h2bc;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e7;
					arA23[49:40] = 'h2bc;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'he;
					arA23[49:40] = 'h2bc;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e6;
					arA23[49:40] = 'h2bc;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h3)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3a5;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h3a1;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h3a1;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h3a1;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h3a1;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h3a1;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h3a1;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h3a1;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h13)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h301;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h159;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h159;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h159;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h159;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h159;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h159;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h159;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h159;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h159;
				end
				11: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hea;
					arA23[49:40] = 'h301;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h1b)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h301;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h159;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h159;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h159;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h159;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h159;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h159;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h159;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h159;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h159;
				end
				11: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hea;
					arA23[49:40] = 'h301;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h20)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h13b;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h15c;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h15c;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h15c;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h15c;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h15c;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h15c;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h15c;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h21)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h341;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h17c;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h17d;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1ff;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h178;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1fa;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h17d;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1ff;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h22)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h133;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3a0;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3a4;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f1;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h325;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1ed;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e5;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h23)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h232;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3a0;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3a4;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f1;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h325;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1ed;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e5;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h28)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h12d;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h3c3;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h3c3;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h3c3;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h3c3;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h3c3;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h3c3;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h3c3;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h29)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h12d;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h3c3;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h3c3;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h3c3;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h3c3;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h3c3;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h3c3;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h3c3;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h2a)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h125;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hb;
					arA23[49:40] = 'h3cb;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hf;
					arA23[49:40] = 'h3cb;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h179;
					arA23[49:40] = 'h3cb;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c6;
					arA23[49:40] = 'h3cb;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e7;
					arA23[49:40] = 'h3cb;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'he;
					arA23[49:40] = 'h3cb;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e6;
					arA23[49:40] = 'h3cb;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h2b)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h345;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h343;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h343;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h343;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h343;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h343;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h343;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h343;
				end
				9: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h3e) == 'h32)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h127;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h123;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1fd;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f5;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f9;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e9;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1fd;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f5;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h6)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h152;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h6;
					arA23[49:40] = 'h151;
				end
				3: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h21c;
					arA23[49:40] = 'h151;
				end
				4: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h103;
					arA23[49:40] = 'h151;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h151;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h151;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'ha;
					arA23[49:40] = 'h151;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e2;
					arA23[49:40] = 'h151;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1c2;
					arA23[49:40] = 'h151;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1e3;
					arA23[49:40] = 'h151;
				end
				11: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'hea;
					arA23[49:40] = 'h152;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if ((opcode[11:6] & 'h7) == 'h7)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2f1;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2f2;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1fb;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h275;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h3e4;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2f2;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1fb;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h3a)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h273;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2b0;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f3;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h293;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f2;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2b0;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f3;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h3b)
			(* full_case, parallel_case *)
			case (col)
				0: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				1: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				2: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h255;
					arA23[49:40] = 1'sbx;
				end
				3: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				4: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				5: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2b4;
					arA23[49:40] = 1'sbx;
				end
				6: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f7;
					arA23[49:40] = 1'sbx;
				end
				7: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h297;
					arA23[49:40] = 1'sbx;
				end
				8: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f6;
					arA23[49:40] = 1'sbx;
				end
				9: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h2b4;
					arA23[49:40] = 1'sbx;
				end
				10: begin
					arIll['h4] = 1'b0;
					arA1[49:40] = 'h1f7;
					arA23[49:40] = 1'sbx;
				end
				11: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
				default: begin
					arIll['h4] = 1'b1;
					arA1[49:40] = 1'sbx;
					arA23[49:40] = 1'sbx;
				end
			endcase
		else if (opcode[11:6] == 'h39) begin
			arIll['h4] = illMisc;
			arA1[49:40] = a1Misc;
			arA23[49:40] = 1'sbx;
		end
		else begin
			arIll['h4] = 1'b1;
			arA1[49:40] = 1'sbx;
			arA23[49:40] = 1'sbx;
		end
	end
	always @(*) begin
		if (_sv2v_0)
			;
		(* full_case, parallel_case *)
		case (movEa)
			0:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h121;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h29b;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h29b;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h29b;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h29b;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h29b;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h29b;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h29b;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h29b;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h29b;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h121;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			2:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h2fa;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h3ab;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h3ab;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h3ab;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h3ab;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h3ab;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h3ab;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h3ab;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h3ab;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h3ab;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h2fa;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			3:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h2fe;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h3af;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h3af;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h3af;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h3af;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h3af;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h3af;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h3af;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h3af;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h3af;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h2fe;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			4:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h2f8;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h38b;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h38b;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h38b;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h38b;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h38b;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h38b;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h38b;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h38b;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h38b;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h2f8;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			5:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h2da;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h38a;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h38a;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h38a;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h38a;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h38a;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h38a;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h38a;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h38a;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h38a;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h2da;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			6:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1eb;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h298;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h298;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h298;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h298;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h298;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h298;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h298;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h298;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h298;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h1eb;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			7:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h2d9;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h388;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h388;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h388;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h388;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h388;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h388;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h388;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h388;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h388;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h2d9;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			8:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1ea;
						arA23[19:10] = 1'sbx;
					end
					1: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
					2: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h6;
						arA23[19:10] = 'h32b;
					end
					3: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h21c;
						arA23[19:10] = 'h32b;
					end
					4: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h103;
						arA23[19:10] = 'h32b;
					end
					5: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h32b;
					end
					6: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h32b;
					end
					7: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'ha;
						arA23[19:10] = 'h32b;
					end
					8: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e2;
						arA23[19:10] = 'h32b;
					end
					9: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1c2;
						arA23[19:10] = 'h32b;
					end
					10: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'h1e3;
						arA23[19:10] = 'h32b;
					end
					11: begin
						arIll['h1] = 1'b0;
						arA1[19:10] = 'hea;
						arA23[19:10] = 'h1ea;
					end
					default: begin
						arIll['h1] = 1'b1;
						arA1[19:10] = 1'sbx;
						arA23[19:10] = 1'sbx;
					end
				endcase
			default: begin
				arIll['h1] = 1'b1;
				arA1[19:10] = 1'sbx;
				arA23[19:10] = 1'sbx;
			end
		endcase
		(* full_case, parallel_case *)
		case (movEa)
			0:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h129;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h129;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h29f;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h29f;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h29f;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29f;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29f;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h29f;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h29f;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29f;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29f;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h129;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			1:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h129;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h129;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h29f;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h29f;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h29f;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29f;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29f;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h29f;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h29f;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29f;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29f;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h129;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			2:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2f9;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2f9;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h3a9;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h3a9;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h3a9;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h3a9;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h3a9;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h3a9;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h3a9;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h3a9;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h3a9;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h2f9;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			3:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2fd;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2fd;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h3ad;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h3ad;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h3ad;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h3ad;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h3ad;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h3ad;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h3ad;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h3ad;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h3ad;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h2fd;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			4:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2fc;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2fc;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h38f;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h38f;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h38f;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38f;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38f;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h38f;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h38f;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38f;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38f;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h2fc;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			5:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2de;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2de;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h38e;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h38e;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h38e;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38e;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38e;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h38e;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h38e;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38e;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38e;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h2de;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			6:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1ef;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1ef;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h29c;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h29c;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h29c;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29c;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29c;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h29c;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h29c;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h29c;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h29c;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h1ef;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			7:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2dd;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h2dd;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h38c;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h38c;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h38c;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38c;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38c;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h38c;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h38c;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h38c;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h38c;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h2dd;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			8:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1ee;
						arA23[29:20] = 1'sbx;
					end
					1: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1ee;
						arA23[29:20] = 1'sbx;
					end
					2: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hb;
						arA23[29:20] = 'h30f;
					end
					3: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'hf;
						arA23[29:20] = 'h30f;
					end
					4: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h179;
						arA23[29:20] = 'h30f;
					end
					5: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h30f;
					end
					6: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h30f;
					end
					7: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'he;
						arA23[29:20] = 'h30f;
					end
					8: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e6;
						arA23[29:20] = 'h30f;
					end
					9: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1c6;
						arA23[29:20] = 'h30f;
					end
					10: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'h1e7;
						arA23[29:20] = 'h30f;
					end
					11: begin
						arIll['h2] = 1'b0;
						arA1[29:20] = 'ha7;
						arA23[29:20] = 'h1ee;
					end
					default: begin
						arIll['h2] = 1'b1;
						arA1[29:20] = 1'sbx;
						arA23[29:20] = 1'sbx;
					end
				endcase
			default: begin
				arIll['h2] = 1'b1;
				arA1[29:20] = 1'sbx;
				arA23[29:20] = 1'sbx;
			end
		endcase
		(* full_case, parallel_case *)
		case (movEa)
			0:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h121;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h121;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h29b;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h29b;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h29b;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h29b;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h29b;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h29b;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h29b;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h29b;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h29b;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h121;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			1:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h279;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h279;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h158;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h158;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h158;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h158;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h158;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h158;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h158;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h158;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h158;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h279;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			2:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2fa;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2fa;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h3ab;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h3ab;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h3ab;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h3ab;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h3ab;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h3ab;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h3ab;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h3ab;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h3ab;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h2fa;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			3:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2fe;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2fe;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h3af;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h3af;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h3af;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h3af;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h3af;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h3af;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h3af;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h3af;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h3af;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h2fe;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			4:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2f8;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2f8;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h38b;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h38b;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h38b;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h38b;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h38b;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h38b;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h38b;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h38b;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h38b;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h2f8;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			5:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2da;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2da;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h38a;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h38a;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h38a;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h38a;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h38a;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h38a;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h38a;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h38a;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h38a;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h2da;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			6:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1eb;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1eb;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h298;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h298;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h298;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h298;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h298;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h298;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h298;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h298;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h298;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h1eb;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			7:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2d9;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h2d9;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h388;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h388;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h388;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h388;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h388;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h388;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h388;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h388;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h388;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h2d9;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			8:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1ea;
						arA23[39:30] = 1'sbx;
					end
					1: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1ea;
						arA23[39:30] = 1'sbx;
					end
					2: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h6;
						arA23[39:30] = 'h32b;
					end
					3: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h21c;
						arA23[39:30] = 'h32b;
					end
					4: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h103;
						arA23[39:30] = 'h32b;
					end
					5: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h32b;
					end
					6: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h32b;
					end
					7: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'ha;
						arA23[39:30] = 'h32b;
					end
					8: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e2;
						arA23[39:30] = 'h32b;
					end
					9: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1c2;
						arA23[39:30] = 'h32b;
					end
					10: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'h1e3;
						arA23[39:30] = 'h32b;
					end
					11: begin
						arIll['h3] = 1'b0;
						arA1[39:30] = 'hea;
						arA23[39:30] = 'h1ea;
					end
					default: begin
						arIll['h3] = 1'b1;
						arA1[39:30] = 1'sbx;
						arA23[39:30] = 1'sbx;
					end
				endcase
			default: begin
				arIll['h3] = 1'b1;
				arA1[39:30] = 1'sbx;
				arA23[39:30] = 1'sbx;
			end
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2d8;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h2f3;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h2f3;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h2f3;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h2f3;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h2f3;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h2f3;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h2f3;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2d8;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h2f3;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h2f3;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h2f3;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h2f3;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h2f3;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h2f3;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h2f3;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'hb;
						arA23[59:50] = 'h2f7;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'hf;
						arA23[59:50] = 'h2f7;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h179;
						arA23[59:50] = 'h2f7;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c6;
						arA23[59:50] = 'h2f7;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e7;
						arA23[59:50] = 'h2f7;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'he;
						arA23[59:50] = 'h2f7;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e6;
						arA23[59:50] = 'h2f7;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h384;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6c;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h380;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h380;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h380;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h380;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h380;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h380;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h380;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2d8;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h2f3;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h2f3;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h2f3;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h2f3;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h2f3;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h2f3;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h2f3;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2d8;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h2f3;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h2f3;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h2f3;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h2f3;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h2f3;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h2f3;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h2f3;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h2dc;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'hb;
						arA23[59:50] = 'h2f7;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'hf;
						arA23[59:50] = 'h2f7;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h179;
						arA23[59:50] = 'h2f7;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c6;
						arA23[59:50] = 'h2f7;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e7;
						arA23[59:50] = 'h2f7;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'he;
						arA23[59:50] = 'h2f7;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e6;
						arA23[59:50] = 'h2f7;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h384;
						arA23[59:50] = 1'sbx;
					end
					1: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6c;
						arA23[59:50] = 1'sbx;
					end
					2: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h6;
						arA23[59:50] = 'h380;
					end
					3: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h21c;
						arA23[59:50] = 'h380;
					end
					4: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h103;
						arA23[59:50] = 'h380;
					end
					5: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1c2;
						arA23[59:50] = 'h380;
					end
					6: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e3;
						arA23[59:50] = 'h380;
					end
					7: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'ha;
						arA23[59:50] = 'h380;
					end
					8: begin
						arIll['h5] = 1'b0;
						arA1[59:50] = 'h1e2;
						arA23[59:50] = 'h380;
					end
					default: begin
						arIll['h5] = 1'b1;
						arA1[59:50] = 1'sbx;
						arA23[59:50] = 1'sbx;
					end
				endcase
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c1;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'h1c3;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'h1c3;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'h1c3;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h1c3;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h1c3;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'h1c3;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'h1c3;
					end
					9: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h1c3;
					end
					10: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h1c3;
					end
					11: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hea;
						arA23[89:80] = 'h1c1;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c1;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'h1c3;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'h1c3;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'h1c3;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h1c3;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h1c3;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'h1c3;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'h1c3;
					end
					9: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h1c3;
					end
					10: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h1c3;
					end
					11: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hea;
						arA23[89:80] = 'h1c1;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c5;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hb;
						arA23[89:80] = 'h1cb;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hf;
						arA23[89:80] = 'h1cb;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h179;
						arA23[89:80] = 'h1cb;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c6;
						arA23[89:80] = 'h1cb;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e7;
						arA23[89:80] = 'h1cb;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'he;
						arA23[89:80] = 'h1cb;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e6;
						arA23[89:80] = 'h1cb;
					end
					9: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c6;
						arA23[89:80] = 'h1cb;
					end
					10: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e7;
						arA23[89:80] = 'h1cb;
					end
					11: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha7;
						arA23[89:80] = 'h1c5;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha6;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'ha4;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'ha4;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'ha4;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'ha4;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'ha4;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'ha4;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'ha4;
					end
					9: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'ha4;
					end
					10: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'ha4;
					end
					11: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hea;
						arA23[89:80] = 'ha6;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1cd;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h107;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'h299;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'h299;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'h299;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h299;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h299;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'h299;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'h299;
					end
					9: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					10: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					11: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'h299;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'h299;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'h299;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'h299;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'h299;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'h299;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'h299;
					end
					9: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					10: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					11: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hb;
						arA23[89:80] = 'h29d;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hf;
						arA23[89:80] = 'h29d;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h179;
						arA23[89:80] = 'h29d;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c6;
						arA23[89:80] = 'h29d;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e7;
						arA23[89:80] = 'h29d;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'he;
						arA23[89:80] = 'h29d;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e6;
						arA23[89:80] = 'h29d;
					end
					9: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					10: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					11: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hae;
						arA23[89:80] = 1'sbx;
					end
					1: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
					2: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h6;
						arA23[89:80] = 'hac;
					end
					3: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h21c;
						arA23[89:80] = 'hac;
					end
					4: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h103;
						arA23[89:80] = 'hac;
					end
					5: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'hac;
					end
					6: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'hac;
					end
					7: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'ha;
						arA23[89:80] = 'hac;
					end
					8: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e2;
						arA23[89:80] = 'hac;
					end
					9: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1c2;
						arA23[89:80] = 'hac;
					end
					10: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'h1e3;
						arA23[89:80] = 'hac;
					end
					11: begin
						arIll['h8] = 1'b0;
						arA1[89:80] = 'hea;
						arA23[89:80] = 'hae;
					end
					default: begin
						arIll['h8] = 1'b1;
						arA1[89:80] = 1'sbx;
						arA23[89:80] = 1'sbx;
					end
				endcase
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c1;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h6;
						arA23[99:90] = 'h1c3;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h21c;
						arA23[99:90] = 'h1c3;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h103;
						arA23[99:90] = 'h1c3;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c3;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c3;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha;
						arA23[99:90] = 'h1c3;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e2;
						arA23[99:90] = 'h1c3;
					end
					9: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c3;
					end
					10: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c3;
					end
					11: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hea;
						arA23[99:90] = 'h1c1;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c1;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c1;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h6;
						arA23[99:90] = 'h1c3;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h21c;
						arA23[99:90] = 'h1c3;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h103;
						arA23[99:90] = 'h1c3;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c3;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c3;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha;
						arA23[99:90] = 'h1c3;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e2;
						arA23[99:90] = 'h1c3;
					end
					9: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c3;
					end
					10: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c3;
					end
					11: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hea;
						arA23[99:90] = 'h1c1;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c5;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c5;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hb;
						arA23[99:90] = 'h1cb;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hf;
						arA23[99:90] = 'h1cb;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h179;
						arA23[99:90] = 'h1cb;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c6;
						arA23[99:90] = 'h1cb;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e7;
						arA23[99:90] = 'h1cb;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'he;
						arA23[99:90] = 'h1cb;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e6;
						arA23[99:90] = 'h1cb;
					end
					9: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c6;
						arA23[99:90] = 'h1cb;
					end
					10: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e7;
						arA23[99:90] = 'h1cb;
					end
					11: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha7;
						arA23[99:90] = 'h1c5;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c9;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c9;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h6;
						arA23[99:90] = 'h1c7;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h21c;
						arA23[99:90] = 'h1c7;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h103;
						arA23[99:90] = 'h1c7;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c7;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c7;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha;
						arA23[99:90] = 'h1c7;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e2;
						arA23[99:90] = 'h1c7;
					end
					9: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h1c7;
					end
					10: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h1c7;
					end
					11: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hea;
						arA23[99:90] = 'h1c9;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c1;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h10f;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h6;
						arA23[99:90] = 'h299;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h21c;
						arA23[99:90] = 'h299;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h103;
						arA23[99:90] = 'h299;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h299;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h299;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha;
						arA23[99:90] = 'h299;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e2;
						arA23[99:90] = 'h299;
					end
					9: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					10: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					11: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c1;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h10f;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h6;
						arA23[99:90] = 'h299;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h21c;
						arA23[99:90] = 'h299;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h103;
						arA23[99:90] = 'h299;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c2;
						arA23[99:90] = 'h299;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e3;
						arA23[99:90] = 'h299;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha;
						arA23[99:90] = 'h299;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e2;
						arA23[99:90] = 'h299;
					end
					9: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					10: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					11: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c5;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h10b;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hb;
						arA23[99:90] = 'h29d;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hf;
						arA23[99:90] = 'h29d;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h179;
						arA23[99:90] = 'h29d;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c6;
						arA23[99:90] = 'h29d;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e7;
						arA23[99:90] = 'h29d;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'he;
						arA23[99:90] = 'h29d;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e6;
						arA23[99:90] = 'h29d;
					end
					9: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					10: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					11: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c5;
						arA23[99:90] = 1'sbx;
					end
					1: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c5;
						arA23[99:90] = 1'sbx;
					end
					2: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hb;
						arA23[99:90] = 'h1cb;
					end
					3: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'hf;
						arA23[99:90] = 'h1cb;
					end
					4: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h179;
						arA23[99:90] = 'h1cb;
					end
					5: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c6;
						arA23[99:90] = 'h1cb;
					end
					6: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e7;
						arA23[99:90] = 'h1cb;
					end
					7: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'he;
						arA23[99:90] = 'h1cb;
					end
					8: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e6;
						arA23[99:90] = 'h1cb;
					end
					9: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1c6;
						arA23[99:90] = 'h1cb;
					end
					10: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'h1e7;
						arA23[99:90] = 'h1cb;
					end
					11: begin
						arIll['h9] = 1'b0;
						arA1[99:90] = 'ha7;
						arA23[99:90] = 'h1c5;
					end
					default: begin
						arIll['h9] = 1'b1;
						arA1[99:90] = 1'sbx;
						arA23[99:90] = 1'sbx;
					end
				endcase
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d1;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6;
						arA23[119:110] = 'h1d3;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h21c;
						arA23[119:110] = 'h1d3;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h103;
						arA23[119:110] = 'h1d3;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1d3;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1d3;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha;
						arA23[119:110] = 'h1d3;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e2;
						arA23[119:110] = 'h1d3;
					end
					9: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1d3;
					end
					10: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1d3;
					end
					11: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hea;
						arA23[119:110] = 'h1d1;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d1;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d1;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6;
						arA23[119:110] = 'h1d3;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h21c;
						arA23[119:110] = 'h1d3;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h103;
						arA23[119:110] = 'h1d3;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1d3;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1d3;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha;
						arA23[119:110] = 'h1d3;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e2;
						arA23[119:110] = 'h1d3;
					end
					9: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1d3;
					end
					10: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1d3;
					end
					11: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hea;
						arA23[119:110] = 'h1d1;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d5;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d5;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hb;
						arA23[119:110] = 'h1d7;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hf;
						arA23[119:110] = 'h1d7;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h179;
						arA23[119:110] = 'h1d7;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c6;
						arA23[119:110] = 'h1d7;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e7;
						arA23[119:110] = 'h1d7;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'he;
						arA23[119:110] = 'h1d7;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e6;
						arA23[119:110] = 'h1d7;
					end
					9: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c6;
						arA23[119:110] = 'h1d7;
					end
					10: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e7;
						arA23[119:110] = 'h1d7;
					end
					11: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha7;
						arA23[119:110] = 'h1d5;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d9;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d9;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6;
						arA23[119:110] = 'h1cf;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h21c;
						arA23[119:110] = 'h1cf;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h103;
						arA23[119:110] = 'h1cf;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1cf;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1cf;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha;
						arA23[119:110] = 'h1cf;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e2;
						arA23[119:110] = 'h1cf;
					end
					9: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h1cf;
					end
					10: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h1cf;
					end
					11: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hea;
						arA23[119:110] = 'h1d9;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h100;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6b;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6;
						arA23[119:110] = 'h299;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h21c;
						arA23[119:110] = 'h299;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h103;
						arA23[119:110] = 'h299;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h299;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h299;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha;
						arA23[119:110] = 'h299;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e2;
						arA23[119:110] = 'h299;
					end
					9: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					10: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					11: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h100;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6b;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6;
						arA23[119:110] = 'h299;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h21c;
						arA23[119:110] = 'h299;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h103;
						arA23[119:110] = 'h299;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c2;
						arA23[119:110] = 'h299;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e3;
						arA23[119:110] = 'h299;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha;
						arA23[119:110] = 'h299;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e2;
						arA23[119:110] = 'h299;
					end
					9: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					10: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					11: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h10c;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h6f;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hb;
						arA23[119:110] = 'h29d;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hf;
						arA23[119:110] = 'h29d;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h179;
						arA23[119:110] = 'h29d;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c6;
						arA23[119:110] = 'h29d;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e7;
						arA23[119:110] = 'h29d;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'he;
						arA23[119:110] = 'h29d;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e6;
						arA23[119:110] = 'h29d;
					end
					9: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					10: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					11: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d5;
						arA23[119:110] = 1'sbx;
					end
					1: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1d5;
						arA23[119:110] = 1'sbx;
					end
					2: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hb;
						arA23[119:110] = 'h1d7;
					end
					3: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'hf;
						arA23[119:110] = 'h1d7;
					end
					4: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h179;
						arA23[119:110] = 'h1d7;
					end
					5: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c6;
						arA23[119:110] = 'h1d7;
					end
					6: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e7;
						arA23[119:110] = 'h1d7;
					end
					7: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'he;
						arA23[119:110] = 'h1d7;
					end
					8: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e6;
						arA23[119:110] = 'h1d7;
					end
					9: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1c6;
						arA23[119:110] = 'h1d7;
					end
					10: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'h1e7;
						arA23[119:110] = 'h1d7;
					end
					11: begin
						arIll['hb] = 1'b0;
						arA1[119:110] = 'ha7;
						arA23[119:110] = 'h1d5;
					end
					default: begin
						arIll['hb] = 1'b1;
						arA1[119:110] = 1'sbx;
						arA23[119:110] = 1'sbx;
					end
				endcase
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c1;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h1c3;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h1c3;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h1c3;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h1c3;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h1c3;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h1c3;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h1c3;
					end
					9: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h1c3;
					end
					10: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h1c3;
					end
					11: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hea;
						arA23[129:120] = 'h1c1;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c1;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h1c3;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h1c3;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h1c3;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h1c3;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h1c3;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h1c3;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h1c3;
					end
					9: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h1c3;
					end
					10: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h1c3;
					end
					11: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hea;
						arA23[129:120] = 'h1c1;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c5;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hb;
						arA23[129:120] = 'h1cb;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hf;
						arA23[129:120] = 'h1cb;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h179;
						arA23[129:120] = 'h1cb;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c6;
						arA23[129:120] = 'h1cb;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e7;
						arA23[129:120] = 'h1cb;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'he;
						arA23[129:120] = 'h1cb;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e6;
						arA23[129:120] = 'h1cb;
					end
					9: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c6;
						arA23[129:120] = 'h1cb;
					end
					10: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e7;
						arA23[129:120] = 'h1cb;
					end
					11: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha7;
						arA23[129:120] = 'h1c5;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h15b;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h15a;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h15a;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h15a;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h15a;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h15a;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h15a;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h15a;
					end
					9: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h15a;
					end
					10: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h15a;
					end
					11: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hea;
						arA23[129:120] = 'h15b;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1cd;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h107;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h299;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h299;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h299;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h299;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h299;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h299;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h299;
					end
					9: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					10: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					11: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h3e3;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h3e3;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h299;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h299;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h299;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h299;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h299;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h299;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h299;
					end
					9: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					10: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					11: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h3e3;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hb;
						arA23[129:120] = 'h29d;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hf;
						arA23[129:120] = 'h29d;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h179;
						arA23[129:120] = 'h29d;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c6;
						arA23[129:120] = 'h29d;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e7;
						arA23[129:120] = 'h29d;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'he;
						arA23[129:120] = 'h29d;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e6;
						arA23[129:120] = 'h29d;
					end
					9: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					10: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					11: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h15b;
						arA23[129:120] = 1'sbx;
					end
					1: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
					2: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h6;
						arA23[129:120] = 'h15a;
					end
					3: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h21c;
						arA23[129:120] = 'h15a;
					end
					4: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h103;
						arA23[129:120] = 'h15a;
					end
					5: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h15a;
					end
					6: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h15a;
					end
					7: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'ha;
						arA23[129:120] = 'h15a;
					end
					8: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e2;
						arA23[129:120] = 'h15a;
					end
					9: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1c2;
						arA23[129:120] = 'h15a;
					end
					10: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'h1e3;
						arA23[129:120] = 'h15a;
					end
					11: begin
						arIll['hc] = 1'b0;
						arA1[129:120] = 'hea;
						arA23[129:120] = 'h15b;
					end
					default: begin
						arIll['hc] = 1'b1;
						arA1[129:120] = 1'sbx;
						arA23[129:120] = 1'sbx;
					end
				endcase
		endcase
		(* full_case, parallel_case *)
		case (row86)
			3'b000:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c1;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h6;
						arA23[139:130] = 'h1c3;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h21c;
						arA23[139:130] = 'h1c3;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h103;
						arA23[139:130] = 'h1c3;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c3;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c3;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha;
						arA23[139:130] = 'h1c3;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e2;
						arA23[139:130] = 'h1c3;
					end
					9: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c3;
					end
					10: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c3;
					end
					11: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hea;
						arA23[139:130] = 'h1c1;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b001:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c1;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c1;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h6;
						arA23[139:130] = 'h1c3;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h21c;
						arA23[139:130] = 'h1c3;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h103;
						arA23[139:130] = 'h1c3;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c3;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c3;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha;
						arA23[139:130] = 'h1c3;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e2;
						arA23[139:130] = 'h1c3;
					end
					9: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c3;
					end
					10: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c3;
					end
					11: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hea;
						arA23[139:130] = 'h1c1;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b010:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c5;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c5;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hb;
						arA23[139:130] = 'h1cb;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hf;
						arA23[139:130] = 'h1cb;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h179;
						arA23[139:130] = 'h1cb;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c6;
						arA23[139:130] = 'h1cb;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e7;
						arA23[139:130] = 'h1cb;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'he;
						arA23[139:130] = 'h1cb;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e6;
						arA23[139:130] = 'h1cb;
					end
					9: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c6;
						arA23[139:130] = 'h1cb;
					end
					10: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e7;
						arA23[139:130] = 'h1cb;
					end
					11: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha7;
						arA23[139:130] = 'h1c5;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b011:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c9;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c9;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h6;
						arA23[139:130] = 'h1c7;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h21c;
						arA23[139:130] = 'h1c7;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h103;
						arA23[139:130] = 'h1c7;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c7;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c7;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha;
						arA23[139:130] = 'h1c7;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e2;
						arA23[139:130] = 'h1c7;
					end
					9: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h1c7;
					end
					10: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h1c7;
					end
					11: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hea;
						arA23[139:130] = 'h1c9;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b100:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c1;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h10f;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h6;
						arA23[139:130] = 'h299;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h21c;
						arA23[139:130] = 'h299;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h103;
						arA23[139:130] = 'h299;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h299;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h299;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha;
						arA23[139:130] = 'h299;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e2;
						arA23[139:130] = 'h299;
					end
					9: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					10: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					11: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b101:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c1;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h10f;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h6;
						arA23[139:130] = 'h299;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h21c;
						arA23[139:130] = 'h299;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h103;
						arA23[139:130] = 'h299;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c2;
						arA23[139:130] = 'h299;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e3;
						arA23[139:130] = 'h299;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha;
						arA23[139:130] = 'h299;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e2;
						arA23[139:130] = 'h299;
					end
					9: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					10: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					11: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b110:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c5;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h10b;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hb;
						arA23[139:130] = 'h29d;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hf;
						arA23[139:130] = 'h29d;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h179;
						arA23[139:130] = 'h29d;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c6;
						arA23[139:130] = 'h29d;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e7;
						arA23[139:130] = 'h29d;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'he;
						arA23[139:130] = 'h29d;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e6;
						arA23[139:130] = 'h29d;
					end
					9: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					10: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					11: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
			3'b111:
				(* full_case, parallel_case *)
				case (col)
					0: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c5;
						arA23[139:130] = 1'sbx;
					end
					1: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c5;
						arA23[139:130] = 1'sbx;
					end
					2: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hb;
						arA23[139:130] = 'h1cb;
					end
					3: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'hf;
						arA23[139:130] = 'h1cb;
					end
					4: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h179;
						arA23[139:130] = 'h1cb;
					end
					5: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c6;
						arA23[139:130] = 'h1cb;
					end
					6: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e7;
						arA23[139:130] = 'h1cb;
					end
					7: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'he;
						arA23[139:130] = 'h1cb;
					end
					8: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e6;
						arA23[139:130] = 'h1cb;
					end
					9: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1c6;
						arA23[139:130] = 'h1cb;
					end
					10: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'h1e7;
						arA23[139:130] = 'h1cb;
					end
					11: begin
						arIll['hd] = 1'b0;
						arA1[139:130] = 'ha7;
						arA23[139:130] = 'h1c5;
					end
					default: begin
						arIll['hd] = 1'b1;
						arA1[139:130] = 1'sbx;
						arA23[139:130] = 1'sbx;
					end
				endcase
		endcase
	end
	initial _sv2v_0 = 0;
endmodule
module nanomig (
	clk_sys,
	reset,
	por,
	clk7_en,
	clk7n_en,
	cpu_nrst_out,
	pwr_led,
	fdd_led,
	fdd_step_n,
	hs,
	vs,
	r,
	g,
	b,
	memory_config,
	fastram_config,
	cpu_config,
	chipset_config,
	floppy_config,
	video_config,
	turbo_config,
	ide_config,
	hdd_led,
	vid_r,
	vid_g,
	vid_b,
	vid_hs,
	vid_vs,
	vid_hbl,
	vid_vbl,
	vid_ce,
	vid_res,
	vid_field,
	vid_lace,
	audio_left,
	audio_right,
	mouse_buttons,
	kbd_mouse_level,
	kbd_mouse_type,
	kbd_mouse_data,
	joystick0,
	joystick1,
	keyboard_ack,
	uart_tx,
	uart_rx,
	serial_status,
	serial_tx_available,
	serial_tx_strobe,
	serial_tx_data,
	serial_rx_available,
	serial_rx_strobe,
	serial_rx_data,
	serial_serper,
	ser_rts_n,
	ser_dtr_n,
	ser_cts_n,
	ser_dsr_n,
	ser_cd_n,
	ser_ri_n,
	rtc,
	sdc_img_mounted,
	sdc_img_size,
	sdc_rd,
	sdc_wr,
	sdc_sector,
	sdc_busy,
	sdc_done,
	sdc_byte_in_strobe,
	sdc_byte_addr,
	sdc_byte_in_data,
	sdc_byte_out_data,
	ram_data,
	ramdata_in,
	chip48,
	refresh,
	ram_address,
	_ram_bhe,
	_ram_ble,
	_ram_we,
	_ram_oe,
	fastram_sel,
	fastram_addr,
	fastram_lds,
	fastram_uds,
	fastram_dout,
	fastram_dout48,
	fastram_din,
	fastram_wr,
	fastram_ready,
	cpu_gap
);
	input clk_sys;
	input reset;
	input por;
	output wire clk7_en;
	output wire clk7n_en;
	output wire cpu_nrst_out;
	output wire pwr_led;
	output wire fdd_led;
	output wire fdd_step_n;
	output wire hs;
	output wire vs;
	output wire [3:0] r;
	output wire [3:0] g;
	output wire [3:0] b;
	input [7:0] memory_config;
	input [2:0] fastram_config;
	input [1:0] cpu_config;
	input [5:0] chipset_config;
	input [3:0] floppy_config;
	input [3:0] video_config;
	input [2:0] turbo_config;
	input [5:0] ide_config;
	output wire hdd_led;
	output wire [7:0] vid_r;
	output wire [7:0] vid_g;
	output wire [7:0] vid_b;
	output wire vid_hs;
	output wire vid_vs;
	output wire vid_hbl;
	output wire vid_vbl;
	output wire vid_ce;
	output wire [1:0] vid_res;
	output wire vid_field;
	output wire vid_lace;
	output wire [14:0] audio_left;
	output wire [14:0] audio_right;
	input [2:0] mouse_buttons;
	input kbd_mouse_level;
	input [1:0] kbd_mouse_type;
	input [7:0] kbd_mouse_data;
	input [7:0] joystick0;
	input [7:0] joystick1;
	output wire keyboard_ack;
	output wire uart_tx;
	input uart_rx;
	output wire [31:0] serial_status;
	output wire [7:0] serial_tx_available;
	input serial_tx_strobe;
	output wire [7:0] serial_tx_data;
	output wire [7:0] serial_rx_available;
	input serial_rx_strobe;
	input [7:0] serial_rx_data;
	output wire [14:0] serial_serper;
	output wire ser_rts_n;
	output wire ser_dtr_n;
	input ser_cts_n;
	input ser_dsr_n;
	input ser_cd_n;
	input ser_ri_n;
	input [11:0] rtc;
	input [7:0] sdc_img_mounted;
	input [63:0] sdc_img_size;
	output wire [7:0] sdc_rd;
	output wire [7:0] sdc_wr;
	output wire [31:0] sdc_sector;
	input sdc_busy;
	input sdc_done;
	input sdc_byte_in_strobe;
	input [8:0] sdc_byte_addr;
	input [7:0] sdc_byte_in_data;
	output wire [7:0] sdc_byte_out_data;
	output wire [15:0] ram_data;
	input [15:0] ramdata_in;
	input [47:0] chip48;
	output wire refresh;
	output wire [23:1] ram_address;
	output wire _ram_bhe;
	output wire _ram_ble;
	output wire _ram_we;
	output wire _ram_oe;
	output wire fastram_sel;
	output wire [23:1] fastram_addr;
	output wire fastram_lds;
	output wire fastram_uds;
	input [15:0] fastram_dout;
	input [47:0] fastram_dout48;
	output wire [15:0] fastram_din;
	output wire fastram_wr;
	input fastram_ready;
	input cpu_gap;
	wire cpu_rst;
	wire [15:0] ram_din;
	reg reset_d;
	always @(posedge clk_sys or posedge reset) begin : sv2v_autoblock_1
		reg [7:0] reset_s;
		reg rs;
		if (reset) begin
			reset_s <= 8'd1;
			reset_d <= 1'b1;
		end
		else begin
			reset_s <= reset_s << 1;
			rs <= |reset_s;
			reset_d <= rs;
		end
	end
	wire c1;
	wire c3;
	wire cck;
	wire [9:0] eclk;
	amiga_clk amiga_clk(
		.clk_28(clk_sys),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.c1(c1),
		.c3(c3),
		.cck(cck),
		.eclk(eclk),
		.reset_n(~por)
	);
	reg cpu_ph1;
	reg cpu_ph2;
	reg cpu_sync;
	always @(posedge clk_sys)
		if (~cpu_rst)
			cpu_sync <= 1'b0;
		else if (c1 && c3)
			cpu_sync <= 1'b1;
	always @(*)
		if (~cpu_rst) begin
			cpu_ph1 = 1'b0;
			cpu_ph2 = 1'b0;
		end
		else begin
			cpu_ph1 = (!c1 && c3) && cpu_sync;
			cpu_ph2 = (c1 && !c3) && cpu_sync;
		end
	wire [1:0] cpu_state;
	wire cpu_clkena;
	wire [3:0] cpu_cacr;
	wire [31:0] cpu_nmi_addr;
	wire [2:0] chip_ipl;
	wire chip_dtack;
	wire chip_as;
	wire chip_uds;
	wire chip_lds;
	wire chip_rw;
	wire [15:0] chip_dout;
	wire [15:0] chip_din;
	wire [23:1] chip_addr;
	wire ovl;
	wire [1:0] cpucfg = cpu_config;
	wire [1:0] chipram_config = memory_config[1:0];
	wire [1:0] slowram_config = memory_config[3:2];
	wire [2:0] turbocfg = turbo_config & {2'b11, ~ovl};
	wire pwr_led_bright;
	reg [1:0] pwr_led_cnt;
	assign pwr_led = (pwr_led_bright ? 1'b1 : !pwr_led_cnt);
	always @(negedge clk_sys) pwr_led_cnt <= pwr_led_cnt + 2'd1;
	wire [15:0] ram_dout;
	wire [28:1] ram_addr;
	wire ram_sel;
	wire ram_lds;
	wire ram_uds;
	reg fastram_ready_d;
	wire ram_ready = cpu_ph1 && (fastram_ready != fastram_ready_d);
	assign fastram_sel = ram_sel && (fastram_ready == fastram_ready_d);
	always @(posedge clk_sys)
		if (!cpu_rst || cpu_ph1)
			fastram_ready_d <= fastram_ready;
	cpu_wrapper cpu_wrapper(
		.reset(cpu_rst),
		.reset_out(cpu_nrst_out),
		.clk(clk_sys),
		.gap(cpu_gap),
		.ph1(cpu_ph1),
		.ph2(cpu_ph2),
		.chip_addr(chip_addr),
		.chip_dout(chip_dout),
		.chip_din(chip_din),
		.chip_as(chip_as),
		.chip_uds(chip_uds),
		.chip_lds(chip_lds),
		.chip_rw(chip_rw),
		.chip_dtack(chip_dtack),
		.chip_ipl(chip_ipl),
		.cpucfg(cpucfg),
		.turbocfg(turbocfg),
		.fastramcfg(fastram_config),
		.chipramcfg(chipram_config),
		.slowramcfg(slowram_config),
		.bootrom(1'b0),
		.ramreq(ram_sel),
		.ramaddr(ram_addr),
		.ramlds(ram_lds),
		.ramuds(ram_uds),
		.ramdout(ram_dout),
		.ramdin(ram_din),
		.ramready(ram_ready),
		.ramshared(),
		.cpustate(cpu_state),
		.cpu_clkena(cpu_clkena),
		.cacr(cpu_cacr),
		.nmi_addr(cpu_nmi_addr)
	);
	assign fastram_addr = ram_addr[23:1];
	assign fastram_lds = ram_lds;
	assign fastram_uds = ram_uds;
	assign fastram_din = ram_din;
	assign fastram_wr = (cpu_state[1:0] == 2'b11 ? 1'b1 : 1'b0);
	assign ram_dout = fastram_dout;
	wire [7:0] sdc_byte_out_data_fdc;
	wire [31:0] sdc_sector_fdc;
	reg [1:0] ide_drv_state [0:1];
	localparam IDE_DRV_STATE_NONE = 2'd0;
	localparam IDE_DRV_STATE_MNT = 2'd1;
	localparam IDE_DRV_STATE_PARSE = 2'd2;
	localparam IDE_DRV_STATE_PRESENT = 2'd3;
	wire [1:0] ide_present = {ide_drv_state[1] == IDE_DRV_STATE_PRESENT, ide_drv_state[0] == IDE_DRV_STATE_PRESENT};
	reg ide_busy;
	reg [3:0] ide_exec;
	localparam IDE_EXEC_IDLE = 4'd0;
	localparam IDE_EXEC_SET_CONFIG = 4'd1;
	localparam IDE_EXEC_SET_REGS = 4'd2;
	localparam IDE_EXEC_GET_REGS = 4'd3;
	localparam IDE_EXEC_SEND_IDENTIFY = 4'd4;
	localparam IDE_EXEC_READ_SECTOR = 4'd5;
	localparam IDE_EXEC_SEND_PAYLOAD = 4'd6;
	localparam IDE_EXEC_WRITE_SECTOR = 4'd7;
	localparam IDE_EXEC_RECV_PAYLOAD = 4'd8;
	reg [8:0] ide_exec_cnt;
	reg [7:0] ide_cmd;
	reg [7:0] ide_status;
	reg [7:0] ide_error;
	reg [7:0] ide_spb;
	reg [15:0] ide_cylinder;
	reg [7:0] ide_sector;
	reg [3:0] ide_head;
	reg [7:0] ide_sector_cnt;
	reg [7:0] ide_io_size;
	reg ide_io_done;
	reg ide_io_fast;
	reg [7:0] ide_features;
	reg ide_drv;
	reg [7:0] ide_sdc_cnt;
	reg [1:0] ide_sdc_rd;
	reg [1:0] ide_sdc_wr;
	reg [31:0] ide_sdc_sector;
	reg [1:0] ide_reported;
	assign sdc_sector = (ide_sdc_rd || ide_sdc_wr ? ide_sdc_sector : sdc_sector_fdc);
	assign sdc_rd[7:4] = {2'b00, ide_sdc_rd};
	assign sdc_wr[7:4] = {2'b00, ide_sdc_wr};
	localparam DRIVES = 2;
	reg [15:0] cylinders [0:1];
	reg [15:0] sectors [0:1];
	reg [15:0] heads [0:1];
	reg [31:0] total_sectors [0:1];
	reg [1:0] lba_st = 2'd0;
	reg lba_wr;
	reg lba_drv;
	reg [15:0] l_cyl;
	reg [3:0] l_head;
	reg [7:0] l_sec;
	reg [7:0] l_s;
	reg [4:0] l_h;
	reg [20:0] l_ch;
	reg [28:0] l_lba;
	always @(posedge clk_sys) begin : sv2v_autoblock_2
		integer drv;
		for (drv = 0; drv < DRIVES; drv = drv + 1)
			begin
				if (sdc_img_mounted[4 + drv]) begin
					if (!sdc_img_size) begin
						if (sdc_img_mounted[4 + drv])
							ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
					end
					else if (sdc_img_mounted[4 + drv] && (ide_drv_state[drv] == IDE_DRV_STATE_NONE)) begin
						$display("HDD%0d: Total sector size: %0d", drv, sdc_img_size[40:9]);
						total_sectors[drv] <= sdc_img_size[40:9];
						ide_drv_state[drv] <= IDE_DRV_STATE_MNT;
					end
				end
				if ((!ide_sdc_rd && !sdc_busy) && (lba_st == 2'd0)) begin
					if (ide_drv_state[drv] == IDE_DRV_STATE_MNT) begin
						ide_sdc_sector <= 32'd0;
						ide_sdc_rd[drv] <= 1'b1;
					end
				end
				case (lba_st)
					2'd1: begin
						l_ch <= (l_cyl * l_h) + l_head;
						lba_st <= 2'd2;
					end
					2'd2: begin
						l_lba <= ((l_ch * l_s) + l_sec) - 8'd1;
						lba_st <= 2'd3;
					end
					2'd3:
						if (!sdc_busy) begin
							ide_sdc_sector <= {3'd0, l_lba};
							if (lba_wr)
								ide_sdc_wr[lba_drv] <= 1'b1;
							else
								ide_sdc_rd[lba_drv] <= 1'b1;
							lba_st <= 2'd0;
						end
					default:
						;
				endcase
				if (((!sdc_busy && !ide_sdc_rd) && (ide_exec == IDE_EXEC_READ_SECTOR)) && (lba_st == 2'd0)) begin
					l_cyl <= ide_cylinder;
					l_head <= ide_head;
					l_sec <= ide_sector;
					l_h <= heads[ide_drv][4:0];
					l_s <= sectors[ide_drv][7:0];
					lba_drv <= ide_drv;
					lba_wr <= 1'b0;
					lba_st <= 2'd1;
					$display("IDE%0d RD %0d/%0d/%0d -> %0d", ide_drv, ide_cylinder, ide_head, ide_sector, ((((ide_cylinder * heads[0]) + ide_head) * sectors[0]) + ide_sector) - 1);
				end
				if (((!sdc_busy && !ide_sdc_wr) && (ide_exec == IDE_EXEC_WRITE_SECTOR)) && (lba_st == 2'd0)) begin
					l_cyl <= ide_cylinder;
					l_head <= ide_head;
					l_sec <= ide_sector;
					l_h <= heads[ide_drv][4:0];
					l_s <= sectors[ide_drv][7:0];
					lba_drv <= ide_drv;
					lba_wr <= 1'b1;
					lba_st <= 2'd1;
					$display("IDE%0d WR %0d/%0d/%0d -> %0d", ide_drv, ide_cylinder, ide_head, ide_sector, ((((ide_cylinder * heads[0]) + ide_head) * sectors[0]) + ide_sector) - 1);
				end
				if (ide_sdc_rd && sdc_busy) begin
					ide_sdc_rd <= 2'b00;
					if (ide_exec != IDE_EXEC_READ_SECTOR) begin
						if (ide_sdc_rd[drv])
							ide_drv_state[drv] <= IDE_DRV_STATE_PARSE;
					end
				end
				if (ide_sdc_wr && sdc_busy)
					ide_sdc_wr <= 2'b00;
				if ((ide_drv_state[drv] == IDE_DRV_STATE_PARSE) && sdc_byte_in_strobe)
					case (sdc_byte_addr)
						0:
							if (sdc_byte_in_data != "R")
								ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
						1:
							if (sdc_byte_in_data != "D")
								ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
						2:
							if (sdc_byte_in_data != "S")
								ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
						3:
							if (sdc_byte_in_data != "K")
								ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
						66: cylinders[drv][15:8] <= sdc_byte_in_data;
						67: cylinders[drv][7:0] <= sdc_byte_in_data;
						70: sectors[drv][15:8] <= sdc_byte_in_data;
						71: sectors[drv][7:0] <= sdc_byte_in_data;
						74: heads[drv][15:8] <= sdc_byte_in_data;
						75: heads[drv][7:0] <= sdc_byte_in_data;
						511: begin
							ide_drv_state[drv] <= IDE_DRV_STATE_PRESENT;
							$display("IDE%0d CHS %0d/%0d/%0d", drv, cylinders[drv], heads[drv], sectors[drv]);
						end
					endcase
			end
		if (por)
			for (drv = 0; drv < DRIVES; drv = drv + 1)
				ide_drv_state[drv] <= IDE_DRV_STATE_NONE;
	end
	wire [5:0] ide_request;
	reg [2:0] ide_req_q;
	reg ide_rst_pend;
	wire [15:0] ide_writedata;
	wire [15:0] ide_readdata;
	always @(posedge clk_sys or posedge reset)
		if (reset) begin
			ide_busy <= 1'b0;
			ide_exec <= IDE_EXEC_IDLE;
			ide_cmd <= 8'h00;
			ide_io_done <= 1'b0;
			ide_io_fast <= 1'b0;
			ide_features <= 8'h00;
			ide_reported <= 2'b00;
			ide_req_q <= 3'b110;
			ide_rst_pend <= 1'b0;
			ide_spb <= 8'd16;
			ide_error <= 8'h00;
			ide_status <= 8'h00;
			ide_drv <= 1'b0;
			ide_cylinder <= 16'd0;
			ide_sector <= 8'd1;
			ide_sector_cnt <= 8'd0;
			ide_io_size <= 8'd1;
		end
		else begin
			if (!ide_busy) begin
				if (ide_reported != ide_present) begin
					ide_exec <= IDE_EXEC_SET_CONFIG;
					ide_exec_cnt <= 9'd0;
					if (ide_present)
						ide_status <= 8'b01000000;
					else
						ide_status <= 8'b00000000;
					ide_reported <= ide_present;
				end
				if (ide_request[2:0] == 3'b100) begin
					ide_status <= 8'h00;
					ide_error <= 8'h00;
					ide_busy <= 1'b1;
					ide_exec <= IDE_EXEC_GET_REGS;
					ide_exec_cnt <= 9'd0;
				end
				if (ide_request[2:0] == 3'b101) begin
					if (ide_cmd == 8'hc4) begin
						ide_exec <= IDE_EXEC_READ_SECTOR;
						ide_exec_cnt <= 9'd0;
						ide_busy <= 1'b1;
						if (ide_sector_cnt < ide_spb) begin
							ide_sdc_cnt <= ide_sector_cnt;
							ide_io_size <= ide_sector_cnt;
						end
						else begin
							ide_sdc_cnt <= ide_spb;
							ide_io_size <= ide_spb;
						end
					end
					else begin
						ide_exec <= IDE_EXEC_WRITE_SECTOR;
						ide_busy <= 1'b1;
					end
				end
			end
			case (ide_exec)
				IDE_EXEC_SET_CONFIG:
					if (ide_exec_cnt != 9'h001)
						ide_exec_cnt <= ide_exec_cnt + 9'd1;
					else begin
						ide_exec <= IDE_EXEC_SET_REGS;
						ide_exec_cnt <= 9'd0;
					end
				IDE_EXEC_SEND_PAYLOAD:
					if (sdc_byte_in_strobe && (sdc_byte_addr == 9'd511)) begin
						ide_sector_cnt <= ide_sector_cnt - 8'd1;
						if (ide_sector < sectors[0])
							ide_sector <= ide_sector + 8'd1;
						else begin
							ide_sector <= 8'd1;
							if (ide_head < (heads[0] - 1))
								ide_head <= ide_head + 8'd1;
							else begin
								ide_head <= 8'd0;
								ide_cylinder <= ide_cylinder + 16'd1;
							end
						end
						ide_sdc_cnt <= ide_sdc_cnt - 8'd1;
						if (ide_sdc_cnt <= 1) begin
							ide_status[3:2] <= 2'b11;
							if (ide_sector_cnt <= 1)
								ide_status[1] <= 1'b1;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
						else begin
							ide_exec <= IDE_EXEC_READ_SECTOR;
							ide_exec_cnt <= 9'd0;
						end
					end
				IDE_EXEC_SEND_IDENTIFY:
					if (ide_exec_cnt != 9'h1ff)
						ide_exec_cnt <= ide_exec_cnt + 9'd1;
					else begin
						ide_status[3:0] <= 4'b1110;
						ide_exec <= IDE_EXEC_SET_REGS;
						ide_exec_cnt <= 9'd0;
					end
				IDE_EXEC_READ_SECTOR:
					if (ide_sdc_rd && sdc_busy)
						ide_exec <= IDE_EXEC_SEND_PAYLOAD;
				IDE_EXEC_WRITE_SECTOR:
					if (ide_sdc_wr && sdc_busy) begin
						ide_exec <= IDE_EXEC_RECV_PAYLOAD;
						ide_exec_cnt <= 9'd0;
					end
				IDE_EXEC_RECV_PAYLOAD:
					if (sdc_done) begin
						ide_sector_cnt <= ide_sector_cnt - 8'd1;
						if (ide_sector < sectors[ide_drv])
							ide_sector <= ide_sector + 8'd1;
						else begin
							ide_sector <= 8'd1;
							if (ide_head < (heads[ide_drv] - 1))
								ide_head <= ide_head + 8'd1;
							else begin
								ide_head <= 8'd0;
								ide_cylinder <= ide_cylinder + 16'd1;
							end
						end
						ide_sdc_cnt <= ide_sdc_cnt - 8'd1;
						if (ide_sdc_cnt <= 1) begin
							if (ide_sector_cnt > 1) begin
								ide_status[3] <= 1'b1;
								if ((ide_sector_cnt - 1) < ide_spb) begin
									ide_sdc_cnt <= ide_sector_cnt - 1;
									ide_io_size <= ide_sector_cnt - 1;
								end
								else begin
									ide_sdc_cnt <= ide_spb;
									ide_io_size <= ide_spb;
								end
							end
							else
								ide_status[3] <= 1'b0;
							ide_status[3] <= ide_sector_cnt > 1;
							ide_status[2] <= 1'b1;
							ide_status[6] <= 1'b1;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
						else begin
							ide_exec <= IDE_EXEC_WRITE_SECTOR;
							ide_busy <= 1'b1;
						end
					end
				IDE_EXEC_GET_REGS: begin
					if (ide_exec_cnt[0])
						case (ide_exec_cnt[5:1])
							0: begin
								ide_features <= ide_readdata[15:8];
								ide_io_fast <= ide_readdata[1];
								ide_io_done <= ide_readdata[0];
							end
							1: {ide_sector, ide_sector_cnt} <= ide_readdata;
							2: ide_cylinder <= ide_readdata;
							5: begin
								ide_cmd <= ide_readdata[15:8];
								ide_drv <= ide_readdata[4];
								ide_head <= ide_readdata[3:0];
							end
						endcase
					if (ide_exec_cnt != 9'h00b)
						ide_exec_cnt <= ide_exec_cnt + 9'd1;
					else if ((!ide_readdata[4] && ide_present[0]) || (ide_readdata[4] && ide_present[1])) begin
						ide_status <= 8'b01000000;
						if (ide_readdata[15:12] == 4'h1) begin
							ide_status[2] <= 1'b1;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:8] == 8'hec) begin
							ide_exec <= IDE_EXEC_SEND_IDENTIFY;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:12] == 4'h2) begin
							ide_sdc_cnt <= 8'd1;
							ide_io_size <= 8'd1;
							ide_exec <= IDE_EXEC_READ_SECTOR;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:8] == 8'hc4) begin
							if (ide_sector_cnt < ide_spb) begin
								ide_sdc_cnt <= ide_sector_cnt;
								ide_io_size <= ide_sector_cnt;
							end
							else begin
								ide_sdc_cnt <= ide_spb;
								ide_io_size <= ide_spb;
							end
							ide_exec <= IDE_EXEC_READ_SECTOR;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:8] == 8'h91) begin
							ide_status[2] <= 1'b1;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:8] == 8'hc6) begin
							ide_spb <= ide_sector_cnt;
							ide_status[2] <= 1'b1;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
						else if (ide_readdata[15:12] == 4'h3) begin
							ide_status[3:2] <= 2'b11;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
							ide_sdc_cnt <= 8'd1;
							ide_io_size <= 8'd1;
						end
						else if (ide_readdata[15:8] == 8'hc5) begin
							ide_status[3:2] <= 2'b11;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
							if (ide_sector_cnt < ide_spb) begin
								ide_sdc_cnt <= ide_sector_cnt;
								ide_io_size <= ide_sector_cnt;
							end
							else begin
								ide_sdc_cnt <= ide_spb;
								ide_io_size <= ide_spb;
							end
						end
						else begin
							ide_status[0] <= 1'b1;
							ide_status[2] <= 1'b1;
							ide_error <= 8'h04;
							ide_exec <= IDE_EXEC_SET_REGS;
							ide_exec_cnt <= 9'd0;
						end
					end
					else begin
						ide_status[0] <= 1'b1;
						ide_status[2] <= 1'b1;
						ide_error <= 8'h04;
						ide_exec <= IDE_EXEC_SET_REGS;
						ide_exec_cnt <= 9'd0;
					end
				end
				IDE_EXEC_SET_REGS:
					if (ide_exec_cnt != 9'h00b)
						ide_exec_cnt <= ide_exec_cnt + 9'd1;
					else begin
						ide_busy <= 1'b0;
						ide_exec <= IDE_EXEC_IDLE;
						ide_exec_cnt <= 9'd0;
					end
			endcase
			ide_req_q <= ide_request[2:0];
			if ((ide_request[2:0] == 3'b110) && (ide_req_q != 3'b110))
				ide_rst_pend <= 1'b1;
			if ((((ide_rst_pend || ((ide_request[2:0] == 3'b110) && (ide_req_q != 3'b110))) && (ide_sdc_rd == 2'b00)) && (ide_sdc_wr == 2'b00)) && !sdc_busy) begin
				ide_rst_pend <= 1'b0;
				ide_busy <= 1'b0;
				ide_exec <= IDE_EXEC_IDLE;
				ide_exec_cnt <= 9'd0;
				ide_reported <= 2'b00;
			end
		end
	wire [15:0] ide_identify_data = (ide_exec_cnt[8:1] == 8'd0 ? 16'h0040 : (ide_exec_cnt[8:1] == 8'd1 ? cylinders[ide_drv] : (ide_exec_cnt[8:1] == 8'd3 ? heads[ide_drv] : (ide_exec_cnt[8:1] == 8'd6 ? sectors[ide_drv] : (ide_exec_cnt[8:1] == 8'd10 ? "AO" : (ide_exec_cnt[8:1] == 8'd11 ? "HD" : (ide_exec_cnt[8:1] == 8'd12 ? "00" : (ide_exec_cnt[8:1] == 8'd13 ? "00" : (ide_exec_cnt[8:1] == 8'd14 ? "0 " : (ide_exec_cnt[8:1] == 8'd15 ? "  " : (ide_exec_cnt[8:1] == 8'd16 ? "  " : (ide_exec_cnt[8:1] == 8'd17 ? "  " : (ide_exec_cnt[8:1] == 8'd18 ? "  " : (ide_exec_cnt[8:1] == 8'd19 ? "  " : (ide_exec_cnt[8:1] == 8'd20 ? 16'd3 : (ide_exec_cnt[8:1] == 8'd21 ? 16'd512 : (ide_exec_cnt[8:1] == 8'd22 ? 16'd4 : ((ide_exec_cnt[8:1] >= 8'd27) && (ide_exec_cnt[8:1] <= 8'd46) ? "  " : (ide_exec_cnt[8:1] == 8'd47 ? 16'h8020 : (ide_exec_cnt[8:1] == 8'd48 ? 16'd1 : (ide_exec_cnt[8:1] == 8'd49 ? 16'b0000000000000000 : (ide_exec_cnt[8:1] == 8'd50 ? 16'h4001 : (ide_exec_cnt[8:1] == 8'd51 ? 16'h0200 : (ide_exec_cnt[8:1] == 8'd52 ? 16'h0200 : (ide_exec_cnt[8:1] == 8'd53 ? 16'h0007 : (ide_exec_cnt[8:1] == 8'd54 ? cylinders[ide_drv] : (ide_exec_cnt[8:1] == 8'd55 ? heads[ide_drv] : (ide_exec_cnt[8:1] == 8'd56 ? sectors[ide_drv] : (ide_exec_cnt[8:1] == 8'd57 ? total_sectors[ide_drv][15:0] : (ide_exec_cnt[8:1] == 8'd58 ? total_sectors[ide_drv][31:16] : (ide_exec_cnt[8:1] == 8'd59 ? 16'h0110 : (ide_exec_cnt[8:1] == 8'd65 ? 16'd120 : (ide_exec_cnt[8:1] == 8'd66 ? 16'd120 : (ide_exec_cnt[8:1] == 8'd67 ? 16'd120 : (ide_exec_cnt[8:1] == 8'd68 ? 16'd120 : (ide_exec_cnt[8:1] == 8'd80 ? 16'h007e : (ide_exec_cnt[8:1] == 8'd82 ? 16'b0100001000000000 : (ide_exec_cnt[8:1] == 8'd83 ? 16'b0111000000000000 : (ide_exec_cnt[8:1] == 8'd84 ? 16'b0100000000000000 : (ide_exec_cnt[8:1] == 8'd85 ? 16'b0100001000000000 : (ide_exec_cnt[8:1] == 8'd86 ? 16'b0111000000000000 : (ide_exec_cnt[8:1] == 8'd87 ? 16'b0100000000000000 : (ide_exec_cnt[8:1] == 8'd93 ? 16'b0110001100001011 : 16'h0000)))))))))))))))))))))))))))))))))))))))))));
	wire [4:0] ide_address = {1'b0, (ide_exec == IDE_EXEC_SET_CONFIG ? 4'd6 : (ide_exec == IDE_EXEC_SET_REGS ? ide_exec_cnt[4:1] : (ide_exec == IDE_EXEC_GET_REGS ? ide_exec_cnt[4:1] : (ide_exec == IDE_EXEC_SEND_IDENTIFY ? 4'd15 : (ide_exec == IDE_EXEC_READ_SECTOR ? 4'd15 : (ide_exec == IDE_EXEC_SEND_PAYLOAD ? 4'd15 : (ide_exec == IDE_EXEC_WRITE_SECTOR ? 4'd15 : (ide_exec == IDE_EXEC_RECV_PAYLOAD ? 4'd15 : 4'd0))))))))};
	wire [15:0] ide_set_register_data = (ide_exec_cnt[4:1] == 4'd0 ? {ide_error, ide_io_size} : (ide_exec_cnt[4:1] == 4'd1 ? {ide_sector, ide_sector_cnt} : (ide_exec_cnt[4:1] == 4'd2 ? ide_cylinder : (ide_exec_cnt[4:1] == 4'd3 ? 16'h0000 : (ide_exec_cnt[4:1] == 4'd4 ? 16'h0000 : (ide_exec_cnt[4:1] == 4'd5 ? {ide_status, 3'b101, ide_drv, ide_head} : 16'h0000))))));
	reg [7:0] sdc_even_byte;
	always @(posedge clk_sys)
		if (sdc_byte_in_strobe && !sdc_byte_addr[0])
			sdc_even_byte <= sdc_byte_in_data;
	wire [15:0] ide_payload_data = {sdc_byte_in_data, sdc_even_byte};
	wire [15:0] ide_config_data = {8'h00, (ide_drv_state[1] == IDE_DRV_STATE_PRESENT ? 4'hf : 4'h0), (ide_drv_state[0] == IDE_DRV_STATE_PRESENT ? 4'hf : 4'h0)};
	assign ide_writedata = (ide_exec == IDE_EXEC_SEND_IDENTIFY ? ide_identify_data : (ide_exec == IDE_EXEC_READ_SECTOR ? ide_payload_data : (ide_exec == IDE_EXEC_SEND_PAYLOAD ? ide_payload_data : (ide_exec == IDE_EXEC_SET_REGS ? ide_set_register_data : (ide_exec == IDE_EXEC_SET_CONFIG ? ide_config_data : 16'h0000)))));
	wire ide_read = !ide_exec_cnt[0] && (ide_exec == IDE_EXEC_GET_REGS);
	reg ide_active = 0;
	always @(posedge clk_sys) begin
		if (|{sdc_rd[5:4], sdc_wr[5:4]})
			ide_active <= 1'b1;
		if (|{sdc_rd[3:0], sdc_wr[3:0]})
			ide_active <= 1'b0;
	end
	reg [7:0] ide_readdataD;
	assign sdc_byte_out_data = (ide_active ? (!sdc_byte_addr[0] ? ide_readdata[7:0] : (sdc_byte_addr == 1 ? ide_readdata[15:8] : ide_readdataD)) : sdc_byte_out_data_fdc);
	reg sdc_addr_toggle;
	always @(posedge clk_sys) begin : sv2v_autoblock_3
		reg last_sdc_addr;
		reg latch_data;
		last_sdc_addr <= sdc_byte_addr[0];
		sdc_addr_toggle <= !last_sdc_addr && sdc_byte_addr[0];
		latch_data <= last_sdc_addr && !sdc_byte_addr[0];
		if (latch_data)
			ide_readdataD <= ide_readdata[15:8];
	end
	wire ide_write = (((!ide_exec_cnt[0] && (((ide_exec == IDE_EXEC_SET_CONFIG) || (ide_exec == IDE_EXEC_SET_REGS)) || (ide_exec == IDE_EXEC_SEND_IDENTIFY))) || ((ide_exec == IDE_EXEC_WRITE_SECTOR) && sdc_addr_toggle)) || ((ide_exec == IDE_EXEC_RECV_PAYLOAD) && sdc_addr_toggle)) || (((ide_exec == IDE_EXEC_SEND_PAYLOAD) && sdc_byte_in_strobe) && sdc_byte_addr[0]);
	wire [7:0] red;
	wire [7:0] green;
	wire [7:0] blue;
	wire hbl;
	wire vbl;
	wire [8:0] htotal;
	assign vid_r = red;
	assign vid_g = green;
	assign vid_b = blue;
	wire hs_in;
	assign vid_hs = hs_in;
	wire vs_in;
	assign vid_vs = vs_in;
	assign vid_hbl = hbl;
	assign vid_vbl = vbl;
	wire [1:0] res;
	assign vid_res = res;
	wire [3:0] r_in = (hbl || vbl ? 4'h0 : red[7:4]);
	wire [3:0] g_in = (hbl || vbl ? 4'h0 : green[7:4]);
	wire [3:0] b_in = (hbl || vbl ? 4'h0 : blue[7:4]);
	wire [15:0] JOY0 = {8'h00, joystick0};
	wire [15:0] JOY1 = {8'h00, joystick1};
	wire [15:0] JOY2 = 16'h0000;
	wire [15:0] JOY3 = 16'h0000;
	wire [15:0] JOYA0 = 16'h0000;
	wire [15:0] JOYA1 = 16'h0000;
	wire [15:0] ramdata_in_ext = ramdata_in;
	wire [14:0] at_serper;
	assign serial_serper = at_serper;
	wire at_rbf;
	wire at_hold;
	wire at_rxd;
	uart_port uart_port(
		.clk(clk_sys),
		.clk7_en(clk7_en),
		.reset(reset_d),
		.serper(at_serper),
		.rbf(at_rbf),
		.txd(uart_tx),
		.rxd(at_rxd),
		.hold(at_hold),
		.serial_status(serial_status),
		.serial_tx_available(serial_tx_available),
		.serial_tx_strobe(serial_tx_strobe),
		.serial_tx_data(serial_tx_data),
		.serial_rx_available(serial_rx_available),
		.serial_rx_strobe(serial_rx_strobe),
		.serial_rx_data(serial_rx_data)
	);
	minimig minimig(
		.cpu_address(chip_addr),
		.cpu_data(chip_dout),
		.cpudata_in(chip_din),
		._cpu_ipl(chip_ipl),
		._cpu_as(chip_as),
		._cpu_uds(chip_uds),
		._cpu_lds(chip_lds),
		.cpu_r_w(chip_rw),
		._cpu_dtack(chip_dtack),
		._cpu_reset(cpu_rst),
		._cpu_reset_in(cpu_nrst_out),
		.nmi_addr(cpu_nmi_addr),
		.ovr(),
		.memory_config(memory_config),
		.chipset_config(chipset_config),
		.floppy_config(floppy_config),
		.ide_config(ide_config),
		.ram_data(ram_data),
		.ramdata_in(ramdata_in_ext),
		.ram_address(ram_address),
		._ram_bhe(_ram_bhe),
		._ram_ble(_ram_ble),
		._ram_we(_ram_we),
		._ram_oe(_ram_oe),
		.chip48(chip48),
		.refresh(refresh),
		.rst_ext(reset_d),
		.rst_out(),
		.clk(clk_sys),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.c1(c1),
		.c3(c3),
		.cck(cck),
		.eclk(eclk),
		.ovl(ovl),
		.rxd(at_rxd),
		.txd(uart_tx),
		.uart_serper(at_serper),
		.uart_rbf(at_rbf),
		.uart_hold(at_hold),
		.cts(ser_cts_n),
		.rts(ser_rts_n),
		.dtr(ser_dtr_n),
		.dsr(ser_dsr_n),
		.cd(ser_cd_n),
		.ri(ser_ri_n),
		._joy1(~JOY0),
		._joy2(~JOY1),
		._joy3(~JOY2),
		._joy4(~JOY3),
		.joya1(JOYA0),
		.joya2(JOYA1),
		.mouse_btn(mouse_buttons),
		.kbd_mouse_data(kbd_mouse_data),
		.kbd_mouse_type(kbd_mouse_type),
		.kms_level(kbd_mouse_level),
		.keyboard_ack(keyboard_ack),
		.pwr_led(pwr_led_bright),
		.fdd_led(fdd_led),
		.fdd_step_n(fdd_step_n),
		.rtc(rtc),
		._hsync(hs_in),
		._vsync(vs_in),
		._csync(),
		.field1(vid_field),
		.lace(vid_lace),
		.red(red),
		.green(green),
		.blue(blue),
		.hblank(hbl),
		.vblank(vbl),
		.res(res),
		.htotal(htotal),
		.ce_pix(vid_ce),
		.ldata(audio_left),
		.rdata(audio_right),
		.ldata_okk(),
		.rdata_okk(),
		.cpucfg(cpucfg),
		.hdd_led(hdd_led),
		.ide_fast(),
		.ide_ext_irq(1'b0),
		.ide_ena(),
		.ide_req(ide_request),
		.ide_address(ide_address),
		.ide_write(ide_write),
		.ide_writedata(ide_writedata),
		.ide_read(ide_read),
		.ide_readdata(ide_readdata),
		.sdc_img_mounted(sdc_img_mounted[3:0]),
		.sdc_img_size(sdc_img_size[31:0]),
		.sdc_rd(sdc_rd[3:0]),
		.sdc_wr(sdc_wr[3:0]),
		.sdc_sector(sdc_sector_fdc),
		.sdc_busy(sdc_busy),
		.sdc_done(sdc_done),
		.sdc_byte_in_strobe(sdc_byte_in_strobe),
		.sdc_byte_addr(sdc_byte_addr),
		.sdc_byte_in_data(sdc_byte_in_data),
		.sdc_byte_out_data(sdc_byte_out_data_fdc)
	);
	Amber AMBER(
		.clk28m(clk_sys),
		.lr_filter(video_config[3:2]),
		.hr_filter(video_config[3:2]),
		.scanline(video_config[1:0]),
		.htotal(htotal[8:1]),
		.hires(res[0]),
		.dblscan(1'b1),
		.red_in(r_in),
		.green_in(g_in),
		.blue_in(b_in),
		._hsync_in(hs_in),
		._vsync_in(vs_in),
		._csync_in(1'b1),
		.red_out(r),
		.green_out(g),
		.blue_out(b),
		._hsync_out(hs),
		._vsync_out(vs)
	);
endmodule
module Amber (
	clk28m,
	lr_filter,
	hr_filter,
	scanline,
	htotal,
	hires,
	dblscan,
	red_in,
	green_in,
	blue_in,
	_hsync_in,
	_vsync_in,
	_csync_in,
	red_out,
	green_out,
	blue_out,
	_hsync_out,
	_vsync_out
);
	input clk28m;
	input [1:0] lr_filter;
	input [1:0] hr_filter;
	input [1:0] scanline;
	input [8:1] htotal;
	input hires;
	input dblscan;
	input [3:0] red_in;
	input [3:0] green_in;
	input [3:0] blue_in;
	input _hsync_in;
	input _vsync_in;
	input _csync_in;
	output reg [3:0] red_out;
	output reg [3:0] green_out;
	output reg [3:0] blue_out;
	output reg _hsync_out;
	output reg _vsync_out;
	reg [3:0] t_red;
	reg [3:0] t_green;
	reg [3:0] t_blue;
	reg [3:0] red_del;
	reg [3:0] green_del;
	reg [3:0] blue_del;
	wire [4:0] red;
	wire [4:0] green;
	wire [4:0] blue;
	reg _hsync_in_del;
	reg hss;
	wire eol;
	reg hfilter;
	reg vfilter;
	reg scanline_ena;
	reg [10:0] wr_ptr;
	reg [10:0] rd_ptr;
	always @(posedge clk28m) _hsync_in_del <= _hsync_in;
	always @(posedge clk28m) hss <= ~_hsync_in & _hsync_in_del;
	always @(posedge clk28m)
		if (wr_ptr[0]) begin
			red_del <= red_in;
			green_del <= green_in;
			blue_del <= blue_in;
		end
	assign red = (hfilter ? red_in + red_del : red_in * 2);
	assign green = (hfilter ? green_in + green_del : green_in * 2);
	assign blue = (hfilter ? blue_in + blue_del : blue_in * 2);
	always @(posedge clk28m)
		if (hss)
			wr_ptr <= 0;
		else
			wr_ptr <= wr_ptr + 1;
	assign eol = (rd_ptr == {htotal[8:1], 2'b11} ? 1'b1 : 1'b0);
	always @(posedge clk28m)
		if (hss || eol)
			rd_ptr <= 0;
		else
			rd_ptr <= rd_ptr + 1;
	always @(posedge clk28m)
		if (hss)
			scanline_ena <= 0;
		else if (eol)
			scanline_ena <= 1;
	always @(posedge clk28m)
		if (hss)
			hfilter <= (hires ? hr_filter[0] : lr_filter[0]);
	always @(posedge clk28m)
		if (hss)
			vfilter <= (hires ? hr_filter[1] : lr_filter[1]);
	reg [15:0] lbf [1023:0];
	reg [15:0] lbfo;
	reg [15:0] lbfo2;
	reg [15:0] lbfdo;
	always @(posedge clk28m) begin
		lbf[wr_ptr[10:1]] <= {_hsync_in, red, green, blue};
		lbfo <= lbf[rd_ptr[9:0]];
	end
	reg [15:0] lbfd [1023:0];
	always @(posedge clk28m) begin : sv2v_autoblock_1
		reg [15:0] lbfdoD;
		lbfd[rd_ptr[9:0] - 10'd1] <= lbfo;
		lbfdoD <= lbfd[rd_ptr[9:0]];
		lbfdo <= lbfdoD;
	end
	always @(posedge clk28m) lbfo2 <= lbfo;
	always @(posedge clk28m) begin
		_hsync_out <= (dblscan ? lbfo2[15] : _csync_in);
		_vsync_out <= (dblscan ? _vsync_in : 1'b1);
		if (vfilter) begin
			t_red <= (lbfo2[14:10] + lbfdo[14:10]) / 4;
			t_green <= (lbfo2[9:5] + lbfdo[9:5]) / 4;
			t_blue <= (lbfo2[4:0] + lbfdo[4:0]) / 4;
		end
		else begin
			t_red <= lbfo2[14:11];
			t_green <= lbfo2[9:6];
			t_blue <= lbfo2[4:1];
		end
	end
	always @(posedge clk28m)
		if ((dblscan && scanline_ena) && (scanline == 2'b10))
			{red_out, green_out, blue_out} <= 12'h000;
		else if ((dblscan && scanline_ena) && (scanline == 2'b01))
			{red_out, green_out, blue_out} <= {1'b0, t_red[3:1], 1'b0, t_green[3:1], 1'b0, t_blue[3:1]};
		else if (dblscan && (scanline == 2'b11)) begin
			if (scanline_ena)
				{red_out, green_out, blue_out} <= {{t_red[2:0], 1'b1} & {4 {t_red[3]}}, {t_green[2:0], 1'b1} & {4 {t_green[3]}}, {t_blue[2:0], 1'b1} & {4 {t_blue[3]}}};
			else
				{red_out, green_out, blue_out} <= {{t_red[2:0], 1'b0} | {4 {t_red[3]}}, {t_green[2:0], 1'b0} | {4 {t_green[3]}}, {t_blue[2:0], 1'b0} | {4 {t_blue[3]}}};
		end
		else
			{red_out, green_out, blue_out} <= {t_red, t_green, t_blue};
endmodule
module agnus_audiodma (
	clk,
	clk7_en,
	dma,
	audio_dmal,
	audio_dmas,
	hpos,
	reg_address_in,
	reg_address_out,
	data_in,
	address_out
);
	input wire clk;
	input wire clk7_en;
	output wire dma;
	input wire [3:0] audio_dmal;
	input wire [3:0] audio_dmas;
	input wire [8:0] hpos;
	input wire [8:1] reg_address_in;
	output reg [8:1] reg_address_out;
	input wire [15:0] data_in;
	output wire [20:1] address_out;
	parameter AUD0DAT_REG = 9'h0aa;
	parameter AUD1DAT_REG = 9'h0ba;
	parameter AUD2DAT_REG = 9'h0ca;
	parameter AUD3DAT_REG = 9'h0da;
	wire audlcena;
	wire [1:0] audlcsel;
	reg [20:16] audlch [3:0];
	reg [15:1] audlcl [3:0];
	wire [20:1] audlcout;
	reg [20:1] audpt [3:0];
	wire [20:1] audptout;
	reg [1:0] channel;
	reg dmal;
	reg dmas;
	assign audlcena = (((~reg_address_in[8] & reg_address_in[7]) & (reg_address_in[6] ^ reg_address_in[5])) & ~reg_address_in[3]) & ~reg_address_in[2];
	assign audlcsel = {~reg_address_in[5], reg_address_in[4]};
	always @(posedge clk)
		if (clk7_en) begin
			if (audlcena & ~reg_address_in[1])
				audlch[audlcsel] <= #(1) data_in[4:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (audlcena & reg_address_in[1])
				audlcl[audlcsel] <= #(1) data_in[15:1];
		end
	assign audlcout = {audlch[channel], audlcl[channel]};
	always @(*)
		case (hpos)
			9'b000100101: dmal = audio_dmal[0];
			9'b000101001: dmal = audio_dmal[1];
			9'b000101101: dmal = audio_dmal[2];
			9'b000110001: dmal = audio_dmal[3];
			default: dmal = 0;
		endcase
	assign dma = dmal;
	always @(*)
		case (hpos)
			9'b000100101: dmas = audio_dmas[0];
			9'b000101001: dmas = audio_dmas[1];
			9'b000101101: dmas = audio_dmas[2];
			9'b000110001: dmas = audio_dmas[3];
			default: dmas = 0;
		endcase
	always @(*)
		case (hpos[3:2])
			2'b01: channel = 0;
			2'b10: channel = 1;
			2'b11: channel = 2;
			2'b00: channel = 3;
		endcase
	assign address_out[20:1] = audptout[20:1];
	always @(posedge clk)
		if (clk7_en) begin
			if (dmal)
				audpt[channel] <= #(1) (dmas ? audlcout[20:1] : audptout[20:1] + 1'b1);
		end
	assign audptout[20:1] = audpt[channel];
	always @(*)
		case (channel)
			0: reg_address_out[8:1] = AUD0DAT_REG[8:1];
			1: reg_address_out[8:1] = AUD1DAT_REG[8:1];
			2: reg_address_out[8:1] = AUD2DAT_REG[8:1];
			3: reg_address_out[8:1] = AUD3DAT_REG[8:1];
		endcase
endmodule
module agnus_blitter_adrgen (
	clk,
	clk7_en,
	reset,
	ptrsel,
	modsel,
	enaptr,
	incptr,
	decptr,
	addmod,
	submod,
	sign_out,
	data_in,
	reg_address_in,
	address_out
);
	input clk;
	input clk7_en;
	input reset;
	input [1:0] ptrsel;
	input [1:0] modsel;
	input enaptr;
	input incptr;
	input decptr;
	input addmod;
	input submod;
	output wire sign_out;
	input [15:0] data_in;
	input [8:1] reg_address_in;
	output wire [20:1] address_out;
	parameter BLTAMOD = 9'h064;
	parameter BLTBMOD = 9'h062;
	parameter BLTCMOD = 9'h060;
	parameter BLTDMOD = 9'h066;
	parameter BLTAPTH = 9'h050;
	parameter BLTAPTL = 9'h052;
	parameter BLTBPTH = 9'h04c;
	parameter BLTBPTL = 9'h04e;
	parameter BLTCPTH = 9'h048;
	parameter BLTCPTL = 9'h04a;
	parameter BLTDPTH = 9'h054;
	parameter BLTDPTL = 9'h056;
	parameter CHA = 2'b10;
	parameter CHB = 2'b01;
	parameter CHC = 2'b00;
	parameter CHD = 2'b11;
	wire [1:0] bltptr_sel;
	wire [20:1] bltptr_in;
	reg [20:16] bltpth [3:0];
	wire [20:16] bltpth_out;
	reg [15:1] bltptl [3:0];
	wire [15:1] bltptl_out;
	wire [20:1] bltptr_out;
	wire [1:0] bltmod_sel;
	reg [15:1] bltmod [3:0];
	wire [15:1] bltmod_out;
	reg [20:1] newptr;
	reg [20:1] t_newptr;
	assign bltptr_in[20:1] = (enaptr ? newptr[20:1] : {data_in[4:0], data_in[15:1]});
	assign bltptr_sel = (enaptr ? ptrsel : {reg_address_in[4], reg_address_in[2]});
	always @(posedge clk)
		if (clk7_en) begin
			if ((((enaptr || (reg_address_in[8:1] == BLTAPTH[8:1])) || (reg_address_in[8:1] == BLTBPTH[8:1])) || (reg_address_in[8:1] == BLTCPTH[8:1])) || (reg_address_in[8:1] == BLTDPTH[8:1]))
				bltpth[bltptr_sel] <= bltptr_in[20:16];
		end
	assign bltpth_out = bltpth[bltptr_sel];
	always @(posedge clk)
		if (clk7_en) begin
			if ((((enaptr || (reg_address_in[8:1] == BLTAPTL[8:1])) || (reg_address_in[8:1] == BLTBPTL[8:1])) || (reg_address_in[8:1] == BLTCPTL[8:1])) || (reg_address_in[8:1] == BLTDPTL[8:1]))
				bltptl[bltptr_sel] <= bltptr_in[15:1];
		end
	assign bltptl_out = bltptl[bltptr_sel];
	assign bltptr_out = {bltpth_out, bltptl_out};
	assign address_out = bltptr_out;
	assign bltmod_sel = (enaptr ? modsel : reg_address_in[2:1]);
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:3] == BLTAMOD[8:3])
				bltmod[bltmod_sel] <= data_in[15:1];
		end
	assign bltmod_out = bltmod[modsel];
	always @(*)
		if (incptr && !decptr)
			t_newptr = bltptr_out + 20'h00001;
		else if (!incptr && decptr)
			t_newptr = bltptr_out - 20'h00001;
		else
			t_newptr = bltptr_out;
	always @(*)
		if (addmod && !submod)
			newptr = t_newptr + {{5 {bltmod_out[15]}}, bltmod_out[15:1]};
		else if (!addmod && submod)
			newptr = t_newptr - {{5 {bltmod_out[15]}}, bltmod_out[15:1]};
		else
			newptr = t_newptr;
	assign sign_out = newptr[15];
endmodule
module agnus_blitter_barrelshifter (
	desc,
	shift,
	new_val,
	old_val,
	out
);
	input desc;
	input [3:0] shift;
	input [15:0] new_val;
	input [15:0] old_val;
	output wire [15:0] out;
	wire [35:0] shifted_new;
	wire [35:0] shifted_old;
	reg [17:0] shift_onehot;
	always @(desc or shift)
		case ({desc, shift[3:0]})
			5'h00: shift_onehot = 18'h10000;
			5'h01: shift_onehot = 18'h08000;
			5'h02: shift_onehot = 18'h04000;
			5'h03: shift_onehot = 18'h02000;
			5'h04: shift_onehot = 18'h01000;
			5'h05: shift_onehot = 18'h00800;
			5'h06: shift_onehot = 18'h00400;
			5'h07: shift_onehot = 18'h00200;
			5'h08: shift_onehot = 18'h00100;
			5'h09: shift_onehot = 18'h00080;
			5'h0a: shift_onehot = 18'h00040;
			5'h0b: shift_onehot = 18'h00020;
			5'h0c: shift_onehot = 18'h00010;
			5'h0d: shift_onehot = 18'h00008;
			5'h0e: shift_onehot = 18'h00004;
			5'h0f: shift_onehot = 18'h00002;
			5'h10: shift_onehot = 18'h00001;
			5'h11: shift_onehot = 18'h00002;
			5'h12: shift_onehot = 18'h00004;
			5'h13: shift_onehot = 18'h00008;
			5'h14: shift_onehot = 18'h00010;
			5'h15: shift_onehot = 18'h00020;
			5'h16: shift_onehot = 18'h00040;
			5'h17: shift_onehot = 18'h00080;
			5'h18: shift_onehot = 18'h00100;
			5'h19: shift_onehot = 18'h00200;
			5'h1a: shift_onehot = 18'h00400;
			5'h1b: shift_onehot = 18'h00800;
			5'h1c: shift_onehot = 18'h01000;
			5'h1d: shift_onehot = 18'h02000;
			5'h1e: shift_onehot = 18'h04000;
			5'h1f: shift_onehot = 18'h08000;
		endcase
	assign shifted_new = {2'b00, new_val[15:0]} * shift_onehot;
	assign shifted_old = {2'b00, old_val[15:0]} * shift_onehot;
	assign out = (desc ? shifted_new[15:0] | shifted_old[31:16] : shifted_new[31:16] | shifted_old[15:0]);
endmodule
module agnus_blitter_fill (
	ife,
	efe,
	fci,
	fco,
	in,
	out
);
	input ife;
	input efe;
	input fci;
	output wire fco;
	input [15:0] in;
	output reg [15:0] out;
	reg [15:0] carry;
	integer j;
	always @(fci or in[0]) carry[0] = fci ^ in[0];
	always @(in or carry)
		for (j = 1; j <= 15; j = j + 1)
			carry[j] = carry[j - 1] ^ in[j];
	assign fco = carry[15];
	always @(ife or efe or carry or in)
		if (efe)
			out[15:0] = carry[15:0];
		else if (ife)
			out[15:0] = carry[15:0] | in[15:0];
		else
			out[15:0] = in[15:0];
endmodule
module agnus_blitter_minterm (
	lf,
	ain,
	bin,
	cin,
	out
);
	input [7:0] lf;
	input [15:0] ain;
	input [15:0] bin;
	input [15:0] cin;
	output wire [15:0] out;
	reg [15:0] mt0;
	reg [15:0] mt1;
	reg [15:0] mt2;
	reg [15:0] mt3;
	reg [15:0] mt4;
	reg [15:0] mt5;
	reg [15:0] mt6;
	reg [15:0] mt7;
	integer j;
	always @(ain or bin or cin or lf)
		for (j = 15; j >= 0; j = j - 1)
			begin
				mt0[j] = ((~ain[j] & ~bin[j]) & ~cin[j]) & lf[0];
				mt1[j] = ((~ain[j] & ~bin[j]) & cin[j]) & lf[1];
				mt2[j] = ((~ain[j] & bin[j]) & ~cin[j]) & lf[2];
				mt3[j] = ((~ain[j] & bin[j]) & cin[j]) & lf[3];
				mt4[j] = ((ain[j] & ~bin[j]) & ~cin[j]) & lf[4];
				mt5[j] = ((ain[j] & ~bin[j]) & cin[j]) & lf[5];
				mt6[j] = ((ain[j] & bin[j]) & ~cin[j]) & lf[6];
				mt7[j] = ((ain[j] & bin[j]) & cin[j]) & lf[7];
			end
	assign out = ((((((mt0 | mt1) | mt2) | mt3) | mt4) | mt5) | mt6) | mt7;
endmodule
module agnus_blitter (
	clk,
	clk7_en,
	reset,
	ecs,
	clkena,
	enadma,
	reqdma,
	ackdma,
	we,
	zero,
	busy,
	int3,
	data_in,
	data_out,
	reg_address_in,
	address_out,
	reg_address_out
);
	input clk;
	input clk7_en;
	input reset;
	input ecs;
	input clkena;
	input enadma;
	output wire reqdma;
	input ackdma;
	output wire we;
	output reg zero;
	output reg busy;
	output wire int3;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input [8:1] reg_address_in;
	output wire [20:1] address_out;
	output reg [8:1] reg_address_out;
	parameter BLTCON0 = 9'h040;
	parameter BLTCON0L = 9'h05a;
	parameter BLTCON1 = 9'h042;
	parameter BLTAFWM = 9'h044;
	parameter BLTALWM = 9'h046;
	parameter BLTADAT = 9'h074;
	parameter BLTBDAT = 9'h072;
	parameter BLTCDAT = 9'h070;
	parameter BLTDDAT = 9'h000;
	parameter BLTSIZE = 9'h058;
	parameter BLTSIZH = 9'h05e;
	parameter BLTSIZV = 9'h05c;
	parameter CHA = 2'b10;
	parameter CHB = 2'b01;
	parameter CHC = 2'b00;
	parameter CHD = 2'b11;
	parameter BLT_IDLE = 5'b00000;
	parameter BLT_INIT = 5'b00001;
	parameter BLT_A = 5'b01001;
	parameter BLT_B = 5'b01011;
	parameter BLT_C = 5'b01010;
	parameter BLT_D = 5'b01000;
	parameter BLT_E = 5'b01100;
	parameter BLT_F = 5'b00100;
	parameter BLT_L1 = 5'b11001;
	parameter BLT_L2 = 5'b11011;
	parameter BLT_L3 = 5'b11010;
	parameter BLT_L4 = 5'b11000;
	reg [15:0] bltcon0;
	wire [3:0] ash;
	wire usea;
	wire useb;
	wire usec;
	wire used;
	reg enad;
	reg [15:0] bltcon1;
	wire [3:0] bsh;
	wire desc;
	wire line;
	wire ife;
	wire efe;
	reg [15:0] bltafwm;
	reg [15:0] bltalwm;
	reg [15:0] bltadat;
	reg [15:0] bltbdat;
	reg [15:0] bltcdat;
	reg [15:0] bltaold;
	reg [15:0] bltbold;
	reg [15:0] bltahold;
	reg [15:0] bltbhold;
	reg [15:0] bltdhold;
	reg [10:0] width;
	reg [14:0] height;
	reg [4:0] blt_state;
	reg [4:0] blt_next;
	wire enable;
	reg [1:0] chsel;
	reg [1:0] ptrsel;
	reg [1:0] modsel;
	reg enaptr;
	reg incptr;
	reg decptr;
	reg addmod;
	reg submod;
	wire incash;
	wire decash;
	wire decbsh;
	wire sign_out;
	reg sign;
	reg sign_del;
	reg first_pixel;
	reg first_line_pixel;
	reg start;
	wire init;
	wire next_word;
	reg store_result;
	reg pipeline_full;
	wire first_word;
	reg first_word_del;
	wire last_word;
	reg last_word_del;
	wire last_line;
	wire done;
	wire [15:0] minterm_out;
	wire [15:0] fill_out;
	wire fci;
	wire fco;
	reg fcy;
	reg [10:0] width_cnt;
	wire width_cnt_dec;
	wire width_cnt_rld;
	reg [14:0] height_cnt;
	reg [15:0] bltamask;
	wire [15:0] shiftaout;
	wire [15:0] shiftbout;
	reg dma_req;
	wire dma_ack;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltcon0[15:12] <= 0;
			else if (enable && incash)
				bltcon0[15:12] <= bltcon0[15:12] + 4'b0001;
			else if (enable && decash)
				bltcon0[15:12] <= bltcon0[15:12] - 4'b0001;
			else if (reg_address_in[8:1] == BLTCON0[8:1])
				bltcon0[15:12] <= data_in[15:12];
		end
	assign ash[3:0] = bltcon0[15:12];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltcon0[11:8] <= 0;
			else if (reg_address_in[8:1] == BLTCON0[8:1])
				bltcon0[11:8] <= data_in[11:8];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (init)
				enad <= 1'b1;
			else if ((reg_address_in[8:1] == BLTCON0[8:1]) && busy)
				enad <= 1'b0;
		end
	assign {usea, useb, usec, used} = {bltcon0[11:9], bltcon0[8] & enad};
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltcon0[7:0] <= 0;
			else if ((reg_address_in[8:1] == BLTCON0[8:1]) || ((reg_address_in[8:1] == BLTCON0L[8:1]) && ecs))
				bltcon0[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltcon1[15:12] <= 0;
			else if (enable && decbsh)
				bltcon1[15:12] <= bltcon1[15:12] - 4'b0001;
			else if (reg_address_in[8:1] == BLTCON1[8:1])
				bltcon1[15:12] <= data_in[15:12];
		end
	assign bsh[3:0] = bltcon1[15:12];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltcon1[11:0] <= 0;
			else if (reg_address_in[8:1] == BLTCON1[8:1])
				bltcon1[11:0] <= data_in[11:0];
		end
	assign line = bltcon1[0];
	assign desc = ~line & bltcon1[1];
	assign efe = ~line & bltcon1[4];
	assign ife = ~line & bltcon1[3];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltafwm[15:0] <= 0;
			else if (reg_address_in[8:1] == BLTAFWM[8:1])
				bltafwm[15:0] <= data_in[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltalwm[15:0] <= 0;
			else if (reg_address_in[8:1] == BLTALWM[8:1])
				bltalwm[15:0] <= data_in[15:0];
		end
	always @(*)
		if (first_word && last_word)
			bltamask[15:0] = bltafwm[15:0] & bltalwm[15:0];
		else if (last_word)
			bltamask[15:0] = bltalwm[15:0];
		else if (first_word)
			bltamask[15:0] = bltafwm[15:0];
		else
			bltamask[15:0] = 16'hffff;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltadat[15:0] <= 0;
			else if (reg_address_in[8:1] == BLTADAT[8:1])
				bltadat[15:0] <= data_in[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (enable) begin
				if (init)
					bltaold[15:0] <= 0;
				else if (next_word && !line)
					bltaold[15:0] <= bltadat[15:0] & bltamask[15:0];
			end
		end
	agnus_blitter_barrelshifter barrel_shifter_A(
		.desc(desc),
		.shift(ash),
		.new_val(bltadat & bltamask),
		.old_val(bltaold),
		.out(shiftaout)
	);
	always @(posedge clk)
		if (clk7_en) begin
			if (enable)
				bltahold[15:0] <= shiftaout[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bltbdat[15:0] <= 0;
			else if (reg_address_in[8:1] == BLTBDAT[8:1])
				bltbdat[15:0] <= data_in[15:0];
		end
	reg bltbold_init;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || done)
				bltbold_init <= 1'b1;
			else if (reg_address_in[8:1] == BLTBDAT[8:1])
				bltbold_init <= 1'b0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTBDAT[8:1]) begin
				if (bltbold_init)
					bltbold[15:0] <= 0;
				else
					bltbold[15:0] <= bltbdat[15:0];
			end
		end
	reg bltbdat_wrtn;
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTBDAT[8:1])
				bltbdat_wrtn <= 1'b1;
			else
				bltbdat_wrtn <= 1'b0;
		end
	agnus_blitter_barrelshifter barrel_shifter_B(
		.desc(desc),
		.shift(bsh),
		.new_val(bltbdat),
		.old_val(bltbold),
		.out(shiftbout)
	);
	always @(posedge clk)
		if (clk7_en) begin
			if (line)
				bltbhold[15:0] <= {16 {shiftbout[0]}};
			else if (bltbdat_wrtn)
				bltbhold[15:0] <= shiftbout[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTCDAT[8:1])
				bltcdat[15:0] <= data_in[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (next_word && enable)
				last_word_del <= last_word;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (next_word && enable)
				first_word_del <= first_word;
		end
	agnus_blitter_minterm bltmt1(
		.lf(bltcon0[7:0]),
		.ain(bltahold[15:0]),
		.bin(bltbhold[15:0]),
		.cin(bltcdat[15:0]),
		.out(minterm_out[15:0])
	);
	agnus_blitter_fill bltfl1(
		.ife(ife),
		.efe(efe),
		.fci(fci),
		.fco(fco),
		.in(minterm_out[15:0]),
		.out(fill_out[15:0])
	);
	assign fci = (first_word_del ? bltcon1[2] : fcy);
	always @(posedge clk)
		if (clk7_en) begin
			if (store_result)
				fcy <= fco;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (store_result)
				bltdhold[15:0] <= fill_out[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (enable && init)
				zero <= 1;
			else if (store_result && |fill_out[15:0])
				zero <= 0;
		end
	assign data_out[15:0] = (ackdma && (chsel[1:0] == CHD) ? bltdhold[15:0] : 16'h0000);
	assign we = (ackdma && (chsel[1:0] == CHD) ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				busy <= 0;
			else if ((reg_address_in[8:1] == BLTSIZE[8:1]) || ((reg_address_in[8:1] == BLTSIZH[8:1]) && ecs))
				busy <= 1;
			else if (done)
				busy <= 0;
		end
	assign int3 = done;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || done)
				start <= 0;
			else if (clkena && busy)
				start <= 1;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTSIZE[8:1])
				width[10:0] <= {4'b0000, ~|data_in[5:0], data_in[5:0]};
			else if ((reg_address_in[8:1] == BLTSIZH[8:1]) && ecs)
				width[10:0] <= data_in[10:0];
		end
	assign width_cnt_dec = enable & next_word;
	assign width_cnt_rld = ((enable & next_word) & last_word) | (init & enable);
	always @(posedge clk)
		if (clk7_en) begin
			if (width_cnt_rld)
				width_cnt[10:0] <= width[10:0];
			else if (width_cnt_dec)
				width_cnt[10:0] <= width_cnt[10:0] - 1'b1;
		end
	assign last_word = (width_cnt[10:0] == 1 ? 1'b1 : 1'b0);
	assign first_word = (width_cnt[10:0] == width[10:0] ? 1'b1 : 1'b0);
	assign last_line = (height_cnt[14:0] == 1 ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				height[14:0] <= 0;
			else if (reg_address_in[8:1] == BLTSIZV[8:1])
				height[14:0] <= data_in[14:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTSIZE[8:1])
				height_cnt[14:0] <= {4'b0000, ~|data_in[15:6], data_in[15:6]};
			else if ((reg_address_in[8:1] == BLTSIZH[8:1]) && ecs)
				height_cnt[14:0] <= height[14:0];
			else if ((enable && next_word) && last_word)
				height_cnt[14:0] <= height_cnt[14:0] - 1'b1;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (enable) begin
				if (init)
					pipeline_full <= 0;
				else if (next_word)
					pipeline_full <= 1;
			end
		end
	agnus_blitter_adrgen address_generator_1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.ptrsel(ptrsel),
		.modsel(modsel),
		.enaptr(enaptr),
		.incptr(incptr),
		.decptr(decptr),
		.addmod(addmod),
		.submod(submod),
		.sign_out(sign_out),
		.data_in(data_in),
		.reg_address_in(reg_address_in),
		.address_out(address_out)
	);
	always @(*)
		case (chsel)
			CHA: reg_address_out = BLTADAT[8:1];
			CHB: reg_address_out = BLTBDAT[8:1];
			CHC: reg_address_out = BLTCDAT[8:1];
			CHD: reg_address_out = BLTDDAT[8:1];
		endcase
	assign enable = enadma & clkena;
	assign reqdma = dma_req & enable;
	assign dma_ack = ackdma;
	wire lineinc = (((bltcon1[4] && !bltcon1[2]) || ((!bltcon1[4] && !bltcon1[3]) && !sign_del)) && (ash == 4'b1111) ? 1'b1 : 1'b0);
	wire linedec = (((bltcon1[4] && bltcon1[2]) || ((!bltcon1[4] && bltcon1[3]) && !sign_del)) && (ash == 4'b0000) ? 1'b1 : 1'b0);
	wire lineadd = ((!bltcon1[4] && !bltcon1[2]) || ((bltcon1[4] && !bltcon1[3]) && !sign_del) ? 1'b1 : 1'b0);
	wire linesub = ((!bltcon1[4] && bltcon1[2]) || ((bltcon1[4] && bltcon1[3]) && !sign_del) ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				blt_state <= BLT_IDLE;
			else
				blt_state <= blt_next;
		end
	always @(*)
		case (blt_state)
			BLT_IDLE: begin
				chsel = 2'bxx;
				ptrsel = 2'bxx;
				modsel = 2'bxx;
				enaptr = 1'b0;
				incptr = 1'bx;
				decptr = 1'bx;
				addmod = 1'bx;
				submod = 1'bx;
				dma_req = 1'b0;
				if (enable) begin
					if (start)
						blt_next = BLT_INIT;
					else
						blt_next = BLT_IDLE;
				end
				else
					blt_next = BLT_IDLE;
			end
			BLT_INIT: begin
				chsel = 2'bxx;
				ptrsel = 2'bxx;
				modsel = 2'bxx;
				enaptr = 1'b0;
				incptr = 1'bx;
				decptr = 1'bx;
				addmod = 1'bx;
				submod = 1'bx;
				dma_req = 1'b0;
				if (enable) begin
					if (line)
						blt_next = BLT_L1;
					else
						blt_next = BLT_A;
				end
				else
					blt_next = BLT_INIT;
			end
			BLT_A: begin
				chsel = CHA;
				ptrsel = CHA;
				modsel = CHA;
				enaptr = dma_ack;
				incptr = ~desc;
				decptr = desc;
				addmod = ~desc & last_word;
				submod = desc & last_word;
				dma_req = usea;
				if (enable) begin
					if (useb)
						blt_next = BLT_B;
					else if ((usec || ife) || efe)
						blt_next = BLT_C;
					else
						blt_next = BLT_D;
				end
				else
					blt_next = BLT_A;
			end
			BLT_B: begin
				chsel = CHB;
				ptrsel = CHB;
				modsel = CHB;
				enaptr = dma_ack;
				incptr = ~desc;
				decptr = desc;
				addmod = ~desc & last_word;
				submod = desc & last_word;
				dma_req = 1'b1;
				if (enable) begin
					if ((usec || ife) || efe)
						blt_next = BLT_C;
					else
						blt_next = BLT_D;
				end
				else
					blt_next = BLT_B;
			end
			BLT_C: begin
				chsel = CHC;
				ptrsel = CHC;
				modsel = CHC;
				enaptr = dma_ack;
				incptr = ~desc;
				decptr = desc;
				addmod = ~desc & last_word;
				submod = desc & last_word;
				dma_req = usec;
				if (enable) begin
					if (used)
						blt_next = BLT_D;
					else if (last_word && last_line)
						blt_next = BLT_IDLE;
					else
						blt_next = BLT_A;
				end
				else
					blt_next = BLT_C;
			end
			BLT_D: begin
				chsel = CHD;
				ptrsel = CHD;
				modsel = CHD;
				enaptr = dma_ack;
				incptr = ~desc;
				decptr = desc;
				addmod = ~desc & last_word_del;
				submod = desc & last_word_del;
				dma_req = used & pipeline_full;
				if (enable) begin
					if (last_word && last_line) begin
						if (used)
							blt_next = BLT_E;
						else
							blt_next = BLT_IDLE;
					end
					else
						blt_next = BLT_A;
				end
				else
					blt_next = BLT_D;
			end
			BLT_E: begin
				chsel = 2'bxx;
				ptrsel = 2'bxx;
				modsel = 2'bxx;
				enaptr = 1'b0;
				incptr = 1'bx;
				decptr = 1'bx;
				addmod = 1'bx;
				submod = 1'bx;
				dma_req = 1'b0;
				if (clkena)
					blt_next = BLT_F;
				else
					blt_next = BLT_E;
			end
			BLT_F: begin
				chsel = CHD;
				ptrsel = CHD;
				modsel = CHD;
				enaptr = dma_ack;
				incptr = ~desc;
				decptr = desc;
				addmod = ~desc & last_word_del;
				submod = desc & last_word_del;
				dma_req = 1'b1;
				if (enable)
					blt_next = BLT_IDLE;
				else
					blt_next = BLT_F;
			end
			BLT_L1: begin
				chsel = CHA;
				ptrsel = CHA;
				modsel = (sign ? CHB : CHA);
				enaptr = enable;
				incptr = 0;
				decptr = 0;
				addmod = 1;
				submod = 0;
				dma_req = 0;
				if (enable)
					blt_next = BLT_L2;
				else
					blt_next = BLT_L1;
			end
			BLT_L2: begin
				chsel = CHC;
				ptrsel = CHC;
				modsel = CHC;
				enaptr = enable;
				incptr = (first_line_pixel ? lineinc : 1'b0);
				decptr = (first_line_pixel ? linedec : 1'b0);
				addmod = (first_line_pixel ? lineadd : 1'b0);
				submod = (first_line_pixel ? linesub : 1'b0);
				dma_req = usec;
				if (enable)
					blt_next = BLT_L3;
				else
					blt_next = BLT_L2;
			end
			BLT_L3: begin
				chsel = CHA;
				ptrsel = CHD;
				modsel = CHD;
				enaptr = (first_line_pixel ? 1'b0 : enable);
				incptr = lineinc;
				decptr = linedec;
				addmod = lineadd;
				submod = linesub;
				dma_req = 0;
				if (enable)
					blt_next = BLT_L4;
				else
					blt_next = BLT_L3;
			end
			BLT_L4: begin
				chsel = CHD;
				ptrsel = (first_line_pixel ? CHD : CHC);
				modsel = (first_line_pixel ? CHD : CHC);
				enaptr = enable;
				incptr = lineinc;
				decptr = linedec;
				addmod = lineadd;
				submod = linesub;
				dma_req = usec & ((~bltcon1[1] | ~bltcon1[4]) | first_pixel);
				if (enable) begin
					if (last_line)
						blt_next = BLT_IDLE;
					else
						blt_next = BLT_L1;
				end
				else
					blt_next = BLT_L4;
			end
			default: begin
				chsel = CHA;
				ptrsel = 2'bxx;
				modsel = 2'bxx;
				enaptr = 0;
				incptr = 0;
				decptr = 0;
				addmod = 0;
				submod = 0;
				dma_req = 0;
				blt_next = BLT_IDLE;
			end
		endcase
	assign init = (blt_state == BLT_INIT ? 1'b1 : 1'b0);
	assign next_word = (((((blt_state == BLT_C) && !used) || (blt_state == BLT_D)) || (blt_state == BLT_L2)) || (blt_state == BLT_L4) ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				store_result <= 0;
			else
				store_result <= enable && next_word;
		end
	assign done = ((((((blt_state == BLT_C) && !used) || (blt_state == BLT_D)) && last_word) && last_line) || ((blt_state == BLT_L4) && last_line) ? enable : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (enable) begin
				if (blt_state == BLT_INIT)
					first_pixel <= 1'b1;
				else if (blt_state == BLT_L4)
					first_pixel <= ~sign_del;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				first_line_pixel <= #(1) 1'b0;
			else if (enable) begin
				if (blt_state == BLT_INIT)
					first_line_pixel <= #(1) 1'b1;
				else if (blt_state == BLT_L4)
					first_line_pixel <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BLTCON1[8:1])
				sign <= data_in[6];
			else if (enable && (blt_state == BLT_L1))
				sign <= sign_out;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (enable && (blt_state == BLT_L1))
				sign_del <= sign;
		end
	assign incash = ((enable && (blt_state == BLT_L4)) && ((bltcon1[4] && !bltcon1[2]) || ((!bltcon1[4] && !bltcon1[3]) && !sign_del)) ? 1'b1 : 1'b0);
	assign decash = ((enable && (blt_state == BLT_L4)) && ((bltcon1[4] && bltcon1[2]) || ((!bltcon1[4] && bltcon1[3]) && !sign_del)) ? 1'b1 : 1'b0);
	assign decbsh = (enable && (blt_state == BLT_L4) ? 1'b1 : 1'b0);
endmodule
module agnus_copper (
	clk,
	clk7_en,
	reset,
	ecs,
	reqdma,
	ackdma,
	enadma,
	sof,
	blit_busy,
	vpos,
	hpos,
	data_in,
	reg_address_in,
	reg_address_out,
	address_out
);
	input clk;
	input clk7_en;
	input reset;
	input ecs;
	output wire reqdma;
	input ackdma;
	input enadma;
	input sof;
	input blit_busy;
	input [7:0] vpos;
	input [8:0] hpos;
	input [15:0] data_in;
	input [8:1] reg_address_in;
	output reg [8:1] reg_address_out;
	output reg [20:1] address_out;
	parameter COP1LCH = 9'h080;
	parameter COP1LCL = 9'h082;
	parameter COP2LCH = 9'h084;
	parameter COP2LCL = 9'h086;
	parameter COPCON = 9'h02e;
	parameter COPINS = 9'h08c;
	parameter COPJMP1 = 9'h088;
	parameter COPJMP2 = 9'h08a;
	parameter RESET = 3'b000;
	parameter FETCH1 = 3'b100;
	parameter FETCH2 = 3'b101;
	parameter WAITSKIP1 = 3'b111;
	parameter WAITSKIP2 = 3'b110;
	reg [20:16] cop1lch;
	reg [15:1] cop1lcl;
	reg [20:16] cop2lch;
	reg [15:1] cop2lcl;
	reg cdang;
	reg [15:1] ir1;
	reg [15:0] ir2;
	reg [2:0] copper_state;
	reg [2:0] copper_next;
	reg strobe1;
	reg strobe2;
	reg strobe;
	reg illegalreg;
	reg skip_flag;
	reg selins;
	reg selreg;
	reg skip;
	wire enable;
	reg dma_req;
	wire dma_ack;
	wire dma_ena;
	reg beam_match;
	wire beam_match_skip;
	reg beam_match_wait;
	wire clk_ena;
	reg bus_ena;
	reg bus_blk;
	assign clk_ena = hpos[0];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				cop1lch[20:16] <= 0;
			else if (reg_address_in[8:1] == COP1LCH[8:1])
				cop1lch[20:16] <= data_in[4:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				cop1lcl[15:1] <= 0;
			else if (reg_address_in[8:1] == COP1LCL[8:1])
				cop1lcl[15:1] <= data_in[15:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				cop2lch[20:16] <= 0;
			else if (reg_address_in[8:1] == COP2LCH[8:1])
				cop2lch[20:16] <= data_in[4:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				cop2lcl[15:1] <= 0;
			else if (reg_address_in[8:1] == COP2LCL[8:1])
				cop2lcl[15:1] <= data_in[15:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				cdang <= 0;
			else if (reg_address_in[8:1] == COPCON[8:1])
				cdang <= data_in[1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == COPINS[8:1]) begin
				ir1[15:1] <= ir2[15:1];
				ir2[15:0] <= data_in[15:0];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if ((dma_ack && strobe1) && (copper_state == RESET))
				address_out[20:1] <= {cop1lch[20:16], cop1lcl[15:1]};
			else if ((dma_ack && strobe2) && (copper_state == RESET))
				address_out[20:1] <= {cop2lch[20:16], cop2lcl[15:1]};
			else if (dma_ack && (selins || selreg))
				address_out[20:1] <= address_out[20:1] + 1'b1;
		end
	always @(*)
		if (enable & selins)
			reg_address_out[8:1] = COPINS[8:1];
		else if (enable & selreg)
			reg_address_out[8:1] = ir2[8:1];
		else
			reg_address_out[8:1] = 8'hff;
	always @(*)
		if (((ir2[8:7] == 2'b00) && !cdang) || ((ir2[8:6] == 3'b000) && !ecs))
			illegalreg = 1'b1;
		else
			illegalreg = 1'b0;
	reg copjmp1;
	reg copjmp2;
	always @(posedge clk)
		if (clk7_en) begin
			if ((reg_address_in[8:1] == COPJMP1[8:1]) || sof)
				copjmp1 <= 1;
			else if (clk_ena)
				copjmp1 <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == COPJMP2[8:1])
				copjmp2 <= 1;
			else if (clk_ena)
				copjmp2 <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (copjmp1 && clk_ena)
				strobe1 <= 1;
			else if ((copper_state == RESET) && dma_ack)
				strobe1 <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (copjmp2 && clk_ena)
				strobe2 <= 1;
			else if ((copper_state == RESET) && dma_ack)
				strobe2 <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (clk_ena)
				strobe <= copjmp1 | copjmp2;
		end
	wire [8:2] horcmp;
	wire [7:0] vercmp;
	assign horcmp[2] = (ir2[1] ? ir1[1] : hpos[2]);
	assign horcmp[3] = (ir2[2] ? ir1[2] : hpos[3]);
	assign horcmp[4] = (ir2[3] ? ir1[3] : hpos[4]);
	assign horcmp[5] = (ir2[4] ? ir1[4] : hpos[5]);
	assign horcmp[6] = (ir2[5] ? ir1[5] : hpos[6]);
	assign horcmp[7] = (ir2[6] ? ir1[6] : hpos[7]);
	assign horcmp[8] = (ir2[7] ? ir1[7] : hpos[8]);
	assign vercmp[0] = (ir2[8] ? ir1[8] : vpos[0]);
	assign vercmp[1] = (ir2[9] ? ir1[9] : vpos[1]);
	assign vercmp[2] = (ir2[10] ? ir1[10] : vpos[2]);
	assign vercmp[3] = (ir2[11] ? ir1[11] : vpos[3]);
	assign vercmp[4] = (ir2[12] ? ir1[12] : vpos[4]);
	assign vercmp[5] = (ir2[13] ? ir1[13] : vpos[5]);
	assign vercmp[6] = (ir2[14] ? ir1[14] : vpos[6]);
	assign vercmp[7] = ir1[15];
	always @(posedge clk)
		if (clk7_en) begin
			if (clk_ena) begin
				if ({vpos[7:0], hpos[8:2]} >= {vercmp[7:0], horcmp[8:2]})
					beam_match <= 1'b1;
				else
					beam_match <= 1'b0;
			end
		end
	assign beam_match_skip = beam_match & (ir2[15] | ~blit_busy);
	always @(posedge clk)
		if (clk7_en) begin
			if (clk_ena)
				beam_match_wait <= beam_match_skip;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (clk_ena) begin
				if (hpos[8:1] == 8'h01)
					bus_blk <= 1;
				else
					bus_blk <= 0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (clk_ena) begin
				if (bus_blk)
					bus_ena <= 1;
				else
					bus_ena <= ~bus_ena;
			end
		end
	assign enable = (~bus_blk & bus_ena) & clk_ena;
	assign reqdma = (dma_req & bus_ena) & clk_ena;
	assign dma_ack = ackdma & enable;
	assign dma_ena = enadma;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || (clk_ena && strobe))
				copper_state <= RESET;
			else if (enable)
				copper_state <= copper_next;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (enable)
				skip_flag <= skip;
		end
	always @(*)
		case (copper_state)
			RESET: begin
				skip = 0;
				selins = 0;
				selreg = 0;
				dma_req = 1;
				if (dma_ack)
					copper_next = FETCH1;
				else
					copper_next = RESET;
			end
			FETCH1: begin
				skip = skip_flag;
				selins = 1;
				selreg = 0;
				dma_req = 1;
				if (dma_ack)
					copper_next = FETCH2;
				else
					copper_next = FETCH1;
			end
			FETCH2:
				if (!ir2[0] && illegalreg) begin
					skip = 0;
					selins = 0;
					selreg = 0;
					dma_req = 0;
					copper_next = FETCH2;
				end
				else if (!ir2[0] && skip_flag) begin
					selins = 1;
					selreg = 0;
					dma_req = 1;
					if (dma_ack) begin
						skip = 0;
						copper_next = FETCH1;
					end
					else begin
						skip = 1;
						copper_next = FETCH2;
					end
				end
				else if (!ir2[0]) begin
					skip = 0;
					selins = 0;
					selreg = 1;
					dma_req = 1;
					if (dma_ack)
						copper_next = FETCH1;
					else
						copper_next = FETCH2;
				end
				else begin
					skip = 0;
					selins = 1;
					selreg = 0;
					dma_req = 1;
					if (dma_ack)
						copper_next = WAITSKIP1;
					else
						copper_next = FETCH2;
				end
			WAITSKIP1: begin
				skip = 0;
				selins = 0;
				selreg = 0;
				dma_req = 0;
				if (dma_ena)
					copper_next = WAITSKIP2;
				else
					copper_next = WAITSKIP1;
			end
			WAITSKIP2:
				if (!ir2[0]) begin
					if (beam_match_wait) begin
						skip = 0;
						selins = 0;
						selreg = 0;
						dma_req = 0;
						if (dma_ena)
							copper_next = FETCH1;
						else
							copper_next = WAITSKIP2;
					end
					else begin
						skip = 0;
						selins = 0;
						selreg = 0;
						dma_req = 0;
						copper_next = WAITSKIP2;
					end
				end
				else if (beam_match_skip) begin
					skip = 1;
					selins = 0;
					selreg = 0;
					dma_req = 0;
					if (dma_ena)
						copper_next = FETCH1;
					else
						copper_next = WAITSKIP2;
				end
				else begin
					skip = 0;
					selins = 0;
					selreg = 0;
					dma_req = 0;
					if (dma_ena)
						copper_next = FETCH1;
					else
						copper_next = WAITSKIP2;
				end
			default: begin
				skip = 0;
				selins = 0;
				selreg = 0;
				dma_req = 0;
				copper_next = FETCH1;
			end
		endcase
endmodule
module agnus_diskdma (
	clk,
	clk7_en,
	dma,
	dmal,
	dmas,
	speed,
	hpos,
	wr,
	reg_address_in,
	reg_address_out,
	data_in,
	address_out
);
	input clk;
	input clk7_en;
	output wire dma;
	input dmal;
	input dmas;
	input speed;
	input [8:0] hpos;
	output wire wr;
	input [8:1] reg_address_in;
	output wire [8:1] reg_address_out;
	input [15:0] data_in;
	output reg [20:1] address_out;
	parameter DSKPTH = 9'h020;
	parameter DSKPTL = 9'h022;
	parameter DSKDAT = 9'h026;
	parameter DSKDATR = 9'h008;
	wire [20:1] address_outnew;
	reg dmaslot;
	always @(*)
		case (hpos[8:1])
			8'h04: dmaslot = speed;
			8'h06: dmaslot = speed;
			8'h08: dmaslot = speed;
			8'h0a: dmaslot = speed;
			8'h0c: dmaslot = 1;
			8'h0e: dmaslot = 1;
			8'h10: dmaslot = 1;
			default: dmaslot = 0;
		endcase
	assign dma = (dmal & dmaslot) & hpos[0];
	assign wr = ~dmas;
	assign address_outnew[20:1] = (dma ? address_out[20:1] + 1'b1 : {data_in[4:0], data_in[15:1]});
	always @(posedge clk)
		if (clk7_en) begin
			if (dma || (reg_address_in[8:1] == DSKPTH[8:1]))
				address_out[20:16] <= address_outnew[20:16];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (dma || (reg_address_in[8:1] == DSKPTL[8:1]))
				address_out[15:1] <= address_outnew[15:1];
		end
	assign reg_address_out[8:1] = (wr ? DSKDATR[8:1] : DSKDAT[8:1]);
endmodule
module agnus_refresh (
	hpos,
	dma
);
	input [8:0] hpos;
	output reg dma;
	always @(hpos)
		case (hpos)
			9'b000001001: dma = 1'b1;
			9'b000001101: dma = 1'b1;
			9'b000010001: dma = 1'b1;
			9'b000010101: dma = 1'b1;
			default: dma = 1'b0;
		endcase
endmodule
module agnus_spritedma (
	clk,
	clk7_en,
	reset,
	aga,
	ecs,
	reqdma,
	ackdma,
	hpos,
	vpos,
	vbl,
	vblend,
	reg_address_in,
	reg_address_out,
	data_in,
	address_out
);
	input clk;
	input clk7_en;
	input reset;
	input aga;
	input ecs;
	output reg reqdma;
	input ackdma;
	input [8:0] hpos;
	input [10:0] vpos;
	input vbl;
	input vblend;
	input [8:1] reg_address_in;
	output reg [8:1] reg_address_out;
	input [15:0] data_in;
	output wire [20:1] address_out;
	parameter SPRPTBASE_REG = 9'h120;
	parameter SPRPOSCTLBASE_REG = 9'h140;
	parameter FMODE_REG = 9'h1fc;
	reg [20:16] sprpth [7:0];
	reg [15:1] sprptl [7:0];
	reg [15:8] sprpos [7:0];
	reg sprposh [7:0];
	reg [15:4] sprctl [7:0];
	wire [9:0] vstart;
	wire spr_sscan2;
	wire [9:0] vstop;
	reg [2:0] sprite;
	wire [20:1] newptr;
	reg enable;
	reg sprvstop;
	reg sprdmastate;
	reg dmastate_mem [7:0];
	wire dmastate;
	reg dmastate_in;
	reg [2:0] sprsel;
	always @(posedge clk)
		if (sprsel[2] == hpos[0])
			sprsel <= sprsel + 1'b1;
	reg [15:0] fmode;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				fmode <= 0;
			else if (aga && (reg_address_in[8:1] == FMODE_REG[8:1]))
				fmode <= data_in;
		end
	reg [2:0] spr_fmode_ptradd;
	always @(*)
		case (fmode[3:2])
			2'b00: spr_fmode_ptradd = 3'd1;
			2'b11: spr_fmode_ptradd = 3'd4;
			default: spr_fmode_ptradd = 3'd2;
		endcase
	wire [2:0] ptsel;
	wire [2:0] pcsel;
	assign ptsel = (ackdma ? sprite : reg_address_in[4:2]);
	assign pcsel = (ackdma ? sprite : reg_address_in[5:3]);
	assign newptr = address_out[20:1] + spr_fmode_ptradd;
	wire [20:16] sprpth_in;
	assign sprpth_in = (ackdma ? newptr[20:16] : data_in[4:0]);
	always @(posedge clk)
		if (clk7_en) begin
			if (ackdma || ((reg_address_in[8:5] == SPRPTBASE_REG[8:5]) && !reg_address_in[1]))
				sprpth[ptsel] <= sprpth_in;
		end
	assign address_out[20:16] = sprpth[sprite];
	wire [15:1] sprptl_in;
	assign sprptl_in = (ackdma ? newptr[15:1] : data_in[15:1]);
	always @(posedge clk)
		if (clk7_en) begin
			if (ackdma || ((reg_address_in[8:5] == SPRPTBASE_REG[8:5]) && reg_address_in[1]))
				sprptl[ptsel] <= sprptl_in;
		end
	assign address_out[15:1] = sprptl[sprite];
	always @(posedge clk)
		if (clk7_en) begin
			if ((reg_address_in[8:6] == SPRPOSCTLBASE_REG[8:6]) && (reg_address_in[2:1] == 2'b00)) begin
				sprpos[pcsel] <= data_in[15:8];
				sprposh[pcsel] <= data_in[7];
			end
		end
	assign vstart[7:0] = sprpos[sprsel];
	assign spr_sscan2 = sprposh[sprsel];
	always @(posedge clk)
		if (clk7_en) begin
			if ((reg_address_in[8:6] == SPRPOSCTLBASE_REG[8:6]) && (reg_address_in[2:1] == 2'b01))
				sprctl[pcsel] <= {data_in[15:8], data_in[6], data_in[5], data_in[2], data_in[1]};
		end
	assign {vstop[7:0], vstart[9], vstop[9], vstart[8], vstop[8]} = sprctl[sprsel];
	always @(posedge clk) dmastate_mem[sprsel] <= dmastate_in;
	assign dmastate = dmastate_mem[sprsel];
	always @(*)
		if (vbl || ({ecs & vstop[9], vstop[8:0]} == vpos[9:0]))
			dmastate_in = 0;
		else if ({ecs & vstart[9], vstart[8:0]} == vpos[9:0])
			dmastate_in = 1;
		else
			dmastate_in = dmastate;
	always @(posedge clk)
		if ((sprite == sprsel) && (hpos[2:1] == 2'b01))
			sprdmastate <= dmastate & ~((fmode[15] && spr_sscan2) && (vpos[0] != vstart[0]));
	always @(posedge clk)
		if ((sprite == sprsel) && (hpos[2:1] == 2'b01))
			sprvstop <= {ecs & vstop[9], vstop[8:0]} == vpos[9:0];
	always @(posedge clk)
		if (clk7_en) begin
			if ((hpos[8:1] == 8'h18) && hpos[0])
				enable <= 1;
			else if ((hpos[8:1] == 8'h38) && hpos[0])
				enable <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[2:0] == 3'b001)
				sprite[2:0] <= {hpos[5] ^ hpos[4], ~hpos[4], hpos[3]};
		end
	always @(*)
		if (enable && (hpos[1:0] == 2'b01)) begin
			if (vblend || (sprvstop && ~vbl)) begin
				reqdma = 1;
				if (hpos[2])
					reg_address_out[8:1] = {SPRPOSCTLBASE_REG[8:6], sprite, 2'b00};
				else
					reg_address_out[8:1] = {SPRPOSCTLBASE_REG[8:6], sprite, 2'b01};
			end
			else if (sprdmastate) begin
				reqdma = 1;
				if (hpos[2])
					reg_address_out[8:1] = {SPRPOSCTLBASE_REG[8:6], sprite, 2'b10};
				else
					reg_address_out[8:1] = {SPRPOSCTLBASE_REG[8:6], sprite, 2'b11};
			end
			else begin
				reqdma = 0;
				reg_address_out[8:1] = 8'hff;
			end
		end
		else begin
			reqdma = 0;
			reg_address_out[8:1] = 8'hff;
		end
endmodule
module agnus (
	clk,
	clk7_en,
	cck,
	reset,
	aen,
	rd,
	hwr,
	lwr,
	data_in,
	data_out,
	address_in,
	address_out,
	reg_address_out,
	cpu_custom,
	dbr,
	dbwe,
	refresh,
	_hsync,
	_vsync,
	_csync,
	field1,
	lace,
	hblank,
	vblank,
	hde,
	sol,
	sof,
	vbl_int,
	strhor_denise,
	strhor_paula,
	htotal,
	harddis,
	varbeamen,
	int3,
	audio_dmal,
	audio_dmas,
	disk_dmal,
	disk_dmas,
	bls,
	ntsc,
	a1k,
	ecs,
	aga,
	floppy_speed
);
	input clk;
	input clk7_en;
	input cck;
	input reset;
	input aen;
	input rd;
	input hwr;
	input lwr;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input [8:1] address_in;
	output reg [20:1] address_out;
	output wire [8:1] reg_address_out;
	output reg cpu_custom;
	output reg dbr;
	output reg dbwe;
	output reg refresh;
	output wire _hsync;
	output wire _vsync;
	output wire _csync;
	output wire field1;
	output wire lace;
	output wire hblank;
	output wire vblank;
	output wire hde;
	output wire sol;
	output wire sof;
	output wire vbl_int;
	output wire strhor_denise;
	output wire strhor_paula;
	output wire [8:0] htotal;
	output wire harddis;
	output wire varbeamen;
	output wire int3;
	input [3:0] audio_dmal;
	input [3:0] audio_dmas;
	input disk_dmal;
	input disk_dmas;
	input bls;
	input ntsc;
	input a1k;
	input ecs;
	input aga;
	input floppy_speed;
	localparam DMACON = 9'h096;
	localparam DMACONR = 9'h002;
	localparam DIWSTRT = 9'h08e;
	localparam DIWSTOP = 9'h090;
	localparam DIWHIGH = 9'h1e4;
	parameter BLS_CNT_MAX = 3;
	wire [8:1] reg_address_cpu = (aen & ((rd | hwr) | lwr) ? address_in : 8'hff);
	wire req_spr;
	wire req_cop;
	wire req_blt;
	wire spren;
	wire copen;
	wire bplen;
	wire ddf;
	wire blten;
	wire dma_spr = (req_spr & spren) & !(ddf & bplen);
	wire dma_cop = req_cop & copen;
	wire dma_blt = req_blt & blten;
	wire dma_ref;
	reg ack_cop;
	reg ack_blt;
	reg ack_spr;
	reg [8:1] reg_address;
	assign reg_address_out = reg_address;
	wire dma_dsk;
	wire wr_dsk;
	wire [20:1] address_dsk;
	wire [8:1] reg_address_dsk;
	wire dma_aud;
	wire [20:1] address_aud;
	wire [8:1] reg_address_aud;
	wire dma_bpl;
	wire [20:1] address_bpl;
	wire [8:1] reg_address_bpl;
	wire [20:1] address_spr;
	wire [8:1] reg_address_spr;
	wire [20:1] address_cop;
	wire [8:1] reg_address_cop;
	wire blit_zero;
	wire [20:1] address_blt;
	wire [8:1] reg_address_blt;
	wire [15:0] data_blt;
	wire we_blt;
	wire [15:0] data_bmc;
	reg [1:0] bls_cnt;
	always @(*)
		if (dma_dsk) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 0;
			address_out = address_dsk;
			reg_address = reg_address_dsk;
			dbwe = wr_dsk;
			refresh = 0;
		end
		else if (dma_ref) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 0;
			address_out = 0;
			reg_address = 8'hff;
			dbwe = 0;
			refresh = 1;
		end
		else if (dma_aud) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 0;
			address_out = address_aud;
			reg_address = reg_address_aud;
			dbwe = 0;
			refresh = 0;
		end
		else if (dma_bpl) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 0;
			address_out = address_bpl;
			reg_address = reg_address_bpl;
			dbwe = 0;
			refresh = 0;
		end
		else if (dma_spr) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 1;
			address_out = address_spr;
			reg_address = reg_address_spr;
			dbwe = 0;
			refresh = 0;
		end
		else if (dma_cop) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 1;
			ack_blt = 0;
			ack_spr = 0;
			address_out = address_cop;
			reg_address = reg_address_cop;
			dbwe = 0;
			refresh = 0;
		end
		else if (dma_blt && (bls_cnt != BLS_CNT_MAX)) begin
			cpu_custom = 0;
			dbr = 1;
			ack_cop = 0;
			ack_blt = 1;
			ack_spr = 0;
			address_out = address_blt;
			reg_address = reg_address_blt;
			dbwe = we_blt;
			refresh = 0;
		end
		else begin
			cpu_custom = 1;
			dbr = 0;
			ack_cop = 0;
			ack_blt = 0;
			ack_spr = 0;
			address_out = 0;
			reg_address = reg_address_cpu;
			dbwe = 0;
			refresh = 0;
		end
	reg [12:0] dmacon;
	reg [15:0] dmaconr;
	wire blit_busy;
	assign data_out = (data_bmc | dmaconr) | data_blt;
	always @(*)
		if (reg_address[8:1] == DMACONR[8:1])
			dmaconr[15:0] = {1'b0, blit_busy, blit_zero, dmacon[12:0]};
		else
			dmaconr = 0;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dmacon <= 0;
			else if (reg_address[8:1] == DMACON[8:1]) begin
				if (data_in[15])
					dmacon[12:0] <= dmacon[12:0] | data_in[12:0];
				else
					dmacon[12:0] <= dmacon[12:0] & ~data_in[12:0];
			end
		end
	wire bltpri = dmacon[10];
	assign bplen = dmacon[8] & dmacon[9];
	assign copen = dmacon[7] & dmacon[9];
	assign blten = dmacon[6] & dmacon[9];
	assign spren = dmacon[5] & dmacon[9];
	wire ena_cop = ~dma_bpl;
	wire ena_blt = (~(((((dma_ref | dma_dsk) | dma_aud) | dma_spr) | dma_bpl) | dma_cop) && (bls_cnt != BLS_CNT_MAX) ? 1'b1 : 1'b0);
	wire [8:0] hpos;
	wire [10:0] vpos;
	wire vbl;
	wire vblend;
	agnus_refresh ref1(
		.hpos(hpos),
		.dma(dma_ref)
	);
	agnus_diskdma dsk1(
		.clk(clk),
		.clk7_en(clk7_en),
		.dma(dma_dsk),
		.dmal(disk_dmal),
		.dmas(disk_dmas),
		.speed(floppy_speed),
		.hpos(hpos),
		.wr(wr_dsk),
		.reg_address_in(reg_address),
		.reg_address_out(reg_address_dsk),
		.data_in(data_in),
		.address_out(address_dsk)
	);
	agnus_audiodma aud1(
		.clk(clk),
		.clk7_en(clk7_en),
		.dma(dma_aud),
		.audio_dmal(audio_dmal),
		.audio_dmas(audio_dmas),
		.hpos(hpos),
		.reg_address_in(reg_address),
		.reg_address_out(reg_address_aud),
		.data_in(data_in),
		.address_out(address_aud)
	);
	agnus_bitplanedma bpd1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.harddis(harddis),
		.ddf(ddf),
		.aga(aga),
		.ecs(ecs),
		.a1k(a1k),
		.sof(sof),
		.dmaena(bplen),
		.vpos(vpos),
		.hpos(hpos),
		.hde(hde),
		.dma(dma_bpl),
		.reg_address_in(reg_address),
		.reg_address_out(reg_address_bpl),
		.data_in(data_in),
		.address_out(address_bpl)
	);
	agnus_spritedma spr1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aga(aga),
		.ecs(ecs),
		.reqdma(req_spr),
		.ackdma(ack_spr),
		.hpos(hpos),
		.vpos(vpos),
		.vbl(vbl),
		.vblend(vblend),
		.reg_address_in(reg_address),
		.reg_address_out(reg_address_spr),
		.data_in(data_in),
		.address_out(address_spr)
	);
	agnus_copper cp1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.ecs(ecs),
		.reqdma(req_cop),
		.ackdma(ack_cop),
		.enadma(ena_cop),
		.sof(sof),
		.blit_busy(blit_busy),
		.vpos(vpos[7:0]),
		.hpos(hpos),
		.data_in(data_in),
		.reg_address_in(reg_address),
		.reg_address_out(reg_address_cop),
		.address_out(address_cop)
	);
	always @(posedge clk)
		if (clk7_en) begin
			if (!cck) begin
				if (!bls || bltpri)
					bls_cnt <= 2'b00;
				else if (bls_cnt[1:0] != BLS_CNT_MAX)
					bls_cnt <= bls_cnt + 2'b01;
			end
		end
	agnus_blitter bl1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.ecs(ecs),
		.clkena(cck),
		.enadma(blten & ena_blt),
		.reqdma(req_blt),
		.ackdma(ack_blt),
		.we(we_blt),
		.zero(blit_zero),
		.busy(blit_busy),
		.int3(int3),
		.data_in(data_in),
		.data_out(data_blt),
		.reg_address_in(reg_address),
		.address_out(address_blt),
		.reg_address_out(reg_address_blt)
	);
	agnus_beamcounter bc1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.cck(cck),
		.ntsc(ntsc),
		.aga(aga),
		.ecs(ecs),
		.a1k(a1k),
		.reg_address_in(reg_address),
		.data_in(data_in),
		.data_out(data_bmc),
		.hpos(hpos),
		.vpos(vpos),
		._hsync(_hsync),
		._vsync(_vsync),
		._csync(_csync),
		.field1(field1),
		.lace(lace),
		.hblank(hblank),
		.vblank(vblank),
		.vbl(vbl),
		.vblend(vblend),
		.eol(sol),
		.eof(sof),
		.vbl_int(vbl_int),
		.htotal_out(htotal),
		.harddis_out(harddis),
		.varbeamen_out(varbeamen)
	);
	assign strhor_denise = ((hpos == 11) && ((vpos > 8) || ecs) ? 1'b1 : 1'b0);
	assign strhor_paula = (hpos == 13 ? 1'b1 : 1'b0);
endmodule
module amiga_clk (
	clk_28,
	clk7_en,
	clk7n_en,
	c1,
	c3,
	cck,
	eclk,
	reset_n
);
	input clk_28;
	output wire clk7_en;
	output wire clk7n_en;
	output reg c1;
	output reg c3;
	output reg cck;
	output wire [9:0] eclk;
	input reset_n;
	reg [1:0] clk7_cnt = 2'b10;
	reg clk7_en_reg = 1'b1;
	reg clk7n_en_reg = 1'b1;
	reg [9:0] shifter;
	always @(posedge clk_28 or negedge reset_n)
		if (!reset_n) begin
			clk7_cnt <= 2'b10;
			clk7_en_reg <= 1'b1;
			clk7n_en_reg <= 1'b1;
			cck <= 1;
			shifter <= 1;
		end
		else begin
			clk7_cnt <= clk7_cnt + 2'b01;
			clk7_en_reg <= clk7_cnt == 2'b00;
			clk7n_en_reg <= clk7_cnt == 2'b10;
			if (clk7_cnt == 2'b01) begin
				cck <= ~cck;
				shifter <= {shifter[8:0], shifter[9]};
				if (!shifter)
					shifter <= 1;
			end
		end
	wire clk_7 = clk7_cnt[1];
	assign clk7_en = clk7_en_reg;
	assign clk7n_en = clk7n_en_reg;
	always @(posedge clk_28) c3 <= clk_7;
	always @(posedge clk_28) c1 <= ~c3;
	assign eclk = shifter;
endmodule
module cia_int (
	clk,
	clk7_en,
	wr,
	reset,
	icrs,
	ta,
	tb,
	alrm,
	flag,
	ser,
	data_in,
	data_out,
	irq
);
	input clk;
	input clk7_en;
	input wr;
	input reset;
	input icrs;
	input ta;
	input tb;
	input alrm;
	input flag;
	input ser;
	input [7:0] data_in;
	output wire [7:0] data_out;
	output wire irq;
	reg [4:0] icr = 5'd0;
	reg [4:0] icrmask = 5'd0;
	assign data_out[7:0] = (icrs && !wr ? {irq, 2'b00, icr[4:0]} : 8'b00000000);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				icrmask[4:0] <= 5'b00000;
			else if (icrs && wr) begin
				if (data_in[7])
					icrmask[4:0] <= icrmask[4:0] | data_in[4:0];
				else
					icrmask[4:0] <= icrmask[4:0] & ~data_in[4:0];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				icr[4:0] <= 5'b00000;
			else if (icrs && !wr) begin
				icr[0] <= ta;
				icr[1] <= tb;
				icr[2] <= alrm;
				icr[3] <= ser;
				icr[4] <= flag;
			end
			else begin
				icr[0] <= icr[0] | ta;
				icr[1] <= icr[1] | tb;
				icr[2] <= icr[2] | alrm;
				icr[3] <= icr[3] | ser;
				icr[4] <= icr[4] | flag;
			end
		end
	assign irq = ((((icrmask[0] & icr[0]) | (icrmask[1] & icr[1])) | (icrmask[2] & icr[2])) | (icrmask[3] & icr[3])) | (icrmask[4] & icr[4]);
endmodule
module cia_timera (
	clk,
	clk7_en,
	wr,
	reset,
	tlo,
	thi,
	tcr,
	data_in,
	data_out,
	eclk,
	tmra_ovf,
	spmode,
	irq
);
	input clk;
	input clk7_en;
	input wr;
	input reset;
	input tlo;
	input thi;
	input tcr;
	input [7:0] data_in;
	output wire [7:0] data_out;
	input eclk;
	output wire tmra_ovf;
	output wire spmode;
	output wire irq;
	reg [15:0] tmr;
	reg [7:0] tmlh;
	reg [7:0] tmll;
	reg [6:0] tmcr;
	reg forceload;
	wire oneshot;
	wire start;
	reg thi_load;
	wire reload;
	wire zero;
	wire underflow;
	wire count;
	assign count = eclk;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmcr[6:0] <= 7'd0;
			else if (tcr && wr)
				tmcr[6:0] <= {data_in[6:5], 1'b0, data_in[3:0]};
			else if (thi_load && oneshot)
				tmcr[0] <= 1'd1;
			else if (underflow && oneshot)
				tmcr[0] <= 1'd0;
		end
	always @(posedge clk)
		if (clk7_en)
			forceload <= (tcr & wr) & data_in[4];
	assign oneshot = tmcr[3];
	assign start = tmcr[0];
	assign spmode = tmcr[6];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmll[7:0] <= 8'b11111111;
			else if (tlo && wr)
				tmll[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmlh[7:0] <= 8'b11111111;
			else if (thi && wr)
				tmlh[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en)
			thi_load <= (thi & wr) & (~start | oneshot);
	assign reload = (thi_load | forceload) | underflow;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmr[15:0] <= 16'hffff;
			else if (reload)
				tmr[15:0] <= {tmlh[7:0], tmll[7:0]};
			else if (start && count)
				tmr[15:0] <= tmr[15:0] - 16'd1;
		end
	assign zero = ~|tmr;
	assign underflow = (zero & start) & count;
	assign tmra_ovf = underflow;
	assign irq = underflow;
	assign data_out[7:0] = (({8 {~wr & tlo}} & tmr[7:0]) | ({8 {~wr & thi}} & tmr[15:8])) | ({8 {~wr & tcr}} & {1'b0, tmcr[6:0]});
endmodule
module cia_timerb (
	clk,
	clk7_en,
	wr,
	reset,
	tlo,
	thi,
	tcr,
	data_in,
	data_out,
	eclk,
	tmra_ovf,
	irq
);
	input clk;
	input clk7_en;
	input wr;
	input reset;
	input tlo;
	input thi;
	input tcr;
	input [7:0] data_in;
	output wire [7:0] data_out;
	input eclk;
	input tmra_ovf;
	output wire irq;
	reg [15:0] tmr;
	reg [7:0] tmlh;
	reg [7:0] tmll;
	reg [6:0] tmcr;
	reg forceload;
	wire oneshot;
	wire start;
	reg thi_load;
	wire reload;
	wire zero;
	wire underflow;
	wire count;
	assign count = (tmcr[6] ? tmra_ovf : eclk);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmcr[6:0] <= 7'd0;
			else if (tcr && wr)
				tmcr[6:0] <= {data_in[6:5], 1'b0, data_in[3:0]};
			else if (thi_load && oneshot)
				tmcr[0] <= 1'd1;
			else if (underflow && oneshot)
				tmcr[0] <= 1'd0;
		end
	always @(posedge clk)
		if (clk7_en)
			forceload <= (tcr & wr) & data_in[4];
	assign oneshot = tmcr[3];
	assign start = tmcr[0];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmll[7:0] <= 8'b11111111;
			else if (tlo && wr)
				tmll[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmlh[7:0] <= 8'b11111111;
			else if (thi && wr)
				tmlh[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en)
			thi_load <= (thi & wr) & (~start | oneshot);
	assign reload = (thi_load | forceload) | underflow;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tmr[15:0] <= 16'hffff;
			else if (reload)
				tmr[15:0] <= {tmlh[7:0], tmll[7:0]};
			else if (start && count)
				tmr[15:0] <= tmr[15:0] - 16'd1;
		end
	assign zero = ~|tmr;
	assign underflow = (zero & start) & count;
	assign irq = underflow;
	assign data_out[7:0] = (({8 {~wr & tlo}} & tmr[7:0]) | ({8 {~wr & thi}} & tmr[15:8])) | ({8 {~wr & tcr}} & {1'b0, tmcr[6:0]});
endmodule
module cia_timerd (
	clk,
	clk7_en,
	wr,
	reset,
	tlo,
	tme,
	thi,
	tcr,
	data_in,
	data_out,
	count,
	irq
);
	input clk;
	input clk7_en;
	input wr;
	input reset;
	input tlo;
	input tme;
	input thi;
	input tcr;
	input [7:0] data_in;
	output reg [7:0] data_out;
	input count;
	output wire irq;
	reg latch_ena;
	reg count_ena;
	reg crb7;
	reg [23:0] tod;
	reg [23:0] alarm;
	reg [23:0] tod_latch;
	reg count_del;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				latch_ena <= 1'd1;
			else if (!wr) begin
				if (thi && !crb7)
					latch_ena <= 1'd0;
				else if (tlo)
					latch_ena <= 1'd1;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (latch_ena)
				tod_latch[23:0] <= tod[23:0];
		end
	always @(*)
		if (!wr) begin
			if (thi)
				data_out[7:0] = tod_latch[23:16];
			else if (tme)
				data_out[7:0] = tod_latch[15:8];
			else if (tlo)
				data_out[7:0] = tod_latch[7:0];
			else if (tcr)
				data_out[7:0] = {crb7, 7'b0000000};
			else
				data_out[7:0] = 8'd0;
		end
		else
			data_out[7:0] = 8'd0;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				count_ena <= 1'd1;
			else if (wr && !crb7) begin
				if (thi)
					count_ena <= 1'd0;
				else if (tlo)
					count_ena <= 1'd1;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				tod[23:0] <= 24'd0;
			else if (wr && !crb7) begin
				if (tlo)
					tod[7:0] <= data_in[7:0];
				if (tme)
					tod[15:8] <= data_in[7:0];
				if (thi)
					tod[23:16] <= data_in[7:0];
			end
			else if (count_ena && count)
				tod[23:0] <= tod[23:0] + 24'd1;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				alarm[7:0] <= 8'b11111111;
				alarm[15:8] <= 8'b11111111;
				alarm[23:16] <= 8'b11111111;
			end
			else if (wr && crb7) begin
				if (tlo)
					alarm[7:0] <= data_in[7:0];
				if (tme)
					alarm[15:8] <= data_in[7:0];
				if (thi)
					alarm[23:16] <= data_in[7:0];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				crb7 <= 1'd0;
			else if (wr && tcr)
				crb7 <= data_in[7];
		end
	always @(posedge clk)
		if (clk7_en)
			count_del <= count & count_ena;
	assign irq = ((tod[23:0] == alarm[23:0]) && count_del ? 1'b1 : 1'b0);
endmodule
module ciaa (
	clk,
	clk7_en,
	clk7n_en,
	aen,
	rd,
	wr,
	reset,
	rs,
	data_in,
	data_out,
	tick,
	eclk,
	irq,
	porta_in,
	porta_out,
	portb_in,
	kms_level,
	kbd_mouse_type,
	kbd_mouse_data,
	keyboard_ack,
	freeze,
	hrtmon_en
);
	input clk;
	input clk7_en;
	input clk7n_en;
	input aen;
	input rd;
	input wr;
	input reset;
	input [3:0] rs;
	input [7:0] data_in;
	output wire [7:0] data_out;
	input tick;
	input eclk;
	output wire irq;
	input [7:2] porta_in;
	output wire [3:0] porta_out;
	input [7:0] portb_in;
	input kms_level;
	input [1:0] kbd_mouse_type;
	input [7:0] kbd_mouse_data;
	output wire keyboard_ack;
	output wire freeze;
	input hrtmon_en;
	wire [7:0] icr_out;
	wire [7:0] tmra_out;
	wire [7:0] tmrb_out;
	wire [7:0] tmrd_out;
	wire [7:0] sdr_out;
	reg [7:0] pa_out;
	reg [7:0] pb_out;
	wire [7:0] portb_out;
	wire alrm;
	wire ta;
	wire tb;
	wire tmra_ovf;
	wire spmode;
	wire ser_tx_irq;
	reg [3:0] ser_tx_cnt;
	reg ser_tx_run;
	reg tick_del;
	wire pra;
	wire prb;
	wire ddra;
	wire ddrb;
	wire cra;
	wire talo;
	wire tahi;
	wire crb;
	wire tblo;
	wire tbhi;
	wire tdlo;
	wire tdme;
	wire tdhi;
	wire icrs;
	wire sdr;
	wire enable;
	assign enable = aen & (rd | wr);
	assign pra = enable && (rs == 4'h0);
	assign prb = enable && (rs == 4'h1);
	assign ddra = enable && (rs == 4'h2);
	assign ddrb = enable && (rs == 4'h3);
	assign talo = enable && (rs == 4'h4);
	assign tahi = enable && (rs == 4'h5);
	assign tblo = enable && (rs == 4'h6);
	assign tbhi = enable && (rs == 4'h7);
	assign tdlo = enable && (rs == 4'h8);
	assign tdme = enable && (rs == 4'h9);
	assign tdhi = enable && (rs == 4'ha);
	assign sdr = enable && (rs == 4'hc);
	assign icrs = enable && (rs == 4'hd);
	assign cra = enable && (rs == 4'he);
	assign crb = enable && (rs == 4'hf);
	assign data_out = (((((icr_out | tmra_out) | tmrb_out) | tmrd_out) | sdr_out) | pb_out) | pa_out;
	reg keystrobe;
	assign keyboard_ack = (!reset && sdr) && !wr;
	reg [7:0] sdr_latch;
	reg freeze_reg = 0;
	assign freeze = freeze_reg;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg kms_levelD;
		if (reset) begin
			kms_levelD <= kms_level;
			keystrobe <= 1'b0;
		end
		else if (clk7n_en) begin
			kms_levelD <= kms_level;
			keystrobe <= (kms_level ^ kms_levelD) && (kbd_mouse_type == 2);
		end
	end
	always @(posedge clk)
		if (reset) begin
			sdr_latch[7:0] <= 0;
			freeze_reg <= 0;
		end
		else if (clk7_en) begin
			if (keystrobe) begin
				sdr_latch[7:0] <= ~{kbd_mouse_data[6:0], kbd_mouse_data[7]};
				if (hrtmon_en && (kbd_mouse_data == 8'h5f))
					freeze_reg <= 1;
				else
					freeze_reg <= 0;
			end
			else if (wr & sdr)
				sdr_latch[7:0] <= data_in[7:0];
		end
	assign sdr_out = (!wr && sdr ? sdr_latch[7:0] : 8'h00);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || !spmode)
				ser_tx_run <= 0;
			else if (sdr && wr)
				ser_tx_run <= 1;
			else if (ser_tx_irq)
				ser_tx_run <= 0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (!ser_tx_run)
				ser_tx_cnt <= 4'd0;
			else if (tmra_ovf)
				ser_tx_cnt <= ser_tx_cnt + 4'd1;
		end
	assign ser_tx_irq = &ser_tx_cnt & tmra_ovf;
	reg [7:2] porta_in2;
	reg [3:0] regporta;
	reg [7:0] ddrporta;
	always @(posedge clk)
		if (clk7_en)
			porta_in2[7:2] <= porta_in[7:2];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				regporta[3:0] <= 4'd0;
			else if (wr && pra)
				regporta[3:0] <= {data_in[7:6], data_in[1:0]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				ddrporta[7:0] <= 8'd0;
			else if (wr && ddra)
				ddrporta[7:0] <= data_in[7:0];
		end
	always @(*)
		if (!wr && pra)
			pa_out[7:0] = {porta_in2[7:2], porta_out[1:0]};
		else if (!wr && ddra)
			pa_out[7:0] = ddrporta[7:0];
		else
			pa_out[7:0] = 8'h00;
	assign porta_out[3:0] = {~ddrporta[7:6] | regporta[3:2], ~ddrporta[1:0] | regporta[1:0]};
	reg [7:0] ddrportb;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				ddrportb[7:0] <= 8'd0;
			else if (wr && ddrb)
				ddrportb[7:0] <= data_in[7:0];
		end
	always @(*)
		if (!wr && prb)
			pb_out[7:0] = portb_in[7:0];
		else if (!wr && ddrb)
			pb_out[7:0] = ddrportb[7:0];
		else
			pb_out[7:0] = 8'h00;
	always @(posedge clk)
		if (clk7_en)
			tick_del <= tick;
	cia_int cnt(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.icrs(icrs),
		.ta(ta),
		.tb(tb),
		.alrm(alrm),
		.flag(1'b0),
		.ser(keystrobe | ser_tx_irq),
		.data_in(data_in),
		.data_out(icr_out),
		.irq(irq)
	);
	cia_timera tmra(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(talo),
		.thi(tahi),
		.tcr(cra),
		.data_in(data_in),
		.data_out(tmra_out),
		.eclk(eclk),
		.spmode(spmode),
		.tmra_ovf(tmra_ovf),
		.irq(ta)
	);
	cia_timerb tmrb(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(tblo),
		.thi(tbhi),
		.tcr(crb),
		.data_in(data_in),
		.data_out(tmrb_out),
		.eclk(eclk),
		.tmra_ovf(tmra_ovf),
		.irq(tb)
	);
	cia_timerd tmrd(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(tdlo),
		.tme(tdme),
		.thi(tdhi),
		.tcr(crb),
		.data_in(data_in),
		.data_out(tmrd_out),
		.count(tick & ~tick_del),
		.irq(alrm)
	);
endmodule
module ciab (
	clk,
	clk7_en,
	aen,
	rd,
	wr,
	reset,
	rs,
	data_in,
	data_out,
	tick,
	eclk,
	flag,
	irq,
	porta_in,
	porta_out,
	portb_out
);
	input clk;
	input clk7_en;
	input aen;
	input rd;
	input wr;
	input reset;
	input [3:0] rs;
	input [7:0] data_in;
	output wire [7:0] data_out;
	input tick;
	input eclk;
	input flag;
	output wire irq;
	input [5:0] porta_in;
	output wire [7:6] porta_out;
	output wire [7:0] portb_out;
	wire [7:0] icr_out;
	wire [7:0] tmra_out;
	wire [7:0] tmrb_out;
	wire [7:0] tmrd_out;
	reg [7:0] pa_out;
	reg [7:0] pb_out;
	wire alrm;
	wire ta;
	wire tb;
	wire tmra_ovf;
	reg [7:0] sdr_latch;
	wire [7:0] sdr_out;
	reg tick_del;
	wire pra;
	wire prb;
	wire ddra;
	wire ddrb;
	wire cra;
	wire talo;
	wire tahi;
	wire crb;
	wire tblo;
	wire tbhi;
	wire tdlo;
	wire tdme;
	wire tdhi;
	wire sdr;
	wire icrs;
	wire enable;
	assign enable = aen & (rd | wr);
	assign pra = (enable && (rs == 4'h0) ? 1'b1 : 1'b0);
	assign prb = (enable && (rs == 4'h1) ? 1'b1 : 1'b0);
	assign ddra = (enable && (rs == 4'h2) ? 1'b1 : 1'b0);
	assign ddrb = (enable && (rs == 4'h3) ? 1'b1 : 1'b0);
	assign talo = (enable && (rs == 4'h4) ? 1'b1 : 1'b0);
	assign tahi = (enable && (rs == 4'h5) ? 1'b1 : 1'b0);
	assign tblo = (enable && (rs == 4'h6) ? 1'b1 : 1'b0);
	assign tbhi = (enable && (rs == 4'h7) ? 1'b1 : 1'b0);
	assign tdlo = (enable && (rs == 4'h8) ? 1'b1 : 1'b0);
	assign tdme = (enable && (rs == 4'h9) ? 1'b1 : 1'b0);
	assign tdhi = (enable && (rs == 4'ha) ? 1'b1 : 1'b0);
	assign sdr = (enable && (rs == 4'hc) ? 1'b1 : 1'b0);
	assign icrs = (enable && (rs == 4'hd) ? 1'b1 : 1'b0);
	assign cra = (enable && (rs == 4'he) ? 1'b1 : 1'b0);
	assign crb = (enable && (rs == 4'hf) ? 1'b1 : 1'b0);
	assign data_out = (((((icr_out | tmra_out) | tmrb_out) | tmrd_out) | sdr_out) | pb_out) | pa_out;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				sdr_latch[7:0] <= 8'h00;
			else if (wr & sdr)
				sdr_latch[7:0] <= data_in[7:0];
		end
	assign sdr_out = (!wr && sdr ? sdr_latch[7:0] : 8'h00);
	reg [5:0] porta_in2;
	reg [7:0] regporta;
	reg [7:0] ddrporta;
	always @(posedge clk)
		if (clk7_en)
			porta_in2 <= porta_in;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				regporta[7:0] <= 8'd0;
			else if (wr && pra)
				regporta[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				ddrporta[7:0] <= 8'd0;
			else if (wr && ddra)
				ddrporta[7:0] <= data_in[7:0];
		end
	always @(*)
		if (!wr && pra)
			pa_out[7:0] = {porta_out[7:6], porta_in2[5:0]};
		else if (!wr && ddra)
			pa_out[7:0] = ddrporta[7:0];
		else
			pa_out[7:0] = 8'h00;
	assign porta_out[7:6] = ~ddrporta[7:6] | regporta[7:6];
	reg [7:0] regportb;
	reg [7:0] ddrportb;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				regportb[7:0] <= 8'd0;
			else if (wr && prb)
				regportb[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				ddrportb[7:0] <= 8'd0;
			else if (wr && ddrb)
				ddrportb[7:0] <= data_in[7:0];
		end
	always @(*)
		if (!wr && prb)
			pb_out[7:0] = portb_out[7:0];
		else if (!wr && ddrb)
			pb_out[7:0] = ddrportb[7:0];
		else
			pb_out[7:0] = 8'h00;
	assign portb_out[7:0] = ~ddrportb[7:0] | regportb[7:0];
	always @(posedge clk)
		if (clk7_en)
			tick_del <= tick;
	cia_int cnt(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.icrs(icrs),
		.ta(ta),
		.tb(tb),
		.alrm(alrm),
		.flag(flag),
		.ser(1'b0),
		.data_in(data_in),
		.data_out(icr_out),
		.irq(irq)
	);
	cia_timera tmra(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(talo),
		.thi(tahi),
		.tcr(cra),
		.data_in(data_in),
		.data_out(tmra_out),
		.eclk(eclk),
		.tmra_ovf(tmra_ovf),
		.spmode(),
		.irq(ta)
	);
	cia_timerb tmrb(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(tblo),
		.thi(tbhi),
		.tcr(crb),
		.data_in(data_in),
		.data_out(tmrb_out),
		.eclk(eclk),
		.tmra_ovf(tmra_ovf),
		.irq(tb)
	);
	cia_timerd tmrd(
		.clk(clk),
		.clk7_en(clk7_en),
		.wr(wr),
		.reset(reset),
		.tlo(tdlo),
		.tme(tdme),
		.thi(tdhi),
		.tcr(crb),
		.data_in(data_in),
		.data_out(tmrd_out),
		.count(tick & ~tick_del),
		.irq(alrm)
	);
endmodule
module denise_bitplane_shifter (
	clk,
	clk7_en,
	c1,
	c3,
	load,
	hires,
	shres,
	fmode,
	aga,
	data_in,
	scroll,
	scroller_out,
	scroller_in,
	out
);
	input wire clk;
	input wire clk7_en;
	input wire c1;
	input wire c3;
	input wire load;
	input wire hires;
	input wire shres;
	input wire [1:0] fmode;
	input wire aga;
	input wire [63:0] data_in;
	input wire [7:0] scroll;
	input wire scroller_out;
	output wire scroller_in;
	output wire out;
	reg [63:0] shifter;
	reg shift;
	reg [7:0] sh_scroller;
	reg [2:0] sh_select;
	always @(*)
		if (shres)
			shift = 1'b1;
		else if (hires)
			shift = ~c1 ^ c3;
		else
			shift = ~c1 & ~c3;
	always @(posedge clk)
		if ((load && !c1) && !c3)
			shifter[63:0] <= data_in[63:0];
		else if (shift)
			shifter[63:0] <= {shifter[62:0], 1'b0};
	assign scroller_in = shifter[63];
	always @(*)
		if (shres)
			sh_select = (aga ? 3'b110 : 3'b011);
		else if (hires)
			sh_select = {aga, scroll[0], 1'b1};
		else
			sh_select = {1'b0, scroll[1:0]} + (aga ? 3'd3 : 3'b000);
	always @(posedge clk) sh_scroller[7:0] <= {sh_scroller[6:0], scroller_out};
	assign out = sh_scroller[sh_select];
endmodule
module denise_bitplanes (
	clk,
	clk7_en,
	reset,
	c1,
	c3,
	aga,
	reg_address_in,
	data_in,
	chip48,
	hires,
	shres,
	hpos,
	bpldata
);
	input clk;
	input clk7_en;
	input reset;
	input c1;
	input c3;
	input aga;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	input [47:0] chip48;
	input hires;
	input shres;
	input [8:0] hpos;
	output wire [8:1] bpldata;
	parameter BPLCON1 = 9'h102;
	parameter BPL1DAT = 9'h110;
	parameter BPL2DAT = 9'h112;
	parameter BPL3DAT = 9'h114;
	parameter BPL4DAT = 9'h116;
	parameter BPL5DAT = 9'h118;
	parameter BPL6DAT = 9'h11a;
	parameter BPL7DAT = 9'h11c;
	parameter BPL8DAT = 9'h11e;
	parameter FMODE = 9'h1fc;
	reg [15:0] bplcon1;
	reg [15:0] fmode;
	reg [63:0] bpl1dat;
	reg [63:0] bpl2dat;
	reg [63:0] bpl3dat;
	reg [63:0] bpl4dat;
	reg [63:0] bpl5dat;
	reg [63:0] bpl6dat;
	reg [63:0] bpl7dat;
	reg [63:0] bpl8dat;
	reg load;
	reg [7:0] extra_delay_f0;
	reg [7:0] extra_delay_f12;
	reg [7:0] extra_delay_f3;
	reg [7:0] extra_delay_r;
	reg [7:0] pf1h;
	reg [7:0] pf2h;
	reg [7:0] pf1h_del;
	reg [7:0] pf2h_del;
	always @(hpos)
		case (hpos[3:2])
			2'b00: extra_delay_f0 = 8'b00000000;
			2'b01: extra_delay_f0 = 8'b00110000;
			2'b10: extra_delay_f0 = 8'b00100000;
			2'b11: extra_delay_f0 = 8'b00010000;
		endcase
	always @(hpos)
		case (hpos[4:3])
			2'b00: extra_delay_f12 = 8'b00000000;
			2'b01: extra_delay_f12 = 8'b01100000;
			2'b10: extra_delay_f12 = 8'b01000000;
			2'b11: extra_delay_f12 = 8'b00100000;
		endcase
	always @(hpos)
		case (hpos[5:2])
			4'b0000: extra_delay_f3 = 8'b00000000;
			4'b0001: extra_delay_f3 = 8'b11110000;
			4'b0010: extra_delay_f3 = 8'b11100000;
			4'b0011: extra_delay_f3 = 8'b11010000;
			4'b0100: extra_delay_f3 = 8'b11000000;
			4'b0101: extra_delay_f3 = 8'b10110000;
			4'b0110: extra_delay_f3 = 8'b10100000;
			4'b0111: extra_delay_f3 = 8'b10010000;
			4'b1000: extra_delay_f3 = 8'b10000000;
			4'b1001: extra_delay_f3 = 8'b01110000;
			4'b1010: extra_delay_f3 = 8'b01100000;
			4'b1011: extra_delay_f3 = 8'b01010000;
			4'b1100: extra_delay_f3 = 8'b01000000;
			4'b1101: extra_delay_f3 = 8'b00110000;
			4'b1110: extra_delay_f3 = 8'b00100000;
			4'b1111: extra_delay_f3 = 8'b00010000;
		endcase
	always @(posedge clk)
		if (clk7_en) begin
			if (load)
				extra_delay_r <= #(1) (fmode[1:0] == 2'b00 ? extra_delay_f0 : (fmode[1:0] == 2'b11 ? extra_delay_f3 : extra_delay_f12));
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (load)
				pf1h <= {bplcon1[11:10], bplcon1[3:0], bplcon1[9:8]};
		end
	always @(posedge clk)
		if (clk7_en)
			pf1h_del <= pf1h + extra_delay_r;
	always @(posedge clk)
		if (clk7_en) begin
			if (load)
				pf2h <= {bplcon1[15:14], bplcon1[7:4], bplcon1[13:12]};
		end
	always @(posedge clk)
		if (clk7_en)
			pf2h_del <= pf2h + extra_delay_r;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon1 <= #(1) 16'h3300;
			if (reg_address_in[8:1] == BPLCON1[8:1])
				bplcon1 <= #(1) (aga ? data_in[15:0] : {8'b00110011, data_in[7:0]});
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				fmode <= #(1) 16'h0000;
			else if (aga && (reg_address_in[8:1] == FMODE[8:1]))
				fmode <= #(1) data_in;
		end
	reg [47:0] chip48_fmode = 0;
	always @(*)
		case (fmode[1:0])
			2'b11: chip48_fmode[47:0] = chip48[47:0];
			2'b10, 2'b01: chip48_fmode[47:0] = {chip48[47:32], 32'h00000000};
			default: chip48_fmode[47:0] = 48'h000000000000;
		endcase
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL1DAT[8:1])
				bpl1dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL2DAT[8:1])
				bpl2dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL3DAT[8:1])
				bpl3dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL4DAT[8:1])
				bpl4dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL5DAT[8:1])
				bpl5dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL6DAT[8:1])
				bpl6dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL7DAT[8:1])
				bpl7dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL8DAT[8:1])
				bpl8dat <= {data_in, chip48_fmode};
		end
	always @(posedge clk)
		if (clk7_en)
			load <= (reg_address_in[8:1] == BPL1DAT[8:1] ? 1'b1 : 1'b0);
	reg [5:0] fmode_mask;
	reg shift;
	always @(*)
		case (fmode[1:0])
			2'b00: fmode_mask = 6'b001111;
			2'b01, 2'b10: fmode_mask = 6'b011111;
			2'b11: fmode_mask = 6'b111111;
		endcase
	always @(*)
		if (shres)
			shift = 1'b1;
		else if (hires)
			shift = ~c1 ^ c3;
		else
			shift = ~c1 & ~c3;
	wire [8:1] scroller_in;
	wire [8:1] scroller_out;
	wire [5:0] select1 = (shres ? pf1h_del[5:0] : (hires ? pf1h_del[6:1] : pf1h_del[7:2])) & fmode_mask;
	wire [5:0] select2 = (shres ? pf2h_del[5:0] : (hires ? pf2h_del[6:1] : pf2h_del[7:2])) & fmode_mask;
	denise_scroll_history history(
		.clk(clk),
		.shift(shift),
		.pixel(scroller_in),
		.select1(select1),
		.select2(select2),
		.out(scroller_out)
	);
	denise_bitplane_shifter bplshft1(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl1dat),
		.scroll(pf1h_del),
		.scroller_in(scroller_in[1]),
		.scroller_out(scroller_out[1]),
		.out(bpldata[1])
	);
	denise_bitplane_shifter bplshft2(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl2dat),
		.scroll(pf2h_del),
		.scroller_in(scroller_in[2]),
		.scroller_out(scroller_out[2]),
		.out(bpldata[2])
	);
	denise_bitplane_shifter bplshft3(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl3dat),
		.scroll(pf1h_del),
		.scroller_in(scroller_in[3]),
		.scroller_out(scroller_out[3]),
		.out(bpldata[3])
	);
	denise_bitplane_shifter bplshft4(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl4dat),
		.scroll(pf2h_del),
		.scroller_in(scroller_in[4]),
		.scroller_out(scroller_out[4]),
		.out(bpldata[4])
	);
	denise_bitplane_shifter bplshft5(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl5dat),
		.scroll(pf1h_del),
		.scroller_in(scroller_in[5]),
		.scroller_out(scroller_out[5]),
		.out(bpldata[5])
	);
	denise_bitplane_shifter bplshft6(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl6dat),
		.scroll(pf2h_del),
		.scroller_in(scroller_in[6]),
		.scroller_out(scroller_out[6]),
		.out(bpldata[6])
	);
	denise_bitplane_shifter bplshft7(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl7dat),
		.scroll(pf1h_del),
		.scroller_in(scroller_in[7]),
		.scroller_out(scroller_out[7]),
		.out(bpldata[7])
	);
	denise_bitplane_shifter bplshft8(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.load(load),
		.hires(hires),
		.shres(shres),
		.fmode(fmode[1:0]),
		.aga(aga),
		.data_in(bpl8dat),
		.scroll(pf2h_del),
		.scroller_in(scroller_in[8]),
		.scroller_out(scroller_out[8]),
		.out(bpldata[8])
	);
endmodule
module denise_scroll_history (
	clk,
	shift,
	pixel,
	select1,
	select2,
	out
);
	input clk;
	input shift;
	input [7:0] pixel;
	input [5:0] select1;
	input [5:0] select2;
	output wire [7:0] out;
	reg [5:0] wrptr = 0;
	reg [3:0] odd_mem [0:63];
	reg [3:0] even_mem [0:63];
	integer history_init;
	initial for (history_init = 0; history_init < 64; history_init = history_init + 1)
		begin
			odd_mem[history_init] = 0;
			even_mem[history_init] = 0;
		end
	wire [5:0] rd1 = (wrptr - 6'd1) - select1;
	wire [5:0] rd2 = (wrptr - 6'd1) - select2;
	always @(posedge clk)
		if (shift) begin
			odd_mem[wrptr] <= {pixel[6], pixel[4], pixel[2], pixel[0]};
			even_mem[wrptr] <= {pixel[7], pixel[5], pixel[3], pixel[1]};
			wrptr <= wrptr + 1'b1;
		end
	assign {out[6], out[4], out[2], out[0]} = odd_mem[rd1];
	assign {out[7], out[5], out[3], out[1]} = even_mem[rd2];
endmodule
module denise_collision (
	clk,
	clk7_en,
	reset,
	aga,
	reg_address_in,
	data_in,
	data_out,
	dblpf,
	bpldata,
	nsprite
);
	input clk;
	input clk7_en;
	input reset;
	input aga;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input dblpf;
	input [7:0] bpldata;
	input [7:0] nsprite;
	parameter CLXCON = 9'h098;
	parameter CLXCON2 = 9'h10e;
	parameter CLXDAT = 9'h00e;
	reg [15:0] clxcon;
	reg [15:0] clxcon2;
	reg [14:0] clxdat;
	wire [3:0] sprmatch;
	wire oddmatch;
	wire evenmatch;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				clxcon <= 16'h0fff;
			else if (reg_address_in[8:1] == CLXCON[8:1])
				clxcon <= data_in;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || (reg_address_in[8:1] == CLXCON[8:1]))
				clxcon2 <= #(1) 16'h0000;
			else if (aga && (reg_address_in[8:1] == CLXCON2[8:1]))
				clxcon2 <= #(1) data_in;
		end
	wire [7:0] bm;
	assign bm = (bpldata[7:0] ^ ~{clxcon2[1:0], clxcon[5:0]}) | ~{clxcon2[7:6], clxcon[11:6]};
	assign oddmatch = (((bm[6] & bm[4]) & bm[2]) & bm[0]) & (dblpf | evenmatch);
	assign evenmatch = ((bm[7] & bm[5]) & bm[3]) & bm[1];
	assign sprmatch[0] = nsprite[0] | (nsprite[1] & clxcon[12]);
	assign sprmatch[1] = nsprite[2] | (nsprite[3] & clxcon[13]);
	assign sprmatch[2] = nsprite[4] | (nsprite[5] & clxcon[14]);
	assign sprmatch[3] = nsprite[6] | (nsprite[7] & clxcon[15]);
	wire [14:0] cl;
	reg clxdat_read_del;
	assign cl[0] = evenmatch & oddmatch;
	assign cl[1] = oddmatch & sprmatch[0];
	assign cl[2] = oddmatch & sprmatch[1];
	assign cl[3] = oddmatch & sprmatch[2];
	assign cl[4] = oddmatch & sprmatch[3];
	assign cl[5] = evenmatch & sprmatch[0];
	assign cl[6] = evenmatch & sprmatch[1];
	assign cl[7] = evenmatch & sprmatch[2];
	assign cl[8] = evenmatch & sprmatch[3];
	assign cl[9] = sprmatch[0] & sprmatch[1];
	assign cl[10] = sprmatch[0] & sprmatch[2];
	assign cl[11] = sprmatch[0] & sprmatch[3];
	assign cl[12] = sprmatch[1] & sprmatch[2];
	assign cl[13] = sprmatch[1] & sprmatch[3];
	assign cl[14] = sprmatch[2] & sprmatch[3];
	wire clxdat_read = reg_address_in[8:1] == CLXDAT[8:1];
	always @(posedge clk)
		if (clk7_en)
			clxdat_read_del <= clxdat_read;
	always @(posedge clk)
		if (clk7_en) begin
			if (!clxdat_read & clxdat_read_del)
				clxdat <= 0;
			else
				clxdat <= clxdat[14:0] | cl[14:0];
		end
	assign data_out = (reg_address_in[8:1] == CLXDAT[8:1] ? {1'b1, clxdat[14:0]} : 16'd0);
endmodule
module denise_colortable (
	clk,
	clk7_en,
	reg_address_in,
	data_in,
	rdram,
	select,
	bplxor,
	bank,
	loct,
	ehb_en,
	rgb
);
	input wire clk;
	input wire clk7_en;
	input wire [8:1] reg_address_in;
	input wire [11:0] data_in;
	input wire rdram;
	input wire [7:0] select;
	input wire [7:0] bplxor;
	input wire [2:0] bank;
	input wire loct;
	input wire ehb_en;
	output reg [23:0] rgb;
	parameter COLORBASE = 9'h180;
	wire [7:0] select_xored = select;
	wire [7:0] wr_adr = {bank[2:0], reg_address_in[5:1]};
	wire wr_en = ((reg_address_in[8:6] == COLORBASE[8:6]) && clk7_en) && !rdram;
	wire [11:0] wr_dat = data_in[11:0];
	wire [1:0] wr_ws = (loct ? 2'b01 : 2'b11);
	wire [7:0] rd_adr = (rdram ? wr_adr : (ehb_en ? {3'b000, select_xored[4:0]} : select_xored));
	wire [23:0] rd_dat;
	reg ehb_sel;
	denise_colortable_ram_mf clut(
		.clock(clk),
		.wraddress(wr_adr),
		.wren(wr_en),
		.ena_a(wr_ws),
		.data(wr_dat),
		.rdaddress(rd_adr),
		.q(rd_dat)
	);
	always @(posedge clk) ehb_sel <= #(1) select_xored[5];
	wire [11:0] color_hi = rd_dat[23:12];
	wire [11:0] color_lo = rd_dat[11:0];
	wire [23:0] color = {color_hi[11:8], color_lo[11:8], color_hi[7:4], color_lo[7:4], color_hi[3:0], color_lo[3:0]};
	always @(*)
		if (ehb_sel && ehb_en)
			rgb = {1'b0, color[23:17], 1'b0, color[15:9], 1'b0, color[7:1]};
		else
			rgb = color;
endmodule
module denise_hamgenerator (
	clk,
	clk7_en,
	reg_address_in,
	data_in,
	select,
	bplxor,
	bank,
	loct,
	ham8,
	rgb
);
	input wire clk;
	input wire clk7_en;
	input wire [8:1] reg_address_in;
	input wire [11:0] data_in;
	input wire [7:0] select;
	input wire [7:0] bplxor;
	input wire [2:0] bank;
	input wire loct;
	input wire ham8;
	output reg [23:0] rgb;
	parameter COLORBASE = 9'h180;
	wire [7:0] select_xored = select ^ bplxor;
	wire [7:0] wr_adr = {bank[2:0], reg_address_in[5:1]};
	wire wr_en = (reg_address_in[8:6] == COLORBASE[8:6]) && clk7_en;
	wire [11:0] wr_dat = data_in[11:0];
	wire [1:0] wr_ws = (loct ? 2'b01 : 2'b11);
	wire [7:0] rd_adr = (ham8 ? {2'b00, select_xored[7:2]} : select_xored);
	wire [23:0] rd_dat;
	reg [23:0] rgb_prev;
	reg [7:0] select_r;
	denise_colortable_ram_mf clut(
		.clock(clk),
		.wraddress(wr_adr),
		.wren(wr_en),
		.ena_a(wr_ws),
		.data(wr_dat),
		.rdaddress(rd_adr),
		.q(rd_dat)
	);
	wire [11:0] color_hi = rd_dat[23:12];
	wire [11:0] color_lo = rd_dat[11:0];
	wire [23:0] color = {color_hi[11:8], color_lo[11:8], color_hi[7:4], color_lo[7:4], color_hi[3:0], color_lo[3:0]};
	always @(posedge clk) rgb_prev <= #(1) rgb;
	always @(posedge clk) select_r <= #(1) select_xored;
	always @(*)
		if (ham8)
			case (select_r[1:0])
				2'b00: rgb = color;
				2'b01: rgb = {rgb_prev[23:8], select_r[7:2], rgb_prev[1:0]};
				2'b10: rgb = {select_r[7:2], rgb_prev[17:16], rgb_prev[15:0]};
				2'b11: rgb = {rgb_prev[23:16], select_r[7:2], rgb_prev[9:8], rgb_prev[7:0]};
				default: rgb = color;
			endcase
		else
			case (select_r[5:4])
				2'b00: rgb = color;
				2'b01: rgb = {rgb_prev[23:8], select_r[3:0], select_r[3:0]};
				2'b10: rgb = {select_r[3:0], select_r[3:0], rgb_prev[15:0]};
				2'b11: rgb = {rgb_prev[23:16], select_r[3:0], select_r[3:0], rgb_prev[7:0]};
				default: rgb = color;
			endcase
endmodule
module denise_playfields (
	aga,
	bpldata,
	dblpf,
	pf2of,
	bplcon2,
	nplayfield,
	plfdata
);
	input aga;
	input [8:1] bpldata;
	input dblpf;
	input [2:0] pf2of;
	input [6:0] bplcon2;
	output reg [2:1] nplayfield;
	output reg [7:0] plfdata;
	wire pf2pri;
	wire [2:0] pf2p;
	reg [7:0] pf2of_val;
	assign pf2pri = bplcon2[6];
	assign pf2p = bplcon2[5:3];
	always @(*)
		case (pf2of)
			3'd0: pf2of_val = 8'd0;
			3'd1: pf2of_val = 8'd2;
			3'd2: pf2of_val = 8'd4;
			3'd3: pf2of_val = 8'd8;
			3'd4: pf2of_val = 8'd16;
			3'd5: pf2of_val = 8'd32;
			3'd6: pf2of_val = 8'd64;
			3'd7: pf2of_val = 8'd128;
		endcase
	always @(*)
		if (dblpf) begin
			if (((bpldata[7] || bpldata[5]) || bpldata[3]) || bpldata[1])
				nplayfield[1] = 1;
			else
				nplayfield[1] = 0;
			if (((bpldata[8] || bpldata[6]) || bpldata[4]) || bpldata[2])
				nplayfield[2] = 1;
			else
				nplayfield[2] = 0;
		end
		else begin
			nplayfield[1] = 0;
			if (bpldata[8:1] != 8'b00000000)
				nplayfield[2] = 1;
			else
				nplayfield[2] = 0;
		end
	always @(*)
		if (dblpf) begin
			if (pf2pri) begin
				if (nplayfield[2]) begin
					if (aga)
						plfdata[7:0] = {4'b0000, bpldata[8], bpldata[6], bpldata[4], bpldata[2]} + pf2of_val;
					else
						plfdata[7:0] = {5'b00001, bpldata[6], bpldata[4], bpldata[2]};
				end
				else if (nplayfield[1])
					plfdata[7:0] = {4'b0000, bpldata[7], bpldata[5], bpldata[3], bpldata[1]};
				else
					plfdata[7:0] = 8'b00000000;
			end
			else if (nplayfield[1])
				plfdata[7:0] = {4'b0000, bpldata[7], bpldata[5], bpldata[3], bpldata[1]};
			else if (nplayfield[2]) begin
				if (aga)
					plfdata[7:0] = {4'b0000, bpldata[8], bpldata[6], bpldata[4], bpldata[2]} + pf2of_val;
				else
					plfdata[7:0] = {5'b00001, bpldata[6], bpldata[4], bpldata[2]};
			end
			else
				plfdata[7:0] = 8'b00000000;
		end
		else if (((pf2p > 5) && bpldata[5]) && !aga)
			plfdata[7:0] = 8'b00010000;
		else
			plfdata[7:0] = bpldata[8:1];
endmodule
module denise_spritepriority (
	bplcon2,
	nplayfield,
	nsprite,
	sprsel
);
	input [5:0] bplcon2;
	input [2:1] nplayfield;
	input [7:0] nsprite;
	output reg sprsel;
	reg [2:0] sprcode;
	wire [3:0] sprgroup;
	wire pf1front;
	wire pf2front;
	assign sprgroup[0] = (nsprite[1:0] == 2'd0 ? 1'b0 : 1'b1);
	assign sprgroup[1] = (nsprite[3:2] == 2'd0 ? 1'b0 : 1'b1);
	assign sprgroup[2] = (nsprite[5:4] == 2'd0 ? 1'b0 : 1'b1);
	assign sprgroup[3] = (nsprite[7:6] == 2'd0 ? 1'b0 : 1'b1);
	always @(*)
		if (sprgroup[0])
			sprcode = 3'd1;
		else if (sprgroup[1])
			sprcode = 3'd2;
		else if (sprgroup[2])
			sprcode = 3'd3;
		else if (sprgroup[3])
			sprcode = 3'd4;
		else
			sprcode = 3'd7;
	assign pf1front = (sprcode[2:0] > bplcon2[2:0] ? 1'b1 : 1'b0);
	assign pf2front = (sprcode[2:0] > bplcon2[5:3] ? 1'b1 : 1'b0);
	always @(*)
		if (sprcode[2:0] == 3'd7)
			sprsel = 1'b0;
		else if (pf1front && nplayfield[1])
			sprsel = 1'b0;
		else if (pf2front && nplayfield[2])
			sprsel = 1'b0;
		else
			sprsel = 1'b1;
endmodule
module denise_sprites_shifter (
	clk,
	clk7_en,
	reset,
	aen,
	address,
	hpos,
	fmode,
	shift,
	chip48,
	data_in,
	sprdata,
	attach
);
	input clk;
	input clk7_en;
	input reset;
	input aen;
	input [1:0] address;
	input [8:0] hpos;
	input [15:0] fmode;
	input shift;
	input [47:0] chip48;
	input [15:0] data_in;
	output wire [1:0] sprdata;
	output reg attach;
	parameter POS = 2'b00;
	parameter CTL = 2'b01;
	parameter DATA = 2'b10;
	parameter DATB = 2'b11;
	reg [63:0] datla;
	reg [63:0] datlb;
	reg [63:0] shifta;
	reg [63:0] shiftb;
	reg [8:0] hstart;
	reg armed;
	reg load;
	reg load_del;
	reg [63:0] spr_fmode_dat;
	always @(*)
		case (fmode[3:2])
			2'b00: spr_fmode_dat = {data_in, 48'h000000000000};
			2'b11: spr_fmode_dat = {data_in, chip48[47:0]};
			default: spr_fmode_dat = {data_in, chip48[47:32], 32'h00000000};
		endcase
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				armed <= 0;
			else if (aen && (address == CTL))
				armed <= 0;
			else if (aen && (address == DATA))
				armed <= 1;
		end
	always @(posedge clk)
		if (clk7_en)
			load <= ((armed && (hpos[7:0] == hstart[7:0])) && (fmode[15] || (hpos[8] == hstart[8])) ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (aen && (address == POS))
				hstart[8:1] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (aen && (address == CTL))
				{attach, hstart[0]} <= {data_in[7], data_in[0]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (aen && (address == DATA))
				datla[63:0] <= spr_fmode_dat;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (aen && (address == DATB))
				datlb[63:0] <= spr_fmode_dat;
		end
	always @(posedge clk)
		if (clk7_en && load) begin
			shifta[63:0] <= datla[63:0];
			shiftb[63:0] <= datlb[63:0];
		end
		else if (shift) begin
			shifta[63:0] <= {shifta[62:0], 1'b0};
			shiftb[63:0] <= {shiftb[62:0], 1'b0};
		end
	reg [7:0] sprdata_r;
	always @(posedge clk) sprdata_r <= {shiftb[63], shifta[63], sprdata_r[7:2]};
	assign sprdata[1:0] = sprdata_r[1:0];
endmodule
module denise_sprites (
	clk,
	clk7_en,
	reset,
	c1,
	c3,
	aga,
	reg_address_in,
	hpos,
	data_in,
	chip48,
	sprena,
	esprm,
	osprm,
	spres,
	nsprite,
	sprdata
);
	input clk;
	input clk7_en;
	input reset;
	input c1;
	input c3;
	input aga;
	input [8:1] reg_address_in;
	input [8:0] hpos;
	input [15:0] data_in;
	input [47:0] chip48;
	input sprena;
	input [3:0] esprm;
	input [3:0] osprm;
	input [1:0] spres;
	output wire [7:0] nsprite;
	output reg [7:0] sprdata;
	parameter SPRPOSCTLBASE = 9'h140;
	parameter FMODE = 9'h1fc;
	wire selspr0;
	wire selspr1;
	wire selspr2;
	wire selspr3;
	wire selspr4;
	wire selspr5;
	wire selspr6;
	wire selspr7;
	wire [1:0] sprdat0;
	wire [1:0] sprdat1;
	wire [1:0] sprdat2;
	wire [1:0] sprdat3;
	wire [1:0] sprdat4;
	wire [1:0] sprdat5;
	wire [1:0] sprdat6;
	wire [1:0] sprdat7;
	wire attach0;
	wire attach1;
	wire attach2;
	wire attach3;
	wire attach4;
	wire attach5;
	wire attach6;
	wire attach7;
	wire selsprx;
	assign selsprx = (SPRPOSCTLBASE[8:6] == reg_address_in[8:6] ? 1'b1 : 1'b0);
	assign selspr0 = (selsprx && (reg_address_in[5:3] == 3'd0) ? 1'b1 : 1'b0);
	assign selspr1 = (selsprx && (reg_address_in[5:3] == 3'd1) ? 1'b1 : 1'b0);
	assign selspr2 = (selsprx && (reg_address_in[5:3] == 3'd2) ? 1'b1 : 1'b0);
	assign selspr3 = (selsprx && (reg_address_in[5:3] == 3'd3) ? 1'b1 : 1'b0);
	assign selspr4 = (selsprx && (reg_address_in[5:3] == 3'd4) ? 1'b1 : 1'b0);
	assign selspr5 = (selsprx && (reg_address_in[5:3] == 3'd5) ? 1'b1 : 1'b0);
	assign selspr6 = (selsprx && (reg_address_in[5:3] == 3'd6) ? 1'b1 : 1'b0);
	assign selspr7 = (selsprx && (reg_address_in[5:3] == 3'd7) ? 1'b1 : 1'b0);
	reg [15:0] fmode;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				fmode <= #(1) 16'h0000;
			else if (aga && (reg_address_in[8:1] == FMODE[8:1]))
				fmode <= #(1) data_in;
		end
	reg shift;
	always @(*)
		case (spres)
			2'b11: shift = 1'b1;
			2'b10: shift = ~c1 ^ c3;
			default: shift = ~c1 & ~c3;
		endcase
	denise_sprites_shifter sps0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr0),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat0),
		.attach(attach0)
	);
	denise_sprites_shifter sps1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr1),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat1),
		.attach(attach1)
	);
	denise_sprites_shifter sps2(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr2),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat2),
		.attach(attach2)
	);
	denise_sprites_shifter sps3(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr3),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat3),
		.attach(attach3)
	);
	denise_sprites_shifter sps4(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr4),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat4),
		.attach(attach4)
	);
	denise_sprites_shifter sps5(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr5),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat5),
		.attach(attach5)
	);
	denise_sprites_shifter sps6(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr6),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat6),
		.attach(attach6)
	);
	denise_sprites_shifter sps7(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aen(selspr7),
		.address(reg_address_in[2:1]),
		.hpos(hpos),
		.fmode(fmode),
		.shift(shift),
		.chip48(chip48),
		.data_in(data_in),
		.sprdata(sprdat7),
		.attach(attach7)
	);
	assign nsprite[0] = (sprena && (sprdat0[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[1] = (sprena && (sprdat1[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[2] = (sprena && (sprdat2[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[3] = (sprena && (sprdat3[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[4] = (sprena && (sprdat4[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[5] = (sprena && (sprdat5[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[6] = (sprena && (sprdat6[1:0] != 2'b00) ? 1'b1 : 1'b0);
	assign nsprite[7] = (sprena && (sprdat7[1:0] != 2'b00) ? 1'b1 : 1'b0);
	always @(*)
		if (nsprite[1:0] != 2'b00) begin
			if (attach1 || (!aga && attach0))
				sprdata[7:0] = {osprm, sprdat1[1:0], sprdat0[1:0]};
			else if (nsprite[0])
				sprdata[7:0] = {esprm, 2'b00, sprdat0[1:0]};
			else
				sprdata[7:0] = {osprm, 2'b00, sprdat1[1:0]};
		end
		else if (nsprite[3:2] != 2'b00) begin
			if (attach3 || (!aga && attach2))
				sprdata[7:0] = {osprm, sprdat3[1:0], sprdat2[1:0]};
			else if (nsprite[2])
				sprdata[7:0] = {esprm, 2'b01, sprdat2[1:0]};
			else
				sprdata[7:0] = {osprm, 2'b01, sprdat3[1:0]};
		end
		else if (nsprite[5:4] != 2'b00) begin
			if (attach5 || (!aga && attach4))
				sprdata[7:0] = {osprm, sprdat5[1:0], sprdat4[1:0]};
			else if (nsprite[4])
				sprdata[7:0] = {esprm, 2'b10, sprdat4[1:0]};
			else
				sprdata[7:0] = {osprm, 2'b10, sprdat5[1:0]};
		end
		else if (nsprite[7:6] != 2'b00) begin
			if (attach7 || (!aga && attach6))
				sprdata[7:0] = {osprm, sprdat7[1:0], sprdat6[1:0]};
			else if (nsprite[6])
				sprdata[7:0] = {esprm, 2'b11, sprdat6[1:0]};
			else
				sprdata[7:0] = {osprm, 2'b11, sprdat7[1:0]};
		end
		else
			sprdata[7:0] = 8'b00000000;
endmodule
module denise (
	clk,
	clk7_en,
	c1,
	c3,
	cck,
	reset,
	strhor,
	reg_address_in,
	data_in,
	chip48,
	data_out,
	blank,
	blank_out,
	red,
	green,
	blue,
	a1k,
	ecs,
	aga,
	hires,
	shres
);
	input wire clk;
	input wire clk7_en;
	input wire c1;
	input wire c3;
	input wire cck;
	input wire reset;
	input wire strhor;
	input wire [8:1] reg_address_in;
	input wire [15:0] data_in;
	input wire [47:0] chip48;
	output wire [15:0] data_out;
	input wire blank;
	output wire blank_out;
	output wire [7:0] red;
	output wire [7:0] green;
	output wire [7:0] blue;
	input wire a1k;
	input wire ecs;
	input wire aga;
	output wire hires;
	output wire shres;
	parameter DIWSTRT = 9'h08e;
	parameter DIWSTOP = 9'h090;
	parameter DIWHIGH = 9'h1e4;
	parameter BPLCON0 = 9'h100;
	parameter BPLCON2 = 9'h104;
	parameter BPLCON3 = 9'h106;
	parameter BPLCON4 = 9'h10c;
	parameter DENISEID = 9'h07c;
	parameter BPL1DAT = 9'h110;
	parameter COLORBASE = 9'h180;
	reg [8:0] hpos;
	reg [3:0] l_bpu;
	reg [8:0] hdiwstrt;
	reg [8:0] hdiwstop;
	wire [8:1] bpldata_out;
	wire [8:1] bpldata;
	wire [7:0] sprdata;
	wire [7:0] plfdata;
	wire [2:1] nplayfield;
	wire [7:0] nsprite;
	wire sprsel;
	wire [23:0] ham_rgb;
	reg [7:0] clut_data;
	reg window;
	wire [15:0] deniseid_out;
	wire [15:0] col_out;
	wire [15:0] rgb_out;
	reg display_ena;
	assign data_out = (col_out | deniseid_out) | rgb_out;
	always @(posedge clk)
		if (clk7_en) begin
			if (strhor)
				hpos <= 9'd2;
			else
				hpos <= hpos + 9'd1;
		end
	reg blank_d;
	always @(posedge clk)
		if (clk7_en) begin
			blank_d <= blank;
			if (reset || (blank & !blank_d))
				display_ena <= 0;
			else if (reg_address_in[8:1] == BPL1DAT[8:1])
				display_ena <= 1;
		end
	reg [15:0] bplcon0;
	wire [3:0] bpu;
	wire ham;
	wire dpf;
	wire color;
	wire gaud;
	wire uhres;
	wire bypass;
	wire lpen;
	wire lace;
	wire ersy;
	wire ecsena;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon0 <= #(1) 16'h0000;
			else if (reg_address_in[8:1] == BPLCON0[8:1])
				bplcon0[15:0] <= #(1) data_in[15:0];
		end
	assign hires = bplcon0[15];
	assign bpu = {bplcon0[4] & aga, bplcon0[14:12]};
	assign ham = bplcon0[11];
	assign dpf = bplcon0[10];
	assign color = bplcon0[9];
	assign gaud = bplcon0[8];
	assign uhres = bplcon0[7];
	assign shres = bplcon0[6];
	assign bypass = bplcon0[5];
	assign lpen = bplcon0[3];
	assign lace = bplcon0[2];
	assign ersy = bplcon0[1];
	assign ecsena = bplcon0[0];
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL1DAT[8:1]) begin
				if (aga)
					l_bpu <= #(1) bpu;
				else
					l_bpu <= #(1) (bpu == 4'd7 ? 4'd6 : bpu);
			end
		end
	reg [15:0] bplcon2;
	wire rdram;
	wire killehb;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon2 <= 16'h0000;
			else if (reg_address_in[8:1] == BPLCON2[8:1])
				bplcon2[15:0] <= data_in[15:0];
		end
	assign rdram = bplcon2[8] & aga;
	wire [23:0] clut_rgb;
	wire loct;
	assign rgb_out = ((reg_address_in[8:6] == COLORBASE[8:6]) && rdram ? {4'b0000, (loct ? {clut_rgb[19:16], clut_rgb[11:8], clut_rgb[3:0]} : {clut_rgb[23:20], clut_rgb[15:12], clut_rgb[7:4]})} : 16'h0000);
	assign killehb = bplcon2[9] & ecs;
	reg [15:0] bplcon3;
	wire [2:0] bank;
	wire [2:0] pf2of;
	wire [1:0] spres;
	wire brdrblnk;
	wire brdntran;
	wire zdclken;
	wire brdsprt;
	wire extblken;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon3 <= 16'h0c00;
			else if (reg_address_in[8:1] == BPLCON3[8:1])
				bplcon3[15:0] <= data_in[15:0];
		end
	assign bank = bplcon3[15:13] & {3 {aga}};
	assign pf2of = bplcon3[12:10];
	assign loct = bplcon3[9] & aga;
	assign spres = bplcon3[7:6] & {2 {aga}};
	assign brdrblnk = bplcon3[5] & ecsena;
	assign brdntran = bplcon3[4] & ecsena;
	assign zdclken = bplcon3[2] & ecsena;
	assign brdsprt = bplcon3[1] & ecsena;
	assign extblken = bplcon3[0] & ecsena;
	reg [15:0] bplcon4;
	reg [7:0] bplxor;
	reg [3:0] esprm;
	reg [3:0] osprm;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon4 <= 16'h0011;
			else if (aga && (reg_address_in[8:1] == BPLCON4[8:1]))
				bplcon4[15:0] <= data_in[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset || (hpos[8:0] == hdiwstop[8:0]))
				bplxor <= 8'd0;
			else if (display_ena)
				bplxor <= bplcon4[15:8];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				esprm <= 4'd1;
				osprm <= 4'd1;
			end
			else begin
				esprm <= bplcon4[7:4];
				osprm <= bplcon4[3:0];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTRT[8:1])
				hdiwstrt[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTRT[8:1])
				hdiwstrt[8] <= 1'b0;
			else if ((reg_address_in[8:1] == DIWHIGH[8:1]) && ecs)
				hdiwstrt[8] <= data_in[5];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTOP[8:1])
				hdiwstop[7:0] <= data_in[7:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTOP[8:1])
				hdiwstop[8] <= 1'b1;
			else if ((reg_address_in[8:1] == DIWHIGH[8:1]) && ecs)
				hdiwstop[8] <= data_in[13];
		end
	assign deniseid_out = (reg_address_in[8:1] == DENISEID[8:1] ? (aga ? 16'h00f8 : (ecs ? 16'hfffc : 16'hffff)) : 16'h0000);
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[8:0] == hdiwstrt[8:0])
				window <= 1;
			else if (hpos[8:0] == hdiwstop[8:0])
				window <= 0;
		end
	reg window_ena;
	always @(posedge clk)
		if (clk7_en)
			window_ena <= window;
	denise_bitplanes bplm0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.c1(c1),
		.c3(c3),
		.aga(aga),
		.reg_address_in(reg_address_in),
		.data_in(data_in),
		.chip48(chip48),
		.hires(hires),
		.shres(shres & ecs),
		.hpos(hpos),
		.bpldata(bpldata_out)
	);
	assign bpldata[1] = (window_ena && (l_bpu > 0) ? bpldata_out[1] : 1'b0);
	assign bpldata[2] = (window_ena && (l_bpu > 1) ? bpldata_out[2] : 1'b0);
	assign bpldata[3] = (window_ena && (l_bpu > 2) ? bpldata_out[3] : 1'b0);
	assign bpldata[4] = (window_ena && (l_bpu > 3) ? bpldata_out[4] : 1'b0);
	assign bpldata[5] = (window_ena && (l_bpu > 4) ? bpldata_out[5] : 1'b0);
	assign bpldata[6] = (window_ena && (l_bpu > 5) ? bpldata_out[6] : 1'b0);
	assign bpldata[7] = (window_ena && (l_bpu > 6) ? bpldata_out[7] : 1'b0);
	assign bpldata[8] = (window_ena && (l_bpu > 7) ? bpldata_out[8] : 1'b0);
	denise_playfields plfm0(
		.aga(aga),
		.bpldata(bpldata),
		.dblpf(dpf),
		.pf2of(pf2of),
		.bplcon2(bplcon2[6:0]),
		.nplayfield(nplayfield),
		.plfdata(plfdata)
	);
	wire [1:0] spres_d;
	assign spres_d = (spres == 2'b00 ? (shres ? 2'b10 : 2'b01) : spres);
	denise_sprites sprm0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.c1(c1),
		.c3(c3),
		.aga(aga),
		.reg_address_in(reg_address_in),
		.hpos(hpos),
		.data_in(data_in),
		.chip48(chip48),
		.sprena(display_ena | brdsprt),
		.esprm(esprm),
		.osprm(osprm),
		.spres(spres_d),
		.nsprite(nsprite),
		.sprdata(sprdata)
	);
	wire [2:1] nplayfield_masked = nplayfield & {window_ena, window_ena};
	denise_spritepriority spm0(
		.bplcon2(bplcon2[5:0]),
		.nplayfield(nplayfield_masked),
		.nsprite(nsprite),
		.sprsel(sprsel)
	);
	wire ehb_en;
	assign ehb_en = (((!killehb && !a1k) && !ham) && !dpf) && (l_bpu == 4'd6);
	denise_colortable clut0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reg_address_in(reg_address_in),
		.data_in(data_in[11:0]),
		.rdram(rdram),
		.select(clut_data),
		.bplxor(bplxor),
		.bank(bank),
		.loct(loct),
		.ehb_en(ehb_en),
		.rgb(clut_rgb)
	);
	wire ham8 = ham && (l_bpu == 4'd8);
	wire ham_sel = ham;
	denise_hamgenerator ham0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reg_address_in(reg_address_in),
		.data_in(data_in[11:0]),
		.select(bpldata),
		.bplxor(bplxor),
		.bank(bank),
		.loct(loct),
		.ham8(ham8),
		.rgb(ham_rgb)
	);
	denise_collision col0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.aga(aga),
		.reg_address_in(reg_address_in),
		.data_in(data_in),
		.data_out(col_out),
		.dblpf(dpf),
		.bpldata(bpldata),
		.nsprite(nsprite)
	);
	always @(*)
		if (brdsprt && sprsel)
			clut_data = sprdata;
		else if (!window_ena)
			clut_data = 8'b00000000;
		else if (sprsel)
			clut_data = sprdata;
		else
			clut_data = plfdata ^ bplxor;
	reg window_del;
	reg sprsel_del;
	always @(posedge clk) begin
		window_del <= window_ena;
		sprsel_del <= sprsel;
	end
	wire [23:0] out_rgb = ((ham_sel && window_del) && !sprsel_del ? ham_rgb : clut_rgb);
	wire t_blank;
	assign t_blank = blank | (((ecs & ecsena) & brdrblnk) & (~window_del | ~display_ena));
	assign {red[7:0], green[7:0], blue[7:0]} = (t_blank ? 24'h000000 : out_rgb);
	assign blank_out = t_blank;
endmodule
module gayle (
	clk,
	reset,
	addr,
	data_in,
	data_out,
	rd,
	wr,
	sel_ide,
	sel_gayle,
	irq,
	nrdy,
	longword,
	ide_req,
	ide_address,
	ide_write,
	ide_writedata,
	ide_read,
	ide_readdata,
	led
);
	input clk;
	input reset;
	input [23:1] addr;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input rd;
	input wr;
	input sel_ide;
	input sel_gayle;
	output wire irq;
	output wire nrdy;
	input longword;
	output wire [5:0] ide_req;
	input [4:0] ide_address;
	input ide_write;
	input [15:0] ide_writedata;
	input ide_read;
	output wire [15:0] ide_readdata;
	output wire led;
	wire sel_gayleid = sel_gayle && (addr[15:12] == 4'b0001);
	wire sel_tfr = sel_ide && (addr[15:14] == 2'b00);
	wire sel_cs = sel_ide && (addr[15:12] == 4'b1000);
	wire sel_intreq = sel_ide && (addr[15:12] == 4'b1001);
	wire sel_intena = sel_ide && (addr[15:12] == 4'b1010);
	wire sel_cfg = sel_ide && (addr[15:12] == 4'b1011);
	reg old_wr;
	wire wr_stb = ~old_wr & wr;
	always @(posedge clk) old_wr <= wr;
	reg old_reset = 0;
	wire reset_stb = ~old_reset & reset;
	always @(posedge clk) old_reset <= reset;
	reg [1:0] cs;
	reg [5:0] cs_mask;
	always @(posedge clk)
		if (reset) begin
			cs_mask <= 0;
			cs <= 0;
		end
		else if (wr_stb && sel_cs) begin
			cs_mask <= data_in[15:10];
			cs <= data_in[9:8];
		end
	reg [3:0] cfg;
	always @(posedge clk) begin
		if (reset)
			cfg <= 0;
		if (wr_stb && sel_cfg)
			cfg <= data_in[15:12];
	end
	reg [1:0] gayleid_cnt;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg old_rd;
		old_rd <= rd & sel_gayleid;
		if (wr_stb & sel_gayleid)
			gayleid_cnt <= 0;
		if (old_rd & ~(rd & sel_gayleid))
			gayleid_cnt <= gayleid_cnt + 1'd1;
	end
	reg intena;
	always @(posedge clk)
		if (reset)
			intena <= 0;
		else if (sel_intena && wr_stb)
			intena <= data_in[15];
	reg longword_r;
	always @(posedge clk) longword_r <= (rd && longword) && !addr[4:1];
	wire port_num = addr[12];
	wire data_wr = wr_stb || ((addr[4:1] == 1) && wr);
	wire io_32 = (longword_r | longword) && rd;
	wire [31:0] ide0_data;
	wire [15:0] ide0_readdata;
	wire ide0_intreq;
	wire ide0_drq;
	wire ide0_nodata;
	ide ide0(
		.clk(clk),
		.rst_n(~reset_stb),
		.irq(ide0_intreq),
		.io_address({1'b0, addr[4:2]}),
		.io_read((rd & sel_tfr) & ~port_num),
		.io_write((data_wr & sel_tfr) & ~port_num),
		.io_readdata({ide0_data[23:16], ide0_data[31:24], ide0_data[7:0], ide0_data[15:8]}),
		.io_writedata({16'd0, data_in[7:0], data_in[15:8]}),
		.io_32(io_32),
		.io_wait(),
		.request(ide_req[2:0]),
		.drq(ide0_drq),
		.use_fast(1'b1),
		.no_data(ide0_nodata),
		.drive_en(),
		.mgmt_address(ide_address[3:0]),
		.mgmt_write(ide_write & ~ide_address[4]),
		.mgmt_writedata(ide_writedata),
		.mgmt_read(ide_read & ~ide_address[4]),
		.mgmt_readdata(ide0_readdata)
	);
	wire [31:0] ide1_data;
	wire [15:0] ide1_readdata;
	wire ide1_intreq;
	wire ide1_drq;
	wire ide1_nodata;
	ide ide1(
		.clk(clk),
		.rst_n(~reset_stb),
		.irq(ide1_intreq),
		.io_address({1'b0, addr[4:2]}),
		.io_read((rd & sel_tfr) & port_num),
		.io_write((data_wr & sel_tfr) & port_num),
		.io_readdata({ide1_data[23:16], ide1_data[31:24], ide1_data[7:0], ide1_data[15:8]}),
		.io_writedata({16'd0, data_in[7:0], data_in[15:8]}),
		.io_32(io_32),
		.io_wait(),
		.request(ide_req[5:3]),
		.drq(ide1_drq),
		.drive_en(),
		.use_fast(1'b1),
		.no_data(ide1_nodata),
		.mgmt_address(ide_address[3:0]),
		.mgmt_write(ide_write & ide_address[4]),
		.mgmt_writedata(ide_writedata),
		.mgmt_read(ide_read & ide_address[4]),
		.mgmt_readdata(ide1_readdata)
	);
	wire intreq = ide0_intreq | ide1_intreq;
	wire gayleid = ~gayleid_cnt[1] | gayleid_cnt[0];
	wire [31:0] tfr = (port_num ? ide1_data : ide0_data);
	assign data_out = (((((sel_tfr & rd ? (longword_r & addr[1] ? tfr[31:16] : tfr[15:0]) : 16'h0000) | (sel_cs & rd ? {cs_mask[5] || intreq, cs_mask[4:0], cs, 8'h00} : 16'h0000)) | (sel_intreq & rd ? {intreq, 15'b000000000000000} : 16'h0000)) | (sel_intena & rd ? {intena, 15'b000000000000000} : 16'h0000)) | (sel_gayleid & rd ? {gayleid, 15'b000000000000000} : 16'h0000)) | (sel_cfg & rd ? {cfg, 12'b000000000000} : 16'h0000);
	assign ide_readdata = (ide_address[4] ? ide1_readdata : ide0_readdata);
	assign led = ide0_drq | ide1_drq;
	assign nrdy = (sel_tfr & !addr[4:2]) & (port_num ? ide1_nodata : ide0_nodata);
	assign irq = intreq & intena;
endmodule
module minimig_bankmapper (
	chip0,
	chip1,
	chip2,
	chip3,
	slow0,
	slow1,
	slow2,
	kick,
	kick256kmirror,
	memory_config,
	bank
);
	input chip0;
	input chip1;
	input chip2;
	input chip3;
	input slow0;
	input slow1;
	input slow2;
	input kick;
	input kick256kmirror;
	input [1:0] memory_config;
	output wire [7:0] bank;
	reg [7:0] bank_r;
	assign bank = bank_r;
	always @(*) begin
		bank_r[7:4] = {kick, kick256kmirror, ((chip3 | chip2) | chip1) | chip0, (slow0 | slow1) | slow2};
		case (memory_config)
			0: bank_r[3:0] = {3'b000, ((chip3 | chip2) | chip1) | chip0};
			1: bank_r[3:0] = {2'b00, chip3 | chip1, chip2 | chip0};
			2: bank_r[3:0] = {1'b0, chip2, chip1, chip0};
			3: bank_r[3:0] = {chip3, chip2, chip1, chip0};
		endcase
	end
endmodule
module minimig_m68k_bridge (
	clk,
	clk7_en,
	clk7n_en,
	c1,
	c3,
	eclk,
	vpa,
	dbr,
	dbs,
	xbs,
	nrdy,
	bls,
	cck,
	memory_config,
	_as,
	_lds,
	_uds,
	r_w,
	_dtack,
	rd,
	hwr,
	lwr,
	address,
	address_out,
	data,
	cpudatain,
	data_out,
	data_in,
	rd_cyc
);
	input clk;
	input clk7_en;
	input clk7n_en;
	input c1;
	input c3;
	input [9:0] eclk;
	input vpa;
	input dbr;
	input dbs;
	input xbs;
	input nrdy;
	output wire bls;
	input cck;
	input [3:0] memory_config;
	input _as;
	input _lds;
	input _uds;
	input r_w;
	output wire _dtack;
	output wire rd;
	output wire hwr;
	output wire lwr;
	input [23:1] address;
	output wire [23:1] address_out;
	output wire [15:0] data;
	input [15:0] cpudatain;
	output wire [15:0] data_out;
	input [15:0] data_in;
	output wire rd_cyc;
	reg lvpa;
	always @(posedge clk)
		if (clk7_en)
			lvpa <= vpa;
	reg vma;
	always @(posedge clk)
		if (clk7_en) begin
			if (eclk[9])
				vma <= 0;
			else if (eclk[3] && lvpa)
				vma <= 1;
		end
	reg lr_w;
	reg l_as;
	reg l_dtack;
	always @(posedge clk)
		if (clk7_en) begin
			lr_w <= r_w;
			l_as <= _as;
			l_dtack <= _dtack;
		end
	reg l_uds;
	reg l_lds;
	always @(posedge clk) begin
		l_uds <= _uds;
		l_lds <= _lds;
	end
	wire _as_and_cs = _as;
	reg _ta_n;
	always @(posedge clk or posedge _as_and_cs)
		if (_as_and_cs)
			_ta_n <= 1;
		else if (clk7n_en) begin
			if (((!l_as && cck) && ((!vpa && !(dbr && dbs)) || ((vpa && vma) && eclk[8]))) && !nrdy)
				_ta_n <= 0;
		end
	assign _dtack = _ta_n;
	wire enable = (~l_as & ~l_dtack) & ~cck;
	assign rd = enable & lr_w;
	assign hwr = (enable & ~lr_w) & ~l_uds;
	assign lwr = (enable & ~lr_w) & ~l_lds;
	assign rd_cyc = ~l_as & lr_w;
	assign bls = (dbs & ~l_as) & l_dtack;
	reg [15:0] cpudatain_r;
	always @(posedge clk) cpudatain_r <= cpudatain;
	assign data_out = cpudatain_r;
	reg [15:0] ldata_in;
	always @(posedge clk)
		if ((!c1 && c3) && enable)
			ldata_in <= data_in;
	assign data[15:0] = ldata_in;
	reg [23:1] address_r;
	always @(posedge clk) address_r <= address;
	assign address_out[23:1] = address_r;
endmodule
module minimig_syscontrol (
	clk,
	clk7_en,
	cnt,
	mrst,
	reset
);
	input clk;
	input clk7_en;
	input cnt;
	input mrst;
	output wire reset;
	reg reset_R = 1'b1;
	assign reset = reset_R;
	reg [2:0] rst_cnt = 0;
	always @(posedge clk)
		if (clk7_en) begin
			if (mrst)
				rst_cnt <= 0;
			else if (~rst_cnt[2] && cnt)
				rst_cnt <= rst_cnt + 3'd1;
			reset_R <= ~rst_cnt[2];
		end
endmodule
module paula_audio_channel (
	clk,
	clk7_en,
	cck,
	reset,
	aen,
	dmaena,
	reg_address_in,
	data,
	volume,
	sample,
	sample_okk,
	intreq,
	intpen,
	dmareq,
	dmas,
	strhor
);
	input clk;
	input clk7_en;
	input cck;
	input reset;
	input aen;
	input dmaena;
	input [3:1] reg_address_in;
	input [15:0] data;
	output wire [6:0] volume;
	output wire [7:0] sample;
	output wire [7:0] sample_okk;
	output wire intreq;
	input intpen;
	output reg dmareq;
	output reg dmas;
	input strhor;
	parameter AUDLEN = 4'h4;
	parameter AUDPER = 4'h6;
	parameter AUDVOL = 4'h8;
	parameter AUDDAT = 4'ha;
	reg [15:0] audlen;
	reg [15:0] audper;
	reg [6:0] audvol;
	reg [15:0] auddat;
	reg [15:0] datbuf;
	reg [2:0] audio_state;
	reg [2:0] audio_next;
	wire datwrite;
	reg [5:0] volcnt;
	wire volcount;
	reg volcntrld;
	wire volgate;
	reg pbufld1;
	reg [15:0] percnt;
	reg percount;
	reg percntrld;
	wire perfin;
	reg [15:0] lencnt;
	reg lencount;
	reg lencntrld;
	wire lenfin;
	reg AUDxDAT;
	wire AUDxON;
	reg AUDxDR;
	reg AUDxIR;
	wire AUDxIP;
	reg intreq2_set;
	reg intreq2_clr;
	reg intreq2;
	reg dmasen;
	reg penhi;
	reg silence;
	reg silence_d;
	reg dmaena_d;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				audlen[15:0] <= 16'h0000;
			else if (aen && (reg_address_in[3:1] == AUDLEN[3:1]))
				audlen[15:0] <= data[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				audper[15:0] <= 16'h0000;
			else if (aen && (reg_address_in[3:1] == AUDPER[3:1]))
				audper[15:0] <= data[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				audvol[6:0] <= 7'b0000000;
			else if (aen && (reg_address_in[3:1] == AUDVOL[3:1]))
				audvol[6:0] <= data[6:0];
		end
	assign datwrite = (aen && (reg_address_in[3:1] == AUDDAT[3:1]) ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				auddat[15:0] <= 16'h0000;
			else if (datwrite)
				auddat[15:0] <= data[15:0];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (datwrite)
				AUDxDAT <= 1'b1;
			else if (cck)
				AUDxDAT <= 1'b0;
		end
	assign AUDxON = dmaena;
	assign AUDxIP = intpen;
	assign intreq = AUDxIR;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset && cck)
				volcnt <= 6'h00;
			else if (volcntrld && cck)
				volcnt[5:0] <= audvol[5:0];
			else if (volcount && cck)
				volcnt[5:0] <= volcnt[5:0] - 6'd1;
		end
	assign volcount = 1'd1;
	assign volgate = volcnt < audvol;
	always @(posedge clk)
		if (clk7_en) begin
			if (percntrld && cck)
				percnt[15:0] <= audper[15:0];
			else if (percount && cck)
				percnt[15:0] <= percnt[15:0] - 16'd1;
		end
	assign perfin = ((percnt[15:0] == 1) && cck ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (lencntrld && cck) begin
				lencnt[15:0] <= audlen[15:0];
				silence <= 1'b0;
				if ((audlen == 1) || (audlen == 0))
					silence <= 1'b1;
			end
			else if (lencount && cck)
				lencnt[15:0] <= lencnt[15:0] - 1'd1;
			dmaena_d <= dmaena;
			if ((dmaena_d == 1'b1) && (dmaena == 1'b0)) begin
				silence_d <= 1'b1;
				silence <= 1'b1;
			end
			if (AUDxDAT && cck) begin
				if (silence_d)
					silence_d <= 1'b0;
				else
					silence <= 1'b0;
			end
		end
	assign lenfin = ((lencnt[15:0] == 1) && cck ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				datbuf[15:0] <= 16'h0000;
			else if (pbufld1 && cck)
				datbuf[15:0] <= auddat[15:0];
		end
	assign sample[7:0] = (silence ? 8'b00000000 : (penhi ? datbuf[15:8] : datbuf[7:0]));
	assign sample_okk[7:0] = (silence | !volgate ? 8'b00000000 : (penhi ? datbuf[15:8] : datbuf[7:0]));
	assign volume[6:0] = audvol[6:0];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				dmareq <= 1'b0;
				dmas <= 1'b0;
			end
			else if (AUDxDR && cck) begin
				dmareq <= 1'b1;
				dmas <= dmasen | lenfin;
			end
			else if (strhor) begin
				dmareq <= 1'b0;
				dmas <= 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (cck) begin
				if (intreq2_set)
					intreq2 <= 1'b1;
				else if (intreq2_clr)
					intreq2 <= 1'b0;
			end
		end
	parameter AUDIO_STATE_0 = 3'b000;
	parameter AUDIO_STATE_1 = 3'b001;
	parameter AUDIO_STATE_2 = 3'b011;
	parameter AUDIO_STATE_3 = 3'b010;
	parameter AUDIO_STATE_4 = 3'b110;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				audio_state <= AUDIO_STATE_0;
			else if (cck)
				audio_state <= audio_next;
		end
	always @(*)
		case (audio_state)
			AUDIO_STATE_0: begin
				intreq2_clr = 1'b1;
				intreq2_set = 1'b0;
				lencount = 1'b0;
				penhi = 1'b0;
				percount = 1'b0;
				percntrld = 1'b1;
				if (AUDxON) begin
					audio_next = AUDIO_STATE_1;
					AUDxDR = 1'b1;
					AUDxIR = 1'b0;
					dmasen = 1'b1;
					lencntrld = 1'b1;
					pbufld1 = 1'b0;
					volcntrld = 1'b0;
				end
				else if ((AUDxDAT && !AUDxON) && !AUDxIP) begin
					audio_next = AUDIO_STATE_3;
					AUDxDR = 1'b0;
					AUDxIR = 1'b1;
					dmasen = 1'b0;
					lencntrld = 1'b0;
					pbufld1 = 1'b1;
					volcntrld = 1'b1;
				end
				else begin
					audio_next = AUDIO_STATE_0;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					dmasen = 1'b0;
					lencntrld = 1'b0;
					pbufld1 = 1'b0;
					volcntrld = 1'b0;
				end
			end
			AUDIO_STATE_1: begin
				dmasen = 1'b0;
				intreq2_clr = 1'b1;
				intreq2_set = 1'b0;
				lencntrld = 1'b0;
				penhi = 1'b0;
				percount = 1'b0;
				if (AUDxON && AUDxDAT) begin
					audio_next = AUDIO_STATE_2;
					AUDxDR = 1'b1;
					AUDxIR = 1'b1;
					lencount = ~lenfin;
					pbufld1 = 1'b0;
					percntrld = 1'b0;
					volcntrld = 1'b0;
				end
				else if (!AUDxON) begin
					audio_next = AUDIO_STATE_0;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					lencount = 1'b0;
					pbufld1 = 1'b0;
					percntrld = 1'b0;
					volcntrld = 1'b0;
				end
				else begin
					audio_next = AUDIO_STATE_1;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					lencount = 1'b0;
					pbufld1 = 1'b0;
					percntrld = 1'b0;
					volcntrld = 1'b0;
				end
			end
			AUDIO_STATE_2: begin
				dmasen = 1'b0;
				intreq2_clr = 1'b1;
				intreq2_set = 1'b0;
				lencntrld = 1'b0;
				penhi = 1'b0;
				percount = 1'b0;
				if (AUDxON && AUDxDAT) begin
					audio_next = AUDIO_STATE_3;
					AUDxDR = 1'b1;
					AUDxIR = 1'b0;
					lencount = ~lenfin;
					pbufld1 = 1'b1;
					percntrld = 1'b1;
					volcntrld = 1'b1;
				end
				else if (!AUDxON) begin
					audio_next = AUDIO_STATE_0;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					lencount = 1'b0;
					pbufld1 = 1'b0;
					percntrld = 1'b0;
					volcntrld = 1'b0;
				end
				else begin
					audio_next = AUDIO_STATE_2;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					lencount = 1'b0;
					pbufld1 = 1'b0;
					percntrld = 1'b0;
					volcntrld = 1'b0;
				end
			end
			AUDIO_STATE_3: begin
				AUDxDR = 1'b0;
				AUDxIR = 1'b0;
				dmasen = 1'b0;
				intreq2_clr = 1'b0;
				intreq2_set = (lenfin & AUDxON) & AUDxDAT;
				lencount = (~lenfin & AUDxON) & AUDxDAT;
				lencntrld = (lenfin & AUDxON) & AUDxDAT;
				pbufld1 = 1'b0;
				penhi = 1'b1;
				volcntrld = 1'b0;
				if (perfin) begin
					audio_next = AUDIO_STATE_4;
					percount = 1'b0;
					percntrld = 1'b1;
				end
				else begin
					audio_next = AUDIO_STATE_3;
					percount = 1'b1;
					percntrld = 1'b0;
				end
			end
			AUDIO_STATE_4: begin
				dmasen = 1'b0;
				intreq2_set = (lenfin & AUDxON) & AUDxDAT;
				lencount = (~lenfin & AUDxON) & AUDxDAT;
				lencntrld = (lenfin & AUDxON) & AUDxDAT;
				penhi = 1'b0;
				volcntrld = 1'b0;
				if (perfin && (AUDxON || !AUDxIP)) begin
					audio_next = AUDIO_STATE_3;
					AUDxDR = AUDxON;
					AUDxIR = (intreq2 & AUDxON) | ~AUDxON;
					intreq2_clr = intreq2;
					pbufld1 = 1'b1;
					percount = 1'b0;
					percntrld = 1'b1;
				end
				else if ((perfin && !AUDxON) && AUDxIP) begin
					audio_next = AUDIO_STATE_0;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					intreq2_clr = 1'b0;
					pbufld1 = 1'b0;
					percount = 1'b0;
					percntrld = 1'b0;
				end
				else begin
					audio_next = AUDIO_STATE_4;
					AUDxDR = 1'b0;
					AUDxIR = 1'b0;
					intreq2_clr = 1'b0;
					pbufld1 = 1'b0;
					percount = 1'b1;
					percntrld = 1'b0;
				end
			end
			default: begin
				audio_next = AUDIO_STATE_0;
				AUDxDR = 1'b0;
				AUDxIR = 1'b0;
				dmasen = 1'b0;
				intreq2_clr = 1'b0;
				intreq2_set = 1'b0;
				lencntrld = 1'b0;
				lencount = 1'b0;
				pbufld1 = 1'b0;
				penhi = 1'b0;
				percount = 1'b0;
				percntrld = 1'b0;
				volcntrld = 1'b0;
			end
		endcase
endmodule
module paula_audio_mixer (
	clk,
	clk7_en,
	sample0,
	sample1,
	sample2,
	sample3,
	vol0,
	vol1,
	vol2,
	vol3,
	ldatasum,
	rdatasum
);
	input clk;
	input clk7_en;
	input [7:0] sample0;
	input [7:0] sample1;
	input [7:0] sample2;
	input [7:0] sample3;
	input [6:0] vol0;
	input [6:0] vol1;
	input [6:0] vol2;
	input [6:0] vol3;
	output reg [14:0] ldatasum;
	output reg [14:0] rdatasum;
	wire [13:0] msample0;
	wire [13:0] msample1;
	wire [13:0] msample2;
	wire [13:0] msample3;
	paula_audio_volume sv0(
		.sample(sample0),
		.volume({vol0[6] | vol0[5], vol0[6] | vol0[4], vol0[6] | vol0[3], vol0[6] | vol0[2], vol0[6] | vol0[1], vol0[6] | vol0[0]}),
		.out(msample0)
	);
	paula_audio_volume sv1(
		.sample(sample1),
		.volume({vol1[6] | vol1[5], vol1[6] | vol1[4], vol1[6] | vol1[3], vol1[6] | vol1[2], vol1[6] | vol1[1], vol1[6] | vol1[0]}),
		.out(msample1)
	);
	paula_audio_volume sv2(
		.sample(sample2),
		.volume({vol2[6] | vol2[5], vol2[6] | vol2[4], vol2[6] | vol2[3], vol2[6] | vol2[2], vol2[6] | vol2[1], vol2[6] | vol2[0]}),
		.out(msample2)
	);
	paula_audio_volume sv3(
		.sample(sample3),
		.volume({vol3[6] | vol3[5], vol3[6] | vol3[4], vol3[6] | vol3[3], vol3[6] | vol3[2], vol3[6] | vol3[1], vol3[6] | vol3[0]}),
		.out(msample3)
	);
	always @(posedge clk) begin
		ldatasum <= #(1) {msample0[13], msample0} + {msample3[13], msample3};
		rdatasum <= #(1) {msample1[13], msample1} + {msample2[13], msample2};
	end
endmodule
module paula_audio_volume (
	sample,
	volume,
	out
);
	input [7:0] sample;
	input [5:0] volume;
	output wire [13:0] out;
	wire [13:0] sesample;
	wire [13:0] sevolume;
	assign sesample[13:0] = {{6 {sample[7]}}, sample[7:0]};
	assign sevolume[13:0] = {8'b00000000, volume[5:0]};
	assign out[13:0] = {sesample[13:0] * sevolume[13:0]};
endmodule
module paula_audio (
	clk,
	clk7_en,
	cck,
	rst,
	strhor,
	reg_address_in,
	data_in,
	dmaena,
	audint,
	audpen,
	dmal,
	dmas,
	ldata,
	rdata,
	ldata_okk,
	rdata_okk
);
	input wire clk;
	input wire clk7_en;
	input wire cck;
	input wire rst;
	input wire strhor;
	input wire [8:1] reg_address_in;
	input wire [15:0] data_in;
	input wire [3:0] dmaena;
	output wire [3:0] audint;
	input wire [3:0] audpen;
	output reg [3:0] dmal;
	output reg [3:0] dmas;
	output wire [14:0] ldata;
	output wire [14:0] rdata;
	output wire [8:0] ldata_okk;
	output wire [8:0] rdata_okk;
	parameter AUD0BASE = 9'h0a0;
	parameter AUD1BASE = 9'h0b0;
	parameter AUD2BASE = 9'h0c0;
	parameter AUD3BASE = 9'h0d0;
	wire [3:0] aen;
	wire [3:0] dmareq;
	wire [3:0] dmaspc;
	wire [7:0] sample0;
	wire [7:0] sample1;
	wire [7:0] sample2;
	wire [7:0] sample3;
	wire [7:0] sample0_okk;
	wire [7:0] sample1_okk;
	wire [7:0] sample2_okk;
	wire [7:0] sample3_okk;
	wire [6:0] vol0;
	wire [6:0] vol1;
	wire [6:0] vol2;
	wire [6:0] vol3;
	wire [15:0] ldatasum;
	wire [15:0] rdatasum;
	assign aen[0] = (reg_address_in[8:4] == AUD0BASE[8:4] ? 1'b1 : 1'b0);
	assign aen[1] = (reg_address_in[8:4] == AUD1BASE[8:4] ? 1'b1 : 1'b0);
	assign aen[2] = (reg_address_in[8:4] == AUD2BASE[8:4] ? 1'b1 : 1'b0);
	assign aen[3] = (reg_address_in[8:4] == AUD3BASE[8:4] ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (strhor) begin
				dmal <= dmareq;
				dmas <= dmaspc;
			end
		end
	paula_audio_channel ach0(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(rst),
		.cck(cck),
		.aen(aen[0]),
		.dmaena(dmaena[0]),
		.reg_address_in(reg_address_in[3:1]),
		.data(data_in),
		.volume(vol0),
		.sample(sample0),
		.sample_okk(sample0_okk),
		.intreq(audint[0]),
		.intpen(audpen[0]),
		.dmareq(dmareq[0]),
		.dmas(dmaspc[0]),
		.strhor(strhor)
	);
	paula_audio_channel ach1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(rst),
		.cck(cck),
		.aen(aen[1]),
		.dmaena(dmaena[1]),
		.reg_address_in(reg_address_in[3:1]),
		.data(data_in),
		.volume(vol1),
		.sample(sample1),
		.sample_okk(sample1_okk),
		.intreq(audint[1]),
		.intpen(audpen[1]),
		.dmareq(dmareq[1]),
		.dmas(dmaspc[1]),
		.strhor(strhor)
	);
	paula_audio_channel ach2(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(rst),
		.cck(cck),
		.aen(aen[2]),
		.dmaena(dmaena[2]),
		.reg_address_in(reg_address_in[3:1]),
		.data(data_in),
		.volume(vol2),
		.sample(sample2),
		.sample_okk(sample2_okk),
		.intreq(audint[2]),
		.intpen(audpen[2]),
		.dmareq(dmareq[2]),
		.dmas(dmaspc[2]),
		.strhor(strhor)
	);
	paula_audio_channel ach3(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(rst),
		.cck(cck),
		.aen(aen[3]),
		.dmaena(dmaena[3]),
		.reg_address_in(reg_address_in[3:1]),
		.data(data_in),
		.volume(vol3),
		.sample(sample3),
		.sample_okk(sample3_okk),
		.intreq(audint[3]),
		.intpen(audpen[3]),
		.dmareq(dmareq[3]),
		.dmas(dmaspc[3]),
		.strhor(strhor)
	);
	assign ldata_okk = {sample0_okk[7], sample0_okk[7:0]} + {sample3_okk[7], sample3_okk[7:0]};
	assign rdata_okk = {sample1_okk[7], sample1_okk[7:0]} + {sample2_okk[7], sample2_okk[7:0]};
	paula_audio_mixer mix(
		.clk(clk),
		.clk7_en(clk7_en),
		.sample0(sample0),
		.sample1(sample1),
		.sample2(sample2),
		.sample3(sample3),
		.vol0(vol0),
		.vol1(vol1),
		.vol2(vol2),
		.vol3(vol3),
		.ldatasum(ldata),
		.rdatasum(rdata)
	);
endmodule
module paula_intcontroller (
	clk,
	clk7_en,
	reset,
	reg_address_in,
	data_in,
	data_out,
	rxint,
	txint,
	vblint,
	int2,
	int3,
	int6,
	blckint,
	syncint,
	audint,
	audpen,
	rbfmirror,
	_ipl
);
	input clk;
	input clk7_en;
	input reset;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input rxint;
	input txint;
	input vblint;
	input int2;
	input int3;
	input int6;
	input blckint;
	input syncint;
	input [3:0] audint;
	output wire [3:0] audpen;
	output wire rbfmirror;
	output reg [2:0] _ipl;
	parameter INTENAR = 9'h01c;
	parameter INTREQR = 9'h01e;
	parameter INTENA = 9'h09a;
	parameter INTREQ = 9'h09c;
	reg [14:0] intena;
	reg [15:0] intenar;
	reg [14:0] intreq;
	reg [15:0] intreqr;
	assign rbfmirror = intreq[11];
	assign audpen[3:0] = intreq[10:7];
	assign data_out = intenar | intreqr;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				intena <= 0;
			else if (reg_address_in[8:1] == INTENA[8:1]) begin
				if (data_in[15])
					intena[14:0] <= intena[14:0] | data_in[14:0];
				else
					intena[14:0] <= intena[14:0] & ~data_in[14:0];
			end
		end
	always @(*)
		if (reg_address_in[8:1] == INTENAR[8:1])
			intenar[15:0] = {1'b0, intena[14:0]};
		else if (reg_address_in[8:1] == INTENA[8:1])
			intenar = 16'hffff;
		else
			intenar = 16'd0;
	always @(*)
		if (reg_address_in[8:1] == INTREQR[8:1])
			intreqr[15:0] = {1'b0, intreq[14:0]};
		else if (reg_address_in[8:1] == INTREQ[8:1])
			intreqr = 16'hffff;
		else
			intreqr = 16'd0;
	reg [14:0] tmp;
	always @(*)
		if (reg_address_in[8:1] == INTREQ[8:1]) begin
			if (data_in[15])
				tmp[14:0] = intreq[14:0] | data_in[14:0];
			else
				tmp[14:0] = intreq[14:0] & ~data_in[14:0];
		end
		else
			tmp[14:0] = intreq[14:0];
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				intreq <= 0;
			else begin
				intreq[0] <= tmp[0] | txint;
				intreq[1] <= tmp[1] | blckint;
				intreq[2] <= tmp[2];
				intreq[3] <= tmp[3] | int2;
				intreq[4] <= tmp[4];
				intreq[5] <= tmp[5] | vblint;
				intreq[6] <= tmp[6] | int3;
				intreq[7] <= tmp[7] | audint[0];
				intreq[8] <= tmp[8] | audint[1];
				intreq[9] <= tmp[9] | audint[2];
				intreq[10] <= tmp[10] | audint[3];
				intreq[11] <= tmp[11] | rxint;
				intreq[12] <= tmp[12] | syncint;
				intreq[13] <= tmp[13] | int6;
				intreq[14] <= tmp[14];
			end
		end
	reg [14:0] intreqena;
	always @(*)
		if (intena[14])
			intreqena[14:0] = intreq[14:0] & intena[14:0];
		else
			intreqena[14:0] = 15'b000000000000000;
	always @(posedge clk)
		if (clk7_en)
			casez (intreqena[14:0])
				15'b1zzzzzzzzzzzzzz: _ipl <= 1;
				15'b01zzzzzzzzzzzzz: _ipl <= 1;
				15'b001zzzzzzzzzzzz: _ipl <= 2;
				15'b0001zzzzzzzzzzz: _ipl <= 2;
				15'b00001zzzzzzzzzz: _ipl <= 3;
				15'b000001zzzzzzzzz: _ipl <= 3;
				15'b0000001zzzzzzzz: _ipl <= 3;
				15'b00000001zzzzzzz: _ipl <= 3;
				15'b000000001zzzzzz: _ipl <= 4;
				15'b0000000001zzzzz: _ipl <= 4;
				15'b00000000001zzzz: _ipl <= 4;
				15'b000000000001zzz: _ipl <= 5;
				15'b0000000000001zz: _ipl <= 6;
				15'b00000000000001z: _ipl <= 6;
				15'b000000000000001: _ipl <= 6;
				15'b000000000000000: _ipl <= 7;
				default: _ipl <= 7;
			endcase
endmodule
module userio (
	clk,
	reset,
	clk7_en,
	reg_address_in,
	data_in,
	data_out,
	pot_cnt_en,
	_fire0,
	_fire1,
	_fire0_dat,
	_fire1_dat,
	_joy1,
	_joy2,
	joy_ana1,
	joy_ana2,
	mouse_btn,
	kms_level,
	kbd_mouse_type,
	kbd_mouse_data
);
	input clk;
	input reset;
	input clk7_en;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	output reg [15:0] data_out;
	input pot_cnt_en;
	output wire _fire0;
	output wire _fire1;
	input _fire0_dat;
	input _fire1_dat;
	input [15:0] _joy1;
	input [15:0] _joy2;
	input [15:0] joy_ana1;
	input [15:0] joy_ana2;
	input [2:0] mouse_btn;
	input kms_level;
	input [1:0] kbd_mouse_type;
	input [7:0] kbd_mouse_data;
	parameter JOY0DAT = 9'h00a;
	parameter JOY1DAT = 9'h00c;
	parameter SCRDAT = 9'h1f0;
	parameter POT0DAT = 9'h012;
	parameter POT1DAT = 9'h014;
	parameter POTINP = 9'h016;
	parameter POTGO = 9'h034;
	parameter JOYTEST = 9'h036;
	parameter KEY_MENU = 8'h69;
	parameter KEY_ESC = 8'h45;
	parameter KEY_ENTER = 8'h44;
	parameter KEY_UP = 8'h4c;
	parameter KEY_DOWN = 8'h4d;
	parameter KEY_LEFT = 8'h4f;
	parameter KEY_RIGHT = 8'h4e;
	parameter KEY_PGUP = 8'h6c;
	parameter KEY_PGDOWN = 8'h6d;
	reg [15:0] _sjoy1;
	reg [15:0] _djoy1;
	reg [15:0] _sjoy2;
	reg [15:0] _djoy2;
	reg [15:0] potreg;
	wire [15:0] mouse0dat;
	reg [15:0] dmouse0dat;
	reg [15:0] dmouse1dat;
	wire _mleft;
	wire _mthird;
	wire _mright;
	reg joy1enable;
	wire test_load;
	wire [15:0] test_data;
	wire cd32pad = 1'b0;
	wire joy_swap = 1'b0;
	wire joy_ana_en = 1'b0;
	always @(posedge clk)
		if (reset)
			potreg <= 0;
		else if (clk7_en) begin
			if (reg_address_in[8:1] == POTGO[8:1])
				potreg[15:0] <= data_in[15:0];
			else
				potreg[0] <= 0;
		end
	reg [15:0] ajoy1;
	reg [15:0] ajoy2;
	always @(posedge clk) begin
		ajoy1 <= (joy_swap ? joy_ana1 : joy_ana2);
		ajoy2 <= (joy_swap ? joy_ana2 : joy_ana1);
	end
	reg [7:0] pot0x;
	reg [7:0] pot0y;
	reg [7:0] pot1x;
	reg [7:0] pot1y;
	reg [3:0] potcnt;
	reg [3:0] potcap;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				{pot0x, pot0y, pot1x, pot1y} <= 0;
				potcnt <= 4'b0000;
			end
			else if (potreg[0]) begin
				{pot0x, pot0y, pot1x, pot1y} <= 0;
				potcnt <= 4'b1111;
			end
			else if (pot_cnt_en) begin
				if (joy_ana_en & joy1enable) begin
					if (potcnt[0] && ({~ajoy1[7], ajoy1[6:0]} != pot1y))
						pot0y <= pot0y + 1'd1;
					else
						potcnt[0] <= 0;
					if (potcnt[1] && ({~ajoy1[15], ajoy1[14:8]} != pot1x))
						pot0x <= pot0x + 1'd1;
					else
						potcnt[1] <= 0;
				end
				else begin
					if (!potcap[0])
						pot0x <= pot0x + 1'd1;
					if (!potcap[1])
						pot0y <= pot0y + 1'd1;
				end
				if (joy_ana_en) begin
					if (potcnt[2] && ({~ajoy2[7], ajoy2[6:0]} != pot1y))
						pot1y <= pot1y + 1'd1;
					else
						potcnt[2] <= 0;
					if (potcnt[3] && ({~ajoy2[15], ajoy2[14:8]} != pot1x))
						pot1x <= pot1x + 1'd1;
					else
						potcnt[3] <= 0;
				end
				else begin
					if (!potcap[2])
						pot1x <= pot1x + 1'd1;
					if (!potcap[3])
						pot1y <= pot1y + 1'd1;
				end
			end
		end
	wire joy2_pin5 = ~(potreg[13] & ~potreg[12]);
	wire joy1_pin5 = ~(potreg[9] & ~potreg[8]);
	reg [7:0] cd32pad1_reg;
	reg [7:0] cd32pad2_reg;
	always @(posedge clk)
		if (reset)
			potcap <= 0;
		else if (clk7_en) begin
			if (cd32pad & ~joy2_pin5)
				potcap[3] <= cd32pad2_reg[7];
			else
				potcap[3] <= _djoy2[5] & ~(potreg[15] & ~potreg[14]);
			potcap[2] <= joy2_pin5;
			if ((joy1enable & cd32pad) & ~joy1_pin5)
				potcap[1] <= cd32pad1_reg[7];
			else
				potcap[1] <= (_mright & _djoy1[5]) & ~(potreg[11] & ~potreg[10]);
			potcap[0] <= _mthird & joy1_pin5;
		end
	reg fire1_d;
	always @(posedge clk)
		if (reset)
			fire1_d <= 1;
		else
			fire1_d <= _fire0_dat;
	wire cd32pad1_reg_load = joy1_pin5;
	wire cd32pad1_reg_shift = _fire0_dat && !fire1_d;
	always @(posedge clk)
		if (reset)
			cd32pad1_reg <= 8'hff;
		else if (cd32pad1_reg_load)
			cd32pad1_reg <= {_djoy1[5], _djoy1[4], _djoy1[6], _djoy1[7], _djoy1[8], _djoy1[9], _djoy1[10], 1'b1};
		else if (cd32pad1_reg_shift)
			cd32pad1_reg <= {cd32pad1_reg[6:0], 1'b0};
	reg fire2_d;
	always @(posedge clk)
		if (reset)
			fire2_d <= 1;
		else
			fire2_d <= _fire1_dat;
	wire cd32pad2_reg_load = joy2_pin5;
	wire cd32pad2_reg_shift = _fire1_dat && !fire2_d;
	always @(posedge clk)
		if (reset)
			cd32pad2_reg <= 8'hff;
		else if (cd32pad2_reg_load)
			cd32pad2_reg <= {_djoy2[5], _djoy2[4], _djoy2[6], _djoy2[7], _djoy2[8], _djoy2[9], _djoy2[10], 1'b1};
		else if (cd32pad2_reg_shift)
			cd32pad2_reg <= {cd32pad2_reg[6:0], 1'b0};
	wire [15:0] joy1 = (joy_ana_en ? {{12 {1'b1}}, _joy1[6], 1'b1, _joy1[4], _joy1[5]} : _joy1);
	wire [15:0] joy2 = (joy_ana_en ? {{12 {1'b1}}, _joy2[6], 1'b1, _joy2[4], _joy2[5]} : _joy2);
	always @(posedge clk) begin
		_sjoy1 <= (joy_swap ? joy1 : joy2);
		_djoy1 <= _sjoy1;
		_sjoy2 <= (joy_swap ? joy2 : joy1);
		_djoy2 <= _sjoy2;
	end
	always @(posedge clk)
		if (!_mleft || reset)
			joy1enable = 0;
		else if (!_sjoy1[4])
			joy1enable = 1;
	always @(posedge clk)
		if (clk7_en) begin
			if (test_load)
				dmouse0dat[7:0] <= 8'h00;
			else if (((((!_djoy1[0] && _sjoy1[0]) && _sjoy1[2]) || ((_djoy1[0] && !_sjoy1[0]) && !_sjoy1[2])) || ((!_djoy1[2] && _sjoy1[2]) && !_sjoy1[0])) || ((_djoy1[2] && !_sjoy1[2]) && _sjoy1[0]))
				dmouse0dat[7:0] <= dmouse0dat[7:0] + 1'd1;
			else if (((((!_djoy1[0] && _sjoy1[0]) && !_sjoy1[2]) || ((_djoy1[0] && !_sjoy1[0]) && _sjoy1[2])) || ((!_djoy1[2] && _sjoy1[2]) && _sjoy1[0])) || ((_djoy1[2] && !_sjoy1[2]) && !_sjoy1[0]))
				dmouse0dat[7:0] <= dmouse0dat[7:0] - 1'd1;
			else
				dmouse0dat[1:0] <= {!_djoy1[0], _djoy1[0] ^ _djoy1[2]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (test_load)
				dmouse0dat[15:8] <= 8'h00;
			else if (((((!_djoy1[1] && _sjoy1[1]) && _sjoy1[3]) || ((_djoy1[1] && !_sjoy1[1]) && !_sjoy1[3])) || ((!_djoy1[3] && _sjoy1[3]) && !_sjoy1[1])) || ((_djoy1[3] && !_sjoy1[3]) && _sjoy1[1]))
				dmouse0dat[15:8] <= dmouse0dat[15:8] + 1'd1;
			else if (((((!_djoy1[1] && _sjoy1[1]) && !_sjoy1[3]) || ((_djoy1[1] && !_sjoy1[1]) && _sjoy1[3])) || ((!_djoy1[3] && _sjoy1[3]) && _sjoy1[1])) || ((_djoy1[3] && !_sjoy1[3]) && !_sjoy1[1]))
				dmouse0dat[15:8] <= dmouse0dat[15:8] - 1'd1;
			else
				dmouse0dat[9:8] <= {!_djoy1[1], _djoy1[1] ^ _djoy1[3]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (test_load)
				dmouse1dat[7:2] <= test_data[7:2];
			else if (((((!_djoy2[0] && _sjoy2[0]) && _sjoy2[2]) || ((_djoy2[0] && !_sjoy2[0]) && !_sjoy2[2])) || ((!_djoy2[2] && _sjoy2[2]) && !_sjoy2[0])) || ((_djoy2[2] && !_sjoy2[2]) && _sjoy2[0]))
				dmouse1dat[7:0] <= dmouse1dat[7:0] + 1'd1;
			else if (((((!_djoy2[0] && _sjoy2[0]) && !_sjoy2[2]) || ((_djoy2[0] && !_sjoy2[0]) && _sjoy2[2])) || ((!_djoy2[2] && _sjoy2[2]) && _sjoy2[0])) || ((_djoy2[2] && !_sjoy2[2]) && !_sjoy2[0]))
				dmouse1dat[7:0] <= dmouse1dat[7:0] - 1'd1;
			else
				dmouse1dat[1:0] <= {!_djoy2[0], _djoy2[0] ^ _djoy2[2]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (test_load)
				dmouse1dat[15:10] <= test_data[15:10];
			else if (((((!_djoy2[1] && _sjoy2[1]) && _sjoy2[3]) || ((_djoy2[1] && !_sjoy2[1]) && !_sjoy2[3])) || ((!_djoy2[3] && _sjoy2[3]) && !_sjoy2[1])) || ((_djoy2[3] && !_sjoy2[3]) && _sjoy2[1]))
				dmouse1dat[15:8] <= dmouse1dat[15:8] + 1'd1;
			else if (((((!_djoy2[1] && _sjoy2[1]) && !_sjoy2[3]) || ((_djoy2[1] && !_sjoy2[1]) && _sjoy2[3])) || ((!_djoy2[3] && _sjoy2[3]) && _sjoy2[1])) || ((_djoy2[3] && !_sjoy2[3]) && !_sjoy2[1]))
				dmouse1dat[15:8] <= dmouse1dat[15:8] - 1'd1;
			else
				dmouse1dat[9:8] <= {!_djoy2[1], _djoy2[1] ^ _djoy2[3]};
		end
	reg [7:0] mouse0scr;
	always @(*)
		if ((reg_address_in[8:1] == JOY0DAT[8:1]) && joy1enable)
			data_out[15:0] = {mouse0dat[15:10] + dmouse0dat[15:10], dmouse0dat[9:8], mouse0dat[7:2] + dmouse0dat[7:2], dmouse0dat[1:0]};
		else if (reg_address_in[8:1] == JOY0DAT[8:1])
			data_out[15:0] = {mouse0dat[15:8] + dmouse0dat[15:8], mouse0dat[7:0] + dmouse0dat[7:0]};
		else if (reg_address_in[8:1] == JOY1DAT[8:1])
			data_out[15:0] = dmouse1dat;
		else if (reg_address_in[8:1] == POT0DAT[8:1])
			data_out[15:0] = {pot0y, pot0x};
		else if (reg_address_in[8:1] == POT1DAT[8:1])
			data_out[15:0] = {pot1y, pot1x};
		else if (reg_address_in[8:1] == POTINP[8:1])
			data_out[15:0] = {1'b0, potcap[3], 1'b0, potcap[2], 1'b0, potcap[1], 1'b0, potcap[0], 8'h00};
		else if (reg_address_in[8:1] == SCRDAT[8:1])
			data_out[15:0] = {8'h00, mouse0scr};
		else
			data_out[15:0] = 16'h0000;
	assign _fire0 = (cd32pad && !cd32pad1_reg_load ? fire1_d : _sjoy1[4] & _mleft);
	assign _fire1 = (cd32pad && !cd32pad2_reg_load ? fire2_d : _sjoy2[4]);
	assign test_load = (reg_address_in[8:1] == JOYTEST[8:1] ? 1'b1 : 1'b0);
	assign test_data = data_in[15:0];
	reg [7:0] xcount;
	reg [7:0] ycount;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg old_level;
		reg wheel;
		old_level <= kms_level;
		if (reset) begin
			xcount <= 0;
			ycount <= 0;
			mouse0scr <= 0;
			wheel <= 0;
		end
		else if (test_load && clk7_en) begin
			ycount[7:2] <= test_data[15:10];
			xcount[7:2] <= test_data[7:2];
		end
		else if (old_level ^ kms_level) begin
			if (kbd_mouse_type == 0) begin
				wheel <= 0;
				xcount[7:0] <= xcount[7:0] + kbd_mouse_data;
			end
			if (kbd_mouse_type == 1) begin
				wheel <= 1;
				if (wheel)
					mouse0scr <= mouse0scr + kbd_mouse_data;
				else
					ycount[7:0] <= ycount[7:0] + kbd_mouse_data;
			end
		end
	end
	assign mouse0dat = {ycount, xcount};
	assign _mleft = ~mouse_btn[0];
	assign _mright = ~mouse_btn[1];
	assign _mthird = ~mouse_btn[2];
endmodule
module paula_floppy (
	clk,
	clk7_en,
	clk7n_en,
	reset,
	ntsc,
	sof,
	enable,
	reg_address_in,
	data_in,
	data_out,
	dmal,
	dmas,
	_step,
	direc,
	_sel,
	side,
	_motor,
	_track0,
	_change,
	_ready,
	_wprot,
	index,
	blckint,
	syncint,
	wordsync,
	fdd_led,
	floppy_drives,
	floppy_wrprot,
	sdc_image_mounted,
	sdc_image_size,
	sdc_rd,
	sdc_wr,
	sdc_sector,
	sdc_busy,
	sdc_done,
	sdc_byte_in_strobe,
	sdc_byte_addr,
	sdc_byte_in_data,
	sdc_byte_out_data
);
	input clk;
	input clk7_en;
	input clk7n_en;
	input reset;
	input ntsc;
	input sof;
	input enable;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	output wire [15:0] data_out;
	output wire dmal;
	output wire dmas;
	input _step;
	input direc;
	input [3:0] _sel;
	input side;
	input _motor;
	output wire _track0;
	output wire _change;
	output wire _ready;
	output wire _wprot;
	output wire index;
	output reg blckint;
	output wire syncint;
	input wordsync;
	output wire fdd_led;
	input [1:0] floppy_drives;
	input floppy_wrprot;
	input [3:0] sdc_image_mounted;
	input [31:0] sdc_image_size;
	output reg [3:0] sdc_rd;
	output reg [3:0] sdc_wr;
	output reg [31:0] sdc_sector;
	input sdc_busy;
	input sdc_done;
	input sdc_byte_in_strobe;
	input [8:0] sdc_byte_addr;
	input [7:0] sdc_byte_in_data;
	output reg [7:0] sdc_byte_out_data;
	parameter DSKBYTR = 9'h01a;
	parameter DSKDAT = 9'h026;
	parameter DSKDATR = 9'h008;
	parameter DSKSYNC = 9'h07e;
	parameter DSKLEN = 9'h024;
	reg [15:0] dsksync;
	reg [15:0] dsklen;
	reg [6:0] dsktrack [3:0];
	wire [7:0] track;
	wire [13:0] dsklen_len = dsklen[13:0];
	wire dsklen_wr = dsklen[14];
	wire dsklen_dma = dsklen[15];
	reg dmaon;
	wire lenzero;
	reg trackwr;
	reg trackrd;
	wire _dsktrack0;
	wire dsktracklast;
	wire [15:0] fifo_in;
	wire [15:0] fifo_out;
	wire fifo_wr;
	reg fifo_wr_del;
	wire fifo_rd;
	wire fifo_empty;
	wire fifo_full;
	wire [15:0] dskbytr;
	wire [15:0] dskdatr;
	wire fifo_reset;
	reg dmaen;
	reg [15:0] wr_fifo_status;
	reg [3:0] disk_present = 4'b0000;
	wire [3:0] disk_writable;
	wire _selx;
	wire [1:0] sel;
	reg [1:0] drives;
	reg [3:0] _disk_change;
	reg _step_del;
	reg [8:0] step_ena_cnt;
	wire step_ena;
	reg [3:0] _sel_del;
	reg [3:0] motor_on;
	reg cmd_fdd;
	reg [7:0] fd_dma_csum [3:0];
	reg [8:0] fd_dma_wr_ptr;
	reg [3:0] fd_dma_buf_wr_state;
	wire [15:0] fd_dma_buf_out;
	reg [3:0] fd_dma_wr_sec;
	reg fd_dma_sector_ready;
	reg fd_dma_buffer_trigger_wr;
	reg [7:0] fd_dma_buffer_data_wr;
	reg fd_dma_is_writing;
	reg fd_dma_buffer_trigger_wrD;
	reg [15:0] fd_dma_dina;
	reg [15:0] fd_dma_dinb;
	reg [1:0] fd_dma_bsa;
	reg [1:0] fd_dma_bsb;
	reg [7:0] fd_dma_addra;
	reg [7:0] fd_dma_addrb;
	reg fd_dma_wrena;
	reg fd_dma_wrenb;
	wire [15:0] fd_dma_douta;
	wire [15:0] fd_dma_doutb;
	assign fd_dma_buf_out = fd_dma_doutb;
	wire [7:0] fd_dma_buf_even_q = fd_dma_douta[15:8];
	wire [7:0] fd_dma_buf_odd_q = fd_dma_douta[7:0];
	always @(posedge clk) sdc_byte_out_data <= (sdc_byte_addr[0] ? {fd_dma_buf_odd_q[3], fd_dma_buf_even_q[3], fd_dma_buf_odd_q[2], fd_dma_buf_even_q[2], fd_dma_buf_odd_q[1], fd_dma_buf_even_q[1], fd_dma_buf_odd_q[0], fd_dma_buf_even_q[0]} : {fd_dma_buf_odd_q[7], fd_dma_buf_even_q[7], fd_dma_buf_odd_q[6], fd_dma_buf_even_q[6], fd_dma_buf_odd_q[5], fd_dma_buf_even_q[5], fd_dma_buf_odd_q[4], fd_dma_buf_even_q[4]});
	reg [7:0] fd_dma_rd_ptr;
	reg [9:0] fifo_word_counter;
	always @(*) begin
		fd_dma_rd_ptr = fifo_word_counter - 10'd32;
		fd_dma_addra = sdc_byte_addr[8:1];
		fd_dma_dina = {sdc_byte_in_data, sdc_byte_in_data};
		fd_dma_bsa = (sdc_byte_addr[0] ? 2'b01 : 2'b10);
		fd_dma_wrena = (!fd_dma_is_writing && (fd_dma_buf_wr_state == 4'd2)) && sdc_byte_in_strobe;
		fd_dma_addrb = (fd_dma_is_writing ? fd_dma_wr_ptr[7:0] : fd_dma_rd_ptr);
		fd_dma_dinb = {fd_dma_buffer_data_wr, fd_dma_buffer_data_wr};
		fd_dma_bsb = (fd_dma_wr_ptr[8] ? 2'b10 : 2'b01);
		fd_dma_wrenb = (fd_dma_is_writing && (fd_dma_buf_wr_state == 4'd6)) && (fd_dma_buffer_trigger_wr != fd_dma_buffer_trigger_wrD);
	end
	paula_floppy_dma_buf fd_dma_buf(
		.clk(clk),
		.dina(fd_dma_dina),
		.dinb(fd_dma_dinb),
		.bsa(fd_dma_bsa),
		.bsb(fd_dma_bsb),
		.addra(fd_dma_addra),
		.addrb(fd_dma_addrb),
		.wrena(fd_dma_wrena),
		.wrenb(fd_dma_wrenb),
		.douta(fd_dma_douta),
		.doutb(fd_dma_doutb)
	);
	reg [3:0] cpu_wr_state;
	reg [8:0] cpu_wr_cnt;
	reg [31:0] wr_sector_header_word;
	wire dma_active = (dmaen && !lenzero) && enable;
	wire [3:0] fd_dma_wr_sec_next = (fd_dma_wr_sec < 4'd10 ? fd_dma_wr_sec + 4'd1 : 4'd0);
	reg fifo_is_writing;
	reg fifo_reading_sector;
	reg [15:0] wr_sector_csum;
	always @(posedge clk)
		if (reset) begin
			fd_dma_buf_wr_state <= 4'h0;
			sdc_rd <= 4'b0000;
			sdc_wr <= 4'b0000;
			fd_dma_sector_ready <= 1'b0;
			fd_dma_wr_sec <= 4'd10;
		end
		else
			case (fd_dma_buf_wr_state)
				4'd0: begin
					if (((dma_active && !sdc_busy) && !lenzero) && !dsklen[14]) begin
						$display("paula_floppy.v: DMA read, lenzero = %0d, dsklen = %0d/%0d/%0d", lenzero, dsklen[15], dsklen[14], dsklen[13:0]);
						fd_dma_is_writing <= 1'b0;
						fd_dma_csum[0] <= 8'd0;
						fd_dma_csum[1] <= 8'd0;
						fd_dma_csum[2] <= 8'd0;
						fd_dma_csum[3] <= 8'd0;
						fd_dma_sector_ready <= 1'b0;
						fd_dma_wr_sec <= fd_dma_wr_sec_next;
						sdc_sector <= (track * 11) + fd_dma_wr_sec_next;
						sdc_rd <= ~_sel;
						fd_dma_buf_wr_state <= 4'd1;
					end
					if (dma_active && dsklen[14]) begin
						$display("paula_floppy.v: DMA write, lenzero = %0d, dsklen = %0d/%0d/%0d", lenzero, dsklen[15], dsklen[14], dsklen[13:0]);
						fd_dma_is_writing <= 1'b1;
						fd_dma_buf_wr_state <= 4'd5;
						fd_dma_wr_ptr <= 9'd0;
						fd_dma_buffer_trigger_wrD <= fd_dma_buffer_trigger_wr;
					end
				end
				4'd1:
					if (sdc_busy) begin
						sdc_rd <= 4'b0000;
						sdc_wr <= 4'b0000;
						fd_dma_buf_wr_state <= 4'd2;
					end
				4'd2:
					if (sdc_byte_in_strobe) begin
						fd_dma_csum[sdc_byte_addr[1:0]] <= 8'haa | ((fd_dma_csum[sdc_byte_addr[1:0]] ^ sdc_byte_in_data) ^ {1'b0, sdc_byte_in_data[7:1]});
						if (sdc_byte_addr == 9'd511) begin
							if (!lenzero) begin
								fd_dma_buf_wr_state <= 4'd3;
								fd_dma_sector_ready <= 1'b1;
							end
							else
								fd_dma_buf_wr_state <= 4'd0;
						end
					end
				4'd3:
					if (fifo_is_writing)
						fd_dma_buf_wr_state <= 4'd4;
				4'd4:
					if (lenzero)
						fd_dma_buf_wr_state <= 4'd0;
					else if (!fifo_reading_sector && !sdc_busy) begin
						fd_dma_csum[0] <= 8'd0;
						fd_dma_csum[1] <= 8'd0;
						fd_dma_csum[2] <= 8'd0;
						fd_dma_csum[3] <= 8'd0;
						fd_dma_sector_ready <= 1'b0;
						fd_dma_wr_sec <= fd_dma_wr_sec_next;
						sdc_sector <= (track * 11) + fd_dma_wr_sec_next;
						sdc_rd <= ~_sel;
						fd_dma_buf_wr_state <= 4'd1;
					end
				4'd5:
					if (cpu_wr_state != 4'd0)
						fd_dma_buf_wr_state <= 4'd6;
				4'd6: begin
					if (fd_dma_buffer_trigger_wr != fd_dma_buffer_trigger_wrD) begin
						fd_dma_wr_ptr <= fd_dma_wr_ptr + 9'd1;
						fd_dma_buffer_trigger_wrD <= fd_dma_buffer_trigger_wr;
					end
					if (cpu_wr_state == 4'd0) begin
						$display("paula_floppy.v: tr %0d, sd %0d, sec %0d write data complete, CHK %x", wr_sector_header_word[23:17], wr_sector_header_word[16], wr_sector_header_word[15:8], wr_sector_csum);
						if (wr_sector_csum) begin
							$display("paula_floppy.v: data checkum failed, not writing to sd card");
							fd_dma_buf_wr_state <= 4'd0;
						end
						else begin
							sdc_sector <= (wr_sector_header_word[23:16] * 11) + wr_sector_header_word[15:8];
							fd_dma_buf_wr_state <= 4'd7;
						end
					end
				end
				4'd7:
					if (!sdc_busy) begin
						sdc_wr <= ~_sel;
						fd_dma_buf_wr_state <= 4'd8;
					end
				4'd8:
					if (sdc_busy) begin
						$display("paula_floppy.v: sd write request acknowledged");
						sdc_wr <= 4'b0000;
						fd_dma_buf_wr_state <= 4'd0;
					end
				4'd9:
					if (!sdc_busy)
						fd_dma_buf_wr_state <= 4'd0;
			endcase
	reg [3:0] fifo_sector_counter;
	wire [7:0] sector = {4'd0, fifo_sector_counter};
	wire [31:0] sector_header_word = {8'hff, track, sector, 8'd11 - sector};
	wire [15:0] mfm_encoder_in = (fifo_word_counter == 10'd4 ? sector_header_word[31:16] : (fifo_word_counter == 10'd5 ? sector_header_word[15:0] : (fifo_word_counter == 10'd6 ? sector_header_word[31:16] : (fifo_word_counter == 10'd7 ? sector_header_word[15:0] : (fifo_word_counter == 10'd26 ? sector_header_word[31:16] : (fifo_word_counter == 10'd27 ? sector_header_word[15:0] : 16'h0000))))));
	wire [15:0] mfm_encoder_odd = {1'b0, mfm_encoder_in[15], 1'b0, mfm_encoder_in[13], 1'b0, mfm_encoder_in[11], 1'b0, mfm_encoder_in[9], 1'b0, mfm_encoder_in[7], 1'b0, mfm_encoder_in[5], 1'b0, mfm_encoder_in[3], 1'b0, mfm_encoder_in[1]};
	wire [15:0] mfm_encoder_even = {1'b0, mfm_encoder_in[14], 1'b0, mfm_encoder_in[12], 1'b0, mfm_encoder_in[10], 1'b0, mfm_encoder_in[8], 1'b0, mfm_encoder_in[6], 1'b0, mfm_encoder_in[4], 1'b0, mfm_encoder_in[2], 1'b0, mfm_encoder_in[0]};
	wire [15:0] mfm_encoder_checksum = (mfm_encoder_odd ^ mfm_encoder_even) | 16'haaaa;
	wire [15:0] data_odd = {1'b1, fd_dma_buf_out[15], 1'b1, fd_dma_buf_out[13], 1'b1, fd_dma_buf_out[11], 1'b1, fd_dma_buf_out[9], 1'b1, fd_dma_buf_out[7], 1'b1, fd_dma_buf_out[5], 1'b1, fd_dma_buf_out[3], 1'b1, fd_dma_buf_out[1]};
	wire [15:0] data_even = {1'b1, fd_dma_buf_out[14], 1'b1, fd_dma_buf_out[12], 1'b1, fd_dma_buf_out[10], 1'b1, fd_dma_buf_out[8], 1'b1, fd_dma_buf_out[6], 1'b1, fd_dma_buf_out[4], 1'b1, fd_dma_buf_out[2], 1'b1, fd_dma_buf_out[0]};
	wire [15:0] floppy_sector_data = (fifo_word_counter[9:1] == 9'd1 ? 16'h4489 : (fifo_word_counter[9:1] == 9'd2 ? mfm_encoder_odd : (fifo_word_counter[9:1] == 9'd3 ? mfm_encoder_even : (fifo_word_counter[9:1] == 9'd13 ? mfm_encoder_checksum : (fifo_word_counter == 10'd30 ? {fd_dma_csum[0], fd_dma_csum[1]} : (fifo_word_counter == 10'd31 ? {fd_dma_csum[2], fd_dma_csum[3]} : ((fifo_word_counter >= 10'd32) && (fifo_word_counter < 10'd288) ? data_odd : ((fifo_word_counter >= 10'd288) && (fifo_word_counter < 10'd544) ? data_even : 16'haaaa))))))));
	wire [15:0] floppy_data = (fifo_sector_counter <= 10 ? floppy_sector_data : 16'haaaa);
	parameter DISKDMA_IDLE = 2'b00;
	reg [1:0] dskstate;
	reg trackrdok;
	always @(posedge clk)
		if (clk7_en) begin
			if (dskstate == DISKDMA_IDLE) begin
				fifo_word_counter <= 10'd0;
				fifo_reading_sector <= 1'b0;
				fifo_is_writing <= 1'b0;
			end
			else if (!fifo_reading_sector) begin
				fifo_is_writing <= 1'b0;
				if (fd_dma_sector_ready)
					fifo_reading_sector <= 1'b1;
			end
			else if ((!trackrdok || fifo_wr) && !lenzero) begin
				if (!fifo_is_writing)
					fifo_sector_counter <= fd_dma_wr_sec;
				fifo_is_writing <= 1'b1;
				if ((fifo_sector_counter != 4'd11) && (fifo_word_counter == 10'd543)) begin
					fifo_word_counter <= 10'd0;
					fifo_sector_counter <= fifo_sector_counter + 4'd1;
					if (fifo_sector_counter != 4'd10)
						fifo_reading_sector <= 1'b0;
				end
				else if ((fifo_sector_counter == 4'd11) && (fifo_word_counter == 10'd349)) begin
					fifo_word_counter <= 10'd0;
					fifo_sector_counter <= 4'd0;
					fifo_reading_sector <= 1'b0;
				end
				else
					fifo_word_counter <= fifo_word_counter + 10'd1;
			end
			else if (lenzero)
				fifo_is_writing <= 1'b0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				drives <= floppy_drives;
		end
	reg [3:0] rpm_pulse_cnt;
	always @(posedge clk)
		if (clk7_en) begin
			if (sof) begin
				if ((rpm_pulse_cnt == 11) || (!ntsc && (rpm_pulse_cnt == 9)))
					rpm_pulse_cnt <= 0;
				else
					rpm_pulse_cnt <= rpm_pulse_cnt + 4'd1;
			end
		end
	assign index = (|(~_sel & motor_on) & ~|rpm_pulse_cnt) & sof;
	assign data_out = dskbytr | dskdatr;
	assign _selx = &_sel[3:0];
	always @(posedge clk)
		if (clk7_en)
			_step_del <= _step;
	always @(posedge clk)
		if (clk7_en) begin
			if (!step_ena)
				step_ena_cnt <= step_ena_cnt + 9'd1;
			else if (_step && !_step_del)
				step_ena_cnt <= 0;
		end
	assign step_ena = step_ena_cnt[8];
	always @(posedge clk)
		if (clk7_en)
			_disk_change <= (_disk_change | (((~_sel & {4 {_step}}) & ~{4 {_step_del}}) & disk_present)) & ~({4 {reset}} | ~disk_present);
	assign sel = (!_sel[0] ? 2'd0 : (!_sel[1] ? 2'd1 : (!_sel[2] ? 2'd2 : (!_sel[3] ? 2'd3 : 2'd0))));
	always @(posedge clk)
		if (clk7_en)
			_sel_del <= _sel;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				motor_on[0] <= 0;
			else if (!_sel[0] && _sel_del[0])
				motor_on[0] <= ~_motor;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				motor_on[1] <= 0;
			else if (!_sel[1] && _sel_del[1])
				motor_on[1] <= ~_motor;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				motor_on[2] <= 0;
			else if (!_sel[2] && _sel_del[2])
				motor_on[2] <= ~_motor;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				motor_on[3] <= 0;
			else if (!_sel[3] && _sel_del[3])
				motor_on[3] <= ~_motor;
		end
	assign _change = &(_sel | _disk_change);
	assign _wprot = &(_sel | disk_writable);
	assign _track0 = &(_selx | _dsktrack0);
	assign track = {dsktrack[sel], ~side};
	always @(posedge clk)
		if (clk7_en) begin
			if (((!_selx && _step) && !_step_del) && step_ena) begin
				if (!dsktracklast && !direc)
					dsktrack[sel] <= dsktrack[sel] + 7'd1;
				else if (_dsktrack0 && direc)
					dsktrack[sel] <= dsktrack[sel] - 7'd1;
			end
		end
	assign _dsktrack0 = ~(dsktrack[sel] == 0);
	assign dsktracklast = dsktrack[sel] == 82;
	assign _ready = (((_sel[3] | ~(drives[1] & drives[0])) & (_sel[2] | ~drives[1])) & (_sel[1] | ~(drives[1] | drives[0]))) & _sel[0];
	assign dskbytr = (reg_address_in[8:1] == DSKBYTR[8:1] ? {1'b1, trackrd | trackwr, dsklen[14], 13'h1000} : 16'h0000);
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dsksync[15:0] <= 16'h4489;
			else if (reg_address_in[8:1] == DSKSYNC[8:1])
				dsksync[15:0] <= data_in;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dsklen[14:0] <= 0;
			else if (reg_address_in[8:1] == DSKLEN[8:1])
				dsklen[14:0] <= data_in[14:0];
			else if (fifo_wr)
				dsklen[13:0] <= dsklen[13:0] - 14'd1;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dsklen[15] <= 0;
			else if (blckint)
				dsklen[15] <= 0;
			else if (reg_address_in[8:1] == DSKLEN[8:1])
				dsklen[15] <= data_in[15];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dmaen <= 0;
			else if (blckint)
				dmaen <= 0;
			else if (reg_address_in[8:1] == DSKLEN[8:1])
				dmaen <= data_in[15] & dsklen[15];
		end
	assign lenzero = dsklen[13:0] == 0;
	wire busrd;
	wire buswr;
	assign busrd = reg_address_in[8:1] == DSKDATR[8:1];
	assign buswr = reg_address_in[8:1] == DSKDAT[8:1];
	assign fifo_in[15:0] = (trackrd ? floppy_data : data_in);
	wire buffer_write_allowed = (cpu_wr_state != 3) || ((fd_dma_buf_wr_state == 4'd6) && !sdc_busy);
	wire stbdat = (trackwr & !fifo_empty) & buffer_write_allowed;
	assign fifo_wr = (trackrdok && ((fifo_reading_sector & !fifo_full) & ~lenzero)) | (buswr & dmaon);
	wire fifo_out_sync = fifo_out == dsksync[15:0];
	reg [7:0] dbyte;
	always @(posedge clk)
		if (clk7_en) begin
			if (!trackwr) begin
				if (cpu_wr_state) begin
					$display("paula_floppy.v: resetting write state");
					cpu_wr_state <= 4'd0;
				end
			end
			else if (stbdat) begin
				dbyte = {fifo_out[14], fifo_out[12], fifo_out[10], fifo_out[8], fifo_out[6], fifo_out[4], fifo_out[2], fifo_out[0]};
				case (cpu_wr_state)
					4'd0:
						if (fifo_out == dsksync[15:0]) begin
							$display("paula_floppy.v: initial sync marker found");
							cpu_wr_state <= 4'd1;
							cpu_wr_cnt <= 9'd0;
							wr_sector_csum <= 16'h0000;
						end
					4'd1:
						if (((cpu_wr_cnt == 0) && (fifo_out != dsksync[15:0])) || (cpu_wr_cnt != 0)) begin
							cpu_wr_cnt <= cpu_wr_cnt + 9'd1;
							if (cpu_wr_cnt < 4) begin : sv2v_autoblock_1
								integer i;
								for (i = 0; i < 8; i = i + 1)
									case (cpu_wr_cnt[1:0])
										2'd0: wr_sector_header_word[17 + (2 * i)] <= fifo_out[2 * i];
										2'd1: wr_sector_header_word[1 + (2 * i)] <= fifo_out[2 * i];
										2'd2: wr_sector_header_word[16 + (2 * i)] <= fifo_out[2 * i];
										2'd3: wr_sector_header_word[0 + (2 * i)] <= fifo_out[2 * i];
									endcase
							end
							if (cpu_wr_cnt <= 21) begin
								if (cpu_wr_cnt[0])
									wr_sector_csum[15:8] <= wr_sector_csum[15:8] ^ dbyte;
								else
									wr_sector_csum[7:0] <= wr_sector_csum[7:0] ^ dbyte;
							end
							if ((cpu_wr_cnt == 22) && (dbyte != wr_sector_csum[7:0])) begin
								$display("paula_floppy.v: header checksum failure %x != %x", dbyte, wr_sector_csum[7:0]);
								cpu_wr_state <= 4'd0;
							end
							if (cpu_wr_cnt == 23) begin
								if (dbyte != wr_sector_csum[15:8]) begin
									$display("paula_floppy.v: header checksum failure %x != %x", dbyte, wr_sector_csum[15:8]);
									cpu_wr_state <= 4'd0;
								end
								else if (track != wr_sector_header_word[23:16]) begin
									$display("paula_floppy.v: track mismatch %0d != %0d", track, wr_sector_header_word[23:16]);
									cpu_wr_state <= 4'd0;
								end
								else begin
									cpu_wr_state <= 4'd2;
									cpu_wr_cnt <= 9'd0;
								end
							end
						end
						else
							$display("paula_floppy.v: skipping additional sync marker");
					4'd2: begin
						cpu_wr_cnt <= cpu_wr_cnt + 9'd1;
						if (cpu_wr_cnt == 2)
							wr_sector_csum[7:0] <= dbyte;
						if (cpu_wr_cnt == 3) begin
							wr_sector_csum[15:8] <= dbyte;
							cpu_wr_state <= 4'd3;
							cpu_wr_cnt <= 9'd0;
						end
					end
					4'd3: begin
						cpu_wr_cnt <= cpu_wr_cnt + 9'd1;
						if (cpu_wr_cnt[0])
							wr_sector_csum[15:8] <= wr_sector_csum[15:8] ^ dbyte;
						else
							wr_sector_csum[7:0] <= wr_sector_csum[7:0] ^ dbyte;
						fd_dma_buffer_trigger_wr <= !fd_dma_buffer_trigger_wr;
						fd_dma_buffer_data_wr <= dbyte;
						if (cpu_wr_cnt == 9'd511) begin
							cpu_wr_state <= 4'd4;
							cpu_wr_cnt <= 9'd0;
						end
					end
					4'd4: begin
						cpu_wr_cnt <= cpu_wr_cnt + 9'd1;
						if (cpu_wr_cnt == 9'd1) begin
							$display("paula_floppy.v: received complete sector tr %0d, sd %0d, sec %0d", wr_sector_header_word[23:17], wr_sector_header_word[16], wr_sector_header_word[15:8]);
							cpu_wr_state <= 4'd0;
						end
					end
					default:
						;
				endcase
			end
		end
	always @(posedge clk)
		if (clk7_en)
			fifo_wr_del <= fifo_wr;
	assign fifo_rd = (busrd & dmaon) | stbdat;
	wire sync_match;
	assign sync_match = ((dsksync[15:0] == floppy_data) && fifo_reading_sector) && trackrd;
	assign syncint = sync_match | ((~dmaen & |((~_sel & motor_on) & disk_present)) & sof);
	always @(posedge clk)
		if (clk7_en) begin
			if (!trackrd)
				trackrdok <= 0;
			else
				trackrdok <= (~wordsync | sync_match) | trackrdok;
		end
	assign fifo_reset = reset | ~dmaen;
	paula_floppy_fifo db1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(fifo_reset),
		.in(fifo_in),
		.out(fifo_out),
		.rd(fifo_rd),
		.wr(fifo_wr),
		.empty(fifo_empty),
		.full(fifo_full)
	);
	assign dskdatr[15:0] = (busrd ? fifo_out[15:0] : 16'h0000);
	assign dmal = dmaon & ((~dsklen[14] & ~fifo_empty) | (dsklen[14] & ~fifo_full));
	assign dmas = (dmaon & dsklen[14]) & ~fifo_full;
	reg [1:0] nextstate;
	parameter DISKDMA_ACTIVE = 2'b10;
	parameter DISKDMA_INT = 2'b11;
	always @(posedge clk) begin : sv2v_autoblock_2
		integer i;
		for (i = 0; i < 4; i = i + 1)
			if (sdc_image_mounted[i])
				disk_present[i] <= |sdc_image_size;
	end
	assign disk_writable = (floppy_wrprot ? 4'b0000 : 4'b1111);
	assign fdd_led = dskstate != DISKDMA_IDLE;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				dskstate <= DISKDMA_IDLE;
			else begin
				if ((dskstate != DISKDMA_ACTIVE) && (nextstate == DISKDMA_ACTIVE))
					$display("paula_floppy.v: DMA getting active, RD:%0d WR:%0d", ~lenzero & ~dsklen[14], dsklen[14]);
				if ((dskstate != DISKDMA_INT) && (nextstate == DISKDMA_INT))
					$display("paula_floppy.v: DMA generating irq");
				if ((dskstate != DISKDMA_IDLE) && (nextstate == DISKDMA_IDLE))
					$display("paula_floppy.v: DMA becoming idle");
				dskstate <= nextstate;
			end
		end
	always @(*)
		case (dskstate)
			DISKDMA_IDLE: begin
				trackrd = 0;
				trackwr = 0;
				dmaon = 0;
				blckint = 0;
				if (dma_active)
					nextstate = DISKDMA_ACTIVE;
				else
					nextstate = DISKDMA_IDLE;
			end
			DISKDMA_ACTIVE: begin
				trackrd = ~lenzero & ~dsklen[14];
				trackwr = dsklen[14];
				dmaon = ~lenzero | ~dsklen[14];
				blckint = 0;
				if (!dmaen || !enable)
					nextstate = DISKDMA_IDLE;
				else if ((lenzero && fifo_empty) && !fifo_wr_del)
					nextstate = DISKDMA_INT;
				else
					nextstate = DISKDMA_ACTIVE;
			end
			DISKDMA_INT: begin
				trackrd = 0;
				trackwr = 0;
				dmaon = 0;
				blckint = 1;
				nextstate = DISKDMA_IDLE;
			end
			default: begin
				trackrd = 1'bx;
				trackwr = 1'bx;
				dmaon = 1'bx;
				blckint = 1'bx;
				nextstate = DISKDMA_IDLE;
			end
		endcase
endmodule
module paula_floppy_dma_buf (
	dina,
	dinb,
	bsa,
	bsb,
	addra,
	addrb,
	clk,
	wrena,
	wrenb,
	douta,
	doutb
);
	input wire [15:0] dina;
	input wire [15:0] dinb;
	input wire [1:0] bsa;
	input wire [1:0] bsb;
	input wire [7:0] addra;
	input wire [7:0] addrb;
	input wire clk;
	input wire wrena;
	input wire wrenb;
	output wire [15:0] douta;
	output wire [15:0] doutb;
	reg [7:0] hi [0:255];
	reg [7:0] lo [0:255];
	reg [7:0] hi_a;
	reg [7:0] hi_b;
	reg [7:0] lo_a;
	reg [7:0] lo_b;
	assign douta = {hi_a, lo_a};
	assign doutb = {hi_b, lo_b};
	always @(posedge clk) begin
		if (wrena && bsa[1])
			hi[addra] <= dina[15:8];
		hi_a <= hi[addra];
	end
	always @(posedge clk) begin
		if (wrenb && bsb[1])
			hi[addrb] <= dinb[15:8];
		hi_b <= hi[addrb];
	end
	always @(posedge clk) begin
		if (wrena && bsa[0])
			lo[addra] <= dina[7:0];
		lo_a <= lo[addra];
	end
	always @(posedge clk) begin
		if (wrenb && bsb[0])
			lo[addrb] <= dinb[7:0];
		lo_b <= lo[addrb];
	end
endmodule
module paula_floppy_fifo (
	clk,
	clk7_en,
	reset,
	in,
	out,
	rd,
	wr,
	empty,
	full
);
	input clk;
	input clk7_en;
	input reset;
	input [15:0] in;
	output reg [15:0] out;
	input rd;
	input wr;
	output wire empty;
	output wire full;
	reg [15:0] mem [2047:0];
	reg [11:0] in_ptr;
	reg [11:0] out_ptr;
	wire equal = in_ptr[10:0] == out_ptr[10:0];
	assign empty = equal && (in_ptr[11] == out_ptr[11]);
	assign full = equal && (in_ptr[11] != out_ptr[11]);
	reg [15:0] mem_next;
	always @(posedge clk) mem_next <= mem[out_ptr[10:0] + 11'd1];
	always @(posedge clk) begin : sv2v_autoblock_1
		reg empty_write;
		if (clk7_en) begin
			empty_write <= 1'b0;
			if (reset) begin
				in_ptr <= 12'd0;
				out_ptr <= 12'd0;
				empty_write <= 1'b0;
			end
			else begin
				if (rd && !empty) begin
					if (empty_write)
						out <= in;
					else
						out <= mem_next;
					out_ptr <= out_ptr + 12'd1;
				end
				if (wr && !full) begin
					mem[in_ptr[10:0]] <= in;
					in_ptr <= in_ptr + 12'd1;
					if (empty) begin
						out <= in;
						empty_write <= 1'b1;
					end
				end
			end
		end
	end
endmodule
module ide (
	clk,
	rst_n,
	irq,
	drq,
	use_fast,
	no_data,
	drive_en,
	io_address,
	io_read,
	io_readdata,
	io_write,
	io_writedata,
	io_32,
	io_wait,
	request,
	mgmt_address,
	mgmt_write,
	mgmt_writedata,
	mgmt_read,
	mgmt_readdata
);
	input clk;
	input rst_n;
	output reg irq;
	output wire drq;
	input use_fast;
	output wire no_data;
	output wire [1:0] drive_en;
	input [3:0] io_address;
	input io_read;
	output reg [31:0] io_readdata;
	input io_write;
	input [31:0] io_writedata;
	input io_32;
	output reg io_wait;
	output reg [2:0] request;
	input [3:0] mgmt_address;
	input mgmt_write;
	input [15:0] mgmt_writedata;
	input mgmt_read;
	output reg [15:0] mgmt_readdata;
	reg [7:0] status = 0;
	assign drq = status[3];
	reg [1:0] present = 0;
	assign drive_en = present;
	wire io_wr = io_write & |present;
	wire [31:0] buf_q;
	reg [7:0] error;
	reg [13:0] io_cnt;
	reg hob;
	reg [15:0] sector_count;
	reg [15:0] sector;
	reg [31:0] cylinder;
	reg [7:0] drv_addr;
	always @(posedge clk)
		if (io_read) begin
			if (!present)
				io_readdata <= 32'hffffffff;
			else
				case (io_address)
					0: io_readdata <= (status[3] ? {buf_q[31:16], (~io_32 & io_cnt[0] ? buf_q[31:16] : buf_q[15:0])} : 32'd0);
					1: io_readdata <= error;
					2: io_readdata <= (hob ? sector_count[15:8] : sector_count[7:0]);
					3: io_readdata <= (hob ? sector[15:8] : sector[7:0]);
					4: io_readdata <= (hob ? cylinder[23:16] : cylinder[7:0]);
					5: io_readdata <= (hob ? cylinder[31:24] : cylinder[15:8]);
					6: io_readdata <= drv_addr;
					7: io_readdata <= status;
					14: io_readdata <= status;
					15: io_readdata <= {2'b10, ~drv_addr[3:0], ~drv_addr[4], drv_addr[4]};
					default: io_readdata <= 0;
				endcase
		end
	reg [7:0] features;
	always @(posedge clk)
		if (~rst_n)
			features <= 8'h00;
		else if (io_wr && (io_address == 1))
			features <= io_writedata[7:0];
	reg [15:0] blk_size;
	always @(posedge clk)
		if (~rst_n)
			blk_size <= 16'h0000;
		else if (mgmt_write && (mgmt_address == 0))
			blk_size <= {mgmt_writedata[7:0], 8'h00};
		else if ((mgmt_write && (mgmt_address == 4)) && blk_size[15])
			blk_size <= mgmt_writedata[15:0];
	always @(posedge clk)
		if (~rst_n)
			error <= 8'h00;
		else if (mgmt_write && (mgmt_address == 0))
			error <= mgmt_writedata[15:8];
	always @(posedge clk)
		if (~rst_n)
			sector_count <= 16'd1;
		else if (mgmt_write && (mgmt_address == 1))
			sector_count[7:0] <= mgmt_writedata[7:0];
		else if (mgmt_write && (mgmt_address == 3))
			sector_count[15:8] <= mgmt_writedata[7:0];
		else if (io_wr && (io_address == 2))
			sector_count <= {sector_count[7:0], io_writedata[7:0]};
	always @(posedge clk)
		if (~rst_n)
			sector <= 16'd1;
		else if (mgmt_write && (mgmt_address == 1))
			sector[7:0] <= mgmt_writedata[15:8];
		else if (mgmt_write && (mgmt_address == 3))
			sector[15:8] <= mgmt_writedata[15:8];
		else if (io_wr && (io_address == 3))
			sector <= {sector[7:0], io_writedata[7:0]};
	always @(posedge clk)
		if (~rst_n)
			cylinder <= 32'hffffffff;
		else if (mgmt_write && (mgmt_address == 2))
			cylinder[15:0] <= mgmt_writedata[15:0];
		else if (mgmt_write && (mgmt_address == 4))
			cylinder[31:16] <= mgmt_writedata[15:0];
		else if (io_wr && (io_address == 4))
			{cylinder[23:16], cylinder[7:0]} <= {cylinder[7:0], io_writedata[7:0]};
		else if (io_wr && (io_address == 5))
			{cylinder[31:24], cylinder[15:8]} <= {cylinder[15:8], io_writedata[7:0]};
	always @(posedge clk)
		if (~rst_n)
			drv_addr <= 8'd0;
		else if (mgmt_write && (mgmt_address == 5))
			drv_addr <= mgmt_writedata[7:0];
		else if (io_wr && (io_address == 6))
			drv_addr <= io_writedata[7:0];
	reg [7:0] cmd;
	always @(posedge clk)
		if (~rst_n)
			cmd <= 8'd0;
		else if (io_wr && (io_address == 7))
			cmd <= io_writedata[7:0];
	wire reset;
	wire io_done;
	reg last_read = 0;
	always @(posedge clk)
		if (reset)
			status <= 8'h80;
		else if (mgmt_write && (mgmt_address == 5))
			status <= {mgmt_writedata[15:14], 1'b0, mgmt_writedata[12:11], 2'b00, mgmt_writedata[8]};
		else if (io_wr && (io_address == 7))
			status <= 8'h80;
		else if ((io_done & drq) & last_read)
			status <= 8'h40;
		else if (io_done & drq)
			status <= 8'h80;
	always @(posedge clk)
		if (reset)
			last_read <= 0;
		else if (mgmt_write && (mgmt_address == 5))
			last_read <= mgmt_writedata[9];
		else if (io_done & drq)
			last_read <= 0;
	reg fast_read = 0;
	always @(posedge clk)
		if (reset)
			fast_read <= 0;
		else if (mgmt_write && (mgmt_address == 5))
			fast_read <= mgmt_writedata[13];
		else if (io_done & drq)
			fast_read <= 0;
	reg sw_reset;
	reg use_wait = 0;
	always @(posedge clk)
		if (~rst_n)
			io_wait <= 1'd0;
		else if (sw_reset)
			io_wait <= use_wait;
		else if (mgmt_write && (mgmt_address == 5))
			io_wait <= 1'd0;
		else if (io_wr && (io_address == 7))
			io_wait <= use_wait;
		else if (io_done & drq)
			io_wait <= use_wait;
	always @(posedge clk)
		if (reset)
			request <= 3'b110;
		else if (mgmt_write && (mgmt_address == 5))
			request <= 3'b000;
		else if (io_wr && (io_address == 7))
			request <= 3'b100;
		else if ((io_done & drq) & ~last_read)
			request <= 3'b101;
	reg disable_irq;
	always @(posedge clk)
		if (reset)
			irq <= 1'b0;
		else if (((mgmt_write && (mgmt_address == 5)) && mgmt_writedata[10]) && ~disable_irq)
			irq <= 1'b1;
		else if ((io_read | io_wr) && (io_address == 7))
			irq <= 1'b0;
	reg [13:0] mgmt_cnt;
	wire [31:0] buf_readdata;
	always @(posedge clk)
		case (mgmt_address)
			0: mgmt_readdata <= {features, 6'd0, use_fast, io_done};
			1: mgmt_readdata <= {sector[7:0], sector_count[7:0]};
			2: mgmt_readdata <= {cylinder[15:0]};
			3: mgmt_readdata <= {sector[15:8], sector_count[15:8]};
			4: mgmt_readdata <= {cylinder[31:16]};
			5: mgmt_readdata <= {cmd, drv_addr};
			default: mgmt_readdata <= (mgmt_cnt[0] ? buf_readdata[31:16] : buf_readdata[15:0]);
		endcase
	reg [1:0] hob_ena = 0;
	always @(posedge clk) begin
		if ((mgmt_write && (mgmt_address == 6)) && mgmt_writedata[3])
			{hob_ena[0], present[0]} <= mgmt_writedata[1:0];
		if ((mgmt_write && (mgmt_address == 6)) && mgmt_writedata[7])
			{hob_ena[1], present[1]} <= mgmt_writedata[5:4];
	end
	always @(posedge clk)
		if ((mgmt_write && (mgmt_address == 6)) && mgmt_writedata[9])
			use_wait <= mgmt_writedata[8];
	assign reset = ~rst_n | sw_reset;
	always @(posedge clk)
		if (reset)
			disable_irq <= 1'b0;
		else if (io_wr && (io_address == 14))
			disable_irq <= io_writedata[1];
	always @(posedge clk)
		if (~rst_n)
			sw_reset <= 1'b0;
		else if (io_wr && (io_address == 14))
			sw_reset <= io_writedata[2];
	reg hob_pre;
	always @(posedge clk)
		if (reset)
			hob_pre <= 1'b0;
		else if (io_wr && (io_address == 14))
			hob_pre <= io_writedata[7];
	always @(posedge clk) hob <= hob_pre & hob_ena[drv_addr[4]];
	wire write_data_io = (io_wr && (io_address == 0)) && drq;
	wire read_data_io = (io_read && (io_address == 0)) && drq;
	assign io_done = blk_size && (io_cnt >= blk_size);
	wire io_stb = read_data_io | write_data_io;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg old_stb;
		reg r_32;
		old_stb <= io_stb;
		if (io_stb)
			r_32 <= io_32;
		if (reset)
			io_cnt <= 0;
		else if (mgmt_write && (mgmt_address == 5))
			io_cnt <= 0;
		else if (old_stb & ~io_stb)
			io_cnt <= (io_cnt + 1'd1) + r_32;
	end
	always @(posedge clk) begin : sv2v_autoblock_2
		reg old_wr;
		reg old_rd;
		old_wr <= mgmt_write;
		old_rd <= mgmt_read;
		if ((old_wr & ~mgmt_write) | (old_rd & ~mgmt_read)) begin
			if (&mgmt_address)
				mgmt_cnt <= mgmt_cnt + 1'd1;
			else
				mgmt_cnt <= 0;
		end
		if (~rst_n)
			mgmt_cnt <= 0;
	end
	wire n_data = ((mgmt_cnt[13:1] <= io_cnt[13:1]) && drq) && fast_read;
	reg [1:0] n_data_r;
	assign no_data = n_data || n_data_r;
	always @(posedge clk) n_data_r <= {n_data_r[0], n_data};
	ide_dpram io_buf0(
		.clka(clk),
		.reseta(reset),
		.cea(1'b1),
		.ada(mgmt_cnt[12:1]),
		.wrea((mgmt_write & (&mgmt_address)) & ~mgmt_cnt[0]),
		.dina(mgmt_writedata),
		.ocea(1'b1),
		.douta(buf_readdata[15:0]),
		.clkb(clk),
		.resetb(reset),
		.ceb(1'b1),
		.adb(io_cnt[12:1]),
		.wreb(write_data_io & (io_32 | ~io_cnt[0])),
		.dinb(io_writedata[15:0]),
		.oceb(1'b1),
		.doutb(buf_q[15:0])
	);
	ide_dpram io_buf1(
		.clka(clk),
		.reseta(reset),
		.cea(1'b1),
		.ada(mgmt_cnt[12:1]),
		.wrea((mgmt_write & (&mgmt_address)) & mgmt_cnt[0]),
		.dina(mgmt_writedata),
		.ocea(1'b1),
		.douta(buf_readdata[31:16]),
		.clkb(clk),
		.resetb(reset),
		.ceb(1'b1),
		.adb(io_cnt[12:1]),
		.wreb(write_data_io & (io_32 | io_cnt[0])),
		.dinb((io_32 ? io_writedata[31:16] : io_writedata[15:0])),
		.oceb(1'b1),
		.doutb(buf_q[31:16])
	);
endmodule
module ide_dpram (
	clka,
	reseta,
	cea,
	ada,
	wrea,
	dina,
	ocea,
	douta,
	clkb,
	resetb,
	ceb,
	adb,
	wreb,
	dinb,
	oceb,
	doutb
);
	input clka;
	input reseta;
	input cea;
	input [11:0] ada;
	input wrea;
	input [15:0] dina;
	input ocea;
	output reg [15:0] douta;
	input clkb;
	input resetb;
	input ceb;
	input [11:0] adb;
	input wreb;
	input [15:0] dinb;
	input oceb;
	output reg [15:0] doutb;
	reg [15:0] mem [0:4095];
	always @(posedge clka)
		if (cea) begin
			if (wrea)
				mem[ada] <= dina;
			douta <= mem[ada];
		end
	always @(posedge clkb)
		if (ceb) begin
			if (wreb)
				mem[adb] <= dinb;
			doutb <= mem[adb];
		end
endmodule
module minimig (
	cpu_address,
	cpu_data,
	cpudata_in,
	_cpu_ipl,
	_cpu_as,
	_cpu_uds,
	_cpu_lds,
	cpu_r_w,
	_cpu_dtack,
	_cpu_reset,
	_cpu_reset_in,
	nmi_addr,
	ovr,
	ovl,
	ram_data,
	ramdata_in,
	ram_address,
	_ram_bhe,
	_ram_ble,
	_ram_we,
	_ram_oe,
	chip48,
	refresh,
	rst_ext,
	rst_out,
	clk,
	clk7_en,
	clk7n_en,
	c1,
	c3,
	cck,
	eclk,
	rxd,
	txd,
	uart_serper,
	uart_rbf,
	uart_hold,
	cts,
	rts,
	dtr,
	dsr,
	cd,
	ri,
	_joy1,
	_joy2,
	_joy3,
	_joy4,
	joya1,
	joya2,
	mouse_btn,
	kms_level,
	kbd_mouse_type,
	kbd_mouse_data,
	keyboard_ack,
	pwr_led,
	fdd_led,
	fdd_step_n,
	rtc,
	memory_config,
	chipset_config,
	floppy_config,
	ide_config,
	hdd_led,
	sdc_img_mounted,
	sdc_img_size,
	sdc_rd,
	sdc_wr,
	sdc_sector,
	sdc_busy,
	sdc_done,
	sdc_byte_in_strobe,
	sdc_byte_addr,
	sdc_byte_in_data,
	sdc_byte_out_data,
	_hsync,
	_vsync,
	_csync,
	field1,
	lace,
	hblank,
	vblank,
	red,
	green,
	blue,
	ce_pix,
	res,
	htotal,
	ldata,
	rdata,
	ldata_okk,
	rdata_okk,
	ide_ena,
	ide_fast,
	ide_ext_irq,
	ide_req,
	ide_address,
	ide_write,
	ide_writedata,
	ide_read,
	ide_readdata,
	cpucfg
);
	input [23:1] cpu_address;
	output wire [15:0] cpu_data;
	input [15:0] cpudata_in;
	output wire [2:0] _cpu_ipl;
	input _cpu_as;
	input _cpu_uds;
	input _cpu_lds;
	input cpu_r_w;
	output wire _cpu_dtack;
	output wire _cpu_reset;
	input _cpu_reset_in;
	input [31:0] nmi_addr;
	output wire ovr;
	output reg ovl;
	output wire [15:0] ram_data;
	input [15:0] ramdata_in;
	output wire [23:1] ram_address;
	output wire _ram_bhe;
	output wire _ram_ble;
	output wire _ram_we;
	output wire _ram_oe;
	input [47:0] chip48;
	output wire refresh;
	input rst_ext;
	output wire rst_out;
	input clk;
	input clk7_en;
	input clk7n_en;
	input c1;
	input c3;
	input cck;
	input [9:0] eclk;
	input rxd;
	output wire txd;
	output wire [14:0] uart_serper;
	output wire uart_rbf;
	input uart_hold;
	input cts;
	output wire rts;
	output wire dtr;
	input dsr;
	input cd;
	input ri;
	input [15:0] _joy1;
	input [15:0] _joy2;
	input [15:0] _joy3;
	input [15:0] _joy4;
	input [15:0] joya1;
	input [15:0] joya2;
	input [2:0] mouse_btn;
	input kms_level;
	input [1:0] kbd_mouse_type;
	input [7:0] kbd_mouse_data;
	output wire keyboard_ack;
	output wire pwr_led;
	output wire fdd_led;
	output wire fdd_step_n;
	input [11:0] rtc;
	input [7:0] memory_config;
	input [5:0] chipset_config;
	input [3:0] floppy_config;
	input [5:0] ide_config;
	output wire hdd_led;
	input [3:0] sdc_img_mounted;
	input [31:0] sdc_img_size;
	output wire [3:0] sdc_rd;
	output wire [3:0] sdc_wr;
	output wire [31:0] sdc_sector;
	input sdc_busy;
	input sdc_done;
	input sdc_byte_in_strobe;
	input [8:0] sdc_byte_addr;
	input [7:0] sdc_byte_in_data;
	output wire [7:0] sdc_byte_out_data;
	output wire _hsync;
	output wire _vsync;
	output wire _csync;
	output wire field1;
	output wire lace;
	output wire hblank;
	output wire vblank;
	output wire [7:0] red;
	output wire [7:0] green;
	output wire [7:0] blue;
	output wire ce_pix;
	output wire [1:0] res;
	output wire [8:0] htotal;
	output wire [14:0] ldata;
	output wire [14:0] rdata;
	output wire [8:0] ldata_okk;
	output wire [8:0] rdata_okk;
	output wire ide_ena;
	output wire ide_fast;
	input ide_ext_irq;
	output wire [5:0] ide_req;
	input [4:0] ide_address;
	input ide_write;
	input [15:0] ide_writedata;
	input ide_read;
	output wire [15:0] ide_readdata;
	input [1:0] cpucfg;
	parameter [0:0] NTSC = 1'b0;
	wire [15:0] cpu_data_in;
	wire [15:0] cpu_data_out;
	wire [15:0] ram_data_in;
	wire [15:0] ram_data_out;
	wire [15:0] custom_data_in;
	wire [15:0] custom_data_out;
	wire [15:0] agnus_data_out;
	wire [15:0] paula_data_out;
	wire [15:0] denise_data_out;
	wire [15:0] user_data_out;
	wire [15:0] gary_data_out;
	wire [15:0] gayle_data_out;
	wire [15:0] cia_data_out;
	wire [15:0] ar3_data_out;
	wire [23:1] cpu_address_out;
	wire [20:1] dma_address_out;
	wire [23:1] ram_address_out;
	wire ram_rd;
	wire ram_hwr;
	wire ram_lwr;
	wire cpu_rd;
	wire rd_cyc;
	wire cpu_hwr;
	wire cpu_lwr;
	wire [8:1] reg_address;
	wire cpu_custom;
	wire dbr;
	wire dbwe;
	wire dbs;
	wire xbs;
	wire _led;
	wire [3:0] sel_chip;
	wire [2:0] sel_slow;
	wire sel_kick;
	wire sel_kick256kmirror;
	wire sel_cia;
	wire sel_reg;
	wire sel_rtc;
	wire sel_cia_a;
	wire sel_cia_b;
	wire int2;
	wire int3;
	wire int6;
	wire int6_toccata;
	wire freeze;
	wire _fire0;
	wire _fire1;
	wire _fire0_dat;
	wire _fire1_dat;
	wire [3:0] audio_dmal;
	wire [3:0] audio_dmas;
	wire disk_dmal;
	wire disk_dmas;
	wire index;
	wire sol;
	wire sof;
	wire vbl_int;
	wire strhor_denise;
	wire strhor_paula;
	wire harddis;
	wire varbeamen;
	wire _step;
	wire direc;
	wire _sel0;
	wire _sel1;
	wire _sel2;
	wire _sel3;
	wire side;
	wire _motor;
	wire _track0;
	wire _change;
	wire _ready;
	wire _wprot;
	wire [1:0] blver = 2'b00;
	wire hbl;
	wire hde;
	assign hblank = (blver ? ~hde : hbl);
	wire bls;
	wire cpurst = 1'b0;
	wire cpuhlt = 1'b0;
	wire int7;
	wire [2:0] _iplx;
	wire sel_cart;
	wire [15:0] cart_data_out;
	wire hires;
	wire sel_ide;
	wire sel_gayle;
	wire gayle_irq;
	wire gayle_nrdy;
	wire [7:0] bank;
	reg ntsc = NTSC;
	wire sys_reset;
	wire rom_readonly;
	wire reset = sys_reset | ~_cpu_reset_in;
	assign pwr_led = ~_led;
	always @(posedge clk)
		if (clk7_en && reset)
			ntsc <= chipset_config[1];
	assign ide_ena = ide_config[0];
	assign ide_fast = ~ide_config[5] & cpucfg[1];
	agnus AGNUS1(
		.clk(clk),
		.clk7_en(clk7_en),
		.cck(cck),
		.reset(reset),
		.aen(sel_reg),
		.rd(cpu_rd),
		.hwr(cpu_hwr),
		.lwr(cpu_lwr),
		.data_in(custom_data_in),
		.data_out(agnus_data_out),
		.address_in(cpu_address_out[8:1]),
		.address_out(dma_address_out),
		.reg_address_out(reg_address),
		.cpu_custom(cpu_custom),
		.dbr(dbr),
		.dbwe(dbwe),
		.refresh(refresh),
		._hsync(_hsync),
		._vsync(_vsync),
		._csync(_csync),
		.hde(hde),
		.field1(field1),
		.lace(lace),
		.hblank(hbl),
		.vblank(vblank),
		.sol(sol),
		.sof(sof),
		.vbl_int(vbl_int),
		.strhor_denise(strhor_denise),
		.strhor_paula(strhor_paula),
		.htotal(htotal),
		.harddis(harddis),
		.varbeamen(varbeamen),
		.int3(int3),
		.audio_dmal(audio_dmal),
		.audio_dmas(audio_dmas),
		.disk_dmal(disk_dmal),
		.disk_dmas(disk_dmas),
		.bls(bls),
		.ntsc(ntsc),
		.a1k(chipset_config[2]),
		.ecs(|chipset_config[4:3]),
		.aga(chipset_config[4]),
		.floppy_speed(floppy_config[0])
	);
	paula PAULA1(
		.clk(clk),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.cck(cck),
		.reset(reset),
		.reg_address_in(reg_address),
		.data_in(custom_data_in),
		.data_out(paula_data_out),
		.txd(txd),
		.rxd(rxd),
		.uart_serper(uart_serper),
		.uart_rbf(uart_rbf),
		.uart_hold(uart_hold),
		.ntsc(ntsc),
		.sof(sof),
		.strhor(strhor_paula),
		.vblint(vbl_int),
		.int2(int2 | (ide_fast ? ide_ext_irq : gayle_irq)),
		.int3(int3),
		.int6(int6 | int6_toccata),
		._ipl(_iplx),
		.audio_dmal(audio_dmal),
		.audio_dmas(audio_dmas),
		.disk_dmal(disk_dmal),
		.disk_dmas(disk_dmas),
		._step(_step),
		.direc(direc),
		._sel({_sel3, _sel2, _sel1, _sel0}),
		.side(side),
		._motor(_motor),
		._track0(_track0),
		._change(_change),
		._ready(_ready),
		._wprot(_wprot),
		.index(index),
		.fdd_led(fdd_led),
		.ldata(ldata),
		.rdata(rdata),
		.ldata_okk(ldata_okk),
		.rdata_okk(rdata_okk),
		.sdc_image_mounted(sdc_img_mounted),
		.sdc_image_size(sdc_img_size),
		.sdc_rd(sdc_rd),
		.sdc_wr(sdc_wr),
		.sdc_sector(sdc_sector),
		.sdc_busy(sdc_busy),
		.sdc_done(sdc_done),
		.sdc_byte_in_strobe(sdc_byte_in_strobe),
		.sdc_byte_addr(sdc_byte_addr),
		.sdc_byte_in_data(sdc_byte_in_data),
		.sdc_byte_out_data(sdc_byte_out_data),
		.floppy_wrprot(floppy_config[1]),
		.floppy_drives(floppy_config[3:2])
	);
	userio USERIO1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.reg_address_in(reg_address),
		.data_in(custom_data_in),
		.data_out(user_data_out),
		.pot_cnt_en((sol & ~c1) & ~c3),
		._fire0(_fire0),
		._fire1(_fire1),
		._fire0_dat(_fire0_dat),
		._fire1_dat(_fire1_dat),
		._joy1(_joy1),
		._joy2(_joy2),
		.joy_ana1(joya1),
		.joy_ana2(joya2),
		.mouse_btn(mouse_btn),
		.kbd_mouse_type(kbd_mouse_type),
		.kms_level(kms_level),
		.kbd_mouse_data(kbd_mouse_data)
	);
	wire shres;
	assign ce_pix = ((shres & |chipset_config[4:3]) | (hires & clk7n_en)) | clk7_en;
	assign res = {shres & |chipset_config[4:3], hires};
	denise DENISE1(
		.clk(clk),
		.clk7_en(clk7_en),
		.c1(c1),
		.c3(c3),
		.cck(cck),
		.reset(reset),
		.strhor(strhor_denise),
		.reg_address_in(reg_address),
		.data_in(custom_data_in),
		.chip48(chip48),
		.data_out(denise_data_out),
		.blank(~_hsync | vblank),
		.red(red),
		.green(green),
		.blue(blue),
		.a1k(chipset_config[2]),
		.ecs(|chipset_config[4:3]),
		.aga(chipset_config[4]),
		.hires(hires),
		.shres(shres)
	);
	wire [3:0] porta_out;
	assign {_fire1_dat, _fire0_dat, _led} = porta_out[3:1];
	ciaa CIAA1(
		.clk(clk),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.aen(sel_cia_a),
		.rd(cpu_rd),
		.wr(cpu_lwr | cpu_hwr),
		.reset(reset),
		.rs(cpu_address_out[11:8]),
		.data_in(cpu_data_out[7:0]),
		.data_out(cia_data_out[7:0]),
		.tick(_vsync),
		.eclk(eclk[8]),
		.irq(int2),
		.porta_in({_fire1, _fire0, _ready, _track0, _wprot, _change}),
		.porta_out(porta_out),
		.portb_in({_joy4[0], _joy4[1], _joy4[2], _joy4[3], _joy3[0], _joy3[1], _joy3[2], _joy3[3]}),
		.kbd_mouse_type(kbd_mouse_type),
		.kms_level(kms_level),
		.kbd_mouse_data(kbd_mouse_data),
		.keyboard_ack(keyboard_ack),
		.freeze(freeze),
		.hrtmon_en(memory_config[6])
	);
	ciab CIAB1(
		.clk(clk),
		.clk7_en(clk7_en),
		.aen(sel_cia_b),
		.rd(cpu_rd),
		.wr(cpu_hwr | cpu_lwr),
		.reset(reset),
		.rs(cpu_address_out[11:8]),
		.data_in(cpu_data_out[15:8]),
		.data_out(cia_data_out[15:8]),
		.tick(_hsync),
		.eclk(eclk[8]),
		.irq(int6),
		.flag(index),
		.porta_in({cd, cts, dsr, ri & _joy3[4], 1'b1, _joy4[4]}),
		.porta_out({dtr, rts}),
		.portb_out({_motor, _sel3, _sel2, _sel1, _sel0, side, direc, _step})
	);
	assign fdd_step_n = _step;
	minimig_m68k_bridge CPU1(
		.clk(clk),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.c1(c1),
		.c3(c3),
		.cck(cck),
		.eclk(eclk),
		.vpa(sel_cia),
		.dbr(dbr),
		.dbs(dbs),
		.xbs(xbs),
		.nrdy(gayle_nrdy & rd_cyc),
		.bls(bls),
		.memory_config(memory_config[3:0]),
		._as(_cpu_as),
		._lds(_cpu_lds),
		._uds(_cpu_uds),
		.r_w(cpu_r_w),
		._dtack(_cpu_dtack),
		.rd(cpu_rd),
		.rd_cyc(rd_cyc),
		.hwr(cpu_hwr),
		.lwr(cpu_lwr),
		.address(cpu_address),
		.address_out(cpu_address_out),
		.cpudatain(cpudata_in),
		.data(cpu_data),
		.data_out(cpu_data_out),
		.data_in(cpu_data_in)
	);
	minimig_bankmapper BMAP1(
		.chip0((((~ovr | ~cpu_rd) | dbr) | cpuhlt) & sel_chip[0]),
		.chip1(sel_chip[1]),
		.chip2(sel_chip[2]),
		.chip3(sel_chip[3]),
		.slow0(sel_slow[0]),
		.slow1(sel_slow[1]),
		.slow2(sel_slow[2]),
		.kick(sel_kick),
		.kick256kmirror(sel_kick256kmirror),
		.memory_config(memory_config[1:0]),
		.bank(bank)
	);
	minimig_sram_bridge RAM1(
		.clk(clk),
		.c1(c1),
		.c3(c3),
		.bank(bank),
		.address_in(ram_address_out),
		.data_in(ram_data_in),
		.data_out(ram_data_out),
		.rd(ram_rd),
		.hwr(ram_hwr),
		.lwr(ram_lwr),
		._bhe(_ram_bhe),
		._ble(_ram_ble),
		._we(_ram_we),
		._oe(_ram_oe),
		.address(ram_address),
		.data(ram_data),
		.ramdata_in(ramdata_in)
	);
	assign int7 = 1'b0;
	assign ovr = 1'b0;
	assign sel_cart = 1'b0;
	assign _cpu_ipl = (int7 ? 3'b000 : _iplx);
	gary GARY1(
		.cpu_address_in(cpu_address_out),
		.dma_address_in(dma_address_out),
		.ram_address_out(ram_address_out),
		.cpu_data_out(cpu_data_out),
		.cpu_data_in(gary_data_out),
		.custom_data_out(custom_data_out),
		.custom_data_in(custom_data_in),
		.ram_data_out(ram_data_out),
		.ram_data_in(ram_data_in),
		.cpu_rd(cpu_rd),
		.cpu_hwr(cpu_hwr),
		.cpu_lwr(cpu_lwr),
		.cpu_hlt(cpuhlt),
		.ovl(ovl),
		.dbr(dbr),
		.dbwe(dbwe),
		.dbs(dbs),
		.xbs(xbs),
		.memory_config(memory_config[3:0]),
		.hdc_ena(ide_ena & ~ide_fast),
		.ram_rd(ram_rd),
		.ram_hwr(ram_hwr),
		.ram_lwr(ram_lwr),
		.ecs(|chipset_config[4:3]),
		.a1k(chipset_config[2]),
		.sel_chip(sel_chip),
		.sel_slow(sel_slow),
		.sel_kick(sel_kick),
		.sel_kick256kmirror(sel_kick256kmirror),
		.sel_cia(sel_cia),
		.sel_reg(sel_reg),
		.sel_cia_a(sel_cia_a),
		.sel_cia_b(sel_cia_b),
		.sel_ide(sel_ide),
		.sel_gayle(sel_gayle),
		.sel_rtc(sel_rtc),
		.sel_rtg(),
		.reset(reset),
		.clk(clk),
		.rom_readonly(rom_readonly),
		.bootrom(1'b0)
	);
	gayle GAYLE1(
		.clk(clk),
		.reset(reset),
		.addr(cpu_address_out),
		.data_in(cpu_data_out),
		.data_out(gayle_data_out),
		.rd(cpu_rd),
		.wr(cpu_hwr),
		.sel_ide(sel_ide),
		.sel_gayle(sel_gayle),
		.irq(gayle_irq),
		.nrdy(gayle_nrdy),
		.longword(1'b0),
		.ide_req(ide_req),
		.ide_address(ide_address),
		.ide_write(ide_write),
		.ide_writedata(ide_writedata),
		.ide_read(ide_read),
		.ide_readdata(ide_readdata),
		.led(hdd_led)
	);
	minimig_syscontrol CONTROL1(
		.clk(clk),
		.clk7_en(clk7_en),
		.cnt(sof),
		.mrst(rst_ext),
		.reset(sys_reset)
	);
	always @(posedge clk)
		if (~_cpu_reset | ~_cpu_reset_in)
			ovl <= 1;
		else if (sel_cia_a & (cpu_lwr | cpu_hwr))
			ovl <= 0;
	reg [3:0] rtc_reg_0;
	reg [3:0] rtc_reg_1;
	reg [3:0] rtc_reg_2;
	reg [3:0] rtc_reg_3;
	reg [3:0] rtc_reg_4;
	reg [3:0] rtc_reg_5;
	reg [3:0] rtc_reg_6;
	reg [3:0] rtc_reg_7;
	reg [3:0] rtc_reg_8;
	reg [3:0] rtc_reg_9;
	reg [3:0] rtc_reg_A;
	reg [3:0] rtc_reg_B;
	reg [3:0] rtc_reg_C;
	wire [15:0] rtc_out = (!(sel_rtc && cpu_rd) ? 16'h0000 : {12'h000, (cpu_address_out[5:2] == 4'h0 ? rtc_reg_0 : (cpu_address_out[5:2] == 4'h1 ? rtc_reg_1 : (cpu_address_out[5:2] == 4'h2 ? rtc_reg_2 : (cpu_address_out[5:2] == 4'h3 ? rtc_reg_3 : (cpu_address_out[5:2] == 4'h4 ? rtc_reg_4 : (cpu_address_out[5:2] == 4'h5 ? rtc_reg_5 : (cpu_address_out[5:2] == 4'h6 ? rtc_reg_6 : (cpu_address_out[5:2] == 4'h7 ? rtc_reg_7 : (cpu_address_out[5:2] == 4'h8 ? rtc_reg_8 : (cpu_address_out[5:2] == 4'h9 ? rtc_reg_9 : (cpu_address_out[5:2] == 4'ha ? rtc_reg_A : (cpu_address_out[5:2] == 4'hb ? rtc_reg_B : (cpu_address_out[5:2] == 4'hc ? rtc_reg_C : (cpu_address_out[5:2] == 4'hd ? 4'h0 : (cpu_address_out[5:2] == 4'he ? 4'h0 : 4'h4)))))))))))))))});
	wire [7:0] b2c_in = (rtc[10:8] == 3'd1 ? rtc[7:0] + 1 : (rtc[10:8] == 3'd2 ? {3'd0, rtc[4:0]} : rtc[7:0]));
	reg [7:0] bcd;
	always @(*) begin : sv2v_autoblock_1
		integer i;
		bcd = 8'h00;
		for (i = 7; i >= 0; i = i - 1)
			begin
				if (bcd[3:0] >= 5)
					bcd[3:0] = bcd[3:0] + 4'd3;
				bcd = {bcd[6:0], b2c_in[i]};
			end
	end
	reg rtc_old_flg = 1'b0;
	reg [31:0] rtc_cnt = 32'd0;
	always @(posedge clk) begin
		rtc_old_flg <= rtc[11];
		if (rtc_old_flg ^ rtc[11]) begin
			if (rtc[10:8] == 3'd0)
				{rtc_reg_B, rtc_reg_A} <= bcd;
			if (rtc[10:8] == 3'd1)
				{rtc_reg_9, rtc_reg_8} <= bcd;
			if (rtc[10:8] == 3'd2)
				{rtc_reg_7, rtc_reg_6} <= bcd;
			if (rtc[10:8] == 3'd3)
				{rtc_reg_5, rtc_reg_4} <= bcd;
			if (rtc[10:8] == 3'd4)
				{rtc_reg_3, rtc_reg_2} <= bcd;
			if (rtc[10:8] == 3'd5)
				{rtc_reg_1, rtc_reg_0} <= bcd;
			if (rtc[10:8] == 3'd2)
				rtc_reg_C <= {1'b0, rtc[7:5]};
		end
		else if (rtc_cnt < 32'd28375159)
			rtc_cnt <= rtc_cnt + 32'd1;
		else begin
			rtc_cnt <= 32'd0;
			if (rtc_reg_0 < 9)
				rtc_reg_0 <= rtc_reg_0 + 4'd1;
			else begin
				rtc_reg_0 <= 4'h0;
				if (rtc_reg_1 < 5)
					rtc_reg_1 <= rtc_reg_1 + 4'd1;
				else begin
					rtc_reg_1 <= 4'h0;
					if (rtc_reg_2 < 9)
						rtc_reg_2 <= rtc_reg_2 + 4'd1;
					else begin
						rtc_reg_2 <= 4'h0;
						if (rtc_reg_2 < 5)
							rtc_reg_2 <= rtc_reg_2 + 4'd1;
						else begin
							rtc_reg_2 <= 4'h0;
							if (rtc_reg_3 < 9)
								rtc_reg_3 <= rtc_reg_3 + 4'd1;
							else begin
								rtc_reg_3 <= 4'h0;
								if (rtc_reg_4 < 5)
									rtc_reg_4 <= rtc_reg_4 + 4'd1;
								else
									rtc_reg_4 <= 4'h0;
							end
						end
					end
				end
			end
		end
	end
	assign int6_toccata = 1'b0;
	assign cpu_data_in = ((gary_data_out | cia_data_out) | gayle_data_out) | rtc_out;
	assign custom_data_out[15:0] = ((agnus_data_out[15:0] | paula_data_out[15:0]) | denise_data_out[15:0]) | user_data_out[15:0];
	reg _rst;
	assign _cpu_reset = _rst;
	always @(posedge clk) begin : sv2v_autoblock_2
		reg r;
		r <= ~(cpurst || sys_reset);
		_rst <= r;
	end
	assign rst_out = reset;
endmodule
module cpu_wrapper (
	reset,
	reset_out,
	clk,
	ph1,
	ph2,
	gap,
	cpucfg,
	fastramcfg,
	chipramcfg,
	slowramcfg,
	turbocfg,
	bootrom,
	chip_addr,
	chip_dout,
	chip_din,
	chip_as,
	chip_uds,
	chip_lds,
	chip_rw,
	chip_dtack,
	chip_ipl,
	ramreq,
	ramaddr,
	ramdin,
	ramdout,
	ramready,
	ramlds,
	ramuds,
	ramshared,
	cpustate,
	cacr,
	nmi_addr,
	cpu_clkena
);
	input reset;
	output reg reset_out;
	input clk;
	input ph1;
	input ph2;
	input gap;
	input [1:0] cpucfg;
	input [2:0] fastramcfg;
	input [1:0] chipramcfg;
	input [1:0] slowramcfg;
	input [2:0] turbocfg;
	input bootrom;
	output reg [23:1] chip_addr;
	input [15:0] chip_dout;
	output reg [15:0] chip_din;
	output reg chip_as;
	output reg chip_uds;
	output reg chip_lds;
	output reg chip_rw;
	input chip_dtack;
	input [2:0] chip_ipl;
	output wire ramreq;
	output wire [28:1] ramaddr;
	output wire [15:0] ramdin;
	input [15:0] ramdout;
	input ramready;
	output wire ramlds;
	output wire ramuds;
	output wire ramshared;
	output reg [1:0] cpustate;
	output reg [3:0] cacr;
	output reg [31:0] nmi_addr;
	output wire cpu_clkena;
	reg skip_fetch;
	wire cpu_req = (cpustate != 2'b01) && !skip_fetch;
	reg [31:0] cpu_addr;
	reg [15:0] cpu_dout;
	reg wr;
	reg uds_in;
	reg lds_in;
	reg z2ram_ena;
	reg [4:0] z3ram_base0;
	reg [3:0] z3ram_base1;
	reg z3ram_ena0;
	reg z3ram_ena1;
	wire sel_z3ram0 = (cpu_addr[31:27] == z3ram_base0) && z3ram_ena0;
	wire sel_z3ram1 = (cpu_addr[31:28] == z3ram_base1) && z3ram_ena1;
	wire sel_z2ram = ((cpu_addr[31:24] == 8'h00) && (cpu_addr[23] ^ |cpu_addr[22:21])) && z2ram_ena;
	wire sel_zram = (sel_z3ram0 | sel_z3ram1) | sel_z2ram;
	wire sel_dd = 1'b0;
	wire sel_rtg = 1'b0;
	wire cchip;
	wire ckick;
	wire cslow;
	reg [2:0] fastramcfg_d;
	reg [1:0] chipramcfg_d;
	reg [1:0] slowramcfg_d;
	always @(posedge clk)
		if (~reset | ~reset_out) begin
			fastramcfg_d <= fastramcfg;
			chipramcfg_d <= chipramcfg;
			slowramcfg_d <= slowramcfg;
		end
	wire sel_kick = (cpu_addr[31:24] == 8'h00) && &cpu_addr[23:19];
	wire sel_kickram = (sel_kick && ckick) && wr;
	wire sel_chip = ((((cpu_addr[31:24] == 8'h00) && (cpu_addr[23:21] == 3'b000)) && ((chipramcfg_d != 2'b00) || (cpu_addr[20:19] == 2'b00))) && ((chipramcfg_d != 2'b01) || (cpu_addr[20] == 1'b0))) && ((chipramcfg_d != 2'b10) || (cpu_addr[20:19] != 2'b11));
	wire sel_chipram = sel_chip && cchip;
	wire sel_slow = (cpu_addr[31:24] == 8'h00) && (((cpu_addr[23:20] == 4'b1100) && (((cpu_addr[19] == 1'b0) && (slowramcfg_d != 2'b00)) || ((cpu_addr[19] == 1'b1) && (slowramcfg_d[1] == 1'b1)))) || ((cpu_addr[23:20] == 4'b1101) && ((cpu_addr[19] == 1'b0) && (slowramcfg_d == 2'b11))));
	wire sel_slowram = sel_slow && cslow;
	wire sel_nmi_vector;
	reg ram_dtack = 1'b1;
	wire ramsel = (cpu_req & ~sel_nmi_vector) & (((((sel_zram | sel_chipram) | sel_kickram) | sel_slowram) | sel_dd) | sel_rtg);
	assign ramreq = ramsel & ram_dtack;
	assign ramshared = sel_dd;
	reg [31:0] vbr;
	always @(posedge clk) nmi_addr <= vbr + 32'h0000007c;
	assign sel_nmi_vector = 1'b0;
	wire [15:0] ramdat;
	assign ramlds = (sel_rtg ? uds_in : lds_in);
	assign ramuds = (sel_rtg ? lds_in : uds_in);
	assign ramdin = (sel_rtg ? {cpu_dout[7:0], cpu_dout[15:8]} : cpu_dout);
	assign ramdat = (sel_rtg ? {ramdout[7:0], ramdout[15:8]} : ramdout);
	assign ramaddr[28:24] = 5'b00000;
	assign ramaddr[23:21] = (cpu_addr[23:21] == 3'b011 ? 3'b101 : (cpu_addr[23:21] == 3'b100 ? 3'b100 : {1'b0, cpu_addr[22], cpu_addr[21] | cpu_addr[23]}));
	assign ramaddr[20:1] = cpu_addr[20:1];
	wire sel_autoconfig;
	reg [1:0] autocfg_card;
	reg [3:0] autocfg_data;
	reg [15:0] chip_data;
	wire [15:0] cpu_din = (ramsel ? ramdat : {(sel_autoconfig ? autocfg_data : chip_data[15:12]), chip_data[11:0]});
	wire skip_fetch_i;
	wire [15:0] cpu_dout_o;
	wire [23:1] cpu_addr_o;
	wire [2:0] fc_o;
	wire wr_o;
	wire as_o;
	wire uds_o;
	wire lds_o;
	wire reset_out_o;
	always @(*) begin
		cpu_dout = cpu_dout_o;
		cpu_addr = {8'b00000000, cpu_addr_o, 1'b0};
		casez ({as_o, fc_o[1], fc_o[0], wr_o})
			4'b0101: cpustate = 2'b00;
			4'b0011: cpustate = 2'b10;
			4'b0zz0: cpustate = 2'b11;
			default: cpustate = 2'b01;
		endcase
		cacr = 1;
		vbr = 0;
		wr = wr_o;
		uds_in = uds_o;
		lds_in = lds_o;
		reset_out = reset_out_o;
		chip_as = ramsel | as_o;
		chip_rw = wr_o;
		chip_uds = uds_o;
		chip_lds = lds_o;
		chip_addr = cpu_addr_o[23:1];
		chip_din = cpu_dout_o;
		chip_data = chip_dout;
		skip_fetch = 0;
	end
	always @(posedge clk)
		if (~reset | ~reset_out)
			ram_dtack <= 1'b1;
		else begin
			if (ph2)
				ram_dtack <= ramsel;
			if (ramready)
				ram_dtack <= 1'b0;
		end
	wire [15:0] cpu_dout_p;
	wire [31:0] cpu_addr_p;
	wire [1:0] cpustate_p;
	wire [3:0] cacr_p;
	wire [31:0] vbr_p;
	wire wr_p;
	wire uds_p;
	wire lds_p;
	wire reset_out_p;
	wire longword;
	reg [2:0] cpu_ipl;
	reg chipready;
	reg tg68_armed;
	always @(posedge clk) tg68_armed <= reset;
	assign cpu_clkena = ((~cpu_req & tg68_armed) | chipready) | ramready;
	fx68k cpu_inst_o(
		.clk(clk),
		.enPhi1(ph1),
		.enPhi2(ph2),
		.extReset(~reset),
		.pwrUp(~reset),
		.oRESETn(reset_out_o),
		.HALTn(1),
		.eRWn(wr_o),
		.ASn(as_o),
		.LDSn(lds_o),
		.UDSn(uds_o),
		.DTACKn((ramsel ? ram_dtack : chip_dtack)),
		.FC0(fc_o[0]),
		.FC1(fc_o[1]),
		.FC2(fc_o[2]),
		.VPAn(~&fc_o),
		.BERRn(1),
		.BRn(1),
		.BGACKn(1),
		.IPL0n(chip_ipl[0]),
		.IPL1n(chip_ipl[1]),
		.IPL2n(chip_ipl[2]),
		.iEdb(cpu_din),
		.oEdb(cpu_dout_o),
		.eab(cpu_addr_o)
	);
	reg turbochip_d;
	reg turbokick_d;
	reg turbodata_d;
	assign cchip = turbochip_d && (turbodata_d || (cpustate == 2'b00));
	assign ckick = turbokick_d && (turbodata_d || (cpustate == 2'b00));
	assign cslow = turbochip_d | turbodata_d;
	always @(posedge clk)
		if (~reset | ~reset_out) begin
			turbochip_d <= 0;
			turbokick_d <= 0;
			turbodata_d <= 0;
		end
		else if (~cpu_req) begin
			turbochip_d <= turbocfg[0];
			turbokick_d <= turbocfg[1];
			turbodata_d <= turbocfg[2];
		end
	wire cfg_z3 = fastramcfg_d[2] & cpucfg[1];
	wire fast_ac = |autocfg_card;
	always @(*) begin
		autocfg_data = 4'b1111;
		if (fast_ac) begin
			if (~cfg_z3)
				case (chip_addr[6:1])
					6'b000000: autocfg_data = 4'b1110;
					6'b000001:
						case ({autocfg_card, fastramcfg_d})
							5'b01001: autocfg_data = 4'b0110;
							5'b01010: autocfg_data = 4'b0111;
							default: autocfg_data = 4'b0000;
						endcase
					6'b001000: autocfg_data = 4'b1110;
					6'b001001: autocfg_data = 4'b1100;
					6'b001010: autocfg_data = 4'b0110;
					6'b001011: autocfg_data = 4'b0011;
					6'b010011: autocfg_data = 4'b1110;
					default:
						;
				endcase
			else
				case (chip_addr[6:1])
					6'b000000: autocfg_data = 4'b1010;
					6'b000001: autocfg_data = (autocfg_card[1] ? 4'b0011 : 4'b0100);
					6'b000010: autocfg_data = 4'b1110;
					6'b000100: autocfg_data = 4'b0000;
					6'b000101: autocfg_data = 4'b1111;
					6'b001000: autocfg_data = 4'b1110;
					6'b001001: autocfg_data = 4'b1100;
					6'b001010: autocfg_data = 4'b0110;
					6'b001011: autocfg_data = 4'b0011;
					6'b010011: autocfg_data = {2'b11, ~autocfg_card};
					default:
						;
				endcase
		end
	end
	assign sel_autoconfig = (|fastramcfg_d && (chip_addr[23:16] == 8'b11101000)) && |autocfg_card;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg old_uds;
		old_uds <= chip_uds;
		if (~reset | ~reset_out) begin
			autocfg_card <= 2'b01;
			z2ram_ena <= 0;
			z3ram_ena0 <= 0;
			z3ram_ena1 <= 0;
			z3ram_base0 <= 1;
			z3ram_base1 <= 1;
		end
		else if (((sel_autoconfig && ~chip_rw) && ~chip_uds) && old_uds) begin
			if (~cfg_z3) begin
				if (chip_addr[6:1] == 6'b100100) begin
					z2ram_ena <= 1;
					autocfg_card <= 2'b00;
				end
			end
			else if (chip_addr[6:1] == 6'b100010) begin
				if (autocfg_card == 2'b01) begin
					z3ram_base1 <= cpu_dout[15:12];
					z3ram_ena1 <= 1;
					autocfg_card <= {fastramcfg_d[0], 1'b0};
				end
				else begin
					z3ram_base0 <= cpu_dout[15:11];
					z3ram_ena0 <= 1;
					autocfg_card <= 2'b00;
				end
			end
		end
	end
	wire chipreq = cpu_req & ~ramsel;
	reg [15:0] chipdout_i;
	reg c_as;
	reg c_rw;
	reg c_uds;
	reg c_lds;
	always @(posedge clk or negedge reset) begin : sv2v_autoblock_2
		reg [1:0] stage;
		reg waitm;
		reg ready;
		if (~reset) begin
			stage <= 0;
			c_as <= 1;
			c_rw <= 1;
			c_uds <= 1;
			c_lds <= 1;
			chipready <= 0;
		end
		else begin
			chipready <= 0;
			if (ph2)
				case (stage)
					0: cpu_ipl <= chip_ipl;
					1:
						;
					2: begin
						cpu_ipl <= chip_ipl;
						waitm <= chip_dtack;
					end
					3: begin
						c_as <= 1;
						c_rw <= 1;
						c_uds <= 1;
						c_lds <= 1;
						ready <= 0;
						chipdout_i <= chip_dout;
						if (!ready)
							chipready <= 1;
						else
							stage <= 0;
					end
					default:
						;
				endcase
			if (ph1)
				case (stage)
					0:
						if (chipreq) begin
							c_as <= 0;
							c_rw <= wr;
							c_uds <= uds_in;
							c_lds <= lds_in;
							stage <= 1;
						end
					1: stage <= 2;
					2:
						if (~waitm)
							stage <= 3;
					3: ready <= 1;
				endcase
		end
	end
endmodule
module gary (
	cpu_address_in,
	dma_address_in,
	ram_address_out,
	cpu_data_out,
	cpu_data_in,
	custom_data_out,
	custom_data_in,
	ram_data_out,
	ram_data_in,
	a1k,
	bootrom,
	clk,
	reset,
	cpu_rd,
	cpu_hwr,
	cpu_lwr,
	cpu_hlt,
	ovl,
	dbr,
	dbwe,
	dbs,
	xbs,
	memory_config,
	ecs,
	hdc_ena,
	ram_rd,
	ram_hwr,
	ram_lwr,
	sel_reg,
	sel_chip,
	sel_slow,
	sel_kick,
	sel_kick256kmirror,
	sel_cia,
	sel_cia_a,
	sel_cia_b,
	sel_rtg,
	sel_rtc,
	sel_ide,
	sel_gayle,
	rom_readonly
);
	input [23:1] cpu_address_in;
	input [20:1] dma_address_in;
	output wire [23:1] ram_address_out;
	input [15:0] cpu_data_out;
	output wire [15:0] cpu_data_in;
	input [15:0] custom_data_out;
	output wire [15:0] custom_data_in;
	input [15:0] ram_data_out;
	output wire [15:0] ram_data_in;
	input a1k;
	input bootrom;
	input clk;
	input reset;
	input cpu_rd;
	input cpu_hwr;
	input cpu_lwr;
	input cpu_hlt;
	input ovl;
	input dbr;
	input dbwe;
	output wire dbs;
	output wire xbs;
	input [3:0] memory_config;
	input ecs;
	input hdc_ena;
	output wire ram_rd;
	output wire ram_hwr;
	output wire ram_lwr;
	output wire sel_reg;
	output reg [3:0] sel_chip;
	output reg [2:0] sel_slow;
	output reg sel_kick;
	output reg sel_kick256kmirror;
	output wire sel_cia;
	output wire sel_cia_a;
	output wire sel_cia_b;
	output wire sel_rtg;
	output wire sel_rtc;
	output wire sel_ide;
	output wire sel_gayle;
	output reg rom_readonly;
	wire [2:0] t_sel_slow;
	wire sel_xram;
	wire sel_bank_1;
	assign ram_data_in = (dbr ? custom_data_out : cpu_data_out);
	assign custom_data_in = (dbr ? ram_data_out : (cpu_rd ? 16'hffff : cpu_data_out));
	assign cpu_data_in = (dbr ? 16'h0000 : (custom_data_out | ram_data_out) | {16 {sel_bank_1}});
	assign ram_rd = (dbr ? ~dbwe : cpu_rd);
	assign ram_hwr = (dbr ? dbwe : cpu_hwr);
	assign ram_lwr = (dbr ? dbwe : cpu_lwr);
	assign ram_address_out = (dbr ? {3'b000, dma_address_in[20:1]} : cpu_address_in[23:1]);
	always @(posedge clk)
		if (reset)
			rom_readonly <= ~bootrom;
		else if ((cpu_hwr || cpu_lwr) && (cpu_address_in[23:18] == 6'b111110))
			rom_readonly <= 1;
	always @(*)
		if (dbr) begin
			sel_chip[0] = ~dma_address_in[20] & ~dma_address_in[19];
			sel_chip[1] = ~dma_address_in[20] & dma_address_in[19];
			sel_chip[2] = dma_address_in[20] & ~dma_address_in[19];
			sel_chip[3] = dma_address_in[20] & dma_address_in[19];
			sel_slow[0] = (ecs && (memory_config == 4'b0100)) && (dma_address_in[20:19] == 2'b01);
			sel_slow[1] = 0;
			sel_slow[2] = 0;
			sel_kick = 0;
			sel_kick256kmirror = 0;
		end
		else begin
			sel_chip[0] = (cpu_address_in[23:19] == 5'b00000) && (!ovl || cpu_hlt);
			sel_chip[1] = cpu_address_in[23:19] == 5'b00001;
			sel_chip[2] = cpu_address_in[23:19] == 5'b00010;
			sel_chip[3] = cpu_address_in[23:19] == 5'b00011;
			sel_slow[0] = t_sel_slow[0];
			sel_slow[1] = t_sel_slow[1];
			sel_slow[2] = t_sel_slow[2];
			sel_kick = ((cpu_address_in[23:19] == 5'b11111) && ((cpu_rd || cpu_hlt) || (!rom_readonly && cpu_address_in[18]))) || ((cpu_rd && ovl) && (cpu_address_in[23:19] == 5'b00000));
			sel_kick256kmirror = ((((cpu_address_in[23:19] == 5'b11111) && cpu_rd) && rom_readonly) && !cpu_hlt) && bootrom;
		end
	assign t_sel_slow[0] = (cpu_address_in[23:19] == 5'b11000) && |memory_config[3:2];
	assign t_sel_slow[1] = (cpu_address_in[23:19] == 5'b11001) && memory_config[3];
	assign t_sel_slow[2] = (cpu_address_in[23:19] == 5'b11010) && &memory_config[3:2];
	assign sel_ide = hdc_ena && (cpu_address_in[23:16] == 8'b11011010);
	assign sel_gayle = hdc_ena && (cpu_address_in[23:12] == 12'b110111100001);
	assign sel_rtc = cpu_address_in[23:16] == 8'b11011100;
	assign sel_reg = (cpu_address_in[23:21] == 3'b110 ? ~(((|t_sel_slow | sel_rtc) | sel_ide) | sel_gayle) : 1'b0);
	assign sel_cia = cpu_address_in[23:16] == 8'hbf;
	assign sel_cia_a = sel_cia & ~cpu_address_in[12];
	assign sel_cia_b = sel_cia & ~cpu_address_in[13];
	assign sel_rtg = cpu_address_in[23:16] == 8'hb8;
	assign sel_bank_1 = cpu_address_in[23:21] == 3'b001;
	assign dbs = (((cpu_address_in[23:21] == 3'b000) || (cpu_address_in[23:20] == 4'b1100)) || (cpu_address_in[23:19] == 5'b11010)) || (cpu_address_in[23:16] == 8'b11011111);
	assign xbs = ~((sel_cia | sel_gayle) | sel_ide);
endmodule
module denise_colortable_ram_mf (
	ena_a,
	clock,
	data,
	rdaddress,
	wraddress,
	wren,
	q
);
	input [1:0] ena_a;
	input clock;
	input [11:0] data;
	input [7:0] rdaddress;
	input [7:0] wraddress;
	input wren;
	output reg [23:0] q;
	reg [11:0] ram0 [255:0];
	reg [11:0] ram1 [255:0];
	reg wren_r = 1'b0;
	reg [1:0] ena_r;
	reg [7:0] wa_r;
	reg [11:0] data_r;
	always @(posedge clock) begin
		wren_r <= wren;
		ena_r <= ena_a;
		wa_r <= wraddress;
		data_r <= data;
		if (wren_r) begin
			if (ena_r[0])
				ram0[wa_r] <= data_r;
			if (ena_r[1])
				ram1[wa_r] <= data_r;
		end
		q <= {ram1[rdaddress], ram0[rdaddress]};
	end
endmodule
module agnus_beamcounter (
	clk,
	clk7_en,
	reset,
	cck,
	ntsc,
	aga,
	ecs,
	a1k,
	data_in,
	data_out,
	reg_address_in,
	hpos,
	vpos,
	_hsync,
	_vsync,
	field1,
	lace,
	_csync,
	hblank,
	vblank,
	vbl,
	vblend,
	eol,
	eof,
	vbl_int,
	htotal_out,
	harddis_out,
	varbeamen_out
);
	input clk;
	input clk7_en;
	input reset;
	input cck;
	input ntsc;
	input aga;
	input ecs;
	input a1k;
	input [15:0] data_in;
	output reg [15:0] data_out;
	input [8:1] reg_address_in;
	output reg [8:0] hpos;
	output reg [10:0] vpos;
	output reg _hsync;
	output reg _vsync;
	output wire field1;
	output reg lace;
	output wire _csync;
	output reg hblank;
	output reg vblank;
	output wire vbl;
	output wire vblend;
	output wire eol;
	output wire eof;
	output reg vbl_int;
	output wire [8:0] htotal_out;
	output wire harddis_out;
	output wire varbeamen_out;
	parameter VPOSR = 9'h004;
	parameter VPOSW = 9'h02a;
	parameter VHPOSR = 9'h006;
	parameter VHPOSW = 9'h02c;
	parameter BPLCON0 = 9'h100;
	parameter HTOTAL = 9'h1c0;
	parameter HSSTOP = 9'h1c2;
	parameter HBSTRT = 9'h1c4;
	parameter HBSTOP = 9'h1c6;
	parameter VTOTAL = 9'h1c8;
	parameter VSSTOP = 9'h1ca;
	parameter VBSTRT = 9'h1cc;
	parameter VBSTOP = 9'h1ce;
	parameter HSSTRT = 9'h1de;
	parameter BEAMCON0 = 9'h1dc;
	parameter VSSTRT = 9'h1e0;
	parameter HCENTER = 9'h1e2;
	parameter HBSTRT_VAL = 25;
	parameter HSSTRT_VAL = 37;
	parameter HSSTOP_VAL = 70;
	parameter HBSTOP_VAL = 102;
	parameter HCENTER_VAL = 264;
	parameter VSSTRT_VAL = 2;
	parameter VSSTOP_VAL = 5;
	parameter VBSTRT_VAL = 0;
	parameter HTOTAL_VAL = 8'd227 - 8'd1;
	parameter VTOTAL_PAL_VAL = 11'd312 - 11'd1;
	parameter VTOTAL_NTSC_VAL = 11'd262 - 11'd1;
	parameter VBSTOP_PAL_VAL = 9'd25;
	parameter VBSTOP_NTSC_VAL = 9'd20;
	reg long_frame;
	reg long_line;
	always @(*)
		if ((reg_address_in[8:1] == VPOSR[8:1]) || (reg_address_in[8:1] == VPOSW[8:1]))
			data_out[15:0] = {long_frame, 1'b0, ecs, ntsc, 2'b00, {2 {aga}}, long_line, 4'b0000, vpos[10:8]};
		else if ((reg_address_in[8:1] == VHPOSR[8:1]) || (reg_address_in[8:1] == VHPOSW[8:1]))
			data_out[15:0] = {vpos[7:0], hpos[8:1]};
		else
			data_out[15:0] = 0;
	reg [15:0] beamcon0_reg;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				beamcon0_reg <= {10'b0000000000, ~ntsc, 5'b00000};
			else if ((reg_address_in[8:1] == BEAMCON0[8:1]) && ecs)
				beamcon0_reg <= data_in[15:0];
		end
	wire harddis = beamcon0_reg[14];
	wire varvben = beamcon0_reg[12];
	wire loldis = beamcon0_reg[11];
	wire varvsyen = beamcon0_reg[9];
	wire varhsyen = beamcon0_reg[8];
	wire varbeamen = beamcon0_reg[7];
	reg ersy;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				ersy <= 1'b0;
			else if (reg_address_in[8:1] == BPLCON0[8:1])
				ersy <= data_in[1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				lace <= 1'b0;
			else if (reg_address_in[8:1] == BPLCON0[8:1])
				lace <= data_in[2];
		end
	reg pal;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				pal <= ~ntsc;
			else if ((reg_address_in[8:1] == BEAMCON0[8:1]) && ecs)
				pal <= data_in[5];
		end
	reg [8:0] htotal_reg;
	reg [8:0] hsstrt_reg;
	reg [8:0] hsstop_reg;
	reg [8:0] hcenter_reg;
	reg [8:0] hbstrt_reg;
	reg [8:0] hbstop_reg;
	reg [10:0] vtotal_reg;
	reg [10:0] vsstrt_reg;
	reg [10:0] vsstop_reg;
	reg [10:0] vbstop_reg;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				htotal_reg <= HTOTAL_VAL << 1;
				hsstrt_reg <= HSSTRT_VAL[8:0];
				hsstop_reg <= HSSTOP_VAL[8:0];
				hcenter_reg <= HCENTER_VAL[8:0];
				hbstrt_reg <= HBSTRT_VAL[8:0];
				hbstop_reg <= HBSTOP_VAL[8:0];
				vtotal_reg <= (pal ? VTOTAL_PAL_VAL : VTOTAL_NTSC_VAL);
				vsstrt_reg <= VSSTRT_VAL[10:0];
				vsstop_reg <= VSSTOP_VAL[10:0];
				vbstop_reg <= (pal ? VBSTOP_PAL_VAL : VBSTOP_NTSC_VAL);
			end
			else
				case (reg_address_in[8:1])
					HTOTAL[8:1]: htotal_reg <= {data_in[7:0], 1'b0};
					HSSTRT[8:1]: hsstrt_reg <= {data_in[7:0], 1'b0};
					HSSTOP[8:1]: hsstop_reg <= {data_in[7:0], 1'b0};
					HCENTER[8:1]: hcenter_reg <= {data_in[7:0], 1'b0};
					HBSTRT[8:1]: hbstrt_reg <= {data_in[7:0], 1'b0};
					HBSTOP[8:1]: hbstop_reg <= {data_in[7:0], 1'b0};
					VTOTAL[8:1]: vtotal_reg <= {data_in[10:0]};
					VSSTRT[8:1]: vsstrt_reg <= {data_in[10:0]};
					VSSTOP[8:1]: vsstop_reg <= {data_in[10:0]};
					VBSTOP[8:1]: vbstop_reg <= {data_in[10:0]};
				endcase
		end
	wire [8:0] htotal = (varbeamen ? htotal_reg : HTOTAL_VAL << 1);
	wire [8:0] hsstrt = (varhsyen && varbeamen ? hsstrt_reg : HSSTRT_VAL[8:0]);
	wire [8:0] hsstop = (varhsyen && varbeamen ? hsstop_reg : HSSTOP_VAL[8:0]);
	wire [8:0] hcenter = (varhsyen && varbeamen ? hcenter_reg : HCENTER_VAL[8:0]);
	wire [8:0] hbstrt = (varbeamen ? hbstrt_reg : HBSTRT_VAL[8:0]);
	wire [8:0] hbstop = (varbeamen ? hbstop_reg : HBSTOP_VAL[8:0]);
	wire [10:0] vtotal = (varbeamen ? vtotal_reg : (pal ? VTOTAL_PAL_VAL : VTOTAL_NTSC_VAL));
	wire [10:0] vsstrt = (varvsyen && varbeamen ? vsstrt_reg : VSSTRT_VAL[10:0]);
	wire [10:0] vsstop = (varvsyen && varbeamen ? vsstop_reg : VSSTOP_VAL[10:0]);
	wire [10:0] vbstop = (varvben && varbeamen ? vbstop_reg : (pal ? VBSTOP_PAL_VAL : VBSTOP_NTSC_VAL));
	assign htotal_out = htotal;
	assign harddis_out = (harddis || varbeamen) || varvben;
	assign varbeamen_out = varbeamen;
	wire [7:0] hend = htotal[8:1] + {7'd0, long_line};
	reg end_of_line;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[8:0] == {hend, 1'b0})
				end_of_line <= 1'b1;
			else
				end_of_line <= 1'b0;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == VHPOSW[8:1])
				hpos[8:1] <= data_in[7:0];
			else if (end_of_line)
				hpos[8:1] <= 0;
			else if (cck && (~ersy || |hpos[8:1]))
				hpos[8:1] <= hpos[8:1] + 1'b1;
		end
	always @(cck) hpos[0] = cck;
	always @(posedge clk)
		if (clk7_en) begin
			if (end_of_line) begin
				if (pal || (loldis && varbeamen))
					long_line <= 1'b0;
				else if (!(loldis && varbeamen))
					long_line <= ~long_line;
			end
		end
	reg vpos_inc;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos == 2)
				vpos_inc <= 1'b1;
			else
				vpos_inc <= 1'b0;
		end
	assign eol = vpos_inc;
	wire last_line;
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == VPOSW[8:1])
				vpos[10:8] <= data_in[2:0];
			else if (reg_address_in[8:1] == VHPOSW[8:1])
				vpos[7:0] <= data_in[15:8];
			else if (vpos_inc) begin
				if (last_line)
					vpos <= 0;
				else
					vpos <= vpos + 1'b1;
			end
		end
	wire end_of_frame;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				long_frame <= 1'b1;
			else if (reg_address_in[8:1] == VPOSW[8:1])
				long_frame <= data_in[15];
			else if (end_of_frame && lace)
				long_frame <= ~long_frame;
		end
	wire vpos_equ_vtotal = vpos == vtotal;
	reg extra_line;
	always @(posedge clk)
		if (clk7_en) begin
			if (vpos_inc) begin
				if (long_frame && vpos_equ_vtotal)
					extra_line <= 1'b1;
				else
					extra_line <= 1'b0;
			end
		end
	assign last_line = (long_frame ? extra_line : vpos_equ_vtotal);
	assign field1 = ~long_frame;
	assign end_of_frame = vpos_inc & last_line;
	assign eof = end_of_frame;
	always @(posedge clk)
		if (clk7_en)
			vbl_int <= (hpos == 8) && (vpos == (a1k ? 1 : 0));
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos == hsstrt)
				_hsync <= 1'b0;
			else if (hpos == hsstop)
				_hsync <= 1'b1;
		end
	always @(posedge clk)
		if (clk7_en) begin
			if ((((vpos == (vsstrt + 1)) && (hpos == hsstrt)) && long_frame) || (((vpos == vsstrt) && (hpos == hcenter)) && !long_frame))
				_vsync <= 1'b0;
			else if ((((vpos == vsstop) && (hpos == hcenter)) && long_frame) || (((vpos == vsstop) && (hpos == hsstrt)) && !long_frame))
				_vsync <= 1'b1;
		end
	reg vser;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos == (hsstrt - (hsstop - hsstrt)))
				vser <= 1'b1;
			else if (hpos == hsstrt)
				vser <= 1'b0;
		end
	assign _csync = (_hsync & _vsync) | vser;
	reg vbl_r;
	always @(posedge clk) vbl_r <= vpos <= vbstop;
	assign vbl = vbl_r;
	reg vblend_r;
	always @(posedge clk) vblend_r <= vpos == vbstop;
	assign vblend = vblend_r;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos == hbstrt)
				hblank <= 1;
			else if (hpos == hbstop) begin
				vblank <= vbl;
				hblank <= 0;
			end
		end
endmodule
module agnus_bitplanedma (
	clk,
	clk7_en,
	reset,
	harddis,
	ddf,
	aga,
	ecs,
	a1k,
	sof,
	dmaena,
	vpos,
	hpos,
	hde,
	dma,
	reg_address_in,
	reg_address_out,
	data_in,
	address_out
);
	input wire clk;
	input wire clk7_en;
	input wire reset;
	input wire harddis;
	output wire ddf;
	input wire aga;
	input wire ecs;
	input wire a1k;
	input wire sof;
	input wire dmaena;
	input wire [10:0] vpos;
	input wire [8:0] hpos;
	output reg hde;
	output wire dma;
	input wire [8:1] reg_address_in;
	output reg [8:1] reg_address_out;
	input wire [15:0] data_in;
	output wire [20:1] address_out;
	localparam DIWSTRT_REG = 9'h08e;
	localparam DIWSTOP_REG = 9'h090;
	localparam DIWHIGH_REG = 9'h1e4;
	localparam BPLPTBASE_REG = 9'h0e0;
	localparam DDFSTRT_REG = 9'h092;
	localparam DDFSTOP_REG = 9'h094;
	localparam BPL1MOD_REG = 9'h108;
	localparam BPL2MOD_REG = 9'h10a;
	localparam BPLCON0_REG = 9'h100;
	localparam FMODE_REG = 9'h1fc;
	reg [8:2] ddfstrt;
	reg [8:2] ddfstop;
	wire [8:2] ddfdiff;
	wire [8:2] ddfdiff_masked;
	reg [15:1] bpl1mod;
	reg [15:1] bpl2mod;
	reg [15:1] bpl1mod_bscan;
	reg [15:1] bpl2mod_bscan;
	reg [5:0] bplcon0;
	reg [5:0] bplcon0_delayed;
	reg [5:0] bplcon0_delay [1:0];
	reg [15:0] fmode;
	wire hires;
	wire shres;
	wire [3:0] bpu;
	reg [20:1] newpt;
	reg [20:16] bplpth [7:0];
	reg [15:1] bplptl [7:0];
	reg [4:0] plane;
	wire mod;
	reg hardena;
	reg softena;
	reg ddfena;
	reg ddfena_0;
	reg [4:0] ddfseq;
	reg ddfrun;
	reg ddfend;
	reg [1:0] dmaena_delayed;
	reg [10:0] vdiwstrt;
	reg [10:0] vdiwstop;
	reg vdiwena;
	reg [8:0] hdiwstrt;
	reg [8:0] hdiwstop;
	wire [2:0] bplptr_sel;
	wire [20:16] bplpth_in;
	wire [15:1] bplptl_in;
	wire ddfstrt_sel;
	wire bp_fmode0;
	wire bp_fmode12;
	wire bp_fmode3;
	reg soft_start;
	reg soft_stop;
	reg hard_start;
	reg hard_stop;
	wire ddfseq_match;
	always @(posedge clk) begin : sv2v_autoblock_1
		reg [8:0] best_hdiwstrt;
		reg [8:0] cur_hdiwstrt;
		reg [8:0] prev_hdiwstrt;
		reg [8:0] best_hdiwstop;
		reg [8:0] cur_hdiwstop;
		reg [8:0] prev_hdiwstop;
		reg [10:0] d_hde;
		if (clk7_en) begin
			if (!hpos) begin
				if ((hdiwstrt < hdiwstop) && ((best_hdiwstop - best_hdiwstrt) < (hdiwstop - hdiwstrt))) begin
					best_hdiwstrt <= hdiwstrt;
					best_hdiwstop <= hdiwstop;
				end
				if (!vpos) begin
					if (best_hdiwstrt || best_hdiwstop) begin
						cur_hdiwstrt <= best_hdiwstrt;
						cur_hdiwstop <= best_hdiwstop;
					end
					best_hdiwstrt <= 0;
					best_hdiwstop <= 0;
				end
			end
			if (hpos == cur_hdiwstrt)
				d_hde[0] <= 1;
			if ((hpos == cur_hdiwstop) || !hpos)
				d_hde[0] <= 0;
			d_hde[10:1] <= d_hde[9:0];
			hde <= d_hde[10];
		end
	end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTRT_REG[8:1]) begin
				vdiwstrt <= {3'b000, data_in[15:8]};
				hdiwstrt <= {1'b0, data_in[7:0]};
			end
			else if ((reg_address_in[8:1] == DIWHIGH_REG[8:1]) && ecs) begin
				vdiwstrt[10:8] <= data_in[2:0];
				hdiwstrt[8] <= data_in[5];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DIWSTOP_REG[8:1]) begin
				vdiwstop <= {2'b00, ~data_in[15], data_in[15:8]};
				hdiwstop <= {1'b1, data_in[7:0]};
			end
			else if ((reg_address_in[8:1] == DIWHIGH_REG[8:1]) && ecs) begin
				vdiwstop[10:8] <= data_in[10:8];
				hdiwstop[8] <= data_in[13];
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (((sof && ~a1k) || ((vpos[10:0] == 0) && a1k)) || (vpos[10:0] == vdiwstop[10:0]))
				vdiwena <= #(1) 1'b0;
			else if (vpos[10:0] == vdiwstrt[10:0])
				vdiwena <= #(1) 1'b1;
		end
	assign bplptr_sel = (dma ? plane[2:0] : reg_address_in[4:2]);
	assign bplpth_in = (dma ? newpt[20:16] : data_in[4:0]);
	always @(posedge clk)
		if (clk7_en) begin
			if (dma || ((reg_address_in[8:5] == BPLPTBASE_REG[8:5]) && !reg_address_in[1]))
				bplpth[bplptr_sel] <= #(1) bplpth_in;
		end
	assign address_out[20:16] = bplpth[plane[2:0]];
	assign bplptl_in = (dma ? newpt[15:1] : data_in[15:1]);
	always @(posedge clk)
		if (clk7_en) begin
			if (dma || ((reg_address_in[8:5] == BPLPTBASE_REG[8:5]) && reg_address_in[1]))
				bplptl[bplptr_sel] <= #(1) bplptl_in;
		end
	assign address_out[15:1] = bplptl[plane[2:0]];
	assign ddfstrt_sel = (reg_address_in[8:1] == DDFSTRT_REG[8:1] ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en) begin
			if (ddfstrt_sel)
				ddfstrt[8:2] <= #(1) data_in[7:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == DDFSTOP_REG[8:1])
				ddfstop[8:2] <= #(1) data_in[7:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL1MOD_REG[8:1])
				bpl1mod[15:1] <= #(1) data_in[15:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reg_address_in[8:1] == BPL2MOD_REG[8:1])
				bpl2mod[15:1] <= #(1) data_in[15:1];
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				bplcon0 <= #(1) 6'b000000;
			else if (reg_address_in[8:1] == BPLCON0_REG[8:1])
				bplcon0 <= #(1) {data_in[6], data_in[15], aga & data_in[4], data_in[14:12]};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				bplcon0_delay[0] <= #(1) bplcon0;
				bplcon0_delay[1] <= #(1) bplcon0_delay[0];
				bplcon0_delayed <= #(1) bplcon0_delay[1];
			end
		end
	assign shres = ecs & bplcon0_delayed[5];
	assign hires = bplcon0_delayed[4];
	assign bpu = (aga ? bplcon0_delayed[3:0] : {1'b0, (&bplcon0_delayed[2:0] ? 3'd4 : bplcon0_delayed[2:0])});
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				fmode <= #(1) 16'h0000;
			else if (aga && (reg_address_in[8:1] == FMODE_REG[8:1]))
				fmode <= #(1) data_in;
		end
	assign bp_fmode0 = fmode[1:0] == 2'b00;
	assign bp_fmode12 = (fmode[1:0] == 2'b01) || (fmode[1:0] == 2'b10);
	assign bp_fmode3 = fmode[1:0] == 2'b11;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[1:0] == 2'b11)
				dmaena_delayed[1:0] <= #(1) {dmaena_delayed[0], dmaena};
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (hpos[8:1] == {ddfstrt[8:3], ddfstrt[2] & ecs, 1'b0})
					soft_start <= #(1) 1'b1;
				else
					soft_start <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (hpos[8:1] == {ddfstop[8:3], ddfstop[2] & ecs, 1'b0})
					soft_stop <= #(1) 1'b1;
				else
					soft_stop <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (hpos[8:1] == 8'h18)
					hard_start <= #(1) 1'b1;
				else
					hard_start <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (hpos[8:1] == 8'hd8)
					hard_stop <= #(1) 1'b1;
				else
					hard_stop <= #(1) 1'b0;
			end
		end
	reg softena_off;
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if ((soft_start && (ecs || (vdiwena && dmaena))) && !ddfstrt_sel)
					softena <= #(1) 1'b1;
				else if (softena_off || (soft_stop || (!ecs && hard_stop)))
					softena <= #(1) 1'b0;
				softena_off <= (soft_stop && soft_start) && ecs;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (hard_start)
					hardena <= #(1) 1'b1;
				else if (hard_stop)
					hardena <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				ddfena_0 <= #(1) (hardena || harddis) && softena;
				ddfena <= #(1) ddfena_0;
			end
		end
	assign ddf = (ecs ? ddfrun : ddfena);
	assign ddfseq_match = ((((!hires && !shres) && bp_fmode3) && (ddfseq[4:0] == 5'd7)) || ((((!shres && !hires) && bp_fmode12) || (hires && bp_fmode3)) && (ddfseq[3:0] == 4'd7))) || ((!((!hires && !shres) && bp_fmode3) && !(((!shres && !hires) && bp_fmode12) || (hires && bp_fmode3))) && (ddfseq[2:0] == 3'd7));
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (((ddfena && vdiwena) && !hpos[1]) && dmaena_delayed[0])
					ddfrun <= #(1) 1'b1;
				else if ((ddfend || !vdiwena) && ddfseq_match)
					ddfrun <= #(1) 1'b0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if (hpos[0]) begin
				if (ddfrun)
					ddfseq <= #(1) ddfseq + 5'd1;
				else
					ddfseq <= #(1) 5'd0;
			end
		end
	always @(posedge clk)
		if (clk7_en) begin
			if ((hpos[0] && ddfseq_match) && ddfend)
				ddfend <= #(1) 1'b0;
			else if ((hpos[0] && (ddfseq[2:0] == 7)) && !ddfena)
				ddfend <= #(1) 1'b1;
		end
	assign mod = (shres && bp_fmode0 ? (ddfend & ddfseq[2]) & ddfseq[1] : ((hires && bp_fmode0) || (shres && bp_fmode12) ? ddfend & ddfseq[2] : ddfend));
	always @(*)
		if (shres && bp_fmode0)
			plane = {4'b0000, ~ddfseq[0]};
		else if ((hires && bp_fmode0) || (shres && bp_fmode12))
			plane = {3'b000, ~ddfseq[0], ~ddfseq[1]};
		else if ((((!shres && !hires) && bp_fmode0) || (hires && bp_fmode12)) || (shres && bp_fmode3))
			plane = {2'b00, ~ddfseq[0], ~ddfseq[1], ~ddfseq[2]};
		else if (((!shres && !hires) && bp_fmode12) || (hires && bp_fmode3))
			plane = {1'b0, ddfseq[3], ~ddfseq[0], ~ddfseq[1], ~ddfseq[2]};
		else
			plane = {ddfseq[4], ddfseq[3], ~ddfseq[0], ~ddfseq[1], ~ddfseq[2]};
	reg plane_lt_r;
	always @(posedge clk) plane_lt_r <= plane[4:0] < {1'b0, bpu[3:0]};
	assign dma = (((ddfrun && dmaena_delayed[1]) && hpos[0]) && plane_lt_r ? 1'b1 : 1'b0);
	always @(posedge clk)
		if (clk7_en && hpos[0]) begin
			bpl1mod_bscan <= (fmode[14] ? (vdiwstrt[0] ^ vpos[0] ? bpl2mod : bpl1mod) : bpl1mod);
			bpl2mod_bscan <= (fmode[14] ? (vdiwstrt[0] ^ vpos[0] ? bpl2mod : bpl1mod) : bpl2mod);
		end
	always @(*)
		if (mod) begin
			if (plane[0])
				newpt[20:1] = (address_out[20:1] + {{5 {bpl2mod_bscan[15]}}, bpl2mod_bscan[15:1]}) + (fmode[1:0] == 2'b11 ? 3'd4 : (fmode[1:0] == 2'b00 ? 3'd1 : 3'd2));
			else
				newpt[20:1] = (address_out[20:1] + {{5 {bpl1mod_bscan[15]}}, bpl1mod_bscan[15:1]}) + (fmode[1:0] == 2'b11 ? 3'd4 : (fmode[1:0] == 2'b00 ? 3'd1 : 3'd2));
		end
		else
			newpt[20:1] = address_out[20:1] + (fmode[1:0] == 2'b11 ? 3'd4 : (fmode[1:0] == 2'b00 ? 3'd1 : 3'd2));
	always @(*)
		case (plane[2:0])
			3'b000: reg_address_out[8:1] = 8'h88;
			3'b001: reg_address_out[8:1] = 8'h89;
			3'b010: reg_address_out[8:1] = 8'h8a;
			3'b011: reg_address_out[8:1] = 8'h8b;
			3'b100: reg_address_out[8:1] = 8'h8c;
			3'b101: reg_address_out[8:1] = 8'h8d;
			3'b110: reg_address_out[8:1] = 8'h8e;
			3'b111: reg_address_out[8:1] = 8'h8f;
		endcase
endmodule
module minimig_sram_bridge (
	clk,
	c1,
	c3,
	bank,
	address_in,
	data_in,
	data_out,
	rd,
	hwr,
	lwr,
	_bhe,
	_ble,
	_we,
	_oe,
	address,
	data,
	ramdata_in
);
	input clk;
	input c1;
	input c3;
	input [7:0] bank;
	input [23:1] address_in;
	input [15:0] data_in;
	output wire [15:0] data_out;
	input rd;
	input hwr;
	input lwr;
	output wire _bhe;
	output wire _ble;
	output wire _we;
	output wire _oe;
	output wire [22:1] address;
	output wire [15:0] data;
	input [15:0] ramdata_in;
	wire enable = |bank[7:0];
	assign _we = (!hwr && !lwr) | !enable;
	assign _oe = !rd | !enable;
	assign _bhe = !hwr | !enable;
	assign _ble = !lwr | !enable;
	assign address[17:1] = address_in[17:1];
	assign address[22:18] = (bank[6] ? 5'b11111 : (bank[7] ? {4'b1111, address_in[18]} : (bank[5] ? {2'b00, bank[3] | bank[2], bank[3] | bank[1], address_in[18]} : (bank[4] ? {2'b11, address_in[20:18]} : address_in[22:18]))));
	reg rd_en_r = 1'b0;
	always @(posedge clk) rd_en_r <= enable && rd;
	assign data_out[15:0] = (rd_en_r ? ramdata_in[15:0] : 16'b0000000000000000);
	assign data[15:0] = data_in[15:0];
endmodule
module paula (
	clk,
	clk7_en,
	clk7n_en,
	cck,
	reset,
	reg_address_in,
	data_in,
	data_out,
	txd,
	rxd,
	uart_serper,
	uart_rbf,
	uart_hold,
	ntsc,
	sof,
	strhor,
	vblint,
	int2,
	int3,
	int6,
	_ipl,
	audio_dmal,
	audio_dmas,
	disk_dmal,
	disk_dmas,
	_step,
	direc,
	_sel,
	side,
	_motor,
	_track0,
	_change,
	_ready,
	_wprot,
	index,
	fdd_led,
	sdc_image_mounted,
	sdc_image_size,
	sdc_rd,
	sdc_wr,
	sdc_sector,
	sdc_busy,
	sdc_done,
	sdc_byte_in_strobe,
	sdc_byte_addr,
	sdc_byte_in_data,
	sdc_byte_out_data,
	ldata,
	rdata,
	ldata_okk,
	rdata_okk,
	floppy_drives,
	floppy_wrprot
);
	input clk;
	input clk7_en;
	input clk7n_en;
	input cck;
	input reset;
	input [8:1] reg_address_in;
	input [15:0] data_in;
	output wire [15:0] data_out;
	output wire txd;
	input rxd;
	output wire [14:0] uart_serper;
	output wire uart_rbf;
	input uart_hold;
	input ntsc;
	input sof;
	input strhor;
	input vblint;
	input int2;
	input int3;
	input int6;
	output wire [2:0] _ipl;
	output wire [3:0] audio_dmal;
	output wire [3:0] audio_dmas;
	output wire disk_dmal;
	output wire disk_dmas;
	input _step;
	input direc;
	input [3:0] _sel;
	input side;
	input _motor;
	output wire _track0;
	output wire _change;
	output wire _ready;
	output wire _wprot;
	output wire index;
	output wire fdd_led;
	input [3:0] sdc_image_mounted;
	input [31:0] sdc_image_size;
	output wire [3:0] sdc_rd;
	output wire [3:0] sdc_wr;
	output wire [31:0] sdc_sector;
	input sdc_busy;
	input sdc_done;
	input sdc_byte_in_strobe;
	input [8:0] sdc_byte_addr;
	input [7:0] sdc_byte_in_data;
	output wire [7:0] sdc_byte_out_data;
	output wire [14:0] ldata;
	output wire [14:0] rdata;
	output wire [8:0] ldata_okk;
	output wire [8:0] rdata_okk;
	input [1:0] floppy_drives;
	input floppy_wrprot;
	parameter DMACON = 9'h096;
	parameter ADKCON = 9'h09e;
	parameter ADKCONR = 9'h010;
	reg [4:0] dmacon;
	reg dmaen;
	reg [14:0] adkcon;
	wire [15:0] uartdata_out;
	wire [15:0] intdata_out;
	wire [15:0] diskdata_out;
	wire [15:0] adkconr;
	wire rbfmirror;
	wire rxint;
	wire txint;
	wire blckint;
	wire syncint;
	wire [3:0] audint;
	wire [3:0] audpen;
	wire [3:0] auden;
	wire dsken;
	assign data_out = ((uartdata_out | intdata_out) | diskdata_out) | adkconr;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				dmaen <= 0;
				dmacon <= 5'd0;
			end
			else if (reg_address_in[8:1] == DMACON[8:1]) begin
				if (data_in[15])
					{dmaen, dmacon[4:0]} <= {dmaen, dmacon[4:0]} | {data_in[9], data_in[4:0]};
				else
					{dmaen, dmacon[4:0]} <= {dmaen, dmacon[4:0]} & ~{data_in[9], data_in[4:0]};
			end
		end
	assign dsken = dmacon[4] & dmaen;
	assign auden[3] = dmacon[3] & dmaen;
	assign auden[2] = dmacon[2] & dmaen;
	assign auden[1] = dmacon[1] & dmaen;
	assign auden[0] = dmacon[0] & dmaen;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset)
				adkcon <= 15'd0;
			else if (reg_address_in[8:1] == ADKCON[8:1]) begin
				if (data_in[15])
					adkcon[14:0] <= adkcon[14:0] | data_in[14:0];
				else
					adkcon[14:0] <= adkcon[14:0] & ~data_in[14:0];
			end
		end
	assign adkconr[15:0] = (reg_address_in[8:1] == ADKCONR[8:1] ? {1'b0, adkcon[14:0]} : 16'h0000);
	paula_uart pu1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.rga_i(reg_address_in),
		.data_i(data_in),
		.data_o(uartdata_out),
		.uartbrk(adkcon[11]),
		.rbfmirror(rbfmirror),
		.rxint(rxint),
		.txint(txint),
		.rxd(rxd),
		.txd(txd),
		.serper_o(uart_serper),
		.tx_hold(uart_hold),
		.rx_full(uart_rbf)
	);
	paula_intcontroller pi1(
		.clk(clk),
		.clk7_en(clk7_en),
		.reset(reset),
		.reg_address_in(reg_address_in),
		.data_in(data_in),
		.data_out(intdata_out),
		.rxint(rxint),
		.txint(txint),
		.vblint(vblint),
		.int2(int2),
		.int3(int3),
		.int6(int6),
		.blckint(blckint),
		.syncint(syncint),
		.audint(audint),
		.audpen(audpen),
		.rbfmirror(rbfmirror),
		._ipl(_ipl)
	);
	paula_floppy pf1(
		.clk(clk),
		.clk7_en(clk7_en),
		.clk7n_en(clk7n_en),
		.reset(reset),
		.ntsc(ntsc),
		.sof(sof),
		.enable(dsken),
		.reg_address_in(reg_address_in),
		.data_in(data_in),
		.data_out(diskdata_out),
		.dmal(disk_dmal),
		.dmas(disk_dmas),
		._step(_step),
		.direc(direc),
		._sel(_sel),
		.side(side),
		._motor(_motor),
		._track0(_track0),
		._change(_change),
		._ready(_ready),
		._wprot(_wprot),
		.index(index),
		.blckint(blckint),
		.syncint(syncint),
		.wordsync(adkcon[10]),
		.fdd_led(fdd_led),
		.floppy_drives(floppy_drives),
		.floppy_wrprot(floppy_wrprot),
		.sdc_image_mounted(sdc_image_mounted),
		.sdc_image_size(sdc_image_size),
		.sdc_rd(sdc_rd),
		.sdc_wr(sdc_wr),
		.sdc_sector(sdc_sector),
		.sdc_busy(sdc_busy),
		.sdc_done(sdc_done),
		.sdc_byte_in_strobe(sdc_byte_in_strobe),
		.sdc_byte_addr(sdc_byte_addr),
		.sdc_byte_in_data(sdc_byte_in_data),
		.sdc_byte_out_data(sdc_byte_out_data)
	);
	paula_audio ad1(
		.clk(clk),
		.clk7_en(clk7_en),
		.cck(cck),
		.rst(reset),
		.strhor(strhor),
		.reg_address_in(reg_address_in),
		.data_in(data_in),
		.dmaena(auden[3:0]),
		.audint(audint[3:0]),
		.audpen(audpen),
		.dmal(audio_dmal),
		.dmas(audio_dmas),
		.ldata(ldata),
		.rdata(rdata),
		.ldata_okk(ldata_okk),
		.rdata_okk(rdata_okk)
	);
endmodule
module paula_uart (
	clk,
	clk7_en,
	reset,
	rga_i,
	data_i,
	data_o,
	uartbrk,
	rbfmirror,
	txint,
	rxint,
	txd,
	rxd,
	serper_o,
	tx_hold,
	rx_full
);
	input wire clk;
	input wire clk7_en;
	input wire reset;
	input wire [7:0] rga_i;
	input wire [15:0] data_i;
	output wire [15:0] data_o;
	input wire uartbrk;
	input wire rbfmirror;
	output wire txint;
	output wire rxint;
	output wire txd;
	input wire rxd;
	output wire [14:0] serper_o;
	input wire tx_hold;
	output wire rx_full;
	localparam REG_SERDAT = 9'h030;
	localparam REG_SERDATR = 9'h018;
	localparam REG_SERPER = 9'h032;
	localparam LONG_BIT = 15;
	localparam OVRUN_BIT = 4;
	localparam RBF_BIT = 3;
	localparam TBE_BIT = 2;
	localparam TSRE_BIT = 1;
	localparam RXD_BIT = 0;
	reg [1:0] rxd_sync = 2'b11;
	wire rxds;
	always @(posedge clk)
		if (clk7_en)
			rxd_sync <= #(1) {rxd_sync[0], rxd};
	assign rxds = rxd_sync[1];
	reg [15:0] serper = 16'h0000;
	always @(posedge clk)
		if (clk7_en) begin
			if (rga_i == REG_SERPER[8:1])
				serper <= #(1) data_i;
		end
	reg [15:0] serdat = 16'h0000;
	always @(posedge clk)
		if (clk7_en) begin
			if (rga_i == REG_SERDAT[8:1])
				serdat <= #(1) data_i;
		end
	localparam [1:0] TX_IDLE = 2'd0;
	localparam [1:0] TX_SHIFT = 2'd2;
	reg [1:0] tx_state;
	reg [15:0] tx_cnt;
	reg [15:0] tx_shift;
	reg tx_txd;
	reg tx_irq;
	reg tx_tbe;
	reg tx_tsre;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				tx_state <= #(1) TX_IDLE;
				tx_txd <= #(1) 1'b1;
				tx_irq <= #(1) 1'b0;
				tx_tbe <= #(1) 1'b1;
				tx_tsre <= #(1) 1'b1;
			end
			else begin
				case (tx_state)
					TX_IDLE: begin
						tx_txd <= #(1) 1'b1;
						if (!tx_tbe && !tx_hold) begin
							tx_irq <= #(1) 1'b1;
							tx_txd <= #(1) 1'b0;
							tx_tsre <= #(1) 1'b0;
							tx_shift <= #(1) serdat;
							tx_cnt <= #(1) {serper[14:0], 1'b1};
							tx_state <= #(1) TX_SHIFT;
						end
					end
					TX_SHIFT: begin
						tx_irq <= #(1) 1'b0;
						if (tx_cnt == 16'd0) begin
							if (tx_shift == 16'd0) begin
								if (tx_tbe)
									tx_tsre <= #(1) 1'b1;
								tx_state <= #(1) TX_IDLE;
							end
							else begin
								tx_cnt <= #(1) {serper[14:0], 1'b1};
								tx_shift <= #(1) {1'b0, tx_shift[15:1]};
								tx_txd <= #(1) tx_shift[0];
							end
						end
						else
							tx_cnt <= #(1) tx_cnt - 16'd1;
					end
					default: tx_state <= #(1) TX_IDLE;
				endcase
				if (uartbrk)
					tx_txd <= #(1) 1'b0;
				tx_tbe <= #(1) (rga_i == REG_SERDAT[8:1] ? 1'b0 : ((tx_state == TX_IDLE) && !tx_hold ? 1'b1 : tx_tbe));
			end
		end
	localparam [1:0] RX_IDLE = 2'd0;
	localparam [1:0] RX_START = 2'd1;
	localparam [1:0] RX_SHIFT = 2'd2;
	reg [1:0] rx_state;
	reg [15:0] rx_cnt;
	reg [9:0] rx_shift;
	reg [9:0] rx_data;
	reg rx_rbf;
	reg rx_rxd;
	reg rx_irq;
	reg rx_ovrun;
	reg [9:0] rx_hold;
	reg rx_held = 1'b0;
	wire rx_to_hold = rbfmirror && !rx_held;
	always @(posedge clk)
		if (clk7_en) begin
			if (reset) begin
				rx_state <= #(1) RX_IDLE;
				rx_rbf <= #(1) 1'b0;
				rx_rxd <= #(1) 1'b1;
				rx_irq <= #(1) 1'b0;
				rx_ovrun <= #(1) 1'b0;
				rx_held <= #(1) 1'b0;
			end
			else begin
				case (rx_state)
					RX_IDLE: begin
						rx_irq <= #(1) 1'b0;
						if (rx_rxd && !rxds) begin
							rx_shift <= #(1) {serper[LONG_BIT], {9 {1'b1}}};
							rx_cnt <= #(1) {1'b0, serper[14:0]};
							rx_state <= #(1) RX_START;
						end
					end
					RX_START: begin
						if (rx_cnt == 16'h0000) begin
							if (!rxds) begin
								rx_shift <= #(1) {rxds, rx_shift[9:1]};
								rx_cnt <= #(1) {serper[14:0], 1'b1};
								rx_state <= #(1) RX_SHIFT;
							end
							else
								rx_state <= #(1) RX_IDLE;
						end
						else
							rx_cnt <= #(1) rx_cnt - 16'd1;
						if (!rx_rxd && rxds)
							rx_state <= #(1) RX_IDLE;
					end
					RX_SHIFT:
						if (rx_cnt == 16'h0000) begin
							rx_shift <= #(1) {rxds, rx_shift[9:1]};
							rx_cnt <= #(1) {serper[14:0], 1'b1};
							if (rx_shift[0] == 1'b0) begin
								rx_irq <= #(1) 1'b1;
								if (rx_to_hold) begin
									rx_irq <= #(1) 1'b0;
									rx_held <= #(1) 1'b1;
									rx_hold[9] <= #(1) rxds;
									if (serper[LONG_BIT])
										rx_hold[8:0] <= #(1) rx_shift[9:1];
									else
										rx_hold[8:0] <= #(1) {rxds, rx_shift[9:2]};
								end
								else begin
									rx_data[9] <= #(1) rxds;
									if (serper[LONG_BIT])
										rx_data[8:0] <= #(1) rx_shift[9:1];
									else
										rx_data[8:0] <= #(1) {rxds, rx_shift[9:2]};
								end
								rx_state <= #(1) RX_IDLE;
							end
						end
						else
							rx_cnt <= #(1) rx_cnt - 16'd1;
					default: rx_state <= #(1) RX_IDLE;
				endcase
				rx_rxd <= #(1) rxds;
				rx_rbf <= #(1) rbfmirror;
				rx_ovrun <= #(1) (!rbfmirror && rx_rbf ? 1'b0 : (((rx_state == RX_SHIFT) && ~|rx_cnt) && !rx_shift[0] ? rbfmirror && !rx_to_hold : rx_ovrun));
				if ((!rbfmirror && rx_rbf) && rx_held) begin
					rx_data <= #(1) rx_hold;
					rx_held <= #(1) 1'b0;
					rx_irq <= #(1) 1'b1;
				end
			end
		end
	wire [4:0] serdatr;
	assign serdatr = {rx_ovrun, rx_rbf, tx_tbe, tx_tsre, rx_rxd};
	assign txint = tx_irq;
	assign rxint = rx_irq;
	assign txd = tx_txd;
	assign serper_o = serper[14:0];
	assign rx_full = rx_held;
	assign data_o = (rga_i == REG_SERDATR[8:1] ? {serdatr, 1'b0, rx_data} : 16'h0000);
endmodule
module uart_port (
	clk,
	clk7_en,
	reset,
	serper,
	rbf,
	txd,
	rxd,
	hold,
	serial_status,
	serial_tx_available,
	serial_tx_strobe,
	serial_tx_data,
	serial_rx_available,
	serial_rx_strobe,
	serial_rx_data
);
	input clk;
	input clk7_en;
	input reset;
	input [14:0] serper;
	input rbf;
	input txd;
	output wire rxd;
	output wire hold;
	output wire [31:0] serial_status;
	output wire [7:0] serial_tx_available;
	input serial_tx_strobe;
	output wire [7:0] serial_tx_data;
	output wire [7:0] serial_rx_available;
	input serial_rx_strobe;
	input [7:0] serial_rx_data;
	wire tx_full;
	reg tx_push;
	reg [7:0] tx_byte;
	assign hold = tx_full;
	assign serial_tx_available[7:4] = 4'h0;
	io_fifo tx_fifo(
		.reset(reset),
		.in_clk(clk),
		.in(tx_byte),
		.in_strobe(1'b0),
		.in_enable(tx_push),
		.out_clk(clk),
		.out(serial_tx_data),
		.out_strobe(serial_tx_strobe),
		.out_enable(1'b0),
		.space(),
		.used(serial_tx_available[3:0]),
		.empty(),
		.full(tx_full)
	);
	localparam [1:0] TX_IDLE = 2'd0;
	localparam [1:0] TX_START = 2'd1;
	localparam [1:0] TX_DATA = 2'd2;
	localparam [1:0] TX_STOP = 2'd3;
	reg [1:0] tx_state;
	reg [15:0] tx_cnt;
	reg [2:0] tx_bit;
	reg txd_d;
	always @(posedge clk) begin
		tx_push <= 1'b0;
		if (reset) begin
			tx_state <= TX_IDLE;
			txd_d <= 1'b1;
		end
		else if (clk7_en) begin
			txd_d <= txd;
			case (tx_state)
				TX_IDLE:
					if (txd_d && !txd) begin
						tx_cnt <= {1'b0, serper};
						tx_state <= TX_START;
					end
				TX_START:
					if (tx_cnt == 16'd0) begin
						if (!txd) begin
							tx_cnt <= {serper, 1'b1};
							tx_bit <= 3'd0;
							tx_state <= TX_DATA;
						end
						else
							tx_state <= TX_IDLE;
					end
					else
						tx_cnt <= tx_cnt - 16'd1;
				TX_DATA:
					if (tx_cnt == 16'd0) begin
						tx_byte <= {txd, tx_byte[7:1]};
						tx_cnt <= {serper, 1'b1};
						tx_bit <= tx_bit + 3'd1;
						if (tx_bit == 3'd7)
							tx_state <= TX_STOP;
					end
					else
						tx_cnt <= tx_cnt - 16'd1;
				TX_STOP:
					if (tx_cnt == 16'd0) begin
						if (txd && !tx_full)
							tx_push <= 1'b1;
						tx_state <= TX_IDLE;
					end
					else
						tx_cnt <= tx_cnt - 16'd1;
			endcase
		end
	end
	wire [7:0] rx_byte;
	wire rx_empty;
	reg rx_pop;
	assign serial_rx_available[7:4] = 4'h0;
	io_fifo rx_fifo(
		.reset(reset),
		.in_clk(clk),
		.in(serial_rx_data),
		.in_strobe(serial_rx_strobe),
		.in_enable(1'b0),
		.out_clk(clk),
		.out(rx_byte),
		.out_strobe(1'b0),
		.out_enable(rx_pop),
		.space(serial_rx_available[3:0]),
		.empty(rx_empty),
		.full()
	);
	reg [9:0] rx_sr = 10'h3ff;
	reg [3:0] rx_bits;
	reg [15:0] rx_cnt;
	reg rx_busy;
	assign rxd = rx_sr[0];
	wire rx_start = ((!rx_busy && !rx_empty) && (serper != 15'd0)) && !rbf;
	always @(posedge clk) begin
		rx_pop <= 1'b0;
		if (reset) begin
			rx_sr <= 10'h3ff;
			rx_busy <= 1'b0;
		end
		else if (clk7_en) begin
			if (rx_start) begin
				rx_sr <= {1'b1, rx_byte, 1'b0};
				rx_bits <= 4'd10;
				rx_cnt <= {serper, 1'b1};
				rx_busy <= 1'b1;
				rx_pop <= 1'b1;
			end
			else if (rx_busy) begin
				if (rx_cnt == 16'd0) begin
					rx_sr <= {1'b1, rx_sr[9:1]};
					rx_cnt <= {serper, 1'b1};
					rx_bits <= rx_bits - 4'd1;
					if (rx_bits == 4'd1)
						rx_busy <= 1'b0;
				end
				else
					rx_cnt <= rx_cnt - 16'd1;
			end
		end
	end
	wire [23:0] bitrate = 24'h800001;
	assign serial_status = {bitrate[7:0], bitrate[15:8], bitrate[23:16], 8'h80};
endmodule
module io_fifo (
	reset,
	in,
	in_clk,
	in_strobe,
	in_enable,
	out_clk,
	out,
	out_strobe,
	out_enable,
	space,
	empty,
	used,
	full
);
	parameter DATA_WIDTH = 8;
	parameter DEPTH = 4;
	input reset;
	input [DATA_WIDTH - 1:0] in;
	input in_clk;
	input in_strobe;
	input in_enable;
	input out_clk;
	output wire [DATA_WIDTH - 1:0] out;
	input out_strobe;
	input out_enable;
	output wire [DEPTH - 1:0] space;
	output wire empty;
	output wire [DEPTH - 1:0] used;
	output wire full;
	localparam FIFO_ADDR_BITS = DEPTH;
	localparam FIFO_DEPTH = 1 << FIFO_ADDR_BITS;
	reg [DATA_WIDTH - 1:0] fifo [FIFO_DEPTH - 1:0];
	reg [FIFO_ADDR_BITS - 1:0] writeP;
	reg [FIFO_ADDR_BITS - 1:0] readP;
	assign full = readP == (writeP + 1'd1);
	assign empty = readP == writeP;
	assign used = writeP - readP;
	assign space = (readP - writeP) - 1'd1;
	reg in_strobeD;
	reg in_strobeD2;
	reg out_strobeD;
	reg out_strobeD2;
	assign out = (!empty ? fifo[readP] : fifo[readP - 1'd1]);
	always @(posedge out_clk) begin
		out_strobeD <= out_strobe;
		out_strobeD2 <= out_strobeD;
		if (reset)
			readP <= 0;
		else if ((out_strobeD && !out_strobeD2) || out_enable)
			readP <= readP + 1'd1;
	end
	always @(posedge in_clk) begin
		in_strobeD <= in_strobe;
		in_strobeD2 <= in_strobeD;
		if (reset)
			writeP <= 0;
		else if ((in_strobeD && !in_strobeD2) || in_enable) begin
			fifo[writeP] <= in;
			writeP <= writeP + 1'd1;
		end
	end
endmodule