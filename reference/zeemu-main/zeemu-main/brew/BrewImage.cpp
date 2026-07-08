#include "third_party/stb_image.h"

#include "brew/BrewImage.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewBitmap.h"
#include "brew/BrewMemAStream.h"
#include "vfs/VirtualFileSystem.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace {
constexpr uint32_t kAeeRoCopy = 2;
constexpr uint32_t kAeeRoTransparent = 7;
constexpr uint32_t kAeeRoBlend = 9;
constexpr int kIdibColorScheme888 = 24;

struct DecodedImageCacheEntry {
    int width = 0;
    int height = 0;
    int channels = 0;
    uint32_t rop = kAeeRoCopy;
    std::vector<uint32_t> pixels;
};

uint64_t fnv1a64(const uint8_t* data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h ^ (static_cast<uint64_t>(size) * 0x9e3779b97f4a7c15ull);
}

std::unordered_map<uint64_t, DecodedImageCacheEntry>& decoded_image_cache() {
    static std::unordered_map<uint64_t, DecodedImageCacheEntry> cache;
    return cache;
}

struct GuestBitmapProbe {
    addr_t bits = 0;
    int width = 0;
    int height = 0;
    int pitch = 0;
    int type = 0;
};

bool valid_guest_ptr(addr_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

GuestBitmapProbe probe_bjt_guest_bitmap(EndianMemory& memory, addr_t object_ptr) {
    GuestBitmapProbe probe;
    if (!valid_guest_ptr(object_ptr)) {
        return probe;
    }
    const addr_t state = memory.read_value(object_ptr + 0x44);
    if (!valid_guest_ptr(state)) {
        return probe;
    }
    const addr_t surface = memory.read_value(state + 0x58);
    if (!valid_guest_ptr(surface)) {
        return probe;
    }
    probe.width = static_cast<int>(memory.read_value(state + 0x64));
    probe.height = static_cast<int>(memory.read_value(state + 0x68));
    probe.type = static_cast<int>(static_cast<int8_t>(memory.read_value(surface + 0x08, EndianMemory::Byte)));
    probe.pitch = static_cast<int>(memory.read_value(surface + 0x0c));
    probe.bits = memory.read_value(surface + 0x24);
    if (!valid_guest_ptr(probe.bits) || probe.width <= 0 || probe.width > 4096 ||
        probe.height <= 0 || probe.height > 4096 || probe.pitch <= 0 || probe.pitch > 65536) {
        return GuestBitmapProbe{};
    }
    return probe;
}

uint32_t count_nonzero_guest_probe(EndianMemory& memory, const GuestBitmapProbe& probe, uint32_t limit = 4096) {
    if (!valid_guest_ptr(probe.bits) || probe.width <= 0 || probe.height <= 0) {
        return 0;
    }
    const uint32_t total = std::min<uint32_t>(
        static_cast<uint32_t>(probe.width) * static_cast<uint32_t>(probe.height), limit);
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const uint32_t x = i % static_cast<uint32_t>(probe.width);
        const uint32_t y = i / static_cast<uint32_t>(probe.width);
        const addr_t row = probe.bits + static_cast<addr_t>(y) * static_cast<addr_t>(probe.pitch);
        uint32_t pixel = 0;
        if (probe.type == 3) {
            pixel = memory.read_value(row + x * 2u, EndianMemory::Halfword);
        } else if (probe.type == 4) {
            pixel = memory.read_value(row + x * 4u);
        } else {
            pixel = memory.read_value(row + x, EndianMemory::Byte);
        }
        if (pixel != 0) {
            ++count;
        }
    }
    return count;
}
}

