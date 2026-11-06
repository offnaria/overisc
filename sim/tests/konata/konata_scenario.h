// SPDX-License-Identifier: Apache-2.0
// A small pipeline scenario for the Kanata log tests, and a parser of the log.
//
// Blocks B0..B4 are 8-byte fetch blocks; I0..I4 are instructions. I2 is a jump in X that
// redirects at the end of cycle 7: I3 (D), B2 (Fetch Buffer) and B3 (F2) are flushed, and B4
// is fetched from the target.
//
// scenario() reports every item in every cycle it is in a stage (level). pulses() turns that
// into reports of stage entries only; both must give the same log.
//
//  cycle  F0  F1  F2  FB      D       X   M   W   retire
//    0    B0
//    1    B1  B0
//    2    B2  B1  B0
//    3    B3  B2  B1  B0      I0(B0)
//    4        B3  B2  B1      I1(B0)  I0
//    5            B3  B2      I2(B1)  I1  I0
//    6            B3  B2      I2      I1      I0  I0
//    7            B3  B2      I3(B1)  I2  I1          redirect from X
//    8    B4                              I2  I1  I1
//    9        B4                              I2  I2
//   10            B4
//   11                B4      I4(B4)
//   12                                I4
//   13                                    I4
//   14                                        I4  I4
#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "konata_tracer.h"

namespace konata_test {

inline std::vector<TraceSample> scenario() {
  const uint32_t insn[] = {0x00150513, 0x00258593, 0x0100006f, 0x00000013, 0x00000013};
  const uint64_t pc[] = {0x80000000, 0x80000004, 0x80000008, 0x8000000c, 0x80000018};
  const uint32_t fid_of[] = {0, 0, 1, 1, 4};
  std::vector<TraceSample> v(15);
  auto f = [&](int c, int stage, uint32_t fid, uint64_t fpc = 0) {
    v[c].f[stage] = {true, fid};
    if (stage == 0) v[c].f0_pc = fpc;
  };
  auto d = [&](int c, uint32_t i) {
    v[c].d = {true, i};
    v[c].d_fid = fid_of[i];
    v[c].d_pc = pc[i];
    v[c].d_insn = insn[i];
  };
  auto b = [&](int c, int stage, uint32_t i) { v[c].b[stage] = {true, i}; };
  // the block written into the Fetch Buffer at the end of cycle c is in FB from c + 1
  auto push = [&](int c, uint32_t fid) { v[c + 1].fb = {true, fid}; };
  auto retire = [&](int c, uint32_t i) { v[c].retire.push_back({true, i}); };

  f(0, 0, 0, 0x80000000);
  f(1, 0, 1, 0x80000008), f(1, 1, 0);
  f(2, 0, 2, 0x80000010), f(2, 1, 1), f(2, 2, 0), push(2, 0);
  f(3, 0, 3, 0x80000018), f(3, 1, 2), f(3, 2, 1), push(3, 1), d(3, 0);
  f(4, 1, 3), f(4, 2, 2), push(4, 2), d(4, 1), b(4, 0, 0);
  f(5, 2, 3), d(5, 2), b(5, 0, 1), b(5, 1, 0);
  f(6, 2, 3), d(6, 2), b(6, 0, 1), b(6, 2, 0), retire(6, 0);
  v[6].fb = {true, 2};  // B2 waits in the Fetch Buffer
  v[7].fb = {true, 2};
  f(7, 2, 3), d(7, 3), b(7, 0, 2), b(7, 1, 1);
  v[7].redirect = true;
  v[7].redirect_stage = 5;  // X
  f(8, 0, 4, 0x80000018), b(8, 1, 2), b(8, 2, 1), retire(8, 1);
  f(9, 1, 4), b(9, 2, 2), retire(9, 2);
  f(10, 2, 4), push(10, 4);
  d(11, 4);
  b(12, 0, 4);
  b(13, 1, 4);
  b(14, 2, 4), retire(14, 4);
  return v;
}

// The same scenario reported as stage entries only: a slot is kept only in the cycle its
// content changed.
inline std::vector<TraceSample> pulses() {
  std::vector<TraceSample> v = scenario();
  std::vector<TraceSample> out = v;
  auto same = [](const TraceSlot& a, const TraceSlot& b) {
    return a.valid && b.valid && a.id == b.id;
  };
  for (size_t c = 1; c < v.size(); ++c) {
    for (int k = 0; k < TraceSample::kNumF; ++k) {
      if (same(v[c].f[k], v[c - 1].f[k])) out[c].f[k] = {};
    }
    if (same(v[c].fb, v[c - 1].fb)) out[c].fb = {};
    if (same(v[c].d, v[c - 1].d)) out[c].d = {};
    for (int k = 0; k < TraceSample::kNumB; ++k) {
      if (same(v[c].b[k], v[c - 1].b[k])) out[c].b[k] = {};
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
