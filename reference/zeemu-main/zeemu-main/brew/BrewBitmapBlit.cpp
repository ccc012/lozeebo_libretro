#include "brew/BrewBitmap.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr uint32_t kAeeSuccess = 0u;
constexpr uint32_t kAeeUnsupported = 20u;
constexpr uint32_t kCompositeKeyColor = 0x00u;
constexpr uint32_t kCompositeOpaque = 0xffu;

bool valid_transform_bitmap_pair(const BrewBitmap* dst, const BrewBitmap* src,
                                 int copy_w, int copy_h) {
    return dst && src && dst->get_depth() == 16 && src->get_depth() == 16 &&
           copy_w > 0 && copy_h > 0;
}

bool should_skip_composite(uint32_t composite, uint16_t pixel, uint16_t transparent) {
    return composite == kCompositeKeyColor && pixel == transparent;
}
}

uint32_t BrewBitmap::blit_from_bitmap(BrewBitmap* src, int dst_x, int dst_y, int src_x, int src_y,
                                      int copy_w, int copy_h, int scale_x, int scale_y, uint32_t composite) {
    if (!src || depth_ != 16 || src->depth_ != 16 || copy_w <= 0 || copy_h <= 0) {
        return kAeeUnsupported;
    }
    scale_x = std::max(scale_x, 1);
    scale_y = std::max(scale_y, 1);
    const int out_w = copy_w * scale_x;
    const int out_h = copy_h * scale_y;
    const uint16_t transparent = static_cast<uint16_t>(src->transparent_color_);
    for (int y = 0; y < out_h; ++y) {
        const int sy = src_y + y / scale_y;
        const int dy = dst_y + y;
        if (sy < 0 || dy < 0 || sy >= src->height_ || dy >= height_) {
            continue;
        }
        for (int x = 0; x < out_w; ++x) {
            const int sx = src_x + x / scale_x;
            const int dx = dst_x + x;
            if (sx < 0 || dx < 0 || sx >= src->width_ || dx >= width_) {
                continue;
            }
            const uint16_t pixel = static_cast<uint16_t>(
                memory_.read_value(src->buffer_ptr_ + sy * src->pitch_ + sx * 2, EndianMemory::Halfword));
            if (composite == kCompositeKeyColor && pixel == transparent) {
                continue;
            }
            memory_.write_value(buffer_ptr_ + dy * pitch_ + dx * 2, pixel, EndianMemory::Halfword);
        }
    }
    return kAeeSuccess;
}

uint32_t BrewBitmap::transform_blit_from_bitmap(BrewBitmap* src, int dst_x, int dst_y,
                                                int src_x, int src_y, int copy_w, int copy_h,
                                                int a, int b, int c, int d, uint32_t composite) {
    if (!valid_transform_bitmap_pair(this, src, copy_w, copy_h)) {
        return kAeeUnsupported;
    }

    const double ma = static_cast<double>(a) / 256.0;
    const double mb = static_cast<double>(b) / 256.0;
    const double mc = static_cast<double>(c) / 256.0;
    const double md = static_cast<double>(d) / 256.0;
    const double det = ma * md - mb * mc;
    if (std::abs(det) < 1.0e-9) {
        return kAeeUnsupported;
    }

    // AEEITransform.h: transformed blits use the source area's center as the
    // origin. xDst/yDst name the untransformed top-left position, so the
    // transform center is still xDst + dxSrc / 2, yDst + dySrc / 2.
    const double src_cx = static_cast<double>(copy_w) * 0.5;
    const double src_cy = static_cast<double>(copy_h) * 0.5;
    const double dst_cx = static_cast<double>(dst_x) + src_cx;
    const double dst_cy = static_cast<double>(dst_y) + src_cy;

    const double corners[4][2] = {
        {-src_cx, -src_cy},
        { src_cx, -src_cy},
        {-src_cx,  src_cy},
        { src_cx,  src_cy},
    };
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double tx = ma * corners[i][0] + mb * corners[i][1];
        const double ty = mc * corners[i][0] + md * corners[i][1];
        if (i == 0 || tx < min_x) min_x = tx;
        if (i == 0 || tx > max_x) max_x = tx;
        if (i == 0 || ty < min_y) min_y = ty;
        if (i == 0 || ty > max_y) max_y = ty;
    }

    const int x0 = std::max(0, static_cast<int>(std::floor(dst_cx + min_x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(dst_cy + min_y)));
    const int x1 = std::min(width_, static_cast<int>(std::ceil(dst_cx + max_x)));
    const int y1 = std::min(height_, static_cast<int>(std::ceil(dst_cy + max_y)));
    if (x0 >= x1 || y0 >= y1) {
        return kAeeSuccess;
    }

    const uint16_t transparent = static_cast<uint16_t>(src->transparent_color_);
    for (int y = y0; y < y1; ++y) {
        const double dy = (static_cast<double>(y) + 0.5) - dst_cy;
        for (int x = x0; x < x1; ++x) {
            const double dx = (static_cast<double>(x) + 0.5) - dst_cx;
            const double rel_x = (md * dx - mb * dy) / det;
            const double rel_y = (-mc * dx + ma * dy) / det;
            const int sx = src_x + static_cast<int>(std::floor(rel_x + src_cx));
            const int sy = src_y + static_cast<int>(std::floor(rel_y + src_cy));
            if (sx < src_x || sy < src_y || sx >= src_x + copy_w || sy >= src_y + copy_h ||
                sx < 0 || sy < 0 || sx >= src->width_ || sy >= src->height_) {
                continue;
            }
            const uint16_t pixel = static_cast<uint16_t>(
                memory_.read_value(src->buffer_ptr_ + sy * src->pitch_ + sx * 2, EndianMemory::Halfword));
            if (should_skip_composite(composite, pixel, transparent)) {
                continue;
            }
            memory_.write_value(buffer_ptr_ + y * pitch_ + x * 2, pixel, EndianMemory::Halfword);
        }
    }
    return kAeeSuccess;
}

bool BrewBitmap::handle_blit_hook(const std::string& name, CPU& cpu) {
    if (name != "IBitmap_BltIn") {
        return false;
    }

    const int dst_x = static_cast<int>(cpu.get_reg(REG_R1));
    const int dst_y = static_cast<int>(cpu.get_reg(REG_R2));
    const int copy_w = static_cast<int>(cpu.get_reg(REG_R3));
    const uint32_t sp = cpu.get_reg(REG_SP);
    const int copy_h = static_cast<int>(memory_.read_value(sp));
    const addr_t src_obj = memory_.read_value(sp + 4);
    const int src_x = static_cast<int>(memory_.read_value(sp + 8));
    const int src_y = static_cast<int>(memory_.read_value(sp + 12));
    BrewBitmap* src = lookup_brew_bitmap(src_obj);
    if (src && depth_ == 16 && src->depth_ == 16 && copy_w > 0 && copy_h > 0) {
        blit_from_bitmap(src, dst_x, dst_y, src_x, src_y, copy_w, copy_h, 1, 1, kCompositeOpaque);
        static int bltin_logs = 0;
        if (bltin_logs++ < 16) {
            printf("  IBitmap_BltIn: dst=0x%08x (%d,%d %dx%d) src=0x%08x (%d,%d)\n",
                   object_ptr_, dst_x, dst_y, copy_w, copy_h, src_obj, src_x, src_y);
        }
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  IBitmap_BltIn unsupported dst=0x%08x src=0x%08x %dx%d depths=%d/%d\n",
               object_ptr_, src_obj, copy_w, copy_h, depth_, src ? src->depth_ : 0);
        cpu.set_reg(REG_R0, 4);
    }
    return true;
}