BrewImage::BrewImage(BrewShell& shell, EndianMemory& memory, const std::string& virtual_path, const std::string& current_directory)
    : shell_(shell), memory_(memory), source_path_(virtual_path)
{
    std::string data;
    if (shell_.get_vfs().read_file(virtual_path, data, current_directory)) {
        load_encoded_image(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    if (width_ <= 0 || height_ <= 0) {
        width_ = 1;
        height_ = 1;
        draw_width_ = 1;
        draw_height_ = 1;
        rgba_pixels_.assign(1, 0xFFFF00FFu);
    }

    setup_vtable();
}

BrewImage::BrewImage(BrewShell& shell, EndianMemory& memory, const std::string& source_label, const uint8_t* data, size_t size)
    : shell_(shell), memory_(memory), source_path_(source_label)
{
    load_encoded_image(data, size);

    if (width_ <= 0 || height_ <= 0) {
        width_ = 1;
        height_ = 1;
        draw_width_ = 1;
        draw_height_ = 1;
        rgba_pixels_.assign(1, 0xFFFF00FFu);
    }

    setup_vtable();
}

bool BrewImage::load_encoded_image(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return false;
    }

    const uint64_t cache_key = fnv1a64(data, size);
    auto& cache = decoded_image_cache();
    if (auto it = cache.find(cache_key); it != cache.end()) {
        const auto& cached = it->second;
        width_ = cached.width;
        height_ = cached.height;
        src_channels_ = cached.channels;
        draw_width_ = cached.width;
        draw_height_ = cached.height;
        frame_width_ = cached.width;
        frame_count_ = 1;
        rop_ = cached.rop;
        rgba_pixels_ = cached.pixels;
        loaded_image_ = true;
        drawn_ = false;
        logged_blit_ = false;
        static int cache_logs = 0;
        if (cache_logs++ < 16 || std::getenv("ZEEMU_TRACE_HLE") != nullptr) {
            printf("  IImage decode cache hit size=%zu -> %dx%d\n", size, width_, height_);
        }
        return true;
    }

    int w = 0;
    int h = 0;
    int ch = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data), (int)size, &w, &h, &ch, 4);
    if (px && w > 0 && h > 0) {
        width_ = w;
        height_ = h;
        src_channels_ = ch;
        draw_width_ = w;
        draw_height_ = h;
        frame_width_ = w;
        frame_count_ = 1;
        rgba_pixels_.resize((size_t)w * (size_t)h);
        bool has_alpha = false;
        for (int i = 0; i < w * h; ++i) {
            has_alpha = has_alpha || px[i * 4 + 3] < 255;
            rgba_pixels_[i] =
                ((uint32_t)px[i * 4 + 0] << 24) |
                ((uint32_t)px[i * 4 + 1] << 16) |
                ((uint32_t)px[i * 4 + 2] << 8) |
                ((uint32_t)px[i * 4 + 3]);
        }
        rop_ = has_alpha ? kAeeRoBlend : kAeeRoCopy;
        cache.emplace(cache_key, DecodedImageCacheEntry{w, h, ch, rop_, rgba_pixels_});
        stbi_image_free(px);
        loaded_image_ = true;
        drawn_ = false;
        logged_blit_ = false;
        return true;
    }

    if (px) {
        stbi_image_free(px);
    }
    return false;
}

void BrewImage::setup_vtable() {
    vtable_ptr_ = shell_.malloc(11 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    const char* names[] = {
        "AddRef", "Release", "Draw", "DrawFrame",
        "GetInfo", "SetParm", "Start", "Stop",
        "SetStream", "HandleEvent", "Notify"
    };
    for (int i = 0; i < 11; ++i) {
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(std::string("IImage_") + names[i], this));
    }
}

void BrewImage::write_image_info(addr_t pInfo) const {
    if (pInfo == 0 || pInfo >= 0xFF000000) {
        return;
    }
    memory_.write_value(pInfo + 0, (uint16_t)width_, EndianMemory::Halfword);
    memory_.write_value(pInfo + 2, (uint16_t)height_, EndianMemory::Halfword);
    memory_.write_value(pInfo + 4, (uint16_t)0, EndianMemory::Halfword); // nColors: truecolor/unknown
    memory_.write_value(pInfo + 6, (uint8_t)(frame_count_ > 1 ? 1 : 0), EndianMemory::Byte);
    memory_.write_value(pInfo + 8, (uint16_t)frame_width_, EndianMemory::Halfword);
}

