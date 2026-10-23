// SPDX-License-Identifier: Apache-2.0
// Runs one program on the core (tb_core.sv) and requires it to pass (B4).
// CTest registers one run per riscv-test x seed x memory profile (sim/CMakeLists.txt).
//
// usage: test_core --elf <path> [--seed <n>] [--profile <name>] [--max-cycles <n>]
//                  [--fst <file>] [Catch2 options]
// The waveform can also be requested with the OVERISC_FST environment variable.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <string>

#include "Vtb_core.h"
#include "sim_run.h"

namespace {

struct Args {
  std::string elf;
  uint64_t seed = 1;
  std::string profile = "fast";
  uint64_t max_cycles = 1'000'000;
  std::string fst;
} g_args;

}  // namespace

TEST_CASE("program passes on the core", "[core]") {
  REQUIRE_FALSE(g_args.elf.empty());
  SimRunOptions opt;
  opt.plusargs = {
      "+elf=" + g_args.elf,
      "+seed=" + std::to_string(g_args.seed),
      "+mem_profile=" + g_args.profile,
      "+max_cycles=" + std::to_string(g_args.max_cycles),
      // randomize the initial value of every state element that is not reset
      "+verilator+rand+reset+2",
      "+verilator+seed+" + std::to_string(g_args.seed),
  };
  opt.max_cycles = g_args.max_cycles + 1000;  // the host enforces the real limit
  opt.fst = g_args.fst.empty() ? fst_from_env() : g_args.fst;

  const SimResult r = run_clocked<Vtb_core>(opt);
  INFO("elf=" << g_args.elf << " seed=" << g_args.seed << " profile=" << g_args.profile);
  INFO("result=" << to_string(r.kind) << " tohost=" << r.value << " cycles=" << r.cycles);
  if (r.kind == SimResultKind::kFail) INFO("failed test number " << (r.value >> 1));
  CHECK(r.kind == SimResultKind::kPass);
}

int main(int argc, char* argv[]) {
  Catch::Session session;
  using Catch::Clara::Opt;
  auto cli = session.cli() |
             Opt(g_args.elf, "path")["--elf"]("program to run") |
             Opt(g_args.seed, "n")["--seed"]("random seed (memory timing, initial values)") |
             Opt(g_args.profile, "name")["--profile"]("memory timing: fast|small|large|random") |
             Opt(g_args.max_cycles, "n")["--max-cycles"]("cycle limit") |
             Opt(g_args.fst, "file")["--fst"]("write an FST waveform");
  session.cli(cli);
  if (const int rc = session.applyCommandLine(argc, argv); rc != 0) return rc;
  return session.run();
}
