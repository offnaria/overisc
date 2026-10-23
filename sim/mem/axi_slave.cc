// SPDX-License-Identifier: Apache-2.0
#include "axi_slave.h"

#include <cstring>
#include <set>
#include <sstream>
#include <vector>

namespace {

constexpr size_t kMaxWBeats = 512;  // must hold at least one full burst (256 beats)

std::string hex(uint64_t v) {
  std::ostringstream s;
  s << "0x" << std::hex << v;
  return s.str();
}

}  // namespace

bool AxiTiming::from_profile(const std::string& name, SimRng& rng, AxiTiming* out) {
  AxiTiming t;
  if (name == "fast") {
    // defaults: always ready, minimum latency, in order
  } else if (name == "small") {
    t.aw_ready_pct = t.w_ready_pct = t.ar_ready_pct = 70;
    t.b_ready_pct = t.r_ready_pct = 70;
    t.lat_min = 0;
    t.lat_max = 4;
    t.gap_pct = 10;
    t.reorder = true;
  } else if (name == "large") {
    t.aw_ready_pct = t.w_ready_pct = t.ar_ready_pct = 40;
    t.b_ready_pct = t.r_ready_pct = 40;
    t.lat_min = 10;
    t.lat_max = 60;
    t.gap_pct = 30;
    t.reorder = true;
    t.interleave = true;
  } else if (name == "random") {
    t.aw_ready_pct = static_cast<unsigned>(rng.range(10, 100));
    t.w_ready_pct = static_cast<unsigned>(rng.range(10, 100));
    t.ar_ready_pct = static_cast<unsigned>(rng.range(10, 100));
    t.b_ready_pct = static_cast<unsigned>(rng.range(10, 100));
    t.r_ready_pct = static_cast<unsigned>(rng.range(10, 100));
    t.lat_min = static_cast<unsigned>(rng.range(0, 20));
    t.lat_max = t.lat_min + static_cast<unsigned>(rng.range(0, 40));
    t.gap_pct = static_cast<unsigned>(rng.range(0, 50));
    t.reorder = rng.chance(50);
    t.interleave = rng.chance(50);
    t.max_outstanding = static_cast<unsigned>(rng.range(1, 16));
  } else {
    return false;
  }
  *out = t;
  return true;
}

uint64_t axi_beat_addr(const AxiAx& ax, unsigned i) {
  const uint64_t nbytes = uint64_t{1} << ax.size;
  switch (ax.burst) {
    case kAxiBurstFixed:
      return ax.addr;
    case kAxiBurstWrap: {
      const uint64_t total = nbytes * (uint64_t{ax.len} + 1);
      const uint64_t lower = ax.addr & ~(total - 1);
      return lower + ((ax.addr - lower + i * nbytes) & (total - 1));
    }
    default: {
      const uint64_t aligned = ax.addr & ~(nbytes - 1);
      return i == 0 ? ax.addr : aligned + i * nbytes;
    }
  }
}

uint64_t axi_beat_lanes(const AxiAx& ax, unsigned i, unsigned bus_bytes) {
  const uint64_t nbytes = uint64_t{1} << ax.size;
  const uint64_t addr = axi_beat_addr(ax, i);
  const unsigned lo = static_cast<unsigned>(addr % bus_bytes);
  const unsigned hi = static_cast<unsigned>((addr & ~(nbytes - 1)) % bus_bytes + nbytes - 1);
  const uint64_t upto_hi = hi >= 63 ? ~uint64_t{0} : (uint64_t{1} << (hi + 1)) - 1;
  return upto_hi & ~((uint64_t{1} << lo) - 1);
}

AxiSlave::AxiSlave(std::string name, SimMemory& mem, unsigned data_bytes)
    : name_(std::move(name)), mem_(mem), data_bytes_(data_bytes) {}

void AxiSlave::reset(uint64_t seed, const AxiTiming& timing) {
  timing_ = timing;
  rng_.reseed(seed);
  error_.clear();
  cycle_ = 0;
  seq_ = 0;
  out_ = AxiSlaveOut{};
  reads_.clear();
  cur_r_seq_.reset();
  aws_.clear();
  wbeats_.clear();
  bresps_.clear();
  stalled_aw_.reset();
  stalled_w_.reset();
  stalled_ar_.reset();
  // Outputs stay low during reset (the SV wrapper clears its registers); the first step
  // after reset decides the ready signals.
}

