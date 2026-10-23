// SPDX-License-Identifier: Apache-2.0
// Sparse byte-addressed memory shared by all simulated bus ports.
// Unwritten bytes read as zero (same as Spike). Accesses are only legal inside the
// registered regions; the bus models turn other addresses into DECERR.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class SimMemory {
 public:
  struct Region {
    uint64_t base;
    uint64_t size;
  };

  void add_region(uint64_t base, uint64_t size) { regions_.push_back({base, size}); }
  const std::vector<Region>& regions() const { return regions_; }

  // True if [addr, addr + len) lies entirely inside one region.
  bool contains(uint64_t addr, uint64_t len) const;

  void read(uint64_t addr, uint8_t* dst, size_t len) const;
  void write(uint64_t addr, const uint8_t* src, size_t len);

  // Drops all contents (regions are kept).
  void clear() { pages_.clear(); }

 private:
  static constexpr unsigned kPageBits = 12;
  static constexpr uint64_t kPageSize = uint64_t{1} << kPageBits;
  using Page = std::array<uint8_t, kPageSize>;

  std::vector<Region> regions_;
  std::unordered_map<uint64_t, std::unique_ptr<Page>> pages_;
};
