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
//   - fetch block ID: assigned when a block is created (8-byte fetch block, in F0)
//   - instruction ID: assigned when an instruction leaves the Fetch Buffer
// Each must increase by one per block / instruction in program order (wrong-path ones included).
//
// Stage reports (entry events): a valid slot says "this block / instruction is in that stage".
// The first report for a stage starts the stage; further reports for the same stage are
// ignored, so a one-cycle handshake pulse and a level held while the item waits give the same
// log. An item stays in its last reported stage until it is reported in a later one. Missing
// reports mean nothing; in particular they do not mean "flushed".
//
// Leaving the pipeline is explicit:
//   - retire: the instruction retires at the end of this cycle
//   - redirect: everything in a stage before redirect_stage (blocks in F0..F2 and the Fetch
//     Buffer, instructions in D..W) is flushed at the end of this cycle; the item in
//     redirect_stage itself stays. This is the redirect bus (oldest source wins).
//
// d_fid names the block that holds the first halfword of the instruction in D.
package overisc_trace_pkg;

  import overisc_cfg_pkg::*;
  import overisc_commit_pkg::NRET;

  localparam int TRACE_ID_W = 32;
  localparam int TRACE_NF = 3;  // F0, F1, F2
  localparam int TRACE_NB = 3;  // X, M, W

  typedef logic [TRACE_ID_W-1:0] trace_id_t;

  typedef enum logic [2:0] {
    TRACE_F0 = 3'd0,
    TRACE_F1 = 3'd1,
    TRACE_F2 = 3'd2,
    TRACE_FB = 3'd3,
    TRACE_D  = 3'd4,
    TRACE_X  = 3'd5,
    TRACE_M  = 3'd6,
    TRACE_W  = 3'd7
  } trace_stage_e;

  typedef struct packed {
    logic      valid;
    trace_id_t id;
  } trace_slot_t;

  typedef struct packed {
    // fetch blocks reported in F0..F2 (index 0 = F0); id = fetch block ID
    trace_slot_t [TRACE_NF-1:0] f;
    logic [XLEN-1:0]            f0_pc;           // fetch address of the block in f[0]
    trace_slot_t                fb;              // fetch block reported in the Fetch Buffer
    // instruction reported in D; id = instruction ID
    trace_slot_t                d;
    trace_id_t                  d_fid;
    logic [XLEN-1:0]            d_pc;
    logic [ILEN-1:0]            d_insn;
    // instructions reported in X, M, W (index 0 = X)
    trace_slot_t [TRACE_NB-1:0] b;
    // instructions that retire at the end of this cycle
    trace_slot_t [NRET-1:0]     retire;
    // flush of everything before redirect_stage at the end of this cycle
    logic                       redirect;
    logic [2:0]                 redirect_stage;  // trace_stage_e (plain bits: '{default: '0} works)
  } trace_t;

endpackage