bool AxiSlave::fail(const std::string& msg) {
  std::ostringstream s;
  s << name_ << ": cycle " << cycle_ << ": " << msg;
  error_ = s.str();
  return false;
}

bool AxiSlave::check_ax(const AxiAx& ax, const char* ch) {
  const std::string p = std::string(ch) + " id=" + std::to_string(ax.id) + " addr=" + hex(ax.addr);
  const uint64_t nbytes = uint64_t{1} << ax.size;
  if (nbytes > data_bytes_) return fail(p + ": size exceeds the data bus width");
  switch (ax.burst) {
    case kAxiBurstFixed:
      if (ax.len > 15) return fail(p + ": FIXED burst longer than 16 beats");
      break;
    case kAxiBurstIncr: {
      const uint64_t last = (ax.addr & ~(nbytes - 1)) + (uint64_t{ax.len} + 1) * nbytes - 1;
      if ((last >> 12) != (ax.addr >> 12)) return fail(p + ": INCR burst crosses a 4KB boundary");
      break;
    }
    case kAxiBurstWrap:
      if (ax.len != 1 && ax.len != 3 && ax.len != 7 && ax.len != 15) {
        return fail(p + ": WRAP burst length must be 2, 4, 8 or 16");
      }
      if (ax.addr & (nbytes - 1)) return fail(p + ": WRAP burst start is not size-aligned");
      break;
    default:
      return fail(p + ": reserved burst type");
  }
  return true;
}

bool AxiSlave::check_stable(const AxiSlaveIn& in) {
  if (stalled_aw_ && !(in.aw_valid && in.aw == *stalled_aw_)) {
    return fail("AW valid dropped or payload changed before ready");
  }
  if (stalled_ar_ && !(in.ar_valid && in.ar == *stalled_ar_)) {
    return fail("AR valid dropped or payload changed before ready");
  }
  if (stalled_w_ && !(in.w_valid && in.w_strb == stalled_w_->strb &&
                      in.w_last == stalled_w_->last &&
                      std::memcmp(in.w_data, stalled_w_->data, data_bytes_) == 0)) {
    return fail("W valid dropped or payload changed before ready");
  }
  return true;
}

bool AxiSlave::step(const AxiSlaveIn& in) {
  if (!error_.empty()) return false;
  if (!check_stable(in)) return false;

  // Handshakes at this edge, against the outputs of the previous step.
  if (in.aw_valid && out_.aw_ready) {
    if (!check_ax(in.aw, "AW")) return false;
    aws_.push_back(in.aw);
  }
  if (in.w_valid && out_.w_ready) {
    WBeat b;
    std::memcpy(b.data, in.w_data, sizeof(b.data));
    b.strb = in.w_strb;
    b.last = in.w_last;
    wbeats_.push_back(b);
  }
  if (in.ar_valid && out_.ar_ready) {
    if (!check_ax(in.ar, "AR")) return false;
    reads_.push_back({in.ar, cycle_ + latency(), 0, seq_++});
  }
  if (out_.b_valid && in.b_ready) out_.b_valid = false;
  if (out_.r_valid && in.r_ready) {
    out_.r_valid = false;
    for (auto it = reads_.begin(); it != reads_.end(); ++it) {
      if (it->seq != *cur_r_seq_) continue;
      if (++it->beat > it->ax.len) {
        reads_.erase(it);
        cur_r_seq_.reset();
      }
      break;
    }
  }

  // Remember payloads that must be held until accepted.
  stalled_aw_.reset();
  stalled_w_.reset();
  stalled_ar_.reset();
  if (in.aw_valid && !out_.aw_ready) stalled_aw_ = in.aw;
  if (in.ar_valid && !out_.ar_ready) stalled_ar_ = in.ar;
  if (in.w_valid && !out_.w_ready) {
    WBeat b;
    std::memcpy(b.data, in.w_data, sizeof(b.data));
    b.strb = in.w_strb;
    b.last = in.w_last;
    stalled_w_ = b;
  }

  if (!complete_writes()) return false;

  ++cycle_;
  select_r();
  select_b();
  update_ready();
  return true;
}

