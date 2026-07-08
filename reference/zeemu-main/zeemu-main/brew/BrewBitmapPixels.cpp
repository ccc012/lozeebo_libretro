#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t kRgbNone = 0xffffffffu;
constexpr uint32_t kAeeRoXor = 1u;
}

bool BrewBitmap::handle_pixel_hook(const std::string& name, CPU& cpu) {
    if (name == "IBitmap_GetPixel") {
        const auto x = static_cast<unsigned>(cpu.get_reg(REG_R1));
        const auto y = static_cast<unsigned>(cpu.get_reg(REG_R2));
        const addr_t pColor = cpu.get_reg(REG_R3);
        constexpr uint32_t AEE_SUCCESS = 0u;
        constexpr uint32_t AEE_EBADPARM = 14u;
        constexpr uint32_t AEE_EUNSUPPORTED = 20u;
        if (pColor == 0 || pColor >= 0xFF000000u ||
            x >= static_cast<unsigned>(width_) || y >= static_cast<unsigned>(height_)) {
            cpu.set_reg(REG_R0, AEE_EBADPARM);
            return true;
        }

        uint32_t native = 0;
        const addr_t row = buffer_ptr_ + static_cast<addr_t>(y) * static_cast<addr_t>(pitch_);
        if (depth_ == 16) {
            native = memory_.read_value(row + static_cast<addr_t>(x) * 2u, EndianMemory::Halfword);
        } else if (depth_ == 8) {
            native = memory_.read_value(row + x, EndianMemory::Byte);
        } else if (depth_ == 1 || depth_ == 2 || depth_ == 4) {
            const uint32_t bit = x * static_cast<uint32_t>(depth_);
            const uint8_t packed = static_cast<uint8_t>(memory_.read_value(row + bit / 8u, EndianMemory::Byte));
            const uint32_t shift = 8u - static_cast<uint32_t>(depth_) - (bit % 8u);
            native = (packed >> shift) & ((1u << static_cast<uint32_t>(depth_)) - 1u);
        } else {
            cpu.set_reg(REG_R0, AEE_EUNSUPPORTED);
            return true;
        }

        memory_.write_value(pColor, native);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return true;
    }

    if (name == "IBitmap_FillRect" || name == "IDisplay_FillRect") {
        const uint32_t pRect = cpu.get_reg(REG_R1);
        const uint32_t color = cpu.get_reg(REG_R2);
        const uint32_t rop = cpu.get_reg(REG_R3);
        if (std::getenv("ZEEMU_TRACE_BITMAP")) {
            printf("  %s: obj=0x%08x Rect=0x%x NativeColor=0x%x Rop=%u\n",
                   name.c_str(), object_ptr_, pRect, color, rop);
        }

        if (color == kRgbNone) {
            cpu.set_reg(REG_R0, 0);
            return true;
        }

        int16_t rx = 0;
        int16_t ry = 0;
        int16_t rdx = static_cast<int16_t>(width_);
        int16_t rdy = static_cast<int16_t>(height_);
        if (pRect > 1 && pRect < 0xFF000000) {
            rx = static_cast<int16_t>(memory_.read_value(pRect + 0, EndianMemory::Halfword));
            ry = static_cast<int16_t>(memory_.read_value(pRect + 2, EndianMemory::Halfword));
            rdx = static_cast<int16_t>(memory_.read_value(pRect + 4, EndianMemory::Halfword));
            rdy = static_cast<int16_t>(memory_.read_value(pRect + 6, EndianMemory::Halfword));
        }

        if (depth_ == 16) {
            const uint16_t c16 = static_cast<uint16_t>(color);
            const int x0 = std::max<int>(rx, 0);
            const int y0 = std::max<int>(ry, 0);
            const int x1 = std::min<int>(rx + rdx, width_);
            const int y1 = std::min<int>(ry + rdy, height_);
            for (int y = y0; y < y1; ++y) {
                if (y < 0) {
                    continue;
                }
                const addr_t row_ptr = buffer_ptr_ + (y * pitch_);
                for (int x = x0; x < x1; ++x) {
                    const addr_t pixel = row_ptr + (x * 2);
                    if (rop == kAeeRoXor) {
                        const uint16_t dst = static_cast<uint16_t>(memory_.read_value(pixel, EndianMemory::Halfword));
                        memory_.write_value(pixel, static_cast<uint16_t>(dst ^ c16), EndianMemory::Halfword);
                    } else {
                        memory_.write_value(pixel, c16, EndianMemory::Halfword);
                    }
                }
            }
        }
        cpu.set_reg(REG_R0, 0);
        return true;
    }

    if (name == "IBitmap_DrawPixel") {
        const int x = static_cast<int>(cpu.get_reg(REG_R1));
        const int y = static_cast<int>(cpu.get_reg(REG_R2));
        const uint32_t color = cpu.get_reg(REG_R3);
        if (color != kRgbNone && x >= 0 && x < width_ && y >= 0 && y < height_ && depth_ == 16) {
            uint16_t c16 = static_cast<uint16_t>(color);
            const addr_t pixel_ptr = buffer_ptr_ + (y * pitch_) + x * 2;
            const uint32_t rop = memory_.read_value(cpu.get_reg(REG_SP));
            if (rop == kAeeRoXor) {
                const uint16_t dst = static_cast<uint16_t>(memory_.read_value(pixel_ptr, EndianMemory::Halfword));
                c16 = static_cast<uint16_t>(dst ^ c16);
            }
            memory_.write_value(pixel_ptr, c16, EndianMemory::Halfword);
        }
        cpu.set_reg(REG_R0, 0);
        return true;
    }

    return false;
}
