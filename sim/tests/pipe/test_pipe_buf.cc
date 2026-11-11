// SPDX-License-Identifier: Apache-2.0
// Unit tests of overisc_reg_slice, overisc_skid_buffer (rtl/common) and overisc_fetch_buffer
// (rtl/core) through tb_pipe_buf.sv.
//
// A random producer (obeying the ready/valid rules) and a random consumer move numbered items
// through the buffer, with random kills. Checked:
//   - items come out once and in order; after a kill, no item from before it comes out
//   - kill semantics at the edge where kill is high: an item taken at the output is delivered,
//     an item accepted at the input is dropped, and the output is empty in the next cycle
//   - the buffer obeys the rules on its output: valid is held until ready, data stays stable
//     (except for a kill)
//   - nothing comes out of reset; full throughput when both sides are always willing
//   - skid buffer and fetch buffer: in_ready does not depend combinationally on out_ready
//   - fetch buffer: its trace output names the item that is in its output in the next cycle
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <deque>
#include <memory>

#include "Vtb_fetch_buffer.h"
#include "Vtb_fetch_buffer16.h"
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
    top.kill = 0;
    top.rst = 1;
    for (int i = 0; i < 3; ++i) edge();
    top.rst = 0;
  }
  // Settle the combinational outputs for the current inputs (before the rising edge).
  void settle() {
    top.clk = 0;
    top.eval();
  }
  // Rising edge; inputs must be set beforehand.
  void edge() {
    settle();
    top.clk = 1;
    top.eval();
  }
  ~Bench() { top.final(); }
};

struct Stats {
  uint64_t sent = 0, received = 0, kills = 0, dropped = 0;
};

