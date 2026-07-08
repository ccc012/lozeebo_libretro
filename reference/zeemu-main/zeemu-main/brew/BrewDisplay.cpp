#include "brew/BrewDisplay.h"
#include "graphics/RenderBackend.h"
#include "brew/BrewFont.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/VirtualMemory.h"
#include "brew/BrewBitmap.h"
#include <algorithm>
#include <iostream>
#include <cctype>
#include <cstdlib>

namespace {
constexpr uint32_t IDF_TEXT_FORMAT_OEM = 0x00010000;
constexpr uint32_t RGB_NONE = 0xffffffffu;
constexpr uint32_t AEE_RO_COPY = 2u;
constexpr uint32_t AEE_RO_TRANSPARENT = 7u;
constexpr uint32_t IDF_RECT_FRAME = 0x1u;
constexpr uint32_t IDF_RECT_FILL = 0x2u;
constexpr uint32_t SUCCESS = 0u;
constexpr uint32_t EFAILED = 1u;
constexpr uint32_t EUNSUPPORTED = 8u;
constexpr uint32_t AEE_FONT_NORMAL = 0x8000u;
constexpr uint32_t AEE_NUM_FONTS = 8u;

uint32_t display_arg_stack_base(EndianMemory& memory, CPU& cpu) {
    uint32_t sp = cpu.get_reg(REG_SP);
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    if (sp == 0 || sp >= 0xFF000000) {
        return sp;
    }
    for (int depth = 0; depth < 4; ++depth) {
        if (memory.read_value(sp + 0) != r0 ||
            memory.read_value(sp + 4) != r1 ||
            memory.read_value(sp + 8) != r2) {
            break;
        }
        sp += 20;
    }
    if (memory.read_value(sp + 12) == r0) {
        sp += 20;
    }
    return sp;
}

uint32_t default_font_clsid_for_designator(uint32_t nFont) {
    switch (nFont) {
    case AEE_FONT_NORMAL + 0: return 0x01012786u; // AEECLSID_FONTSYSNORMAL
    case AEE_FONT_NORMAL + 1: return 0x01012788u; // AEECLSID_FONTSYSBOLD
    case AEE_FONT_NORMAL + 2: return 0x01012787u; // AEECLSID_FONTSYSLARGE
    case AEE_FONT_NORMAL + 3: return 0x0101402cu; // AEECLSID_FONTSYSITALIC
    case AEE_FONT_NORMAL + 4: return 0x0101402du; // AEECLSID_FONTSYSBOLDITALIC
    case AEE_FONT_NORMAL + 5: return 0x0101402eu; // AEECLSID_FONTSYSLARGEITALIC
    case AEE_FONT_NORMAL + 6:
    case AEE_FONT_NORMAL + 7:
        return 0x01012786u;
    default:
        return 0;
    }
}

std::string read_guest_byte_text(EndianMemory& memory, uint32_t pch, int nChars) {
    std::string text;
    if (!pch || pch >= 0xFF000000) {
        return text;
    }
    int limit = nChars;
    if (limit < 0 || limit > 4096) {
        limit = 4096;
    }
    for (int i = 0; i < limit; ++i) {
        uint8_t c = (uint8_t)memory.read_value(pch + (uint32_t)i, EndianMemory::Byte);
        if (c == 0) {
            break;
        }
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
            text.push_back((char)c);
        } else {
            text.push_back('?');
        }
    }
    return text;
}

std::string read_guest_aechar_text(EndianMemory& memory, uint32_t pch, int nChars) {
    std::string text;
    if (!pch || pch >= 0xFF000000) {
        return text;
    }
    int limit = nChars;
    if (limit < 0 || limit > 4096) {
        limit = 4096;
    }
    for (int i = 0; i < limit; ++i) {
        uint16_t c = (uint16_t)memory.read_value(pch + (uint32_t)i * 2u, EndianMemory::Halfword);
        if (c == 0) {
            break;
        }
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
            text.push_back((char)c);
        } else {
            text.push_back('?');
        }
    }
    return text;
}

int printable_score(const std::string& text) {
    int score = 0;
    for (unsigned char c : text) {
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
            ++score;
        }
    }
    return score;
}

bool looks_like_guest_ascii_text(EndianMemory& memory, uint32_t pch) {
    if (pch == 0 || pch >= 0xFF000000u) return false;
    int printable = 0;
    int total = 0;
    for (int i = 0; i < 16; ++i) {
        uint8_t c = (uint8_t)memory.read_value(pch + (uint32_t)i, EndianMemory::Byte);
        if (c == 0) break;
        ++total;
        if ((c >= 0x20 && c <= 0x7eu) || c == '\n' || c == '\r' || c == '\t') {
            ++printable;
        }
    }
    return total >= 2 && printable == total;
}

uint32_t resolve_display_text_descriptor(EndianMemory& memory, uint32_t pch, bool& force_byte_text) {
    force_byte_text = false;
    if (pch == 0 || pch >= 0xFF000000u) return pch;
    uint32_t object = memory.read_value(pch);
    if (object == 0 || object >= 0xFF000000u) return pch;
    uint32_t text = memory.read_value(object + 8u);
    if (looks_like_guest_ascii_text(memory, text)) {
        force_byte_text = true;
        return text;
    }
    return pch;
}

void trace_text_bytes(EndianMemory& memory, const char* label, uint32_t pch, int nChars) {
    if (!std::getenv("ZEEMU_TRACE_TEXT_BYTES") || !pch || pch >= 0xFF000000) {
        return;
    }
    int limit = nChars;
    if (limit < 0 || limit > 16) {
        limit = 16;
    }
    printf("  [%s bytes] ptr=0x%08x n=%d raw=", label, pch, nChars);
    for (int i = 0; i < limit * 2; ++i) {
        printf("%02x", (uint8_t)memory.read_value(pch + (uint32_t)i, EndianMemory::Byte));
        if ((i & 1) == 1) {
            printf(" ");
        }
    }
    printf("aechar=");
    for (int i = 0; i < limit; ++i) {
        printf("%04x", (uint16_t)memory.read_value(pch + (uint32_t)i * 2u, EndianMemory::Halfword));
        if (i + 1 < limit) {
            printf(" ");
        }
    }
    printf("\n");
}

