// SPDX-License-Identifier: Apache-2.0
// Unit test of the Kanata log writer on the scenario in konata_scenario.h.
#include <catch2/catch_test_macros.hpp>
#include <sstream>

#include "konata_label.h"
#include "konata_scenario.h"

using Stages = std::vector<std::pair<std::string, uint64_t>>;

namespace {

struct Run {
  std::string text;
  konata_test::ParsedLog log;
  uint64_t violations;
};

Run run(const std::vector<TraceSample>& v) {
  std::ostringstream out, warn;
  KonataTracer t(out, konata_label, &warn);
  for (size_t c = 0; c < v.size(); ++c) t.sample(c, v[c]);
  t.finish();
  return {out.str(), konata_test::parse(out.str()), t.violations()};
}

TraceSample at(int stage, uint32_t id, uint64_t pc = 0x80000000) {
  TraceSample s;
  s.stage[stage] = {true, id};
  s.f0_pc = pc;
  return s;
}

}  // namespace

TEST_CASE("Kanata log follows the format rules", "[konata]") {
  const Run r = run(konata_test::scenario());
  INFO(r.text);
  CHECK(r.violations == 0);
  CHECK(r.log.errors.empty());
  CHECK(r.log.insns.size() == 8);
}

TEST_CASE("Kanata log has the expected stages", "[konata]") {
  const Run r = run(konata_test::scenario());
  INFO(r.text);
  REQUIRE(r.log.insns.size() == 8);
  const auto& i = r.log.insns;

  // I0: straight through; FB is not reported
  CHECK(i.at(0).stages == Stages{{"F0", 0}, {"F1", 1}, {"F2", 2}, {"D", 3}, {"X", 4}, {"M", 5}, {"W", 6}});
  CHECK(i.at(0).retire_type == 0);
  CHECK(i.at(0).retire_cycle == 7);
  // I1: waits one cycle in X
  CHECK(i.at(1).stages == Stages{{"F0", 1}, {"F1", 2}, {"F2", 3}, {"D", 4}, {"X", 5}, {"M", 7}, {"W", 8}});
  CHECK(i.at(1).retire_cycle == 9);
  // I2: the jump, waits in FB
  CHECK(i.at(2).stages == Stages{{"F0", 2}, {"F1", 3}, {"F2", 4}, {"FB", 5}, {"D", 6}, {"X", 7}, {"M", 8}, {"W", 9}});
  CHECK(i.at(2).retire_cycle == 10);
  // I3..I6: flushed by the jump in D, FB, F2, F1
  CHECK(i.at(3).stages == Stages{{"F0", 3}, {"F1", 4}, {"F2", 5}, {"FB", 6}, {"D", 7}});
  CHECK(i.at(4).stages == Stages{{"F0", 4}, {"F1", 5}, {"F2", 6}, {"FB", 7}});
  CHECK(i.at(5).stages == Stages{{"F0", 5}, {"F1", 6}, {"F2", 7}});
  CHECK(i.at(6).stages == Stages{{"F0", 6}, {"F1", 7}});
  for (int k = 3; k <= 6; ++k) {
    CHECK(i.at(k).retire_type == 1);
    CHECK(i.at(k).retire_cycle == 8);
  }
  // I7: the jump target
  CHECK(i.at(7).stages == Stages{{"F0", 8}, {"F1", 9}, {"F2", 10}, {"D", 11}, {"X", 12}, {"M", 13}, {"W", 14}});
  CHECK(i.at(7).retire_type == 0);
  CHECK(i.at(7).retire_cycle == 15);
  CHECK(i.at(7).sim_id == 7);
}

TEST_CASE("Kanata labels show the PC and, from F2 on, the instruction", "[konata]") {
  const Run r = run(konata_test::scenario());
  REQUIRE(r.log.insns.size() == 8);
  CHECK(r.log.insns.at(0).label.rfind("80000000: ", 0) == 0);
  CHECK(r.log.insns.at(6).label == "80000018: ");  // flushed in F1, before its bits are known
#ifdef OVERISC_HAVE_SPIKE_DISASM
  CHECK(r.log.insns.at(0).label == "80000000: addi    a0, a0, 1");
#endif
}

TEST_CASE("Kanata writer handles an empty run", "[konata]") {
  std::ostringstream out;
  KonataTracer t(out, konata_label);
  t.finish();
  CHECK(out.str().empty());
}

TEST_CASE("Kanata log is the same for level and pulse reports", "[konata]") {
  const Run level = run(konata_test::scenario());
  const Run pulse = run(konata_test::pulses());
  CHECK(pulse.violations == 0);
  CHECK(pulse.text == level.text);
}

