#include "brew/BrewMicro3D.h"

#include "brew/BrewBitmap.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/VirtualMemory.h"
#include "graphics/RenderBackend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool parse_micro3d_slot(const std::string& name, int& slot) {
    static const std::string micro3d_prefix = "Private3D_Fn";
    if (name.rfind(micro3d_prefix, 0) == 0) {
        slot = std::atoi(name.c_str() + micro3d_prefix.size());
        return true;
    }

    return false;
}

bool guest_ptr(addr_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

uint32_t read_guest32(EndianMemory& memory, addr_t ptr) {
    return guest_ptr(ptr) ? memory.read_value(ptr) : 0;
}

bool should_trace_micro3d_call(int slot) {
    if (std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
        std::getenv("ZEEMU_TRACE_MICRO3D") != nullptr) {
        return true;
    }
    if (slot < 0 || slot >= 128) {
        return false;
    }
    static std::array<uint16_t, 128> slot_trace_counts{};
    uint16_t& count = slot_trace_counts[(size_t)slot];
    if (count < 8) {
        ++count;
        return true;
    }
    if (count != UINT16_MAX) {
        ++count;
    }
    return false;
}

void trace_micro3d_call(EndianMemory& memory, int slot, CPU& cpu, const char* note) {
    const bool force_log = note != nullptr && std::strstr(note, "not implemented yet") != nullptr;
    if (!force_log && !should_trace_micro3d_call(slot)) {
        return;
    }
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const uint32_t sp = cpu.get_reg(REG_SP);
    const uint32_t lr = cpu.get_reg(REG_LR);
    const uint32_t s0 = read_guest32(memory, sp);
    const uint32_t s1 = read_guest32(memory, sp + 4);
    printf("  Private3D_Fn%d %sR0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x SP=0x%08x LR=0x%08x [SP]=0x%08x [SP+4]=0x%08x\n",
           slot, note ? note : "", r0, r1, r2, r3, sp, lr, s0, s1);
    if (slot == 37 && guest_ptr(s0)) {
        printf("    Fn37 source16=%04x %04x %04x %04x\n",
               static_cast<uint16_t>(memory.read_value(s0 + 0, EndianMemory::Halfword)),
               static_cast<uint16_t>(memory.read_value(s0 + 2, EndianMemory::Halfword)),
               static_cast<uint16_t>(memory.read_value(s0 + 4, EndianMemory::Halfword)),
               static_cast<uint16_t>(memory.read_value(s0 + 6, EndianMemory::Halfword)));
    }
    if ((slot == 23 || slot == 25 || slot == 29 || slot == 39 || slot == 40 || slot == 52 || slot == 59 || slot == 72) &&
        guest_ptr(r1)) {
        printf("    Fn%d *R1=%08x %08x %08x %08x\n",
               slot,
               memory.read_value(r1 + 0),
               memory.read_value(r1 + 4),
               memory.read_value(r1 + 8),
               memory.read_value(r1 + 12));
    }
    if (slot == 40 && guest_ptr(r3)) {
        printf("    Fn40 *R3=%08x %08x %08x %08x\n",
               memory.read_value(r3 + 0),
               memory.read_value(r3 + 4),
               memory.read_value(r3 + 8),
               memory.read_value(r3 + 12));
    }
}

int32_t micro3d_q12_trig(uint32_t angle, bool cosine) {
    // Action Hero 3D builds a 13-entry table with angle steps of 4096/12
    // through private 0x010292c3 slots 64/65. This matches the classic BREW I3DUtil Q12
    // angle convention already proven by the SDK Tutori3D samples.
    const double radians = (static_cast<double>(static_cast<int32_t>(angle)) / 4096.0) *
                           2.0 * 3.14159265358979323846;
    const double value = cosine ? std::cos(radians) : std::sin(radians);
    return static_cast<int32_t>(std::lround(value * 4096.0));
}

int32_t read_guest_i32(EndianMemory& memory, addr_t ptr) {
    return static_cast<int32_t>(read_guest32(memory, ptr));
}

void write_guest_i32(EndianMemory& memory, addr_t ptr, int32_t value) {
    memory.write_value(ptr, static_cast<uint32_t>(value));
}

void write_guest_byte(EndianMemory& memory, addr_t ptr, uint8_t value) {
    if (guest_ptr(ptr)) {
        memory.write_value(ptr, value, EndianMemory::Byte);
    }
}

void write_guest_half(EndianMemory& memory, addr_t ptr, uint16_t value) {
    if (guest_ptr(ptr)) {
        memory.write_value(ptr, value, EndianMemory::Halfword);
    }
}

void write_guest_zero32(EndianMemory& memory, addr_t base, uint32_t offset) {
    if (guest_ptr(base + offset)) {
        memory.write_value(base + offset, 0);
    }
}

void write_identity_matrix3x4_q12(EndianMemory& memory, addr_t dst) {
    if (!guest_ptr(dst)) {
        return;
    }
    // Constant copied by imicro3d.mod Fn3 at 0x10002288.
    static constexpr uint32_t kIdentity3x4[12] = {
        0x1000, 0, 0, 0,
        0, 0x1000, 0, 0,
        0, 0, 0x1000, 0,
    };
    for (size_t i = 0; i < std::size(kIdentity3x4); ++i) {
        memory.write_value(dst + static_cast<addr_t>(i * 4), kIdentity3x4[i]);
    }
}

void init_micro3d_list(EndianMemory& memory, addr_t ptr) {
    if (!guest_ptr(ptr)) {
        return;
    }
    // Helper at imicro3d.mod 0x100138a8: two object pointers, counters/flags.
    memory.write_value(ptr + 0x00, 0);
    memory.write_value(ptr + 0x04, 0);
    memory.write_value(ptr + 0x08, 0);
    memory.write_value(ptr + 0x0c, 0);
    memory.write_value(ptr + 0x10, 0);
    write_guest_byte(memory, ptr + 0x14, 0);
    write_guest_byte(memory, ptr + 0x15, 0);
}

void init_micro3d_owner_cache(EndianMemory& memory, addr_t object, addr_t owner) {
    if (!guest_ptr(object)) {
        return;
    }
    // Helper at imicro3d.mod 0x10007c50. The real code also allocates a 32-byte
    // table through the owner object when present; the traced path does not read
    // that table before further Micro3D setup.
    memory.write_value(object + 0x00, owner);
    memory.write_value(object + 0x04, 0);
    memory.write_value(object + 0x08, 0);
    memory.write_value(object + 0x0c, 0);
    memory.write_value(object + 0x10, 0);
    memory.write_value(object + 0x14, 0);
    memory.write_value(object + 0x18, 0);
    memory.write_value(object + 0x1c, 0);
    memory.write_value(object + 0x28, 0);
}

void init_micro3d_fn21_object(EndianMemory& memory, addr_t object, addr_t owner) {
    if (!guest_ptr(object)) {
        return;
    }
    memory.write_value(object + 0x0c, owner);
    static constexpr uint32_t kZeroOffsets[] = {
        0x00, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24,
    };
    for (uint32_t offset : kZeroOffsets) {
        write_guest_zero32(memory, object, offset);
    }
}

void mark_micro3d_resource_loaded(EndianMemory& memory, addr_t object) {
    if (!guest_ptr(object)) {
        return;
    }
    // imicro3d.mod 0x10002938 parses a model stream and marks [object] as
    // non-zero on success. DMC checks that boolean result before asking for the
    // animation table size through slot 72.
    memory.write_value(object + 0x00, 1);
}

void init_micro3d_fn67_object(EndianMemory& memory, addr_t object, addr_t owner) {
    if (!guest_ptr(object)) {
        return;
    }
    memory.write_value(object + 0x00, 0);
    memory.write_value(object + 0x0c, owner);
    memory.write_value(object + 0x10, 0);
    memory.write_value(object + 0x44, 0);
    write_guest_byte(memory, object + 0x48, 0);
    init_micro3d_list(memory, object + 0x14);
    init_micro3d_list(memory, object + 0x2c);
}

void init_micro3d_fn27_object(EndianMemory& memory, addr_t object, addr_t owner) {
    if (!guest_ptr(object)) {
        return;
    }
    memory.write_value(object + 0x0c, owner);
    static constexpr uint32_t kZeroOffsets[] = {
        0x00, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x2c,
        0x30, 0x34, 0x40, 0x44, 0x50, 0x54, 0x60, 0x64,
        0xc8, 0xcc, 0xd0, 0xd4, 0xdc, 0xe0, 0xe8, 0xec,
    };
    for (uint32_t offset : kZeroOffsets) {
        write_guest_zero32(memory, object, offset);
    }
    init_micro3d_fn67_object(memory, object + 0x70, 0);
}

int32_t q12_dot_neg_origin(EndianMemory& memory, addr_t origin, const std::array<int32_t, 3>& axis) {
    const int64_t x = -static_cast<int64_t>(read_guest_i32(memory, origin + 0));
    const int64_t y = -static_cast<int64_t>(read_guest_i32(memory, origin + 4));
    const int64_t z = -static_cast<int64_t>(read_guest_i32(memory, origin + 8));
    const int64_t sum = x * axis[0] + y * axis[1] + z * axis[2];
    return static_cast<int32_t>((sum + 0x800) >> 12);
}

std::array<int32_t, 3> read_vec3(EndianMemory& memory, addr_t ptr) {
    return {
        read_guest_i32(memory, ptr + 0),
        read_guest_i32(memory, ptr + 4),
        read_guest_i32(memory, ptr + 8),
    };
}

void write_vec3(EndianMemory& memory, addr_t ptr, const std::array<int32_t, 3>& v) {
    write_guest_i32(memory, ptr + 0, v[0]);
    write_guest_i32(memory, ptr + 4, v[1]);
    write_guest_i32(memory, ptr + 8, v[2]);
}

std::array<int32_t, 3> cross_vec3_raw(const std::array<int32_t, 3>& a,
                                      const std::array<int32_t, 3>& b) {
    return {
        static_cast<int32_t>(static_cast<int64_t>(a[1]) * b[2] - static_cast<int64_t>(a[2]) * b[1]),
        static_cast<int32_t>(static_cast<int64_t>(a[2]) * b[0] - static_cast<int64_t>(a[0]) * b[2]),
        static_cast<int32_t>(static_cast<int64_t>(a[0]) * b[1] - static_cast<int64_t>(a[1]) * b[0]),
    };
}

std::array<int32_t, 3> normalize_vec3_q12(const std::array<int32_t, 3>& v) {
    const double x = static_cast<double>(v[0]);
    const double y = static_cast<double>(v[1]);
    const double z = static_cast<double>(v[2]);
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0) {
        return {0, 0, 4096};
    }
    return {
        static_cast<int32_t>(std::lround((x * 4096.0) / length)),
        static_cast<int32_t>(std::lround((y * 4096.0) / length)),
        static_cast<int32_t>(std::lround((z * 4096.0) / length)),
    };
}

