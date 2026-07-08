#ifndef ZEEMU_BREW_GRAPHICS_H_
#define ZEEMU_BREW_GRAPHICS_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>

class BrewGraphics : public BrewService {
public:
    BrewGraphics(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtable();
    static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    addr_t destination_ptr_ = 0;
    int16_t viewport_x_ = 0;
    int16_t viewport_y_ = 0;
    int16_t viewport_w_ = 0;
    int16_t viewport_h_ = 0;
    int16_t translate_x_ = 0;
    int16_t translate_y_ = 0;
    uint8_t background_r_ = 0;
    uint8_t background_g_ = 0;
    uint8_t background_b_ = 0;
    uint8_t color_r_ = 255;
    uint8_t color_g_ = 255;
    uint8_t color_b_ = 255;
    uint8_t color_a_ = 255;
    uint8_t fill_r_ = 255;
    uint8_t fill_g_ = 255;
    uint8_t fill_b_ = 255;
    uint8_t fill_a_ = 255;
    bool fill_mode_ = false;
    uint8_t point_size_ = 1;
    uint8_t paint_mode_ = 0;
    uint8_t algorithm_hint_ = 0;
    uint8_t stroke_style_ = 0;
};

#endif