TEST_CASE("Kanata log: a gap between stage reports does not flush", "[konata]") {
  const Run r = run({at(konata_test::kF0, 0), at(konata_test::kF1, 0), TraceSample{}, at(konata_test::kF2, 0)});
  CHECK(r.violations == 0);
  CHECK(r.log.errors.empty());
  REQUIRE(r.log.insns.size() == 1);
  CHECK(r.log.insns.at(0).stages == Stages{{"F0", 0}, {"F1", 1}, {"F2", 3}});
}

TEST_CASE("Kanata log: a redirect flushes only the stages before its source", "[konata]") {
  using namespace konata_test;
  TraceSample c1 = at(kF0, 1);
  c1.stage[kF1] = {true, 0};
  c1.redirect = true;
  c1.redirect_stage = kF1;  // I1 in F0 is flushed, I0 in F1 stays
  const Run r = run({at(kF0, 0), c1, at(kF2, 0)});
  CHECK(r.violations == 0);
  REQUIRE(r.log.insns.size() == 2);
  CHECK(r.log.insns.at(1).retire_type == 1);
  CHECK(r.log.insns.at(1).retire_cycle == 2);
  CHECK(r.log.insns.at(0).stages.back().first == "F2");
}

TEST_CASE("Kanata writer writes the log while running", "[konata]") {
  std::ostringstream out;
  KonataTracer t(out, konata_label);
  t.sample(0, at(konata_test::kF0, 0));
  t.sample(1, TraceSample{});
  CHECK(out.str().find("I\t0") != std::string::npos);
}

TEST_CASE("Kanata writer reports IDs reported after a flush", "[konata]") {
  using namespace konata_test;
  TraceSample c0 = at(kF0, 0);
  c0.redirect = true;
  c0.redirect_stage = kF1;  // flushes I0
  const Run r = run({c0, at(kF1, 0)});
  CHECK(r.violations == 1);
  CHECK(r.log.insns.size() == 1);
}

TEST_CASE("Kanata writer reports instructions reported after retiring", "[konata]") {
  std::vector<TraceSample> v = konata_test::scenario();
  v.push_back(at(konata_test::kW, 7));  // I7 retired at cycle 14
  const Run r = run(v);
  CHECK(r.violations == 1);
  CHECK(r.log.errors.empty());
  CHECK(r.log.insns.size() == 8);
}

TEST_CASE("Kanata writer reports instructions first seen after F0", "[konata]") {
  const Run r = run({at(konata_test::kX, 9)});
  CHECK(r.violations == 1);
  CHECK(r.log.insns.empty());
}

TEST_CASE("Kanata writer reports instructions moving back", "[konata]") {
  using namespace konata_test;
  const Run r = run({at(kF0, 0), at(kF2, 0), at(kF1, 0)});
  CHECK(r.violations == 1);
  REQUIRE(r.log.insns.size() == 1);
  CHECK(r.log.errors.empty());
}

TEST_CASE("Kanata writer reports a younger instruction overtaking an older one", "[konata]") {
  using namespace konata_test;
  const Run r = run({at(kF0, 0), at(kF0, 1), at(kF1, 1)});  // I1 to F1 while I0 is in F0
  CHECK(r.violations == 1);
  CHECK(r.log.errors.empty());
}

TEST_CASE("Kanata writer reports IDs out of order", "[konata]") {
  using namespace konata_test;
  const Run r = run({at(kF0, 5), at(kF0, 3)});
  CHECK(r.violations == 1);
  CHECK(r.log.insns.size() == 1);
}

TEST_CASE("Kanata writer reports retire of an instruction not in flight", "[konata]") {
  TraceSample s;
  s.retire.push_back({true, 3});
  CHECK(run({s}).violations == 1);
}

TEST_CASE("Kanata writer stays fast with many instructions in flight", "[konata]") {
  // nothing retires yet (a core without a back end), so instructions pile up until the end
  using namespace konata_test;
  std::ostringstream out, warn;
  KonataTracer t(out, konata_label, &warn);
  const uint32_t n = 200000;
  for (uint32_t c = 0; c < n + 2; ++c) {
    TraceSample s;
    if (c < n) s.stage[kF0] = {true, c};
    if (c >= 1 && c - 1 < n) s.stage[kF1] = {true, c - 1};
    if (c >= 2 && c - 2 < n) s.stage[kF2] = {true, c - 2};
    s.f0_pc = 0x80000000 + 4 * c;
    t.sample(c, s);
  }
  t.finish();
  CHECK(t.violations() == 0);
  CHECK(parse(out.str()).insns.size() == n);
}
