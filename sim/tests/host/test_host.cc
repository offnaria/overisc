// SPDX-License-Identifier: Apache-2.0
// Test of overisc_sim_host (B3) without a core: the top is the host module itself, and the
// "program" is C++ writing tohost through the memory backdoor.
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "Vovl_host.h"
#include "Vovl_host_rv1000.h"
#include "sim_run.h"

namespace {

const std::string kElf = std::string(RVTESTS_DIR) + "/rv64ui-p-simple";

// Runs the host until $finish; writes `value` to tohost after `at` cycles (0: never).
template <class Model = Vovl_host>
SimResult run_host(uint64_t value, uint64_t at, std::vector<std::string> plusargs) {
  auto ctx = std::make_unique<VerilatedContext>();
  sim_reset_state();
  std::vector<const char*> argv{"sim"};
  for (const auto& a : plusargs) argv.push_back(a.c_str());
  ctx->commandArgs(static_cast<int>(argv.size()), argv.data());
  ctx->quiet(true);
  Model top{ctx.get()};
  top.rst = 1;
  for (uint64_t c = 0; c < 2'000'000 && !ctx->gotFinish(); ++c) {
    if (c == 3) top.rst = 0;
    if (value != 0 && c == at) {
      // tohost of rv64ui-p-simple (0x80001000, see the ELF test)
      const uint8_t* b = reinterpret_cast<const uint8_t*>(&value);
      sim_memory().write(0x80001000, b, 8);
    }
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
  }
  top.final();
  return sim_result();
}

}  // namespace

TEST_CASE("host reports PASS when tohost is 1", "[host]") {
  const SimResult r = run_host(1, 100, {"+elf=" + kElf});
  CHECK(r.kind == SimResultKind::kPass);
}

TEST_CASE("host reports FAIL with the test number", "[host]") {
  const SimResult r = run_host((3 << 1) | 1, 100, {"+elf=" + kElf});
  CHECK(r.kind == SimResultKind::kFail);
  CHECK((r.value >> 1) == 3);
}

TEST_CASE("host reports TIMEOUT", "[host]") {
  const SimResult r = run_host(0, 0, {"+elf=" + kElf, "+max_cycles=500"});
  CHECK(r.kind == SimResultKind::kTimeout);
  CHECK(r.cycles == 500);
}

TEST_CASE("host reports ERROR for a missing ELF", "[host]") {
  CHECK(run_host(0, 0, {"+elf=/nonexistent"}).kind == SimResultKind::kError);
  CHECK(run_host(0, 0, {}).kind == SimResultKind::kError);
}

TEST_CASE("host reports ERROR for an unknown memory profile", "[host]") {
  CHECK(run_host(0, 0, {"+elf=" + kElf, "+mem_profile=nope"}).kind == SimResultKind::kError);
}

TEST_CASE("host reports ERROR when the entry point differs from the reset vector", "[host]") {
  // Vovl_host_rv1000 is the same module built with RESET_VECTOR = 0x1000
  CHECK(run_host<Vovl_host_rv1000>(0, 0, {"+elf=" + kElf}).kind == SimResultKind::kError);
}
