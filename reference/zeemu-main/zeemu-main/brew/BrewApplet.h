#ifndef ZEEMU_BREW_APPLET_H_
#define ZEEMU_BREW_APPLET_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"

class BrewApplet : public BrewService {
public:
    BrewApplet(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void set_current_class(uint32_t cls_id);

    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    uint32_t refs_ = 1;
};

#endif
