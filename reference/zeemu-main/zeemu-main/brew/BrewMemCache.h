#ifndef ZEEMU_BREW_MEM_CACHE_H_
#define ZEEMU_BREW_MEM_CACHE_H_

#include "brew/BrewService.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <string>

class BrewShell;
class CPU;

class BrewMemCache : public BrewService {
public:
    BrewMemCache(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, CPU& cpu) override;

private:
    void setup_vtable();

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    uint32_t ref_count_ = 1;
};

#endif