bool AxiSlave::complete_writes() {
  while (!aws_.empty() && wbeats_.size() > aws_.front().len) {
    const AxiAx ax = aws_.front();
    aws_.pop_front();
    uint8_t resp = kAxiRespOkay;
    for (unsigned i = 0; i <= ax.len; ++i) {
      const WBeat b = wbeats_.front();
      wbeats_.pop_front();
      if (b.last != (i == ax.len)) {
        return fail("AW id=" + std::to_string(ax.id) + " addr=" + hex(ax.addr) + ": WLAST on beat " +
                    std::to_string(i) + " of " + std::to_string(ax.len + 1));
      }
      const uint64_t lanes = axi_beat_lanes(ax, i, data_bytes_);
      if (b.strb & ~lanes) {
        return fail("AW id=" + std::to_string(ax.id) + " addr=" + hex(ax.addr) + ": WSTRB " +
                    hex(b.strb) + " outside active lanes " + hex(lanes) + " on beat " +
                    std::to_string(i));
      }
      const uint64_t block = axi_beat_addr(ax, i) & ~uint64_t{data_bytes_ - 1};
      for (unsigned j = 0; j < data_bytes_; ++j) {
        if (!((b.strb >> j) & 1)) continue;
        if (mem_.contains(block + j, 1)) {
          mem_.write(block + j, &b.data[j], 1);
        } else {
          resp = kAxiRespDecErr;
        }
      }
    }
    bresps_.push_back({ax.id, resp, cycle_ + latency()});
  }
  return true;
}

uint64_t AxiSlave::latency() { return rng_.range(timing_.lat_min, timing_.lat_max); }

void AxiSlave::select_r() {
  if (out_.r_valid || reads_.empty()) return;
  if (rng_.chance(timing_.gap_pct)) return;

  std::vector<ReadTxn*> cands;
  if (!timing_.interleave && cur_r_seq_) {
    // keep sending the burst in progress
    for (auto& t : reads_) {
      if (t.seq == *cur_r_seq_) cands.push_back(&t);
    }
  } else if (!timing_.reorder) {
    if (reads_.front().ready_at <= cycle_) cands.push_back(&reads_.front());
  } else {
    // oldest transaction of each ID
    std::set<uint32_t> seen;
    for (auto& t : reads_) {
      if (!seen.insert(t.ax.id).second) continue;
      if (t.ready_at <= cycle_) cands.push_back(&t);
    }
  }
  if (cands.empty()) return;

  ReadTxn& t = *cands[rng_.range(0, cands.size() - 1)];
  const uint64_t lanes = axi_beat_lanes(t.ax, t.beat, data_bytes_);
  const uint64_t block = axi_beat_addr(t.ax, t.beat) & ~uint64_t{data_bytes_ - 1};
  bool ok = true;
  for (unsigned j = 0; j < data_bytes_; ++j) {
    if (((lanes >> j) & 1) && !mem_.contains(block + j, 1)) ok = false;
  }
  std::memset(out_.r_data, 0, sizeof(out_.r_data));
  if (ok) mem_.read(block, out_.r_data, data_bytes_);
  out_.r_valid = true;
  out_.r_id = t.ax.id;
  out_.r_resp = ok ? kAxiRespOkay : kAxiRespDecErr;
  out_.r_last = t.beat == t.ax.len;
  cur_r_seq_ = t.seq;
}

void AxiSlave::select_b() {
  if (out_.b_valid || bresps_.empty()) return;
  if (rng_.chance(timing_.gap_pct)) return;

  std::vector<size_t> cands;
  if (!timing_.reorder) {
    if (bresps_.front().ready_at <= cycle_) cands.push_back(0);
  } else {
    std::set<uint32_t> seen;
    for (size_t i = 0; i < bresps_.size(); ++i) {
      if (!seen.insert(bresps_[i].id).second) continue;
      if (bresps_[i].ready_at <= cycle_) cands.push_back(i);
    }
  }
  if (cands.empty()) return;

  const size_t i = cands[rng_.range(0, cands.size() - 1)];
  out_.b_valid = true;
  out_.b_id = bresps_[i].id;
  out_.b_resp = bresps_[i].resp;
  bresps_.erase(bresps_.begin() + static_cast<std::ptrdiff_t>(i));
}

void AxiSlave::update_ready() {
  out_.aw_ready = aws_.size() < timing_.max_outstanding && rng_.chance(timing_.aw_ready_pct);
  out_.w_ready = wbeats_.size() < kMaxWBeats && rng_.chance(timing_.w_ready_pct);
  out_.ar_ready = reads_.size() < timing_.max_outstanding && rng_.chance(timing_.ar_ready_pct);
}
