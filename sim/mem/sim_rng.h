// SPDX-License-Identifier: Apache-2.0
// Deterministic random number generator for the simulation models.
// xoshiro256** seeded through splitmix64, so the same seed gives the same sequence on any
// platform (std::*_distribution is implementation-defined and is not used).
#pragma once

#include <cstdint>

class SimRng {
 public:
  explicit SimRng(uint64_t seed = 0) { reseed(seed); }

  void reseed(uint64_t seed) {
    for (auto& s : s_) s = splitmix64(seed);
  }

  uint64_t next() {
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform in [lo, hi] (modulo bias is negligible for simulation purposes).
  uint64_t range(uint64_t lo, uint64_t hi) { return lo + next() % (hi - lo + 1); }

  // True with the given probability in percent.
  bool chance(unsigned percent) { return next() % 100 < percent; }

  static uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }

 private:
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

  uint64_t s_[4];
};
