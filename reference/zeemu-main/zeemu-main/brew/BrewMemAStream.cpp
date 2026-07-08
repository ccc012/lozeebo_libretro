#include "brew/BrewMemAStream.h"

#include "brew/BrewFileMgr.h"
#include "cpu/core/CPU.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

bool trace_memastream() {
    return std::getenv("ZEEMU_TRACE_UNZIP") != nullptr ||
           std::getenv("ZEEMU_TRACE_RESOURCES") != nullptr;
}

} // namespace

BrewMemAStream::BrewMemAStream(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewMemAStream::setup_vtable() {
    // SDK AEE.h: IMemAStream inherits IAStream/IBase.
    // [0] AddRef [1] Release [2] Readable [3] Read [4] Cancel
    // [5] Set    [6] SetEx
    vtable_ptr_ = shell_.malloc(7 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        const addr_t hook = shell_.add_hook("IMemAStream_" + name, this);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(index * 4), hook);
    };

    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "Readable");
    add_method(3, "Read");
    add_method(4, "Cancel");
    add_method(5, "Set");
    add_method(6, "SetEx");
}

void BrewMemAStream::set_buffer(addr_t guest_buffer, uint32_t size, uint32_t offset) {
    data_.clear();
    read_pos_ = 0;

    if (guest_buffer == 0 || guest_buffer >= 0xFF000000 || offset > size) {
        if (trace_memastream()) {
            printf("  IMemAStream_Set invalid buffer=0x%08x size=%u offset=%u\n",
                   guest_buffer, size, offset);
        }
        return;
    }

    const uint32_t available = size - offset;
    data_.resize(available);
    for (uint32_t i = 0; i < available; ++i) {
        data_[i] = static_cast<uint8_t>(memory_.read_value(guest_buffer + offset + i,
                                                           EndianMemory::Byte));
    }

    if (trace_memastream()) {
        printf("  IMemAStream_Set buffer=0x%08x size=%u offset=%u stored=%zu\n",
               guest_buffer, size, offset, data_.size());
    }
}

bool BrewMemAStream::read_remaining_from_current(std::vector<uint8_t>& out) {
    out.clear();
    if (read_pos_ > data_.size()) {
        return false;
    }
    out.insert(out.end(), data_.begin() + static_cast<std::ptrdiff_t>(read_pos_), data_.end());
    read_pos_ = data_.size();
    return true;
}

void BrewMemAStream::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const uint32_t r5 = cpu.get_reg(REG_R5);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const uint32_t r7 = cpu.get_reg(REG_R7);
    const bool is_thunk = r1 >= 0xFF000000;

    if (name == "IMemAStream_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }

    if (name == "IMemAStream_Release") {
        data_.clear();
        read_pos_ = 0;
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IMemAStream_Readable") {
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IMemAStream_Read") {
        const addr_t dest = is_thunk ? r5 : r1;
        const uint32_t want = is_thunk ? r6 : r2;
        if (dest == 0 || dest >= 0xFF000000 || want == 0 || read_pos_ >= data_.size()) {
            cpu.set_reg(REG_R0, 0);
            return;
        }

        const size_t n = std::min<size_t>(want, data_.size() - read_pos_);
        if (dest < 0x10000) {
            brew_store_low_pointer_shadow(dest, data_.data() + read_pos_, n);
            if (trace_memastream()) {
                printf("  IMemAStream_Read low dest shadow ptr=0x%x bytes=%zu\n", dest, n);
            }
        } else {
            memory_.write_bytes(dest, data_.data() + read_pos_, n);
        }
        read_pos_ += n;
        if (trace_memastream() || n != want) {
            printf("  IMemAStream_Read want=%u -> read=%zu\n", want, n);
        }
        cpu.set_reg(REG_R0, static_cast<uint32_t>(n));
        return;
    }

    if (name == "IMemAStream_Cancel") {
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IMemAStream_Set" || name == "IMemAStream_SetEx") {
        const addr_t buffer = is_thunk ? r5 : r1;
        const uint32_t size = is_thunk ? r6 : r2;
        const uint32_t offset = is_thunk ? r7 : r3;
        set_buffer(buffer, size, offset);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, 1);
}
