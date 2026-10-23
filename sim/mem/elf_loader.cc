// SPDX-License-Identifier: Apache-2.0
#include "elf_loader.h"

#include <elfio/elfio.hpp>
#include <sstream>
#include <vector>

namespace {

std::string hex(uint64_t v) {
  std::ostringstream s;
  s << "0x" << std::hex << v;
  return s.str();
}

}  // namespace

bool load_elf(const std::string& path, SimMemory& mem, ElfImage* image, std::string* err) {
  ELFIO::elfio elf;
  if (!elf.load(path)) {
    *err = path + ": cannot open or not an ELF file";
    return false;
  }
  if (elf.get_class() != ELFIO::ELFCLASS64 || elf.get_encoding() != ELFIO::ELFDATA2LSB ||
      elf.get_machine() != ELFIO::EM_RISCV) {
    *err = path + ": not a little-endian RISC-V ELF64 file";
    return false;
  }
  image->entry = elf.get_entry();
  image->symbols.clear();

  for (const auto& seg : elf.segments) {
    if (seg->get_type() != ELFIO::PT_LOAD || seg->get_memory_size() == 0) continue;
    const uint64_t paddr = seg->get_physical_address();
    const uint64_t memsz = seg->get_memory_size();
    const uint64_t filesz = seg->get_file_size();
    if (filesz > memsz) {
      *err = path + ": malformed segment at " + hex(paddr);
      return false;
    }
    if (!mem.contains(paddr, memsz)) {
      *err = path + ": segment at " + hex(paddr) + " (" + hex(memsz) +
             " bytes) is outside memory";
      return false;
    }
    if (filesz > 0) {
      mem.write(paddr, reinterpret_cast<const uint8_t*>(seg->get_data()), filesz);
    }
    const std::vector<uint8_t> zeros(memsz - filesz, 0);
    mem.write(paddr + filesz, zeros.data(), zeros.size());
  }

  for (const auto& sec : elf.sections) {
    if (sec->get_type() != ELFIO::SHT_SYMTAB) continue;
    const ELFIO::symbol_section_accessor syms(elf, sec.get());
    for (ELFIO::Elf_Xword i = 0; i < syms.get_symbols_num(); ++i) {
      std::string name;
      ELFIO::Elf64_Addr value = 0;
      ELFIO::Elf_Xword size = 0;
      unsigned char bind = 0, type = 0, other = 0;
      ELFIO::Elf_Half shndx = 0;
      if (syms.get_symbol(i, name, value, size, bind, type, shndx, other) && !name.empty()) {
        image->symbols[name] = value;
      }
    }
  }
  return true;
}
