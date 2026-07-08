#ifndef ZEEMU_UTIL_COMPRESSION_H_
#define ZEEMU_UTIL_COMPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zeemu::compression {

enum class DeflateContainer {
    Raw,
    Zlib,
};

bool inflate_deflate(const uint8_t* data, size_t size, DeflateContainer container, std::vector<uint8_t>& out);
bool inflate_gzip(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

} // namespace zeemu::compression

#endif
