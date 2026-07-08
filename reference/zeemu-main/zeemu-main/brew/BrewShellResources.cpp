#include "brew/BrewShellResources.h"
#include "vfs/VirtualFileSystem.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>

static std::string resource_lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

static BarResource* get_cached_bar_resource(BrewShell& shell, const std::string& res_file) {
    static std::unordered_map<std::string, BarResource> cache;

    const std::string key = shell.get_current_directory() + '\0' + res_file;
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (std::getenv("ZEEMU_TRACE_RESOURCES")) {
            printf("  [resources] BAR cache hit '%s' cd='%s' entries=%zu\n",
                   res_file.c_str(),
                   shell.get_current_directory().c_str(),
                   it->second.entries.size());
        }
        return &it->second;
    }

    std::string bar_data;
    if (!shell.get_vfs().read_file(res_file, bar_data, shell.get_current_directory())) {
        if (std::getenv("ZEEMU_TRACE_RESOURCES")) {
            printf("  [resources] read failed '%s' cd='%s'\n",
                   res_file.c_str(),
                   shell.get_current_directory().c_str());
        }
        return nullptr;
    }

    BarResource bar;
    const bool parsed = parse_bar_resource_file(bar_data, bar);
    if (std::getenv("ZEEMU_TRACE_RESOURCES")) {
        printf("  [resources] read '%s' cd='%s' bytes=%zu parsed=%d entries=%zu\n",
               res_file.c_str(),
               shell.get_current_directory().c_str(),
               bar_data.size(),
               parsed ? 1 : 0,
               bar.entries.size());
    }
    if (!parsed) {
        return nullptr;
    }

    auto inserted = cache.emplace(key, std::move(bar));
    return &inserted.first->second;
}

static uint16_t read_u16_le(const std::string& data, size_t off) {
    if (off + 1 >= data.size()) {
        return 0;
    }
    return static_cast<uint16_t>((uint8_t) data[off] | ((uint16_t) (uint8_t) data[off + 1] << 8));
}

static uint32_t read_u32_le(const std::string& data, size_t off) {
    if (off + 3 >= data.size()) {
        return 0;
    }
    return (uint32_t)(static_cast<uint8_t>(data[off]) |
                      (static_cast<uint32_t>((uint8_t) data[off + 1]) << 8) |
                      (static_cast<uint32_t>((uint8_t) data[off + 2]) << 16) |
                      (static_cast<uint32_t>((uint8_t) data[off + 3]) << 24));
}

static bool looks_like_utf16le(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4 || (bytes.size() & 1u) != 0) {
        return false;
    }
    size_t zero_hi = 0;
    size_t pairs = bytes.size() / 2;
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        if (bytes[i + 1] == 0) {
            ++zero_hi;
        }
    }
    return zero_hi * 2 >= pairs;
}

static bool looks_like_single_byte_string_payload(const std::vector<uint8_t>& bytes, size_t offset) {
    bool saw_text = false;
    for (size_t i = offset; i < bytes.size(); ++i) {
        const uint8_t ch = bytes[i];
        if (ch == 0) {
            break;
        }
        if (ch == '\t' || ch == '\n' || ch == '\r' || ch >= 0x20) {
            saw_text = true;
            continue;
        }
        return false;
    }
    return saw_text;
}

static void strip_brew_string_encoding_marker(std::vector<uint8_t>& bytes) {
    if (bytes.size() > 1 &&
        bytes[0] == 0x03 &&
        looks_like_single_byte_string_payload(bytes, 1)) {
        // BREW string BAR entries can prefix native single-byte text with an
        // encoding marker. It is metadata, not part of the returned AECHAR
        // string. Disney All Star Cards' menu strings use this form.
        bytes.erase(bytes.begin());
    }
}

static bool starts_with(const std::vector<uint8_t>& bytes, const char* magic, size_t count) {
    if (bytes.size() < count) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] != static_cast<uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

static bool looks_like_mp3_frame(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0;
}

static bool looks_like_adts_aac(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xf6) == 0xf0;
}

