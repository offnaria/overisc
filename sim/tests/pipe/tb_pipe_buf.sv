// SPDX-License-Identifier: Apache-2.0
// Test top for the pipeline buffers: flattens the overisc_pipe_if ports so the C++ test can
// drive them. KIND selects the module under test.
//
// The fetch buffer carries a struct with an `id` field (needed for its trace output); the test
// puts the item number in `id` and its complement in `aux`, so a mix-up of fields shows as
// out_aux != ~out_data.
module tb_pipe_buf
  import overisc_trace_pkg::trace_id_t;
#(
    // 0: overisc_reg_slice, 1: overisc_skid_buffer, 2: overisc_fetch_buffer
    parameter int KIND    = 0,
    parameter int DEPTH_W = 2   // fetch buffer only
) (
    input  logic        clk,
    input  logic        rst,
    input  logic        kill,
    input  logic        in_valid,
    input  logic [31:0] in_data,
    output logic        in_ready,
    output logic        out_valid,
    output logic [31:0] out_data,
    output logic [31:0] out_aux,
    input  logic        out_ready,
    // fetch buffer only: what it reports as entering its output this cycle
    output logic        trace_valid,
    output logic [31:0] trace_id
);

  typedef struct packed {
    logic [31:0] aux;
    trace_id_t   id;
  } fb_item_t;

  localparam int DataW = (KIND == 2) ? $bits(fb_item_t) : 32;

  overisc_pipe_if #(.DATA_W(DataW)) sink ();
  overisc_pipe_if #(.DATA_W(DataW)) source ();

  assign sink.valid   = in_valid;
  assign in_ready     = sink.ready;
  assign out_valid    = source.valid;
  assign source.ready = out_ready;

  if (KIND == 2) begin : gen_fetch_buffer
    fb_item_t in_item, out_item;
    trace_id_t fb_trace_id;
    logic fb_trace_valid;
    assign in_item     = '{aux: ~in_data, id: in_data};
    assign sink.data   = in_item;
    assign out_item    = fb_item_t'(source.data);
    assign out_data    = out_item.id;
    assign out_aux     = out_item.aux;
    assign trace_valid = fb_trace_valid;
    assign trace_id    = fb_trace_id;
    overisc_fetch_buffer #(
        .DATA_T (fb_item_t),
        .DEPTH_W(DEPTH_W)
    ) u_dut (
        .clk           (clk),
        .rst           (rst),
        .sink          (sink),
        .source        (source),
        .fb_trace_id   (fb_trace_id),
        .fb_trace_valid(fb_trace_valid),
        .kill          (kill)
    );
  end else begin : gen_plain
    assign sink.data   = in_data;
    assign out_data    = source.data;
    assign out_aux     = ~source.data;
    assign trace_valid = 1'b0;
    assign trace_id    = '0;
    if (KIND == 0) begin : gen_reg_slice
      overisc_reg_slice #(
          .DATA_W(32)
      ) u_dut (
          .clk   (clk),
          .rst   (rst),
          .sink  (sink),
          .source(source),
          .kill  (kill)
      );
    end else begin : gen_skid_buffer
      overisc_skid_buffer #(
          .DATA_W(32)
      ) u_dut (
          .clk   (clk),
          .rst   (rst),
          .sink  (sink),
          .source(source),
          .kill  (kill)
      );
    end
  end

endmodule
