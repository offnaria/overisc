// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline trace for the Konata viewer (simulation only in effect).
//
// The core drives one internal signal of type trace_t named `trace` in overisc_core_top; the
// testbench reads it by hierarchical reference (tb_core.u_core.trace) and writes a Kanata log.
// Nothing else reads it, so synthesis removes it together with the IDs below.
//
// Every field describes the cycle that ends at the next rising edge ("this cycle").
//
// IDs are simulation-only sequence numbers carried in the pipeline payloads:
//   - fetch block ID: assigned when a block enters F0 (8-byte fetch block)
//   - instruction ID: assigned when an instruction leaves the Fetch Buffer
// Each must increase by one per block / instruction in program order (wrong-path ones included).
//
// Rules the logger relies on:
//   - a block or instruction that disappears from all stages without fb_push / retire was
//     flushed (the Fetch Buffer is the exception: blocks in it are invisible until fb_flush)
//   - d_fid names the block that holds the first halfword of the instruction in D
package overisc_trace_pkg;

  import overisc_cfg_pkg::*;
  import overisc_commit_pkg::NRET;

  localparam int TRACE_ID_W = 32;
  localparam int TRACE_NF = 3;  // F0, F1, F2
  localparam int TRACE_NB = 3;  // X, M, W

  typedef logic [TRACE_ID_W-1:0] trace_id_t;

  typedef struct packed {
    logic      valid;
    trace_id_t id;
  } trace_slot_t;

  typedef struct packed {
    // fetch blocks in F0..F2 (index 0 = F0); id = fetch block ID
    trace_slot_t [TRACE_NF-1:0] f;
    logic [XLEN-1:0]            f0_pc;     // fetch address of the block in F0
    trace_slot_t                fb_push;   // block written into the Fetch Buffer this cycle
    logic                       fb_flush;  // Fetch Buffer cleared at the end of this cycle
    // instruction in D; id = instruction ID
    trace_slot_t                d;
    trace_id_t                  d_fid;
    logic [XLEN-1:0]            d_pc;
    logic [ILEN-1:0]            d_insn;
    // instructions in X, M, W (index 0 = X)
    trace_slot_t [TRACE_NB-1:0] b;
    // instructions that retire at the end of this cycle
    trace_slot_t [NRET-1:0]     retire;
  } trace_t;

endpackage
