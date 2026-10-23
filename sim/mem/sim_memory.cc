// SPDX-License-Identifier: Apache-2.0
#include "sim_memory.h"

#include <algorithm>
#include <cstring>

bool SimMemory::contains(uint64_t addr, uint64_t len) const {
  for (const auto& r : regions_) {
    if (addr >= r.base && len <= r.size && addr - r.base <= r.size - len) return true;
  }
  return false;
}

void SimMemory::read(uint64_t addr, uint8_t* dst, size_t len) const {
  while (len > 0) {
    const uint64_t off = addr & (kPageSize - 1);
    const size_t n = std::min<uint64_t>(len, kPageSize - off);
    const auto it = pages_.find(addr >> kPageBits);
    if (it == pages_.end()) {
      std::memset(dst, 0, n);
    } else {
      std::memcpy(dst, it->second->data() + off, n);
    }
    addr += n;
    dst += n;
    len -= n;
  }
}

void SimMemory::write(uint64_t addr, const uint8_t* src, size_t len) {
  while (len > 0) {
    const uint64_t off = addr & (kPageSize - 1);
    const size_t n = std::min<uint64_t>(len, kPageSize - off);
    auto& page = pages_[addr >> kPageBits];
    if (!page) {
      page = std::make_unique<Page>();
      page->fill(0);
    }
    std::memcpy(page->data() + off, src, n);
    addr += n;
    src += n;
    len -= n;
  }
}
