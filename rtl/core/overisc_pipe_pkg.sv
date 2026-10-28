// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline structure definition of Overisc core.

package overisc_pipe_pkg;
  import overisc_cfg_pkg::*, overisc_trace_pkg::trace_id_t;

  typedef struct packed {
    logic [XLEN-1:0] pc;
    logic [XLEN-1:0] untaken_pc;
    trace_id_t id;
  } pcgen_source_t;

  typedef struct packed {
    logic [XLEN-1:0] inst;
    // TODO: Add exception information.

    // from pcgen
    logic [XLEN-1:0] pc;
    logic [XLEN-1:0] untaken_pc;
    trace_id_t id;
  } ifu_source_t;

endpackage
