#include "brew/BrewLoader.h"
#include "cpu/memory/VirtualMemory.h"
#include <fstream>
#include <iostream>
#include <cstring>

BrewLoader::BrewLoader(Memory& memory, VirtualMemory* vmem, bool quiet)
    : memory_(memory), vmem_(vmem), quiet_(quiet)
{
}

bool BrewLoader::read_file(const std::string& path, std::vector<uint8_t>& buffer)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    buffer.resize(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return true;
    }
    return false;
}

uint32_t BrewLoader::read_le32(const std::vector<uint8_t>& buffer, size_t offset)
{
    if (offset + 4 > buffer.size()) return 0;
    return buffer[offset] | (buffer[offset + 1] << 8) | (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
}

BrewModule* BrewLoader::load_module(const std::string& path, addr_t load_address)
{
    std::vector<uint8_t> buffer;
    if (!read_file(path, buffer)) {
        std::cerr << "Failed to read module: " << path << std::endl;
        return nullptr;
    }

    BrewModule* mod = new BrewModule();
    mod->name = path;
    mod->base_address = load_address;
    mod->is_raw = true;
    mod->header_size = 0;
    mod->image_size = static_cast<uint32_t>(buffer.size());

    // Detect BREW header at 0x0 or 0x40
    size_t header_offset = 0;
    bool found_header = false;
    
    if (buffer.size() >= 0x10 && buffer[8] == 'B' && buffer[9] == 'R' && buffer[10] == 'E' && buffer[11] == 'W') {
        header_offset = 0;
        found_header = true;
    } else if (buffer.size() >= 0x50 && buffer[0x48] == 'B' && buffer[0x49] == 'R' && buffer[0x4A] == 'E' && buffer[0x4B] == 'W') {
        header_offset = 0x40;
        found_header = true;
    }

    if (found_header) {
        mod->is_raw = false;
        uint32_t branch0 = read_le32(buffer, header_offset + 0);
        uint32_t branch1 = read_le32(buffer, header_offset + 4);
        
        auto arm_branch_target = [](uint32_t insn, uint32_t pc) {
            int32_t imm24 = static_cast<int32_t>(insn & 0x00FFFFFF);
            if (imm24 & 0x00800000) imm24 |= 0xFF000000;
            return pc + 8 + (imm24 << 2);
        };

        mod->entry_point = load_address + header_offset + arm_branch_target(branch0, 0);
        mod->mod_info_point = load_address + header_offset + arm_branch_target(branch1, 4);
        mod->code_size = read_le32(buffer, header_offset + 0x1C);
        mod->data_size = read_le32(buffer, header_offset + 0x20);
        mod->bss_size = read_le32(buffer, header_offset + 0x24);
        mod->header_size = read_le32(buffer, header_offset + 0x10);

        if (!quiet_) {
            std::cout << "Loaded BREW module with header at 0x" << std::hex << header_offset << ": " << path << std::endl;
            std::cout << "  Entry point: 0x" << std::hex << mod->entry_point << std::endl;
        }
    } else {
        // Raw ARM binary
        mod->entry_point = load_address;
        mod->mod_info_point = 0; // Not available or unknown
        mod->code_size = static_cast<uint32_t>(buffer.size());
        mod->data_size = 0;
        mod->bss_size = 0;
        mod->header_size = 0;

        if (!quiet_) {
            std::cout << "Loaded raw module: " << path << std::endl;
            std::cout << "  Entry point: 0x" << std::hex << mod->entry_point << std::endl;
        }
    }

    // Write code+header to load_address (ROPI: code runs at load_address)
    std::string data(buffer.begin(), buffer.end());
    memory_.write(load_address, data);
    // Mirror the image at base 0 so link-time pointers work (RVCT/BREW common pattern).
    // Use alias_pages to share host memory — avoids dual-copy data incoherence.
    // Mirror generously: code references data at link-time addresses well past the file size
    // (e.g. Quake references 0x64C800 while file is only 0x6B440). 8MB covers common cases.
    uint32_t mirror_size = std::max(static_cast<uint32_t>(buffer.size()), 0x800000u);
    if (vmem_) {
        vmem_->alias_pages(0, load_address, mirror_size);
        if (!quiet_) {
            printf("  Link-time mirror: VA=0x00000000 aliased to 0x%08X size=0x%X\n", load_address, mirror_size);
        }
    } else {
        memory_.write(0, data);
        if (!quiet_) {
            printf("  Link-time mirror: VA=0x00000000 size=0x%zX\n", buffer.size());
        }
    }

    if (!found_header) {
        // For raw modules, also map a small BSS to avoid immediate OOB
        std::string bss(64 * 1024, '\0');
        memory_.write(static_cast<addr_t>(buffer.size()), bss);
    }

    // For BREW ROPI modules: map data section and BSS at their absolute link-time VAs.
    // The linker base is 0x0, so VA == file offset. Code ends at header_offset+0x40+code_size;
    // data immediately follows in the file at the same file offset = absolute VA.
    if (found_header && (mod->data_size > 0 || mod->bss_size > 0)) {
        const uint32_t BREW_HDR = 0x40;
        addr_t data_file_off = header_offset + BREW_HDR + mod->code_size;
        addr_t data_va       = data_file_off; // link base 0 → VA == file offset

        if (mod->data_size > 0 && data_file_off + mod->data_size <= buffer.size()) {
            std::string dsect(buffer.begin() + data_file_off,
                              buffer.begin() + data_file_off + mod->data_size);
            memory_.write(data_va, dsect);
            if (!quiet_) {
                printf("  Data section: VA=0x%08X size=0x%X\n", data_va, mod->data_size);
            }
        }

        if (mod->bss_size > 0) {
            addr_t bss_va = data_va + mod->data_size;
            std::string bss(mod->bss_size, '\0');
            memory_.write(bss_va, bss);
            if (!quiet_) {
                printf("  BSS section:  VA=0x%08X size=0x%X\n", bss_va, mod->bss_size);
            }
        }
    }

    return mod;
}
