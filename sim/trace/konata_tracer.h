// SPDX-License-Identifier: Apache-2.0
// Writes a Kanata log (Konata viewer, format version 0004) from per-cycle pipeline samples
// (overisc_trace_pkg::trace_t, see rtl/core/overisc_trace_pkg.sv for the rules).
//
// Samples are stage reports: the first report of an instruction in a stage starts that stage,
// repeats are ignored, and missing reports mean nothing. Instructions leave by retire or by a
// redirect, which flushes everything in the stages before the redirecting one. Stages
// (lane 0): F0 F1 F2 FB D X M W. The label is the PC from F0 followed by the disassembly from
// F2, so an instruction flushed before F2 shows only its PC.
//
// Input that breaks the rules (an ID reported after it left, an instruction first reported
// after F0, moving back to an earlier stage, overtaking an older one) is reported on `warn`
// and ignored, so it cannot produce misleading entries.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

struct TraceSlot {
  bool valid = false;
  uint32_t id = 0;
};

struct TraceSample {
  static constexpr int kNumStages = 8;
  TraceSlot stage[kNumStages];  // indexed by overisc_trace_pkg::trace_stage_e
  uint64_t f0_pc = 0;
  uint32_t f2_inst = 0;
  std::vector<TraceSlot> retire;
  bool redirect = false;
  int redirect_stage = 0;  // overisc_trace_pkg::trace_stage_e
};

class KonataTracer {
 public:
  // disasm(inst) returns the instruction text for the label pane (e.g. "addi a0, a0, 1").
  using DisasmFn = std::function<std::string(uint32_t inst)>;

  KonataTracer(std::ostream& out, DisasmFn disasm, std::ostream* warn = nullptr);

  // Records one cycle. Cycles must be passed in increasing order. Everything before this cycle
  // is written to the log.
  void sample(uint64_t cycle, const TraceSample& s);

  // Ends the log: everything still in flight is written as flushed.
  void finish();

  // Number of trace-rule violations seen so far (see above).
  uint64_t violations() const { return violations_; }

 private:
  // Same order as overisc_trace_pkg::trace_stage_e.
  enum Stage { kF0,
               kF1,
               kF2,
               kFB,
               kD,
               kX,
               kM,
               kW };

  struct Insn {
    uint64_t kid;  // ID in the log file
    Stage stage;
    bool has_inst;  // disassembly added to the label
  };

  void emit(uint64_t cycle, std::string line);
  void report(uint64_t c, uint32_t id, Stage st, const TraceSample& s);
  void write_until(uint64_t cycle);  // writes events with cycle < `cycle`
  void violation(uint64_t cycle, const std::string& what);

  std::ostream& out_;
  DisasmFn disasm_;
  std::ostream* warn_;
  std::multimap<uint64_t, std::string> pending_;  // insertion order kept per cycle
  bool header_ = false;
  uint64_t written_cycle_ = 0;
  uint64_t last_cycle_ = 0;
  uint64_t next_kid_ = 0;
  uint64_t next_retire_ = 0;
  uint64_t violations_ = 0;
  std::map<uint32_t, Insn> insns_;   // live instructions by instruction ID
  std::optional<uint32_t> last_id_;  // youngest ID seen; older ones not in insns_ have left
};
