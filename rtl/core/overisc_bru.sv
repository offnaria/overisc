// SPDX-License-Identifier: CERN-OHL-P-2.0
// Branch Resolution Unit module of Overisc core.

module overisc_bru
  import overisc_cfg_pkg::XLEN, overisc_pipe_pkg::*;
(
    input bru_cmd_t bru_cmd,
    input [XLEN-1:0] pc,
    input [XLEN-1:0] x1,
    input [XLEN-1:0] x2,
    input [XLEN-1:0] imm,
    output valid,
    output [XLEN-1:0] tpc  // target PC
);

  logic [BRU_BGEU:BRU_JAL] valid_tmp;

  always_comb begin
    valid_tmp[BRU_JAL] = (bru_cmd[BRU_JAL]) ? 1'b1 : '0;  // always
    valid_tmp[BRU_JALR] = (bru_cmd[BRU_JALR]) ? 1'b1 : '0;  // always
    valid_tmp[BRU_BEQ] = (bru_cmd[BRU_BEQ]) ? x1 == x2 : '0;
    valid_tmp[BRU_BNE] = (bru_cmd[BRU_BNE]) ? x1 != x2 : '0;
    valid_tmp[BRU_BLT] = (bru_cmd[BRU_BLT]) ? signed'(x1) < signed'(x2) : '0;
    valid_tmp[BRU_BGE] = (bru_cmd[BRU_BGE]) ? signed'(x1) >= signed'(x2) : '0;
    valid_tmp[BRU_BLTU] = (bru_cmd[BRU_BLTU]) ? x1 < x2 : '0;
    valid_tmp[BRU_BGEU] = (bru_cmd[BRU_BGEU]) ? x1 >= x2 : '0;

    // TODO: This must be modified when branch prediction, except for "always unvalid", is implemented,
    //       because predicted "valid" can be wrong and have to be corrected.
    valid = ^valid_tmp;

    tpc = imm + ((bru_cmd[BRU_JALR]) ? x1 : pc);
    tpc[0] = 1'b0;  // For JALR. (imm+pc)[0]==0 if JAL or BRANCH.

  end

endmodule
