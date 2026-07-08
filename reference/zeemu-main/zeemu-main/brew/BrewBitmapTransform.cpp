#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace {
constexpr uint32_t kAeeSuccess = 0u;
constexpr uint32_t kAeeClassNotSupported = 4u;
constexpr uint32_t kAeeIidBitmap = 0x01001021u;
constexpr uint32_t kAeeIidTransform = 0x01001029u;
constexpr uint32_t kAeeIidDib = 0x01001045u;
constexpr uint32_t kAeeIidDib20 = 0x0100102cu;

void scale_for_simple_transform(uint32_t transform, int& numerator, int& denominator) {
    numerator = 1;
    denominator = 1;
    switch (transform & 0x0038u) {
    case 0x0008u: numerator = 2; break;
    case 0x0010u: numerator = 4; break;
    case 0x0018u: numerator = 8; break;
    case 0x0028u: denominator = 8; break;
    case 0x0030u: denominator = 4; break;
    case 0x0038u: denominator = 2; break;
    default: break;
    }
}

void matrix_for_simple_transform(uint32_t transform, int& a, int& b, int& c, int& d) {
    int scale_num = 1;
    int scale_den = 1;
    scale_for_simple_transform(transform, scale_num, scale_den);

    const int s = 256 * scale_num / scale_den;
    switch (transform & 0x0003u) {
    case 0x0001u: // TRANSFORM_ROTATE_90
        a = 0; b = s; c = -s; d = 0;
        break;
    case 0x0002u: // TRANSFORM_ROTATE_180
        a = -s; b = 0; c = 0; d = -s;
        break;
    case 0x0003u: // TRANSFORM_ROTATE_270
        a = 0; b = -s; c = s; d = 0;
        break;
    default:
        a = s; b = 0; c = 0; d = s;
        break;
    }
    if (transform & 0x0004u) { // TRANSFORM_FLIP_X
        a = -a;
        c = -c;
    }
}
}

void BrewBitmap::setup_transform_vtable() {
    transform_vtable_ptr_ = shell_.malloc(5 * 4);
    transform_object_ptr_ = shell_.malloc(4);
    memory_.write_value(transform_object_ptr_, transform_vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        const addr_t hook_addr = shell_.add_hook("ITransform_" + name, this);
        memory_.write_value(transform_vtable_ptr_ + (index * 4), hook_addr);
    };

    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "QueryInterface");
    add_method(3, "TransformBltSimple");
    add_method(4, "TransformBltComplex");
}

void BrewBitmap::handle_transform_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);

    if (name == "ITransform_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "ITransform_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "ITransform_QueryInterface") {
        const uint32_t cls = r1;
        const uint32_t pp = cpu.get_reg(REG_R2);
        uint32_t obj = 0;
        if (cls == kAeeIidTransform) {
            obj = transform_object_ptr_;
        } else if (cls == kAeeIidBitmap || cls == kAeeIidDib || cls == kAeeIidDib20) {
            obj = object_ptr_;
        }
        if (pp != 0 && pp < 0xFF000000) {
            memory_.write_value(pp, obj);
        }
        cpu.set_reg(REG_R0, obj ? kAeeSuccess : kAeeClassNotSupported);
    } else if (name == "ITransform_TransformBltSimple") {
        const int dst_x = static_cast<int>(r1);
        const int dst_y = static_cast<int>(cpu.get_reg(REG_R2));
        const addr_t src_obj = cpu.get_reg(REG_R3);
        const uint32_t sp = cpu.get_reg(REG_SP);
        const int src_x = static_cast<int>(memory_.read_value(sp + 0));
        const int src_y = static_cast<int>(memory_.read_value(sp + 4));
        const int copy_w = static_cast<int>(memory_.read_value(sp + 8));
        const int copy_h = static_cast<int>(memory_.read_value(sp + 12));
        const uint32_t transform = memory_.read_value(sp + 16);
        const uint32_t composite = memory_.read_value(sp + 20) & 0xffu;
        int a = 256;
        int b = 0;
        int c = 0;
        int d = 256;
        matrix_for_simple_transform(transform, a, b, c, d);
        BrewBitmap* src = lookup_brew_bitmap(src_obj);
        const uint32_t result = transform_blit_from_bitmap(src, dst_x, dst_y, src_x, src_y,
                                                           copy_w, copy_h, a, b, c, d, composite);
        if (std::getenv("ZEEMU_TRACE_BITMAP")) {
            printf("  ITransform_TransformBltSimple: dst=0x%08x (%d,%d) src=0x%08x (%d,%d %dx%d) transform=0x%04x matrix=[%d,%d,%d,%d] composite=0x%02x result=%u\n",
                   object_ptr_, dst_x, dst_y, src_obj, src_x, src_y, copy_w, copy_h,
                   transform & 0xffffu, a, b, c, d, composite, result);
        }
        cpu.set_reg(REG_R0, result);
    } else if (name == "ITransform_TransformBltComplex") {
        const int dst_x = static_cast<int>(r1);
        const int dst_y = static_cast<int>(cpu.get_reg(REG_R2));
        const addr_t src_obj = cpu.get_reg(REG_R3);
        const uint32_t sp = cpu.get_reg(REG_SP);
        const int src_x = static_cast<int>(memory_.read_value(sp + 0));
        const int src_y = static_cast<int>(memory_.read_value(sp + 4));
        const int copy_w = static_cast<int>(memory_.read_value(sp + 8));
        const int copy_h = static_cast<int>(memory_.read_value(sp + 12));
        const addr_t matrix_ptr = memory_.read_value(sp + 16);
        const uint32_t composite = memory_.read_value(sp + 20) & 0xffu;
        int a = 256;
        int b = 0;
        int c = 0;
        int d = 256;
        if (matrix_ptr != 0 && matrix_ptr < 0xFF000000) {
            a = static_cast<int16_t>(memory_.read_value(matrix_ptr + 0, EndianMemory::Halfword));
            b = static_cast<int16_t>(memory_.read_value(matrix_ptr + 2, EndianMemory::Halfword));
            c = static_cast<int16_t>(memory_.read_value(matrix_ptr + 4, EndianMemory::Halfword));
            d = static_cast<int16_t>(memory_.read_value(matrix_ptr + 6, EndianMemory::Halfword));
        }
        BrewBitmap* src = lookup_brew_bitmap(src_obj);
        const uint32_t result = transform_blit_from_bitmap(src, dst_x, dst_y, src_x, src_y,
                                                           copy_w, copy_h, a, b, c, d, composite);
        if (std::getenv("ZEEMU_TRACE_BITMAP")) {
            printf("  ITransform_TransformBltComplex: dst=0x%08x (%d,%d) src=0x%08x (%d,%d %dx%d) matrix=0x%08x [%d,%d,%d,%d] composite=0x%02x result=%u\n",
                   object_ptr_, dst_x, dst_y, src_obj, src_x, src_y, copy_w, copy_h,
                   matrix_ptr, a, b, c, d, composite, result);
        }
        cpu.set_reg(REG_R0, result);
    }
}
