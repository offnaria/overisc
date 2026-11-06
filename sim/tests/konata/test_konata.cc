// SPDX-License-Identifier: Apache-2.0
// Unit test of the Kanata log writer on the scenario in konata_scenario.h.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "konata_label.h"
#include "konata_scenario.h"

using konata_test::ParsedInsn;
using Stages = std::vector<std::pair<std::string, uint64_t>>;

namespace {

std::string run_scenario() {
  std::ostringstream out;
  KonataTracer t(out, konata_label);
  const auto v = konata_test::scenario();
  for (size_t c = 0; c < v.size(); ++c) t.sample(c, v[c]);
  t.finish();
  return out.str();
}

}  // namespace

TEST_CASE("Kanata log follows the format rules", "[konata]") {
  std::ostringstream out, warn;
  KonataTracer t(out, konata_label, &warn);
  const auto v = konata_test::scenario();
  for (size_t c = 0; c < v.size(); ++c) t.sample(c, v[c]);
  t.finish();
  CHECK(t.violations() == 0);
  CHECK(warn.str().empty());
  const auto log = konata_test::parse(run_scenario());
  INFO(run_scenario());
  CHECK(log.errors.empty());
  CHECK(log.insns.size() == 7);  // 5 instructions + 2 flushed fetch blocks
}

TEST_CASE("Kanata log has the expected stages", "[konata]") {
  const std::string text = run_scenario();
  const auto log = konata_test::parse(text);
  INFO(text);
  REQUIRE(log.insns.size() == 7);
  const auto& i = log.insns;

  // I0: straight through; it never waits in the Fetch Buffer
  CHECK(i.at(0).stages == Stages{{"F0", 0}, {"F1", 1}, {"F2", 2}, {"D", 3}, {"X", 4},
                                 {"M", 5}, {"W", 6}});
  CHECK(i.at(0).retire_type == 0);
  CHECK(i.at(0).retire_cycle == 7);
  // I1: same block, waits one cycle in FB and one in X
  CHECK(i.at(1).stages == Stages{{"F0", 0}, {"F1", 1}, {"F2", 2}, {"FB", 3}, {"D", 4},
                                 {"X", 5}, {"M", 7}, {"W", 8}});
  CHECK(i.at(1).retire_cycle == 9);
  // I2: the jump
  CHECK(i.at(2).stages == Stages{{"F0", 1}, {"F1", 2}, {"F2", 3}, {"FB", 4}, {"D", 5},
                                 {"X", 7}, {"M", 8}, {"W", 9}});
  CHECK(i.at(2).retire_cycle == 10);
  // I3: flushed in D
  CHECK(i.at(3).stages == Stages{{"F0", 1}, {"F1", 2}, {"F2", 3}, {"FB", 4}, {"D", 7}});
  CHECK(i.at(3).retire_type == 1);
  CHECK(i.at(3).retire_cycle == 8);
  // B2: flushed in the Fetch Buffer, B3: flushed in F2
  CHECK(i.at(4).label.find("80000010: fetch block") != std::string::npos);
  CHECK(i.at(4).stages == Stages{{"F0", 2}, {"F1", 3}, {"F2", 4}, {"FB", 5}});
  CHECK(i.at(4).retire_type == 1);
  CHECK(i.at(4).retire_cycle == 8);
  CHECK(i.at(5).label.find("80000018: fetch block") != std::string::npos);
  CHECK(i.at(5).stages == Stages{{"F0", 3}, {"F1", 4}, {"F2", 5}});
  CHECK(i.at(5).retire_type == 1);
  CHECK(i.at(5).retire_cycle == 8);
  // I4: from the jump target
  CHECK(i.at(6).stages == Stages{{"F0", 8}, {"F1", 9}, {"F2", 10}, {"D", 11}, {"X", 12},
                                 {"M", 13}, {"W", 14}});
  CHECK(i.at(6).retire_cycle == 15);
  CHECK(i.at(6).sim_id == 4);
}

TEST_CASE("Kanata labels show PC and instruction", "[konata]") {
  const auto log = konata_test::parse(run_scenario());
  REQUIRE(log.insns.size() == 7);
  CHECK(log.insns.at(0).label.rfind("80000000: ", 0) == 0);
  CHECK(log.insns.at(2).label.rfind("80000008: ", 0) == 0);
#ifdef OVERISC_HAVE_SPIKE_DISASM
  CHECK(log.insns.at(0).label == "80000000: addi    a0, a0, 1");
#endif
}

TEST_CASE("Kanata writer handles an empty run", "[konata]") {
  std::ostringstream out;
  KonataTracer t(out, konata_label);
  t.finish();
  CHECK(out.str().empty());
}

namespace {

// Runs samples and returns (log, violation count).
std::pair<konata_test::ParsedLog, uint64_t> run(const std::vector<TraceSample>& v) {
  std::ostringstream out, warn;
  KonataTracer t(out, konata_label, &warn);
  for (size_t c = 0; c < v.size(); ++c) t.sample(c, v[c]);
  t.finish();
  return {konata_test::parse(out.str()), t.violations()};
}

TraceSample fetch(int stage, uint32_t fid, uint64_t pc = 0x80000000) {
  TraceSample s;
  s.f[stage] = {true, fid};
  s.f0_pc = pc;
  return s;
}

}  // namespace

TEST_CASE("Kanata writer reports handshake pulses instead of occupancy", "[konata]") {
  // block 0: F0, F1 as a one-cycle pulse, nothing, then F2 (what the trace looked like when
  // driven by handshakes); it must not reappear as a second entry
  std::vector<TraceSample> v = {fetch(0, 0), fetch(1, 0), TraceSample{}, fetch(2, 0)};
  const auto [log, n] = run(v);
  CHECK(n == 1);
  CHECK(log.errors.empty());
  CHECK(log.insns.size() == 1);
}

TEST_CASE("Kanata writer reports blocks first seen after F0", "[konata]") {
  const auto [log, n] = run({fetch(1, 5)});
  CHECK(n == 1);
  CHECK(log.insns.empty());
}

TEST_CASE("Kanata writer reports blocks moving back", "[konata]") {
  const auto [log, n] = run({fetch(0, 0), fetch(2, 0), fetch(1, 0)});
  CHECK(n == 1);
  REQUIRE(log.insns.size() == 1);
  CHECK(log.errors.empty());
}

TEST_CASE("Kanata writer reports an older block behind a younger one", "[konata]") {
  TraceSample c1 = fetch(0, 1);  // block 1 in F0, block 0 in F1
  c1.f[1] = {true, 0};
  TraceSample c2;  // block 1 in F2, block 0 in F1: block 1 overtook block 0
  c2.f[1] = {true, 0};
  c2.f[2] = {true, 1};
  const auto [log, n] = run({fetch(0, 0), c1, c2});
  CHECK(n == 1);
  CHECK(log.errors.empty());
  CHECK(log.insns.size() == 2);
}

TEST_CASE("Kanata writer reports instructions shown again after retiring", "[konata]") {
  std::vector<TraceSample> v = konata_test::scenario();
  TraceSample again;
  again.b[2] = {true, 4};  // I4 retired at cycle 14
  v.push_back(again);
  const auto [log, n] = run(v);
  CHECK(n == 1);
  CHECK(log.errors.empty());
  CHECK(log.insns.size() == 7);
}

TEST_CASE("Kanata writer reports instructions in X that never were in D", "[konata]") {
  TraceSample s;
  s.b[0] = {true, 9};
  const auto [log, n] = run({s});
  CHECK(n == 1);
  CHECK(log.insns.empty());
}
