#include "tools/firmware_inspector/SplitFirmwareInspector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr uint64_t kBlockSize = 0x20000;

struct KnownPartition {
    const char* name;
    uint32_t start_block;
    uint32_t blocks;
};

struct Elf32Header {
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct Elf32ProgramHeader {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

const KnownPartition kKnownPartitions[] = {
    {"MIBIB", 0x000, 0x00a},
    {"QCSBL", 0x00a, 0x002},
    {"OEMSBL1", 0x00c, 0x003},
    {"OEMSBL2", 0x00f, 0x003},
    {"AMSS", 0x012, 0x0a5},
    {"APPSBL", 0x0b7, 0x003},
    {"FOTA", 0x0ba, 0x002},
    {"EFS2", 0x0bc, 0x02a},
    {"APPS", 0x0e6, 0x0a9},
    {"FTL", 0x18f, 0x002},
    {"EFS2APPS", 0x191, 0x26f},
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

uint16_t read_le16(const std::string& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size()) {
        return 0;
    }
    return static_cast<unsigned char>(bytes[offset])
        | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
}

uint32_t read_le32(const std::string& bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return static_cast<unsigned char>(bytes[offset])
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

bool is_elf32_little_arm(const std::string& bytes)
{
    return bytes.size() >= 0x34
        && static_cast<unsigned char>(bytes[0]) == 0x7f
        && bytes[1] == 'E'
        && bytes[2] == 'L'
        && bytes[3] == 'F'
        && static_cast<unsigned char>(bytes[4]) == 1
        && static_cast<unsigned char>(bytes[5]) == 1
        && read_le16(bytes, 0x12) == 0x28;
}

Elf32Header read_elf_header(const std::string& bytes)
{
    Elf32Header header{};
    header.type = read_le16(bytes, 0x10);
    header.machine = read_le16(bytes, 0x12);
    header.version = read_le32(bytes, 0x14);
    header.entry = read_le32(bytes, 0x18);
    header.phoff = read_le32(bytes, 0x1c);
    header.shoff = read_le32(bytes, 0x20);
    header.flags = read_le32(bytes, 0x24);
    header.ehsize = read_le16(bytes, 0x28);
    header.phentsize = read_le16(bytes, 0x2a);
    header.phnum = read_le16(bytes, 0x2c);
    header.shentsize = read_le16(bytes, 0x2e);
    header.shnum = read_le16(bytes, 0x30);
    header.shstrndx = read_le16(bytes, 0x32);
    return header;
}

Elf32ProgramHeader read_program_header(const std::string& bytes, std::size_t offset)
{
    Elf32ProgramHeader header{};
    header.type = read_le32(bytes, offset);
    header.offset = read_le32(bytes, offset + 4);
    header.vaddr = read_le32(bytes, offset + 8);
    header.paddr = read_le32(bytes, offset + 12);
    header.filesz = read_le32(bytes, offset + 16);
    header.memsz = read_le32(bytes, offset + 20);
    header.flags = read_le32(bytes, offset + 24);
    header.align = read_le32(bytes, offset + 28);
    return header;
}

std::string flags_to_string(uint32_t flags)
{
    std::string text;
    text.push_back((flags & 4) ? 'R' : '-');
    text.push_back((flags & 2) ? 'W' : '-');
    text.push_back((flags & 1) ? 'X' : '-');
    return text;
}

void print_hex(std::ostream& out, uint64_t value, int width)
{
    out << "0x" << std::hex << std::setw(width) << std::setfill('0') << value
        << std::dec << std::setfill(' ');
}

void print_elf_summary(const std::string& bytes, std::ostream& out)
{
    const Elf32Header elf = read_elf_header(bytes);
    out << "  format: ELF32 little-endian ARM\n";
    out << "  entry: ";
    print_hex(out, elf.entry, 8);
    out << "  flags: ";
    print_hex(out, elf.flags, 8);
    out << "  program headers: " << elf.phnum << "\n";

    if (elf.phoff == 0 || elf.phentsize < 32) {
        return;
    }

    out << "  " << std::left
        << std::setw(6) << "#"
        << std::setw(8) << "type"
        << std::right
        << std::setw(12) << "offset"
        << std::setw(13) << "vaddr"
        << std::setw(13) << "paddr"
        << std::setw(12) << "filesz"
        << std::setw(12) << "memsz"
        << std::setw(8) << "flags"
        << '\n';

    for (uint16_t i = 0; i < elf.phnum; ++i) {
        const std::size_t offset = elf.phoff + static_cast<std::size_t>(i) * elf.phentsize;
        if (offset + 32 > bytes.size()) {
            break;
        }

        const Elf32ProgramHeader ph = read_program_header(bytes, offset);
        out << "  " << std::left
            << std::setw(6) << i
            << std::setw(8) << (ph.type == 1 ? "LOAD" : std::to_string(ph.type))
            << std::right;
        print_hex(out, ph.offset, 8);
        out << " ";
        print_hex(out, ph.vaddr, 8);
        out << " ";
        print_hex(out, ph.paddr, 8);
        out << " ";
        print_hex(out, ph.filesz, 8);
        out << " ";
        print_hex(out, ph.memsz, 8);
        out << " " << std::setw(8) << flags_to_string(ph.flags) << '\n';
    }
}

std::string upper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

std::string find_partition_name(const std::filesystem::path& file)
{
    const std::string stem = upper(file.stem().string());
    for (const KnownPartition& partition : kKnownPartitions) {
        const std::string name(partition.name);
        if (stem.size() >= name.size() && stem.rfind(name) == stem.size() - name.size()) {
            return name;
        }
    }
    return {};
}
}

bool SplitFirmwareInspector::inspect(const std::string& split_directory, std::ostream& out)
{
    const std::filesystem::path directory(split_directory);
    if (!std::filesystem::is_directory(directory)) {
        std::cerr << "Split firmware directory not found: " << split_directory << std::endl;
        return false;
    }

    std::map<std::string, std::filesystem::path> files_by_partition;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string partition = find_partition_name(entry.path());
        if (!partition.empty()) {
            files_by_partition[partition] = entry.path();
        }
    }

    out << "Split firmware: " << directory.string() << "\n\n";
    out << "Known partitions:\n";
    out << "  " << std::left
        << std::setw(10) << "name"
        << std::right
        << std::setw(12) << "block"
        << std::setw(12) << "blocks"
        << std::setw(14) << "address"
        << std::setw(14) << "size"
        << std::setw(14) << "file"
        << '\n';

    for (const KnownPartition& partition : kKnownPartitions) {
        const uint64_t address = static_cast<uint64_t>(partition.start_block) * kBlockSize;
        const uint64_t expected_size = static_cast<uint64_t>(partition.blocks) * kBlockSize;
        const auto file_it = files_by_partition.find(partition.name);

        out << "  " << std::left << std::setw(10) << partition.name << std::right;
        print_hex(out, partition.start_block, 8);
        out << "  ";
        print_hex(out, partition.blocks, 8);
        out << "  ";
        print_hex(out, address, 10);
        out << "  ";
        print_hex(out, expected_size, 10);

        if (file_it == files_by_partition.end()) {
            out << "  " << std::setw(12) << "missing";
        } else {
            const uint64_t actual_size = std::filesystem::file_size(file_it->second);
            out << "  ";
            print_hex(out, actual_size, 10);
            if (actual_size != expected_size) {
                out << " size-mismatch";
            }
        }
        out << '\n';
    }

    out << "\nDetected files:\n";
    for (const auto& [partition, path] : files_by_partition) {
        const std::string bytes = read_file(path);
        out << "\n" << partition << ": " << path.filename().string()
            << " (" << bytes.size() << " bytes)\n";

        if (is_elf32_little_arm(bytes)) {
            print_elf_summary(bytes, out);
        } else {
            out << "  format: raw/unknown\n";
            out << "  first word: ";
            print_hex(out, read_le32(bytes, 0), 8);
            out << '\n';
        }
    }

    return true;
}
