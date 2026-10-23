// SPDX-License-Identifier: Apache-2.0
// Unit test of AxiSlave (B9): a random AXI4 master drives the slave and checks every read
// beat and write response against a shadow copy of memory.
//
// The master never has two in-flight transactions touching the same bytes when one of them
// is a write, so the expected data is unambiguous regardless of reordering.
//
// usage: test_axi_slave [seed] [profile] [cycles]
#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "axi_slave.h"

namespace {

constexpr unsigned kBus = 16;
constexpr uint64_t kBase = 0x80000000;
constexpr uint64_t kSize = 64 * 1024;  // test window inside the region
constexpr uint64_t kHole = 0x40000000;  // unmapped
constexpr unsigned kIds = 4;

struct Range {
  uint64_t lo, hi;  // inclusive
  bool overlaps(const Range& o) const { return lo <= o.hi && o.lo <= hi; }
};

struct Txn {
  AxiAx ax;
  Range range;
  bool expect_err;
  uint64_t seq = 0;  // AR handshake order
  std::vector<std::vector<uint8_t>> beats;  // expected read data per beat (active lanes)
};

// Reference burst address/lane calculation, independent of axi_slave.cc (AXI spec A3.4.1 style:
// walk the beats one by one instead of using a closed form).
std::vector<uint64_t> ref_beat_addrs(const AxiAx& ax) {
  const uint64_t n = uint64_t{1} << ax.size;
  const unsigned beats = ax.len + 1u;
  std::vector<uint64_t> a;
  uint64_t addr = ax.addr;
  const uint64_t wrap_bytes = n * beats;
  const uint64_t wrap_lo = (ax.addr / wrap_bytes) * wrap_bytes;
  for (unsigned i = 0; i < beats; ++i) {
    a.push_back(addr);
    if (ax.burst == kAxiBurstFixed) continue;
    addr = (addr / n) * n + n;  // next aligned address
    if (ax.burst == kAxiBurstWrap && addr >= wrap_lo + wrap_bytes) addr = wrap_lo;
  }
  return a;
}

uint64_t ref_lanes(uint64_t addr, uint8_t size) {
  const uint64_t n = uint64_t{1} << size;
  uint64_t m = 0;
  for (uint64_t b = addr; b < (addr / n) * n + n; ++b) m |= uint64_t{1} << (b % kBus);
  return m;
}

[[noreturn]] void die(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void die(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::fprintf(stderr, "FAIL: ");
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  va_end(ap);
  std::exit(1);
}

class Master {
 public:
  Master(uint64_t seed, const AxiTiming& t) : rng_(seed ^ 0x5eed), t_(t) {}

  // Drive the master outputs for this cycle.
  void drive(AxiSlaveIn* in, bool issue) {
    if (!ar_ && issue && rng_.chance(30)) new_read();
    if (!aw_ && issue && rng_.chance(30) && w_queue_.size() < 64) new_write();
    in->ar_valid = ar_.has_value();
    if (ar_) in->ar = ar_->ax;
    in->aw_valid = aw_.has_value();
    if (aw_) in->aw = aw_->ax;
    in->w_valid = !w_queue_.empty();
    if (!w_queue_.empty()) {
      std::memcpy(in->w_data, w_queue_.front().data, kBus);
      in->w_strb = w_queue_.front().strb;
      in->w_last = w_queue_.front().last;
    }
    in->r_ready = rng_.chance(t_.r_ready_pct);
    in->b_ready = rng_.chance(t_.b_ready_pct);
  }

  // Observe the handshakes at the edge (outputs of the slave before stepping).
  void observe(const AxiSlaveIn& in, const AxiSlaveOut& out) {
    if (in.ar_valid && out.ar_ready) {
      ar_->seq = ar_seq_++;
      rd_[ar_->ax.id].push_back(*ar_);
      ar_.reset();
    }
    if (in.aw_valid && out.aw_ready) {
      wr_[aw_->ax.id].push_back(*aw_);
      aw_.reset();
    }
    if (in.w_valid && out.w_ready) w_queue_.pop_front();
    if (out.r_valid && in.r_ready) check_r(out);
    if (out.b_valid && in.b_ready) check_b(out);
  }

  bool idle() const {
    if (ar_ || aw_ || !w_queue_.empty()) return false;
    for (const auto& q : rd_) if (!q.second.empty()) return false;
    for (const auto& q : wr_) if (!q.second.empty()) return false;
    return true;
  }

  uint64_t reads = 0, writes = 0, errors = 0, reordered = 0, interleaved = 0;

 private:
  struct WBeat {
    uint8_t data[kBus];
    uint64_t strb;
    bool last;
  };

  bool busy(const Range& r, bool is_write) const {
    for (const auto& q : wr_) for (const auto& t : q.second) if (t.range.overlaps(r)) return true;
    if (aw_ && aw_->range.overlaps(r)) return true;
    if (!is_write) return false;
    for (const auto& q : rd_) for (const auto& t : q.second) if (t.range.overlaps(r)) return true;
    if (ar_ && ar_->range.overlaps(r)) return true;
    return false;
  }

  AxiAx random_ax() {
    AxiAx ax;
    ax.id = static_cast<uint32_t>(rng_.range(0, kIds - 1));
    ax.size = static_cast<uint8_t>(rng_.range(0, 4));
    const uint64_t nbytes = uint64_t{1} << ax.size;
    ax.burst = static_cast<uint8_t>(rng_.range(0, 2));
    if (ax.burst == kAxiBurstWrap) {
      static const uint8_t lens[] = {1, 3, 7, 15};
      ax.len = lens[rng_.range(0, 3)];
      ax.addr = (kBase + rng_.range(0, kSize - 1)) & ~(nbytes - 1);
    } else if (ax.burst == kAxiBurstFixed) {
      ax.len = static_cast<uint8_t>(rng_.range(0, 15));
      ax.addr = kBase + rng_.range(0, kSize - 1);
    } else {
      ax.len = static_cast<uint8_t>(rng_.chance(80) ? rng_.range(0, 15) : rng_.range(0, 255));
      ax.addr = kBase + rng_.range(0, kSize - 1);
      // stay inside the 4KB page
      const uint64_t span = (uint64_t{ax.len} + 1) * nbytes;
      if (span > 4096) ax.len = static_cast<uint8_t>(4096 / nbytes - 1);
      const uint64_t page_end = (ax.addr | 0xfff);
      const uint64_t last = (ax.addr & ~(nbytes - 1)) + (uint64_t{ax.len} + 1) * nbytes - 1;
      if (last > page_end) ax.addr -= last - page_end;
    }
    if (rng_.chance(3)) ax.addr = ax.addr - kBase + kHole;  // unmapped
    return ax;
  }

  static Range range_of(const AxiAx& ax) {
    Range r{~uint64_t{0}, 0};
    for (unsigned i = 0; i <= ax.len; ++i) {
      const uint64_t lanes = ref_lanes(ref_beat_addrs(ax)[i], ax.size);
      const uint64_t block = ref_beat_addrs(ax)[i] & ~uint64_t{kBus - 1};
      for (unsigned j = 0; j < kBus; ++j) {
        if (!((lanes >> j) & 1)) continue;
        r.lo = std::min(r.lo, block + j);
        r.hi = std::max(r.hi, block + j);
      }
    }
    return r;
  }

  void new_read() {
    Txn t;
    t.ax = random_ax();
    t.range = range_of(t.ax);
    if (busy(t.range, false)) return;
    t.expect_err = t.ax.addr < kBase;
    if (!t.expect_err) {
      for (unsigned i = 0; i <= t.ax.len; ++i) {
        const uint64_t block = ref_beat_addrs(t.ax)[i] & ~uint64_t{kBus - 1};
        std::vector<uint8_t> b(kBus);
        for (unsigned j = 0; j < kBus; ++j) b[j] = shadow_byte(block + j);
        t.beats.push_back(b);
      }
    }
    ar_ = t;
  }

  void new_write() {
    Txn t;
    t.ax = random_ax();
    t.range = range_of(t.ax);
    if (busy(t.range, true)) return;
    t.expect_err = t.ax.addr < kBase;
    for (unsigned i = 0; i <= t.ax.len; ++i) {
      WBeat b;
      for (auto& d : b.data) d = static_cast<uint8_t>(rng_.next());
      b.strb = ref_lanes(ref_beat_addrs(t.ax)[i], t.ax.size) & rng_.next();
      b.last = i == t.ax.len;
      const uint64_t block = ref_beat_addrs(t.ax)[i] & ~uint64_t{kBus - 1};
      if (!t.expect_err) {
        for (unsigned j = 0; j < kBus; ++j) {
          if ((b.strb >> j) & 1) shadow_[block + j] = b.data[j];
        }
      }
      w_queue_.push_back(b);
    }
    aw_ = t;
  }

  uint8_t shadow_byte(uint64_t a) const {
    const auto it = shadow_.find(a);
    return it == shadow_.end() ? 0 : it->second;
  }

  void check_r(const AxiSlaveOut& out) {
    auto& q = rd_[out.r_id];
    if (q.empty()) die("R with id %u but no outstanding read", out.r_id);
    Txn& t = q.front();
    if (cur_r_ && *cur_r_ != &t) ++interleaved;
    if (!beat_.count(&t) && oldest_read() != &t) ++reordered;
    const unsigned i = beat_[&t]++;
    if (out.r_last != (i == t.ax.len)) die("RLAST mismatch on beat %u of id %u", i, out.r_id);
    if (t.expect_err) {
      if (out.r_resp != kAxiRespDecErr) die("expected DECERR on read addr 0x%" PRIx64, t.ax.addr);
    } else {
      if (out.r_resp != kAxiRespOkay) die("unexpected RRESP %u", out.r_resp);
      const uint64_t lanes = ref_lanes(ref_beat_addrs(t.ax)[i], t.ax.size);
      for (unsigned j = 0; j < kBus; ++j) {
        if (((lanes >> j) & 1) && out.r_data[j] != t.beats[i][j]) {
          die("read data mismatch: addr 0x%" PRIx64 " beat %u lane %u: got %02x expected %02x",
              t.ax.addr, i, j, out.r_data[j], t.beats[i][j]);
        }
      }
    }
    cur_r_ = &t;
    if (out.r_last) {
      reads++;
      if (t.expect_err) errors++;
      beat_.erase(&t);
      cur_r_.reset();
      q.pop_front();
    }
  }

  void check_b(const AxiSlaveOut& out) {
    auto& q = wr_[out.b_id];
    if (q.empty()) die("B with id %u but no outstanding write", out.b_id);
    const Txn& t = q.front();
    const uint8_t expect = t.expect_err ? kAxiRespDecErr : kAxiRespOkay;
    // a write whose strobes are all zero touches nothing, so it cannot fail
    if (out.b_resp != expect && !(t.expect_err && out.b_resp == kAxiRespOkay)) {
      die("BRESP %u, expected %u (addr 0x%" PRIx64 ")", out.b_resp, expect, t.ax.addr);
    }
    writes++;
    if (t.expect_err) errors++;
    q.pop_front();
  }

  // Oldest accepted read that has not started returning data.
  const Txn* oldest_read() const {
    const Txn* oldest = nullptr;
    for (const auto& q : rd_) {
      for (const auto& t : q.second) {
        if (beat_.count(const_cast<Txn*>(&t))) continue;
        if (!oldest || t.seq < oldest->seq) oldest = &t;
      }
    }
    return oldest;
  }

  SimRng rng_;
  AxiTiming t_;
  std::optional<Txn> ar_, aw_;
  std::deque<WBeat> w_queue_;
  std::map<uint32_t, std::deque<Txn>> rd_, wr_;
  std::map<Txn*, unsigned> beat_;
  std::optional<Txn*> cur_r_;
  uint64_t ar_seq_ = 0;
  std::map<uint64_t, uint8_t> shadow_;
};

// A master that drops ARVALID before ARREADY must be caught.
void test_violation() {
  SimMemory mem;
  mem.add_region(kBase, kSize);
  AxiSlave s("viol", mem, kBus);
  AxiTiming t;
  t.ar_ready_pct = 0;
  s.reset(1, t);
  AxiSlaveIn in;
  in.ar_valid = true;
  in.ar.addr = kBase;
  if (!s.step(in)) die("violation test: unexpected error: %s", s.error().c_str());
  in.ar_valid = false;
  if (s.step(in)) die("violation test: dropped ARVALID was not detected");
}

// A strobe below the start address of an unaligned INCR burst must be caught.
void test_bad_strobe() {
  SimMemory mem;
  mem.add_region(kBase, kSize);
  AxiSlave s("strb", mem, kBus);
  s.reset(1, AxiTiming{});
  AxiSlaveIn in;
  if (!s.step(in)) die("bad strobe test: idle cycle failed");  // ready rises after reset
  in.aw_valid = true;
  in.aw.addr = kBase + 2;  // lanes 2..3 for a 4-byte beat
  in.aw.size = 2;
  in.aw.burst = kAxiBurstIncr;
  in.w_valid = true;
  in.w_strb = 0x3;  // lanes 0..1: illegal
  in.w_last = true;
  if (s.step(in)) die("bad strobe test: strobe outside the active lanes was not detected");
}

}  // namespace

int main(int argc, char** argv) {
  const uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 1;
  const std::string profile = argc > 2 ? argv[2] : "random";
  const uint64_t cycles = argc > 3 ? std::strtoull(argv[3], nullptr, 0) : 200000;

  test_violation();
  test_bad_strobe();

  SimRng prng(seed);
  AxiTiming t;
  if (!AxiTiming::from_profile(profile, prng, &t)) die("unknown profile %s", profile.c_str());

  SimMemory mem;
  mem.add_region(kBase, kSize);
  AxiSlave slave("dut", mem, kBus);
  slave.reset(seed, t);
  Master m(seed, t);

  uint64_t c = 0;
  for (; c < cycles || !m.idle(); ++c) {
    if (c > cycles + 100000) die("did not drain");
    AxiSlaveIn in;
    m.drive(&in, c < cycles);
    m.observe(in, slave.out());
    if (!slave.step(in)) die("%s", slave.error().c_str());
  }
  std::printf("PASS seed=%" PRIu64 " profile=%s cycles=%" PRIu64 " reads=%" PRIu64
              " writes=%" PRIu64 " decerr=%" PRIu64 " reordered=%" PRIu64
              " interleaved=%" PRIu64 "\n",
              seed, profile.c_str(), c, m.reads, m.writes, m.errors, m.reordered,
              m.interleaved);
  return 0;
}
