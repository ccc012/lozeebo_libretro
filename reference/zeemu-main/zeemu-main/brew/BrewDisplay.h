#ifndef ZEEMU_BREW_DISPLAY_H_
#define ZEEMU_BREW_DISPLAY_H_

#include "brew/BrewShell.h"
#include "brew/BrewBitmap.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

class BrewDisplay : public BrewService {
public:
    BrewDisplay(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

    BrewBitmap* get_device_bitmap() { return device_bitmap_.get(); }
    BrewBitmap* get_destination_bitmap();
    addr_t get_destination_ptr() const { return destination_ptr_; }
    void draw_text_to_device_bitmap(int x, int y, const std::string& text);
    // Same glyph renderer with an explicit BREW RGBVAL color (r<<8|g<<16|b<<24)
    // instead of the current CLR_USER_TEXT, for controls that own their colors
    // (e.g. IMenuCtl with IMENUCTL_SetColors overrides).
    void draw_text_to_device_bitmap_rgb(int x, int y, const std::string& text, uint32_t rgb);
    // Solid RGBVAL fill on the destination bitmap (16bpp), clipped to bounds.
    void fill_rect_device(int x, int y, int dx, int dy, uint32_t rgb);
    void get_clip_rect(int& x, int& y, int& dx, int& dy) const {
        x = clip_x_;
        y = clip_y_;
        dx = clip_dx_;
        dy = clip_dy_;
    }

private:
    void setup_vtable();
    uint16_t rgb_to_native(uint32_t rgb) const;

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_;
    addr_t vtable_ptr_;
    std::unique_ptr<BrewBitmap> device_bitmap_;
    addr_t draw_rect_tmp_ = 0;
    std::array<addr_t, 8> current_font_ptrs_{};
    uint32_t user_text_color_ = 0xffffff00;
    uint32_t user_background_color_ = 0x00000000;
    uint32_t user_line_color_ = 0xffffff00;
    addr_t destination_ptr_ = 0;
    addr_t last_offscreen_destination_ptr_ = 0;
    std::vector<std::unique_ptr<BrewBitmap>> dib_bitmaps_;
    int16_t clip_x_ = 0;
    int16_t clip_y_ = 0;
    int16_t clip_dx_ = 640;
    int16_t clip_dy_ = 480;
};

#endif
