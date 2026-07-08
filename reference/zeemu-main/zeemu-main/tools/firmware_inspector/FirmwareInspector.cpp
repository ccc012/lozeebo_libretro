#include "tools/firmware_inspector/FirmwareInspector.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kPartitionMagic1 = 0x55ee73aa;
constexpr uint32_t kPartitionMagic2 = 0xe35ebddb;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEntrySize = 28;
constexpr std::size_t kNameSize = 16;
constexpr uint64_t kNandSize = 0x8000000;
constexpr uint64_t kBlockSize = 0x20000;
constexpr uint64_t kPageSize = 0x800;
constexpr uint64_t kPagesPerBlock = kBlockSize / kPageSize;
constexpr std::size_t kDataChunkSize = 0x200;
constexpr std::size_t kSpareChunkSize = 0x10;
constexpr std::size_t kSpareStride = kDataChunkSize + kSpareChunkSize;

struct PartitionEntry {
    std::string name;
    uint32_t start;
    uint32_t length;
    uint32_t attr;
};

struct PartitionTable {
    std::size_t offset;
    uint32_t version;
    uint32_t count;
    std::vector<PartitionEntry> entries;
};

struct FirmwareImage {
    std::string data;
    bool had_spare = false;
    std::size_t spare_chunks = 0;
    std::size_t non_ff_spare_chunks = 0;
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool is_all_ff(const std::string& bytes, std::size_t offset, std::size_t size)
{
    for (std::size_t i = 0; i < size && offset + i < bytes.size(); ++i) {
        if (static_cast<unsigned char>(bytes[offset + i]) != 0xff) {
            return false;
        }
    }
    return true;
}

FirmwareImage normalize_firmware_image(const std::string& bytes)
{
    FirmwareImage image;
    if (bytes.size() % kSpareStride != 0) {
        image.data = bytes;
        return image;
    }

    const std::size_t chunks = bytes.size() / kSpareStride;
    const std::size_t data_size = chunks * kDataChunkSize;
    if (data_size != kNandSize) {
        image.data = bytes;
        return image;
    }

    image.had_spare = true;
    image.spare_chunks = chunks;
    image.data.reserve(data_size);
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const std::size_t offset = chunk * kSpareStride;
        image.data.append(bytes.data() + offset, kDataChunkSize);
        if (!is_all_ff(bytes, offset + kDataChunkSize, kSpareChunkSize)) {
            ++image.non_ff_spare_chunks;
        }
    }
    return image;
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

std::string read_name(const std::string& bytes, std::size_t offset)
{
    std::string name;
    for (std::size_t i = 0; i < kNameSize && offset + i < bytes.size(); ++i) {
        const char ch = bytes[offset + i];
        if (ch == '\0') {
            break;
        }
        name.push_back(ch);
    }
    return name;
}

bool is_partition_name(const std::string& name)
{
    if (name.rfind("0:", 0) != 0 || name.size() <= 2) {
        return false;
    }
    return std::all_of(name.begin() + 2, name.end(), [](unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    });
}

std::vector<PartitionTable> find_partition_tables(const std::string& bytes)
{
    std::vector<PartitionTable> tables;
    for (std::size_t offset = 0; offset + kHeaderSize + kEntrySize <= bytes.size(); offset += 4) {
        if (read_le32(bytes, offset) != kPartitionMagic1 || read_le32(bytes, offset + 4) != kPartitionMagic2) {
            continue;
        }

        PartitionTable table;
        table.offset = offset;
        table.version = read_le32(bytes, offset + 8);
        table.count = read_le32(bytes, offset + 12);

        if (table.count == 0 || table.count > 64 || offset + kHeaderSize + table.count * kEntrySize > bytes.size()) {
            continue;
        }

        bool valid_names = true;
        for (uint32_t i = 0; i < table.count; ++i) {
            const std::size_t entry_offset = offset + kHeaderSize + i * kEntrySize;
            PartitionEntry entry;
            entry.name = read_name(bytes, entry_offset);
            entry.start = read_le32(bytes, entry_offset + 16);
            entry.length = read_le32(bytes, entry_offset + 20);
            entry.attr = read_le32(bytes, entry_offset + 24);
            valid_names = valid_names && is_partition_name(entry.name);
            table.entries.push_back(entry);
        }

        if (valid_names) {
            tables.push_back(table);
        }
    }
    return tables;
}

void print_hex_value(std::ostream& out, uint64_t value, int width)
{
    out << "0x" << std::hex << std::setw(width) << std::setfill('0') << value
        << std::dec << std::setfill(' ');
}
}

