// SPDX-License-Identifier: CERN-OHL-P-2.0
// Top module of Overisc core.

/* verilator lint_off UNUSEDSIGNAL */
module overisc_core_top
  import overisc_cfg_pkg::*, overisc_axi_pkg::*, overisc_commit_pkg::*, overisc_pipe_pkg::*;
#(
    parameter logic [XLEN-1:0] RESET_VECTOR = '0
) (
    input clk,
    input rst,

    // Instruction bus
    output axi_req_t  ibus_req,
    input  axi_resp_t ibus_resp,

    // Data bus (cached)
    output axi_req_t  dbus_req,
    input  axi_resp_t dbus_resp,

    // IO bus (uncached)
    output axil_req_t  io_req,
    input  axil_resp_t io_resp,

    // Commit record
    output commit_t [NRET-1:0] commit
);

  always_comb begin
    // setup


    // logic


    // output
    dbus_req = '{default: '0};
    io_req   = '{default: '0};
    commit   = '{default: '0};
  end

  overisc_pipe_if #(.DATA_W($bits(pcgen_source_t))) pcgen_source ();
  overisc_pipe_if #(.DATA_W($bits(ifu_source_t))) ifu_source ();

  overisc_pcgen #(.RESET_VECTOR(RESET_VECTOR)) i_pcgen (.*);

  overisc_ifu #() i_ifu (
      .ifu_sink(pcgen_source),
      .*
  );

  // TODO: Remove this block once implementation completed.
  always_comb begin
    ifu_source.ready = 1'b1;
    if (0) begin
      $write("data=%d valid=%d ready=%d\n", ifu_source.data, ifu_source.valid, ifu_source.ready);
    end
  end

endmodule
