// SPDX-License-Identifier: Apache-2.0
// Label-pane text of an instruction in the Kanata log: "<pc>: <disassembly>".
// Uses Spike's disassembler when it was found at build time, otherwise prints the encoding.
#pragma once

#include <cstdint>
#include <string>

std::string konata_label(uint64_t pc, uint32_t insn);