static bool looks_like_iso_media(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 12 || !starts_with(std::vector<uint8_t>(bytes.begin() + 4, bytes.begin() + 8), "ftyp", 4)) {
        return false;
    }
    auto has_brand = [&](const char* brand, size_t count) {
        for (size_t off = 8; off + count <= std::min<size_t>(bytes.size(), 64); ++off) {
            bool match = true;
            for (size_t i = 0; i < count; ++i) {
                if (bytes[off + i] != static_cast<uint8_t>(brand[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    };
    return has_brand("isom", 4) || has_brand("iso2", 4) || has_brand("mp41", 4) ||
           has_brand("mp42", 4) || has_brand("3gp", 3) || has_brand("3g2", 3) ||
           has_brand("M4V ", 4) || has_brand("M4A ", 4);
}

static bool looks_like_tga(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 18) {
        return false;
    }
    const uint8_t id_len = bytes[0];
    const uint8_t color_map_type = bytes[1];
    const uint8_t image_type = bytes[2];
    if (color_map_type > 1) {
        return false;
    }
    if (image_type != 1 && image_type != 2 && image_type != 3 &&
        image_type != 9 && image_type != 10 && image_type != 11) {
        return false;
    }
    if (color_map_type == 0 && (image_type == 1 || image_type == 9)) {
        return false;
    }

    const uint16_t color_map_len = static_cast<uint16_t>(bytes[5] | (bytes[6] << 8));
    const uint8_t color_map_depth = bytes[7];
    if (color_map_type == 1) {
        if (color_map_len == 0 ||
            (color_map_depth != 15 && color_map_depth != 16 && color_map_depth != 24 && color_map_depth != 32)) {
            return false;
        }
    } else if (color_map_len != 0) {
        return false;
    }

    const uint16_t width = static_cast<uint16_t>(bytes[12] | (bytes[13] << 8));
    const uint16_t height = static_cast<uint16_t>(bytes[14] | (bytes[15] << 8));
    const uint8_t pixel_depth = bytes[16];
    const uint8_t descriptor = bytes[17];
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return false;
    }
    if (pixel_depth != 8 && pixel_depth != 15 && pixel_depth != 16 && pixel_depth != 24 && pixel_depth != 32) {
        return false;
    }
    if ((descriptor & 0xc0) != 0) {
        return false;
    }

    size_t color_map_bytes = 0;
    if (color_map_type == 1) {
        color_map_bytes = static_cast<size_t>(color_map_len) * ((color_map_depth + 7u) / 8u);
    }
    const size_t header_end = 18u + id_len + color_map_bytes;
    if (header_end > bytes.size()) {
        return false;
    }
    if (image_type == 1 || image_type == 2 || image_type == 3) {
        const size_t pixel_bytes = static_cast<size_t>((pixel_depth + 7u) / 8u) * width * height;
        const size_t expected = header_end + pixel_bytes;
        return expected <= bytes.size() && bytes.size() - expected <= 4096;
    }
    return header_end < bytes.size();
}

static bool looks_like_svg(const std::vector<uint8_t>& bytes) {
    const size_t count = std::min<size_t>(bytes.size(), 4096);
    std::string head;
    head.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        head.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(bytes[i]))));
    }
    const size_t first = head.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return false;
    }
    const std::string trimmed = head.substr(first);
    return trimmed.rfind("<svg", 0) == 0 ||
           (trimmed.rfind("<?xml", 0) == 0 && trimmed.find("<svg") != std::string::npos);
}

static bool looks_like_imelody(const std::vector<uint8_t>& bytes) {
    const size_t count = std::min<size_t>(bytes.size(), 256);
    std::string head;
    head.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        head.push_back(static_cast<char>(std::toupper(bytes[i])));
    }
    return head.find("BEGIN:IMELODY") != std::string::npos;
}

