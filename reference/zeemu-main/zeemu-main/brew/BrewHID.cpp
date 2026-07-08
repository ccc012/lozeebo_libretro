#include "brew/BrewHID.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace {
uint32_t button_id_from_uid(uint32_t uid) {
    switch (uid) {
        case 0x0106C40A: return 0;  // AEEUID_HIDJoystick_Button_1
        case 0x0106C40B: return 1;  // AEEUID_HIDJoystick_Button_2
        case 0x0106C40C: return 2;  // AEEUID_HIDJoystick_Button_3
        case 0x0106C40D: return 3;  // AEEUID_HIDJoystick_Button_4
        case 0x0106C3FE: return 12; // DPad Up
        case 0x0106C3FF: return 13; // DPad Left
        case 0x0106C400: return 14; // DPad Down
        case 0x0106C401: return 15; // DPad Right
        case 0x0106C406: return 4;  // Left shoulder upper / Zeebo ZL
        case 0x0106C408: return 5;  // Right shoulder upper / Zeebo ZR
        case 0x0106C407: return 6;  // Left shoulder lower
        case 0x0106C409: return 7;  // Right shoulder lower
        case 0x0106C403: return 8;  // Zeebo HOME: AEEHIDButton_Uid2Id maps Back to ZEEBO_BUTTON_HOME.
        case 0x0106C402: return 9;  // Joystick Start exists in HID, but is not the Zeebo HOME button.
        case 0x0106C404: return 10; // Left thumbstick button
        case 0x0106C405: return 11; // Right thumbstick button
        default: return 0xffffffffu;
    }
}

uint32_t uid_from_button_id(uint32_t id) {
    switch (id) {
        case 0: return 0x0106C40A;  // AEEUID_HIDJoystick_Button_1
        case 1: return 0x0106C40B;  // AEEUID_HIDJoystick_Button_2
        case 2: return 0x0106C40C;  // AEEUID_HIDJoystick_Button_3
        case 3: return 0x0106C40D;  // AEEUID_HIDJoystick_Button_4
        case 4: return 0x0106C406;  // Left shoulder upper / Zeebo ZL
        case 5: return 0x0106C408;  // Right shoulder upper / Zeebo ZR
        case 6: return 0x0106C407;  // Left shoulder lower
        case 7: return 0x0106C409;  // Right shoulder lower
        case 8: return 0x0106C403;  // Zeebo HOME.
        case 9: return 0x0106C402;  // Generic joystick Start.
        case 10: return 0x0106C404; // Left thumbstick button
        case 11: return 0x0106C405; // Right thumbstick button
        case 12: return 0x0106C3FE; // DPad Up
        case 13: return 0x0106C3FF; // DPad Left
        case 14: return 0x0106C400; // DPad Down
        case 15: return 0x0106C401; // DPad Right
        default: return 0;
    }
}

constexpr uint32_t AEE_SUCCESS = 0;
constexpr uint32_t AEE_EBADPARM = 14;
constexpr uint32_t AEE_ENOSUCH = 39;
constexpr uint32_t kHidMouseDevice = 0x0106C3FBu;
constexpr uint32_t kHidKeyboardDevice = 0x0106C3FCu;
constexpr uint32_t kHidJoystickDevice = 0x0106C3FDu;
constexpr uint32_t kHidButtonCount = 16;
constexpr uint32_t kKeyboardButtonCount = 256;
constexpr uint32_t kHidRumbleMax = 0xffffu;
constexpr uint32_t kHidUnsupportedAxis = 0xffffffffu; // AEEHIDPositionInfo axis UID -1.
constexpr uint32_t kKeyboardDeviceIndex = 2;

enum HidPositionOffset : uint32_t {
    kPosRelativeAxes = 0,
    kPosX = 4,
    kPosY = 8,
    kPosZ = 12,
    kPosRx = 16,
    kPosRy = 20,
    kPosRz = 24,
    kPosSize = 100,
};

void write_button_info(EndianMemory& memory, addr_t pInfo, uint32_t id, uint32_t uid, bool down) {
    if (pInfo == 0 || pInfo >= 0xFF000000) {
        return;
    }
    memory.write_value(pInfo + 0, id);
    memory.write_value(pInfo + 4, down ? 1u : 0u);
    memory.write_value(pInfo + 8, uid);
    memory.write_value(pInfo + 12, 0u);
    memory.write_value(pInfo + 16, 1u);
}

