// SPDX-License-Identifier: CERN-OHL-P-2.0
// Fetch Buffer module of Overisc core.
//
// DEPTH_W: The bit width of the depth of the buffer. Array-based FIFO,
//          which depth is 2^DEPTH_W, with output register, is implemented.

module overisc_fetch_buffer #(
    parameter DATA_W  = 32,
    parameter DEPTH_W = 4
) (
    input clk,
    input rst,

    overisc_pipe_if.sink   sink,
    overisc_pipe_if.source source
);

  logic [DATA_W-1:0] source_data, source_data_w;
  logic source_valid, source_valid_w;

  // logic [DATA_W-1:0] skid_data, skid_data_w;
  // logic skid_valid, skid_valid_w;
  // logic skid_ready;


  logic sink_ready, sink_ready_w;

  logic sink_hs;

  // Memory array
  logic [DATA_W-1:0] fifo_mem[2**DEPTH_W];
  logic [DEPTH_W:0] fifo_wptr, fifo_wptr_w;  // MSB is used to check if FIFO is full.
  logic [DEPTH_W:0] fifo_rptr, fifo_rptr_w;
  logic fifo_used, fifo_used_w;
  logic fifo_full, fifo_full_w;
  logic fifo_ready;
  logic mem_write;


  always_comb begin
    // setup
    source_data_w = source_data;
    source_valid_w = source_valid;
    sink_ready_w = sink_ready;
    fifo_wptr_w = fifo_wptr;
    fifo_rptr_w = fifo_rptr;
    fifo_used_w = fifo_used;
    fifo_full_w = fifo_full;

    // logic
    fifo_ready = !source_valid || source.ready;
    sink_hs = sink_ready && sink.valid;

    if (fifo_ready) begin
      source_data_w = (fifo_used) ? fifo_mem[fifo_rptr[DEPTH_W-1:0]] : sink.data; // If memory is used, read the data.
      source_valid_w = fifo_used || sink_hs;
    end

    if (fifo_used && fifo_ready) begin  // Memory read.
      fifo_rptr_w = fifo_rptr + 'd1;
    end

    mem_write = '0;
    if (sink_hs && !fifo_ready) begin  // Memory write
      fifo_wptr_w = fifo_wptr + 'd1;
      mem_write   = 1'b1;
    end

    fifo_used_w = fifo_wptr_w != fifo_rptr_w;
    fifo_full_w = (fifo_wptr_w[DEPTH_W] ^ fifo_rptr_w[DEPTH_W]) && (fifo_wptr_w[DEPTH_W-1:0] == fifo_rptr_w[DEPTH_W-1:0]);
    sink_ready_w = !fifo_full_w;

    // output
    source.data = source_data;
    source.valid = source_valid;
    sink.ready = sink_ready;

  end

  always_ff @(posedge clk) begin
    source_data <= source_data_w;
    source_valid <= source_valid_w;
    sink_ready <= sink_ready_w;
    fifo_wptr <= fifo_wptr_w;
    fifo_rptr <= fifo_rptr_w;
    fifo_used <= fifo_used_w;
    fifo_full <= fifo_full_w;

    if (mem_write) begin
      fifo_mem[fifo_wptr[DEPTH_W-1:0]] <= sink.data;
    end

    if (rst) begin
      source_valid <= '0;
      sink_ready <= '0;
      fifo_wptr <= '0;
      fifo_rptr <= '0;
      fifo_used <= '0;
      fifo_full <= '0;
    end
  end

endmodule