bool FirmwareInspector::inspect(const std::string& firmware_path, std::ostream& out)
{
    const std::filesystem::path path(firmware_path);
    if (!std::filesystem::exists(path)) {
        std::cerr << "Firmware not found: " << firmware_path << std::endl;
        return false;
    }

    const std::string raw_bytes = read_file(path);
    if (raw_bytes.empty()) {
        std::cerr << "Firmware is empty: " << firmware_path << std::endl;
        return false;
    }
    const FirmwareImage image = normalize_firmware_image(raw_bytes);
    const std::string& bytes = image.data;

    out << "Firmware: " << path.string() << '\n';
    out << "File size: " << raw_bytes.size() << " bytes\n";
    if (image.had_spare) {
        out << "Format: interleaved spare/OOB dump\n";
        out << "  layout: 0x200 data + 0x10 spare repeated\n";
        out << "  data size after stripping spare: " << bytes.size() << " bytes\n";
        out << "  spare chunks: " << image.spare_chunks << '\n';
        out << "  non-0xff spare chunks: " << image.non_ff_spare_chunks << "\n\n";
    } else {
        out << "Format: data-only dump\n";
        out << "Data size: " << bytes.size() << " bytes\n\n";
    }

    const std::vector<PartitionTable> tables = find_partition_tables(bytes);
    out << "NAND geometry used for partition addresses:\n";
    out << "  NAND size: 0x" << std::hex << kNandSize << std::dec << " bytes\n";
    out << "  block size: 0x" << std::hex << kBlockSize << std::dec << " bytes\n";
    out << "  page size: 0x" << std::hex << kPageSize << std::dec << " bytes\n";
    out << "  pages per block: 0x" << std::hex << kPagesPerBlock << std::dec << "\n\n";

    out << "Partition tables: " << tables.size() << "\n\n";
    for (const PartitionTable& table : tables) {
        out << "Table at 0x" << std::hex << table.offset
            << " version=" << std::dec << table.version
            << " entries=" << table.count << '\n';

        out << "  " << std::left
            << std::setw(14) << "name"
            << std::right
            << std::setw(12) << "block"
            << std::setw(12) << "blocks"
            << std::setw(12) << "attr"
            << std::setw(14) << "address"
            << std::setw(14) << "end"
            << std::setw(12) << "page"
            << std::setw(12) << "page_end"
            << '\n';

        for (const PartitionEntry& entry : table.entries) {
            const bool has_start = entry.start != 0xffffffff;
            uint64_t blocks = entry.length;
            if (has_start && entry.length == 0xffffffff) {
                blocks = (kNandSize / kBlockSize) - entry.start;
            }
            const bool has_extent = has_start && blocks != 0xffffffff;
            const uint64_t address = has_extent ? static_cast<uint64_t>(entry.start) * kBlockSize : 0;
            const uint64_t size = has_extent ? blocks * kBlockSize : 0;
            const uint64_t end = has_extent ? address + size - 1 : 0;
            const uint64_t page = has_extent ? static_cast<uint64_t>(entry.start) * kPagesPerBlock : 0;
            const uint64_t page_end = has_extent ? page + blocks * kPagesPerBlock - 1 : 0;

            out << "  " << std::left << std::setw(14) << entry.name << std::right;
            print_hex_value(out, entry.start, 8);
            out << "  ";
            print_hex_value(out, blocks, 8);
            out << "  ";
            print_hex_value(out, entry.attr, 8);

            if (has_extent) {
                out << "  ";
                print_hex_value(out, address, 10);
                out << "  ";
                print_hex_value(out, end, 10);
                out << "  ";
                print_hex_value(out, page, 8);
                out << "  ";
                print_hex_value(out, page_end, 8);
            } else {
                out << "  " << std::setw(12) << "-"
                    << "  " << std::setw(12) << "-"
                    << "  " << std::setw(10) << "-"
                    << "  " << std::setw(10) << "-";
            }
            out << std::dec << '\n';
        }
        out << "\n";
    }

    out << "Note: length 0xffffffff is treated as extending to the end of the 128 MiB NAND.\n";
    return true;
}
