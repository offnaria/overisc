// SPDX-License-Identifier: CERN-OHL-P-2.0
// Executor module of Overisc core.

module overisc_exec
  import overisc_cfg_pkg::XLEN, overisc_pipe_pkg::*;
#(

) (
    // input clk,
    // input rst,

    overisc_pipe_if.sink   ex_sink,
    overisc_pipe_if.source ex_source,

    output bru_valid,
    output [XLEN-1:0] bru_tpc
);

  dec_source_t sink_data;
  exec_source_t source_data;
  logic [XLEN-1:0] alu_rslt;
  logic bru_valid_tmp;

  always_comb begin
    // setup


    // logic
    sink_data = dec_source_t'(ex_sink.data);

    source_data = '{
        alu_rslt: alu_rslt,
        rs1: sink_data.rs1,
        rs2: sink_data.rs2,
        rd: sink_data.rd,
        x1: sink_data.x1,
        x2: sink_data.x2,
        imm: sink_data.imm,
        wb_en: sink_data.wb_en,
        alu_cmd: sink_data.alu_cmd,
        bru_cmd: sink_data.bru_cmd,
        is_op32: sink_data.is_op32,
        is_rvc: sink_data.is_rvc,
        inst: sink_data.inst,
        pc: sink_data.pc,
        untaken_pc: sink_data.untaken_pc,
        id: sink_data.id
    };

    // output
    ex_source.valid = ex_sink.valid;
    ex_source.data = source_data;
    ex_sink.ready = ex_source.ready;

    bru_valid = bru_valid_tmp && ex_sink.valid;

  end

  overisc_alu i_alu (
      .alu_cmd(sink_data.alu_cmd),
      .opa(sink_data.x1),  // TODO: Implement forwarding logic.
      .opb(sink_data.x2),  // TODO: Implement forwarding logic.
      .is_op32(sink_data.is_op32),
      .rslt(alu_rslt)
  );

  overisc_bru i_bru (
      .bru_cmd(sink_data.bru_cmd),
      .pc(sink_data.pc),
      .x1(sink_data.x1),  // TODO: Implement forwarding logic.
      .x2(sink_data.x2),  // TODO: Implement forwarding logic.
      .imm(sink_data.imm),
      .valid(bru_valid_tmp),
      .tpc(bru_tpc)
  );

endmodule
