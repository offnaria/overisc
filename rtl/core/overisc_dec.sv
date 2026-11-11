// SPDX-License-Identifier: CERN-OHL-P-2.0
// Decoder module of Overisc core.
//
// Immediate and other control signals use so-called one-hot encoding optimization.
// Reference: https://arxiv.org/abs/2002.03568
//
// RESET_GPR: Reset all GPR entries to 0 when rst==1.
//
// TODO: Support multiple retire for superscalar.

module overisc_dec
  import overisc_cfg_pkg::*, overisc_pipe_pkg::*;
#(
    parameter RESET_GPR = 0
) (
    input clk,
    input rst,

    overisc_pipe_if.sink   dec_sink,
    overisc_pipe_if.source dec_source,

    overisc_pipe_if.sink wb  // write back
);

  typedef enum logic [4:0] {
    LOAD = 5'b00000,
    // LOAD_FP = 5'b00001,
    // CUSTOM_0 = 5'b00010,
    MISC_MEM = 5'b00011,
    OP_IMM = 5'b00100,
    AUIPC = 5'b00101,
    OP_IMM_32 = 5'b00110,
    STORE = 5'b01000,
    // STORE_FP = 5'b01001,
    // CUSTOM_1 = 5'b01010,
    AMO = 5'b01011,
    OP = 5'b01100,
    LUI = 5'b01101,
    OP_32 = 5'b01110,
    // MADD = 5'b10000,
    // MSUB = 5'b10001,
    // NMSUB = 5'b10010,
    // NMADD = 5'b10011,
    // OP_FP = 5'b10100,
    // OP_V = 5'b10101,
    // CUSTOM_2 = 5'b10110,
    BRANCH = 5'b11000,
    JALR = 5'b11001,
    JAL = 5'b11011,
    SYSTEM = 5'b11100,
    // OP_VE = 5'b11101,
    // CUSTOM_3 = 5'b11110,
    UNKNOWN
  } opcode_e;

  predec_source_t sink_data;
  dec_source_t source_data;
  wb_source_t wb_data;

  logic [ILEN-1:0] inst;
  opcode_e ope;
  logic type_r, type_i, type_s, type_b, type_u, type_j;
  logic [XLEN-1:0] imm, imm_i, imm_s, imm_b, imm_u, imm_j;  // immediate
  logic [4:0] rs1, rs2, rd;
  logic [XLEN-1:0] x1, x2;  // latest register value
  logic wb_en;  // write back enabled
  logic [2:0] funct3;
  logic [6:0] funct7;
  logic alu_ope;
  alu_cmd_t alu_cmd;
  logic bru_ope;
  bru_cmd_t bru_cmd;
  logic is_op32;

  // General Purpose Register
  logic [XLEN-1:0] gpr[32];

  always_comb begin
    // setup


    // logic
    sink_data = predec_source_t'(dec_sink.data);
    wb_data = wb_source_t'(wb.data);

    inst = sink_data.inst;
    ope = opcode_e'(inst[6:2]);

    type_r = ope inside {AMO, OP, OP_32};
    type_i = ope inside {LOAD, OP_IMM, OP_IMM_32, JALR};
    type_s = ope inside {STORE};
    type_b = ope inside {BRANCH};
    type_u = ope inside {AUIPC, LUI};
    type_j = ope inside {JAL};

    imm_i = (type_i) ? XLEN'(signed'({inst[31:20]})) : '0;
    imm_s = (type_s) ? XLEN'(signed'({inst[31:25], inst[11:7]})) : '0;
    imm_b = (type_b) ? XLEN'(signed'({inst[31], inst[7], inst[30:25], inst[11:8], 1'b0})) : '0;
    imm_u = (type_u) ? XLEN'(signed'({inst[31:12], 12'h000})) : '0;
    imm_j = (type_j) ? XLEN'(signed'({inst[31], inst[19:12], inst[20], inst[30:21], 1'b0})) : '0;
    imm = imm_i ^ imm_s ^ imm_b ^ imm_u ^ imm_j;  // Non-exclusive OR is also available.

    rs1 = inst[19:15];
    rs2 = inst[24:20];
    rd = inst[11:7];
    x1 = (rs1 == '0) ? '0 : gpr[rs1];  // TODO: Use the latest value on wb.
    x2 = (rs2 == '0) ? '0 : gpr[rs2];  // TODO: Use the latest value on wb.
    // opa = (ope inside {AUIPC, JAL}) ? sink_data.pc : (ope == LUI) ? '0 : x1;
    // opb = (type_r) ? x2 : imm;
    wb_en = (ope inside {LUI, AUIPC, JAL, JALR, LOAD, OP, OP_IMM, OP_32, OP_IMM_32}) && (rd != '0);

    funct3 = inst[14:12];
    funct7 = inst[31:25];

    alu_ope = ope inside {OP, OP_IMM, OP_32, OP_IMM_32};
    alu_cmd[ALU_SUB] = alu_ope && (funct3 == 3'b000) && (inst[5] && funct7[5]);  // OP, OP_32
    alu_cmd[ALU_SLL] = alu_ope && (funct3 == 3'b001);
    alu_cmd[ALU_SLT] = alu_ope && (funct3 == 3'b010);
    alu_cmd[ALU_SLTU] = alu_ope && (funct3 == 3'b011);
    alu_cmd[ALU_XOR] = alu_ope && (funct3 == 3'b100);
    alu_cmd[ALU_SRL] = alu_ope && (funct3 == 3'b101) && !funct7[5];
    alu_cmd[ALU_SRA] = alu_ope && (funct3 == 3'b101) && funct7[5];
    alu_cmd[ALU_OR] = alu_ope && (funct3 == 3'b110);
    alu_cmd[ALU_AND] = alu_ope && (funct3 == 3'b111);
    alu_cmd[ALU_ADD] = !(|alu_cmd[ALU_AND:ALU_SUB]);  // Perform ADD when any other one is not active.

    bru_ope = ope inside {BRANCH, JALR, JAL};
    bru_cmd[BRU_JAL] = (ope == JAL);
    bru_cmd[BRU_JALR] = (ope == JALR);
    bru_cmd[BRU_BEQ] = (ope == BRANCH) && (funct3 == 3'b000);
    bru_cmd[BRU_BNE] = (ope == BRANCH) && (funct3 == 3'b001);
    bru_cmd[BRU_BLT] = (ope == BRANCH) && (funct3 == 3'b100);
    bru_cmd[BRU_BGE] = (ope == BRANCH) && (funct3 == 3'b101);
    bru_cmd[BRU_BLTU] = (ope == BRANCH) && (funct3 == 3'b110);
    bru_cmd[BRU_BGEU] = (ope == BRANCH) && (funct3 == 3'b111);

    is_op32 = ope inside {OP_32, OP_IMM_32};

    source_data = '{
        rs1: rs1,
        rs2: rs2,
        rd: rd,
        x1: x1,
        x2: x2,
        imm: imm,
        wb_en: wb_en,
        alu_cmd: alu_cmd,
        bru_cmd: bru_cmd,
        is_op32: is_op32,
        is_rvc: sink_data.is_rvc,
        inst: inst,
        pc: sink_data.pc,
        untaken_pc: sink_data.untaken_pc,
        id: sink_data.id
    };

    // output
    dec_source.valid = dec_sink.valid;
    dec_source.data = source_data;
    dec_sink.ready = dec_source.ready;
    wb.ready = 1'b1;

  end

  always_ff @(posedge clk) begin
    if (wb.valid && wb_data.wb_en) begin
      gpr[wb_data.rd] <= wb_data.alu_rslt;  // TODO
    end

    if (rst) begin
      if (RESET_GPR) begin
        for (int i = 0; i < 32; ++i) begin
          gpr[i] <= '0;
        end
      end
    end

    if (0) begin
      $write("funct7=%07b, wb_data=%x, type_r=%b bru_ope=%b\n", funct7, wb_data, type_r, bru_ope);
    end
  end

endmodule