void build_view_matrix3x4_q12(EndianMemory& memory, addr_t dst, addr_t origin,
                              addr_t forward, addr_t up) {
    if (!guest_ptr(dst) || !guest_ptr(origin) || !guest_ptr(forward) || !guest_ptr(up)) {
        return;
    }
    // imicro3d.mod Fn11 at 0x100024d8: z=norm(forward),
    // x=norm(forward x up), y=norm(forward x x), translation is dot(-origin, axis).
    const auto z_axis = normalize_vec3_q12(read_vec3(memory, forward));
    const auto x_axis = normalize_vec3_q12(cross_vec3_raw(read_vec3(memory, forward), read_vec3(memory, up)));
    const auto y_axis = normalize_vec3_q12(cross_vec3_raw(read_vec3(memory, forward), x_axis));

    write_vec3(memory, dst + 0x20, z_axis);
    write_guest_i32(memory, dst + 0x2c, q12_dot_neg_origin(memory, origin, z_axis));
    write_vec3(memory, dst + 0x00, x_axis);
    write_guest_i32(memory, dst + 0x0c, q12_dot_neg_origin(memory, origin, x_axis));
    write_vec3(memory, dst + 0x10, y_axis);
    write_guest_i32(memory, dst + 0x1c, q12_dot_neg_origin(memory, origin, y_axis));
}

