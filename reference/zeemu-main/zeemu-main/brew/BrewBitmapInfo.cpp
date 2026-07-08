#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

bool BrewBitmap::handle_info_hook(const std::string& name, CPU& cpu) {
    if (name == "IBitmap_GetInfo") {
        const uint32_t pInfo = cpu.get_reg(REG_R1);
        const uint32_t nSize = cpu.get_reg(REG_R2);
        // Qualcomm AEEIBitmap.h defines AEEBitmapInfo as exactly three uint32
        // fields: cx, cy, nDepth. Older code here used a DIB-like packed
        // layout and wrote past the 12-byte caller buffer, corrupting adjacent
        // stack values in titles such as Caveman Ninja.
        if (pInfo != 0 && pInfo < 0xFF000000 && nSize >= 12) {
            memory_.write_value(pInfo + 0, static_cast<uint32_t>(width_));
            memory_.write_value(pInfo + 4, static_cast<uint32_t>(height_));
            memory_.write_value(pInfo + 8, static_cast<uint32_t>(depth_));
        }
        if (std::getenv("ZEEMU_TRACE_GLES_VERTS") != nullptr) {
            printf("  IBitmap_GetInfo pInfo=0x%08x nSize=%u -> %dx%dx%d\n",
                   pInfo, nSize, width_, height_, depth_);
        }
        cpu.set_reg(REG_R0, nSize >= 12 ? 0u : 4u);
        return true;
    }

    if (name == "IBitmap_CreateCompatibleBitmap") {
        const uint32_t ppBitmap = cpu.get_reg(REG_R1);
        uint16_t cx = static_cast<uint16_t>(cpu.get_reg(REG_R2));
        uint16_t cy = static_cast<uint16_t>(cpu.get_reg(REG_R3));
        if (cx == 0) {
            cx = static_cast<uint16_t>(width_);
        }
        if (cy == 0) {
            cy = static_cast<uint16_t>(height_);
        }
        auto child = std::make_unique<BrewBitmap>(shell_, memory_, cx, cy, depth_, 0, 0, color_scheme_, transparent_color_);
        const uint32_t child_ptr = child->get_object_ptr();
        printf("  IBitmap_CreateCompatibleBitmap: pp=0x%x %dx%dx%d -> 0x%x\n",
               ppBitmap, cx, cy, depth_, child_ptr);
        if (ppBitmap != 0) {
            memory_.write_value(ppBitmap, child_ptr);
        }
        children_.push_back(std::move(child));
        cpu.set_reg(REG_R0, 0);
        return true;
    }

    if (name == "IBitmap_SetTransparencyColor") {
        transparent_color_ = cpu.get_reg(REG_R1);
        memory_.write_value(object_ptr_ + 16, transparent_color_);
        cpu.set_reg(REG_R0, 0);
        return true;
    }

    if (name == "IBitmap_GetTransparencyColor") {
        const uint32_t pColor = cpu.get_reg(REG_R1);
        if (pColor != 0 && pColor < 0xFF000000) {
            memory_.write_value(pColor, transparent_color_);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, 4);
        }
        return true;
    }

    return false;
}
