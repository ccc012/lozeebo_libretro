#ifndef ZEEMU_BREW_APP_HISTORY_H_
#define ZEEMU_BREW_APP_HISTORY_H_

#include "brew/BrewService.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <string>

class BrewShell;

class BrewAppHistory : public BrewService {
public:
    BrewAppHistory(BrewShell& shell, EndianMemory& memory);
    addr_t get_object_ptr() const { return object_ptr_; }
    void set_current_applet_cls(uint32_t cls) { current_cls_ = cls; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    uint32_t current_cls_ = 0;
};

#endif
