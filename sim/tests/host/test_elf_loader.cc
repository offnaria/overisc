// SPDX-License-Identifier: Apache-2.0
// Unit test of the ELF loader (B3): for every built riscv-tests ELF, the loaded memory must
// match `objcopy -O binary` output placed at the entry point, and the tohost symbol must
// match `nm`.
//
// RVTESTS_DIR and RISCV_PREFIX are set by sim/CMakeLists.txt.
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "elf_loader.h"

namespace {

std::vector<std::string> elf_list() {
  std::vector<std::string> v;
  std::ifstream f(std::string(RVTESTS_DIR) + "/m0.list");
  for (std::string line; std::getline(f, line);) {
    if (!line.empty()) v.push_back(line);
  }
  v.push_back("rv64ui-p-ma_data");  // built but excluded from M0; still loadable
  return v;
}

std::string run(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[256];
  while (std::fgets(buf, sizeof(buf), p)) out += buf;
  pclose(p);
  return out;
}

std::vector<uint8_t> objcopy_binary(const std::string& elf) {
  char path[] = "/tmp/overisc_elf_XXXXXX";
  const int fd = mkstemp(path);
  REQUIRE(fd >= 0);
  close(fd);
  REQUIRE(std::system((std::string(RISCV_PREFIX) + "objcopy -O binary " + elf + " " + path)
                          .c_str()) == 0);
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> bin((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::remove(path);
  return bin;
}

uint64_t nm_symbol(const std::string& elf, const std::string& sym) {
  const std::string out =
      run(std::string(RISCV_PREFIX) + "nm " + elf + " | awk '$3 == \"" + sym + "\" { print $1 }'");
  return std::strtoull(out.c_str(), nullptr, 16);
}

}  // namespace

TEST_CASE("ELF loader matches objcopy and nm", "[elf]") {
  const std::vector<std::string> list = elf_list();
  REQUIRE(list.size() > 1);  // m0.list exists (make riscv-tests)
  const std::string name = GENERATE_COPY(from_range(list));
  const std::string elf = std::string(RVTESTS_DIR) + "/" + name;
  CAPTURE(elf);

  SimMemory mem;
  mem.add_region(0x80000000, 0x8000000);
  ElfImage image;
  std::string err;
  REQUIRE(load_elf(elf, mem, &image, &err));

  const std::vector<uint8_t> bin = objcopy_binary(elf);
  REQUIRE_FALSE(bin.empty());
  std::vector<uint8_t> got(bin.size());
  mem.read(image.entry, got.data(), got.size());
  REQUIRE(got == bin);

  REQUIRE(image.symbols.count("tohost") == 1);
  CHECK(image.symbols.at("tohost") == nm_symbol(elf, "tohost"));
}

TEST_CASE("ELF loader rejects segments outside memory", "[elf]") {
  SimMemory mem;
  mem.add_region(0x90000000, 0x1000);
  ElfImage image;
  std::string err;
  REQUIRE_FALSE(load_elf(std::string(RVTESTS_DIR) + "/rv64ui-p-simple", mem, &image, &err));
  CHECK(err.find("outside memory") != std::string::npos);
}

TEST_CASE("ELF loader rejects non-ELF files", "[elf]") {
  SimMemory mem;
  mem.add_region(0x80000000, 0x1000);
  ElfImage image;
  std::string err;
  CHECK_FALSE(load_elf("/nonexistent", mem, &image, &err));
  CHECK_FALSE(load_elf(std::string(RVTESTS_DIR) + "/m0.list", mem, &image, &err));
}