void BrewImage::fire_notify_callback(addr_t pfn, addr_t pUser, CPU& cpu) {
    if (pfn == 0 || pfn >= 0xFF000000) {
        return;
    }

    addr_t info_ptr = shell_.malloc(10);
    write_image_info(info_ptr);

    std::array<uint32_t, 16> saved_regs{};
    for (int i = 0; i < 16; ++i) {
        saved_regs[static_cast<size_t>(i)] = cpu.get_reg(static_cast<CPUReg>(i));
    }
    const uint32_t saved_cpsr = cpu.get_reg(REG_CPSR);
    const auto saved_pipeline = cpu.pipeline;
    const auto saved_opcode = cpu.opcode;
    const auto saved_carry = cpu.carry;
    const auto saved_irq = cpu.irq;

    constexpr uint32_t magic_ret = 0xDEADBEE0u;
    cpu.set_reg(REG_R0, pUser);
    cpu.set_reg(REG_R1, object_ptr_);
    cpu.set_reg(REG_R2, info_ptr);
    cpu.set_reg(REG_R3, 0);
    cpu.set_reg(REG_LR, magic_ret);
    cpu.set_reg(REG_CPSR, (saved_cpsr & ~0x20u) | ((pfn & 1u) ? 0x20u : 0u));
    cpu.set_reg(REG_PC, pfn & ~1u);

    int guard = 0;
    for (; guard < 100000 && !cpu.is_stopped() && cpu.get_reg(REG_PC) != magic_ret; ++guard) {
        cpu.step_once();
    }
    if (guard >= 100000) {
        printf("  IImage_notify timed out pfn=0x%08x pUser=0x%08x PC=0x%08x LR=0x%08x\n",
               pfn, pUser, cpu.get_reg(REG_PC), cpu.get_reg(REG_LR));
    }

    for (int i = 0; i < 16; ++i) {
        cpu.set_reg(static_cast<CPUReg>(i), saved_regs[static_cast<size_t>(i)]);
    }
    cpu.set_reg(REG_CPSR, saved_cpsr);
    cpu.pipeline = saved_pipeline;
    cpu.opcode = saved_opcode;
    cpu.carry = saved_carry;
    cpu.irq = saved_irq;
}

