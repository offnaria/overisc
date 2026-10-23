// SPDX-License-Identifier: Apache-2.0
// Cycle-based AXI4 slave backed by SimMemory.
//
// The model is stepped once per clock edge with the master outputs sampled just before the
// edge, and produces the slave outputs for the next cycle. Handshakes are evaluated against
// the outputs of the previous step, so the outputs behave like registers.
//
// Randomized timing (seeded):
//   - AW/W/AR ready toggle randomly
//   - read data and write responses come back after a random latency
//   - gaps between read beats
//   - responses with different IDs are reordered, read bursts may be interleaved
// Same-ID responses always stay in order, as required by AXI.
//
// The slave also checks the master side of the protocol and stops the simulation on a
// violation (valid dropped or payload changed before ready, illegal bursts, strobes outside
// the active byte lanes, misplaced WLAST).
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "sim_memory.h"
#include "sim_rng.h"

constexpr unsigned kAxiMaxDataBytes = 16;

enum : uint8_t { kAxiBurstFixed = 0, kAxiBurstIncr = 1, kAxiBurstWrap = 2 };
enum : uint8_t { kAxiRespOkay = 0, kAxiRespExOkay = 1, kAxiRespSlvErr = 2, kAxiRespDecErr = 3 };

// AW or AR channel payload (only the fields the model uses).
struct AxiAx {
  uint32_t id = 0;
  uint64_t addr = 0;
  uint8_t len = 0;
  uint8_t size = 0;
  uint8_t burst = 0;

  bool operator==(const AxiAx& o) const {
    return id == o.id && addr == o.addr && len == o.len && size == o.size && burst == o.burst;
  }
};

struct AxiSlaveIn {
  bool aw_valid = false;
  AxiAx aw;
  bool w_valid = false;
  uint8_t w_data[kAxiMaxDataBytes] = {};
  uint64_t w_strb = 0;
  bool w_last = false;
  bool b_ready = false;
  bool ar_valid = false;
  AxiAx ar;
  bool r_ready = false;
};

struct AxiSlaveOut {
  bool aw_ready = false;
  bool w_ready = false;
  bool ar_ready = false;
  bool b_valid = false;
  uint32_t b_id = 0;
  uint8_t b_resp = 0;
  bool r_valid = false;
  uint32_t r_id = 0;
  uint8_t r_data[kAxiMaxDataBytes] = {};
  uint8_t r_resp = 0;
  bool r_last = false;
};

struct AxiTiming {
  unsigned aw_ready_pct = 100;
  unsigned w_ready_pct = 100;
  unsigned ar_ready_pct = 100;
  unsigned b_ready_pct = 100;  // only used by test masters
  unsigned r_ready_pct = 100;  // only used by test masters
  unsigned lat_min = 0;  // cycles from acceptance to the first response beat, beyond the minimum
  unsigned lat_max = 0;
  unsigned gap_pct = 0;  // chance of not presenting a ready response in a cycle
  bool reorder = false;  // reorder responses with different IDs
  bool interleave = false;  // interleave read bursts with different IDs
  unsigned max_outstanding = 8;

  // Profiles: "fast", "small", "large", "random" (parameters drawn from rng).
  static bool from_profile(const std::string& name, SimRng& rng, AxiTiming* out);
};

// Address of beat `i` of a burst.
uint64_t axi_beat_addr(const AxiAx& ax, unsigned i);
// Byte lanes (bit per lane) that beat `i` may use on a bus of `bus_bytes` bytes.
uint64_t axi_beat_lanes(const AxiAx& ax, unsigned i, unsigned bus_bytes);

class AxiSlave {
 public:
  AxiSlave(std::string name, SimMemory& mem, unsigned data_bytes);

  // Clears all state and restarts the random sequence.
  void reset(uint64_t seed, const AxiTiming& timing);

  // Advances one clock edge. Returns false on a protocol violation (see error()).
  bool step(const AxiSlaveIn& in);

  const AxiSlaveOut& out() const { return out_; }
  const std::string& error() const { return error_; }
  const std::string& name() const { return name_; }

 private:
  struct ReadTxn {
    AxiAx ax;
    uint64_t ready_at;
    unsigned beat;
    uint64_t seq;
  };
  struct WBeat {
    uint8_t data[kAxiMaxDataBytes];
    uint64_t strb;
    bool last;
  };
  struct BResp {
    uint32_t id;
    uint8_t resp;
    uint64_t ready_at;
  };

  bool fail(const std::string& msg);
  bool check_ax(const AxiAx& ax, const char* ch);
  bool check_stable(const AxiSlaveIn& in);
  bool complete_writes();
  void select_r();
  void select_b();
  void update_ready();
  uint64_t latency();

  std::string name_;
  SimMemory& mem_;
  unsigned data_bytes_;
  AxiTiming timing_;
  SimRng rng_;
  std::string error_;

  uint64_t cycle_ = 0;
  uint64_t seq_ = 0;
  AxiSlaveOut out_;

  std::deque<ReadTxn> reads_;
  std::optional<uint64_t> cur_r_seq_;
  std::deque<AxiAx> aws_;
  std::deque<WBeat> wbeats_;
  std::deque<BResp> bresps_;

  // Master payloads that were valid but not accepted at the previous edge.
  std::optional<AxiAx> stalled_aw_;
  std::optional<WBeat> stalled_w_;
  std::optional<AxiAx> stalled_ar_;
};