bool valid_out_ptr(uint32_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

int16_t read_i16(EndianMemory& memory, uint32_t ptr) {
    return (int16_t)memory.read_value(ptr, EndianMemory::Halfword);
}

bool supported_dib_depth(int depth) {
    return depth == 1 || depth == 2 || depth == 4 || depth == 8 ||
           depth == 16 || depth == 24 || depth == 32;
}

struct DibView {
    addr_t bits = 0;
    addr_t palette = 0;
    uint32_t transparent = 0xffffffffu;
    int width = 0;
    int height = 0;
    int pitch = 0;
    int palette_entries = 0;
    int depth = 0;
    int color_scheme = 0;
};

bool plausible_dib(const DibView& dib) {
    return dib.bits != 0 && dib.bits < 0xFF000000u &&
           dib.width >= 0 && dib.width <= 4096 &&
           dib.height >= 0 && dib.height <= 4096 &&
           dib.pitch != 0 && dib.pitch > -65536 && dib.pitch < 65536 &&
           supported_dib_depth(dib.depth);
}

DibView read_idib(EndianMemory& memory, addr_t ptr) {
    DibView dib;
    if (!valid_out_ptr(ptr)) {
        return dib;
    }
    dib.bits = memory.read_value(ptr + 8);
    dib.palette = memory.read_value(ptr + 12);
    dib.transparent = memory.read_value(ptr + 16);
    dib.width = (int)memory.read_value(ptr + 20, EndianMemory::Halfword);
    dib.height = (int)memory.read_value(ptr + 22, EndianMemory::Halfword);
    dib.pitch = read_i16(memory, ptr + 24);
    dib.palette_entries = (int)memory.read_value(ptr + 26, EndianMemory::Halfword);
    dib.depth = (int)memory.read_value(ptr + 28, EndianMemory::Byte);
    dib.color_scheme = (int)memory.read_value(ptr + 29, EndianMemory::Byte);
    return plausible_dib(dib) ? dib : DibView{};
}

DibView read_legacy_aeedib(EndianMemory& memory, addr_t ptr) {
    DibView dib;
    if (!valid_out_ptr(ptr)) {
        return dib;
    }
    dib.bits = memory.read_value(ptr + 4);
    dib.palette = memory.read_value(ptr + 8);
    dib.transparent = memory.read_value(ptr + 12);
    dib.width = (int)memory.read_value(ptr + 16, EndianMemory::Halfword);
    dib.height = (int)memory.read_value(ptr + 18, EndianMemory::Halfword);
    dib.pitch = read_i16(memory, ptr + 20);
    dib.palette_entries = (int)memory.read_value(ptr + 22, EndianMemory::Halfword);
    dib.depth = (int)memory.read_value(ptr + 24, EndianMemory::Byte);
    dib.color_scheme = (dib.depth == 16) ? 16 : 0;
    return plausible_dib(dib) ? dib : DibView{};
}

uint32_t dib_palette_entry_to_rgbval(uint32_t entry) {
    const uint32_t b = entry & 0xffu;
    const uint32_t g = (entry >> 8) & 0xffu;
    const uint32_t r = (entry >> 16) & 0xffu;
    return (r << 8) | (g << 16) | (b << 24);
}

uint32_t dib_native_to_rgbval(EndianMemory& memory, const DibView& dib, uint32_t native) {
    if (dib.palette_entries > 0 && dib.palette != 0 && dib.palette < 0xFF000000u &&
        native < (uint32_t)dib.palette_entries) {
        return dib_palette_entry_to_rgbval(memory.read_value(dib.palette + native * 4u));
    }
    if (dib.depth == 16 || dib.color_scheme == 16) {
        const uint32_t r5 = (native >> 11) & 0x1fu;
        const uint32_t g6 = (native >> 5) & 0x3fu;
        const uint32_t b5 = native & 0x1fu;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g6 << 2) | (g6 >> 4);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        return (r << 8) | (g << 16) | (b << 24);
    }
    return native;
}

uint32_t read_dib_pixel(EndianMemory& memory, const DibView& dib, int x, int y) {
    const addr_t row = dib.bits + (addr_t)(y * dib.pitch);
    switch (dib.depth) {
    case 1: {
        const uint8_t byte = (uint8_t)memory.read_value(row + (uint32_t)(x / 8), EndianMemory::Byte);
        return (byte >> (7 - (x & 7))) & 0x1u;
    }
    case 2: {
        const uint8_t byte = (uint8_t)memory.read_value(row + (uint32_t)(x / 4), EndianMemory::Byte);
        return (byte >> ((3 - (x & 3)) * 2)) & 0x3u;
    }
    case 4: {
        const uint8_t byte = (uint8_t)memory.read_value(row + (uint32_t)(x / 2), EndianMemory::Byte);
        return (x & 1) ? (byte & 0x0fu) : ((byte >> 4) & 0x0fu);
    }
    case 8:
        return memory.read_value(row + (uint32_t)x, EndianMemory::Byte);
    case 16:
        return memory.read_value(row + (uint32_t)(x * 2), EndianMemory::Halfword);
    case 24: {
        const addr_t p = row + (uint32_t)(x * 3);
        const uint32_t b = memory.read_value(p + 0, EndianMemory::Byte);
        const uint32_t g = memory.read_value(p + 1, EndianMemory::Byte);
        const uint32_t r = memory.read_value(p + 2, EndianMemory::Byte);
        return (r << 8) | (g << 16) | (b << 24);
    }
    case 32:
        return memory.read_value(row + (uint32_t)(x * 4));
    default:
        return 0;
    }
}

uint32_t count_nonzero_16(EndianMemory& memory, const BrewBitmap* bitmap, uint32_t limit = 4096) {
    if (!bitmap || bitmap->get_depth() != 16) {
        return 0;
    }
    const uint32_t width = static_cast<uint32_t>(std::max(bitmap->get_width(), 0));
    const uint32_t height = static_cast<uint32_t>(std::max(bitmap->get_height(), 0));
    const uint32_t total = std::min<uint32_t>(width * height, limit);
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const uint32_t x = width == 0 ? 0 : i % width;
        const uint32_t y = width == 0 ? 0 : i / width;
        const addr_t pixel = bitmap->get_buffer_ptr() +
                             static_cast<addr_t>(y) * static_cast<addr_t>(bitmap->get_pitch()) +
                             static_cast<addr_t>(x) * 2u;
        if (memory.read_value(pixel, EndianMemory::Halfword) != 0) {
            ++count;
        }
    }
    return count;
}

uint32_t count_nonzero_dib(EndianMemory& memory, const DibView& dib, uint32_t limit = 4096) {
    if (dib.bits == 0 || dib.width <= 0 || dib.height <= 0) {
        return 0;
    }
    const uint32_t total = std::min<uint32_t>(
        static_cast<uint32_t>(dib.width) * static_cast<uint32_t>(dib.height), limit);
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const int x = static_cast<int>(i % static_cast<uint32_t>(dib.width));
        const int y = static_cast<int>(i / static_cast<uint32_t>(dib.width));
        if (read_dib_pixel(memory, dib, x, y) != 0) {
            ++count;
        }
    }
    return count;
}

struct GuestBitmapProbe {
    addr_t bits = 0;
    int width = 0;
    int height = 0;
    int pitch = 0;
    int type = 0;
};

