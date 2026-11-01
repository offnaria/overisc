// SPDX-License-Identifier: CERN-OHL-P-2.0
// Register slice module.

module overisc_reg_slice #(
    parameter DATA_W = 32
) (
    input clk,
    input rst,

    overisc_pipe_if.sink   sink,
    overisc_pipe_if.source source
);

  logic [DATA_W-1:0] source_data, source_data_w;
  logic source_valid, source_valid_w;
  logic sink_ready;

  always_comb begin
    // setup
    source_data_w = source_data;
    source_valid_w = source_valid;

    // logic
    // NOTE: sink_ready can be 1 during reset.
    //       Be careful to use this module at the top interface.
    sink_ready = !source_valid || source.ready;
    if (sink_ready) begin
      source_data_w  = sink.data;  // No need to check sink.valid here.
      source_valid_w = sink.valid;
    end

    // output
    source.data  = source_data;
    source.valid = source_valid;
    sink.ready   = sink_ready;
  end

  always_ff @(posedge clk) begin
    source_data  <= source_data_w;
    source_valid <= source_valid_w;

    if (rst) begin
      source_valid <= '0;
    end
  end

endmodule
