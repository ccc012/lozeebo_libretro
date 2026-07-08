#include <map>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "cpu/memory/EndianMemory.h"
#include "cpu/memory/VirtualMemory.h"

namespace {

struct TraceMemWatch {
    bool enabled = false;
    addr_t start = 0;
    addr_t end = 0;
};

TraceMemWatch get_trace_memwatch()
{
    static bool initialized = false;
    static TraceMemWatch watch;
    if (initialized) {
        return watch;
    }

    initialized = true;
    const char* env = std::getenv("ZEEMU_TRACE_MEMWATCH");
    if (!env || !*env) {
        return watch;
    }

    char* end = nullptr;
    const auto start = static_cast<addr_t>(std::strtoul(env, &end, 0));
    if (end == env || (*end != ':' && *end != '+' && *end != ',')) {
        return watch;
    }

    const char* size_text = end + 1;
    char* size_end = nullptr;
    const auto size = static_cast<addr_t>(std::strtoul(size_text, &size_end, 0));
    if (size_text == size_end || size == 0) {
        return watch;
    }

    watch.enabled = true;
    watch.start = start;
    watch.end = start + size;
    return watch;
}

bool overlaps_trace_memwatch(addr_t addr, size_t bytes)
{
    const auto watch = get_trace_memwatch();
    if (!watch.enabled || bytes == 0) {
        return false;
    }

    const addr_t end = addr + static_cast<addr_t>(bytes);
    return addr < watch.end && end > watch.start;
}

void trace_mem_write(addr_t addr, ARM_Word value, EndianMemory::Size size)
{
    if (!overlaps_trace_memwatch(addr, static_cast<size_t>(size))) {
        return;
    }

    std::printf("[MEMWATCH] write addr=0x%08x size=%u value=0x%08x\n",
                static_cast<unsigned>(addr),
                static_cast<unsigned>(size),
                static_cast<unsigned>(value));
}

void trace_mem_fill(addr_t addr, uint8_t value, size_t bytes)
{
    if (!overlaps_trace_memwatch(addr, bytes)) {
        return;
    }

    std::printf("[MEMWATCH] fill addr=0x%08x size=%zu value=0x%02x\n",
                static_cast<unsigned>(addr),
                bytes,
                static_cast<unsigned>(value));
}

void trace_mem_write_bytes(addr_t addr, size_t bytes)
{
    if (!overlaps_trace_memwatch(addr, bytes)) {
        return;
    }

    std::printf("[MEMWATCH] write_bytes addr=0x%08x size=%zu\n",
                static_cast<unsigned>(addr),
                bytes);
}

bool fast_access_fits_page(addr_t addr, EndianMemory::Size size)
{
    const std::size_t page_offset = addr % VirtualMemory::page_size();
    return page_offset + static_cast<std::size_t>(size) <= VirtualMemory::page_size();
}

}

/**
 * Returns "data", supposed to be in memory endianness, converted to platform
 * endianness (platform endianness = endianness of CPU on which this ARM interpreter
 * runs.
 */
static void endian_swap(std::string& data, Endianness memory_endianness)
{
    if (memory_endianness != runtime_platform_endianness) {
        std::reverse(data.begin(), data.end());
    }
}

EndianMemory::EndianMemory(Memory *engine, Endianness end)
:
DecoratorMemory(engine),
endianness_(end)
{
    this->engine = engine;
    vm_fast_ = dynamic_cast<VirtualMemory*>(engine);
}

ARM_Word EndianMemory::read_value(addr_t addr, EndianMemory::Size size, EndianMemory::Signedness signedness)
{
    // Fast path: direct pointer read through VirtualMemory
    ARM_Word ret = 0;
    if (try_read_fast(addr, size, ret)) {
        if (signedness == Signed) {
            switch (size) {
            case Halfword: if (ret & 0x8000) ret |= 0xFFFF0000; break;
            case Byte:     if (ret & 0x80)   ret |= 0xFFFFFF00; break;
            default: break;
            }
        }
        return ret;
    }

    // Slow path: original std::string based
    std::string data = engine->read(addr, size);
    if (endianness_ == BigEndian)
        std::reverse(data.begin(), data.end());
    ret = 0;
    for (int i = 0; i < size; i++)
        ret |= ((unsigned char)data[i]) << 8*i;
    
    //sign-extend the value, if needed
    if (signedness == Signed) {
        bool perform = true;
        ARM_Word signmask = 0;
        ARM_Word extendbits = 0;
        switch (size) {
        case Word:
            perform = false;
            break;
        case Halfword:
            signmask   = 0x00008000;
            extendbits = 0xffff0000;
            break;
        case Byte:
            signmask   = 0x00000080;
            extendbits = 0xffffff00;
            break;
        default:
            assert(!"Unknown value size");
            perform = false;
            break;
        }
        if (perform) {
            if (ret & signmask) {
                ret |= extendbits;
            }
        }
    }

    return ret;
}

