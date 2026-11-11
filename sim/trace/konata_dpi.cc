// SPDX-License-Identifier: Apache-2.0
// DPI functions behind overisc_sim_konata.sv: one Kanata log per simulation.
// Per cycle the SV side fills a sample with the overisc_konata_* setters and then calls
// overisc_konata_cycle().
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "konata_label.h"
#include "konata_tracer.h"

namespace {

struct State {
  std::ofstream file;
  std::unique_ptr<KonataTracer> tracer;
  TraceSample sample;
};

std::unique_ptr<State>& state() {
  static std::unique_ptr<State> s;
  return s;
}

}  // namespace

extern "C" {

// Returns 0 on success.
int overisc_konata_open(const char* path) {
  auto s = std::make_unique<State>();
  s->file.open(path);
  if (!s->file) return -1;
  s->tracer = std::make_unique<KonataTracer>(s->file, konata_label, &std::cerr);
  state() = std::move(s);
  return 0;
}

void overisc_konata_stage(int stage, unsigned char valid, int id) {
  state()->sample.stage[stage] = {valid != 0, static_cast<uint32_t>(id)};
}

void overisc_konata_front(long long f0_pc, int f2_inst) {
  TraceSample& s = state()->sample;
  s.f0_pc = static_cast<uint64_t>(f0_pc);
  s.f2_inst = static_cast<uint32_t>(f2_inst);
}

void overisc_konata_redirect(unsigned char valid, int id) {
  TraceSample& s = state()->sample;
  s.redirect = valid != 0;
  s.redirect_id = static_cast<uint32_t>(id);
}

void overisc_konata_retire(unsigned char valid, int id) {
  state()->sample.retire.push_back({valid != 0, static_cast<uint32_t>(id)});
}

void overisc_konata_cycle(long long cycle) {
  State& s = *state();
  s.tracer->sample(static_cast<uint64_t>(cycle), s.sample);
  s.sample = TraceSample{};
}

void overisc_konata_close() {
  if (!state()) return;
  state()->tracer->finish();
  if (const uint64_t n = state()->tracer->violations()) {
    std::cerr << "[konata] " << n << " trace rule violation(s); the log may be incomplete\n";
  }
  state().reset();
}

}  // extern "C"
