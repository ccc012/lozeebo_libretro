#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <cstdint>

namespace {
constexpr uint32_t kRgbNone = 0xffffffffu;

uint16_t rgbval_to_native16(uint32_t rgb) {
    if (rgb <= 0xffffu) {
        return static_cast<uint16_t>(rgb);
    }

    // BREW RGBVAL is MAKE_RGB(r,g,b) = r<<8 | g<<16 | b<<24.
    const uint32_t r = (rgb >> 8) & 0xffu;
    const uint32_t g = (rgb >> 16) & 0xffu;
    const uint32_t b = (rgb >> 24) & 0xffu;
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint32_t native16_to_rgbval(uint32_t native) {
    const uint32_t r5 = (native >> 11) & 0x1fu;
    const uint32_t g6 = (native >> 5) & 0x3fu;
    const uint32_t b5 = native & 0x1fu;
    const uint32_t r = (r5 << 3) | (r5 >> 2);
    const uint32_t g = (g6 << 2) | (g6 >> 4);
    const uint32_t b = (b5 << 3) | (b5 >> 2);
    return (r << 8) | (g << 16) | (b << 24);
}

uint32_t make_rgbval(uint32_t r, uint32_t g, uint32_t b) {
    return ((r & 0xffu) << 8) | ((g & 0xffu) << 16) | ((b & 0xffu) << 24);
}

uint32_t rgbval_to_palette_entry(uint32_t rgb) {
    const uint32_t r = (rgb >> 8) & 0xffu;
    const uint32_t g = (rgb >> 16) & 0xffu;
    const uint32_t b = (rgb >> 24) & 0xffu;
    // AEEIDIB.h: pRGB entries are memory-layout B,G,R,ignored. On little
    // endian this is the byte-swapped form of RGBVAL.
    return b | (g << 8) | (r << 16);
}

uint32_t palette_entry_to_rgbval(uint32_t entry) {
    const uint32_t b = entry & 0xffu;
    const uint32_t g = (entry >> 8) & 0xffu;
    const uint32_t r = (entry >> 16) & 0xffu;
    return make_rgbval(r, g, b);
}

uint32_t expand_bits(uint32_t value, int bits) {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 8) {
        return value & 0xffu;
    }
    const uint32_t max_value = (1u << bits) - 1u;
    return (value * 255u + (max_value / 2u)) / max_value;
}

uint32_t pack_bits(uint32_t value, int bits) {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 8) {
        return value & 0xffu;
    }
    const uint32_t max_value = (1u << bits) - 1u;
    return (value * max_value + 127u) / 255u;
}

bool color_scheme_bits(uint32_t scheme, int& r_bits, int& g_bits, int& b_bits) {
    switch (scheme) {
    case 8:  r_bits = 3; g_bits = 3; b_bits = 2; return true;  // IDIB_COLORSCHEME_332
    case 12: r_bits = 4; g_bits = 4; b_bits = 4; return true;  // IDIB_COLORSCHEME_444
    case 15: r_bits = 5; g_bits = 5; b_bits = 5; return true;  // IDIB_COLORSCHEME_555
    case 16: r_bits = 5; g_bits = 6; b_bits = 5; return true;  // IDIB_COLORSCHEME_565
    case 18: r_bits = 6; g_bits = 6; b_bits = 6; return true;  // IDIB_COLORSCHEME_666
    case 24: r_bits = 8; g_bits = 8; b_bits = 8; return true;  // IDIB_COLORSCHEME_888
    default: return false;
    }
}

uint32_t rgbval_to_scheme_native(uint32_t rgb, uint32_t scheme) {
    int r_bits = 0, g_bits = 0, b_bits = 0;
    if (!color_scheme_bits(scheme, r_bits, g_bits, b_bits)) {
        return rgb;
    }
    const uint32_t r = (rgb >> 8) & 0xffu;
    const uint32_t g = (rgb >> 16) & 0xffu;
    const uint32_t b = (rgb >> 24) & 0xffu;
    return (pack_bits(r, r_bits) << (g_bits + b_bits)) |
           (pack_bits(g, g_bits) << b_bits) |
           pack_bits(b, b_bits);
}