GuestBitmapProbe probe_bjt_guest_bitmap(EndianMemory& memory, addr_t object_ptr) {
    GuestBitmapProbe probe;
    if (!valid_out_ptr(object_ptr)) {
        return probe;
    }
    const addr_t state = memory.read_value(object_ptr + 0x44);
    if (!valid_out_ptr(state)) {
        return probe;
    }
    const addr_t surface = memory.read_value(state + 0x58);
    if (!valid_out_ptr(surface)) {
        return probe;
    }
    probe.width = static_cast<int>(memory.read_value(state + 0x64));
    probe.height = static_cast<int>(memory.read_value(state + 0x68));
    probe.type = static_cast<int>(static_cast<int8_t>(memory.read_value(surface + 0x08, EndianMemory::Byte)));
    probe.pitch = static_cast<int>(memory.read_value(surface + 0x0c));
    probe.bits = memory.read_value(surface + 0x24);
    if (!valid_out_ptr(probe.bits) || probe.width <= 0 || probe.width > 4096 ||
        probe.height <= 0 || probe.height > 4096 || probe.pitch <= 0 || probe.pitch > 65536) {
        return GuestBitmapProbe{};
    }
    return probe;
}

uint16_t read_guest_probe_pixel_565(EndianMemory& memory, const GuestBitmapProbe& probe, int x, int y) {
    const addr_t row = probe.bits + static_cast<addr_t>(y) * static_cast<addr_t>(probe.pitch);
    if (probe.type == 3) {
        return static_cast<uint16_t>(memory.read_value(row + static_cast<addr_t>(x) * 2u,
                                                       EndianMemory::Halfword));
    }
    if (probe.type == 4) {
        const uint32_t rgb666 = memory.read_value(row + static_cast<addr_t>(x) * 4u);
        const uint32_t r6 = (rgb666 >> 12) & 0x3fu;
        const uint32_t g6 = (rgb666 >> 6) & 0x3fu;
        const uint32_t b6 = rgb666 & 0x3fu;
        return static_cast<uint16_t>(((r6 >> 1) << 11) | (g6 << 5) | (b6 >> 1));
    }
    return 0;
}

uint32_t count_nonzero_guest_probe(EndianMemory& memory, const GuestBitmapProbe& probe, uint32_t limit = 4096) {
    if (!valid_out_ptr(probe.bits) || probe.width <= 0 || probe.height <= 0) {
        return 0;
    }
    const uint32_t total = std::min<uint32_t>(
        static_cast<uint32_t>(probe.width) * static_cast<uint32_t>(probe.height), limit);
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const int x = static_cast<int>(i % static_cast<uint32_t>(probe.width));
        const int y = static_cast<int>(i / static_cast<uint32_t>(probe.width));
        if (read_guest_probe_pixel_565(memory, probe, x, y) != 0) {
            ++count;
        }
    }
    return count;
}

}

uint16_t BrewDisplay::rgb_to_native(uint32_t rgb) const {
    if (rgb == RGB_NONE) {
        return 0;
    }
    if (rgb <= 0xFFFF) {
        return (uint16_t)rgb;
    }
    // BREW RGBVAL is MAKE_RGB(r,g,b) = r<<8 | g<<16 | b<<24.
    uint32_t r = (rgb >> 8) & 0xff;
    uint32_t g = (rgb >> 16) & 0xff;
    uint32_t b = (rgb >> 24) & 0xff;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void BrewDisplay::draw_text_to_device_bitmap(int x, int y, const std::string& text) {
    draw_text_to_device_bitmap_rgb(x, y, text, user_text_color_);
}

void BrewDisplay::fill_rect_device(int x, int y, int dx, int dy, uint32_t rgb) {
    BrewBitmap* bitmap = get_destination_bitmap();
    if (!bitmap || bitmap->get_depth() != 16 || dx <= 0 || dy <= 0) {
        return;
    }
    const int width = bitmap->get_width();
    const int height = bitmap->get_height();
    const int pitch = bitmap->get_pitch();
    const uint16_t color = rgb_to_native(rgb);
    addr_t base = bitmap->get_buffer_ptr();
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(width, x + dx);
    int y1 = std::min(height, y + dy);
    for (int py = y0; py < y1; ++py) {
        addr_t row = base + static_cast<addr_t>(py) * static_cast<addr_t>(pitch);
        for (int px = x0; px < x1; ++px) {
            memory_.write_value(row + static_cast<addr_t>(px) * 2u, color, EndianMemory::Halfword);
        }
    }
}

void BrewDisplay::draw_text_to_device_bitmap_rgb(int x, int y, const std::string& text, uint32_t rgb) {
    BrewBitmap* bitmap = get_destination_bitmap();
    if (!bitmap || bitmap->get_depth() != 16 || text.empty()) {
        return;
    }

    const int width = bitmap->get_width();
    const int height = bitmap->get_height();
    const int pitch = bitmap->get_pitch();
    const uint16_t color = rgb_to_native(rgb);
    addr_t base = bitmap->get_buffer_ptr();

    auto draw_pixel = [&](int px, int py) {
        if (px < 0 || py < 0 || px >= width || py >= height) {
            return;
        }
        memory_.write_value(base + static_cast<addr_t>(py) * static_cast<addr_t>(pitch) +
                                static_cast<addr_t>(px) * 2u,
                            color, EndianMemory::Halfword);
    };

    auto draw_cell = [&](int px, int py, unsigned char ch) {
        if (ch == ' ') {
            return;
        }
        ch = (unsigned char)std::toupper(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                bool on = false;
                if (ch >= '0' && ch <= '9') {
                    const bool top = row == 0;
                    const bool mid = row == 3;
                    const bool bottom = row == 6;
                    const bool left = col == 0;
                    const bool right = col == 4;
                    switch (ch) {
                    case '0': on = top || bottom || left || right; break;
                    case '1': on = right || (row == 1 && col == 3) || (bottom && col > 0); break;
                    case '2': on = top || mid || bottom || (right && row < 3) || (left && row > 3); break;
                    case '3': on = top || mid || bottom || right; break;
                    case '4': on = mid || right || (left && row < 3); break;
                    case '5': on = top || mid || bottom || (left && row < 3) || (right && row > 3); break;
                    case '6': on = top || mid || bottom || left || (right && row > 3); break;
                    case '7': on = top || right; break;
                    case '8': on = top || mid || bottom || left || right; break;
                    case '9': on = top || mid || bottom || right || (left && row < 3); break;
                    }
                } else if (ch >= 'A' && ch <= 'Z') {
                    const bool top = row == 0;
                    const bool mid = row == 3;
                    const bool bottom = row == 6;
                    const bool left = col == 0;
                    const bool right = col == 4;
                    switch (ch) {
                    case 'A': on = top || mid || left || right; break;
                    case 'B': on = top || mid || bottom || left || right; break;
                    case 'C': on = top || bottom || left; break;
                    case 'D': on = top || bottom || left || right; break;
                    case 'E': on = top || mid || bottom || left; break;
                    case 'F': on = top || mid || left; break;
                    case 'G': on = top || bottom || left || (right && row >= 3) || (mid && col >= 2); break;
                    case 'H': on = mid || left || right; break;
                    case 'I': on = top || bottom || col == 2; break;
                    case 'J': on = top || right || (bottom && col < 4) || (left && row > 4); break;
                    case 'K': on = left || (col == 4 - row && row < 4) || (col == row - 2 && row >= 3); break;
                    case 'L': on = left || bottom; break;
                    case 'M': on = left || right || (row < 3 && (col == row || col == 4 - row)); break;
                    case 'N': on = left || right || col == row - 1; break;
                    case 'O': on = top || bottom || left || right; break;
                    case 'P': on = top || mid || left || (right && row < 3); break;
                    case 'Q': on = top || bottom || left || right || (row >= 4 && col == row - 2); break;
                    case 'R': on = top || mid || left || (right && row < 3) || (row >= 3 && col == row - 2); break;
                    case 'S': on = top || mid || bottom || (left && row < 3) || (right && row > 3); break;
                    case 'T': on = top || col == 2; break;
                    case 'U': on = left || right || bottom; break;
                    case 'V': on = (row < 5 && (left || right)) || (row == 5 && (col == 1 || col == 3)) || (row == 6 && col == 2); break;
                    case 'W': on = left || right || (row > 3 && (col == 1 || col == 3)); break;
                    case 'X': on = col == row - 1 || col == 5 - row; break;
                    case 'Y': on = (row < 3 && (col == row || col == 4 - row)) || (row >= 3 && col == 2); break;
                    case 'Z': on = top || bottom || col == 6 - row; break;
                    }
                } else {
                    on = row == 0 || row == 6 || col == 0 || col == 4;
                }
                if (on) {
                    draw_pixel(px + col, py + row);
                }
            }
        }
    };

    int cursor_x = x;
    for (unsigned char ch : text) {
        if (ch == '\n') {
            cursor_x = x;
            y += 9;
            continue;
        }
        draw_cell(cursor_x, y, ch);
        cursor_x += 6;
    }
}

