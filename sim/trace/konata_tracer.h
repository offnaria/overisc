// SPDX-License-Identifier: Apache-2.0
// Writes a Kanata log (Konata viewer, format version 0004) from per-cycle pipeline samples
// (overisc_trace_pkg::trace_t, see rtl/core/overisc_trace_pkg.sv).
//
// F0..F2 work on fetch blocks; instructions only exist from the Fetch Buffer exit on. When an
// instruction first shows up in D, the history of its fetch block (F0, F1, F2, FB) is written
// as the instruction's first stages. Because a Kanata log cannot go back in time, events are
// buffered and written in cycle order once no live fetch block can add earlier events.
//
// Stages (lane 0): F0 F1 F2 FB D X M W. Fetch blocks that are flushed before any of their
// instructions reach D appear as flushed pseudo-instructions labeled "fetch block".
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>

struct TraceSlot {
  bool valid = false;
  uint32_t id = 0;
};

struct TraceSample {
  static constexpr int kNumF = 3;  // F0, F1, F2
  static constexpr int kNumB = 3;  // X, M, W
  TraceSlot f[kNumF];
  uint64_t f0_pc = 0;
  TraceSlot fb_push;
  bool fb_flush = false;
  TraceSlot d;
  uint32_t d_fid = 0;
  uint64_t d_pc = 0;
  uint32_t d_insn = 0;
  TraceSlot b[kNumB];
  std::vector<TraceSlot> retire;
};

class KonataTracer {
 public:
  // label(pc, insn) returns the text for the label pane (e.g. "80000000: addi a0, a0, 1").
  using LabelFn = std::function<std::string(uint64_t pc, uint32_t insn)>;

  KonataTracer(std::ostream& out, LabelFn label);

  // Records one cycle. Cycles must be passed in increasing order.
  void sample(uint64_t cycle, const TraceSample& s);

  // Ends the log: everything still in flight is written as flushed.
  void finish();

 private:
  enum Stage { kF0, kF1, kF2, kFB, kD, kX, kM, kW, kNone };

  struct Block {
    uint64_t pc = 0;
    uint64_t start[4] = {};  // cycle each of F0, F1, F2, FB started
    bool has[4] = {};
    Stage stage = kNone;
    bool in_fb = false;
    bool used = false;  // at least one instruction reached D
  };

  struct Insn {
    uint64_t kid;  // ID in the log file
    Stage stage;
  };

  void emit(uint64_t cycle, std::string line);
  uint64_t new_kid(uint64_t cycle, uint64_t sim_id);
  void flush_block(uint32_t fid, const Block& b, uint64_t cycle);
  void write_until(uint64_t cycle);  // writes events with cycle < `cycle`

  std::ostream& out_;
  LabelFn label_;
  std::multimap<uint64_t, std::string> pending_;  // insertion order kept per cycle
  bool header_ = false;
  uint64_t written_cycle_ = 0;
  uint64_t last_cycle_ = 0;
  uint64_t next_kid_ = 0;
  uint64_t next_retire_ = 0;
  std::map<uint32_t, Block> blocks_;  // live fetch blocks by fetch block ID
  std::map<uint32_t, Insn> insns_;    // live instructions by instruction ID
};
