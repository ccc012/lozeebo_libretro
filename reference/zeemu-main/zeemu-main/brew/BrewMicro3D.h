#ifndef ZEEMU_BREW_MICRO3D_H_
#define ZEEMU_BREW_MICRO3D_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <string>
#include <unordered_map>
#include <vector>

class BrewShell;

class BrewMicro3D : public BrewService {
public:
    BrewMicro3D(BrewShell& shell, EndianMemory& memory);

    addr_t get_object_ptr() const { return object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct Surface {
        int width = 0;
        int height = 0;
        int dst_x0 = 0;
        int dst_y0 = 0;
        int dst_x1 = 0;
        int dst_y1 = 0;
        std::vector<uint16_t> rgb565;
    };

    void setup_vtable();
    void upload_surface(addr_t object_ptr, CPU& cpu);
    void set_destination_rect(addr_t object_ptr, CPU& cpu);
    void blit_surface_to_display(addr_t object_ptr);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    std::unordered_map<addr_t, Surface> surfaces_;
};

#endif