static std::string mime_from_bytes(const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 &&
        bytes[1] == 'P' &&
        bytes[2] == 'N' &&
        bytes[3] == 'G' &&
        bytes[4] == 0x0d &&
        bytes[5] == 0x0a &&
        bytes[6] == 0x1a &&
        bytes[7] == 0x0a) {
        return "image/png";
    }
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        return "image/bmp";
    }
    if (bytes.size() >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) {
        return "image/jpeg";
    }
    if (starts_with(bytes, "GIF87a", 6) || starts_with(bytes, "GIF89a", 6)) {
        return "image/gif";
    }
    if (looks_like_tga(bytes)) {
        return "image/x-tga";
    }
    if (bytes.size() >= 12 &&
        bytes[0] == 'R' &&
        bytes[1] == 'I' &&
        bytes[2] == 'F' &&
        bytes[3] == 'F' &&
        bytes[8] == 'W' &&
        bytes[9] == 'A' &&
        bytes[10] == 'V' &&
        bytes[11] == 'E') {
        return "audio/wav";
    }
    if (bytes.size() >= 12 &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'Q' && bytes[9] == 'L' && bytes[10] == 'C' && bytes[11] == 'M') {
        return "audio/qcp";
    }
    if (bytes.size() >= 12 &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'D' && bytes[9] == 'L' && bytes[10] == 'S' && bytes[11] == ' ') {
        return "audio/dls";
    }
    if (starts_with(bytes, "MThd", 4)) {
        return "audio/mid";
    }
    if (starts_with(bytes, "#!AMR\n", 6) || starts_with(bytes, "#!AMR-WB\n", 9)) {
        return "audio/amr";
    }
    if (looks_like_adts_aac(bytes)) {
        return "audio/aac";
    }
    if (starts_with(bytes, "ID3", 3) || looks_like_mp3_frame(bytes)) {
        return "audio/mp3";
    }
    if (starts_with(bytes, "MMMD", 4)) {
        return "audio/mmf";
    }
    if (starts_with(bytes, "XMF_", 4)) {
        return "audio/xmf";
    }
    if (starts_with(bytes, "cmid", 4)) {
        return "video/pmd";
    }
    if (looks_like_imelody(bytes)) {
        return "audio/imy";
    }
    if (looks_like_iso_media(bytes)) {
        return "video/mp4";
    }
    if (bytes.size() >= 16 &&
        bytes[0] == 0x30 && bytes[1] == 0x26 && bytes[2] == 0xb2 && bytes[3] == 0x75 &&
        bytes[4] == 0x8e && bytes[5] == 0x66 && bytes[6] == 0xcf && bytes[7] == 0x11 &&
        bytes[8] == 0xa6 && bytes[9] == 0xd9 && bytes[10] == 0x00 && bytes[11] == 0xaa &&
        bytes[12] == 0x00 && bytes[13] == 0x62 && bytes[14] == 0xce && bytes[15] == 0x6c) {
        return "video/wmv";
    }
    if (looks_like_svg(bytes)) {
        return "image/svg+xml";
    }
    if (looks_like_utf16le(bytes)) {
        return "text/plain";
    }
    return "application/octet-stream";
}