uint32_t scheme_native_to_rgbval(uint32_t native, uint32_t scheme) {
    int r_bits = 0, g_bits = 0, b_bits = 0;
    if (!color_scheme_bits(scheme, r_bits, g_bits, b_bits)) {
        return native;
    }
    const uint32_t b_mask = (1u << b_bits) - 1u;
    const uint32_t g_mask = (1u << g_bits) - 1u;
    const uint32_t b = native & b_mask;
    const uint32_t g = (native >> b_bits) & g_mask;
    const uint32_t r = native >> (g_bits + b_bits);
    return make_rgbval(expand_bits(r, r_bits), expand_bits(g, g_bits), expand_bits(b, b_bits));
}
}

bool BrewBitmap::handle_color_hook(const std::string& name, CPU& cpu) {
    if (name == "IBitmap_RGBToNative") {
        const uint32_t rgb = cpu.get_reg(REG_R1);
        uint32_t native = rgb;
        const uint32_t pRGB = memory_.read_value(object_ptr_ + 12);
        const uint32_t cntRGB = memory_.read_value(object_ptr_ + 26, EndianMemory::Halfword);
        const uint32_t color_scheme = memory_.read_value(object_ptr_ + 29, EndianMemory::Byte);
        if (rgb == kRgbNone) {
            native = 0;
        } else if (cntRGB != 0 && pRGB != 0 && pRGB < 0xFF000000) {
            const uint32_t wanted = rgbval_to_palette_entry(rgb);
            uint32_t best_index = 0;
            uint32_t best_distance = UINT32_MAX;
            for (uint32_t i = 0; i < cntRGB; ++i) {
                const uint32_t entry = memory_.read_value(pRGB + i * 4u);
                if ((entry & 0x00ffffffu) == wanted) {
                    best_index = i;
                    break;
                }
                const uint32_t entry_rgb = palette_entry_to_rgbval(entry);
                const int dr = static_cast<int>((entry_rgb >> 8) & 0xffu) - static_cast<int>((rgb >> 8) & 0xffu);
                const int dg = static_cast<int>((entry_rgb >> 16) & 0xffu) - static_cast<int>((rgb >> 16) & 0xffu);
                const int db = static_cast<int>((entry_rgb >> 24) & 0xffu) - static_cast<int>((rgb >> 24) & 0xffu);
                const uint32_t distance = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_index = i;
                }
            }
            native = best_index;
        } else if (color_scheme != 0) {
            native = rgbval_to_scheme_native(rgb, color_scheme);
        } else if (depth_ == 16) {
            native = rgbval_to_native16(rgb);
        }
        cpu.set_reg(REG_R0, native);
        return true;
    }

    if (name == "IBitmap_NativeToRGB") {
        const uint32_t native = cpu.get_reg(REG_R1);
        uint32_t rgb = native;
        const uint32_t pRGB = memory_.read_value(object_ptr_ + 12);
        const uint32_t cntRGB = memory_.read_value(object_ptr_ + 26, EndianMemory::Halfword);
        const uint32_t color_scheme = memory_.read_value(object_ptr_ + 29, EndianMemory::Byte);
        if (cntRGB != 0 && pRGB != 0 && pRGB < 0xFF000000 && native < cntRGB) {
            rgb = palette_entry_to_rgbval(memory_.read_value(pRGB + native * 4u));
        } else if (color_scheme != 0) {
            rgb = scheme_native_to_rgbval(native, color_scheme);
        } else if (depth_ == 16) {
            rgb = native16_to_rgbval(native);
        }
        cpu.set_reg(REG_R0, rgb);
        return true;
    }

    return false;
}
