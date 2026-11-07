// SPDX-License-Identifier: Apache-2.0
#include "konata_label.h"

#include <sstream>

#ifdef OVERISC_HAVE_SPIKE_DISASM
#include <riscv/disasm.h>
#include <riscv/isa_parser.h>
#endif

std::string konata_label(uint32_t inst) {
#ifdef OVERISC_HAVE_SPIKE_DISASM
  static const isa_parser_t isa("rv64imac_zicsr_zifencei_zicntr", "msu");
  static const disassembler_t dis(&isa);
  return dis.disassemble(insn_t(inst));
#else
  std::ostringstream s;
  s << "inst 0x" << std::hex << inst;
  return s.str();
#endif
}