void update_micro3d_view_derived(EndianMemory& memory, addr_t ctx) {
    if (!guest_ptr(ctx)) {
        return;
    }
    const int16_t x = static_cast<int16_t>(memory.read_value(ctx + 0xa48, EndianMemory::Halfword));
    const int16_t y = static_cast<int16_t>(memory.read_value(ctx + 0xa4a, EndianMemory::Halfword));
    memory.write_value(ctx + 0xa88, static_cast<uint32_t>(static_cast<int32_t>(x)));
    memory.write_value(ctx + 0xa98, static_cast<uint32_t>(static_cast<int32_t>(y)));
    write_guest_byte(memory, ctx + 0xadc, 0);
}

void init_micro3d_context(EndianMemory& memory, addr_t ctx, addr_t owner) {
    if (!guest_ptr(ctx)) {
        return;
    }
    memory.write_value(ctx + 0x00, owner);
    for (uint32_t offset = 0x04; offset <= 0x54; offset += 4) {
        memory.write_value(ctx + offset, 0);
    }
    write_identity_matrix3x4_q12(memory, ctx + 0xa7c);
    write_identity_matrix3x4_q12(memory, ctx + 0xa4c);
    write_identity_matrix3x4_q12(memory, ctx + 0xaac);
    write_guest_byte(memory, ctx + 0xadc, 1);
    init_micro3d_owner_cache(memory, ctx + 0x11c, owner);
    init_micro3d_list(memory, ctx + 0x148);
    memory.write_value(ctx + 0xa40, 8);
    memory.write_value(ctx + 0x160, 0);
    for (uint32_t offset = 0x180; offset < 0x1c0; offset += 4) {
        memory.write_value(ctx + offset, 0);
    }
    memory.write_value(ctx + 0x1c4, 0);
    init_micro3d_fn67_object(memory, ctx + 0x1c8, owner);
    memory.write_value(ctx + 0xae0, 0);
    memory.write_value(ctx + 0xae4, 0);
    memory.write_value(ctx + 0xae8, 0x1000);
    memory.write_value(ctx + 0xaec, 0x1000);
    memory.write_value(ctx + 0xa20, 0);
    memory.write_value(ctx + 0x1c0, 0);
    write_guest_byte(memory, ctx + 0x164, 0);

    // Practical framebuffer extent used by the traced 320-wide A3D path. The
    // real code derives these through helper calls after owner-cache setup.
    memory.write_value(ctx + 0x150, 320);
    memory.write_value(ctx + 0x154, 240);
    update_micro3d_view_derived(memory, ctx);
}