bool parse_bar_resource_file(const std::string& data, BarResource& out) {
    if (data.size() < 0x3c) {
        return false;
    }

    const uint16_t range_count = read_u16_le(data, 0x06);
    const uint32_t range_table = read_u32_le(data, 0x08);
    const uint32_t offset_table = read_u32_le(data, 0x10);
    const uint32_t first_blob = read_u32_le(data, 0x18);
    if (range_count > 0 &&
        range_table >= 0x20 &&
        range_table + static_cast<uint32_t>(range_count) * 8u <= data.size() &&
        offset_table > range_table &&
        first_blob > offset_table &&
        first_blob <= data.size()) {
        std::vector<uint32_t> blob_offsets;
        for (uint32_t off = offset_table; off + 4 <= first_blob; off += 4) {
            const uint32_t blob_off = read_u32_le(data, off);
            if (blob_off >= first_blob && blob_off <= data.size()) {
                blob_offsets.push_back(blob_off);
            }
        }
        if (blob_offsets.empty() || blob_offsets.back() != data.size()) {
            blob_offsets.push_back(static_cast<uint32_t>(data.size()));
        }
        std::vector<bool> covered_blobs(blob_offsets.size(), false);

        for (uint16_t range = 0; range < range_count; ++range) {
            const uint32_t desc = range_table + static_cast<uint32_t>(range) * 8u;
            const uint16_t type = read_u16_le(data, desc);
            const uint16_t base_id = read_u16_le(data, desc + 2);
            const uint16_t count = read_u16_le(data, desc + 4);
            const uint16_t first_index = read_u16_le(data, desc + 6);
            if (count == 0 || first_index >= blob_offsets.size()) {
                continue;
            }

            if (out.entries.empty()) {
                out.base_id = base_id;
                out.type = type;
            }

            for (uint16_t i = 0; i < count; ++i) {
                const size_t index = static_cast<size_t>(first_index) + i;
                if (index + 1 >= blob_offsets.size()) {
                    break;
                }

                const uint32_t start = blob_offsets[index];
                const uint32_t end = blob_offsets[index + 1];
                if (start + 2 >= end || end > data.size()) {
                    continue;
                }

                const uint8_t data_offset = static_cast<uint8_t>(data[start]);
                if (data[start + 1] != 0 || data_offset < 3 || start + data_offset > end) {
                    continue;
                }

                const size_t mime_start = start + 2;
                size_t mime_end = start + data_offset;
                const size_t nul = data.find('\0', mime_start);
                if (nul != std::string::npos && nul < mime_end) {
                    mime_end = nul;
                }

                BarResource::Entry entry;
                entry.id = static_cast<uint16_t>(base_id + i);
                entry.mime = data.substr(mime_start, mime_end - mime_start);
                entry.raw.assign(
                    reinterpret_cast<const uint8_t*>(data.data() + start),
                    reinterpret_cast<const uint8_t*>(data.data() + end));
                entry.data.assign(
                    reinterpret_cast<const uint8_t*>(data.data() + start + data_offset),
                    reinterpret_cast<const uint8_t*>(data.data() + end));
                out.entries.push_back(std::move(entry));
                covered_blobs[index] = true;
            }
        }

        if (!out.entries.empty()) {
            auto has_entry_id = [&out](uint16_t id) {
                return std::any_of(out.entries.begin(), out.entries.end(), [id](const BarResource::Entry& entry) {
                    return entry.id == id;
                });
            };

            for (uint16_t range = range_count; range-- > 0;) {
                const uint32_t desc = range_table + static_cast<uint32_t>(range) * 8u;
                const uint16_t base_id = read_u16_le(data, desc + 2);
                const uint16_t count = read_u16_le(data, desc + 4);
                const uint16_t first_index = read_u16_le(data, desc + 6);
                if (count != 0 || first_index >= blob_offsets.size()) {
                    continue;
                }

                uint16_t end_index = static_cast<uint16_t>(blob_offsets.size() - 1);
                for (uint16_t next = static_cast<uint16_t>(range + 1); next < range_count; ++next) {
                    const uint32_t next_desc = range_table + static_cast<uint32_t>(next) * 8u;
                    const uint16_t next_first = read_u16_le(data, next_desc + 6);
                    if (next_first > first_index && next_first < blob_offsets.size()) {
                        end_index = next_first;
                        break;
                    }
                }

                for (uint16_t i = 0; i < end_index - first_index; ++i) {
                    const uint16_t id = static_cast<uint16_t>(base_id + i);
                    if (has_entry_id(id)) {
                        continue;
                    }
                    bool collides_with_counted_range = false;
                    for (uint16_t other = 0; other < range_count; ++other) {
                        const uint32_t other_desc = range_table + static_cast<uint32_t>(other) * 8u;
                        const uint16_t other_base = read_u16_le(data, other_desc + 2);
                        const uint16_t other_count = read_u16_le(data, other_desc + 4);
                        if (other_count != 0 &&
                            id >= other_base &&
                            id <= static_cast<uint16_t>(other_base + other_count)) {
                            collides_with_counted_range = true;
                            break;
                        }
                    }
                    if (collides_with_counted_range) {
                        continue;
                    }

                    const size_t index = static_cast<size_t>(first_index) + i;
                    if (index + 1 >= blob_offsets.size()) {
                        break;
                    }

                    const uint32_t start = blob_offsets[index];
                    const uint32_t end = blob_offsets[index + 1];
                    if (start + 2 >= end || end > data.size()) {
                        continue;
                    }

                    const uint8_t data_offset = static_cast<uint8_t>(data[start]);
                    if (data[start + 1] != 0 || data_offset < 3 || start + data_offset > end) {
                        continue;
                    }

                    const size_t mime_start = start + 2;
                    size_t mime_end = start + data_offset;
                    const size_t nul = data.find('\0', mime_start);
                    if (nul != std::string::npos && nul < mime_end) {
                        mime_end = nul;
                    }

                    BarResource::Entry entry;
                    entry.id = id;
                    entry.mime = data.substr(mime_start, mime_end - mime_start);
                    entry.raw.assign(
                        reinterpret_cast<const uint8_t*>(data.data() + start),
                        reinterpret_cast<const uint8_t*>(data.data() + end));
                    entry.data.assign(
                        reinterpret_cast<const uint8_t*>(data.data() + start + data_offset),
                        reinterpret_cast<const uint8_t*>(data.data() + end));
                    out.entries.push_back(std::move(entry));
                }
            }

            for (size_t index = 0; index + 1 < blob_offsets.size(); ++index) {
                if (covered_blobs[index]) {
                    continue;
                }

                const uint32_t start = blob_offsets[index];
                const uint32_t end = blob_offsets[index + 1];
                if (start + 2 >= end || end > data.size()) {
                    continue;
                }

                const uint8_t data_offset = static_cast<uint8_t>(data[start]);
                if (data[start + 1] != 0 || data_offset < 3 || start + data_offset > end) {
                    continue;
                }

                const size_t mime_start = start + 2;
                size_t mime_end = start + data_offset;
                const size_t nul = data.find('\0', mime_start);
                if (nul != std::string::npos && nul < mime_end) {
                    mime_end = nul;
                }
                const std::string mime = data.substr(mime_start, mime_end - mime_start);
                if (mime != "image/png" && mime != "image/bmp" && mime != "image/jpeg" && mime != "image/x-tga") {
                    continue;
                }

                bool have_prev = false;
                bool have_next = false;
                uint16_t inferred_id = 0;
                uint16_t next_base = 0xffff;
                for (uint16_t range = 0; range < range_count; ++range) {
                    const uint32_t desc = range_table + static_cast<uint32_t>(range) * 8u;
                    const uint16_t base_id = read_u16_le(data, desc + 2);
                    const uint16_t count = read_u16_le(data, desc + 4);
                    const uint16_t first_index = read_u16_le(data, desc + 6);
                    if (count == 0) {
                        continue;
                    }
                    const size_t range_first = first_index;
                    const size_t range_end = range_first + count;
                    if (range_end <= index) {
                        inferred_id = static_cast<uint16_t>(base_id + (index - range_first));
                        have_prev = true;
                    } else if (range_first > index) {
                        next_base = base_id;
                        have_next = true;
                        break;
                    }
                }

                if (!have_prev || (have_next && inferred_id >= next_base)) {
                    continue;
                }

                BarResource::Entry entry;
                entry.id = inferred_id;
                entry.mime = mime;
                entry.raw.assign(
                    reinterpret_cast<const uint8_t*>(data.data() + start),
                    reinterpret_cast<const uint8_t*>(data.data() + end));
                entry.data.assign(
                    reinterpret_cast<const uint8_t*>(data.data() + start + data_offset),
                    reinterpret_cast<const uint8_t*>(data.data() + end));
                out.entries.push_back(std::move(entry));
            }

            for (uint16_t range = 0; range < range_count; ++range) {
                const uint32_t desc = range_table + static_cast<uint32_t>(range) * 8u;
                const uint16_t type = read_u16_le(data, desc);
                const uint16_t base_id = read_u16_le(data, desc + 2);
                const uint16_t count = read_u16_le(data, desc + 4);
                const uint16_t first_index = read_u16_le(data, desc + 6);
                if (count == 0 || first_index >= blob_offsets.size()) {
                    continue;
                }

                // Some Zeebo BARs, including Alien Breaker Deluxe's data.bar,
                // use count as the last local entry index for raw image runs.
                // Keep the normal SDK-shaped count path above, but include this
                // final raw blob when the table leaves it otherwise unreachable.
                const uint16_t inclusive_count = static_cast<uint16_t>(count + 1u);
                for (uint16_t i = 0; i < inclusive_count; ++i) {
                    const uint16_t id = static_cast<uint16_t>(base_id + i);
                    if (has_entry_id(id)) {
                        continue;
                    }

                    const size_t index = static_cast<size_t>(first_index) + i;
                    if (index + 1 >= blob_offsets.size()) {
                        break;
                    }

                    const uint32_t start = blob_offsets[index];
                    const uint32_t end = blob_offsets[index + 1];
                    if (start >= end || end > data.size()) {
                        continue;
                    }

                    BarResource::Entry entry;
                    entry.id = id;
                    entry.raw.assign(
                        reinterpret_cast<const uint8_t*>(data.data() + start),
                        reinterpret_cast<const uint8_t*>(data.data() + end));
                    entry.data = entry.raw;
                    entry.mime = (type == 1 && looks_like_utf16le(entry.data))
                        ? "text/plain"
                        : mime_from_bytes(entry.data);
                    out.entries.push_back(std::move(entry));
                }
            }
            return true;
        }

        auto fallback_has_entry_id = [&out](uint16_t id) {
            return std::any_of(out.entries.begin(), out.entries.end(), [id](const BarResource::Entry& entry) {
                return entry.id == id;
            });
        };

        for (uint16_t range = 0; range < range_count; ++range) {
            const uint32_t desc = range_table + static_cast<uint32_t>(range) * 8u;
            const uint16_t type = read_u16_le(data, desc);
            const uint16_t base_id = read_u16_le(data, desc + 2);
            const uint16_t first_index = read_u16_le(data, desc + 6);
            const uint16_t table_count = read_u16_le(data, desc + 4);
            uint16_t count = table_count;
            if (first_index >= blob_offsets.size()) {
                continue;
            }
            uint16_t end_index = static_cast<uint16_t>(blob_offsets.size() - 1);
            if (range + 1 < range_count) {
                const uint32_t next_desc = range_table + static_cast<uint32_t>(range + 1) * 8u;
                const uint16_t next_first = read_u16_le(data, next_desc + 6);
                if (next_first > first_index && next_first < blob_offsets.size()) {
                    end_index = next_first;
                }
            }
            const uint16_t inferred_count = static_cast<uint16_t>(end_index - first_index);
            if (count == 0 || count < inferred_count) {
                count = inferred_count;
            }
            if (table_count != 0) {
                // ABD's raw BAR ranges store the last local entry index rather
                // than a conventional item count; include that final payload.
                ++count;
            }

            for (uint16_t i = 0; i < count; ++i) {
                const uint16_t id = static_cast<uint16_t>(base_id + i);
                if (fallback_has_entry_id(id)) {
                    continue;
                }
                if (table_count == 0) {
                    bool collides_with_counted_range = false;
                    for (uint16_t other = 0; other < range_count; ++other) {
                        const uint32_t other_desc = range_table + static_cast<uint32_t>(other) * 8u;
                        const uint16_t other_base = read_u16_le(data, other_desc + 2);
                        const uint16_t other_count = read_u16_le(data, other_desc + 4);
                        if (other_count != 0 &&
                            id >= other_base &&
                            id <= static_cast<uint16_t>(other_base + other_count)) {
                            collides_with_counted_range = true;
                            break;
                        }
                    }
                    if (collides_with_counted_range) {
                        continue;
                    }
                }

                const size_t index = static_cast<size_t>(first_index) + i;
                if (index + 1 >= blob_offsets.size()) {
                    break;
                }

                const uint32_t start = blob_offsets[index];
                const uint32_t end = blob_offsets[index + 1];
                if (start >= end || end > data.size()) {
                    continue;
                }

                BarResource::Entry entry;
                entry.id = id;
                if (type == 1 &&
                    end - start >= 2 &&
                    ((static_cast<uint8_t>(data[start]) == 0xff && static_cast<uint8_t>(data[start + 1]) == 0xfe) ||
                     (static_cast<uint8_t>(data[start]) == 0xfe && static_cast<uint8_t>(data[start + 1]) == 0xff))) {
                    entry.mime = "text/plain";
                    entry.raw.assign(
                        reinterpret_cast<const uint8_t*>(data.data() + start),
                        reinterpret_cast<const uint8_t*>(data.data() + end));
                    entry.data.assign(
                        reinterpret_cast<const uint8_t*>(data.data() + start),
                        reinterpret_cast<const uint8_t*>(data.data() + end));
                    out.entries.push_back(std::move(entry));
                } else {
                    bool parsed_mime_blob = false;
                    if (start + 2 < end) {
                        const uint8_t data_offset = static_cast<uint8_t>(data[start]);
                        if (data[start + 1] == 0 && data_offset >= 3 && start + data_offset <= end) {
                            parsed_mime_blob = true;
                            const size_t mime_start = start + 2;
                            size_t mime_end = start + data_offset;
                            const size_t nul = data.find('\0', mime_start);
                            if (nul != std::string::npos && nul < mime_end) {
                                mime_end = nul;
                            }
                            entry.mime = data.substr(mime_start, mime_end - mime_start);
                            entry.raw.assign(
                                reinterpret_cast<const uint8_t*>(data.data() + start),
                                reinterpret_cast<const uint8_t*>(data.data() + end));
                            entry.data.assign(
                                reinterpret_cast<const uint8_t*>(data.data() + start + data_offset),
                                reinterpret_cast<const uint8_t*>(data.data() + end));
                            out.entries.push_back(std::move(entry));
                        }
                    }
                    if (!parsed_mime_blob) {
                        entry.raw.assign(
                            reinterpret_cast<const uint8_t*>(data.data() + start),
                            reinterpret_cast<const uint8_t*>(data.data() + end));
                        entry.data = entry.raw;
                        entry.mime = mime_from_bytes(entry.data);
                        out.entries.push_back(std::move(entry));
                    }
                }
            }
        }

        if (!out.entries.empty()) {
            return true;
        }
    }

    const uint32_t class_word = read_u32_le(data, 0x20);
    out.base_id = static_cast<uint16_t>(class_word >> 16);
    out.type = static_cast<uint16_t>(class_word & 0xffffu);

    std::vector<uint32_t> offsets;
    offsets.reserve(8);
    uint32_t prev = 0;
    for (size_t off = 0x28; off + 4 <= data.size(); off += 4) {
        const uint32_t cur = read_u32_le(data, off);
        if (cur == 0 || cur > data.size() || cur <= prev) {
            break;
        }
        offsets.push_back(cur);
        prev = cur;
        if (cur == data.size()) {
            break;
        }
    }

    if (offsets.size() < 2) {
        return false;
    }

    if (offsets.back() != data.size()) {
        offsets.push_back(static_cast<uint32_t>(data.size()));
    }

    for (size_t i = 0; i + 1 < offsets.size(); ++i) {
        const uint32_t start = offsets[i];
        const uint32_t end = offsets[i + 1];
        if (start + 2 >= end || end > data.size()) {
            continue;
        }

        const uint16_t data_offset = read_u16_le(data, start);
        if (data_offset < 2 || start + data_offset > end) {
            continue;
        }

        const size_t mime_start = start + 2;
        size_t mime_end = start + data_offset;
        const size_t nul = data.find('\0', mime_start);
        if (nul != std::string::npos && nul < mime_end) {
            mime_end = nul;
        }

        BarResource::Entry entry;
        entry.id = static_cast<uint16_t>(out.base_id + i);
        entry.mime = data.substr(mime_start, mime_end - mime_start);
        entry.raw.assign(
            reinterpret_cast<const uint8_t*>(data.data() + start),
            reinterpret_cast<const uint8_t*>(data.data() + end));
        entry.data.assign(
            reinterpret_cast<const uint8_t*>(data.data() + start + data_offset),
            reinterpret_cast<const uint8_t*>(data.data() + end));
        out.entries.push_back(std::move(entry));
    }

    return !out.entries.empty();
}

