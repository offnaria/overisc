// SPDX-License-Identifier: Apache-2.0
// Core testbench (B4): the core with the simulation memory on its two AXI4 ports, the DECERR
// terminator on the MMIO port, and the host (ELF loading, tohost, cycle limit).
// The clock and reset come from the C++ harness (sim/harness/sim_run.h).
module tb_core
  import overisc_axi_pkg::*;
  import overisc_commit_pkg::*;
#(
    parameter longint RESET_VECTOR = 64'h8000_0000
) (
    input logic clk,
    input logic rst
);

  axi_req_t ibus_req, dbus_req;
  axi_resp_t ibus_resp, dbus_resp;
  axil_req_t io_req;
  axil_resp_t io_resp;
  // compared against Spike in B7
  /* verilator lint_off UNUSEDSIGNAL */
  commit_t [NRET-1:0] commit;
  /* verilator lint_on UNUSEDSIGNAL */

  overisc_core_top #(
      .RESET_VECTOR(RESET_VECTOR)
  ) u_core (
      .clk      (clk),
      .rst      (rst),
      .ibus_req (ibus_req),
      .ibus_resp(ibus_resp),
      .dbus_req (dbus_req),
      .dbus_resp(dbus_resp),
      .io_req   (io_req),
      .io_resp  (io_resp),
      .commit   (commit)
  );

  overisc_sim_axi_mem #(
      .NAME("ibus")
  ) u_imem (
      .clk (clk),
      .rst (rst),
      .req (ibus_req),
      .resp(ibus_resp)
  );

  overisc_sim_axi_mem #(
      .NAME("dbus")
  ) u_dmem (
      .clk (clk),
      .rst (rst),
      .req (dbus_req),
      .resp(dbus_resp)
  );

  overisc_sim_axil_err #(
      .NAME("io")
  ) u_io (
      .clk (clk),
      .rst (rst),
      .req (io_req),
      .resp(io_resp)
  );

`ifdef OVERISC_KONATA
  // Kanata log (+konata=<file>); needs the core's internal `trace` signal
  overisc_sim_konata u_konata (
      .clk  (clk),
      .rst  (rst),
      .trace(u_core.trace)
  );
`endif

  overisc_sim_host #(
      .RESET_VECTOR(RESET_VECTOR)
  ) u_host (
      .clk(clk),
      .rst(rst)
  );

endmodule
