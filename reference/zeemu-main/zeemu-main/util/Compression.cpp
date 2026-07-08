#include "util/Compression.h"

#include "third_party/stb_image.h"

#include <cstdlib>
#include <limits>

namespace zeemu::compression {
namespace {

bool gzip_deflate_range(const uint8_t* data, size_t size, size_t& start, size_t& deflate_size) {
    if (!data || size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 0x08) {
        return false;
    }

    const uint8_t flags = data[3];
    size_t pos = 10;
    if (flags & 0x04) {
        if (pos + 2 > size) {
            return false;
        }
        const uint16_t extra_len = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2 + extra_len;
    }
    if (flags & 0x08) {
        while (pos < size && data[pos] != 0) {
            ++pos;
        }
        if (pos >= size) {
            return false;
        }
        ++pos;
    }
    if (flags & 0x10) {
        while (pos < size && data[pos] != 0) {
            ++pos;
        }
        if (pos >= size) {
            return false;
        }
        ++pos;
    }
    if (flags & 0x02) {
        pos += 2;
    }
    if (pos + 8 > size) {
        return false;
    }

    start = pos;
    deflate_size = size - pos - 8;
    return true;
}

} // namespace

bool inflate_deflate(const uint8_t* data, size_t size, DeflateContainer container, std::vector<uint8_t>& out) {
    out.clear();
    if (!data || size == 0 || size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    int decoded_size = 0;
    char* decoded = stbi_zlib_decode_malloc_guesssize_headerflag(
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        16384,
        &decoded_size,
        container == DeflateContainer::Zlib ? 1 : 0);
    if (!decoded || decoded_size <= 0) {
        if (decoded) {
            stbi_image_free(decoded);
        }
        return false;
    }

    out.assign(reinterpret_cast<uint8_t*>(decoded), reinterpret_cast<uint8_t*>(decoded) + decoded_size);
    stbi_image_free(decoded);
    return true;
}

bool inflate_gzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    size_t deflate_start = 0;
    size_t deflate_size = 0;
    if (!gzip_deflate_range(data, size, deflate_start, deflate_size)) {
        out.clear();
        return false;
    }
    return inflate_deflate(data + deflate_start, deflate_size, DeflateContainer::Raw, out);
}

} // namespace zeemu::compression