void configure_micro3d_projection(EndianMemory& memory, addr_t ctx,
                                  uint32_t near_z, uint32_t far_z, uint32_t fov) {
    if (!guest_ptr(ctx)) {
        return;
    }
    const int32_t near_i = static_cast<int16_t>(near_z & 0xffffu);
    const int32_t far_i = static_cast<int16_t>(far_z & 0xffffu);
    if (near_i <= 0 || far_i <= 0 || near_i >= far_i) {
        return;
    }
    int32_t clamped_fov = static_cast<int32_t>(fov);
    clamped_fov = std::clamp(clamped_fov, 2, 0x7fe);
    const int32_t half_fov = clamped_fov >> 1;
    const int32_t s = micro3d_q12_trig(static_cast<uint32_t>(half_fov), false);
    const int32_t c = micro3d_q12_trig(static_cast<uint32_t>(half_fov), true);
    write_guest_half(memory, ctx + 0x168, static_cast<uint16_t>(near_i));
    write_guest_half(memory, ctx + 0x166, static_cast<uint16_t>(far_i));
    memory.write_value(ctx + 0x16c, static_cast<uint32_t>(s));
    memory.write_value(ctx + 0x170, static_cast<uint32_t>(c));
    memory.write_value(ctx + 0x174, static_cast<uint32_t>((0x7fff / std::max(1, far_i - near_i))));
    memory.write_value(ctx + 0x178, static_cast<uint32_t>((near_i * far_i) / std::max(1, far_i - near_i)));
    write_guest_byte(memory, ctx + 0x164, 1);
    update_micro3d_view_derived(memory, ctx);
}

