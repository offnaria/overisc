// SPDX-License-Identifier: Apache-2.0
// DPI functions of the simulation models: the AXI memory ports (overisc_sim_axi_mem.sv) and
// the host side (overisc_sim_host.sv: ELF loading, tohost polling). All share one SimMemory.
//
// Initialization order: overisc_sim_host calls overisc_sim_init() at time 0; each
// overisc_sim_axi_mem instance registers itself with overisc_sim_axi_new() and picks up the
// seed and timing profile when it is reset (reset is asserted after time 0).
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "axi_slave.h"
#include "elf_loader.h"
#include "sim_memory.h"
#include "sim_state.h"
#include "svdpi.h"

namespace {

struct State {
  SimMemory mem;
  std::vector<std::unique_ptr<AxiSlave>> ports;
  uint64_t seed = 1;
  std::string profile = "fast";
  SimResult result;
};

State& state() {
  static State s;
  return s;
}

void unpack(const svBitVecVal* v, uint8_t* bytes, unsigned n) {
  for (unsigned i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(v[i / 4] >> (8 * (i % 4)));
}

void pack(const uint8_t* bytes, svBitVecVal* v, unsigned n) {
  for (unsigned i = 0; i < (n + 3) / 4; ++i) v[i] = 0;
  for (unsigned i = 0; i < n; ++i) v[i / 4] |= svBitVecVal{bytes[i]} << (8 * (i % 4));
}

}  // namespace

void sim_reset_state() { state() = State{}; }

const SimResult& sim_result() { return state().result; }

SimMemory& sim_memory() { return state().mem; }

std::string to_string(SimResultKind kind) {
  switch (kind) {
    case SimResultKind::kPass:
      return "PASS";
    case SimResultKind::kFail:
      return "FAIL";
    case SimResultKind::kTimeout:
      return "TIMEOUT";
    case SimResultKind::kError:
      return "ERROR";
    default:
      return "NONE";
  }
}

extern "C" {

// Returns 0 on success, -1 for an unknown profile.
int overisc_sim_init(long long seed, const char* profile, long long dram_base,
                     long long dram_size) {
  State& s = state();
  SimRng rng(static_cast<uint64_t>(seed));
  AxiTiming t;
  if (!AxiTiming::from_profile(profile, rng, &t)) {
    std::fprintf(stderr, "overisc_sim_init: unknown memory profile '%s'\n", profile);
    return -1;
  }
  s.seed = static_cast<uint64_t>(seed);
  s.profile = profile;
  s.mem.add_region(static_cast<uint64_t>(dram_base), static_cast<uint64_t>(dram_size));
  return 0;
}

int overisc_sim_axi_new(const char* name, int data_bytes) {
  State& s = state();
  s.ports.push_back(
      std::make_unique<AxiSlave>(name, s.mem, static_cast<unsigned>(data_bytes)));
  return static_cast<int>(s.ports.size() - 1);
}

void overisc_sim_axi_reset(int handle) {
  State& s = state();
  // Each port gets its own stream so adding a port does not perturb the others.
  uint64_t x = s.seed ^ (0x9e3779b97f4a7c15ull * (static_cast<uint64_t>(handle) + 1));
  const uint64_t port_seed = SimRng::splitmix64(x);
  SimRng rng(port_seed);
  AxiTiming t;
  AxiTiming::from_profile(s.profile, rng, &t);
  s.ports[static_cast<size_t>(handle)]->reset(port_seed, t);
}

// Returns 0 on success, -1 on a protocol violation (message printed to stderr).
int overisc_sim_axi_step(int handle,
                         svBit aw_valid, int aw_id, long long aw_addr, int aw_len, int aw_size,
                         int aw_burst,
                         svBit w_valid, const svBitVecVal* w_data, int w_strb, svBit w_last,
                         svBit b_ready,
                         svBit ar_valid, int ar_id, long long ar_addr, int ar_len, int ar_size,
                         int ar_burst,
                         svBit r_ready,
                         svBit* aw_ready, svBit* w_ready, svBit* ar_ready,
                         svBit* b_valid, int* b_id, int* b_resp,
                         svBit* r_valid, int* r_id, svBitVecVal* r_data, int* r_resp,
                         svBit* r_last) {
  AxiSlave& p = *state().ports[static_cast<size_t>(handle)];
  AxiSlaveIn in;
  in.aw_valid = aw_valid;
  in.aw = {static_cast<uint32_t>(aw_id), static_cast<uint64_t>(aw_addr),
           static_cast<uint8_t>(aw_len), static_cast<uint8_t>(aw_size),
           static_cast<uint8_t>(aw_burst)};
  in.w_valid = w_valid;
  unpack(w_data, in.w_data, kAxiMaxDataBytes);
  in.w_strb = static_cast<uint32_t>(w_strb);
  in.w_last = w_last;
  in.b_ready = b_ready;
  in.ar_valid = ar_valid;
  in.ar = {static_cast<uint32_t>(ar_id), static_cast<uint64_t>(ar_addr),
           static_cast<uint8_t>(ar_len), static_cast<uint8_t>(ar_size),
           static_cast<uint8_t>(ar_burst)};
  in.r_ready = r_ready;

  if (!p.step(in)) {
    std::fprintf(stderr, "AXI protocol violation: %s\n", p.error().c_str());
    return -1;
  }

  const AxiSlaveOut& o = p.out();
  *aw_ready = o.aw_ready;
  *w_ready = o.w_ready;
  *ar_ready = o.ar_ready;
  *b_valid = o.b_valid;
  *b_id = static_cast<int>(o.b_id);
  *b_resp = o.b_resp;
  *r_valid = o.r_valid;
  *r_id = static_cast<int>(o.r_id);
  pack(o.r_data, r_data, kAxiMaxDataBytes);
  *r_resp = o.r_resp;
  *r_last = o.r_last;
  return 0;
}

// Loads an ELF file. *tohost is -1 if the symbol is missing. Returns 0 on success.
int overisc_sim_load_elf(const char* path, long long* entry, long long* tohost) {
  ElfImage image;
  std::string err;
  if (!load_elf(path, state().mem, &image, &err)) {
    std::fprintf(stderr, "overisc_sim_load_elf: %s\n", err.c_str());
    return -1;
  }
  *entry = static_cast<long long>(image.entry);
  const auto it = image.symbols.find("tohost");
  *tohost = it == image.symbols.end() ? -1 : static_cast<long long>(it->second);
  return 0;
}

// Outcome of the run, reported by the SV side (kind: SimResultKind). The first report wins.
void overisc_sim_report(int kind, long long value, long long cycles) {
  SimResult& r = state().result;
  if (r.kind != SimResultKind::kNone) return;
  r.kind = static_cast<SimResultKind>(kind);
  r.value = static_cast<uint64_t>(value);
  r.cycles = static_cast<uint64_t>(cycles);
}

// Backdoor access for testbenches (ELF loading, tohost polling, checks).
void overisc_sim_mem_write64(long long addr, long long data) {
  uint8_t b[8];
  std::memcpy(b, &data, 8);
  state().mem.write(static_cast<uint64_t>(addr), b, 8);
}

long long overisc_sim_mem_read64(long long addr) {
  uint8_t b[8];
  state().mem.read(static_cast<uint64_t>(addr), b, 8);
  long long v;
  std::memcpy(&v, b, 8);
  return v;
}

}  // extern "C"
