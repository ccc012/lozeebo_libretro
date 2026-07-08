#ifndef ZEEMU_BREW_ZWHEEL_OEM_H_
#define ZEEMU_BREW_ZWHEEL_OEM_H_

#include "brew/BrewService.h"
#include "cpu/memory/EndianMemory.h"
#include <string>
#include <vector>

class BrewShell;

class BrewZWheelOem : public BrewService {
public:
    BrewZWheelOem(BrewShell& shell, EndianMemory& memory);

    addr_t get_config_object_ptr() const { return config_object_ptr_; }
    addr_t get_root_form_object_ptr() const { return root_form_object_ptr_; }
    addr_t get_mcp_object_ptr() const { return mcp_object_ptr_; }
    addr_t get_telemetry_object_ptr() const { return telemetry_object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    void setup_vtables();
    std::string read_guest_string(addr_t addr) const;

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t config_object_ptr_ = 0;
    addr_t config_vtable_ptr_ = 0;
    addr_t root_form_object_ptr_ = 0;
    addr_t root_form_vtable_ptr_ = 0;
    addr_t mcp_object_ptr_ = 0;
    addr_t mcp_vtable_ptr_ = 0;
    addr_t telemetry_object_ptr_ = 0;
    addr_t telemetry_vtable_ptr_ = 0;
    uint32_t telemetry_send_count_ = 0;
    std::vector<addr_t> config_line_ptrs_;
    size_t config_line_index_ = 0;
};

#endif
