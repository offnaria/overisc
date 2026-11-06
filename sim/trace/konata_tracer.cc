// SPDX-License-Identifier: Apache-2.0
#include "konata_tracer.h"

#include <algorithm>
#include <sstream>

namespace {

const char* const kStageName[] = {"F0", "F1", "F2", "FB", "D", "X", "M", "W", "-"};

std::string hex(uint64_t v) {
  std::ostringstream s;
  s << std::hex << v;
  return s.str();
}

}  // namespace

KonataTracer::KonataTracer(std::ostream& out, LabelFn label, std::ostream* warn)
    : out_(out), label_(std::move(label)), warn_(warn) {}

void KonataTracer::violation(uint64_t cycle, const std::string& what) {
  ++violations_;
  if (warn_ && violations_ <= 10) {
    *warn_ << "[konata] trace rule violation at cycle " << cycle << ": " << what << "\n";
    if (violations_ == 10) *warn_ << "[konata] further violations are counted but not shown\n";
  }
}

void KonataTracer::emit(uint64_t cycle, std::string line) {
  pending_.emplace(cycle, std::move(line));
}

uint64_t KonataTracer::new_kid(uint64_t cycle, uint64_t sim_id) {
  const uint64_t kid = next_kid_++;
  emit(cycle, "I\t" + std::to_string(kid) + "\t" + std::to_string(sim_id) + "\t0");
  return kid;
}

void KonataTracer::flush_block(uint32_t fid, const Block& b, uint64_t cycle) {
  if (b.used) return;
  uint64_t first = cycle;
  for (int s = 0; s < 4; ++s) {
    if (b.has[s]) first = std::min(first, b.start[s]);
  }
  const uint64_t kid = new_kid(first, fid);
  const std::string k = std::to_string(kid);
  emit(first, "L\t" + k + "\t0\t" + (b.has_pc ? hex(b.pc) : std::string("?")) + ": fetch block");
  for (int s = 0; s < 4; ++s) {
    if (b.has[s]) emit(b.start[s], "S\t" + k + "\t0\t" + kStageName[s]);
  }
  emit(cycle, "R\t" + k + "\t0\t1");
}

void KonataTracer::block_report(uint64_t c, uint32_t fid, Stage st, const uint64_t* pc) {
  const std::string where = "fetch block " + std::to_string(fid) + " in " + kStageName[st];
  if (done_fids_.count(fid)) {
    violation(c, where + ": the block was already flushed or consumed");
    return;
  }
  auto it = blocks_.find(fid);
  if (it == blocks_.end()) {
    if (!blocks_.empty() && blocks_.rbegin()->first > fid) {
      violation(c, where + ": a younger block is already in flight (IDs out of order)");
      return;
    }
    it = blocks_.emplace(fid, Block{}).first;
  }
  Block& b = it->second;
  if (pc && !b.has_pc) {
    b.pc = *pc;
    b.has_pc = true;
  }
  if (b.stage != kNone && st < b.stage) {
    violation(c, where + ": moved back from " + kStageName[b.stage]);
    return;
  }
  if (st == b.stage) return;  // repeated report
  for (const auto& [ofid, ob] : blocks_) {
    if (ofid >= fid) break;
    if (ob.stage < st) {
      violation(c, where + ": overtakes older block " + std::to_string(ofid) + " in " +
                       kStageName[ob.stage]);
      return;
    }
  }
  b.stage = st;
  b.start[st] = c;
  b.has[st] = true;
}

void KonataTracer::insn_report(uint64_t c, uint32_t iid, Stage st) {
  const std::string where = "instruction " + std::to_string(iid) + " in " + kStageName[st];
  if (done_iids_.count(iid)) {
    violation(c, where + ": the instruction was already retired or flushed");
    return;
  }
  const auto it = insns_.find(iid);
  if (it == insns_.end()) {
    violation(c, where + ": never reported in D");
    return;
  }
  if (st < it->second.stage) {
    violation(c, where + ": moved back from " + kStageName[it->second.stage]);
    return;
  }
  if (st == it->second.stage) return;  // repeated report
  for (const auto& [oiid, oi] : insns_) {
    if (oiid >= iid) break;
    if (oi.stage < st) {
      violation(c, where + ": overtakes older instruction " + std::to_string(oiid) + " in " +
                       kStageName[oi.stage]);
      return;
    }
  }
  it->second.stage = st;
  emit(c, "S\t" + std::to_string(it->second.kid) + "\t0\t" + kStageName[st]);
}

