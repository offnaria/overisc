// SPDX-License-Identifier: Apache-2.0
#include "konata_tracer.h"

#include <algorithm>
#include <sstream>

namespace {

const char* const kStageName[] = {"F0", "F1", "F2", "FB", "D", "X", "M", "W"};

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
  emit(first, "L\t" + k + "\t0\t" + hex(b.pc) + ": fetch block");
  for (int s = 0; s < 4; ++s) {
    if (b.has[s]) emit(b.start[s], "S\t" + k + "\t0\t" + kStageName[s]);
  }
  emit(cycle, "R\t" + k + "\t0\t1");
}

void KonataTracer::sample(uint64_t c, const TraceSample& s) {
  last_cycle_ = c;

  // ---- fetch blocks in F0..F2 ----
  std::vector<uint32_t> seen_f;
  int64_t prev_fid = -1;  // stages are scanned from F2 (oldest) down to F0
  for (int k = TraceSample::kNumF - 1; k >= 0; --k) {
    if (!s.f[k].valid) continue;
    const uint32_t fid = s.f[k].id;
    const std::string where = "fetch block " + std::to_string(fid) + " in " + kStageName[k];
    if (done_fids_.count(fid)) {
      violation(c, where + " was already flushed or consumed (shown as a pulse?)");
      continue;
    }
    if (prev_fid >= 0 && fid <= prev_fid) {
      violation(c, where + " is not younger than the block in a later stage");
      continue;
    }
    prev_fid = fid;
    auto bit = blocks_.find(fid);
    if (bit == blocks_.end()) {
      if (k != 0) {
        violation(c, where + " first seen after F0");
        continue;
      }
      bit = blocks_.emplace(fid, Block{}).first;
    } else if (bit->second.stage != kNone && bit->second.stage > static_cast<Stage>(k)) {
      violation(c, where + " moved back from " + kStageName[bit->second.stage]);
      seen_f.push_back(fid);  // keep it alive
      continue;
    }
    Block& b = bit->second;
    if (k == 0) b.pc = s.f0_pc;
    if (b.stage != static_cast<Stage>(k)) {
      b.stage = static_cast<Stage>(k);
      b.start[k] = c;
      b.has[k] = true;
    }
    seen_f.push_back(fid);
  }
  // blocks that left F0..F2 without entering the Fetch Buffer were flushed
  for (auto it = blocks_.begin(); it != blocks_.end();) {
    Block& b = it->second;
    const bool in_f = b.stage <= kF2;
    if (in_f && std::find(seen_f.begin(), seen_f.end(), it->first) == seen_f.end()) {
      flush_block(it->first, b, c);
      done_fids_.insert(it->first);
      it = blocks_.erase(it);
    } else {
      ++it;
    }
  }
  // the block written into the Fetch Buffer this cycle is in FB from the next cycle
  if (s.fb_push.valid) {
    const auto bit = blocks_.find(s.fb_push.id);
    const std::string what = "fetch block " + std::to_string(s.fb_push.id) + " pushed into FB";
    if (done_fids_.count(s.fb_push.id)) {
      violation(c, what + " was already flushed or consumed");
    } else if (bit == blocks_.end()) {
      violation(c, what + " was never seen in F0..F2");
    } else {
      Block& b = bit->second;
      b.stage = kFB;
      b.in_fb = true;
      b.start[kFB] = c + 1;
      b.has[kFB] = true;
    }
  }

  // ---- instructions (oldest first: W, M, X, D) ----
  std::vector<uint32_t> seen_i;
  int64_t prev_iid = -1;
  auto check_insn = [&](uint32_t iid, Stage st) {
    const std::string where = "instruction " + std::to_string(iid) + " in " + kStageName[st];
    if (done_iids_.count(iid)) {
      violation(c, where + " was already retired or flushed (shown as a pulse?)");
      return false;
    }
    if (prev_iid >= 0 && iid <= prev_iid) {
      violation(c, where + " is not younger than the instruction in a later stage");
      return false;
    }
    prev_iid = iid;
    return true;
  };
  for (int k = TraceSample::kNumB - 1; k >= 0; --k) {
    if (!s.b[k].valid) continue;
    const uint32_t iid = s.b[k].id;
    const Stage st = static_cast<Stage>(kX + k);
    if (!check_insn(iid, st)) continue;
    auto it = insns_.find(iid);
    if (it == insns_.end()) {
      violation(c, "instruction " + std::to_string(iid) + " in " + kStageName[st] +
                       " was never seen in D");
      continue;
    }
    if (it->second.stage > st) {
      violation(c, "instruction " + std::to_string(iid) + " moved back from " +
                       kStageName[it->second.stage] + " to " + kStageName[st]);
    } else if (it->second.stage != st) {
      it->second.stage = st;
      emit(c, "S\t" + std::to_string(it->second.kid) + "\t0\t" + kStageName[st]);
    }
    seen_i.push_back(iid);
  }
  if (s.d.valid && check_insn(s.d.id, kD)) {
    seen_i.push_back(s.d.id);
    auto it = insns_.find(s.d.id);
    if (it == insns_.end()) {
      // first appearance: write the fetch history of its block
      uint64_t first = c;
      const auto bit = blocks_.find(s.d_fid);
      if (bit == blocks_.end()) {
        violation(c, "instruction " + std::to_string(s.d.id) + " in D comes from fetch block " +
                         std::to_string(s.d_fid) + ", which is not in FB or F0..F2");
      } else {
        for (int k = 0; k < 4; ++k) {
          if (bit->second.has[k] && bit->second.start[k] < c) {
            first = std::min(first, bit->second.start[k]);
          }
        }
      }
      const uint64_t kid = new_kid(first, s.d.id);
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
      insns_[s.d.id] = {kid, kD};
      // the Fetch Buffer is a FIFO: older blocks are done
      for (auto b = blocks_.begin(); b != blocks_.end() && b->first < s.d_fid;) {
        if (b->second.stage == kFB) {
          flush_block(b->first, b->second, c);  // no-op if used
          done_fids_.insert(b->first);
          b = blocks_.erase(b);
        } else {
          ++b;
        }
      }
    } else if (it->second.stage != kD) {
      violation(c, "instruction " + std::to_string(s.d.id) + " moved back to D");
    }
  }
  // instructions that disappeared without retiring were flushed
  for (auto it = insns_.begin(); it != insns_.end();) {
    if (std::find(seen_i.begin(), seen_i.end(), it->first) == seen_i.end()) {
      emit(c, "R\t" + std::to_string(it->second.kid) + "\t0\t1");
      done_iids_.insert(it->first);
      it = insns_.erase(it);
    } else {
      ++it;
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
  // Fetch Buffer cleared at the end of this cycle
  if (s.fb_flush) {
    for (auto it = blocks_.begin(); it != blocks_.end();) {
      if (it->second.stage == kFB) {
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
