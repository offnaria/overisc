// SPDX-License-Identifier: CERN-OHL-P-2.0
// コア内部のメモリ要求/応答（D-36）。フェッチ/LSU とキャッシュの位置（M0〜M3 は AXI への変換器）の間で使う
// 命令側 1 本、データ側はキャッシュ側と MMIO 側の 2 本（ポートが行き先を表す）
// ready/valid は構造体の外に置く（valid は ready に依存させない、受理まで保持・内容不変）
package overisc_mem_pkg;

  import overisc_cfg_pkg::*;

  localparam int MEM_DATA_W = 64;  // フェッチ幅 8B、LSU は最大 8B
  localparam int MEM_STRB_W = MEM_DATA_W / 8;
  localparam int MEM_ID_W = 2;

  typedef enum logic [1:0] {
    MEM_OP_READ  = 2'd0,
    MEM_OP_WRITE = 2'd1
    // 後で cbo 系を追加
  } mem_op_e;

  typedef enum logic [1:0] {
    MEM_SRC_I   = 2'd0,
    MEM_SRC_D   = 2'd1,
    MEM_SRC_PTW = 2'd2
  } mem_src_e;

  typedef enum logic [1:0] {
    MEM_ERR_NONE         = 2'd0,
    MEM_ERR_ACCESS_FAULT = 2'd1,  // AXI の SLVERR/DECERR
    MEM_ERR_HW_ERROR     = 2'd2   // 将来の ECC など
  } mem_err_e;

  typedef struct packed {
    mem_op_e               op;
    logic [PADDR_W-1:0]    paddr;
    logic [1:0]            size;   // log2(バイト数)
    logic [MEM_STRB_W-1:0] wstrb;  // paddr[2:0] で位置合わせ済み
    logic [MEM_DATA_W-1:0] wdata;  // paddr[2:0] で位置合わせ済み
    logic [MEM_ID_W-1:0]   id;
    mem_src_e              src;
  } mem_req_t;

  typedef struct packed {
    logic [MEM_DATA_W-1:0]   rdata;  // 8B 境界に整列した 8B（切り出しと符号拡張は要求側）
    logic [MEM_ID_W-1:0] id;
    mem_err_e err;
  } mem_resp_t;

endpackage