void write_button_info_from_uid(EndianMemory& memory, addr_t pInfo, uint32_t uid, bool down) {
    const uint32_t id = button_id_from_uid(uid);
    write_button_info(memory, pInfo, id == 0xffffffffu ? 0 : id, uid, down);
}

bool is_supported_device_type(uint32_t device_type) {
    return device_type == 0 ||
           device_type == kHidJoystickDevice ||
           device_type == kHidKeyboardDevice ||
           device_type == kHidMouseDevice;
}

void clear_position_info(EndianMemory& memory, addr_t pPos) {
    for (uint32_t offset = 0; offset < kPosSize; offset += 4) {
        memory.write_value(pPos + offset, 0u);
    }
}
}

BrewHID::BrewHID(BrewShell& shell, EndianMemory& memory) : shell_(shell), memory_(memory) {
    vtable_ptr_ = shell_.malloc(8 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    for (int i = 0; i < 8; ++i) {
        const char* names[] = {
            "AddRef", "Release", "QueryInterface", "CreateDevice", "GetDeviceInfo",
            "GetNextConnectEvent", "RegisterForConnectEvents", "GetConnectedDevices",
        };
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(std::string("IHID_") + names[i], this));
    }

    dev_vtable_ptr_ = shell_.malloc(19 * 4);
    for (int i = 0; i < 19; ++i) {
        const char* dev_names[] = {
            "AddRef", "Release", "QueryInterface", "GetDeviceInfo", "GetDeviceStatus",
            "RegisterForStatusChange", "GetButtonInfo", "GetNumberOfButtons",
            "RegisterForButtonEvent", "GetNextButtonEvent", "GetPositionState",
            "GetMinPositionInfo", "GetMaxPositionInfo", "GetAxesInfo",
            "RegisterForPositionChange", "SetExclusiveLevel", "GetExclusiveLevel",
            "Rumble", "GetRumbleStatus"
        };
        memory_.write_value(dev_vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(std::string("IHIDDevice_") + dev_names[i], this));
    }
    for (uint32_t i = 0; i < devices_.size(); ++i) {
        devices_[i].handle = 0x1234u + i;
        devices_[i].device_type = kHidJoystickDevice;
        devices_[i].object_ptr = shell_.malloc(4);
        memory_.write_value(devices_[i].object_ptr, dev_vtable_ptr_);
    }
    devices_[kKeyboardDeviceIndex].handle = 0x2234u;
    devices_[kKeyboardDeviceIndex].device_type = kHidKeyboardDevice;
    if (const char* count_env = std::getenv("ZEEMU_HID_DEVICE_COUNT")) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(count_env, &end, 0);
        if (end != count_env && parsed > 0) {
            connected_device_count_ = std::min<uint32_t>(static_cast<uint32_t>(parsed),
                                                         kKeyboardDeviceIndex);
        }
    }
}

auto BrewHID::device_for_object(addr_t object_ptr) -> DeviceState& {
    for (auto& device : devices_) {
        if (device.object_ptr == object_ptr) {
            return device;
        }
    }
    return devices_[0];
}

auto BrewHID::device_for_handle(uint32_t handle) -> DeviceState& {
    for (auto& device : devices_) {
        if (device.handle == handle) {
            return device;
        }
    }
    return devices_[0];
}

uint32_t BrewHID::device_index_for_object(addr_t object_ptr) const {
    for (uint32_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].object_ptr == object_ptr) {
            return i;
        }
    }
    return 0;
}

bool BrewHID::handle_is_connected(uint32_t handle) const {
    const uint32_t count = std::min<uint32_t>(connected_device_count_,
                                             static_cast<uint32_t>(devices_.size()));
    for (uint32_t i = 0; i < count; ++i) {
        if (devices_[i].handle == handle) {
            return true;
        }
    }
    if (devices_[kKeyboardDeviceIndex].handle == handle) {
        return true;
    }
    return false;
}

void BrewHID::set_rumble_callback(std::function<void(uint32_t, uint32_t, uint32_t)> callback) {
    rumble_callback_ = std::move(callback);
}

bool BrewHID::default_key_events_enabled(uint32_t device_index) const {
    if (device_index >= devices_.size()) {
        return true;
    }
    return devices_[device_index].exclusive_level == 0;
}

