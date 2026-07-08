#ifndef ZEEMU_BREW_APPLET_CTL_H_
#define ZEEMU_BREW_APPLET_CTL_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"

class BrewAppletCtl : public BrewService {
public:
    BrewAppletCtl(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    void write_running_list(uint32_t p_list, uint32_t p_count);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    uint32_t refs_ = 1;
};

#endif
