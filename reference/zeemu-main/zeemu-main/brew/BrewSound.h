#ifndef ZEEMU_BREW_SOUND_H_
#define ZEEMU_BREW_SOUND_H_

#include "brew/BrewShell.h"
#include "cpu/memory/EndianMemory.h"
#include <string>

class BrewSound : public BrewService {
public:
    BrewSound(BrewShell& shell, EndianMemory& memory);
    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
};

#endif
