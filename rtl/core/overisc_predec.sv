// SPDX-License-Identifier: CERN-OHL-P-2.0
// Predecoder module of Overisc core.

module overisc_predec
  import overisc_cfg_pkg::*, overisc_pipe_pkg::*;
#(

) (
    // input clk,
    // input rst,

    overisc_pipe_if.sink   predec_sink,
    overisc_pipe_if.source predec_source
);

  always_comb begin
    // setup

    // logic

    // output

    // TODO
    predec_source.valid = predec_sink.valid;
    predec_source.data  = predec_source_t'(predec_sink.data);
    predec_sink.ready   = predec_source.ready;
  end

endmodule