void BrewHID::signal_position_change(DeviceState& device) {
    if (device.position_signal != 0) {
        shell_.set_signal(device.position_signal, "hid-position");
    }
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] HID position handle=0x%04x lx=0x%04x ly=0x%04x rx=0x%04x ry=0x%04x signal=0x%08x\n",
               device.handle,
               device.axis_left_x,
               device.axis_left_y,
               device.axis_right_x,
               device.axis_right_y,
               device.position_signal);
    }
}

void BrewHID::push_button_event(uint32_t uid, bool down, uint32_t device_index) {
    DeviceState& device = devices_[std::min<uint32_t>(device_index, static_cast<uint32_t>(devices_.size() - 1))];
    const uint32_t id = button_id_from_uid(uid);
    if (id < device.button_down.size()) {
        device.button_down[id] = down;
    }
    device.button_events.push({id, uid, down});
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] HID queue dev=%u handle=0x%04x button id=%u uid=0x%08x down=%u pending=%zu signal=0x%08x\n",
               static_cast<unsigned>(std::min<uint32_t>(device_index, static_cast<uint32_t>(devices_.size() - 1))),
               device.handle,
               id,
               uid,
               down ? 1u : 0u,
               device.button_events.size(),
               device.button_signal);
    }
    if (device.button_signal != 0) {
        shell_.set_signal(device.button_signal, "hid-button");
    }
}

void BrewHID::push_keyboard_event(uint32_t key_id, bool down) {
    if (key_id >= kKeyboardButtonCount) {
        return;
    }
    DeviceState& device = devices_[kKeyboardDeviceIndex];
    device.button_down[key_id] = down;
    device.button_events.push({key_id, key_id, down});
    if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
        printf("[INPUT_TRACE] HID keyboard queue handle=0x%04x id=%u down=%u pending=%zu signal=0x%08x\n",
               device.handle,
               key_id,
               down ? 1u : 0u,
               device.button_events.size(),
               device.button_signal);
    }
    if (device.button_signal != 0) {
        shell_.set_signal(device.button_signal, "hid-keyboard");
    }
}

void BrewHID::set_axis_value(uint32_t axis_uid, uint32_t value, uint32_t device_index) {
    DeviceState& device = devices_[std::min<uint32_t>(device_index, static_cast<uint32_t>(devices_.size() - 1))];
    value = std::min<uint32_t>(value, 0xffffu);

    uint32_t* target = nullptr;
    switch (axis_uid) {
        case 0x0106C4D0: target = &device.axis_left_x; break;  // AEEUID_HIDJoystick_LeftThumb_X
        case 0x0106C4D1: target = &device.axis_left_y; break;  // AEEUID_HIDJoystick_LeftThumb_Y
        case 0x0106C4CE: target = &device.axis_right_x; break; // AEEUID_HIDJoystick_RightThumb_X
        case 0x0106C4CF: target = &device.axis_right_y; break; // AEEUID_HIDJoystick_RightThumb_Y
        default:
            if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                printf("[INPUT_TRACE] HID axis ignored dev=%u uid=0x%08x value=0x%04x\n",
                       static_cast<unsigned>(device_index),
                       axis_uid,
                       value);
            }
            return;
    }

    if (*target == value) {
        return;
    }
    *target = value;
    signal_position_change(device);
}

