#ifndef ZEEMU_BREW_IMAGE_H_
#define ZEEMU_BREW_IMAGE_H_

#include "brew/BrewShell.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>
#include <vector>

class BrewImage : public BrewService {
public:
    BrewImage(BrewShell& shell, EndianMemory& memory, const std::string& virtual_path, const std::string& current_directory);
    BrewImage(BrewShell& shell, EndianMemory& memory, const std::string& source_label, const uint8_t* data, size_t size);

    addr_t get_object_ptr() const { return object_ptr_; }
    void draw_at(int x, int y) { blit_to_device(x, y); drawn_ = true; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    bool load_encoded_image(const uint8_t* data, size_t size);
    void setup_vtable();
    void blit_to_device(int dst_x, int dst_y, int frame_index = 0, class CPU* cpu = nullptr);
    bool blit_to_guest_bitmap(addr_t dst_obj, int dst_x, int dst_y, int frame_index, class CPU& cpu);
    void write_image_info(addr_t pInfo) const;
    void fire_notify_callback(addr_t pfn, addr_t pUser, class CPU& cpu);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::string source_path_;
    int width_ = 0;
    int height_ = 0;
    int src_channels_ = 0;
    int draw_width_ = 0;
    int draw_height_ = 0;
    int offset_x_ = 0;
    int offset_y_ = 0;
    int frame_width_ = 0;
    int frame_count_ = 1;
    uint32_t rop_ = 2;
    bool offscreen_ = false;
    bool drawn_ = false;
    bool logged_blit_ = false;
    bool loaded_image_ = false;
    addr_t notify_pfn_ = 0;
    addr_t notify_user_ = 0;
    uint32_t refs_ = 1;
    std::vector<uint32_t> rgba_pixels_;
};

#endif
