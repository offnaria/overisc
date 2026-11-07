// SPDX-License-Identifier: Apache-2.0
// A small pipeline scenario for the Kanata log tests, and a parser of the log.
//
// I0..I7 are instructions in fetch order. I2 is a jump in X that redirects at the end of cycle
// 7: I3 (D), I4 (FB), I5 (F2) and I6 (F1) are wrong-path and flushed; I7 is the jump target.
//
// scenario() reports every instruction in every cycle it is in a stage (level). pulses() turns
// that into reports of stage entries only; both must give the same log.
//
//  cycle  F0  F1  F2  FB  D   X   M   W   retire
//    0    I0
//    1    I1  I0
//    2    I2  I1  I0
//    3    I3  I2  I1      I0
//    4    I4  I3  I2      I1  I0
//    5    I5  I4  I3  I2      I1  I0
//    6    I6  I5  I4  I3  I2  I1      I0  I0
//    7        I6  I5  I4  I3  I2  I1      redirect from X
//    8    I7                      I2  I1  I1
//    9        I7                      I2  I2
//   10            I7
//   11                    I7
//   12                        I7
//   13                            I7
//   14                                I7  I7
#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "konata_tracer.h"

namespace konata_test {

enum : int { kF0,
             kF1,
             kF2,
             kFB,
             kD,
             kX,
             kM,
             kW };

inline std::vector<TraceSample> scenario() {
  // addi a0, a0, 1 / addi a1, a1, 2 / j +32 / nop ... / nop at the target
  const uint32_t inst[] = {0x00150513, 0x00258593, 0x0200006f, 0x00000013,
                           0x00000013, 0x00000013, 0x00000013, 0x00000013};
  const uint64_t pc[] = {0x80000000, 0x80000004, 0x80000008, 0x8000000c,
                         0x80000010, 0x80000014, 0x80000018, 0x80000028};
  std::vector<TraceSample> v(15);
  auto at = [&](int c, int stage, uint32_t i) {
    v[c].stage[stage] = {true, i};
    if (stage == kF0) v[c].f0_pc = pc[i];
    if (stage == kF2) v[c].f2_inst = inst[i];
  };
  auto retire = [&](int c, uint32_t i) { v[c].retire.push_back({true, i}); };

  at(0, kF0, 0);
  at(1, kF0, 1), at(1, kF1, 0);
  at(2, kF0, 2), at(2, kF1, 1), at(2, kF2, 0);
  at(3, kF0, 3), at(3, kF1, 2), at(3, kF2, 1), at(3, kD, 0);
  at(4, kF0, 4), at(4, kF1, 3), at(4, kF2, 2), at(4, kD, 1), at(4, kX, 0);
  at(5, kF0, 5), at(5, kF1, 4), at(5, kF2, 3), at(5, kFB, 2), at(5, kX, 1), at(5, kM, 0);
  at(6, kF0, 6), at(6, kF1, 5), at(6, kF2, 4), at(6, kFB, 3), at(6, kD, 2), at(6, kX, 1);
  at(6, kW, 0), retire(6, 0);
  at(7, kF1, 6), at(7, kF2, 5), at(7, kFB, 4), at(7, kD, 3), at(7, kX, 2), at(7, kM, 1);
  v[7].redirect = true;
  v[7].redirect_stage = kX;
  at(8, kF0, 7), at(8, kM, 2), at(8, kW, 1), retire(8, 1);
  at(9, kF1, 7), at(9, kW, 2), retire(9, 2);
  at(10, kF2, 7);
  at(11, kD, 7);
  at(12, kX, 7);
  at(13, kM, 7);
  at(14, kW, 7), retire(14, 7);
  return v;
}

// The same scenario reported as stage entries only: a slot is kept only in the cycle its
// content changed.
inline std::vector<TraceSample> pulses() {
  const std::vector<TraceSample> v = scenario();
  std::vector<TraceSample> out = v;
  for (size_t c = 1; c < v.size(); ++c) {
    for (int k = 0; k < TraceSample::kNumStages; ++k) {
      const TraceSlot& a = v[c].stage[k];
      const TraceSlot& b = v[c - 1].stage[k];
      if (a.valid && b.valid && a.id == b.id) out[c].stage[k] = {};
    }
  }
  return out;
}

// Parsed log, per instruction ID in the file.
struct ParsedInsn {
  uint64_t sim_id = 0;
  std::string label;
  std::vector<std::pair<std::string, uint64_t>> stages;  // (name, start cycle), lane 0
  int retire_type = -1;
  uint64_t retire_cycle = 0;
  int r_count = 0;
};

struct ParsedLog {
  std::map<uint64_t, ParsedInsn> insns;
  std::vector<std::string> errors;  // format violations
};

inline ParsedLog parse(const std::string& text) {
  ParsedLog log;
  std::istringstream in(text);
  std::string line;
  uint64_t cycle = 0;
  bool header = false, started = false;
  while (std::getline(in, line)) {
    std::vector<std::string> col;
    std::string c;
    std::istringstream ls(line);
    while (std::getline(ls, c, '\t')) col.push_back(c);
    if (col.empty()) continue;
    if (!header) {
      if (line != "Kanata\t0004") log.errors.push_back("bad header: " + line);
      header = true;
      continue;
    }
    const std::string& cmd = col[0];
    if (cmd == "C=") {
      if (started) log.errors.push_back("C= after the first cycle");
      cycle = std::stoull(col[1]);
      started = true;
      continue;
    }
    if (cmd == "C") {
      if (std::stoull(col[1]) == 0) log.errors.push_back("C 0");
      cycle += std::stoull(col[1]);
      continue;
    }
    const uint64_t id = std::stoull(col[1]);
    if (cmd == "I") {
      if (log.insns.count(id)) log.errors.push_back("I twice for " + col[1]);
      log.insns[id].sim_id = std::stoull(col[2]);
      continue;
    }
    if (!log.insns.count(id)) {
      log.errors.push_back(cmd + " before I for " + col[1]);
      continue;
    }
    ParsedInsn& p = log.insns[id];
    if (p.r_count) log.errors.push_back(cmd + " after R for " + col[1]);
    if (cmd == "L" && col[2] == "0") {
      p.label += col[3];
    } else if (cmd == "S" && col[2] == "0") {
      p.stages.push_back({col[3], cycle});
    } else if (cmd == "R") {
      p.r_count++;
      p.retire_type = std::stoi(col[3]);
      p.retire_cycle = cycle;
    }
  }
  for (const auto& [id, p] : log.insns) {
    if (p.r_count != 1) log.errors.push_back("R count != 1 for " + std::to_string(id));
  }
  return log;
}

}  // namespace konata_test