BrewDisplay::BrewDisplay(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    device_bitmap_ = std::make_unique<BrewBitmap>(shell, memory,
                                                  shell.get_display_width(),
                                                  shell.get_display_height(),
                                                  16);
    destination_ptr_ = device_bitmap_->get_object_ptr();
    clip_dx_ = static_cast<int16_t>(device_bitmap_->get_width());
    clip_dy_ = static_cast<int16_t>(device_bitmap_->get_height());
    setup_vtable();
}

BrewBitmap* BrewDisplay::get_destination_bitmap() {
    if (destination_ptr_ != 0) {
        if (BrewBitmap* bitmap = lookup_brew_bitmap(destination_ptr_)) {
            return bitmap;
        }
    }
    return device_bitmap_.get();
}

void BrewDisplay::setup_vtable() {
    vtable_ptr_ = shell_.malloc(48 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    auto add_method = [&](int index, const std::string& name) {
        addr_t hook_addr = shell_.add_hook("IDisplay_" + name, this);
        memory_.write_value(vtable_ptr_ + (index * 4), hook_addr);
    };

    add_method(0, "AddRef");
    add_method(1, "Release");
    add_method(2, "GetFontMetrics");
    add_method(3, "MeasureTextEx");
    add_method(4, "DrawText");
    add_method(5, "DrawRect");
    add_method(6, "BitBlt");
    add_method(7, "Update");
    add_method(8, "SetAnnunciators");
    add_method(9, "Backlight");
    add_method(10, "SetColor");
    add_method(11, "GetSymbol");
    add_method(12, "DrawFrame");
    add_method(13, "CreateDIBitmap");
    add_method(14, "SetDestination");
    add_method(15, "GetDestination");
    add_method(16, "GetDeviceBitmap");
    add_method(17, "SetFont");
    add_method(18, "SetClipRect");
    add_method(19, "GetClipRect");
    add_method(20, "Clone");
    add_method(21, "MakeDefault");
    add_method(22, "IsEnabled");
    add_method(23, "NotifyEnable");
    add_method(24, "CreateDIBitmapEx");
    add_method(25, "SetPrefs");
    for (int index = 26; index < 48; ++index) {
        add_method(index, "Fn" + std::to_string(index));
    }
}

void BrewDisplay::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    // uint32_t r1 = cpu.get_reg(REG_R1);
    // uint32_t r2 = cpu.get_reg(REG_R2);

    if (name == "IDisplay_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IDisplay_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_GetDeviceBitmap") {
        uint32_t ppBmp = cpu.get_reg(REG_R1);
        if (ppBmp != 0 && ppBmp < 0xFF000000) {
            uint32_t bmp_obj = device_bitmap_->get_object_ptr();
            memory_.write_value(ppBmp, bmp_obj);
        }
        cpu.set_reg(REG_R0, 0); // SUCCESS
    } else if (name == "IDisplay_SetDestination") {
        const addr_t old_destination = destination_ptr_;
        const addr_t requested_destination = cpu.get_reg(REG_R1);
        uint32_t result = SUCCESS;
        if (requested_destination == 0) {
            destination_ptr_ = device_bitmap_->get_object_ptr();
        } else if (BrewBitmap* bitmap = lookup_brew_bitmap(requested_destination)) {
            destination_ptr_ = bitmap->get_object_ptr();
            if (destination_ptr_ != device_bitmap_->get_object_ptr()) {
                last_offscreen_destination_ptr_ = destination_ptr_;
            }
        } else if (valid_out_ptr(requested_destination)) {
            // BREW accepts app-provided IBitmap implementations, not only DIBs
            // created by IDisplay. Keep the raw object as the active
            // destination so image drawing can call the guest IBitmap methods
            // (notably BltIn) instead of silently drawing to the device DIB.
            destination_ptr_ = requested_destination;
            last_offscreen_destination_ptr_ = destination_ptr_;
            result = SUCCESS;
        } else {
            result = EFAILED;
        }
        static int set_destination_logs = 0;
        if (set_destination_logs < 8 || std::getenv("ZEEMU_TRACE_HLE") != nullptr) {
            printf("  IDisplay_SetDestination: old=0x%08x requested=0x%08x new=0x%08x result=%u\n",
                   old_destination, requested_destination, destination_ptr_, result);
            ++set_destination_logs;
        }
        cpu.set_reg(REG_R0, result);
    } else if (name == "IDisplay_GetDestination") {
        cpu.set_reg(REG_R0, destination_ptr_ ? destination_ptr_ : device_bitmap_->get_object_ptr());
    } else if (name == "IDisplay_GetColorCount") {
        cpu.set_reg(REG_R0, 65536);
    } else if (name == "IDisplay_IsColor") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IDisplay_IsEnabled") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IDisplay_Update") {
        const bool destination_is_device = destination_ptr_ == device_bitmap_->get_object_ptr();
        GuestBitmapProbe guest_offscreen_probe;
        uint32_t guest_offscreen_nonzero = 0;
        if (destination_is_device && last_offscreen_destination_ptr_ != 0 &&
            lookup_brew_bitmap(last_offscreen_destination_ptr_) == nullptr) {
            // BJT implements its own IBitmap wrapper. Trace-only probe: the
            // last raw destination can be a sprite-sized surface, not the
            // final framebuffer, so do not present it blindly.
            guest_offscreen_probe = probe_bjt_guest_bitmap(memory_, last_offscreen_destination_ptr_);
            guest_offscreen_nonzero = count_nonzero_guest_probe(memory_, guest_offscreen_probe);
        }
        if (std::getenv("ZEEMU_TRACE_BITMAP")) {
            const BrewBitmap* offscreen = lookup_brew_bitmap(last_offscreen_destination_ptr_);
            printf("  IDisplay_Update: destination=0x%08x device=0x%08x device_nonzero=%u offscreen=0x%08x offscreen_nonzero=%u guest_bits=0x%08x guest=%dx%d pitch=%d type=%d guest_nonzero=%u\n",
                   destination_ptr_,
                   device_bitmap_->get_object_ptr(),
                   count_nonzero_16(memory_, device_bitmap_.get()),
                   last_offscreen_destination_ptr_,
                   count_nonzero_16(memory_, offscreen),
                   guest_offscreen_probe.bits,
                   guest_offscreen_probe.width,
                   guest_offscreen_probe.height,
                   guest_offscreen_probe.pitch,
                   guest_offscreen_probe.type,
                   guest_offscreen_nonzero);
        }
        if (destination_is_device && shell_.get_presenter()) {
            auto* bitmap = device_bitmap_.get();
            if (bitmap->get_depth() == 16) {
                if (auto* vm = shell_.get_virtual_memory()) {
                    void* host_ptr = vm->get_host_address(bitmap->get_buffer_ptr());
                    if (host_ptr) {
                        shell_.get_presenter()->begin_frame();
                        shell_.get_presenter()->present_rgb565(host_ptr, bitmap->get_pitch(),
                                                               bitmap->get_width(), bitmap->get_height());
                        shell_.get_presenter()->end_frame();
                    }
                }
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_Backlight") {
        // Hardware display backlight control has no host-side state in Zeemu.
        // BREW callers use it as a best-effort request, so report success.
        cpu.set_reg(REG_R0, SUCCESS);
    } else if (name == "IDisplay_CreateDIBitmap") {
        const uint32_t ppIDIB = cpu.get_reg(REG_R1);
        const int depth = (int)(cpu.get_reg(REG_R2) & 0xffu);
        const int width = (int)(cpu.get_reg(REG_R3) & 0xffffu);
        const uint32_t stack_base = display_arg_stack_base(memory_, cpu);
        const int height = (int)(memory_.read_value(stack_base) & 0xffffu);

        if (!valid_out_ptr(ppIDIB)) {
            cpu.set_reg(REG_R0, EFAILED);
        } else if (!supported_dib_depth(depth)) {
            memory_.write_value(ppIDIB, 0u);
            cpu.set_reg(REG_R0, EUNSUPPORTED);
        } else {
            // SDK-created DIBs start with no palette, no fixed color scheme,
            // and transparent color 0. The guest may patch public IDIB fields.
            auto dib = std::make_unique<BrewBitmap>(shell_, memory_, width, height, depth, 0, 0, 0, 0);
            const uint32_t object = dib->get_object_ptr();
            memory_.write_value(ppIDIB, object);
            printf("  IDisplay_CreateDIBitmap: pp=0x%08x depth=%d size=%dx%d pitch=%d -> 0x%08x\n",
                   ppIDIB, depth, width, height, dib->get_pitch(), object);
            dib_bitmaps_.push_back(std::move(dib));
            cpu.set_reg(REG_R0, SUCCESS);
        }
    } else if (name == "IDisplay_CreateDIBitmapEx") {
        const uint32_t ppIDIB = cpu.get_reg(REG_R1);
        const int depth = (int)cpu.get_reg(REG_R2);
        const int height = (int)cpu.get_reg(REG_R3);
        const uint32_t stack_base = display_arg_stack_base(memory_, cpu);
        const int width = (int)memory_.read_value(stack_base + 0);
        const int palette_entries = (int)memory_.read_value(stack_base + 4);
        const int extra_bytes = (int)memory_.read_value(stack_base + 8);

        if (!valid_out_ptr(ppIDIB)) {
            cpu.set_reg(REG_R0, EFAILED);
        } else if (!supported_dib_depth(depth) || width < 0 || height < 0 ||
                   palette_entries < 0 || extra_bytes < 0) {
            memory_.write_value(ppIDIB, 0u);
            cpu.set_reg(REG_R0, EUNSUPPORTED);
        } else {
            auto dib = std::make_unique<BrewBitmap>(shell_, memory_, width, height, depth,
                                                    palette_entries, extra_bytes, 0, 0);
            const uint32_t object = dib->get_object_ptr();
            memory_.write_value(ppIDIB, object);
            printf("  IDisplay_CreateDIBitmapEx: pp=0x%08x depth=%d size=%dx%d palette=%d extra=%d pitch=%d -> 0x%08x\n",
                   ppIDIB, depth, width, height, palette_entries, extra_bytes, dib->get_pitch(), object);
            dib_bitmaps_.push_back(std::move(dib));
            cpu.set_reg(REG_R0, SUCCESS);
        }
    } else if (name == "IDisplay_ClearScreen") {
        cpu.set_reg(REG_R1, 0); // pRect = NULL
        cpu.set_reg(REG_R2, rgb_to_native(user_background_color_));
        cpu.set_reg(REG_R3, AEE_RO_COPY);
        get_destination_bitmap()->handle_hook("IBitmap_FillRect", cpu);
    } else if (name == "IDisplay_SetColor") {
        uint32_t index = cpu.get_reg(REG_R1);
        uint32_t color = cpu.get_reg(REG_R2);
        uint32_t previous = 0;
        uint32_t* slot = nullptr;
        if (index == 1) {
            slot = &user_text_color_;
        } else if (index == 2) {
            slot = &user_background_color_;
        } else if (index == 3) {
            slot = &user_line_color_;
        }
        if (slot) {
            previous = *slot;
            if (color != 0xffffffffu) {
                *slot = color;
            }
        }
        cpu.set_reg(REG_R0, previous);
    } else if (name == "IDisplay_SetClipRect") {
        const uint32_t pRect = cpu.get_reg(REG_R1);
        if (pRect != 0 && pRect < 0xFF000000) {
            clip_x_ = static_cast<int16_t>(memory_.read_value(pRect + 0, EndianMemory::Halfword));
            clip_y_ = static_cast<int16_t>(memory_.read_value(pRect + 2, EndianMemory::Halfword));
            clip_dx_ = static_cast<int16_t>(memory_.read_value(pRect + 4, EndianMemory::Halfword));
            clip_dy_ = static_cast<int16_t>(memory_.read_value(pRect + 6, EndianMemory::Halfword));
        } else {
            clip_x_ = 0;
            clip_y_ = 0;
            BrewBitmap* bitmap = get_destination_bitmap();
            clip_dx_ = (int16_t)(bitmap ? bitmap->get_width() : shell_.get_display_width());
            clip_dy_ = (int16_t)(bitmap ? bitmap->get_height() : shell_.get_display_height());
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_GetClipRect") {
        const uint32_t pRect = cpu.get_reg(REG_R1);
        if (pRect != 0 && pRect < 0xFF000000) {
            memory_.write_value(pRect + 0, (uint16_t)clip_x_, EndianMemory::Halfword);
            memory_.write_value(pRect + 2, (uint16_t)clip_y_, EndianMemory::Halfword);
            memory_.write_value(pRect + 4, (uint16_t)clip_dx_, EndianMemory::Halfword);
            memory_.write_value(pRect + 6, (uint16_t)clip_dy_, EndianMemory::Halfword);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, 4);
        }
    } else if (name == "IDisplay_FillRect") {
        cpu.set_reg(REG_R2, rgb_to_native(cpu.get_reg(REG_R2)));
        cpu.set_reg(REG_R3, AEE_RO_COPY);
        get_destination_bitmap()->handle_hook("IBitmap_FillRect", cpu);
    } else if (name == "IDisplay_DrawRect") {
        uint32_t stack_base = display_arg_stack_base(memory_, cpu);
        uint32_t pRect = cpu.get_reg(REG_R1);
        uint32_t clrFrame = cpu.get_reg(REG_R2);
        uint32_t clrFill = cpu.get_reg(REG_R3);
        uint32_t flags = memory_.read_value(stack_base);

        if (pRect == 0 && clrFrame == RGB_NONE && clrFill == RGB_NONE) {
            // Observed BREW idiom in Double Dragon: SetColor(background), then
            // DrawRect(NULL, RGB_NONE, RGB_NONE, flags) to clear with the
            // current display background color.
            clrFill = user_background_color_;
            flags = IDF_RECT_FILL;
        }

        if ((flags & IDF_RECT_FILL) != 0 && clrFill != RGB_NONE) {
            cpu.set_reg(REG_R1, pRect);
            cpu.set_reg(REG_R2, rgb_to_native(clrFill));
            cpu.set_reg(REG_R3, AEE_RO_COPY);
            get_destination_bitmap()->handle_hook("IBitmap_FillRect", cpu);
        }

        if ((flags & IDF_RECT_FRAME) != 0 && clrFrame != RGB_NONE) {
            // Minimal SDK-shaped frame behavior: until a line primitive exists,
            // draw a one-pixel rectangle border via four small FillRect calls.
            int16_t rx = 0;
            int16_t ry = 0;
            BrewBitmap* bitmap = get_destination_bitmap();
            int16_t rdx = static_cast<int16_t>(bitmap ? bitmap->get_width() : shell_.get_display_width());
            int16_t rdy = static_cast<int16_t>(bitmap ? bitmap->get_height() : shell_.get_display_height());
            if (pRect > 1 && pRect < 0xFF000000) {
                rx = (int16_t)memory_.read_value(pRect + 0, EndianMemory::Halfword);
                ry = (int16_t)memory_.read_value(pRect + 2, EndianMemory::Halfword);
                rdx = (int16_t)memory_.read_value(pRect + 4, EndianMemory::Halfword);
                rdy = (int16_t)memory_.read_value(pRect + 6, EndianMemory::Halfword);
            }
            if (draw_rect_tmp_ == 0) {
                draw_rect_tmp_ = shell_.malloc(8);
            }
            auto draw_part = [&](int16_t x, int16_t y, int16_t dx, int16_t dy) {
                if (dx <= 0 || dy <= 0) {
                    return;
                }
                memory_.write_value(draw_rect_tmp_ + 0, (uint16_t)x, EndianMemory::Halfword);
                memory_.write_value(draw_rect_tmp_ + 2, (uint16_t)y, EndianMemory::Halfword);
                memory_.write_value(draw_rect_tmp_ + 4, (uint16_t)dx, EndianMemory::Halfword);
                memory_.write_value(draw_rect_tmp_ + 6, (uint16_t)dy, EndianMemory::Halfword);
                cpu.set_reg(REG_R1, draw_rect_tmp_);
                cpu.set_reg(REG_R2, rgb_to_native(clrFrame));
                cpu.set_reg(REG_R3, AEE_RO_COPY);
                get_destination_bitmap()->handle_hook("IBitmap_FillRect", cpu);
            };
            draw_part(rx, ry, rdx, 1);
            draw_part(rx, (int16_t)(ry + rdy - 1), rdx, 1);
            draw_part(rx, ry, 1, rdy);
            draw_part((int16_t)(rx + rdx - 1), ry, 1, rdy);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_GetFontMetrics") {
        // R1 = AEEFont, R2 = AEEFontMetrics*, R3 = uint16* pnAscent (optional)
        uint32_t pMetrics = cpu.get_reg(REG_R2);
        uint32_t pAscent = cpu.get_reg(REG_R3);
        if (pMetrics && pMetrics < 0xFF000000) {
            memory_.write_value(pMetrics + 0, (uint16_t)12, EndianMemory::Halfword); // nAscent
            memory_.write_value(pMetrics + 2, (uint16_t)4,  EndianMemory::Halfword); // nDescent
            memory_.write_value(pMetrics + 4, (uint16_t)2,  EndianMemory::Halfword); // nLeading
            memory_.write_value(pMetrics + 6, (uint16_t)8,  EndianMemory::Halfword); // nMaxCharWidth
        }
        if (pAscent && pAscent < 0xFF000000) {
            memory_.write_value(pAscent, (uint16_t)12, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_DrawText") {
        // IDISPLAY_DrawText(p, f, psz, nl, x, y, prcb, dwFlags) — 8 params
        // Standard ARM AAPCS: R0=pdp R1=f R2=psz R3=nl sp+0=x sp+4=y sp+8=prcb sp+12=flags
        // Zeebo thunk: vtable dispatch trashes R2/R3; actual args in R5-R7 + stack
        uint32_t r2 = cpu.get_reg(REG_R2);
        uint32_t r3 = cpu.get_reg(REG_R3);
        uint32_t r5 = cpu.get_reg(REG_R5);
        uint32_t r6 = cpu.get_reg(REG_R6);
        uint32_t sp_val = display_arg_stack_base(memory_, cpu);

        bool thunk = (r2 >= 0xFF000000 && r2 != 0xFFFFFFFFu) ||
                     (r3 >= 0xFF000000 && r3 != 0xFFFFFFFFu);
        uint32_t pch;
        int nChars;
        int x, y;
        uint32_t prc_ptr;
        uint32_t dwFlags;

        if (thunk) {
            pch = r5;
            nChars = -1;  // auto-detect from AECHAR null terminator
            x = (int)memory_.read_value(sp_val + 0);
            y = (int)r6;
            prc_ptr = memory_.read_value(sp_val + 4);
            dwFlags = memory_.read_value(sp_val + 8);
        } else {
            pch = r2;
            nChars = static_cast<int>(cpu.get_reg(REG_R3));
            x = static_cast<int>(memory_.read_value(sp_val + 0));
            y = static_cast<int>(memory_.read_value(sp_val + 4));
            prc_ptr = memory_.read_value(sp_val + 8);
            dwFlags = memory_.read_value(sp_val + 12);
        }

        // Validate coordinates; fall back to rect if out of range
        if (prc_ptr && prc_ptr < 0xFF000000 && (x < -1024 || x > 4096 || y < -1024 || y > 4096)) {
            x = (int)memory_.read_value(prc_ptr + 0, EndianMemory::Halfword);
            y = (int)memory_.read_value(prc_ptr + 2, EndianMemory::Halfword);
        }

        bool descriptor_byte_text = false;
        uint32_t resolved_pch = resolve_display_text_descriptor(memory_, pch, descriptor_byte_text);
        std::string s;
        const bool oem_text = (dwFlags & IDF_TEXT_FORMAT_OEM) != 0;
        if (oem_text || descriptor_byte_text) {
            s = read_guest_byte_text(memory_, resolved_pch, nChars);
        } else {
            std::string ae_text = read_guest_aechar_text(memory_, resolved_pch, nChars);
            std::string byte_text = read_guest_byte_text(memory_, resolved_pch, nChars);
            if (!byte_text.empty() &&
                printable_score(byte_text) > printable_score(ae_text) + 2 &&
                byte_text.size() > ae_text.size()) {
                s = byte_text;
            } else {
                s = ae_text;
            }
        }
        if (!s.empty()) {
            printf("  IDisplay_DrawText: '%s' at (%d, %d) flags=0x%x%s\n", s.c_str(), x, y, dwFlags, thunk ? " [thunk]" : "");
        }
        if (std::getenv("ZEEMU_TRACE_TEXT_BYTES")) {
            const uint32_t sp_raw = cpu.get_reg(REG_SP);
            printf("  [DrawText args] R0=%08x R1=%08x R2=%08x R3=%08x R4=%08x R5=%08x R6=%08x R7=%08x SP=%08x base=%08x pch=%08x nChars=%d\n",
                   cpu.get_reg(REG_R0), cpu.get_reg(REG_R1), cpu.get_reg(REG_R2), cpu.get_reg(REG_R3),
                   cpu.get_reg(REG_R4), cpu.get_reg(REG_R5), cpu.get_reg(REG_R6), cpu.get_reg(REG_R7),
                   sp_raw, sp_val, pch, nChars);
            printf("  [DrawText stack]");
            for (uint32_t i = 0; i < 8; ++i) {
                printf(" +%u=%08x", i * 4u, memory_.read_value(sp_raw + i * 4u));
            }
            printf("\n");
            printf("  [DrawText base]");
            for (uint32_t i = 0; i < 8; ++i) {
                printf(" +%u=%08x", i * 4u, memory_.read_value(sp_val + i * 4u));
            }
            printf("\n");
            if (pch != 0 && pch < 0xFF000000u) {
                const uint32_t first_word = memory_.read_value(pch);
                printf("  [DrawText ptr] *pch=%08x\n", first_word);
                if (first_word != 0 && first_word < 0xFF000000u) {
                    printf("  [DrawText object]");
                    for (uint32_t i = 0; i < 8; ++i) {
                        printf(" +%u=%08x", i * 4u, memory_.read_value(first_word + i * 4u));
                    }
                    printf("\n");
                    const uint32_t object_text = memory_.read_value(first_word + 8u);
                    if (object_text != 0 && object_text < 0xFF000000u) {
                        trace_text_bytes(memory_, "IDisplay_DrawText.object+8", object_text, nChars);
                    }
                    trace_text_bytes(memory_, "IDisplay_DrawText.deref", first_word, nChars);
                }
            }
        }
        trace_text_bytes(memory_, "IDisplay_DrawText", pch, nChars);
        draw_text_to_device_bitmap(x, y, s);
        if (shell_.get_presenter() && !s.empty()) {
            shell_.get_presenter()->draw_debug_text((float)x, (float)y, s.c_str());
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_MeasureTextEx") {
        // SDK_Zeebo AEEIDisplay.h: R0=pdp R1=f R2=psz R3=nl sp+0=nMaxWidth sp+4=pnFits.
        // The measured width is returned in R0; there is no pnWidth parameter in this ABI.
        // Zeebo thunk: R2/R3 hold vtable pointers; psz in R5, nl in R6
        uint32_t r2 = cpu.get_reg(REG_R2);
        uint32_t r3 = cpu.get_reg(REG_R3);
        uint32_t r5 = cpu.get_reg(REG_R5);
        uint32_t r6 = cpu.get_reg(REG_R6);
        uint32_t sp_val = display_arg_stack_base(memory_, cpu);

        bool thunk_mtex = (r2 >= 0xFF000000 && r2 != 0xFFFFFFFFu) ||
                          (r3 >= 0xFF000000 && r3 != 0xFFFFFFFFu);
        uint32_t pch;
        int nChars;
        int nMaxWidth;
        uint32_t pnFits;

        if (thunk_mtex) {
            pch = r5;
            nChars = (int)r6;
            nMaxWidth = (int)memory_.read_value(sp_val + 0);
            pnFits = memory_.read_value(sp_val + 4);
        } else {
            pch = r2;
            nChars = (int)r3;
            nMaxWidth = (int)memory_.read_value(sp_val + 0);
            pnFits = memory_.read_value(sp_val + 4);
        }
        
        int width = 0;
        int fits = 0;

        // Guard: null text pointer or garbage nChars
        if (nChars > 0 && nChars != -1 && (uint32_t)nChars > 4096u) nChars = 4096;

        if (pch && pch < 0xFF000000) {
            bool descriptor_byte_text = false;
            uint32_t resolved_pch = resolve_display_text_descriptor(memory_, pch, descriptor_byte_text);
            std::string ae_text = read_guest_aechar_text(memory_, resolved_pch, nChars);
            std::string byte_text = read_guest_byte_text(memory_, resolved_pch, nChars);
            const std::string& text =
                (!byte_text.empty() &&
                 printable_score(byte_text) > printable_score(ae_text) + 2 &&
                 byte_text.size() > ae_text.size())
                    ? byte_text
                    : ae_text;
            int len = (int)text.size();
            width = len * 8; // Stub 8px per char
            fits = (nMaxWidth == -1 || width <= nMaxWidth) ? len : (nMaxWidth / 8);
        } else {
            // pch == 0 often means query font metrics.
            // Some apps expect pnWidth to receive font height in this case.
            width = 16; // Stub font height
            fits = 0;
        }

        if (pnFits && pnFits < 0xFF000000) {
            memory_.write_value(pnFits, (uint32_t)fits);
        }
        printf("  IDisplay_MeasureTextEx: font=0x%x text=0x%x nChars=%d nMaxWidth=%d fits=%d width=%d\n",
               cpu.get_reg(REG_R1), pch, nChars, nMaxWidth, fits, width);
        cpu.set_reg(REG_R0, (uint32_t)width);
    } else if (name == "IDisplay_BitBlt") {
        const uint32_t stack_base = display_arg_stack_base(memory_, cpu);
        const int x_dest = (int32_t)cpu.get_reg(REG_R1);
        const int y_dest = (int32_t)cpu.get_reg(REG_R2);
        const int cx_dest = (int32_t)cpu.get_reg(REG_R3);
        const int cy_dest = (int32_t)memory_.read_value(stack_base + 0);
        const uint32_t source_ptr = memory_.read_value(stack_base + 4);
        const int x_src = (int32_t)memory_.read_value(stack_base + 8);
        const int y_src = (int32_t)memory_.read_value(stack_base + 12);
        const uint32_t rop = memory_.read_value(stack_base + 16);

        BrewBitmap* dst = get_destination_bitmap();
        DibView src = read_idib(memory_, source_ptr);
        if (src.bits == 0) {
            src = read_legacy_aeedib(memory_, source_ptr);
        }
        if (src.bits == 0) {
            if (BrewBitmap* bitmap = lookup_brew_bitmap(source_ptr)) {
                src.bits = bitmap->get_buffer_ptr();
                src.width = bitmap->get_width();
                src.height = bitmap->get_height();
                src.pitch = bitmap->get_pitch();
                src.depth = bitmap->get_depth();
                src.color_scheme = src.depth == 16 ? 16 : 0;
                src.transparent = bitmap->get_transparency_color();
            }
        }
        if (src.bits == 0 && valid_out_ptr(source_ptr) && cx_dest > 0 && cy_dest > 0) {
            src.bits = source_ptr;
            src.width = cx_dest + std::max(x_src, 0);
            src.height = cy_dest + std::max(y_src, 0);
            src.pitch = src.width * 2;
            src.depth = 16;
            src.color_scheme = 16;
            src.transparent = 0xffffffffu;
        }

        if (std::getenv("ZEEMU_TRACE_BITMAP")) {
            printf("  IDisplay_BitBlt: dst=0x%08x (%d,%d %dx%d) src=0x%08x (%d,%d) rop=%u src_bits=0x%08x src_size=%dx%dx%d src_nonzero=%u\n",
                   dst ? dst->get_object_ptr() : 0u,
                   x_dest,
                   y_dest,
                   cx_dest,
                   cy_dest,
                   source_ptr,
                   x_src,
                   y_src,
                   rop,
                   src.bits,
                   src.width,
                   src.height,
                   src.depth,
                   count_nonzero_dib(memory_, src));
        }

        if (dst && dst->get_depth() == 16 && src.bits != 0 && cx_dest > 0 && cy_dest > 0) {
            const int src_w = src.width > 0 ? src.width : cx_dest;
            const int src_h = src.height > 0 ? src.height : cy_dest;
            const int copy_w = std::min(cx_dest, src_w - x_src);
            const int copy_h = std::min(cy_dest, src_h - y_src);
            const int clip_x0 = std::max<int>(clip_x_, 0);
            const int clip_y0 = std::max<int>(clip_y_, 0);
            const int clip_x1 = std::min<int>(clip_x_ + clip_dx_, dst->get_width());
            const int clip_y1 = std::min<int>(clip_y_ + clip_dy_, dst->get_height());
            const int x0 = std::max<int>(x_dest, clip_x0);
            const int y0 = std::max<int>(y_dest, clip_y0);
            const int x1 = std::min<int>(x_dest + copy_w, clip_x1);
            const int y1 = std::min<int>(y_dest + copy_h, clip_y1);
            for (int y = y0; y < y1; ++y) {
                const int sy = y_src + (y - y_dest);
                if (sy < 0 || sy >= src_h) {
                    continue;
                }
                const addr_t dst_row = dst->get_buffer_ptr() + (uint32_t)(y * dst->get_pitch());
                for (int x = x0; x < x1; ++x) {
                    const int sx = x_src + (x - x_dest);
                    if (sx < 0 || sx >= src_w) {
                        continue;
                    }
                    const uint32_t native = read_dib_pixel(memory_, src, sx, sy);
                    if (rop == AEE_RO_TRANSPARENT && native == src.transparent) {
                        continue;
                    }
                    const uint32_t rgb = dib_native_to_rgbval(memory_, src, native);
                    const uint16_t src16 = (src.depth == 16 && src.color_scheme == 16)
                                             ? (uint16_t)native
                                             : rgb_to_native(rgb);
                    const addr_t dst_px = dst_row + (uint32_t)(x * 2);
                    if (rop == 1u) {
                        const uint16_t old = (uint16_t)memory_.read_value(dst_px, EndianMemory::Halfword);
                        memory_.write_value(dst_px, (uint16_t)(old ^ src16), EndianMemory::Halfword);
                    } else {
                        memory_.write_value(dst_px, src16, EndianMemory::Halfword);
                    }
                }
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_CopyBitmap") {
        printf("  [IDisplay_CopyBitmap] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               r0, cpu.get_reg(REG_R1), cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IDisplay_SetFont") {
        const uint32_t nFont = cpu.get_reg(REG_R1);
        const uint32_t new_font = cpu.get_reg(REG_R2);
        if (nFont < AEE_FONT_NORMAL || nFont >= AEE_FONT_NORMAL + AEE_NUM_FONTS) {
            cpu.set_reg(REG_R0, 0);
            return;
        }
        const uint32_t index = nFont - AEE_FONT_NORMAL;
        if (current_font_ptrs_[index] == 0) {
            const uint32_t default_cls = default_font_clsid_for_designator(nFont);
            if (default_cls != 0 && shell_.get_font()) {
                current_font_ptrs_[index] = shell_.get_font()->create_instance(default_cls);
            }
        }
        const addr_t old_font = current_font_ptrs_[index];
        if (new_font != 0 && new_font < 0xFF000000u) {
            current_font_ptrs_[index] = new_font;
        }
        cpu.set_reg(REG_R0, old_font);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, cpu.get_reg(REG_R1), cpu.get_reg(REG_R2), cpu.get_reg(REG_R3));
        cpu.set_reg(REG_R0, 0);
    }
}
