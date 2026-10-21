// SPDX-License-Identifier: CERN-OHL-P-2.0
// 確定命令の記録（design.md §9.1）。W 段から 1 サイクルに最大 NRET 個出す
// テストベンチはこれを見て Spike を 1 命令ずつ進めて比較する。FPGA では直近の記録を BRAM のリングバッファに残す（§9.3）
package overisc_commit_pkg;

  import overisc_cfg_pkg::*;

  localparam int NRET = 1;  // 1 サイクルに確定する最大命令数（2-way で 2）

  typedef struct packed {
    logic valid;
    logic [XLEN-1:0]    cause;  // mcause と同じ形式（割り込みなら最上位ビットが 1）
    logic [XLEN-1:0] tval;
  } commit_trap_t;

  typedef struct packed {
    logic            valid;
    logic [4:0]      addr;   // x0 への書き込みは valid=0
    logic [XLEN-1:0] data;
  } commit_rd_t;

  typedef struct packed {
    logic valid;
    logic [XLEN-1:0] vaddr;
    logic [PADDR_W-1:0] paddr;
    logic [7:0]         rmask;  // vaddr[2:0] で位置合わせ済み（AMO は rmask と wmask の両方）
    logic [7:0] wmask;
    logic [XLEN-1:0] rdata;  // 位置合わせ済み、切り出し前の 8B
    logic [XLEN-1:0] wdata;
  } commit_mem_t;

  typedef struct packed {
    logic            valid;
    logic [11:0]     addr;
    logic [XLEN-1:0] wdata;  // 書き込み後の値（WARL の結果）
  } commit_csr_t;

  typedef struct packed {
    logic            valid;
    logic [63:0]     order;   // 確定順の通し番号（トラップした命令も数える）
    logic [XLEN-1:0] pc;
    logic [ILEN-1:0] insn;    // RVC のときは下位 16bit が元の命令
    logic            is_rvc;
    priv_e           priv;    // 実行したときの特権モード
    // trap.cause の最上位ビットが 0: この命令が例外を起こした（rd/mem/csr は無効）
    // trap.cause の最上位ビットが 1: この命令を実行する前に割り込みを受け付けた（命令は実行していない）
    commit_trap_t    trap;
    commit_rd_t      rd;
    commit_mem_t     mem;
    commit_csr_t     csr;
    // rd の値を Spike に注入する（MMIO の読み出し、mcycle/time/instret などの読み出し）
    logic            inject;
  } commit_t;

endpackage
