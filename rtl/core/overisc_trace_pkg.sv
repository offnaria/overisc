// SPDX-License-Identifier: CERN-OHL-P-2.0
// Pipeline trace for the Konata viewer (simulation only in effect).
//
// The core drives one internal signal of type trace_t named `trace` in overisc_core_top; the
// testbench reads it by hierarchical reference (tb_core.u_core.trace) and writes a Kanata log.
// Nothing else reads it, so synthesis removes it together with the IDs below.
//
// Every field describes the cycle that ends at the next rising edge ("this cycle").
//
// The instruction ID is a simulation-only sequence number carried in the pipeline payloads. It
// is assigned in F0 and must increase in program order (wrong-path instructions included).
//
// Stage reports (entry events): a valid slot says "this instruction is in that stage". The
// first report for a stage starts the stage; further reports for the same stage are ignored,
// so a one-cycle handshake pulse and a level held while the instruction waits give the same
// log. An instruction stays in its last reported stage until it is reported in a later one.
// Missing reports mean nothing, and any stage after F0 may be skipped. An instruction reported
// in several stages in the same cycle (passing through a bypassed buffer) is in the latest one. Every instruction must
// start in F0, where f0_pc gives its PC. f2_inst gives its bits when it is reported in F2.
//
// Leaving the pipeline is explicit:
//   - retire: the instruction retires at the end of this cycle
//   - redirect: every instruction younger than redirect_id (a larger ID) is flushed at the end of
//     this cycle; redirect_id itself stays. The redirect bus provides it (oldest source wins).
//     An instruction first reported in the redirect cycle enters F0 at its end, after the
//     redirect, so it is on the new path and stays.
//     Naming the instruction rather than a stage keeps the flush exact whichever cycle the stages
//     are reported in.
package overisc_trace_pkg;

  import overisc_cfg_pkg::*;
  import overisc_commit_pkg::NRET;

  localparam int TRACE_ID_W = 32;
  localparam int TRACE_NSTAGE = 8;

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
    trace_slot_t [TRACE_NSTAGE-1:0] stage;  // stage reports, indexed by trace_stage_e
    logic [XLEN-1:0] f0_pc;  // PC of the instruction reported in F0
    logic [ILEN-1:0] f2_inst;  // bits of the instruction reported in F2
    trace_slot_t [NRET-1:0] retire;  // instructions that retire at the end of this cycle
    // flush of every instruction younger than redirect_id at the end of this cycle
    logic redirect;
    trace_id_t redirect_id;
  } trace_t;

endpackage
