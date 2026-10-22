// Copyright 2020 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51

// Soft JTAG TAP for boards that connect external JTAG signals to regular
// FPGA I/O pins rather than a vendor-specific user scan-chain primitive.
module dmi_jtag_tap #(
  parameter int unsigned IrLength = 5,
  parameter logic [31:0] IdcodeValue = 32'h00000001
) (
  input  logic        tck_i,
  input  logic        tms_i,
  input  logic        trst_ni,
  input  logic        td_i,
  output logic        td_o,
  output logic        tdo_oe_o,
  input  logic        testmode_i,

  output logic        tck_o,
  output logic        dmi_clear_o,
  output logic        update_o,
  output logic        capture_o,
  output logic        shift_o,
  output logic        tdi_o,
  output logic        dtmcs_select_o,
  input  logic        dtmcs_tdo_i,

  output logic        dmi_select_o,
  input  logic        dmi_tdo_i
);

  typedef enum logic [3:0] {
    TestLogicReset,
    RunTestIdle,
    SelectDRScan,
    CaptureDR,
    ShiftDR,
    Exit1DR,
    PauseDR,
    Exit2DR,
    UpdateDR,
    SelectIRScan,
    CaptureIR,
    ShiftIR,
    Exit1IR,
    PauseIR,
    Exit2IR,
    UpdateIR
  } tap_state_e;

  localparam logic [IrLength-1:0] IdcodeInstruction = 5'h01;
  localparam logic [IrLength-1:0] DtmcsInstruction  = 5'h10;
  localparam logic [IrLength-1:0] DmiInstruction    = 5'h11;
  localparam logic [IrLength-1:0] BypassInstruction = {IrLength{1'b1}};
  localparam logic [IrLength-1:0] IrCaptureValue =
      {{(IrLength-2){1'b0}}, 2'b01};

  tap_state_e state_q, state_d;
  logic [IrLength-1:0] ir_q, ir_shift_q;
  logic [31:0] idcode_shift_q;
  logic bypass_q;
  logic tdo_data;

  always_comb begin
    state_d = state_q;
    unique case (state_q)
      TestLogicReset: state_d = tms_i ? TestLogicReset : RunTestIdle;
      RunTestIdle:    state_d = tms_i ? SelectDRScan : RunTestIdle;
      SelectDRScan:   state_d = tms_i ? SelectIRScan : CaptureDR;
      CaptureDR:      state_d = tms_i ? Exit1DR : ShiftDR;
      ShiftDR:        state_d = tms_i ? Exit1DR : ShiftDR;
      Exit1DR:        state_d = tms_i ? UpdateDR : PauseDR;
      PauseDR:        state_d = tms_i ? Exit2DR : PauseDR;
      Exit2DR:        state_d = tms_i ? UpdateDR : ShiftDR;
      UpdateDR:       state_d = tms_i ? SelectDRScan : RunTestIdle;
      SelectIRScan:   state_d = tms_i ? TestLogicReset : CaptureIR;
      CaptureIR:      state_d = tms_i ? Exit1IR : ShiftIR;
      ShiftIR:        state_d = tms_i ? Exit1IR : ShiftIR;
      Exit1IR:        state_d = tms_i ? UpdateIR : PauseIR;
      PauseIR:        state_d = tms_i ? Exit2IR : PauseIR;
      Exit2IR:        state_d = tms_i ? UpdateIR : ShiftIR;
      UpdateIR:       state_d = tms_i ? SelectDRScan : RunTestIdle;
      default:        state_d = TestLogicReset;
    endcase
  end

  always_ff @(posedge tck_i or negedge trst_ni) begin
    if (!trst_ni) begin
      state_q       <= TestLogicReset;
      ir_q          <= IdcodeInstruction;
      ir_shift_q    <= IrCaptureValue;
      idcode_shift_q <= '0;
      bypass_q      <= 1'b0;
    end else begin
      state_q <= state_d;

      if (state_q == TestLogicReset) begin
        ir_q <= IdcodeInstruction;
      end

      if (state_q == CaptureIR) begin
        ir_shift_q <= IrCaptureValue;
      end else if (state_q == ShiftIR) begin
        ir_shift_q <= {td_i, ir_shift_q[IrLength-1:1]};
      end else if (state_q == UpdateIR) begin
        ir_q <= ir_shift_q;
      end

      if (state_q == CaptureDR) begin
        if (ir_q == IdcodeInstruction) begin
          idcode_shift_q <= IdcodeValue;
        end else if (ir_q == BypassInstruction) begin
          bypass_q <= 1'b0;
        end else if (ir_q != DtmcsInstruction && ir_q != DmiInstruction) begin
          bypass_q <= 1'b0;
        end
      end else if (state_q == ShiftDR) begin
        if (ir_q == IdcodeInstruction) begin
          idcode_shift_q <= {td_i, idcode_shift_q[31:1]};
        end else if (ir_q == BypassInstruction ||
                     (ir_q != DtmcsInstruction && ir_q != DmiInstruction)) begin
          bypass_q <= td_i;
        end
      end
    end
  end

  always_comb begin
    tdo_data = 1'b0;
    if (state_q == ShiftIR) begin
      tdo_data = ir_shift_q[0];
    end else if (state_q == ShiftDR) begin
      if (ir_q == IdcodeInstruction) begin
        tdo_data = idcode_shift_q[0];
      end else if (ir_q == DtmcsInstruction) begin
        tdo_data = dtmcs_tdo_i;
      end else if (ir_q == DmiInstruction) begin
        tdo_data = dmi_tdo_i;
      end else begin
        tdo_data = bypass_q;
      end
    end
  end

  assign tck_o          = tck_i;
  assign td_o            = tdo_data;
  assign tdo_oe_o        = (state_q == ShiftIR) || (state_q == ShiftDR);
  assign tdi_o           = td_i;
  assign dmi_clear_o     = (state_q == TestLogicReset);
  assign update_o        = (state_q == UpdateDR);
  assign capture_o       = (state_q == CaptureDR);
  assign shift_o         = (state_q == ShiftDR);
  assign dtmcs_select_o  = (ir_q == DtmcsInstruction);
  assign dmi_select_o    = (ir_q == DmiInstruction);

  logic unused_testmode;
  assign unused_testmode = testmode_i;

endmodule