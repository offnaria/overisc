// SPDX-License-Identifier: Apache-2.0
// Helpers for running Verilated models from Catch2 tests.
//
// Each run gets a fresh VerilatedContext and model, and the shared DPI state is reset
// first, so several runs can happen in one process (e.g. Catch2 GENERATE).
//
// Two kinds of models:
//   - clocked: the top has `clk` and `rst` inputs and no timing constructs; C++ drives the
//     clock (run_clocked)
//   - timed: the top generates its own clock with delays (built with --timing); C++ only
//     advances time (run_timed)
#pragma once

#include <verilated.h>
#if VM_TRACE_FST
#include <verilated_fst_c.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "sim_state.h"

struct SimRunOptions {
  std::vector<std::string> plusargs;  // e.g. "+seed=1"
  uint64_t max_cycles = 10'000'000;   // safety net for clocked models
  unsigned reset_cycles = 3;
  std::string fst;                    // waveform file (empty: none; needs TRACE_FST build)
};

namespace sim_run_detail {

inline std::unique_ptr<VerilatedContext> make_context(const SimRunOptions& opt) {
  sim_reset_state();
  auto ctx = std::make_unique<VerilatedContext>();
  std::vector<const char*> argv{"sim"};
  for (const auto& a : opt.plusargs) argv.push_back(a.c_str());
  ctx->commandArgs(static_cast<int>(argv.size()), argv.data());
  ctx->quiet(true);  // no "S i m u l a t i o n   R e p o r t" banner
  return ctx;
}

}  // namespace sim_run_detail

// Runs a clocked model until $finish or max_cycles. Returns the reported result.
template <class Model>
SimResult run_clocked(const SimRunOptions& opt) {
  auto ctx = sim_run_detail::make_context(opt);
  Model top{ctx.get()};
#if VM_TRACE_FST
  std::unique_ptr<VerilatedFstC> tfp;
  if (!opt.fst.empty()) {
    ctx->traceEverOn(true);
    tfp = std::make_unique<VerilatedFstC>();
    top.trace(tfp.get(), 99);
    tfp->open(opt.fst.c_str());
  }
#endif
  auto half_cycle = [&](int clk) {
    top.clk = clk;
    top.eval();
#if VM_TRACE_FST
    if (tfp) tfp->dump(ctx->time());
#endif
    ctx->timeInc(5);
  };

  top.rst = 1;
  for (unsigned i = 0; i < opt.reset_cycles; ++i) {
    half_cycle(0);
    half_cycle(1);
  }
  half_cycle(0);
  top.rst = 0;
  for (uint64_t c = 0; c < opt.max_cycles && !ctx->gotFinish(); ++c) {
    half_cycle(1);
    if (ctx->gotFinish()) break;
    half_cycle(0);
  }
  top.final();
#if VM_TRACE_FST
  if (tfp) tfp->close();
#endif
  return sim_result();
}

// Runs a timed model (built with --timing) until $finish or no more events.
template <class Model>
SimResult run_timed(const SimRunOptions& opt) {
  auto ctx = sim_run_detail::make_context(opt);
  Model top{ctx.get()};
  while (!ctx->gotFinish()) {
    top.eval();
    if (!top.eventsPending()) break;
    ctx->time(top.nextTimeSlot());
  }
  top.final();
  return sim_result();
}

// Waveform path from the OVERISC_FST environment variable (empty if unset).
inline std::string fst_from_env() {
  const char* p = std::getenv("OVERISC_FST");
  return p ? p : "";
}