bool BrewImage::blit_to_guest_bitmap(addr_t dst_obj, int dst_x, int dst_y, int frame_index, CPU& cpu) {
    if (dst_obj == 0 || dst_obj >= 0xFF000000) {
        return false;
    }

    const addr_t vtable = memory_.read_value(dst_obj);
    if (vtable == 0 || vtable >= 0xFF000000) {
        return false;
    }
    const addr_t bltin = memory_.read_value(vtable + 10 * 4);
    if (bltin == 0 || bltin >= 0xFF000000) {
        return false;
    }

    const int src_w = width_;
    const int src_h = height_;
    const int clamped_frame_count = std::max(1, frame_count_);
    const int effective_frame_width = frame_width_ > 0 ? frame_width_ : std::max(1, src_w / clamped_frame_count);
    const int selected_frame = std::max(0, std::min(frame_index, clamped_frame_count - 1));
    const int src_base_x = std::max(0, offset_x_ + selected_frame * effective_frame_width);
    const int src_base_y = std::max(0, offset_y_);
    const int max_src_w = std::max(0, src_w - src_base_x);
    const int max_src_h = std::max(0, src_h - src_base_y);
    const int out_w = std::min(draw_width_ > 0 ? draw_width_ : effective_frame_width, max_src_w);
    const int out_h = std::min(draw_height_ > 0 ? draw_height_ : src_h, max_src_h);
    if (out_w <= 0 || out_h <= 0) {
        return false;
    }

    const uint32_t transparent888 = 0x00ff00ffu;
    BrewBitmap source(shell_, memory_, out_w, out_h, 24, 0, 0, kIdibColorScheme888, transparent888);
    const addr_t dst_base = source.get_buffer_ptr();
    for (int y = 0; y < out_h; ++y) {
        const int sy = std::min(src_h - 1, src_base_y + y);
        for (int x = 0; x < out_w; ++x) {
            const int sx = std::min(src_w - 1, src_base_x + x);
            const uint32_t rgba = rgba_pixels_[(size_t)sy * (size_t)src_w + (size_t)sx];
            const uint8_t r = (uint8_t)(rgba >> 24);
            const uint8_t g = (uint8_t)(rgba >> 16);
            const uint8_t b = (uint8_t)(rgba >> 8);
            const uint8_t a = (uint8_t)(rgba & 0xffu);
            uint8_t out_r = 255;
            uint8_t out_g = 0;
            uint8_t out_b = 255;
            if (a != 0 && !(rop_ == kAeeRoTransparent && r == 255 && g == 0 && b == 255)) {
                out_r = r;
                out_g = g;
                out_b = b;
            }
            // IDIB_COLORSCHEME_888 is exposed as 24bpp BGR bytes by the
            // Qualcomm/BREW DIB path consumed by BJT's guest IBitmap_BltIn.
            const addr_t pixel = dst_base + (uint32_t)y * (uint32_t)source.get_pitch() + (uint32_t)x * 3u;
            memory_.write_value(pixel + 0, out_b, EndianMemory::Byte);
            memory_.write_value(pixel + 1, out_g, EndianMemory::Byte);
            memory_.write_value(pixel + 2, out_r, EndianMemory::Byte);
        }
    }

    std::array<uint32_t, 16> saved_regs{};
    for (int i = 0; i < 16; ++i) {
        saved_regs[static_cast<size_t>(i)] = cpu.get_reg(static_cast<CPUReg>(i));
    }
    const uint32_t saved_cpsr = cpu.get_reg(REG_CPSR);
    const auto saved_pipeline = cpu.pipeline;
    const auto saved_opcode = cpu.opcode;
    const auto saved_carry = cpu.carry;
    const auto saved_irq = cpu.irq;

    const uint32_t call_sp = (saved_regs[static_cast<size_t>(REG_SP)] - 24u) & ~7u;
    memory_.write_value(call_sp + 0, (uint32_t)out_h);
    memory_.write_value(call_sp + 4, source.get_object_ptr());
    memory_.write_value(call_sp + 8, 0u);
    memory_.write_value(call_sp + 12, 0u);
    memory_.write_value(call_sp + 16, rop_);

    constexpr uint32_t magic_ret = 0xDEADBEE0u;
    cpu.set_reg(REG_R0, dst_obj);
    cpu.set_reg(REG_R1, (uint32_t)dst_x);
    cpu.set_reg(REG_R2, (uint32_t)dst_y);
    cpu.set_reg(REG_R3, (uint32_t)out_w);
    cpu.set_reg(REG_SP, call_sp);
    cpu.set_reg(REG_LR, magic_ret);
    cpu.set_reg(REG_CPSR, (saved_cpsr & ~0x20u) | ((bltin & 1u) ? 0x20u : 0u));
    cpu.set_reg(REG_PC, bltin & ~1u);

    const uint64_t pixel_count = static_cast<uint64_t>(out_w) * static_cast<uint64_t>(out_h);
    const uint64_t step_budget64 = std::max<uint64_t>(2000000u, pixel_count * 100u + 200000u);
    const int step_budget = static_cast<int>(std::min<uint64_t>(step_budget64, 60000000u));
    int guard = 0;
    for (; guard < step_budget && !cpu.is_stopped() && cpu.get_reg(REG_PC) != magic_ret; ++guard) {
        cpu.step_once();
    }
    const uint32_t result = cpu.get_reg(REG_R0);
    const uint32_t end_pc = cpu.get_reg(REG_PC);
    const uint32_t end_lr = cpu.get_reg(REG_LR);
    const GuestBitmapProbe dst_probe = probe_bjt_guest_bitmap(memory_, dst_obj);
    const uint32_t dst_nonzero = count_nonzero_guest_probe(memory_, dst_probe);

    for (int i = 0; i < 16; ++i) {
        cpu.set_reg(static_cast<CPUReg>(i), saved_regs[static_cast<size_t>(i)]);
    }
    cpu.set_reg(REG_CPSR, saved_cpsr);
    cpu.pipeline = saved_pipeline;
    cpu.opcode = saved_opcode;
    cpu.carry = saved_carry;
    cpu.irq = saved_irq;

    static int guest_blt_logs = 0;
    if (guest_blt_logs < 16 || std::getenv("ZEEMU_TRACE_HLE") != nullptr) {
        printf("  IImage_Draw via guest IBitmap_BltIn dst=0x%08x src=0x%08x %dx%d src_fmt=888 result=%u steps=%d dst_bits=0x%08x dst_type=%d dst_nonzero=%u\n",
               dst_obj, source.get_object_ptr(), out_w, out_h, result, guard,
               dst_probe.bits, dst_probe.type, dst_nonzero);
        ++guest_blt_logs;
    }
    if (guard >= step_budget) {
        printf("  IImage_Draw guest IBitmap_BltIn timed out dst=0x%08x PC=0x%08x LR=0x%08x\n",
               dst_obj, end_pc, end_lr);
    }
    return true;
}

