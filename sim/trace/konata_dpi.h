// SPDX-License-Identifier: Apache-2.0
// Outcome of the Kanata logger (overisc_sim_konata.sv) for the C++ test harness.
#pragma once

#include <cstdint>

struct KonataRunInfo {
  bool opened = false;      // a log was written in this run (+konata given and the hook built in)
  uint64_t violations = 0;  // trace-rule violations seen by KonataTracer
};

// Call before building a model; the logger fills it in when the log is closed.
void konata_reset_run_info();
const KonataRunInfo& konata_run_info();
