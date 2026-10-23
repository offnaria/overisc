// SPDX-License-Identifier: Apache-2.0
// Unit test of the ELF loader (B3): the loaded memory must match `objcopy -O binary` output
// placed at the entry point, and the tohost symbol must match `nm`.
//
// usage: test_elf_loader <elf> <binary> <tohost address>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "elf_loader.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <elf> <binary> <tohost>\n", argv[0]);
    return 2;
  }
  SimMemory mem;
  mem.add_region(0x80000000, 0x8000000);
  ElfImage image;
  std::string err;
  if (!load_elf(argv[1], mem, &image, &err)) {
    std::fprintf(stderr, "FAIL: %s\n", err.c_str());
    return 1;
  }

  std::ifstream f(argv[2], std::ios::binary);
  const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
  if (bin.empty()) {
    std::fprintf(stderr, "FAIL: empty binary %s\n", argv[2]);
    return 1;
  }
  std::vector<uint8_t> got(bin.size());
  mem.read(image.entry, got.data(), got.size());
  for (size_t i = 0; i < bin.size(); ++i) {
    if (got[i] != bin[i]) {
      std::fprintf(stderr, "FAIL: %s: byte at 0x%" PRIx64 ": got %02x expected %02x\n", argv[1],
                   image.entry + i, got[i], bin[i]);
      return 1;
    }
  }

  const uint64_t tohost = std::strtoull(argv[3], nullptr, 16);
  const auto it = image.symbols.find("tohost");
  if (it == image.symbols.end() || it->second != tohost) {
    std::fprintf(stderr, "FAIL: %s: tohost symbol mismatch\n", argv[1]);
    return 1;
  }

  // a segment outside memory must be rejected
  SimMemory small;
  small.add_region(0x90000000, 0x1000);
  if (load_elf(argv[1], small, &image, &err)) {
    std::fprintf(stderr, "FAIL: %s: segment outside memory was accepted\n", argv[1]);
    return 1;
  }
  return 0;
}