void BrewImage::blit_to_device(int dst_x, int dst_y, int frame_index, CPU* cpu) {
    BrewDisplay* display = shell_.get_display();
    if (!display) {
        return;
    }

    const addr_t destination = display->get_destination_ptr();
    if (cpu && destination != 0 && lookup_brew_bitmap(destination) == nullptr &&
        blit_to_guest_bitmap(destination, dst_x, dst_y, frame_index, *cpu)) {
        return;
    }

    BrewBitmap* bitmap = display->get_destination_bitmap();
    if (!bitmap) {
        return;
    }

    const int dst_w = bitmap->get_width();
    const int dst_h = bitmap->get_height();
    const int dst_depth = bitmap->get_depth();
    if (dst_depth != 16) {
        return;
    }

    const addr_t dst_base = bitmap->get_buffer_ptr();
    const int src_w = width_;
    const int src_h = height_;
    const int clamped_frame_count = std::max(1, frame_count_);
    const int effective_frame_width = frame_width_ > 0 ? frame_width_ : std::max(1, src_w / clamped_frame_count);
    const int selected_frame = std::max(0, std::min(frame_index, clamped_frame_count - 1));
    const int src_base_x = std::max(0, offset_x_ + selected_frame * effective_frame_width);
    const int src_base_y = std::max(0, offset_y_);
    const int max_src_w = std::max(0, src_w - src_base_x);
    const int max_src_h = std::max(0, src_h - src_base_y);
    const int out_w = std::min(draw_width_ > 0 ? draw_width_ : effective_frame_width, max_src_w);
    const int out_h = std::min(draw_height_ > 0 ? draw_height_ : src_h, max_src_h);
    if (out_w <= 0 || out_h <= 0) {
        return;
    }

    if (!logged_blit_) {
        printf("[IImage] blit '%s' src=(%d,%d %dx%d) frame=%d rop=%u -> at (%d,%d)\n",
               source_path_.c_str(), src_base_x, src_base_y, out_w, out_h, selected_frame, rop_, dst_x, dst_y);
        logged_blit_ = true;
    }

    int clip_x = 0, clip_y = 0, clip_dx = dst_w, clip_dy = dst_h;
    display->get_clip_rect(clip_x, clip_y, clip_dx, clip_dy);
    const int clip_left = std::max(0, clip_x);
    const int clip_top = std::max(0, clip_y);
    const int clip_right = std::min(dst_w, clip_x + std::max(0, clip_dx));
    const int clip_bottom = std::min(dst_h, clip_y + std::max(0, clip_dy));

    for (int y = 0; y < out_h; ++y) {
        const int sy = std::min(src_h - 1, src_base_y + y);
        const int dy = dst_y + y;
        if (dy < clip_top || dy >= clip_bottom) {
            continue;
        }
        for (int x = 0; x < out_w; ++x) {
            const int sx = std::min(src_w - 1, src_base_x + x);
            const int dx = dst_x + x;
            if (dx < clip_left || dx >= clip_right) {
                continue;
            }

            const uint32_t rgba = rgba_pixels_[(size_t)sy * (size_t)src_w + (size_t)sx];
            const uint8_t r = (uint8_t)(rgba >> 24);
            const uint8_t g = (uint8_t)(rgba >> 16);
            const uint8_t b = (uint8_t)(rgba >> 8);
            const uint8_t a = (uint8_t)(rgba & 0xffu);
            if (a == 0 || (rop_ == kAeeRoTransparent && r == 255 && g == 0 && b == 255)) {
                continue;
            }

            const uint16_t src565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            const addr_t dst_pixel = dst_base + ((dy * dst_w + dx) * 2);
            if (rop_ == kAeeRoBlend && a < 255) {
                const uint16_t dst565 = (uint16_t)memory_.read_value(dst_pixel, EndianMemory::Halfword);
                const uint8_t dr = (uint8_t)(((dst565 >> 11) & 0x1f) << 3);
                const uint8_t dg = (uint8_t)(((dst565 >> 5) & 0x3f) << 2);
                const uint8_t db = (uint8_t)((dst565 & 0x1f) << 3);
                const uint8_t br = (uint8_t)((r * a + dr * (255 - a)) / 255);
                const uint8_t bg = (uint8_t)((g * a + dg * (255 - a)) / 255);
                const uint8_t bb = (uint8_t)((b * a + db * (255 - a)) / 255);
                const uint16_t blended = (uint16_t)(((br >> 3) << 11) | ((bg >> 2) << 5) | (bb >> 3));
                memory_.write_value(dst_pixel, blended, EndianMemory::Halfword);
            } else {
                memory_.write_value(dst_pixel, src565, EndianMemory::Halfword);
            }
        }
    }
}

