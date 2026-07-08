#include "cpu/disassembler/Disassembler.h"
#include "brew/BrewLoader.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/VirtualMemory.h"
#include "cpu/memory/EndianMemory.h"
#include <memory>
#include <vector>
#include <fstream>
#include <iomanip>

namespace {

uint32_t read_le16(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 1 >= data.size()) {
        return 0;
    }
    return static_cast<uint32_t>(data[pos]) |
           (static_cast<uint32_t>(data[pos + 1]) << 8);
}

uint32_t read_le32(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 3 >= data.size()) {
        return 0;
    }
    return static_cast<uint32_t>(data[pos]) |
           (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

} // namespace

static bool disassemble_file(
    const std::string& path, std::ostream& out,
    addr_t base_address, size_t count, bool thumb, size_t file_offset)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    if (data.empty()) return false;
    if (file_offset >= data.size()) {
        out << "file offset outside input: 0x" << std::hex << file_offset
            << " size=0x" << data.size() << std::dec << "\n";
        return false;
    }

    VirtualMemory vmem;
    EndianMemory  mem(&vmem, LittleEndian);
    const bool can_preserve_file_layout = base_address >= file_offset;
    const addr_t map_base = can_preserve_file_layout
        ? static_cast<addr_t>(base_address - file_offset)
        : base_address;
    const size_t write_offset = can_preserve_file_layout ? 0 : file_offset;
    const size_t write_size = data.size() - write_offset;
    vmem.alloc_protect(map_base, write_size,
                       Memory::Read | Memory::Write | Memory::Execute);
    vmem.write(map_base, std::string(data.begin() + static_cast<std::ptrdiff_t>(write_offset), data.end()));

    auto cpu_up = std::make_unique<CPU>(mem);
    CPU& cpu = *cpu_up;

    size_t stride = thumb ? 2 : 4;
    for (size_t i = 0; i < count; ++i) {
        const size_t pos = file_offset + i * stride;
        if (pos + stride > data.size()) break;
        const uint32_t pc = base_address + static_cast<uint32_t>(i * stride);
        const uint32_t op = thumb ? read_le16(data, pos) : read_le32(data, pos);
        out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc
            << "  +0x" << std::setw(6) << pos
            << "  op=0x" << std::setw(thumb ? 4 : 8) << op
            << ": " << cpu.disassemble(pc, thumb) << "\n";
    }
    out << std::dec << std::setfill(' ');
    return true;
}

bool Disassembler::disassemble_raw_arm_le(
    const std::string& path, std::ostream& out,
    addr_t base_address, size_t count, size_t file_offset)
{
    return disassemble_file(path, out, base_address, count, false, file_offset);
}

bool Disassembler::disassemble_raw_thumb_le(
    const std::string& path, std::ostream& out,
    addr_t base_address, size_t count, size_t file_offset)
{
    return disassemble_file(path, out, base_address, count, true, file_offset);
}

bool Disassembler::disassemble_brew_module_le(
    const std::string& path, std::ostream& out,
    addr_t start_address, size_t count, bool thumb)
{
    constexpr addr_t kBaseAddress = 0x10000000u;
    constexpr uint32_t kMinModuleAllocation = 0x800000u;

    VirtualMemory vmem;
    EndianMemory mem(&vmem, LittleEndian);
    BrewLoader loader(mem, &vmem, true);

    vmem.alloc_protect(kBaseAddress, kMinModuleAllocation, Memory::Read | Memory::Write | Memory::Execute);
    std::unique_ptr<BrewModule> mod(loader.load_module(path, kBaseAddress));
    if (!mod) {
        return false;
    }

    const uint32_t image_limit = std::max(mod->image_size, kMinModuleAllocation);
    auto cpu_up = std::make_unique<CPU>(mem);
    CPU& cpu = *cpu_up;
    cpu.set_ropi_params(mod->base_address, image_limit);
    cpu.power();

    addr_t pc = start_address ? start_address : mod->entry_point;
    out << "Module: " << path << "\n";
    out << "  base=0x" << std::hex << std::setw(8) << std::setfill('0') << mod->base_address
        << " entry=0x" << std::setw(8) << mod->entry_point
        << " modinfo=0x" << std::setw(8) << mod->mod_info_point
        << " image=0x" << mod->image_size
        << " code=0x" << mod->code_size
        << " data=0x" << mod->data_size
        << " bss=0x" << mod->bss_size
        << " header=0x" << mod->header_size
        << "\n";
    out << "  disasm-start=0x" << std::setw(8) << pc << std::dec << std::setfill(' ') << "\n";

    const addr_t stride = thumb ? 2u : 4u;
    for (size_t i = 0; i < count; ++i, pc += stride) {
        const bool in_high_image = pc >= mod->base_address && pc < mod->base_address + image_limit;
        const bool in_low_mirror = pc < image_limit;
        if (!in_high_image && !in_low_mirror) {
            out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc
                << ": <outside module image>\n" << std::dec << std::setfill(' ');
            break;
        }
        uint32_t op = thumb ? mem.read_value(pc, EndianMemory::Halfword) : mem.read_value(pc);
        out << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc
            << "  +0x" << std::setw(6)
            << (in_high_image ? pc - mod->base_address : pc)
            << "  op=0x" << std::setw(thumb ? 4 : 8) << op
            << ": " << cpu.disassemble(pc, thumb) << "\n";
    }
    out << std::dec << std::setfill(' ');
    return true;
}
