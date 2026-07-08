#include "brew/BrewApplet.h"
#include "brew/BrewDisplay.h"
#include "cpu/core/CPU.h"
#include <cstdio>

namespace {
static constexpr uint32_t kEvtAppStart = 0x00000001;
static constexpr uint32_t kEvtAppStop = 0x00000002;
}

BrewApplet::BrewApplet(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewApplet::setup_vtable() {
    vtable_ptr_ = shell_.malloc(3 * 4);
    object_ptr_ = shell_.malloc(0x100);

    memory_.write_value(object_ptr_ + 0x00, vtable_ptr_);
    memory_.write_value(object_ptr_ + 0x04, (uint32_t)0);
    memory_.write_value(object_ptr_ + 0x08, refs_);
    // AEEApplet::m_pIShell is an IShell* object pointer. Some generated
    // applet constructors call through this field directly with the usual
    // COM/BREW vtable shape, so do not store Zeemu's bootstrap helper pointer.
    memory_.write_value(object_ptr_ + 0x0c, shell_.get_object_ptr());
    memory_.write_value(object_ptr_ + 0x10, shell_.get_dummy_module_ptr());
    memory_.write_value(object_ptr_ + 0x14, shell_.get_display() ? shell_.get_display()->get_object_ptr() : 0);
    memory_.write_value(object_ptr_ + 0x18, (uint32_t)0);
    memory_.write_value(object_ptr_ + 0x1c, (uint32_t)0);
    memory_.write_value(object_ptr_ + 0x68, shell_.get_file_mgr_object_ptr());

    memory_.write_value(vtable_ptr_ + 0x00, shell_.add_hook("IApplet_AddRef", this));
    memory_.write_value(vtable_ptr_ + 0x04, shell_.add_hook("IApplet_Release", this));
    memory_.write_value(vtable_ptr_ + 0x08, shell_.add_hook("IApplet_HandleEvent", this));
}

void BrewApplet::set_current_class(uint32_t cls_id) {
    memory_.write_value(object_ptr_ + 0x04, cls_id);
    shell_.set_current_applet_cls(cls_id);
    shell_.set_applet_object_ptr(object_ptr_);
}

void BrewApplet::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IApplet_AddRef") {
        ++refs_;
        memory_.write_value(object_ptr_ + 0x08, refs_);
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IApplet_Release") {
        if (refs_ > 0) {
            --refs_;
        }
        memory_.write_value(object_ptr_ + 0x08, refs_);
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IApplet_HandleEvent") {
        const uint32_t evt = r1;
        printf("  IApplet_HandleEvent: evt=0x%x wParam=0x%x dwParam=0x%x cls=0x%08x\n",
               evt, r2, r3, memory_.read_value(object_ptr_ + 0x04));
        if (evt == kEvtAppStart) {
            shell_.set_current_applet_cls(memory_.read_value(object_ptr_ + 0x04));
        } else if (evt == kEvtAppStop) {
            // Keep the object alive; just report handled.
        }
        cpu.set_reg(REG_R0, 1);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), cpu.get_reg(REG_R0), r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
