// SPDX-License-Identifier: CERN-OHL-P-2.0
// PC generator module of Overisc core.
module overisc_pcgen
  import overisc_cfg_pkg::*, overisc_pipe_pkg::*, overisc_trace_pkg::trace_id_t;
#(
    parameter logic [XLEN-1:0] RESET_VECTOR = '0
) (
    input clk,
    input rst,

    overisc_pipe_if.source pcgen_source

    // TODO: Add sink interface to accept jumps
);

  logic [XLEN-1:0] pc, pc_w;
  trace_id_t id, id_w;

  pcgen_source_t source_data;

  always_comb begin
    // setup
    pc_w = pc;
    id_w = id;

    // logic
    if (pcgen_source.ready) begin
      pc_w = pc + 'd4;
      id_w = id + 'd1;
    end

    source_data = '{
        pc: pc,
        untaken_pc: '0,  // TODO
        id: id
    };

    // output
    pcgen_source.data = source_data;
    pcgen_source.valid = 1'b1;
  end

  always_ff @(posedge clk) begin
    pc <= pc_w;
    id <= id_w;

    if (rst) begin
      pc <= RESET_VECTOR;
    end
  end

endmodule
