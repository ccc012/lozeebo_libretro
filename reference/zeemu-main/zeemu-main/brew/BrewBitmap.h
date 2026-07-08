#ifndef ZEEMU_BREW_BITMAP_H_
#define ZEEMU_BREW_BITMAP_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <memory>
#include <string>
#include <vector>

class BrewBitmap : public BrewService {
public:
    BrewBitmap(BrewShell& shell, EndianMemory& memory, int width, int height, int depth,
               int palette_entries = 0, int extra_bytes = 0, int color_scheme = -1,
               uint32_t transparent_color = 0xffffffffu);
    ~BrewBitmap() override;

    addr_t get_object_ptr() const { return object_ptr_; }
    addr_t get_transform_object_ptr() const { return transform_object_ptr_; }
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    int get_depth() const { return depth_; }
    int get_pitch() const { return pitch_; }
    addr_t get_buffer_ptr() const { return buffer_ptr_; }
    uint32_t get_transparency_color() const { return transparent_color_; }
    size_t get_buffer_size() const { return (size_t)pitch_ * (size_t)height_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    void setup_transform_vtable();
    void handle_transform_hook(const std::string& name, class CPU& cpu);
    bool handle_color_hook(const std::string& name, class CPU& cpu);
    bool handle_pixel_hook(const std::string& name, class CPU& cpu);
    bool handle_blit_hook(const std::string& name, class CPU& cpu);
    bool handle_info_hook(const std::string& name, class CPU& cpu);
    void write_dib_fields();
    uint32_t blit_from_bitmap(BrewBitmap* src, int dst_x, int dst_y, int src_x, int src_y,
                              int copy_w, int copy_h, int scale_x, int scale_y, uint32_t composite);
    uint32_t transform_blit_from_bitmap(BrewBitmap* src, int dst_x, int dst_y,
                                        int src_x, int src_y, int copy_w, int copy_h,
                                        int a, int b, int c, int d, uint32_t composite);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
    addr_t transform_object_ptr_ = 0;
    addr_t transform_vtable_ptr_ = 0;
    int width_;
    int height_;
    int depth_;
    int pitch_;
    int palette_entries_;
    int extra_bytes_;
    int color_scheme_;
    addr_t buffer_ptr_;
    addr_t palette_ptr_ = 0;
    uint32_t transparent_color_;
    std::vector<std::unique_ptr<BrewBitmap>> children_;
};

BrewBitmap* lookup_brew_bitmap(addr_t object_ptr);

#endif
