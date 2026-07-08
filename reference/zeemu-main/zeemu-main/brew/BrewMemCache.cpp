#include "brew/BrewMemCache.h"

#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdlib>
#include <cstdio>

namespace {
constexpr uint32_t kAEEIIDIMemCache1 = 0x0106e2e0;
constexpr uint32_t kAEEIIDMemCache = 0x010303a1;
constexpr uint32_t kSuccess = 0;
constexpr uint32_t kEClassNotSupport = 2;

bool trace_memcache() {
    return std::getenv("ZEEMU_TRACE_HLE") != nullptr;
}
}

BrewMemCache::BrewMemCache(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewMemCache::setup_vtable() {
    // BREW MP AEEIMemCache1.h:
    // IQI + ClearCache + GetCacheInfo. The deprecated OEM IMemCache shape uses
    // the same first four slots and names slot 3 Clean, so one object can serve
    // both module-runtime cache flush paths.
    vtable_ptr_ = shell_.malloc(5 * 4);
    const char* names[] = {
        "AddRef", "Release", "QueryInterface", "ClearCache", "GetCacheInfo"
    };
    for (uint32_t i = 0; i < 5; ++i) {
        memory_.write_value(vtable_ptr_ + i * 4, shell_.add_hook(std::string("IMemCache1_") + names[i], this));
    }
    object_ptr_ = shell_.malloc(8);
    memory_.write_value(object_ptr_, vtable_ptr_);
    memory_.write_value(object_ptr_ + 4, ref_count_);
}

void BrewMemCache::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IMemCache1_AddRef") {
        ++ref_count_;
        memory_.write_value(object_ptr_ + 4, ref_count_);
        cpu.set_reg(REG_R0, ref_count_);
        return;
    }

    if (name == "IMemCache1_Release") {
        if (ref_count_ > 0) {
            --ref_count_;
        }
        memory_.write_value(object_ptr_ + 4, ref_count_);
        cpu.set_reg(REG_R0, ref_count_);
        return;
    }

    if (name == "IMemCache1_QueryInterface") {
        const uint32_t iid = r1;
        const uint32_t ppObj = r2;
        if (iid == kAEEIIDIMemCache1 || iid == kAEEIIDMemCache) {
            if (ppObj != 0 && ppObj < 0xff000000u) {
                memory_.write_value(ppObj, object_ptr_);
            }
            ++ref_count_;
            memory_.write_value(object_ptr_ + 4, ref_count_);
            cpu.set_reg(REG_R0, kSuccess);
        } else {
            if (ppObj != 0 && ppObj < 0xff000000u) {
                memory_.write_value(ppObj, 0u);
            }
            cpu.set_reg(REG_R0, kEClassNotSupport);
        }
        return;
    }

    if (name == "IMemCache1_ClearCache") {
        if (trace_memcache()) {
            std::printf("  IMemCache1_ClearCache addr=0x%08x size=%u op=0x%08x flags=0x%08x\n",
                        r1, r2, r3, cpu.mem().read_value(cpu.get_reg(REG_SP)));
        }
        cpu.set_reg(REG_R0, kSuccess);
        return;
    }

    if (name == "IMemCache1_GetCacheInfo") {
        const uint32_t cache_type = r1;
        const uint32_t pInfo = r2;
        if (pInfo != 0 && pInfo < 0xff000000u) {
            memory_.write_value(pInfo + 0, 32u);  // ulLineLen
            memory_.write_value(pInfo + 4, 4u);   // ulAssociativity
            memory_.write_value(pInfo + 8, 128u); // ulNumSets
        }
        if (trace_memcache()) {
            std::printf("  IMemCache1_GetCacheInfo type=0x%08x pInfo=0x%08x\n", cache_type, pInfo);
        }
        cpu.set_reg(REG_R0, kSuccess);
        return;
    }

    std::printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, kEClassNotSupport);
}