void cross_vec3_q12_raw(EndianMemory& memory, addr_t dst, addr_t a, addr_t b) {
    if (!guest_ptr(dst) || !guest_ptr(a) || !guest_ptr(b)) {
        return;
    }
    const int64_t ax = read_guest_i32(memory, a + 0);
    const int64_t ay = read_guest_i32(memory, a + 4);
    const int64_t az = read_guest_i32(memory, a + 8);
    const int64_t bx = read_guest_i32(memory, b + 0);
    const int64_t by = read_guest_i32(memory, b + 4);
    const int64_t bz = read_guest_i32(memory, b + 8);

    write_guest_i32(memory, dst + 0, static_cast<int32_t>(ay * bz - az * by));
    write_guest_i32(memory, dst + 4, static_cast<int32_t>(az * bx - ax * bz));
    write_guest_i32(memory, dst + 8, static_cast<int32_t>(ax * by - ay * bx));
}

void normalize_vec3_q12(EndianMemory& memory, addr_t dst, addr_t src) {
    if (!guest_ptr(dst) || !guest_ptr(src)) {
        return;
    }
    const double x = static_cast<double>(read_guest_i32(memory, src + 0));
    const double y = static_cast<double>(read_guest_i32(memory, src + 4));
    const double z = static_cast<double>(read_guest_i32(memory, src + 8));
    const double length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0) {
        write_guest_i32(memory, dst + 0, 0);
        write_guest_i32(memory, dst + 4, 0);
        write_guest_i32(memory, dst + 8, 4096);
        return;
    }
    write_guest_i32(memory, dst + 0, static_cast<int32_t>(std::lround((x * 4096.0) / length)));
    write_guest_i32(memory, dst + 4, static_cast<int32_t>(std::lround((y * 4096.0) / length)));
    write_guest_i32(memory, dst + 8, static_cast<int32_t>(std::lround((z * 4096.0) / length)));
}

}

BrewMicro3D::BrewMicro3D(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewMicro3D::setup_vtable() {
    object_ptr_ = shell_.malloc(0x80);
    vtable_ptr_ = shell_.malloc(128 * 4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    for (int i = 0; i < 128; ++i) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook("Private3D_Fn" + std::to_string(i), this));
    }
}

void BrewMicro3D::upload_surface(addr_t ptr, CPU& cpu) {
    const int width = static_cast<int>(cpu.get_reg(REG_R1));
    const int height = static_cast<int>(cpu.get_reg(REG_R2));
    const int source_pitch_pixels = static_cast<int>(cpu.get_reg(REG_R3));
    const addr_t source = read_guest32(memory_, cpu.get_reg(REG_SP));

    if (!guest_ptr(ptr) || !guest_ptr(source) || width <= 0 || height <= 0 ||
        width > 2048 || height > 2048 || source_pitch_pixels < width) {
        return;
    }

    Surface& surface = surfaces_[ptr];
    surface.width = width;
    surface.height = height;
    surface.rgb565.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        const addr_t row = source + static_cast<addr_t>(y) * static_cast<addr_t>(source_pitch_pixels) * 2u;
        for (int x = 0; x < width; ++x) {
            surface.rgb565[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                static_cast<uint16_t>(memory_.read_value(row + static_cast<addr_t>(x) * 2u, EndianMemory::Halfword));
        }
    }
}

void BrewMicro3D::set_destination_rect(addr_t ptr, CPU& cpu) {
    if (!guest_ptr(ptr)) {
        return;
    }
    Surface& surface = surfaces_[ptr];
    surface.dst_x0 = static_cast<int>(cpu.get_reg(REG_R1));
    surface.dst_y0 = static_cast<int>(cpu.get_reg(REG_R2));
    surface.dst_x1 = static_cast<int>(cpu.get_reg(REG_R3));
    surface.dst_y1 = static_cast<int>(read_guest32(memory_, cpu.get_reg(REG_SP)));
}

