#ifndef ZEEMU_BREW_UNZIP_STREAM_H_
#define ZEEMU_BREW_UNZIP_STREAM_H_

#include "brew/BrewShell.h"
#include "cpu/memory/EndianMemory.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class BrewUnzipStream : public BrewService {
public:
    BrewUnzipStream(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::vector<uint8_t> decoded_;
    size_t read_pos_ = 0;
};

#endif