// Runs random traffic for `cycles`; returns counts. Fails the test on any violation.
template <class Model>
Stats run_random(uint64_t seed, unsigned in_pct, unsigned out_pct, unsigned kill_pct,
                 uint64_t cycles, bool check_trace) {
  Bench<Model> b;
  SimRng rng(seed);
  std::deque<uint32_t> expected;  // items inside the buffer, oldest first
  uint32_t next = 0;
  bool in_valid = false;
  uint32_t in_data = 0;
  bool prev_out_valid = false, prev_out_ready = false, prev_kill = false;
  uint32_t prev_out_data = 0;
  bool prev_trace_valid = false;
  uint32_t prev_trace_id = 0;
  Stats st;

  for (uint64_t c = 0; c < cycles; ++c) {
    // producer: raise valid with new data only when idle; hold it until accepted
    if (!in_valid && rng.chance(in_pct)) {
      in_valid = true;
      in_data = next++;
    }
    const bool out_ready = rng.chance(out_pct);
    const bool kill = rng.chance(kill_pct);
    b.top.in_valid = in_valid;
    b.top.in_data = in_data;
    b.top.out_ready = out_ready;
    b.top.kill = kill;
    b.settle();

    const bool out_valid = b.top.out_valid;
    const uint32_t out_data = b.top.out_data;
    INFO("cycle " << c);
    if (out_valid) REQUIRE(b.top.out_aux == static_cast<uint32_t>(~out_data));  // fields mixed up
    if (prev_kill) REQUIRE_FALSE(out_valid);                                    // the output must be empty after a kill
    if (prev_out_valid && !prev_out_ready && !prev_kill) {
      REQUIRE(out_valid);                  // valid dropped before ready
      REQUIRE(out_data == prev_out_data);  // data changed before ready
    }
    if (check_trace) {
      // the trace output of the previous cycle names what is in the output now
      REQUIRE(out_valid == prev_trace_valid);
      if (out_valid) REQUIRE(out_data == prev_trace_id);
    }

    // handshakes at this edge
    const bool in_hs = in_valid && b.top.in_ready;
    const bool out_hs = out_valid && out_ready;
    if (out_hs) {
      REQUIRE_FALSE(expected.empty());        // output without input, or from before a kill
      REQUIRE(out_data == expected.front());  // lost, duplicated or reordered
      expected.pop_front();
      st.received++;
    }
    if (in_hs) {
      expected.push_back(in_data);
      in_valid = false;
      st.sent++;
    }
    if (kill) {
      st.kills++;
      st.dropped += expected.size();
      expected.clear();  // everything inside, including the item accepted at this edge
    }

    prev_out_valid = out_valid;
    prev_out_ready = out_ready;
    prev_out_data = out_data;
    prev_kill = kill;
    prev_trace_valid = b.top.trace_valid;
    prev_trace_id = b.top.trace_id;
    b.edge();
  }
  // drain
  b.top.in_valid = 0;
  b.top.out_ready = 1;
  b.top.kill = 0;
  for (int i = 0; i < 40 && !expected.empty(); ++i) {
    b.settle();
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
  b.settle();
  CHECK_FALSE(b.top.out_valid);  // nothing out of reset

  // both sides always willing: after a short start, one item per cycle
  uint32_t next = 0, got = 0;
  int handshakes_late = 0;
  for (int c = 0; c < 40; ++c) {
    b.top.in_valid = 1;
    b.top.in_data = next;
    b.top.out_ready = 1;
    b.settle();
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

// Fill the buffer with the output blocked, kill, then check that only new items come out and
// that it accepts input again.
template <class Model>
void check_kill_when_full() {
  Bench<Model> b;
  uint32_t next = 0;
  for (int c = 0; c < 40; ++c) {
    b.top.in_valid = 1;
    b.top.in_data = next;
    b.top.out_ready = 0;
    b.settle();
    if (b.top.in_ready) next++;
    b.edge();
  }
  b.settle();
  REQUIRE_FALSE(b.top.in_ready);  // full
  REQUIRE(b.top.out_valid);
  b.top.in_valid = 0;
  b.top.kill = 1;
  b.edge();
  b.top.kill = 0;
  b.settle();
  CHECK_FALSE(b.top.out_valid);
  // input accepted again within a cycle (the ready of the buffers is registered)
  b.edge();
  b.settle();
  CHECK(b.top.in_ready);
  // a new item passes, nothing older does
  const uint32_t fresh = 1000;
  b.top.in_valid = 1;
  b.top.in_data = fresh;
  b.top.out_ready = 1;
  bool seen = false;
  for (int c = 0; c < 10; ++c) {
    b.settle();
    if (b.top.out_valid) {
      CHECK(b.top.out_data == fresh);
      seen = true;
      break;
    }
    if (b.top.in_ready) b.top.in_valid = 1;
    b.edge();
    b.top.in_valid = 0;
  }
  CHECK(seen);
}

template <class Model>
void check_ready_is_registered() {
  Bench<Model> b;
  SimRng rng(7);
  for (int c = 0; c < 2000; ++c) {
    b.top.in_valid = rng.chance(60);
    b.top.in_data = c;
    b.top.kill = rng.chance(2);
    b.top.out_ready = 0;
    b.settle();
    const bool ready_when_blocked = b.top.in_ready;
    b.top.out_ready = 1;
    b.settle();
    INFO("cycle " << c);
    REQUIRE(b.top.in_ready == ready_when_blocked);
    b.top.out_ready = rng.chance(50);
    b.edge();
  }
}

template <class Model>
constexpr bool has_trace() {
  return std::is_same_v<Model, Vtb_fetch_buffer> || std::is_same_v<Model, Vtb_fetch_buffer16>;
}

}  // namespace

TEMPLATE_TEST_CASE("pipe buffer passes random traffic in order", "[pipe]", Vtb_reg_slice,
                   Vtb_skid_buffer, Vtb_fetch_buffer, Vtb_fetch_buffer16) {
  const uint64_t seed = GENERATE(range(1, 4));
  const unsigned in_pct = GENERATE(30u, 70u, 100u);
  const unsigned out_pct = GENERATE(30u, 70u, 100u);
  CAPTURE(seed, in_pct, out_pct);
  const Stats st = run_random<TestType>(seed, in_pct, out_pct, 0, 20000, has_trace<TestType>());
  CHECK(st.sent > 1000);
  CHECK(st.received == st.sent);
}

TEMPLATE_TEST_CASE("pipe buffer drops everything on kill", "[pipe]", Vtb_reg_slice,
                   Vtb_skid_buffer, Vtb_fetch_buffer, Vtb_fetch_buffer16) {
  const uint64_t seed = GENERATE(range(1, 4));
  const unsigned in_pct = GENERATE(50u, 100u);
  const unsigned out_pct = GENERATE(30u, 70u, 100u);
  const unsigned kill_pct = GENERATE(2u, 10u, 30u);
  CAPTURE(seed, in_pct, out_pct, kill_pct);
  const Stats st =
      run_random<TestType>(seed, in_pct, out_pct, kill_pct, 20000, has_trace<TestType>());
  CHECK(st.kills > 100);
  CHECK(st.received + st.dropped == st.sent);
  CHECK(st.received > 0);
}

TEMPLATE_TEST_CASE("pipe buffer comes out of reset empty and runs at full throughput", "[pipe]",
                   Vtb_reg_slice, Vtb_skid_buffer, Vtb_fetch_buffer, Vtb_fetch_buffer16) {
  check_reset_and_throughput<TestType>();
}

TEMPLATE_TEST_CASE("pipe buffer: kill when full empties it", "[pipe]", Vtb_reg_slice,
                   Vtb_skid_buffer, Vtb_fetch_buffer, Vtb_fetch_buffer16) {
  check_kill_when_full<TestType>();
}

TEMPLATE_TEST_CASE("pipe buffer: in_ready does not depend combinationally on out_ready",
                   "[pipe]", Vtb_skid_buffer, Vtb_fetch_buffer, Vtb_fetch_buffer16) {
  check_ready_is_registered<TestType>();
}