bool EndianMemory::try_read_fast(addr_t addr, EndianMemory::Size size, ARM_Word& value) const
{
    if (!vm_fast_ || !fast_access_fits_page(addr, size)) {
        return false;
    }

    void* host = vm_fast_->get_host_address(addr);
    if (!host) {
        return false;
    }

    if (endianness_ == LittleEndian) {
        switch (size) {
        case Word:     value = *static_cast<uint32_t*>(host); break;
        case Halfword: value = *static_cast<uint16_t*>(host); break;
        case Byte:     value = *static_cast<uint8_t*>(host); break;
        }
    } else {
        const uint8_t* p = static_cast<const uint8_t*>(host);
        switch (size) {
        case Word:     value = static_cast<uint32_t>(p[3]) | (static_cast<uint32_t>(p[2]) << 8) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[0]) << 24); break;
        case Halfword: value = static_cast<uint16_t>(p[1]) | (static_cast<uint16_t>(p[0]) << 8); break;
        case Byte:     value = p[0]; break;
        }
    }
    return true;
}

void EndianMemory::write_value(addr_t addr, ARM_Word value, EndianMemory::Size size)
{
    // Fast path: direct pointer write through VirtualMemory
    if (try_write_fast(addr, value, size)) {
        return;
    }

    // Slow path
    trace_mem_write(addr, value, size);
    std::string data((char *) &value, sizeof(ARM_Word));
    endian_swap(data, endianness_);
    int start = 0;
    if (endianness_ == BigEndian)
        start = sizeof(ARM_Word) - size;
    data = data.substr(start, size);
    engine->write(addr, data);
}

bool EndianMemory::try_write_fast(addr_t addr, ARM_Word value, EndianMemory::Size size)
{
    if (!vm_fast_ || !fast_access_fits_page(addr, size)) {
        return false;
    }

    void* host = vm_fast_->get_host_address(addr);
    if (!host) {
        return false;
    }

    trace_mem_write(addr, value, size);
    if (endianness_ == LittleEndian) {
        switch (size) {
        case Word:     *static_cast<uint32_t*>(host) = static_cast<uint32_t>(value); break;
        case Halfword: *static_cast<uint16_t*>(host) = static_cast<uint16_t>(value); break;
        case Byte:     *static_cast<uint8_t*>(host) = static_cast<uint8_t>(value); break;
        }
    } else {
        uint8_t* p = static_cast<uint8_t*>(host);
        switch (size) {
        case Word:     p[0] = static_cast<uint8_t>(value >> 24); p[1] = static_cast<uint8_t>(value >> 16); p[2] = static_cast<uint8_t>(value >> 8); p[3] = static_cast<uint8_t>(value); break;
        case Halfword: p[0] = static_cast<uint8_t>(value >> 8); p[1] = static_cast<uint8_t>(value); break;
        case Byte:     p[0] = static_cast<uint8_t>(value); break;
        }
    }
    return true;
}

void EndianMemory::write_bytes(addr_t addr, const uint8_t* data, size_t bytes)
{
    trace_mem_write_bytes(addr, bytes);

    if (bytes == 0) {
        return;
    }

    if (vm_fast_) {
        const std::size_t page_size = VirtualMemory::page_size();
        size_t done = 0;
        while (done < bytes) {
            addr_t cur = addr + static_cast<addr_t>(done);
            void* host = vm_fast_->get_host_address(cur);
            if (!host) {
                break;
            }
            const std::size_t page_left = page_size - (cur % page_size);
            const std::size_t chunk = std::min(page_left, bytes - done);
            std::memcpy(host, data + done, chunk);
            done += chunk;
        }
        if (done == bytes) {
            return;
        }
        addr += static_cast<addr_t>(done);
        data += done;
        bytes -= done;
    }

    engine->write(addr, std::string(reinterpret_cast<const char*>(data), bytes));
}

void EndianMemory::fill(addr_t addr, uint8_t value, size_t bytes)
{
    trace_mem_fill(addr, value, bytes);

    if (bytes == 0) {
        return;
    }

    if (vm_fast_) {
        const std::size_t page_size = VirtualMemory::page_size();
        size_t done = 0;
        while (done < bytes) {
            addr_t cur = addr + static_cast<addr_t>(done);
            void* host = vm_fast_->get_host_address(cur);
            if (!host) {
                break;
            }
            const std::size_t page_left = page_size - (cur % page_size);
            const std::size_t chunk = std::min(page_left, bytes - done);
            std::memset(host, value, chunk);
            done += chunk;
        }
        if (done == bytes) {
            return;
        }
        addr += static_cast<addr_t>(done);
        bytes -= done;
    }

    engine->write(addr, std::string(bytes, static_cast<char>(value)));
}
