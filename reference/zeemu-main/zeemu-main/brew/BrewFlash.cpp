#include "brew/BrewFlash.h"

#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t AEE_SUCCESS = 0;
constexpr uint32_t AEE_EFAILED = 1;
constexpr uint32_t AEE_IFlashPlayer_EPENDING = 0x0796bc00;

constexpr uint32_t AEEIID_IQI = 0x01000001;
constexpr uint32_t AEEIID_IFlashPlayer = 0x010796bc;
constexpr uint32_t AEEIID_IFlashContentData = 0x0108e862;
constexpr uint32_t AEEIID_IFlashContent = 0x0108e863;

constexpr uint32_t IFlashPlayer_Event_LoadComplete = 0x1;
constexpr uint32_t IFlashPlayer_Event_LoadFailed = 0x2;
constexpr uint32_t IFlashPlayer_Event_Exit = 0x4;

bool trace_flash() {
    return std::getenv("ZEEMU_TRACE_FLASH") != nullptr ||
           std::getenv("ZEEMU_TRACE_HLE") != nullptr;
}

bool valid_guest_ptr(addr_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}
} // namespace

BrewFlash::BrewFlash(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtables();
    player_object_ptr_ = shell_.malloc(4);
    content_object_ptr_ = shell_.malloc(4);
    memory_.write_value(player_object_ptr_, player_vtable_ptr_);
    memory_.write_value(content_object_ptr_, content_vtable_ptr_);
}

