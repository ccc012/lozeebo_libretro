#include "brew/GgzArchive.h"

#include "util/Compression.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t GGZ_MAGIC_BE = 0x1f8b0808u;

uint32_t read_be32(const std::vector<uint8_t>& data, size_t offset)
{
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

std::string safe_name(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    return name;
}

bool parse_gzip_entry(const std::vector<uint8_t>& data, size_t start, size_t end, size_t& deflate_start, size_t& deflate_size, std::string& name)
{
    if (start + 18 > end || data[start] != 0x1f || data[start + 1] != 0x8b || data[start + 2] != 0x08) {
        return false;
    }
    const uint8_t flags = data[start + 3];
    size_t pos = start + 10;
    if (flags & 0x04) {
        if (pos + 2 > end) {
            return false;
        }
        const uint16_t extra_len = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2 + extra_len;
    }
    if (flags & 0x08) {
        name.clear();
        while (pos < end && data[pos] != 0) {
            name.push_back(static_cast<char>(data[pos++]));
        }
        if (pos >= end) {
            return false;
        }
        ++pos;
    }
    if (flags & 0x10) {
        while (pos < end && data[pos] != 0) {
            ++pos;
        }
        if (pos >= end) {
            return false;
        }
        ++pos;
    }
    if (flags & 0x02) {
        pos += 2;
    }
    if (pos + 8 > end) {
        return false;
    }
    deflate_start = pos;
    deflate_size = end - pos - 8;
    return true;
}

bool read_all(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return !!file;
}

} // namespace

bool GgzArchive::extract_to_cache(const std::filesystem::path& archive_path)
{
    const std::filesystem::path out_dir = archive_path.parent_path() / (archive_path.filename().string() + ".unpacked");
    std::error_code ec;
    if (std::filesystem::exists(out_dir, ec) && std::filesystem::is_directory(out_dir, ec)) {
        auto it = std::filesystem::directory_iterator(out_dir, ec);
        if (!ec && it != std::filesystem::directory_iterator()) {
            printf("GGZ extract: cache hit '%s'\n", out_dir.string().c_str());
            return true;
        }
    }

    std::vector<uint8_t> data;
    if (!read_all(archive_path, data) || data.size() < 18) {
        return false;
    }

    std::vector<uint32_t> offsets;
    std::vector<uint32_t> original_sizes;
    for (size_t pos = 0; pos + 8 <= data.size(); pos += 8) {
        const uint32_t offset = read_be32(data, pos);
        if ((offset & 0xffff0000u) == 0x1f8b0000u) {
            break;
        }
        if (offset >= data.size()) {
            return false;
        }
        offsets.push_back(offset);
        original_sizes.push_back(read_be32(data, pos + 4));
    }
    if (offsets.empty()) {
        return false;
    }

    ec.clear();
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        printf("GGZ extract: failed to create '%s': %s\n", out_dir.string().c_str(), ec.message().c_str());
        return false;
    }

    size_t extracted = 0;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const size_t entry = offsets[i];
        if (entry + 10 >= data.size() || read_be32(data, entry) != GGZ_MAGIC_BE) {
            continue;
        }

        const size_t next_entry = (i + 1 < offsets.size()) ? offsets[i + 1] : data.size();
        size_t deflate_pos = 0;
        size_t deflate_size = 0;
        std::string name;
        if (next_entry > data.size() || !parse_gzip_entry(data, entry, next_entry, deflate_pos, deflate_size, name)) {
            continue;
        }
        if (name.empty()) {
            name = std::to_string(i);
        }

        std::vector<uint8_t> decoded;
        if (!zeemu::compression::inflate_deflate(data.data() + deflate_pos, deflate_size, zeemu::compression::DeflateContainer::Raw, decoded) ||
            decoded.empty() ||
            (i < original_sizes.size() && original_sizes[i] != 0 && static_cast<uint32_t>(decoded.size()) != original_sizes[i])) {
            continue;
        }

        const std::filesystem::path out_path = out_dir / safe_name(name);
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(reinterpret_cast<const char*>(decoded.data()), static_cast<std::streamsize>(decoded.size()));
            if (out) {
                ++extracted;
            }
        }
    }

    printf("GGZ extract: '%s' -> %zu files in '%s'\n",
           archive_path.string().c_str(),
           extracted,
           out_dir.string().c_str());
    return extracted > 0;
}
