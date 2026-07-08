#include "brew/BrewGraphics.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewBitmap.h"
#include "graphics/RenderBackend.h"
#include "cpu/core/CPU.h"
#include "cpu/memory/VirtualMemory.h"
#include <cstdio>
#include <algorithm>

namespace {

constexpr uint32_t SUCCESS = 0u;
constexpr uint32_t EBADPARM = 14u;

static uint16_t rgb_to_565(uint32_t color) {
    if (color <= 0xFFFFu) {
        return (uint16_t)color;
    }
    const uint32_t r = (color >> 16) & 0xffu;
    const uint32_t g = (color >> 8) & 0xffu;
    const uint32_t b = color & 0xffu;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void present_bitmap(BrewShell& shell, BrewBitmap* bitmap) {
    if (!shell.get_presenter() || !bitmap || bitmap->get_depth() != 16) {
        return;
    }
    auto* vm = shell.get_virtual_memory();
    if (!vm) return;
    void* host_ptr = vm->get_host_address(bitmap->get_buffer_ptr());
    if (!host_ptr) return;
    shell.get_presenter()->begin_frame();
    shell.get_presenter()->present_rgb565(host_ptr, bitmap->get_pitch(),
                                          bitmap->get_width(), bitmap->get_height());
    shell.get_presenter()->end_frame();
}

static BrewBitmap* resolve_bitmap(addr_t object_ptr) {
    return object_ptr ? lookup_brew_bitmap(object_ptr) : nullptr;
}

static void fill_bitmap_rect(BrewBitmap* bitmap, EndianMemory& memory, int x, int y, int w, int h, uint16_t color) {
    if (!bitmap || bitmap->get_depth() != 16 || w <= 0 || h <= 0) {
        return;
    }
    const int bw = bitmap->get_width();
    const int bh = bitmap->get_height();
    const int pitch = bitmap->get_pitch();
    const addr_t base = bitmap->get_buffer_ptr();
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(bw, x + w);
    const int y1 = std::min(bh, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        const addr_t row = base + static_cast<addr_t>(yy) * static_cast<addr_t>(pitch);
        for (int xx = x0; xx < x1; ++xx) {
            memory.write_value(row + (xx * 2), color, EndianMemory::Halfword);
        }
    }
}

static bool read_rect(EndianMemory& memory, addr_t pRect, int& x, int& y, int& w, int& h) {
    if (pRect == 0 || pRect >= 0xFF000000u) {
        return false;
    }
    x = static_cast<int16_t>(memory.read_value(pRect + 0, EndianMemory::Halfword));
    y = static_cast<int16_t>(memory.read_value(pRect + 2, EndianMemory::Halfword));
    w = static_cast<int16_t>(memory.read_value(pRect + 4, EndianMemory::Halfword));
    h = static_cast<int16_t>(memory.read_value(pRect + 6, EndianMemory::Halfword));
    return w > 0 && h > 0;
}

static void draw_rect_frame(BrewBitmap* bitmap, EndianMemory& memory, int x, int y, int w, int h, uint16_t color) {
    fill_bitmap_rect(bitmap, memory, x, y, w, 1, color);
    fill_bitmap_rect(bitmap, memory, x, y + h - 1, w, 1, color);
    fill_bitmap_rect(bitmap, memory, x, y, 1, h, color);
    fill_bitmap_rect(bitmap, memory, x + w - 1, y, 1, h, color);
}

static void blit_bitmap(BrewBitmap* dst, BrewBitmap* src, EndianMemory& memory,
                        int dst_x, int dst_y, int src_x, int src_y, int copy_w, int copy_h) {
    if (!dst || !src || dst->get_depth() != 16 || src->get_depth() != 16) {
        return;
    }
    const int dw = dst->get_width();
    const int dh = dst->get_height();
    const int sw = src->get_width();
    const int sh = src->get_height();
    const addr_t dst_base = dst->get_buffer_ptr();
    const addr_t src_base = src->get_buffer_ptr();
    const int w = std::min({copy_w, sw - src_x, dw - dst_x});
    const int h = std::min({copy_h, sh - src_y, dh - dst_y});
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int yy = 0; yy < h; ++yy) {
        const addr_t src_row = src_base + ((src_y + yy) * sw * 2);
        const addr_t dst_row = dst_base + ((dst_y + yy) * dw * 2);
        for (int xx = 0; xx < w; ++xx) {
            const uint16_t px = (uint16_t)memory.read_value(src_row + ((src_x + xx) * 2), EndianMemory::Halfword);
            memory.write_value(dst_row + ((dst_x + xx) * 2), px, EndianMemory::Halfword);
        }
    }
}

} // namespace