void BrewFlash::setup_vtables() {
    player_vtable_ptr_ = shell_.malloc(15 * 4);
    const char* player_names[] = {
        "AddRef", "Release", "QueryInterface", "SetFrameBuffer", "OnEvent",
        "GetEvent", "LoadURL", "LoadBuffer", "Unload", "Play", "Stop",
        "Pause", "Resume", "ForceDraw", "HandleEvent"
    };
    for (int i = 0; i < 15; ++i) {
        memory_.write_value(player_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IFlashPlayer_") + player_names[i], this));
    }

    content_vtable_ptr_ = shell_.malloc(23 * 4);
    const char* content_names[] = {
        "AddRef", "Release", "QueryInterface", "GetBackgroundColor",
        "SetBackgroundColor", "SetBackgroundAlpha", "TCurrentFrame",
        "TCurrentLabel", "TGetProperty", "TSetProperty", "GetVariable",
        "SetVariable", "LoadLayer", "CallFunction", "SetReturnValueFromContainer",
        "CanContainerAccessMovie", "SetContainerSecurityContext", "GetScriptAccess",
        "SetScriptAccess", "GetScaleMode", "SetScaleMode", "GetAlignMode",
        "SetAlignMode"
    };
    for (int i = 0; i < 23; ++i) {
        memory_.write_value(content_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IFlashContent_") + content_names[i], this));
    }

    data_vtable_ptr_ = shell_.malloc(4 * 4);
    const char* data_names[] = { "AddRef", "Release", "QueryInterface", "GetData" };
    for (int i = 0; i < 4; ++i) {
        memory_.write_value(data_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IFlashContentData_") + data_names[i], this));
    }
}

addr_t BrewFlash::create_instance(uint32_t clsid) {
    if (trace_flash()) {
        printf("  Flash CLSID 0x%08x -> IFlashPlayer 0x%08x\n", clsid, player_object_ptr_);
    }
    return player_object_ptr_;
}

addr_t BrewFlash::create_data_object(std::string data) {
    DataObject item;
    item.object = shell_.malloc(4);
    item.data = std::move(data);
    memory_.write_value(item.object, data_vtable_ptr_);
    data_objects_.push_back(std::move(item));
    return data_objects_.back().object;
}

BrewFlash::DataObject* BrewFlash::find_data(addr_t object) {
    for (auto& item : data_objects_) {
        if (item.object == object) {
            return &item;
        }
    }
    return nullptr;
}

void BrewFlash::write_string_result(addr_t dst, int dst_len, addr_t required_len, const std::string& value) {
    if (valid_guest_ptr(required_len)) {
        memory_.write_value(required_len, static_cast<uint32_t>(value.size() + 1));
    }
    if (!valid_guest_ptr(dst) || dst_len <= 0) {
        return;
    }
    const int copy_len = std::max(0, std::min<int>(dst_len - 1, static_cast<int>(value.size())));
    for (int i = 0; i < copy_len; ++i) {
        memory_.write_value(dst + static_cast<addr_t>(i), static_cast<uint8_t>(value[static_cast<size_t>(i)]), EndianMemory::Byte);
    }
    memory_.write_value(dst + static_cast<addr_t>(copy_len), static_cast<uint8_t>(0), EndianMemory::Byte);
}

void BrewFlash::signal_event_if_ready() {
    if (pending_events_ != 0 && event_signal_ != 0) {
        shell_.set_signal(event_signal_, "flash-event");
    }
}

void BrewFlash::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const bool is_thunk = r1 >= 0xFF000000u;
    const uint32_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    const uint32_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;
    const uint32_t arg3 = is_thunk ? cpu.get_reg(REG_R7) : r3;

    if (name == "IFlashPlayer_AddRef" || name == "IFlashContent_AddRef" || name == "IFlashContentData_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (name == "IFlashPlayer_Release" || name == "IFlashContent_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "IFlashContentData_Release") {
        data_objects_.erase(std::remove_if(data_objects_.begin(), data_objects_.end(),
                                           [r0](const DataObject& item) { return item.object == r0; }),
                            data_objects_.end());
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "IFlashPlayer_QueryInterface") {
        const uint32_t iid = arg1;
        const uint32_t pp = arg2;
        uint32_t obj = 0;
        if (iid == AEEIID_IQI || iid == AEEIID_IFlashPlayer) {
            obj = player_object_ptr_;
        } else if (iid == AEEIID_IFlashContent) {
            obj = content_object_ptr_;
        }
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, obj);
        }
        if (trace_flash()) {
            printf("  IFlashPlayer_QueryInterface iid=0x%08x -> 0x%08x\n", iid, obj);
        }
        cpu.set_reg(REG_R0, obj ? AEE_SUCCESS : AEE_EFAILED);
        return;
    }
    if (name == "IFlashContent_QueryInterface") {
        const uint32_t iid = arg1;
        const uint32_t pp = arg2;
        const uint32_t obj = (iid == AEEIID_IQI || iid == AEEIID_IFlashContent) ? content_object_ptr_ : 0;
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, obj);
        }
        cpu.set_reg(REG_R0, obj ? AEE_SUCCESS : AEE_EFAILED);
        return;
    }
    if (name == "IFlashContentData_QueryInterface") {
        const uint32_t iid = arg1;
        const uint32_t pp = arg2;
        const uint32_t obj = (iid == AEEIID_IQI || iid == AEEIID_IFlashContentData) ? r0 : 0;
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, obj);
        }
        cpu.set_reg(REG_R0, obj ? AEE_SUCCESS : AEE_EFAILED);
        return;
    }

    if (name == "IFlashPlayer_SetFrameBuffer") {
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_OnEvent") {
        event_signal_ = arg1;
        if (trace_flash()) {
            printf("  IFlashPlayer_OnEvent signal=0x%08x pending=0x%08x\n", event_signal_, pending_events_);
        }
        signal_event_if_ready();
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_GetEvent") {
        if (valid_guest_ptr(arg1)) {
            memory_.write_value(arg1, pending_events_);
        }
        if (trace_flash()) {
            printf("  IFlashPlayer_GetEvent -> 0x%08x\n", pending_events_);
        }
        pending_events_ = 0;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_LoadURL") {
        url_ = shell_.read_guest_text(arg1, 1024);
        loaded_ = true;
        pending_events_ |= IFlashPlayer_Event_LoadComplete;
        if (trace_flash()) {
            printf("  IFlashPlayer_LoadURL '%s' -> EPENDING/LoadComplete\n", url_.c_str());
        }
        signal_event_if_ready();
        cpu.set_reg(REG_R0, AEE_IFlashPlayer_EPENDING);
        return;
    }
    if (name == "IFlashPlayer_LoadBuffer") {
        url_.clear();
        loaded_ = true;
        pending_events_ |= IFlashPlayer_Event_LoadComplete;
        if (trace_flash()) {
            printf("  IFlashPlayer_LoadBuffer buf=0x%08x len=%u copy=%u -> EPENDING/LoadComplete\n", arg1, arg2, arg3);
        }
        signal_event_if_ready();
        cpu.set_reg(REG_R0, AEE_IFlashPlayer_EPENDING);
        return;
    }
    if (name == "IFlashPlayer_Unload") {
        loaded_ = false;
        playing_ = false;
        pending_events_ = 0;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_Play") {
        playing_ = loaded_;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_Stop") {
        playing_ = false;
        pending_events_ |= IFlashPlayer_Event_Exit;
        signal_event_if_ready();
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_Pause" || name == "IFlashPlayer_Resume" || name == "IFlashPlayer_ForceDraw") {
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashPlayer_HandleEvent") {
        const uint32_t pb_handled = memory_.read_value(cpu.get_reg(REG_SP));
        if (valid_guest_ptr(pb_handled)) {
            memory_.write_value(pb_handled, static_cast<uint8_t>(0), EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }

    if (name == "IFlashContentData_GetData") {
        DataObject* data = find_data(r0);
        write_string_result(arg1, static_cast<int>(arg2), arg3, data ? data->data : std::string{});
        cpu.set_reg(REG_R0, data ? AEE_SUCCESS : AEE_EFAILED);
        return;
    }

    if (name == "IFlashContent_GetBackgroundColor") {
        if (valid_guest_ptr(arg1)) {
            memory_.write_value(arg1 + 0, static_cast<uint8_t>((background_color_ >> 24) & 0xff), EndianMemory::Byte);
            memory_.write_value(arg1 + 1, static_cast<uint8_t>((background_color_ >> 16) & 0xff), EndianMemory::Byte);
            memory_.write_value(arg1 + 2, static_cast<uint8_t>((background_color_ >> 8) & 0xff), EndianMemory::Byte);
            memory_.write_value(arg1 + 3, static_cast<uint8_t>(background_color_ & 0xff), EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetBackgroundColor") {
        background_color_ = ((arg1 & 0xffu) << 24) | ((arg2 & 0xffu) << 16) | ((arg3 & 0xffu) << 8) | (background_color_ & 0xffu);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetBackgroundAlpha") {
        background_color_ = (background_color_ & 0xffffff00u) | (arg1 & 0xffu);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_TCurrentFrame") {
        if (valid_guest_ptr(arg3)) {
            memory_.write_value(arg3, loaded_ ? 1u : 0u);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_TCurrentLabel") {
        const uint32_t pp = arg3;
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, create_data_object(loaded_ ? "loaded" : ""));
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_TGetProperty") {
        const uint32_t prop = arg2;
        const uint32_t pp = memory_.read_value(cpu.get_reg(REG_SP));
        std::string value = (prop == 5 || prop == 12) ? (loaded_ ? "1" : "0") : "0";
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, create_data_object(std::move(value)));
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_GetVariable") {
        const std::string key = shell_.read_guest_text(arg1, 512);
        const auto it = variables_.find(key);
        const uint32_t pp = arg3;
        if (valid_guest_ptr(pp)) {
            memory_.write_value(pp, create_data_object(it == variables_.end() ? std::string{} : it->second));
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetVariable") {
        variables_[shell_.read_guest_text(arg1, 512)] = shell_.read_guest_text(arg2, 4096);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_LoadLayer") {
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_CallFunction") {
        const std::string request = shell_.read_guest_text(arg1, 4096);
        if (trace_flash()) {
            printf("  IFlashContent_CallFunction '%.*s'\n", 160, request.c_str());
        }
        if (valid_guest_ptr(arg2)) {
            memory_.write_value(arg2, create_data_object(return_value_));
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetReturnValueFromContainer") {
        return_value_ = shell_.read_guest_text(arg1, 4096);
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_CanContainerAccessMovie") {
        if (valid_guest_ptr(arg3)) {
            memory_.write_value(arg3, static_cast<uint8_t>(1), EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetContainerSecurityContext") {
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_GetScriptAccess") {
        if (valid_guest_ptr(arg1)) {
            memory_.write_value(arg1, script_access_);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetScriptAccess") {
        script_access_ = arg1;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_GetScaleMode") {
        if (valid_guest_ptr(arg1)) {
            memory_.write_value(arg1, scale_mode_);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetScaleMode") {
        scale_mode_ = arg1;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_GetAlignMode") {
        if (valid_guest_ptr(arg1)) {
            memory_.write_value(arg1, align_mode_);
        }
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_SetAlignMode") {
        align_mode_ = arg1;
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }
    if (name == "IFlashContent_TSetProperty") {
        cpu.set_reg(REG_R0, AEE_SUCCESS);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, AEE_SUCCESS);
}
