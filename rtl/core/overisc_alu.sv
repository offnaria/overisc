// SPDX-License-Identifier: CERN-OHL-P-2.0
// Arithmetic Logic Unit module of Overisc core.

module overisc_alu
  import overisc_cfg_pkg::XLEN, overisc_pipe_pkg::*;
(
    input alu_cmd_t alu_cmd,
    input [XLEN-1:0] opa,
    input [XLEN-1:0] opb,
    input is_op32,
    output [XLEN-1:0] rslt
);

  logic [XLEN-1:0] alu_tmp[ALU_ADD:ALU_AND];

  always_comb begin
    alu_tmp[ALU_ADD] = (alu_cmd[ALU_ADD]) ? opa + opb : '0;
    alu_tmp[ALU_SUB] = (alu_cmd[ALU_SUB]) ? opa - opb : '0;
    alu_tmp[ALU_SLL] = (alu_cmd[ALU_SLL]) ? opa << opb[5:0] : '0;
    alu_tmp[ALU_SLT] = (alu_cmd[ALU_SLT]) ? XLEN'(unsigned'(signed'(opa) < signed'(opb))) : '0;
    alu_tmp[ALU_SLTU] = (alu_cmd[ALU_SLTU]) ? XLEN'(opa < opb) : '0;
    alu_tmp[ALU_XOR] = (alu_cmd[ALU_XOR]) ? opa ^ opb : '0;
    alu_tmp[ALU_SRL] = (alu_cmd[ALU_SRL]) ? opa >> opb[5:0] : '0;
    alu_tmp[ALU_SRA] = (alu_cmd[ALU_SRA]) ? XLEN'(signed'({opa[XLEN-1], opa}) >> opb[5:0]) : '0;
    alu_tmp[ALU_OR] = (alu_cmd[ALU_OR]) ? opa | opb : '0;
    alu_tmp[ALU_AND] = (alu_cmd[ALU_AND]) ? opa & opb : '0;

    rslt = '0;
    for (int i = ALU_ADD; i <= ALU_AND; ++i) begin
      rslt ^= alu_tmp[i];
    end

    if (is_op32) begin
      rslt = XLEN'(rslt[31:0]);  // NOTE: Some operations don't have "W" variants.
    end
  end

endmodule