void BrewMicro3D::blit_surface_to_display(addr_t ptr) {
    auto it = surfaces_.find(ptr);
    if (it == surfaces_.end()) {
        return;
    }
    Surface& surface = it->second;
    if (surface.width <= 0 || surface.height <= 0 || surface.rgb565.empty()) {
        return;
    }

    BrewDisplay* display = shell_.get_display();
    BrewBitmap* dst = display ? display->get_device_bitmap() : nullptr;
    if (!dst || dst->get_depth() != 16) {
        return;
    }

    int x0 = std::min(surface.dst_x0, surface.dst_x1);
    int y0 = std::min(surface.dst_y0, surface.dst_y1);
    int x1 = std::max(surface.dst_x0, surface.dst_x1);
    int y1 = std::max(surface.dst_y0, surface.dst_y1);
    if (x1 <= x0 || y1 <= y0) {
        x0 = 0;
        y0 = 0;
        x1 = surface.width;
        y1 = surface.height;
    }
    x0 = std::clamp(x0, 0, dst->get_width());
    y0 = std::clamp(y0, 0, dst->get_height());
    x1 = std::clamp(x1, 0, dst->get_width());
    y1 = std::clamp(y1, 0, dst->get_height());
    const int draw_w = x1 - x0;
    const int draw_h = y1 - y0;
    if (draw_w <= 0 || draw_h <= 0) {
        return;
    }

    const addr_t dst_base = dst->get_buffer_ptr();
    for (int y = 0; y < draw_h; ++y) {
        const int sy = std::min(surface.height - 1, (y * surface.height) / draw_h);
        const addr_t dst_row = dst_base + (addr_t)(y0 + y) * (addr_t)dst->get_pitch();
        for (int x = 0; x < draw_w; ++x) {
            const int sx = std::min(surface.width - 1, (x * surface.width) / draw_w);
            const uint16_t px = surface.rgb565[static_cast<size_t>(sy) * static_cast<size_t>(surface.width) + static_cast<size_t>(sx)];
            memory_.write_value(dst_row + static_cast<addr_t>(x0 + x) * 2u, px, EndianMemory::Halfword);
        }
    }

    if (auto* presenter = shell_.get_presenter()) {
        if (auto* vm = shell_.get_virtual_memory()) {
            if (void* host_ptr = vm->get_host_address(dst->get_buffer_ptr())) {
                presenter->begin_frame();
                presenter->present_rgb565(host_ptr, dst->get_pitch(), dst->get_width(), dst->get_height());
                presenter->end_frame();
            }
        }
    }
}

