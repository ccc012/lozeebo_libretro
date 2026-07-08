#include "brew/BrewHeap.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>
#include <cstring>

BrewHeap::BrewHeap(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewHeap::setup_vtable() {
    vtable_ptr_ = shell_.malloc(9 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    const char* names[] = {
        "AddRef", "Release", "Malloc", "Realloc", "Free",
        "StrDup", "CheckAvail", "GetMemStats", "GetModuleMemStats"
    };
    for (int i = 0; i < 9; ++i) {
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(std::string("IHeap_") + names[i], this));
    }
}

void BrewHeap::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);

    if (name == "IHeap_AddRef") {
        cpu.set_reg(REG_R0, ++refs_);
    } else if (name == "IHeap_Release") {
        if (refs_ > 0) {
            --refs_;
        }
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IHeap_Malloc") {
        uint32_t size = r1 & ~0x80000000u;
        cpu.set_reg(REG_R0, shell_.malloc(size ? size : 1, (r1 & 0x80000000u) == 0));
    } else if (name == "IHeap_Realloc") {
        uint32_t p_old = r1;
        uint32_t size = r2 & ~0x80000000u;
        addr_t p_new = shell_.realloc_block(p_old, size ? size : 1, (r2 & 0x80000000u) == 0);
        cpu.set_reg(REG_R0, p_new);
    } else if (name == "IHeap_Free") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHeap_StrDup") {
        addr_t src = r1;
        if (!src || src >= 0xFF000000) {
            cpu.set_reg(REG_R0, 0);
        } else {
            size_t len = 0;
            while (memory_.read_value(src + (uint32_t)(len * 2), EndianMemory::Halfword) != 0) {
                ++len;
            }
            addr_t dst = shell_.malloc((uint32_t)((len + 1) * 2));
            for (size_t i = 0; i <= len; ++i) {
                uint16_t ch = (uint16_t)memory_.read_value(src + (uint32_t)(i * 2), EndianMemory::Halfword);
                memory_.write_value(dst + (uint32_t)(i * 2), ch, EndianMemory::Halfword);
            }
            cpu.set_reg(REG_R0, dst);
        }
    } else if (name == "IHeap_CheckAvail") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IHeap_GetMemStats") {
        // BREW AEEHeap.h: uint32 IHEAP_GetMemStats(IHeap *pIHeap)
        // returns total used memory, not a pointer-filled stats struct.
        cpu.set_reg(REG_R0, shell_.heap_used_bytes());
    } else if (name == "IHeap_GetModuleMemStats") {
        uint32_t p_max = r2;
        uint32_t p_cur = cpu.get_reg(REG_R3);
        if (p_max && p_max < 0xFF000000) {
            memory_.write_value(p_max, 0u);
        }
        if (p_cur && p_cur < 0xFF000000) {
            memory_.write_value(p_cur, 0u);
        }
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x\n",
               name.c_str(), r0, r1, r2);
        cpu.set_reg(REG_R0, r0);
    }
}