const BarResource::Entry* find_bar_entry(const BarResource& bar, uint16_t res_id) {
    if (bar.entries.empty()) {
        return nullptr;
    }
    if (res_id == 0) {
        return &bar.entries.front();
    }
    for (const auto& entry : bar.entries) {
        if (entry.id == res_id) {
            return &entry;
        }
    }
    return nullptr;
}

const BarResource::Entry* find_bar_image_entry(const BarResource& bar, uint16_t res_id) {
    if (bar.entries.empty()) {
        return nullptr;
    }

    auto is_image_mime = [](const std::string& mime) {
        const std::string lower = resource_lower_ascii(mime);
        return lower == "image/bmp" ||
               lower == "image/png" ||
               lower == "image/gif" ||
               lower == "image/jpeg" ||
               lower == "image/x-tga" ||
               lower == "image/tga";
    };

    if (res_id == 0) {
        for (const auto& entry : bar.entries) {
            if (is_image_mime(entry.mime)) {
                return &entry;
            }
        }
        return nullptr;
    }

    for (const auto& entry : bar.entries) {
        if (entry.id == res_id && is_image_mime(entry.mime)) {
            return &entry;
        }
    }
    return nullptr;
}

std::string mime_from_path(const std::string& path) {
    const std::string lower = resource_lower_ascii(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".png") return "image/png";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bmp") return "image/bmp";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".gif") return "image/gif";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".jpg") return "image/jpeg";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".jpeg") return "image/jpeg";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tga") return "image/x-tga";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mid") return "audio/mid";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".midi") return "audio/mid";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mp3") return "audio/mp3";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".wav") return "audio/wav";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".qcp") return "audio/qcp";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".qcf") return "audio/qcf";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".pmd") return "video/pmd";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mp4") return "video/mp4";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".3gp") return "video/mp4";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".3g2") return "video/mp4";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".m4a") return "video/mp4";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mmf") return "audio/mmf";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".spf") return "audio/spf";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".aac") return "audio/aac";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".imy") return "audio/imy";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".amr") return "audio/amr";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".wma") return "audio/wma";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".hvs") return "audio/hvs";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".saf") return "audio/saf";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".xmf") return "audio/xmf";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".mxmf") return "audio/xmf";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".dls") return "audio/dls";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".svg") return "image/svg+xml";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".svgz") return "image/svg+xml";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".wmv") return "video/wmv";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt") return "text/plain";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".dat") return "application/octet-stream";
    return "application/octet-stream";
}