void BrewHID::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t sp = cpu.get_reg(REG_SP);

    if (name == "IHID_AddRef" || name == "IHIDDevice_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IHID_Release" || name == "IHIDDevice_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHID_QueryInterface" || name == "IHIDDevice_QueryInterface") {
        uint32_t pp = r2;
        if (pp && pp < 0xFF000000) memory_.write_value(pp, r0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHID_GetConnectedDevices") {
        uint32_t pHandles = r2;
        uint32_t nMaxHandles = r3;
        uint32_t pnReq = memory_.read_value(sp);
        const bool supported_type = is_supported_device_type(r1);
        std::array<uint32_t, 3> handles{};
        uint32_t count = 0;
        if (supported_type) {
            if (r1 == 0 || r1 == kHidJoystickDevice) {
                const uint32_t joystick_count = std::min<uint32_t>(connected_device_count_, kKeyboardDeviceIndex);
                for (uint32_t i = 0; i < joystick_count; ++i) {
                    handles[count++] = devices_[i].handle;
                }
            }
            if (r1 == 0 || r1 == kHidKeyboardDevice) {
                handles[count++] = devices_[kKeyboardDeviceIndex].handle;
            }
        }
        if (pnReq && pnReq < 0xFF000000) memory_.write_value(pnReq, count);
        if (pHandles && pHandles < 0xFF000000) {
            const uint32_t written = std::min<uint32_t>(nMaxHandles, count);
            for (uint32_t i = 0; i < written; ++i) {
                memory_.write_value(pHandles + i * 4u, handles[i]);
            }
        }
        printf("  IHID_GetConnectedDevices type=0x%08x max=%d -> %u devices (connected=%u req_ptr=0x%x)\n",
               r1,
               nMaxHandles,
               count,
               connected_device_count_,
               pnReq);
        cpu.set_reg(REG_R0, supported_type ? AEE_SUCCESS : AEE_EBADPARM);
    } else if (name == "IHID_GetDeviceInfo") {
        uint32_t pInfo = r2;
        if (!handle_is_connected(r1)) {
            printf("  IHID_GetDeviceInfo handle=0x%08x -> ENOSUCH\n", r1);
            cpu.set_reg(REG_R0, AEE_ENOSUCH);
            return;
        }
        if (pInfo && pInfo < 0xFF000000) {
            DeviceState& device = device_for_handle(r1);
            memory_.write_value(pInfo + 0, device.device_type);
            memory_.write_value(pInfo + 4, static_cast<uint16_t>(0x1234), EndianMemory::Halfword);
            memory_.write_value(pInfo + 6, static_cast<uint16_t>(0x5678), EndianMemory::Halfword);
            memory_.write_value(pInfo + 8, static_cast<uint8_t>(0), EndianMemory::Byte);
        }
        printf("  IHID_GetDeviceInfo handle=0x%08x info=0x%08x\n", r1, pInfo);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHID_GetNextConnectEvent") {
        if (r1 && r1 < 0xFF000000) memory_.write_value(r1, 0);
        if (r2 && r2 < 0xFF000000) memory_.write_value(r2, 0);
        if (r3 && r3 < 0xFF000000) memory_.write_value(r3, static_cast<uint8_t>(0), EndianMemory::Byte);
        cpu.set_reg(REG_R0, 5);
    } else if (name == "IHID_RegisterForConnectEvents") {
        connect_signal_ = r1;
        printf("  IHID_RegisterForConnectEvents signal=0x%x\n", connect_signal_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHID_CreateDevice") {
        uint32_t ppObj = r2;
        if (!handle_is_connected(r1)) {
            if (ppObj && ppObj < 0xFF000000) memory_.write_value(ppObj, 0);
            printf("  IHID_CreateDevice handle=0x%x -> ENOSUCH\n", r1);
            cpu.set_reg(REG_R0, AEE_ENOSUCH);
            return;
        }
        DeviceState& device = device_for_handle(r1);
        if (ppObj && ppObj < 0xFF000000) memory_.write_value(ppObj, device.object_ptr);
        printf("  IHID_CreateDevice handle=0x%x -> SUCCESS, obj=0x%x\n", r1, device.object_ptr);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_GetDeviceInfo") {
        uint32_t pInfo = r1;
        if (pInfo && pInfo < 0xFF000000) {
            DeviceState& device = device_for_object(r0);
            memory_.write_value(pInfo + 0, device.device_type);
            memory_.write_value(pInfo + 4, static_cast<uint16_t>(0x1234), EndianMemory::Halfword);
            memory_.write_value(pInfo + 6, static_cast<uint16_t>(0x5678), EndianMemory::Halfword);
            memory_.write_value(pInfo + 8, static_cast<uint8_t>(0), EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_GetDeviceStatus") {
        uint32_t pStatus = r1;
        if (pStatus && pStatus < 0xFF000000) memory_.write_value(pStatus, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_GetButtonInfo") {
        DeviceState& device = device_for_object(r0);
        const uint32_t id = r1;
        const uint32_t button_count = device.device_type == kHidKeyboardDevice ? kKeyboardButtonCount : kHidButtonCount;
        if (r2 == 0 || r2 >= 0xFF000000 || id >= button_count) {
            cpu.set_reg(REG_R0, AEE_EBADPARM);
            return;
        }
        const bool down = device.button_down[id];
        if (device.device_type == kHidKeyboardDevice) {
            write_button_info(memory_, r2, id, id, down);
        } else {
            write_button_info(memory_, r2, id, uid_from_button_id(id), down);
        }
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
            printf("[INPUT_TRACE] IHIDDevice_GetButtonInfo handle=0x%04x id=%u uid=0x%08x down=%u info=0x%08x\n",
                   device.handle,
                   id,
                   uid_from_button_id(id),
                   down ? 1u : 0u,
                   r2);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "IHIDDevice_GetNumberOfButtons") {
        DeviceState& device = device_for_object(r0);
        uint32_t pnButtons = r1;
        if (pnButtons && pnButtons < 0xFF000000) {
            memory_.write_value(pnButtons, device.device_type == kHidKeyboardDevice ? kKeyboardButtonCount : kHidButtonCount);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_RegisterForButtonEvent") {
        DeviceState& device = device_for_object(r0);
        device.button_signal = r1;
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
            printf("[INPUT_TRACE] IHIDDevice_RegisterForButtonEvent handle=0x%04x signal=0x%08x\n", device.handle, device.button_signal);
        }
        printf("  IHIDDevice_RegisterForButtonEvent handle=0x%04x signal=0x%x\n", device.handle, device.button_signal);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_RegisterForPositionChange") {
        DeviceState& device = device_for_object(r0);
        device.position_signal = r1;
        printf("  IHIDDevice_RegisterForPositionChange handle=0x%04x signal=0x%x\n", device.handle, device.position_signal);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_SetExclusiveLevel") {
        DeviceState& device = device_for_object(r0);
        device.exclusive_level = r1;
        printf("  IHIDDevice_SetExclusiveLevel handle=0x%04x level=%u\n", device.handle, device.exclusive_level);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_GetExclusiveLevel") {
        DeviceState& device = device_for_object(r0);
        if (r1 && r1 < 0xFF000000) memory_.write_value(r1, device.exclusive_level);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_GetPositionState" || name == "IHIDDevice_GetMinPositionInfo" || name == "IHIDDevice_GetMaxPositionInfo" || name == "IHIDDevice_GetAxesInfo") {
        DeviceState& device = device_for_object(r0);
        uint32_t pPos = r1;
        if (pPos == 0 || pPos >= 0xFF000000) {
            cpu.set_reg(REG_R0, AEE_EBADPARM);
            return;
        }
        if (pPos && pPos < 0xFF000000) {
            clear_position_info(memory_, pPos);
            if (name == "IHIDDevice_GetPositionState") {
                // AEEHIDPositionInfo uses absolute axes for joysticks. D-pad
                // buttons and analog sticks are independent physical controls:
                // do not mirror D-pad state into left-stick axes here.
                memory_.write_value(pPos + kPosRelativeAxes, 0u);
                memory_.write_value(pPos + kPosX, device.axis_left_x);
                memory_.write_value(pPos + kPosY, device.axis_left_y);
                memory_.write_value(pPos + kPosRx, device.axis_right_x);
                memory_.write_value(pPos + kPosRy, device.axis_right_y);
                if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                    printf("[INPUT_TRACE] IHIDDevice_GetPositionState handle=0x%04x lx=0x%04x ly=0x%04x rx=0x%04x ry=0x%04x info=0x%08x\n",
                           device.handle,
                           device.axis_left_x,
                           device.axis_left_y,
                           device.axis_right_x,
                           device.axis_right_y,
                           pPos);
                }
            } else if (name == "IHIDDevice_GetMaxPositionInfo") {
                memory_.write_value(pPos + kPosX, 0xffffu);
                memory_.write_value(pPos + kPosY, 0xffffu);
                memory_.write_value(pPos + kPosRx, 0xffffu);
                memory_.write_value(pPos + kPosRy, 0xffffu);
                if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                    printf("[INPUT_TRACE] IHIDDevice_GetMaxPositionInfo handle=0x%04x x/y/rx/ry=0xffff info=0x%08x\n",
                           device.handle,
                           pPos);
                }
            } else if (name == "IHIDDevice_GetMinPositionInfo") {
                // Unsupported axes keep min=max=0 per SDK. Supported absolute
                // thumbstick axes use 0..0xffff.
                if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                    printf("[INPUT_TRACE] IHIDDevice_GetMinPositionInfo handle=0x%04x x/y/rx/ry=0 info=0x%08x\n",
                           device.handle,
                           pPos);
                }
            } else if (name == "IHIDDevice_GetAxesInfo") {
                for (uint32_t offset = kPosX; offset < kPosSize; offset += 4) {
                    memory_.write_value(pPos + offset, kHidUnsupportedAxis);
                }
                memory_.write_value(pPos + kPosRelativeAxes, 0u);
                memory_.write_value(pPos + kPosX, 0x0106C4D0u);  // AEEUID_HIDJoystick_LeftThumb_X
                memory_.write_value(pPos + kPosY, 0x0106C4D1u);  // AEEUID_HIDJoystick_LeftThumb_Y
                memory_.write_value(pPos + kPosRx, 0x0106C4CEu); // AEEUID_HIDJoystick_RightThumb_X
                memory_.write_value(pPos + kPosRy, 0x0106C4CFu); // AEEUID_HIDJoystick_RightThumb_Y
                if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                    printf("[INPUT_TRACE] IHIDDevice_GetAxesInfo handle=0x%04x x=0x0106c4d0 y=0x0106c4d1 rx=0x0106c4ce ry=0x0106c4cf info=0x%08x\n",
                           device.handle,
                           pPos);
                }
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IHIDDevice_Rumble") {
        const int32_t left_signed = static_cast<int32_t>(r1);
        const int32_t right_signed = static_cast<int32_t>(r2);
        if (left_signed < 0 || right_signed < 0 ||
            static_cast<uint32_t>(left_signed) > kHidRumbleMax ||
            static_cast<uint32_t>(right_signed) > kHidRumbleMax) {
            cpu.set_reg(REG_R0, AEE_EBADPARM);
            return;
        }
        DeviceState& device = device_for_object(r0);
        const uint32_t device_index = device_index_for_object(r0);
        device.rumble_left = static_cast<uint32_t>(left_signed);
        device.rumble_right = static_cast<uint32_t>(right_signed);
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
            printf("[INPUT_TRACE] IHIDDevice_Rumble handle=0x%04x dev=%u left=%u right=%u\n",
                   device.handle,
                   device_index,
                   device.rumble_left,
                   device.rumble_right);
        }
        if (rumble_callback_) {
            rumble_callback_(device_index, device.rumble_left, device.rumble_right);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "IHIDDevice_GetRumbleStatus") {
        if (r1 == 0 || r1 >= 0xFF000000 || r2 == 0 || r2 >= 0xFF000000) {
            cpu.set_reg(REG_R0, AEE_EBADPARM);
            return;
        }
        DeviceState& device = device_for_object(r0);
        memory_.write_value(r1, device.rumble_left);
        memory_.write_value(r2, device.rumble_right);
        if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
            printf("[INPUT_TRACE] IHIDDevice_GetRumbleStatus handle=0x%04x left=%u right=%u\n",
                   device.handle,
                   device.rumble_left,
                   device.rumble_right);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
    } else if (name == "IHIDDevice_GetNextButtonEvent") {
        DeviceState& device = device_for_object(r0);
        uint32_t pInfo = r1;
        uint32_t pTimestamp = r2;
        uint32_t pDropped = r3;
        if (!device.button_events.empty()) {
            auto ev = device.button_events.front();
            device.button_events.pop();
            if (pInfo && pInfo < 0xFF000000) {
                if (device.device_type == kHidKeyboardDevice) {
                    write_button_info(memory_, pInfo, ev.id, ev.id, ev.down);
                } else {
                    write_button_info_from_uid(memory_, pInfo, ev.uid, ev.down);
                }
            }
            if (pTimestamp && pTimestamp < 0xFF000000) memory_.write_value(pTimestamp, static_cast<uint32_t>(0));
            if (pDropped && pDropped < 0xFF000000) memory_.write_value(pDropped, static_cast<uint8_t>(0), EndianMemory::Byte);
            if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                printf("[INPUT_TRACE] IHIDDevice_GetNextButtonEvent handle=0x%04x id=%u uid=0x%08x down=%u info=0x%08x remaining=%zu\n",
                       device.handle,
                       ev.id,
                       ev.uid,
                       ev.down ? 1u : 0u,
                       pInfo,
                       device.button_events.size());
            }
            cpu.set_reg(REG_R0, 0);
        } else {
            if (std::getenv("ZEEMU_TRACE_INPUT") != nullptr) {
                printf("[INPUT_TRACE] IHIDDevice_GetNextButtonEvent handle=0x%04x empty info=0x%08x\n", device.handle, pInfo);
            }
            cpu.set_reg(REG_R0, 5);
        }
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
