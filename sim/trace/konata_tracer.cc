// SPDX-License-Identifier: Apache-2.0
#include "konata_tracer.h"

#include <algorithm>
#include <iterator>
#include <sstream>

namespace {

const char* const kStageName[] = {"F0", "F1", "F2", "FB", "D", "X", "M", "W"};

std::string hex(uint64_t v) {
  std::ostringstream s;
  s << std::hex << v;
  return s.str();
}

}  // namespace

KonataTracer::KonataTracer(std::ostream& out, DisasmFn disasm, std::ostream* warn)
    : out_(out), disasm_(std::move(disasm)), warn_(warn) {}

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

void KonataTracer::report(uint64_t c, uint32_t id, Stage st, const TraceSample& s) {
  const std::string where = "instruction " + std::to_string(id) + " in " + kStageName[st];
  auto it = insns_.find(id);
  if (it == insns_.end()) {
    if (last_id_ && id <= *last_id_) {
      violation(c, where + ": the instruction already left the pipeline");
      return;
    }
    // the first report must include F0 (it may also reach a later stage in the same cycle)
    if (st != kF0 && !(s.stage[kF0].valid && s.stage[kF0].id == id)) {
      violation(c, where + ": first report must be in F0");
      return;
    }
    last_id_ = id;
    const uint64_t kid = next_kid_++;
    const std::string k = std::to_string(kid);
    it = insns_.emplace(id, Insn{kid, st, c, c, false}).first;
    emit(c, "I\t" + k + "\t" + std::to_string(id) + "\t0");
    emit(c, "L\t" + k + "\t0\t" + hex(s.f0_pc) + ": ");
    emit(c, "S\t" + k + "\t0\t" + kStageName[st]);
    if (st >= kF2) {  // reached F2 in its first cycle
      it->second.has_inst = true;
      emit(c, "L\t" + k + "\t0\t" + disasm_(s.f2_inst));
    }
    return;
  }
  Insn& i = it->second;
  // Stages are scanned from the latest one, so an earlier stage reported in the same cycle
  // means the instruction passed through it (e.g. a bypassed buffer): keep the latest.
  if (st < i.stage && i.stage_cycle == c) return;
  if (st < i.stage) {
    violation(c, where + ": moved back from " + kStageName[i.stage]);
    return;
  }
  if (st == i.stage) return;  // repeated report
  // Older instructions are never in an earlier stage than younger ones (checked on every move),
  // so the nearest older instruction is the only one that can be overtaken.
  if (it != insns_.begin()) {
    const auto& [oid, older] = *std::prev(it);
    if (older.stage < st) {
      violation(c, where + ": overtakes older instruction " + std::to_string(oid) + " in " +
                       kStageName[older.stage]);
      return;
    }
  }
  i.stage = st;
  i.stage_cycle = c;
  const std::string k = std::to_string(i.kid);
  emit(c, "S\t" + k + "\t0\t" + kStageName[st]);
  if (st == kF2 && !i.has_inst) {
    i.has_inst = true;
    emit(c, "L\t" + k + "\t0\t" + disasm_(s.f2_inst));
  }
}

void KonataTracer::sample(uint64_t c, const TraceSample& s) {
  last_cycle_ = c;

  // stage reports, oldest stage first so that an older instruction moves before a younger one
  for (int k = TraceSample::kNumStages - 1; k >= 0; --k) {
    if (s.stage[k].valid) report(c, s.stage[k].id, static_cast<Stage>(k), s);
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
    insns_.erase(it);
  }

  // redirect: flush every instruction younger than redirect_id at the end of this cycle. One that
  // is first reported in this cycle enters F0 at its end, after the redirect: it is on the new
  // path and stays.
  if (s.redirect) {
    for (auto it = insns_.upper_bound(s.redirect_id); it != insns_.end();) {
      if (it->second.created == c) {
        ++it;
        continue;
      }
      emit(c + 1, "R\t" + std::to_string(it->second.kid) + "\t0\t1");
      it = insns_.erase(it);
    }
  }

  write_until(c);
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
  for (const auto& [id, insn] : insns_) {
    emit(end, "R\t" + std::to_string(insn.kid) + "\t0\t1");
  }
  insns_.clear();
  write_until(end + 1);
  out_.flush();
}
