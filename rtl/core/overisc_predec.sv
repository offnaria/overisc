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

  ifu_source_t sink_data;
  predec_source_t source_data;

  always_comb begin
    // setup

    // logic
    sink_data = ifu_source_t'(predec_sink.data);
    source_data = '{
        is_rvc: (sink_data.inst[1:0] != '1),
        inst: sink_data.inst,
        pc: sink_data.pc,
        untaken_pc: sink_data.untaken_pc,
        id: sink_data.id
    };

    // output

    // TODO
    predec_source.valid = predec_sink.valid;
    predec_source.data = source_data;
    predec_sink.ready = predec_source.ready;
  end

endmodule
