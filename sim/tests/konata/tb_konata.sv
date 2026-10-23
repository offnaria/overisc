// SPDX-License-Identifier: Apache-2.0
// Glue test top for overisc_sim_konata: the trace comes straight from the C++ test.
module tb_konata
  import overisc_trace_pkg::*;
(
    input logic   clk,
    input logic   rst,
    input trace_t trace_in  // not `trace`: clashes with the Verilated model's trace()
);

  overisc_sim_konata u_konata (
      .clk  (clk),
      .rst  (rst),
      .trace(trace_in)
  );

endmodule
