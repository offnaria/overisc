// SPDX-License-Identifier: Apache-2.0
// Glue test of overisc_sim_konata.sv: the scenario driven through the SV module and DPI must
// produce exactly the same log as KonataTracer called directly.
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <sstream>

#include "Vtb_konata.h"
#include "konata_label.h"
#include "konata_scenario.h"
#include "sim_state.h"

namespace {

// Packs fields MSB first, in the order of the packed struct overisc_trace_pkg::trace_t.
class Packer {
 public:
  void push(uint64_t value, int width) {
    for (int i = width - 1; i >= 0; --i) bits_.push_back((value >> i) & 1);
  }
  void slot(const TraceSlot& s) {
    push(s.valid, 1);
    push(s.id, 32);
  }
  template <size_t N>
  void store(VlWide<N>& w) const {
    for (size_t i = 0; i < N; ++i) w[i] = 0;
    const size_t n = bits_.size();
    for (size_t i = 0; i < n; ++i) {
      if (bits_[n - 1 - i]) w[i / 32] |= 1u << (i % 32);  // bit i = LSB-based position
    }
  }
  size_t size() const { return bits_.size(); }

 private:
  std::vector<bool> bits_;
};

Packer pack(const TraceSample& s) {
  Packer p;
  for (int k = TraceSample::kNumStages - 1; k >= 0; --k) p.slot(s.stage[k]);
  p.push(s.f0_pc, 64);
  p.push(s.f2_inst, 32);
  p.slot(s.retire.empty() ? TraceSlot{} : s.retire[0]);  // NRET = 1
  p.push(s.redirect, 1);
  p.push(static_cast<uint64_t>(s.redirect_stage), 3);
  return p;
}

std::string read_file(const std::string& path) {
  std::ifstream f(path);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("Kanata log through SV and DPI matches the direct writer", "[konata][verilator]") {
  const auto v = konata_test::scenario();

  std::ostringstream direct;
  {
    KonataTracer t(direct, konata_label);
    for (size_t c = 0; c < v.size(); ++c) t.sample(c, v[c]);
    t.finish();
  }

  char path[] = "/tmp/overisc_konata_XXXXXX";
  const int fd = mkstemp(path);
  REQUIRE(fd >= 0);
  close(fd);
  {
    sim_reset_state();
    auto ctx = std::make_unique<VerilatedContext>();
    const std::string arg = std::string("+konata=") + path;
    const char* argv[] = {"sim", arg.c_str()};
    ctx->commandArgs(2, argv);
    ctx->quiet(true);
    Vtb_konata top{ctx.get()};
    // $bits(trace_t): 8*33 + 64 + 32 + NRET*33 + 1 + 3 (NRET = 1)
    REQUIRE(pack(TraceSample{}).size() == 397);
    auto edge = [&] {
      top.clk = 0;
      top.eval();
      top.clk = 1;
      top.eval();
    };
    top.rst = 1;
    edge();
    edge();
    top.rst = 0;
    for (const auto& s : v) {
      pack(s).store(top.trace_in);
      edge();
    }
    top.final();
  }
  const std::string via_sv = read_file(path);
  std::remove(path);
  CHECK(via_sv == direct.str());
  CHECK_FALSE(via_sv.empty());
}
