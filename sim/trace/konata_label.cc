// SPDX-License-Identifier: Apache-2.0
#include "konata_label.h"

#include <sstream>

#ifdef OVERISC_HAVE_SPIKE_DISASM
#include <riscv/disasm.h>
#include <riscv/isa_parser.h>
#endif

std::string konata_label(uint64_t pc, uint32_t insn) {
  std::ostringstream s;
  s << std::hex << pc << ": ";
#ifdef OVERISC_HAVE_SPIKE_DISASM
  static const isa_parser_t isa("rv64imac_zicsr_zifencei_zicntr", "msu");
  static const disassembler_t dis(&isa);
  s << dis.disassemble(insn_t(insn));
#else
  s << "insn 0x" << insn;
#endif
  return s.str();
}
