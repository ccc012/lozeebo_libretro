#ifndef ZEEMU_BREW_MEM_ASTREAM_H_
#define ZEEMU_BREW_MEM_ASTREAM_H_

#include "brew/BrewShell.h"
#include "cpu/memory/EndianMemory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class BrewMemAStream : public BrewService {
public:
    BrewMemAStream(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;
    bool read_remaining_from_current(std::vector<uint8_t>& out);

private:
    void setup_vtable();
    void set_buffer(addr_t guest_buffer, uint32_t size, uint32_t offset);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::vector<uint8_t> data_;
    size_t read_pos_ = 0;
};

#endif
