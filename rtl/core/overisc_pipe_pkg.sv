// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline structure definition of Overisc core.

package overisc_pipe_pkg;
  import overisc_cfg_pkg::*;

  typedef struct packed {
    logic [XLEN-1:0] pc;
    logic [XLEN-1:0] untaken_pc;
    logic [INST_ID_W:0] id;
  } pcgen_source_t;

endpackage
