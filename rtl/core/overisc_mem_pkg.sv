// SPDX-License-Identifier: CERN-OHL-P-2.0
// Core-internal memory request/response (D-36), used between fetch/LSU and the cache slot
// (a bridge to AXI until M3). One port on the I-side; two on the D-side (cacheable and MMIO),
// so the port itself selects the destination.
// valid/ready live outside the structs: valid must not depend on ready, and once asserted
// it stays high with stable contents until accepted.
package overisc_mem_pkg;

  import overisc_cfg_pkg::*;

  localparam int MEM_DATA_W = 64;  // 8B fetch width; LSU accesses are at most 8B
  localparam int MEM_STRB_W = MEM_DATA_W / 8;
  localparam int MEM_ID_W = 2;

  typedef enum logic [1:0] {
    MEM_OP_READ  = 2'd0,
    MEM_OP_WRITE = 2'd1
    // cbo.* ops will be added later
  } mem_op_e;

  typedef enum logic [1:0] {
    MEM_SRC_I   = 2'd0,
    MEM_SRC_D   = 2'd1,
    MEM_SRC_PTW = 2'd2
  } mem_src_e;

  typedef enum logic [1:0] {
    MEM_ERR_NONE         = 2'd0,
    MEM_ERR_ACCESS_FAULT = 2'd1,  // AXI SLVERR/DECERR
    MEM_ERR_HW_ERROR     = 2'd2   // e.g. ECC in the future
  } mem_err_e;

  typedef struct packed {
    mem_op_e               op;
    logic [PADDR_W-1:0]    paddr;
    logic [1:0]            size;   // log2(bytes)
    logic [MEM_STRB_W-1:0] wstrb;  // aligned by paddr[2:0]
    logic [MEM_DATA_W-1:0] wdata;  // aligned by paddr[2:0]
    logic [MEM_ID_W-1:0]   id;
    mem_src_e              src;
  } mem_req_t;

  typedef struct packed {
    logic [MEM_DATA_W-1:0] rdata;  // 8B-aligned 8 bytes; requester extracts and sign-extends
    logic [MEM_ID_W-1:0]   id;
    mem_err_e              err;
  } mem_resp_t;

endpackage
