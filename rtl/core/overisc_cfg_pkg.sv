// SPDX-License-Identifier: CERN-OHL-P-2.0
// Widths and basic types shared across the core
package overisc_cfg_pkg;

  localparam int XLEN = 64;
  localparam int ILEN = 32;
  // Physical address width (56 bits for Sv39). May be reduced to save FPGA area;
  // addresses outside the range raise an access fault via PMA.
  localparam int PADDR_W = 56;

  localparam int INST_ID_W = 64;

  typedef enum logic [1:0] {
    PRIV_U = 2'b00,
    PRIV_S = 2'b01,
    PRIV_M = 2'b11
  } priv_e;

endpackage