void BrewImage::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IImage_AddRef") {
        cpu.set_reg(REG_R0, ++refs_);
    } else if (name == "IImage_Release") {
        if (refs_ > 0) {
            --refs_;
        }
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IImage_QueryInterface") {
        uint32_t pp = r2;
        if (pp && pp < 0xFF000000) {
            memory_.write_value(pp, r0);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_Draw") {
        blit_to_device((int)r1, (int)r2, 0, &cpu);
        drawn_ = true;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_DrawFrame") {
        blit_to_device((int)r2, (int)r3, (int)r1, &cpu);
        drawn_ = true;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_GetInfo") {
        uint32_t pInfo = r1;
        write_image_info(pInfo);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_SetParm") {
        // Keep the image usable even if callers set these dynamically later.
        uint32_t parm = r1;
        int32_t value1 = (int32_t)r2;
        int32_t value2 = (int32_t)r3;
        if (parm == 0x00) { // IPARM_SIZE
            draw_width_ = std::max(1, value1);
            draw_height_ = std::max(1, value2);
        } else if (parm == 0x01) { // IPARM_OFFSET
            offset_x_ = value1;
            offset_y_ = value2;
        } else if (parm == 0x02) { // IPARM_CXFRAME
            frame_width_ = value1 > 0 ? value1 : height_;
            frame_count_ = frame_width_ > 0 ? std::max(1, width_ / frame_width_) : 1;
            draw_width_ = std::max(1, std::min(frame_width_, width_));
        } else if (parm == 0x03) { // IPARM_ROP
            rop_ = (uint32_t)value1;
        } else if (parm == 0x04) { // IPARM_NFRAMES
            frame_count_ = value1 > 0 ? value1 : std::max(1, width_ / std::max(1, height_));
            frame_width_ = std::max(1, width_ / std::max(1, frame_count_));
            draw_width_ = std::max(1, frame_width_);
        } else if (parm == 0x06) { // IPARM_OFFSCREEN
            offscreen_ = (value1 != 0);
        } else if (parm == 0x0c) { // IPARM_SCALE
            draw_width_ = std::max(1, value1);
            draw_height_ = std::max(1, value2);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_Start" || name == "IImage_HandleEvent") {
        if (!drawn_ && !offscreen_) {
            blit_to_device(offset_x_, offset_y_);
            drawn_ = true;
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_Stop") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_SetStream") {
        std::vector<uint8_t> encoded;
        BrewMemAStream* stream = shell_.find_mem_astream(r1);
        if (stream && stream->read_remaining_from_current(encoded) &&
            load_encoded_image(encoded.data(), encoded.size())) {
            printf("  IImage_SetStream image='%s' stream=0x%08x bytes=%zu -> %dx%d\n",
                   source_path_.c_str(), r1, encoded.size(), width_, height_);
            if (notify_pfn_ != 0) {
                fire_notify_callback(notify_pfn_, notify_user_, cpu);
            }
        } else {
            printf("  IImage_SetStream image='%s' stream=0x%08x decode unavailable\n",
                   source_path_.c_str(), r1);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IImage_Notify") {
        notify_pfn_ = r1;
        notify_user_ = r2;
        printf("  IImage_Notify pfn=0x%08x pUser=0x%08x image='%s' %dx%d loaded=%d\n",
               r1, r2, source_path_.c_str(), width_, height_, loaded_image_ ? 1 : 0);
        if (loaded_image_) {
            fire_notify_callback(r1, r2, cpu);
        }
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet image='%s' R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), source_path_.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