bool load_resource_payload(
    BrewShell& shell,
    const std::string& res_file,
    uint16_t res_id,
    std::vector<uint8_t>& out_data,
    std::string& out_mime,
    bool* out_from_bar,
    std::vector<uint8_t>* out_raw)
{
    if (out_from_bar) {
        *out_from_bar = false;
    }

    const std::string lower = resource_lower_ascii(res_file);
    const bool is_bar = (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bar");
    if (is_bar) {
        if (BarResource* bar = get_cached_bar_resource(shell, res_file)) {
            const BarResource::Entry* entry = find_bar_entry(*bar, res_id);
            if (entry) {
                out_data = entry->data;
                if (out_raw) {
                    *out_raw = entry->raw.empty() ? entry->data : entry->raw;
                }
                out_mime = entry->mime.empty() ? mime_from_path(res_file) : entry->mime;
                if (out_from_bar) {
                    *out_from_bar = true;
                }
                return true;
            }
            if (std::getenv("ZEEMU_TRACE_RESOURCES")) {
                printf("  [resources] BAR id=0x%04x not found\n", res_id);
            }
        }
    }

    std::string file_data;
    if (!is_bar && shell.get_vfs().read_file(res_file, file_data, shell.get_current_directory())) {
        out_data.assign(file_data.begin(), file_data.end());
        if (out_raw) {
            *out_raw = out_data;
        }
        out_mime = mime_from_path(res_file);
        return true;
    }

    return false;
}

std::vector<uint8_t> make_res_blob(const std::vector<uint8_t>& payload, const std::string& mime) {
    const std::string safe_mime = mime.empty() ? std::string("application/octet-stream") : mime;
    const size_t mime_len = std::min<size_t>(safe_mime.size(), 249);
    const auto header_size = static_cast<uint32_t>(2 + mime_len + 1);

    std::vector<uint8_t> blob;
    blob.resize(header_size + payload.size());
    blob[0] = static_cast<uint8_t>(header_size);
    blob[1] = 0;
    std::memcpy(blob.data() + 2, safe_mime.data(), mime_len);
    blob[2 + mime_len] = 0;
    if (!payload.empty()) {
        std::memcpy(blob.data() + header_size, payload.data(), payload.size());
    }
    return blob;
}

bool load_string_payload(
    BrewShell& shell,
    const std::string& res_file,
    uint16_t res_id,
    std::u16string& out_text)
{
    std::vector<uint8_t> data;
    std::string mime;
    if (!load_resource_payload(shell, res_file, res_id, data, mime, nullptr) || data.empty()) {
        return false;
    }

    strip_brew_string_encoding_marker(data);

    bool force_utf16le = false;
    bool force_utf16be = false;
    if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        force_utf16le = true;
        data.erase(data.begin(), data.begin() + 2);
    } else if (data.size() >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        force_utf16be = true;
        data.erase(data.begin(), data.begin() + 2);
    }

    if (force_utf16be) {
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            uint16_t ch = (static_cast<uint16_t>(data[i]) << 8) | static_cast<uint16_t>(data[i + 1]);
            if (ch == 0) {
                break;
            }
            out_text.push_back(static_cast<char16_t>(ch));
        }
        return !out_text.empty();
    }

    if (force_utf16le || looks_like_utf16le(data)) {
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            uint16_t ch = static_cast<uint16_t>(data[i]) | (static_cast<uint16_t>(data[i + 1]) << 8);
            if (ch == 0) {
                break;
            }
            out_text.push_back(static_cast<char16_t>(ch));
        }
        return !out_text.empty();
    }

    if (!looks_like_single_byte_string_payload(data, 0)) {
        return false;
    }

    for (uint8_t ch : data) {
        if (ch == 0) {
            break;
        }
        out_text.push_back(ch);
    }
    return !out_text.empty();
}
