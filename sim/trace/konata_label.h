// SPDX-License-Identifier: Apache-2.0
// Instruction text for the label pane of the Kanata log.
// Uses Spike's disassembler when it was found at build time, otherwise prints the encoding.
#pragma once

#include <cstdint>
#include <string>

std::string konata_label(uint32_t inst);
