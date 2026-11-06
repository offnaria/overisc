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

void overisc_konata_fetch(int stage, unsigned char valid, int id) {
  state()->sample.f[stage] = {valid != 0, static_cast<uint32_t>(id)};
}

void overisc_konata_front(long long f0_pc, unsigned char fb_push_valid, int fb_push_id,
                          unsigned char fb_flush) {
  TraceSample& s = state()->sample;
  s.f0_pc = static_cast<uint64_t>(f0_pc);
  s.fb_push = {fb_push_valid != 0, static_cast<uint32_t>(fb_push_id)};
  s.fb_flush = fb_flush != 0;
}

void overisc_konata_decode(unsigned char valid, int id, int fid, long long pc, int insn) {
  TraceSample& s = state()->sample;
  s.d = {valid != 0, static_cast<uint32_t>(id)};
  s.d_fid = static_cast<uint32_t>(fid);
  s.d_pc = static_cast<uint64_t>(pc);
  s.d_insn = static_cast<uint32_t>(insn);
}

void overisc_konata_back(int stage, unsigned char valid, int id) {
  state()->sample.b[stage] = {valid != 0, static_cast<uint32_t>(id)};
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
