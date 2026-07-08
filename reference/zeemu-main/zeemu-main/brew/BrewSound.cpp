#include "brew/BrewSound.h"
#include "cpu/core/CPU.h"
#include <iostream>

BrewSound::BrewSound(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}

void BrewSound::setup_vtable() {
    vtable_ptr_ = shell_.malloc(32 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        addr_t hook_addr = shell_.add_hook("ISound_" + name, this);
        memory_.write_value(vtable_ptr_ + (index * 4), hook_addr);
    };

    // AEEISound.h: ISound inherits IBase, then RegisterNotify.
    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "RegisterNotify");
    add_method(3, "Set");
    add_method(4, "Get");
    add_method(5, "SetDevice");
    add_method(6, "PlayTone");
    add_method(7, "PlayToneList");
    add_method(8, "PlayFreqTone");
    add_method(9, "StopTone");
    add_method(10, "Vibrate");
    add_method(11, "StopVibrate");
    add_method(12, "SetVolume");
    add_method(13, "GetVolume");
    add_method(14, "GetResourceCtl");
    for (int index = 15; index < 32; ++index) {
        add_method(index, "Fn" + std::to_string(index));
    }
}

void BrewSound::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);

    if (name == "ISound_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "ISound_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "ISound_RegisterNotify") {
        // RegisterNotify(po, pfn, pUser)
        printf("  ISound_RegisterNotify: pfn=0x%08x pUser=0x%08x\n", r1, r2);
        
        // Some apps (CNK) might be waiting for a success notification.
        // We can't fire it synchronously here without risking stack depth/recursion
        // if the callback calls more HLE. 
        // For now, just return SUCCESS.
        cpu.set_reg(REG_R0, 0);
    } else if (name.substr(0, 8) == "ISound_") {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, 0);
    }
}
