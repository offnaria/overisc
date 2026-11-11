// SPDX-License-Identifier: CERN-OHL-P-2.0
// Skid buffer module.

module overisc_skid_buffer #(
    parameter DATA_W = 32
) (
    input clk,
    input rst,

    overisc_pipe_if.sink   sink,
    overisc_pipe_if.source source,

    input kill  // Invalidate internal data.
);

  logic [DATA_W-1:0] source_data, source_data_w;
  logic source_valid, source_valid_w;

  logic [DATA_W-1:0] skid_data, skid_data_w;
  logic skid_valid, skid_valid_w;
  logic skid_ready;

  logic sink_ready, sink_ready_w;

  logic sink_hs;  // handshake success

  always_comb begin
    // setup
    source_data_w = source_data;
    source_valid_w = source_valid;
    skid_data_w = skid_data;
    skid_valid_w = skid_valid;
    sink_ready_w = sink_ready;

    // logic
    skid_ready = !source_valid || source.ready;
    sink_hs = sink_ready && sink.valid;  // sink_ready==1 should be checked for safety after reset.

    if (skid_ready) begin
      source_data_w  = (skid_valid) ? skid_data : sink.data;
      source_valid_w = skid_valid || sink_hs;
    end

    if (!skid_valid) begin
      skid_data_w  = sink.data;
      skid_valid_w = sink_hs && !skid_ready;
    end else begin
      // sink_ready==0
      skid_valid_w = !source.ready;
    end

    if (kill) begin
      source_valid_w = '0;
      skid_valid_w   = '0;
    end

    sink_ready_w = !skid_valid_w;

    // output
    source.data  = source_data;
    source.valid = source_valid;
    sink.ready   = sink_ready;

  end

  always_ff @(posedge clk) begin
    source_data <= source_data_w;
    source_valid <= source_valid_w;
    skid_data <= skid_data_w;
    skid_valid <= skid_valid_w;
    sink_ready <= sink_ready_w;

    if (rst) begin
      source_valid <= '0;
      skid_valid   <= '0;
      sink_ready   <= '0;
    end
  end

endmodule
