// SPDX-License-Identifier: Apache-2.0
// Writes a Kanata log (Konata viewer) of the core pipeline when +konata=<file> is given.
// The trace comes from the core's internal `trace` signal (see overisc_trace_pkg.sv); the
// conversion to the log format is in C++ (konata_tracer.cc).
module overisc_sim_konata
  import overisc_trace_pkg::*;
  import overisc_commit_pkg::NRET;
(
    input logic   clk,
    input logic   rst,
    input trace_t trace
);

  import "DPI-C" function int overisc_konata_open(input string path);
  import "DPI-C" function void overisc_konata_stage(
    input int stage,
    input bit valid,
    input int id
  );
  import "DPI-C" function void overisc_konata_front(
    input longint f0_pc,
    input int     f2_inst
  );
  import "DPI-C" function void overisc_konata_redirect(
    input bit valid,
    input int id
  );
  import "DPI-C" function void overisc_konata_retire(
    input bit valid,
    input int id
  );
  import "DPI-C" function void overisc_konata_cycle(input longint cycle);
  import "DPI-C" function void overisc_konata_close();

  bit enabled;
  longint cycle;

  initial begin
    string path;
    enabled = 1'b0;
    cycle   = 0;
    if ($value$plusargs("konata=%s", path)) begin
      if (overisc_konata_open(path) == 0) begin
        enabled = 1'b1;
        $display("[konata] writing %s", path);
      end else begin
        $display("[konata] cannot open %s", path);
      end
    end
  end

  always @(posedge clk) begin
    if (enabled && !rst) begin
      for (int k = 0; k < TRACE_NSTAGE; k++) begin
        overisc_konata_stage(k, trace.stage[k].valid, int'(trace.stage[k].id));
      end
      overisc_konata_front(longint'(trace.f0_pc), int'(trace.f2_inst));
      overisc_konata_redirect(trace.redirect, int'(trace.redirect_id));
      for (int k = 0; k < NRET; k++) begin
        overisc_konata_retire(trace.retire[k].valid, int'(trace.retire[k].id));
      end
      overisc_konata_cycle(cycle);
      cycle <= cycle + 1;
    end
  end

  final begin
    if (enabled) overisc_konata_close();
  end

endmodule
