// SPDX-License-Identifier: CERN-OHL-P-2.0
// Commit record (design.md 9.1). Up to NRET records per cycle from the W stage.
// The testbench steps Spike one instruction per record and compares. On FPGA, recent
// records are kept in a BRAM ring buffer (9.3).
package overisc_commit_pkg;

  import overisc_cfg_pkg::*;

  localparam int NRET = 1;  // max instructions retired per cycle (2 for 2-way)

  typedef struct packed {
    logic            valid;
    logic [XLEN-1:0] cause;  // mcause format (MSB set for interrupts)
    logic [XLEN-1:0] tval;
  } commit_trap_t;

  typedef struct packed {
    logic            valid;
    logic [4:0]      addr;   // valid=0 for writes to x0
    logic [XLEN-1:0] data;
  } commit_rd_t;

  typedef struct packed {
    logic               valid;
    logic [XLEN-1:0]    vaddr;
    logic [PADDR_W-1:0] paddr;
    logic [7:0]         rmask;  // aligned by vaddr[2:0]; AMOs set both rmask and wmask
    logic [7:0]         wmask;
    logic [XLEN-1:0]    rdata;  // aligned 8 bytes before extraction
    logic [XLEN-1:0]    wdata;
  } commit_mem_t;

  typedef struct packed {
    logic            valid;
    logic [11:0]     addr;
    logic [XLEN-1:0] wdata;  // value after the write (WARL result)
  } commit_csr_t;

  typedef struct packed {
    logic            valid;
    logic [63:0]     order;   // sequence number in commit order (trapped instructions count)
    logic [XLEN-1:0] pc;
    logic [ILEN-1:0] insn;    // for RVC, the original instruction is in the low 16 bits
    logic            is_rvc;
    priv_e           priv;    // privilege mode the instruction executed in
    // trap.cause MSB = 0: this instruction raised an exception (rd/mem/csr are invalid)
    // trap.cause MSB = 1: an interrupt was taken before this instruction (it did not execute)
    commit_trap_t    trap;
    commit_rd_t      rd;
    commit_mem_t     mem;
    commit_csr_t     csr;
    // inject rd.data into Spike (MMIO loads, mcycle/time/instret reads, etc.)
    logic            inject;
  } commit_t;

endpackage
