#ifndef ZEEMU_BREW_HID_H_
#define ZEEMU_BREW_HID_H_

#include "brew/BrewShell.h"
#include <array>
#include <functional>
#include <queue>

struct HIDButtonEvent {
    uint32_t id;
    uint32_t uid;
    bool down;
};

class BrewHID : public BrewService {
public:
    BrewHID(BrewShell& shell, EndianMemory& memory);

    [[nodiscard]] addr_t get_object_ptr() const { return object_ptr_; }
    void push_button_event(uint32_t uid, bool down, uint32_t device_index = 0);
    void push_keyboard_event(uint32_t key_id, bool down);
    void set_axis_value(uint32_t axis_uid, uint32_t value, uint32_t device_index = 0);
    void set_rumble_callback(std::function<void(uint32_t, uint32_t, uint32_t)> callback);
    [[nodiscard]] bool default_key_events_enabled(uint32_t device_index = 0) const;
    void handle_hook(const std::string& name, CPU& cpu) override;

private:
    struct DeviceState {
        uint32_t handle = 0;
        uint32_t device_type = 0;
        addr_t object_ptr = 0;
        addr_t button_signal = 0;
        addr_t position_signal = 0;
        uint32_t exclusive_level = 0;
        std::queue<HIDButtonEvent> button_events;
        std::array<bool, 256> button_down{};
        uint32_t axis_left_x = 0x8000;
        uint32_t axis_left_y = 0x8000;
        uint32_t axis_right_x = 0x8000;
        uint32_t axis_right_y = 0x8000;
        uint32_t rumble_left = 0;
        uint32_t rumble_right = 0;
    };

    DeviceState& device_for_object(addr_t object_ptr);
    DeviceState& device_for_handle(uint32_t handle);
    uint32_t device_index_for_object(addr_t object_ptr) const;
    bool handle_is_connected(uint32_t handle) const;
    void signal_position_change(DeviceState& device);

    BrewShell& shell_;
    EndianMemory& memory_;
    addr_t object_ptr_ = 0;
    addr_t vtable_ptr_ = 0;
    addr_t dev_vtable_ptr_ = 0;
    addr_t connect_signal_ = 0;
    std::array<DeviceState, 3> devices_;
    uint32_t connected_device_count_ = 1;
    std::function<void(uint32_t, uint32_t, uint32_t)> rumble_callback_;
};

#endif
