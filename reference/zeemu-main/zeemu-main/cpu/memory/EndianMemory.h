#ifndef ZEEMU_MEMORY_ENDIAN_MEMORY_H__
#define ZEEMU_MEMORY_ENDIAN_MEMORY_H__

#include "cpu/cpu.h"
#include "cpu/memory/DecoratorMemory.h"
#include "cpu/memory/VirtualMemory.h"
#include <cstring>

class ZEEMU_EXPORT EndianMemory : public DecoratorMemory {
public:
    enum Size {
        Word = 4,
        Halfword = 2,
        Byte = 1
    };

    enum Signedness {
        Signed,
        Unsigned
    };

    EndianMemory(Memory *engine, Endianness end);

    ARM_Word   read_value (addr_t addr, Size size = Word, Signedness signedness = Unsigned);
    void       write_value(addr_t addr, ARM_Word value, Size size = Word);
    bool       try_read_fast(addr_t addr, Size size, ARM_Word& value) const;
    bool       try_write_fast(addr_t addr, ARM_Word value, Size size);
    bool       try_read_word_mapped_fast(addr_t addr, ARM_Word& value) const {
        if (endianness_ != LittleEndian || !vm_fast_ || (addr & 0xFFFu) > 0xFFCu) {
            return false;
        }
        const void* host = vm_fast_->get_host_address_fast(addr);
        if (!host) {
            return false;
        }
        std::memcpy(&value, host, sizeof(value));
        return true;
    }
    bool       try_write_word_mapped_fast_untraced(addr_t addr, ARM_Word value) {
        if (endianness_ != LittleEndian || !vm_fast_ || (addr & 0xFFFu) > 0xFFCu) {
            return false;
        }
        void* host = vm_fast_->get_host_address_fast(addr);
        if (!host) {
            return false;
        }
        std::memcpy(host, &value, sizeof(value));
        return true;
    }
    void       write_bytes(addr_t addr, const uint8_t* data, size_t bytes);
    void       fill(addr_t addr, uint8_t value, size_t bytes);

private:
    Endianness endianness_;
    VirtualMemory* vm_fast_ = nullptr;
};

#endif