BrewGraphics::BrewGraphics(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewGraphics::setup_vtable() {
    vtable_ptr_ = shell_.malloc(44 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    destination_ptr_ = shell_.get_display() ? shell_.get_display()->get_device_bitmap()->get_object_ptr() : 0;

    const char* names[] = {
        "AddRef", "Release", "SetBackground", "GetBackground", "SetColor", "GetColor",
        "SetFillMode", "GetFillMode", "SetFillColor", "GetFillColor", "SetPointSize", "GetPointSize",
        "SetClip", "GetClip", "SetViewport", "GetViewport", "ClearViewport", "SetPaintMode", "GetPaintMode",
        "GetColorDepth", "DrawPoint", "DrawLine", "DrawRect", "DrawCircle", "DrawArc", "DrawPie",
        "DrawEllipse", "DrawTriangle", "DrawPolygon", "DrawPolyline", "ClearRect", "EnableDoubleBuffer",
        "Update", "Translate", "Pan", "StretchBlt", "SetAlgorithmHint", "GetAlgorithmHint",
        "SetDestination", "GetDestination", "SetStrokeStyle", "GetStrokeStyle", "DrawEllipticalArc",
        "DrawRoundRectangle"
    };
    for (int i = 0; i < 44; ++i) {
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(std::string("IGraphics_") + names[i], this));
    }
}

void BrewGraphics::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IGraphics_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IGraphics_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGraphics_SetBackground") {
        background_r_ = (uint8_t)r1; background_g_ = (uint8_t)r2; background_b_ = (uint8_t)r3;
        cpu.set_reg(REG_R0, pack_rgb(background_r_, background_g_, background_b_));
    } else if (name == "IGraphics_GetBackground") {
        uint32_t pr = r1, pg = r2, pb = r3;
        if (pr && pr < 0xFF000000) memory_.write_value(pr, background_r_, EndianMemory::Byte);
        if (pg && pg < 0xFF000000) memory_.write_value(pg, background_g_, EndianMemory::Byte);
        if (pb && pb < 0xFF000000) memory_.write_value(pb, background_b_, EndianMemory::Byte);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGraphics_SetColor") {
        color_r_ = (uint8_t)r1; color_g_ = (uint8_t)r2; color_b_ = (uint8_t)r3; color_a_ = (uint8_t)cpu.get_reg(REG_R4);
        cpu.set_reg(REG_R0, pack_rgb(color_r_, color_g_, color_b_));
    } else if (name == "IGraphics_GetColor") {
        uint32_t pr = r1, pg = r2, pb = r3, pa = cpu.get_reg(REG_SP);
        if (pr && pr < 0xFF000000) memory_.write_value(pr, color_r_, EndianMemory::Byte);
        if (pg && pg < 0xFF000000) memory_.write_value(pg, color_g_, EndianMemory::Byte);
        if (pb && pb < 0xFF000000) memory_.write_value(pb, color_b_, EndianMemory::Byte);
        if (pa && pa < 0xFF000000) memory_.write_value(pa, color_a_, EndianMemory::Byte);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGraphics_SetFillMode") {
        fill_mode_ = r1 == 1;
        cpu.set_reg(REG_R0, fill_mode_ ? 1u : 0u);
    } else if (name == "IGraphics_GetFillMode") {
        cpu.set_reg(REG_R0, fill_mode_ ? 1u : 0u);
    } else if (name == "IGraphics_SetFillColor") {
        fill_r_ = (uint8_t)r1; fill_g_ = (uint8_t)r2; fill_b_ = (uint8_t)r3; fill_a_ = (uint8_t)cpu.get_reg(REG_R4);
        cpu.set_reg(REG_R0, pack_rgb(fill_r_, fill_g_, fill_b_));
    } else if (name == "IGraphics_GetFillColor") {
        uint32_t pr = r1, pg = r2, pb = r3, pa = cpu.get_reg(REG_SP);
        if (pr && pr < 0xFF000000) memory_.write_value(pr, fill_r_, EndianMemory::Byte);
        if (pg && pg < 0xFF000000) memory_.write_value(pg, fill_g_, EndianMemory::Byte);
        if (pb && pb < 0xFF000000) memory_.write_value(pb, fill_b_, EndianMemory::Byte);
        if (pa && pa < 0xFF000000) memory_.write_value(pa, fill_a_, EndianMemory::Byte);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGraphics_SetPointSize") {
        point_size_ = (uint8_t)r1;
        cpu.set_reg(REG_R0, point_size_);
    } else if (name == "IGraphics_GetPointSize") {
        cpu.set_reg(REG_R0, point_size_);
    } else if (name == "IGraphics_SetClip" || name == "IGraphics_GetClip" ||
               name == "IGraphics_SetViewport" || name == "IGraphics_GetViewport" ||
               name == "IGraphics_ClearViewport" || name == "IGraphics_SetPaintMode" ||
               name == "IGraphics_GetPaintMode" || name == "IGraphics_EnableDoubleBuffer" ||
               name == "IGraphics_Update" || name == "IGraphics_Translate" ||
               name == "IGraphics_Pan" || name == "IGraphics_SetAlgorithmHint" ||
               name == "IGraphics_GetAlgorithmHint" || name == "IGraphics_SetStrokeStyle" ||
               name == "IGraphics_GetStrokeStyle") {
        if (name == "IGraphics_SetPaintMode") paint_mode_ = (uint8_t)r1;
        if (name == "IGraphics_GetPaintMode") cpu.set_reg(REG_R0, paint_mode_);
        else if (name == "IGraphics_SetAlgorithmHint") algorithm_hint_ = (uint8_t)r1;
        else if (name == "IGraphics_GetAlgorithmHint") cpu.set_reg(REG_R0, algorithm_hint_);
        else if (name == "IGraphics_SetStrokeStyle") stroke_style_ = (uint8_t)r1;
        else if (name == "IGraphics_GetStrokeStyle") cpu.set_reg(REG_R0, stroke_style_);
        else if (name == "IGraphics_SetViewport") {
            const uint32_t sp = cpu.get_reg(REG_SP);
            viewport_x_ = (int16_t)r1;
            viewport_y_ = (int16_t)r2;
            viewport_w_ = (int16_t)r3;
            viewport_h_ = (int16_t)memory_.read_value(sp + 0, EndianMemory::Halfword);
            cpu.set_reg(REG_R0, 1);
        } else if (name == "IGraphics_GetViewport") {
            uint32_t pRect = r1;
            if (pRect && pRect < 0xFF000000) {
                memory_.write_value(pRect + 0, (uint16_t)viewport_x_, EndianMemory::Halfword);
                memory_.write_value(pRect + 2, (uint16_t)viewport_y_, EndianMemory::Halfword);
                memory_.write_value(pRect + 4, (uint16_t)viewport_w_, EndianMemory::Halfword);
                memory_.write_value(pRect + 6, (uint16_t)viewport_h_, EndianMemory::Halfword);
            }
            cpu.set_reg(REG_R0, 1);
        } else if (name == "IGraphics_ClearViewport") {
            BrewBitmap* bitmap = destination_ptr_ ? resolve_bitmap(destination_ptr_) : nullptr;
            if (!bitmap && shell_.get_display()) {
                bitmap = shell_.get_display()->get_destination_bitmap();
            }
            if (!bitmap && destination_ptr_) {
                bitmap = resolve_bitmap(destination_ptr_);
            }
            if (bitmap) {
                const uint16_t color = rgb_to_565(pack_rgb(background_r_, background_g_, background_b_));
                fill_bitmap_rect(bitmap, memory_, 0, 0, bitmap->get_width(), bitmap->get_height(), color);
            }
            cpu.set_reg(REG_R0, 1);
        } else if (name == "IGraphics_Translate") {
            translate_x_ = (int16_t)r1;
            translate_y_ = (int16_t)r2;
            cpu.set_reg(REG_R0, 1);
        } else if (name == "IGraphics_Pan") {
            viewport_x_ += (int16_t)r1;
            viewport_y_ += (int16_t)r2;
            cpu.set_reg(REG_R0, 1);
        } else if (name == "IGraphics_Update") {
            BrewBitmap* bitmap = shell_.get_display() ? shell_.get_display()->get_destination_bitmap() : nullptr;
            if (!bitmap && destination_ptr_) {
                bitmap = resolve_bitmap(destination_ptr_);
            }
            present_bitmap(shell_, bitmap);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, 1);
        }
    } else if (name == "IGraphics_GetColorDepth") {
        cpu.set_reg(REG_R0, 16);
    } else if (name == "IGraphics_SetDestination") {
        destination_ptr_ = r1 ? r1 : destination_ptr_;
        if (auto* bitmap = resolve_bitmap(destination_ptr_)) {
            destination_ptr_ = bitmap->get_object_ptr();
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGraphics_GetDestination") {
        cpu.set_reg(REG_R0, destination_ptr_);
    } else if (name == "IGraphics_ClearRect" || name == "IGraphics_DrawPoint" || name == "IGraphics_DrawLine" ||
               name == "IGraphics_DrawRect" || name == "IGraphics_DrawCircle" || name == "IGraphics_DrawArc" ||
               name == "IGraphics_DrawPie" || name == "IGraphics_DrawEllipse" || name == "IGraphics_DrawTriangle" ||
               name == "IGraphics_DrawPolygon" || name == "IGraphics_DrawPolyline" || name == "IGraphics_DrawEllipticalArc" ||
               name == "IGraphics_DrawRoundRectangle" || name == "IGraphics_StretchBlt") {
        BrewBitmap* dst = destination_ptr_ ? resolve_bitmap(destination_ptr_) : nullptr;
        if (!dst && shell_.get_display()) {
            dst = shell_.get_display()->get_destination_bitmap();
        }
        if (name == "IGraphics_StretchBlt" && dst) {
            const uint32_t sp = cpu.get_reg(REG_SP);
            const int16_t dst_x = (int16_t)r1;
            const int16_t dst_y = (int16_t)r2;
            const int16_t dest_w = (int16_t)r3;
            const int16_t dest_h = (int16_t)memory_.read_value(sp + 0, EndianMemory::Halfword);
            const uint32_t src_obj = memory_.read_value(sp + 4);
            const int16_t src_x = (int16_t)memory_.read_value(sp + 8, EndianMemory::Halfword);
            const int16_t src_y = (int16_t)memory_.read_value(sp + 12, EndianMemory::Halfword);
            BrewBitmap* src = resolve_bitmap(src_obj);
            if (!src && destination_ptr_) {
                src = resolve_bitmap(destination_ptr_);
            }
            if (src) {
                blit_bitmap(dst, src, memory_, dst_x + viewport_x_ + translate_x_, dst_y + viewport_y_ + translate_y_,
                            src_x, src_y, std::max(1, (int)dest_w), std::max(1, (int)dest_h));
            }
        } else if (name == "IGraphics_DrawRoundRectangle" || name == "IGraphics_DrawRect" || name == "IGraphics_ClearRect") {
            BrewBitmap* bitmap = dst;
            if (!bitmap && destination_ptr_) {
                bitmap = resolve_bitmap(destination_ptr_);
            }
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            if (!bitmap || !read_rect(memory_, r1, x, y, w, h)) {
                cpu.set_reg(REG_R0, EBADPARM);
                return;
            }
            x += viewport_x_ + translate_x_;
            y += viewport_y_ + translate_y_;
            if (name == "IGraphics_ClearRect") {
                const uint16_t color = rgb_to_565(pack_rgb(background_r_, background_g_, background_b_));
                fill_bitmap_rect(bitmap, memory_, x, y, w, h, color);
            } else {
                // AEEGraphics.h: DrawRect outlines with foreground color and
                // fills the interior only when fill mode is enabled.
                if (fill_mode_) {
                    const uint16_t fill_color = rgb_to_565(pack_rgb(fill_r_, fill_g_, fill_b_));
                    fill_bitmap_rect(bitmap, memory_, x, y, w, h, fill_color);
                }
                const uint16_t frame_color = rgb_to_565(pack_rgb(color_r_, color_g_, color_b_));
                draw_rect_frame(bitmap, memory_, x, y, w, h, frame_color);
            }
        }
        cpu.set_reg(REG_R0, SUCCESS);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