// First report of an instruction (in D): writes the history of its fetch block.
void KonataTracer::insn_first_d(uint64_t c, const TraceSample& s) {
  const uint32_t iid = s.d.id;
  if (!insns_.empty() && insns_.rbegin()->first > iid) {
    violation(c, "instruction " + std::to_string(iid) +
                     " in D: a younger instruction is already in flight (IDs out of order)");
    return;
  }
  uint64_t first = c;
  const auto bit = blocks_.find(s.d_fid);
  if (bit == blocks_.end()) {
    violation(c, "instruction " + std::to_string(iid) + " in D comes from fetch block " +
                     std::to_string(s.d_fid) + ", which is not in flight");
  } else {
    for (int k = 0; k < 4; ++k) {
      if (bit->second.has[k] && bit->second.start[k] < c) {
        first = std::min(first, bit->second.start[k]);
      }
    }
  }
  const uint64_t kid = new_kid(first, iid);
  const std::string kk = std::to_string(kid);
  emit(first, "L\t" + kk + "\t0\t" + label_(s.d_pc, s.d_insn));
  if (bit != blocks_.end()) {
    Block& b = bit->second;
    b.used = true;
    for (int k = 0; k < 4; ++k) {
      if (b.has[k] && b.start[k] < c) emit(b.start[k], "S\t" + kk + "\t0\t" + kStageName[k]);
    }
  }
  emit(c, "S\t" + kk + "\t0\tD");
  insns_[iid] = {kid, kD};
  // the Fetch Buffer is a FIFO: blocks older than this one are done
  for (auto b = blocks_.begin(); b != blocks_.end() && b->first < s.d_fid;) {
    flush_block(b->first, b->second, c);  // no-op if used
    done_fids_.insert(b->first);
    b = blocks_.erase(b);
  }
}

void KonataTracer::sample(uint64_t c, const TraceSample& s) {
  last_cycle_ = c;

  // stage reports, oldest stage first so that an older item moves before a younger one
  for (int k = TraceSample::kNumB - 1; k >= 0; --k) {
    if (s.b[k].valid) insn_report(c, s.b[k].id, static_cast<Stage>(kX + k));
  }
  if (s.d.valid) {
    if (done_iids_.count(s.d.id)) {
      violation(c, "instruction " + std::to_string(s.d.id) +
                       " in D: the instruction was already retired or flushed");
    } else if (insns_.count(s.d.id)) {
      insn_report(c, s.d.id, kD);
    } else {
      insn_first_d(c, s);
    }
  }
  if (s.fb.valid) block_report(c, s.fb.id, kFB, nullptr);
  for (int k = TraceSample::kNumF - 1; k >= 0; --k) {
    if (s.f[k].valid) {
      block_report(c, s.f[k].id, static_cast<Stage>(k), k == 0 ? &s.f0_pc : nullptr);
    }
  }

  // retire at the end of this cycle
  for (const auto& r : s.retire) {
    if (!r.valid) continue;
    const auto it = insns_.find(r.id);
    if (it == insns_.end()) {
      violation(c, "retire of instruction " + std::to_string(r.id) + ", which is not in flight");
      continue;
    }
    emit(c + 1, "R\t" + std::to_string(it->second.kid) + "\t" + std::to_string(next_retire_++) +
                    "\t0");
    done_iids_.insert(r.id);
    insns_.erase(it);
  }

  // redirect: flush everything before redirect_stage at the end of this cycle
  if (s.redirect) {
    const Stage src = static_cast<Stage>(std::clamp(s.redirect_stage, 0, 7));
    for (auto it = insns_.begin(); it != insns_.end();) {
      if (it->second.stage < src) {
        emit(c + 1, "R\t" + std::to_string(it->second.kid) + "\t0\t1");
        done_iids_.insert(it->first);
        it = insns_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = blocks_.begin(); it != blocks_.end();) {
      if (it->second.stage < src) {
        flush_block(it->first, it->second, c + 1);
        done_fids_.insert(it->first);
        it = blocks_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // ---- write what can no longer change ----
  uint64_t safe = c;
  for (const auto& [fid, b] : blocks_) {
    for (int k = 0; k < 4; ++k) {
      if (b.has[k]) safe = std::min(safe, b.start[k]);
    }
  }
  write_until(safe);
}

void KonataTracer::write_until(uint64_t cycle) {
  while (!pending_.empty() && pending_.begin()->first < cycle) {
    const uint64_t c = pending_.begin()->first;
    if (!header_) {
      out_ << "Kanata\t0004\nC=\t" << c << "\n";
      header_ = true;
      written_cycle_ = c;
    } else if (c > written_cycle_) {
      out_ << "C\t" << (c - written_cycle_) << "\n";
      written_cycle_ = c;
    }
    auto range = pending_.equal_range(c);
    for (auto it = range.first; it != range.second; ++it) out_ << it->second << "\n";
    pending_.erase(range.first, range.second);
  }
}

void KonataTracer::finish() {
  const uint64_t end = last_cycle_ + 1;
  for (const auto& [fid, b] : blocks_) flush_block(fid, b, end);
  blocks_.clear();
  for (const auto& [iid, insn] : insns_) {
    emit(end, "R\t" + std::to_string(insn.kid) + "\t0\t1");
  }
  insns_.clear();
  write_until(end + 1);
  out_.flush();
}
