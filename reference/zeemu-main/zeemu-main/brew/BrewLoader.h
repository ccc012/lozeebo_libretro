#ifndef ZEEMU_BREW_LOADER_H_
#define ZEEMU_BREW_LOADER_H_

#include <string>
#include <vector>
#include "cpu/cpu.h"
#include "brew/BrewModule.h"
#include "cpu/memory/EndianMemory.h"

class VirtualMemory;

class BrewLoader {
public:
    explicit BrewLoader(Memory& memory, VirtualMemory* vmem = nullptr, bool quiet = false);

    BrewModule* load_module(const std::string& path, addr_t load_address);

private:
    bool read_file(const std::string& path, std::vector<uint8_t>& buffer);
    uint32_t read_le32(const std::vector<uint8_t>& buffer, size_t offset);

    Memory& memory_;
    VirtualMemory* vmem_;
    bool quiet_;
};

#endif
