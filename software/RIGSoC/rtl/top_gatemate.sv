// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// This is the top level SystemVerilog file that connects the IO on the board to the Ibex Demo System.
//Wir lassen das alles erstmal auf 25MHz laufen, was auch die Clock von Olimex Board ist
module top_gatemate #(
  parameter SRAMInitFile = "firmware_mem_init.svh",
  parameter ClockFrequency = 10000000
) (
  input         IO_CLK,
  input         IO_RST_N,
  input  [ 1:0] SW,
  output [ 1:0] LED,
  input         UART_RX,
  output        UART_TX,
  input         JTAG_TCK,
  input         JTAG_TMS,
  input         JTAG_TDI,
  output        JTAG_TDO
);
  logic clk_sys, rst_sys_n;
  // The board has no reset button and IO_RST_N is not wired to a pad, so it
  // would float. Generate reset internally: hold low for 128 clocks after
  // configuration, then release and saturate.
  logic [7:0] por_cnt = 8'd0;
  always @(posedge clk_sys) begin
    if (!por_cnt[7]) por_cnt <= por_cnt + 8'd1;
  end
  assign rst_sys_n = por_cnt[7];
  wire _unused_rst = IO_RST_N;
  // Instantiating the Ibex Demo System.
  ibex_demo_system #(
    .ClockFrequency(ClockFrequency),
    .GpiWidth     ( 2            ), 
    .GpoWidth     ( 2            ),
    .BaudRate(115200), //magic 4098
    .SRAMInitFile ( SRAMInitFile )
  ) u_ibex_demo_system (
    .clk_sys_i (clk_sys),
    .rst_sys_ni(rst_sys_n),
    .gp_i      (SW), 
    .uart_rx_i (UART_RX),
    .gp_o     (LED), 
    .pwm_o    (),
    .uart_tx_o(UART_TX),

    .spi_rx_i  (1'b0),
    .spi_tx_o  (),
    .spi_sck_o (),
    .trst_ni   (1'b1),
    .tms_i     (JTAG_TMS),
    .tck_i     (JTAG_TCK),
    .td_i      (JTAG_TDI),
    .td_o      (JTAG_TDO)
  );

  //hier ein input-buffer für die clock
  // Clock input: drive clk_sys straight from the pad. The pad buffer is
  // configured by the CCF (Pin_in ... | SCHMITT_TRIGGER=true) and nextpnr
  // promotes this net to the global clock network.
  // NOTE: the previous hand-instantiated CC_IBUF forced V_IO("1.8") and
  // SCHMITT_TRIGGER(0), which is the wrong I/O standard for this board's
  // oscillator - the clock did not reach the fabric reliably.
  assign clk_sys = IO_CLK;

  // Generating the system clock and reset for the FPGA.
  //TODO:Primitve von GateMate einfügen für PLL (funktioniert noch nicht)
  /*
  clkgen_gatemate clkgen(
    .IO_CLK,
    .IO_RST_N,
    .clk_sys,
    .rst_sys_n
  );
*/
endmodule
