#include "brew/BrewUnzipStream.h"

#include "brew/BrewFileMgr.h"
#include "brew/BrewMemAStream.h"
#include "cpu/core/CPU.h"
#include "util/Compression.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

constexpr uint32_t GGZ_MAGIC = 0x1f8b0808u;

bool is_gzip_deflate(const std::vector<uint8_t>& data) {
    return data.size() >= 10 && data[0] == 0x1f && data[1] == 0x8b && data[2] == 0x08;
}

uint32_t read_be32(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

} // namespace

BrewUnzipStream::BrewUnzipStream(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewUnzipStream::setup_vtable() {
    vtable_ptr_ = shell_.malloc(6 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        const addr_t hook = shell_.add_hook("IUnzipAStream_" + name, this);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(index * 4), hook);
    };

    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "Readable");
    add_method(3, "Read");
    add_method(4, "Cancel");
    add_method(5, "SetStream");
}

void BrewUnzipStream::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);

    if (name == "IUnzipAStream_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (name == "IUnzipAStream_Release") {
        decoded_.clear();
        read_pos_ = 0;
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IUnzipAStream_SetStream") {
        decoded_.clear();
        read_pos_ = 0;

        std::vector<uint8_t> compressed;
        BrewFile* file = shell_.find_open_file(r1);
        if (file) {
            file->read_remaining_from_current(compressed);
        } else if (BrewMemAStream* mem_stream = shell_.find_mem_astream(r1)) {
            mem_stream->read_remaining_from_current(compressed);
        }
        if (compressed.empty()) {
            printf("  IUnzipAStream_SetStream: source=0x%08x unavailable\n", r1);
            return;
        }

        const uint8_t* compressed_data = compressed.data();
        size_t compressed_size = compressed.size();
        bool gzip_payload = false;
        if (is_gzip_deflate(compressed)) {
            size_t next_entry = compressed.size();
            for (size_t pos = 4; pos + 4 <= compressed.size(); ++pos) {
                if (read_be32(compressed, pos) == GGZ_MAGIC) {
                    next_entry = pos;
                    break;
                }
            }
            compressed_size = next_entry;
            gzip_payload = true;
        }

        std::vector<uint8_t> decoded;
        const bool decoded_ok = gzip_payload
            ? zeemu::compression::inflate_gzip(compressed_data, compressed_size, decoded)
            : zeemu::compression::inflate_deflate(compressed_data, compressed_size, zeemu::compression::DeflateContainer::Raw, decoded);
        if (!decoded_ok || decoded.empty()) {
            printf("  IUnzipAStream_SetStream: decode failed source=0x%08x compressed=%zu payload=%zu\n", r1, compressed.size(), compressed_size);
            return;
        }

        decoded_ = std::move(decoded);
        printf("  IUnzipAStream_SetStream: source=0x%08x compressed=%zu payload=%zu decoded=%zu\n", r1, compressed.size(), compressed_size, decoded_.size());
        return;
    }

    if (name == "IUnzipAStream_Read") {
        const addr_t dest = r1;
        const uint32_t want = r2;
        if (dest == 0 || dest >= 0xFF000000 || want == 0 || read_pos_ >= decoded_.size()) {
            cpu.set_reg(REG_R0, 0);
            return;
        }

        const size_t n = std::min<size_t>(want, decoded_.size() - read_pos_);
        if (dest < 0x10000) {
            brew_store_low_pointer_shadow(dest, decoded_.data() + read_pos_, n);
            printf("  IUnzipAStream_Read low dest shadow ptr=0x%x bytes=%zu\n", dest, n);
        } else {
            memory_.write_bytes(dest, decoded_.data() + read_pos_, n);
        }
        read_pos_ += n;
        if (std::getenv("ZEEMU_TRACE_UNZIP") || n != want) {
            printf("  IUnzipAStream_Read: want=%u -> read=%zu\n", want, n);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(n));
        return;
    }

    if (name == "IUnzipAStream_Readable") {
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IUnzipAStream_Cancel") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
}
