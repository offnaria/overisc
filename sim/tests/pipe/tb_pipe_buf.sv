// SPDX-License-Identifier: Apache-2.0
// Test top for the pipeline buffers in rtl/common: flattens the overisc_pipe_if ports so the
// C++ test can drive them. KIND selects the module under test.
module tb_pipe_buf #(
    parameter int KIND   = 0,  // 0: overisc_reg_slice, 1: overisc_skid_buffer
    parameter int DATA_W = 32
) (
    input  logic              clk,
    input  logic              rst,
    input  logic              in_valid,
    input  logic [DATA_W-1:0] in_data,
    output logic              in_ready,
    output logic              out_valid,
    output logic [DATA_W-1:0] out_data,
    input  logic              out_ready
);

  overisc_pipe_if #(.DATA_W(DATA_W)) sink ();
  overisc_pipe_if #(.DATA_W(DATA_W)) source ();

  assign sink.valid   = in_valid;
  assign sink.data    = in_data;
  assign in_ready     = sink.ready;
  assign out_valid    = source.valid;
  assign out_data     = source.data;
  assign source.ready = out_ready;

  if (KIND == 0) begin : gen_reg_slice
    overisc_reg_slice #(
        .DATA_W(DATA_W)
    ) u_dut (
        .clk   (clk),
        .rst   (rst),
        .sink  (sink),
        .source(source)
    );
  end else begin : gen_skid_buffer
    overisc_skid_buffer #(
        .DATA_W(DATA_W)
    ) u_dut (
        .clk   (clk),
        .rst   (rst),
        .sink  (sink),
        .source(source)
    );
  end

endmodule
