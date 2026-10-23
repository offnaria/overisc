// SPDX-License-Identifier: Apache-2.0
// Loads a RISC-V ELF64 executable into SimMemory (B3).
// PT_LOAD segments are placed at their physical addresses (p_paddr); the part of a segment
// beyond its file size is zero-filled. Symbols from .symtab are returned so that the
// testbench can find tohost/fromhost.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "sim_memory.h"

struct ElfImage {
  uint64_t entry = 0;
  std::map<std::string, uint64_t> symbols;
};

// Returns false and sets *err on failure (unreadable file, not RV64 ELF, segment outside
// the memory regions).
bool load_elf(const std::string& path, SimMemory& mem, ElfImage* image, std::string* err);