void BrewMicro3D::handle_hook(const std::string& name, CPU& cpu) {
    int slot = -1;
    if (!parse_micro3d_slot(name, slot)) {
        cpu.set_reg(REG_R0, 0);
        return;
    }

    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r2 = cpu.get_reg(REG_R2);

    if (slot == 0) {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (slot == 1) {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (slot == 2) {
        if (guest_ptr(r2)) {
            memory_.write_value(r2, r0);
        }
        cpu.set_reg(REG_R0, 0);
        return;
    }

    // CLSID 0x010292c3 is a private HI Corporation Micro3D service shipped as
    // imicro3d.mod with some titles. Slot 69 is proven by disassembly to be a
    // boolean gate: returning zero forces the caller into cleanup before the
    // asset path can complete, unlike normal BREW SUCCESS-style calls.
    if (slot == 69) {
        trace_micro3d_call(memory_, slot, cpu, "commit/check -> TRUE ");
        cpu.set_reg(REG_R0, 1);
        return;
    }

    switch (slot) {
    case 3:
        trace_micro3d_call(memory_, slot, cpu, "identity matrix3x4 Q12 ");
        write_identity_matrix3x4_q12(memory_, r0);
        cpu.set_reg(REG_R0, r0 + 0x20);
        return;
    case 11:
        trace_micro3d_call(memory_, slot, cpu, "build view matrix3x4 Q12 ");
        build_view_matrix3x4_q12(memory_, r0, cpu.get_reg(REG_R1), r2, cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, r0);
        return;
    case 34:
        trace_micro3d_call(memory_, slot, cpu, "init render context ");
        init_micro3d_context(memory_, r0, cpu.get_reg(REG_R1));
        cpu.set_reg(REG_R0, r0);
        return;
    case 35:
    case 39:
    case 43:
    case 51:
    case 52:
    case 53:
    case 59:
    case 83:
        trace_micro3d_call(memory_, slot, cpu, "not implemented yet ");
        cpu.set_reg(REG_R0, 0);
        return;
    case 21:
        trace_micro3d_call(memory_, slot, cpu, "init object28 ");
        init_micro3d_fn21_object(memory_, r0, cpu.get_reg(REG_R1));
        cpu.set_reg(REG_R0, r0);
        return;
    case 23:
        trace_micro3d_call(memory_, slot, cpu, "load model stream accepted ");
        mark_micro3d_resource_loaded(memory_, r0);
        cpu.set_reg(REG_R0, guest_ptr(r0) && guest_ptr(cpu.get_reg(REG_R1)) ? 1 : 0);
        return;
    case 25:
        trace_micro3d_call(memory_, slot, cpu, "model table value default ");
        cpu.set_reg(REG_R0, 0);
        return;
    case 27:
        trace_micro3d_call(memory_, slot, cpu, "init objectf8 ");
        init_micro3d_fn27_object(memory_, r0, cpu.get_reg(REG_R1));
        cpu.set_reg(REG_R0, r0 + 0x9c);
        return;
    case 29:
        trace_micro3d_call(memory_, slot, cpu, "load resource stream accepted ");
        mark_micro3d_resource_loaded(memory_, r0);
        cpu.set_reg(REG_R0, guest_ptr(r0) && guest_ptr(cpu.get_reg(REG_R1)) ? 1 : 0);
        return;
    case 19:
        trace_micro3d_call(memory_, slot, cpu, "cross vec3 ");
        cross_vec3_q12_raw(memory_, r0, cpu.get_reg(REG_R1), r2);
        cpu.set_reg(REG_R0, 0);
        return;
    case 20:
        trace_micro3d_call(memory_, slot, cpu, "normalize vec3 Q12 ");
        normalize_vec3_q12(memory_, r0, cpu.get_reg(REG_R1));
        cpu.set_reg(REG_R0, 0);
        return;
    case 64:
        trace_micro3d_call(memory_, slot, cpu, "sin Q12 ");
        cpu.set_reg(REG_R0, static_cast<uint32_t>(micro3d_q12_trig(r0, false)));
        return;
    case 65:
        trace_micro3d_call(memory_, slot, cpu, "cos Q12 ");
        cpu.set_reg(REG_R0, static_cast<uint32_t>(micro3d_q12_trig(r0, true)));
        return;
    case 37:
        trace_micro3d_call(memory_, slot, cpu, "upload RGB565 surface ");
        upload_surface(r0, cpu);
        cpu.set_reg(REG_R0, 0);
        return;
    case 38:
        trace_micro3d_call(memory_, slot, cpu, "destination rect ");
        set_destination_rect(r0, cpu);
        cpu.set_reg(REG_R0, 0);
        return;
    case 40:
        trace_micro3d_call(memory_, slot, cpu, "present surface ");
        blit_surface_to_display(r0);
        cpu.set_reg(REG_R0, 0);
        return;
    case 41:
        trace_micro3d_call(memory_, slot, cpu, "configure projection ");
        configure_micro3d_projection(memory_, r0, cpu.get_reg(REG_R1), r2, cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, r0);
        return;
    case 46:
        trace_micro3d_call(memory_, slot, cpu, "get context flags ");
        cpu.set_reg(REG_R0, guest_ptr(r0 + 0xa40) ? memory_.read_value(r0 + 0xa40) : 0);
        return;
    case 67:
        trace_micro3d_call(memory_, slot, cpu, "init resource object ");
        init_micro3d_fn67_object(memory_, r0, cpu.get_reg(REG_R1));
        cpu.set_reg(REG_R0, r0 + 0x2c);
        return;
    case 72:
        trace_micro3d_call(memory_, slot, cpu, "model table count ");
        cpu.set_reg(REG_R0, read_guest32(memory_, r0) != 0 ? 1 : 0);
        return;
    default:
        trace_micro3d_call(memory_, slot, cpu, "not implemented yet ");
        cpu.set_reg(REG_R0, 0);
        return;
    }
}
