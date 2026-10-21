// SPDX-License-Identifier: CERN-OHL-P-2.0
// コア全体で共有する幅と基本的な型
package overisc_cfg_pkg;

  localparam int XLEN = 64;
  localparam int ILEN = 32;
  // 物理アドレス幅。Sv39 の物理アドレスは 56bit。FPGA の面積が気になれば縮めてよい（範囲外は PMA でアクセスフォルト）
  localparam int PADDR_W = 56;

  typedef enum logic [1:0] {
    PRIV_U = 2'b00,
    PRIV_S = 2'b01,
    PRIV_M = 2'b11
  } priv_e;

endpackage
