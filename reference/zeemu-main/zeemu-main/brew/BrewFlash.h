#ifndef ZEEMU_BREW_FLASH_H_
#define ZEEMU_BREW_FLASH_H_

#include "brew/BrewService.h"
#include "cpu/cpu.h"
#include "cpu/memory/EndianMemory.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class BrewShell;

class BrewFlash : public BrewService {
public:
    BrewFlash(BrewShell& shell, EndianMemory& memory);

    addr_t create_instance(uint32_t clsid);
    addr_t get_player_object_ptr() const { return player_object_ptr_; }
    void handle_hook(const std::string& name, class CPU& cpu) override;

private:
    struct DataObject {
        addr_t object = 0;
        std::string data;
    };

    void setup_vtables();
    addr_t create_data_object(std::string data);
    DataObject* find_data(addr_t object);
    void write_string_result(addr_t dst, int dst_len, addr_t required_len, const std::string& value);
    void signal_event_if_ready();

    BrewShell& shell_;
    EndianMemory& memory_;

    addr_t player_object_ptr_ = 0;
    addr_t player_vtable_ptr_ = 0;
    addr_t content_object_ptr_ = 0;
    addr_t content_vtable_ptr_ = 0;
    addr_t data_vtable_ptr_ = 0;

    addr_t event_signal_ = 0;
    uint32_t pending_events_ = 0;
    bool loaded_ = false;
    bool playing_ = false;
    std::string url_;
    std::string return_value_;
    std::map<std::string, std::string> variables_;
    uint32_t background_color_ = 0xffffffffu;
    uint32_t script_access_ = 0; // IFlashContent_Always
    uint32_t scale_mode_ = 0;    // IFlashContent_ShowAll
    uint32_t align_mode_ = 0;    // IFlashContent_Align_Left
    std::vector<DataObject> data_objects_;
};

#endif
