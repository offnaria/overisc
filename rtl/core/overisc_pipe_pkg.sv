// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline structure definition of Overisc core.
//
// Macros are used to improve reusability, while core can access their flattened member
// without deep diving into the structure.

package overisc_pipe_pkg;
  import overisc_cfg_pkg::*, overisc_trace_pkg::trace_id_t;

  // Actual structures
  `define PCGEN_SOURCE_T \
    logic [XLEN-1:0] pc; \
    logic [XLEN-1:0] untaken_pc; \
    trace_id_t id

  `define IFU_SOURCE_T \
    logic [ILEN-1:0] inst
  // TODO: Add exception information.

  `define PREDEC_SOURCE_T \
    logic is_rvc

  `define DEC_SOURCE_T \
    logic [4:0] rs1; \
    logic [4:0] rs2; \
    logic [4:0] rd; \
    logic [XLEN-1:0] x1; \
    logic [XLEN-1:0] x2; \
    logic [XLEN-1:0] imm; \
    logic wb_en; // write back enabled; This instruction will update gpr[rd]. \
    alu_cmd_t alu_cmd; \
    bru_cmd_t bru_cmd; \
    logic is_op32

  `define EXEC_SOURCE_T \
    logic [XLEN-1:0] alu_rslt

  // Typedefs
  typedef enum int {
    ALU_ADD,
    ALU_SUB,
    ALU_SLL,
    ALU_SLT,
    ALU_SLTU,
    ALU_XOR,
    ALU_SRL,
    ALU_SRA,
    ALU_OR,
    ALU_AND
  } alu_cmd_e;
  typedef logic [ALU_AND:ALU_ADD] alu_cmd_t;

  typedef enum int {
    BRU_JAL,
    BRU_JALR,
    BRU_BEQ,
    BRU_BNE,
    BRU_BLT,
    BRU_BGE,
    BRU_BLTU,
    BRU_BGEU
  } bru_cmd_e;
  typedef logic [BRU_BGEU:BRU_JAL] bru_cmd_t;

  typedef struct packed {`PCGEN_SOURCE_T;} pcgen_source_t;

  typedef struct packed {
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } ifu_source_t;

  typedef struct packed {
    `PREDEC_SOURCE_T;
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } predec_source_t;

  typedef struct packed {
    `DEC_SOURCE_T;
    `PREDEC_SOURCE_T;
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } dec_source_t;

  typedef struct packed {
    `EXEC_SOURCE_T;
    `DEC_SOURCE_T;
    `PREDEC_SOURCE_T;
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } exec_source_t;

  typedef struct packed {
    `EXEC_SOURCE_T;
    `DEC_SOURCE_T;
    `PREDEC_SOURCE_T;
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } mem_source_t;

  typedef struct packed {
    `EXEC_SOURCE_T;
    `DEC_SOURCE_T;
    `PREDEC_SOURCE_T;
    `IFU_SOURCE_T;
    `PCGEN_SOURCE_T;
  } wb_source_t;

endpackage
