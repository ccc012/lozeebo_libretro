#include "brew/BrewRandom.h"
#include "cpu/core/CPU.h"
#include <chrono>

static uint64_t random_host_now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

BrewRandom::BrewRandom(BrewShell& shell, EndianMemory& memory) : shell_(shell), memory_(memory) {
    vtable_ptr_ = shell_.malloc(5 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    const char* names[] = { "AddRef", "Release", "QueryInterface", "Read", "Readable" };
    for (int i = 0; i < 5; ++i) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(std::string("Random_") + names[i], this));
    }

    uint64_t seed = random_host_now_ms() ^ reinterpret_cast<uintptr_t>(this);
    rng_.seed(static_cast<uint32_t>(seed ^ (seed >> 32)));
}

void BrewRandom::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    if (name == "Random_AddRef") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "Random_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Random_QueryInterface") {
        uint32_t pp = cpu.get_reg(REG_R2);
        if (pp && pp < 0xFF000000) {
            memory_.write_value(pp, r0);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "Random_Read") {
        if (r1 && r1 < 0xFF000000) {
            for (uint32_t i = 0; i < r2; ++i) {
                memory_.write_value(r1 + i, static_cast<uint8_t>(rng_() & 0xffu), EndianMemory::Byte);
            }
        }
        cpu.set_reg(REG_R0, r2);
    } else if (name == "Random_Readable") {
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x\n",
               name.c_str(), r0, r1, r2);
        cpu.set_reg(REG_R0, 0);
    }
}
