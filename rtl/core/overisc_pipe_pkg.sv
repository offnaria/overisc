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

  // Typedefs
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

endpackage
