#ifndef ZEEMU_DISASSEMBLER_H
#define ZEEMU_DISASSEMBLER_H

#include <string>
#include <iostream>
#include "cpu/cpu.h"

class Disassembler {
public:
    static bool disassemble_raw_arm_le(const std::string& path, std::ostream& out, addr_t base_address, size_t count, size_t file_offset = 0);
    static bool disassemble_raw_thumb_le(const std::string& path, std::ostream& out, addr_t base_address, size_t count, size_t file_offset = 0);
    static bool disassemble_brew_module_le(const std::string& path, std::ostream& out, addr_t start_address, size_t count, bool thumb = false);
};

#endif
