#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace {
std::unordered_map<addr_t, BrewBitmap*> g_bitmap_registry;

constexpr uint32_t kAeeSuccess = 0u;
constexpr uint32_t kAeeClassNotSupported = 4u;
constexpr uint32_t kAeeIidBitmap = 0x01001021u;
constexpr uint32_t kAeeIidTransform = 0x01001029u;
constexpr uint32_t kAeeIidDib = 0x01001045u;
constexpr uint32_t kAeeIidDib20 = 0x0100102cu;

bool trace_hot_hle_call(const char* env_name, int limit = 8) {
    if (std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
        (env_name && std::getenv(env_name) != nullptr)) {
        return true;
    }
    static std::unordered_map<std::string, int> counts;
    const std::string key = env_name ? env_name : "";
    int& count = counts[key];
    if (count < limit) {
        ++count;
        return true;
    }
    return false;
}

int dib_pitch_bytes(int width, int depth) {
    if (width <= 0 || depth <= 0) {
        return 0;
    }
    // BREW-created DIB rows are top-down and 32-bit aligned. This also keeps
    // packed 1/2/4bpp rows usable instead of collapsing to zero bytes.
    const int bits = width * depth;
    return ((bits + 31) / 32) * 4;
}
}

BrewBitmap* lookup_brew_bitmap(addr_t object_ptr) {
    auto it = g_bitmap_registry.find(object_ptr);
    return it == g_bitmap_registry.end() ? nullptr : it->second;
}

BrewBitmap::BrewBitmap(BrewShell& shell, EndianMemory& memory, int width, int height, int depth,
                       int palette_entries, int extra_bytes, int color_scheme,
                       uint32_t transparent_color)
    : shell_(shell), memory_(memory), width_(std::max(width, 0)), height_(std::max(height, 0)),
      depth_(depth), pitch_(dib_pitch_bytes(std::max(width, 0), depth)),
      palette_entries_(std::max(palette_entries, 0)), extra_bytes_(std::max(extra_bytes, 0)),
      color_scheme_(color_scheme >= 0 ? color_scheme : (depth == 16 ? 16 : 0)),
      transparent_color_(transparent_color)
{
    size_t size = get_buffer_size();
    buffer_ptr_ = shell_.malloc((uint32_t)size);
    const size_t palette_size = (size_t)palette_entries_ * 4u + (size_t)extra_bytes_;
    if (palette_size > 0) {
        palette_ptr_ = shell_.malloc((uint32_t)palette_size);
    }
    setup_vtable();
    // BREW DIB allocations are deterministic. A random debug pattern leaks
    // into titles that build sprites through DrawPixel before blitting.
    for (size_t i = 0; i < size; ++i) {
        memory_.write_value(buffer_ptr_ + i, (uint8_t)0, EndianMemory::Byte);
    }
    g_bitmap_registry[object_ptr_] = this;
}

BrewBitmap::~BrewBitmap() {
    auto it = g_bitmap_registry.find(object_ptr_);
    if (it != g_bitmap_registry.end() && it->second == this) {
        g_bitmap_registry.erase(it);
    }
}

void BrewBitmap::setup_vtable() {
    vtable_ptr_ = shell_.malloc(32 * 4);
    object_ptr_ = shell_.malloc(36);
    memory_.write_value(object_ptr_, vtable_ptr_);
    write_dib_fields();
    setup_transform_vtable();

    auto add_method = [&](int index, const std::string& name) {
        addr_t hook_addr = shell_.add_hook("IBitmap_" + name, this);
        memory_.write_value(vtable_ptr_ + (index * 4), hook_addr);
    };

    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "QueryInterface");
    add_method(3, "RGBToNative");
    add_method(4, "NativeToRGB");
    add_method(5, "DrawPixel");
    add_method(6, "GetPixel");
    add_method(7, "SetPixels");
    add_method(8, "DrawHScanline");
    add_method(9, "FillRect");
    add_method(10, "BltIn");
    add_method(11, "BltOut");
    add_method(12, "GetInfo");
    add_method(13, "CreateCompatibleBitmap");
    add_method(14, "SetTransparencyColor");
    add_method(15, "GetTransparencyColor");
    for (int index = 16; index < 32; ++index) {
        add_method(index, "Fn" + std::to_string(index));
    }
}

void BrewBitmap::write_dib_fields() {
    // Qualcomm QDIB exposes IDIB as the bitmap object itself: vtable at +0,
    // then DIB fields. OpenLara and the BREW EGL samples read cx/cy/pBmp
    // directly after IBITMAP_QueryInterface(AEEIID_IDIB).
    memory_.write_value(object_ptr_ + 4, 0u);                 // pPaletteMap
    memory_.write_value(object_ptr_ + 8, buffer_ptr_);        // pBmp
    memory_.write_value(object_ptr_ + 12, palette_ptr_);      // pRGB
    memory_.write_value(object_ptr_ + 16, transparent_color_);// transparentColor
    memory_.write_value(object_ptr_ + 20, (uint16_t)width_, EndianMemory::Halfword);
    memory_.write_value(object_ptr_ + 22, (uint16_t)height_, EndianMemory::Halfword);
    memory_.write_value(object_ptr_ + 24, (uint16_t)pitch_, EndianMemory::Halfword);
    memory_.write_value(object_ptr_ + 26, (uint16_t)palette_entries_, EndianMemory::Halfword);
    memory_.write_value(object_ptr_ + 28, (uint8_t)depth_, EndianMemory::Byte);
    // AEEIDIB.h: nColorScheme identifies native packed mappings. Zeebo's
    // device bitmap is RGB565, and some software renderers read this field
    // directly after QueryInterface(AEEIID_IDIB).
    memory_.write_value(object_ptr_ + 29, (uint8_t)color_scheme_, EndianMemory::Byte);
    for (uint32_t i = 30; i < 36; ++i) {
        memory_.write_value(object_ptr_ + i, (uint8_t)0, EndianMemory::Byte);
    }
}

void BrewBitmap::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);

    if (name.rfind("ITransform_", 0) == 0) {
        handle_transform_hook(name, cpu);
        return;
    }
    if (name == "IBitmap_AddRef") {
        cpu.set_reg(REG_R0, r0);
        return;
    }
    if (name == "IBitmap_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IBitmap_QueryInterface") {
        const uint32_t cls = r1;
        const uint32_t pp = cpu.get_reg(REG_R2);
        uint32_t obj = 0;
        if (cls == kAeeIidBitmap || cls == kAeeIidDib || cls == kAeeIidDib20) {
            obj = object_ptr_;
        } else if (cls == kAeeIidTransform) {
            obj = transform_object_ptr_;
        }
        if (pp != 0) {
            memory_.write_value(pp, obj);
        }
        if (trace_hot_hle_call("ZEEMU_TRACE_BITMAP")) {
            printf("  IBitmap_QueryInterface cls=0x%08x pp=0x%08x -> 0x%08x\n", cls, pp, obj);
        }
        cpu.set_reg(REG_R0, obj ? kAeeSuccess : kAeeClassNotSupported);
        return;
    }
    if (handle_color_hook(name, cpu) ||
        handle_pixel_hook(name, cpu) ||
        handle_blit_hook(name, cpu) ||
        handle_info_hook(name, cpu)) {
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
    cpu.set_reg(REG_R0, 0);
}
