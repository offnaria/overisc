// SPDX-License-Identifier: Apache-2.0
// C++ side of the shared simulation state behind the DPI functions (sim_dpi.cc).
// Test harnesses call sim_reset_state() before building a Verilated model, run it, and read
// the outcome reported by the SV side through overisc_sim_report().
#pragma once

#include <cstdint>
#include <string>

#include "sim_memory.h"

enum class SimResultKind : int { kNone = 0, kPass = 1, kFail = 2, kTimeout = 3, kError = 4 };

struct SimResult {
  SimResultKind kind = SimResultKind::kNone;
  uint64_t value = 0;   // tohost value for kPass/kFail
  uint64_t cycles = 0;  // cycles after reset
};

// Drops memory contents, regions, ports and the last result.
void sim_reset_state();

const SimResult& sim_result();
SimMemory& sim_memory();

std::string to_string(SimResultKind kind);
