// SPDX-License-Identifier: Apache-2.0
// Unit tests of overisc_reg_slice and overisc_skid_buffer (rtl/common) through tb_pipe_buf.sv.
//
// A random producer (obeying the ready/valid rules) and a random consumer move numbered items
// through the buffer. Checked:
//   - every item comes out once, in order
//   - the buffer obeys the rules on its output: valid is held until ready, data stays stable
//   - nothing comes out of reset
//   - full throughput: one item per cycle when both sides are always willing
//   - skid buffer only: in_ready does not depend combinationally on out_ready
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <deque>
#include <memory>

#include "Vtb_reg_slice.h"
#include "Vtb_skid_buffer.h"
#include "sim_rng.h"

namespace {

template <class Model>
struct Bench {
  std::unique_ptr<VerilatedContext> ctx = std::make_unique<VerilatedContext>();
  Model top{ctx.get()};

  Bench() {
    top.in_valid = 0;
    top.out_ready = 0;
    top.rst = 1;
    for (int i = 0; i < 3; ++i) edge();
    top.rst = 0;
  }
  // Rising edge; inputs must be set beforehand.
  void edge() {
    top.clk = 0;
    top.eval();
    top.clk = 1;
    top.eval();
  }
  ~Bench() { top.final(); }
};

struct Stats {
  uint64_t sent = 0, received = 0;
};

// Runs random traffic for `cycles`; returns counts. Fails the test on any violation.
template <class Model>
Stats run_random(uint64_t seed, unsigned in_pct, unsigned out_pct, uint64_t cycles) {
  Bench<Model> b;
  SimRng rng(seed);
  std::deque<uint32_t> expected;
  uint32_t next = 0;
  bool in_valid = false;
  uint32_t in_data = 0;
  bool prev_out_valid = false, prev_out_ready = false;
  uint32_t prev_out_data = 0;
  Stats st;

  for (uint64_t c = 0; c < cycles; ++c) {
    // producer: raise valid with new data only when idle; hold it until accepted
    if (!in_valid && rng.chance(in_pct)) {
      in_valid = true;
      in_data = next++;
    }
    const bool out_ready = rng.chance(out_pct);
    b.top.in_valid = in_valid;
    b.top.in_data = in_data;
    b.top.out_ready = out_ready;
    b.top.clk = 0;
    b.top.eval();

    // output rules (checked on the values seen before this edge)
    const bool out_valid = b.top.out_valid;
    const uint32_t out_data = b.top.out_data;
    if (prev_out_valid && !prev_out_ready) {
      INFO("cycle " << c);
      REQUIRE(out_valid);  // valid dropped before ready
      REQUIRE(out_data == prev_out_data);  // data changed before ready
    }
    const bool in_hs = in_valid && b.top.in_ready;
    const bool out_hs = out_valid && out_ready;
    if (out_hs) {
      INFO("cycle " << c);
      REQUIRE_FALSE(expected.empty());  // output without input
      REQUIRE(out_data == expected.front());  // lost, duplicated or reordered
      expected.pop_front();
      st.received++;
    }
    if (in_hs) {
      expected.push_back(in_data);
      in_valid = false;
      st.sent++;
    }
    prev_out_valid = out_valid;
    prev_out_ready = out_ready;
    prev_out_data = out_data;
    b.edge();
  }
  // drain
  b.top.in_valid = 0;
  b.top.out_ready = 1;
  for (int i = 0; i < 8 && !expected.empty(); ++i) {
    b.top.clk = 0;
    b.top.eval();
    if (b.top.out_valid) {
      REQUIRE(b.top.out_data == expected.front());
      expected.pop_front();
      st.received++;
    }
    b.edge();
  }
  CHECK(expected.empty());
  return st;
}

template <class Model>
void check_reset_and_throughput() {
  Bench<Model> b;
  b.top.clk = 0;
  b.top.eval();
  CHECK_FALSE(b.top.out_valid);  // nothing out of reset

  // both sides always willing: after a short start, one item per cycle
  uint32_t next = 0, got = 0;
  int handshakes_late = 0;
  for (int c = 0; c < 40; ++c) {
    b.top.in_valid = 1;
    b.top.in_data = next;
    b.top.out_ready = 1;
    b.top.clk = 0;
    b.top.eval();
    if (b.top.in_ready) next++;
    if (b.top.out_valid) {
      REQUIRE(b.top.out_data == got);
      got++;
      if (c >= 4) handshakes_late++;
    }
    b.edge();
  }
  CHECK(handshakes_late == 36);
}

}  // namespace

TEMPLATE_TEST_CASE("pipe buffer passes random traffic in order", "[pipe]", Vtb_reg_slice,
                   Vtb_skid_buffer) {
  const uint64_t seed = GENERATE(range(1, 4));
  const unsigned in_pct = GENERATE(30u, 70u, 100u);
  const unsigned out_pct = GENERATE(30u, 70u, 100u);
  CAPTURE(seed, in_pct, out_pct);
  const Stats st = run_random<TestType>(seed, in_pct, out_pct, 20000);
  CHECK(st.sent > 1000);
  CHECK(st.received == st.sent);
}

TEMPLATE_TEST_CASE("pipe buffer comes out of reset empty and runs at full throughput", "[pipe]",
                   Vtb_reg_slice, Vtb_skid_buffer) {
  check_reset_and_throughput<TestType>();
}

TEST_CASE("skid buffer: in_ready does not depend combinationally on out_ready", "[pipe]") {
  Bench<Vtb_skid_buffer> b;
  SimRng rng(7);
  for (int c = 0; c < 2000; ++c) {
    b.top.in_valid = rng.chance(60);
    b.top.in_data = c;
    b.top.out_ready = 0;
    b.top.clk = 0;
    b.top.eval();
    const bool ready_when_blocked = b.top.in_ready;
    b.top.out_ready = 1;
    b.top.eval();
    INFO("cycle " << c);
    REQUIRE(b.top.in_ready == ready_when_blocked);
    b.top.out_ready = rng.chance(50);
    b.top.eval();
    b.edge();
  }
}
