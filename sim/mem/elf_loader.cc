// SPDX-License-Identifier: Apache-2.0
#include "elf_loader.h"

#include <elf.h>

#include <cstring>
#include <fstream>
#include <iterator>
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
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    *err = path + ": cannot open";
    return false;
  }
  const std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
  auto in_file = [&](uint64_t off, uint64_t len) {
    return off <= buf.size() && len <= buf.size() - off;
  };

  Elf64_Ehdr eh;
  if (!in_file(0, sizeof(eh))) {
    *err = path + ": too short for an ELF header";
    return false;
  }
  std::memcpy(&eh, buf.data(), sizeof(eh));
  if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
      eh.e_ident[EI_DATA] != ELFDATA2LSB || eh.e_machine != EM_RISCV) {
    *err = path + ": not a little-endian RISC-V ELF64 file";
    return false;
  }
  image->entry = eh.e_entry;
  image->symbols.clear();

  // segments
  for (unsigned i = 0; i < eh.e_phnum; ++i) {
    Elf64_Phdr ph;
    const uint64_t off = eh.e_phoff + uint64_t{i} * eh.e_phentsize;
    if (!in_file(off, sizeof(ph))) {
      *err = path + ": truncated program header";
      return false;
    }
    std::memcpy(&ph, buf.data() + off, sizeof(ph));
    if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
    if (ph.p_filesz > ph.p_memsz || !in_file(ph.p_offset, ph.p_filesz)) {
      *err = path + ": malformed segment " + std::to_string(i);
      return false;
    }
    if (!mem.contains(ph.p_paddr, ph.p_memsz)) {
      *err = path + ": segment at " + hex(ph.p_paddr) + " (" + hex(ph.p_memsz) +
             " bytes) is outside memory";
      return false;
    }
    mem.write(ph.p_paddr, buf.data() + ph.p_offset, ph.p_filesz);
    const std::vector<uint8_t> zeros(ph.p_memsz - ph.p_filesz, 0);
    mem.write(ph.p_paddr + ph.p_filesz, zeros.data(), zeros.size());
  }

  // symbols
  for (unsigned i = 0; i < eh.e_shnum; ++i) {
    Elf64_Shdr sh;
    const uint64_t off = eh.e_shoff + uint64_t{i} * eh.e_shentsize;
    if (!in_file(off, sizeof(sh))) break;
    std::memcpy(&sh, buf.data() + off, sizeof(sh));
    if (sh.sh_type != SHT_SYMTAB || sh.sh_link >= eh.e_shnum) continue;
    Elf64_Shdr strh;
    const uint64_t stroff = eh.e_shoff + uint64_t{sh.sh_link} * eh.e_shentsize;
    if (!in_file(stroff, sizeof(strh))) break;
    std::memcpy(&strh, buf.data() + stroff, sizeof(strh));
    if (!in_file(sh.sh_offset, sh.sh_size) || !in_file(strh.sh_offset, strh.sh_size)) break;
    for (uint64_t s = 0; s + sizeof(Elf64_Sym) <= sh.sh_size; s += sizeof(Elf64_Sym)) {
      Elf64_Sym sym;
      std::memcpy(&sym, buf.data() + sh.sh_offset + s, sizeof(sym));
      if (sym.st_name == 0 || sym.st_name >= strh.sh_size) continue;
      const char* name = reinterpret_cast<const char*>(buf.data() + strh.sh_offset + sym.st_name);
      const size_t maxlen = strh.sh_size - sym.st_name;
      image->symbols[std::string(name, strnlen(name, maxlen))] = sym.st_value;
    }
  }
  return true;
}
