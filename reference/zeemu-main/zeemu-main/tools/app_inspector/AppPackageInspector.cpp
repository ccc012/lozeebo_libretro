#include "tools/app_inspector/AppPackageInspector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

std::string read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<fs::path> find_by_extension(const fs::path& root, const std::string& extension)
{
    std::vector<fs::path> paths;
    if (!fs::exists(root)) {
        return paths;
    }

    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

std::vector<std::string> ascii_strings(const std::string& bytes, std::size_t min_len = 4)
{
    std::vector<std::string> strings;
    std::string current;
    for (unsigned char byte : bytes) {
        if ((byte >= 0x20 && byte <= 0x7e) || byte == '\t') {
            current.push_back(static_cast<char>(byte));
        } else {
            if (current.size() >= min_len) {
                strings.push_back(current);
            }
            current.clear();
        }
    }
    if (current.size() >= min_len) {
        strings.push_back(current);
    }
    return strings;
}

std::vector<std::string> utf16le_strings(const std::string& bytes, std::size_t min_len = 4)
{
    std::vector<std::string> strings;
    std::string current;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const unsigned char lo = static_cast<unsigned char>(bytes[i]);
        const unsigned char hi = static_cast<unsigned char>(bytes[i + 1]);
        if (hi == 0 && lo >= 0x20 && lo <= 0x7e) {
            current.push_back(static_cast<char>(lo));
        } else {
            if (current.size() >= min_len) {
                strings.push_back(current);
            }
            current.clear();
        }
    }
    if (current.size() >= min_len) {
        strings.push_back(current);
    }
    return strings;
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

bool has_magic(const std::string& bytes, std::size_t offset, const char* magic)
{
    for (std::size_t i = 0; magic[i] != '\0'; ++i) {
        if (offset + i >= bytes.size() || bytes[offset + i] != magic[i]) {
            return false;
        }
    }
    return true;
}

bool looks_like_arm_code(const std::string& bytes)
{
    if (bytes.size() < 16 || bytes.size() % 4 != 0) {
        return false;
    }

    const uint32_t first = read_le32(bytes, 0);
    const uint32_t cond = first >> 28;
    return cond <= 0xe;
}

bool looks_like_brew_mod(const std::string& bytes)
{
    return bytes.size() >= 0x48
        && has_magic(bytes, 8, "BREW")
        && (read_le32(bytes, 0) & 0x0f000000) == 0x0a000000
        && (read_le32(bytes, 4) & 0x0f000000) == 0x0a000000;
}

uint32_t arm_branch_target(uint32_t instruction, uint32_t pc)
{
    int32_t imm24 = static_cast<int32_t>(instruction & 0x00ffffff);
    if ((imm24 & 0x00800000) != 0) {
        imm24 |= static_cast<int32_t>(0xff000000);
    }
    return pc + 8 + (static_cast<uint32_t>(imm24) << 2);
}

void print_hex32(std::ostream& out, uint32_t value)
{
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value
        << std::dec << std::setfill(' ');
}

void print_brew_mod_summary(const std::string& bytes, std::ostream& out)
{
    out << "  format guess: BREW MOD ARM little-endian\n";
    out << "  entry branch target: ";
    print_hex32(out, arm_branch_target(read_le32(bytes, 0), 0));
    out << '\n';
    out << "  secondary branch target: ";
    print_hex32(out, arm_branch_target(read_le32(bytes, 4), 4));
    out << '\n';
    out << "  header size: ";
    print_hex32(out, read_le32(bytes, 0x10));
    out << '\n';
    out << "  version: ";
    print_hex32(out, read_le32(bytes, 0x14));
    out << '\n';
    out << "  code/data size fields: ";
    print_hex32(out, read_le32(bytes, 0x1c));
    out << " ";
    print_hex32(out, read_le32(bytes, 0x20));
    out << " ";
    print_hex32(out, read_le32(bytes, 0x24));
    out << " ";
    print_hex32(out, read_le32(bytes, 0x28));
    out << '\n';
}

void print_strings(std::ostream& out, const char* title, const std::vector<std::string>& strings, std::size_t limit)
{
    out << title << ":\n";
    if (strings.empty()) {
        out << "  none\n";
        return;
    }

    const std::size_t total = std::min(limit, strings.size());
    for (std::size_t i = 0; i < total; ++i) {
        out << "  " << strings[i] << '\n';
    }
    if (strings.size() > total) {
        out << "  ... " << (strings.size() - total) << " more\n";
    }
}
}

bool AppPackageInspector::inspect(const std::string& app_directory, std::ostream& out)
{
    const fs::path root(app_directory);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "App directory not found: " << app_directory << std::endl;
        return false;
    }

    const std::vector<fs::path> mif_files = find_by_extension(root, ".mif");
    const std::vector<fs::path> mod_files = find_by_extension(root, ".mod");
    const std::vector<fs::path> sig_files = find_by_extension(root, ".sig");

    out << "Zeebo app package: " << root.string() << "\n\n";
    out << "Layout:\n";
    out << "  mif files: " << mif_files.size() << '\n';
    out << "  mod files: " << mod_files.size() << '\n';
    out << "  sig files: " << sig_files.size() << "\n\n";

    for (const fs::path& path : mif_files) {
        const std::string bytes = read_file(path);
        out << "MIF: " << path.string() << " (" << bytes.size() << " bytes)\n";
        print_strings(out, "  UTF-16LE strings", utf16le_strings(bytes), 12);
        print_strings(out, "  ASCII strings", ascii_strings(bytes), 12);
        out << '\n';
    }

    for (const fs::path& path : mod_files) {
        const std::string bytes = read_file(path);
        out << "MOD: " << path.string() << " (" << bytes.size() << " bytes)\n";
        if (looks_like_brew_mod(bytes)) {
            print_brew_mod_summary(bytes, out);
        } else {
            out << "  format guess: " << (looks_like_arm_code(bytes) ? "raw ARM little-endian code" : "unknown") << '\n';
        }
        out << "  first word: 0x" << std::hex << std::setw(8) << std::setfill('0') << read_le32(bytes, 0)
            << std::dec << std::setfill(' ') << '\n';
        print_strings(out, "  ASCII strings", ascii_strings(bytes), 24);
        out << '\n';
    }

    for (const fs::path& path : sig_files) {
        const std::string bytes = read_file(path);
        out << "SIG: " << path.string() << " (" << bytes.size() << " bytes)\n";
        print_strings(out, "  ASCII strings", ascii_strings(bytes), 16);
        out << '\n';
    }

    return true;
}
