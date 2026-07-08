#include "brew/BrewZWheelOem.h"

#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>

BrewZWheelOem::BrewZWheelOem(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtables();
}

void BrewZWheelOem::setup_vtables() {
    config_vtable_ptr_ = shell_.malloc(16 * 4);
    config_object_ptr_ = shell_.malloc(4);
    memory_.write_value(config_object_ptr_, config_vtable_ptr_);
    const char* config_names[] = {
        "AddRef", "Release", "QueryInterface", "Fn3",
        "Fn4", "Commit", "Fn6", "Fn7",
        "SetLine", "Fn9", "Finish", "Fn11",
        "Begin"
    };
    for (int i = 0; i < 13; ++i) {
        memory_.write_value(config_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ZWheelConfig_") + config_names[i], this));
    }
    for (int i = 13; i < 16; ++i) {
        memory_.write_value(config_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook("ZWheelConfig_Fn" + std::to_string(i), this));
    }

    root_form_vtable_ptr_ = shell_.malloc(8 * 4);
    root_form_object_ptr_ = shell_.malloc(4);
    memory_.write_value(root_form_object_ptr_, root_form_vtable_ptr_);
    const char* root_names[] = {
        "AddRef", "Release", "QueryInterface", "ConfigureOrEvent"
    };
    for (int i = 0; i < 4; ++i) {
        memory_.write_value(root_form_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ZWheelRootForm_") + root_names[i], this));
    }
    for (int i = 4; i < 8; ++i) {
        memory_.write_value(root_form_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook("ZWheelRootForm_Fn" + std::to_string(i), this));
    }

    mcp_vtable_ptr_ = shell_.malloc(16 * 4);
    mcp_object_ptr_ = shell_.malloc(4);
    memory_.write_value(mcp_object_ptr_, mcp_vtable_ptr_);
    const char* mcp_names[] = {
        "AddRef", "Release", "QueryInterface", "Fn3",
        "Fn4", "Fn5", "Fn6", "Fn7",
        "Fn8", "Fn9", "Fn10", "Fn11",
        "Fn12", "Fn13", "Fn14", "Fn15"
    };
    for (int i = 0; i < 16; ++i) {
        memory_.write_value(mcp_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ZWheelMcp_") + mcp_names[i], this));
    }

    telemetry_vtable_ptr_ = shell_.malloc(32 * 4);
    telemetry_object_ptr_ = shell_.malloc(4);
    memory_.write_value(telemetry_object_ptr_, telemetry_vtable_ptr_);
    for (int i = 0; i < 32; ++i) {
        const std::string slot_name =
            i == 0 ? "AddRef" :
            i == 1 ? "Release" :
            i == 2 ? "QueryInterface" :
                     "Fn" + std::to_string(i);
        memory_.write_value(telemetry_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ZWheelTelemetry_") + slot_name, this));
    }
}

std::string BrewZWheelOem::read_guest_string(addr_t addr) const {
    if (addr == 0 || addr >= 0xFF000000) return {};
    std::string out;
    for (uint32_t i = 0; i < 512; ++i) {
        uint8_t ch = static_cast<uint8_t>(memory_.read_value(addr + i, EndianMemory::Byte));
        if (ch == 0) break;
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

void BrewZWheelOem::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);
    const bool is_thunk = (r1 >= 0xFF000000);
    const uint32_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    const uint32_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;
    const uint32_t arg3 = is_thunk ? cpu.get_reg(REG_R7) : r3;

    if (name == "ZWheelConfig_AddRef" || name == "ZWheelRootForm_AddRef" ||
        name == "ZWheelMcp_AddRef" || name == "ZWheelTelemetry_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (name == "ZWheelConfig_Release" || name == "ZWheelRootForm_Release" ||
        name == "ZWheelMcp_Release" || name == "ZWheelTelemetry_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ZWheelConfig_QueryInterface" || name == "ZWheelRootForm_QueryInterface" ||
        name == "ZWheelMcp_QueryInterface" || name == "ZWheelTelemetry_QueryInterface") {
        if (arg2 && arg2 < 0xFF000000) memory_.write_value(arg2, r0);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "ZWheelConfig_Begin") {
        printf("  ZWheelConfig_Begin key=0x%08x\n", arg1);
        config_line_ptrs_.clear();
        config_line_index_ = 0;
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ZWheelConfig_SetLine") {
        // Trace shows R2 points at the parsed line string. Later Fn6/Fn9 calls
        // iterate these stored strings while the launcher scans key=value data.
        if (r2 && r2 < 0xFF000000) {
            config_line_ptrs_.push_back(r2);
        }
        printf("  ZWheelConfig_SetLine mode=0x%08x item=0x%08x line='%s'\n",
               r1, r2, read_guest_string(r2).c_str());
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "ZWheelConfig_Fn6") {
        const addr_t line = config_line_index_ < config_line_ptrs_.size()
            ? config_line_ptrs_[config_line_index_]
            : 0;
        if (arg2 && arg2 < 0xFF000000) {
            memory_.write_value(arg2, line);
        }
        printf("  ZWheelConfig_Fn6 index=%zu -> 0x%08x\n", config_line_index_, line);
        cpu.set_reg(REG_R0, line);
        return;
    }
    if (name == "ZWheelConfig_Fn9") {
        if (config_line_index_ < config_line_ptrs_.size()) {
            ++config_line_index_;
        }
        printf("  ZWheelConfig_Fn9 advance -> index=%zu\n", config_line_index_);
        cpu.set_reg(REG_R0, config_line_index_ < config_line_ptrs_.size() ? 1u : 0u);
        return;
    }
    if (name == "ZWheelConfig_Commit" || name == "ZWheelConfig_Finish" ||
        name == "ZWheelConfig_Fn3" || name == "ZWheelConfig_Fn4" ||
        name == "ZWheelConfig_Fn6" || name == "ZWheelConfig_Fn7" ||
        name == "ZWheelConfig_Fn9" || name == "ZWheelConfig_Fn11" ||
        name == "ZWheelConfig_Fn13" || name == "ZWheelConfig_Fn14" ||
        name == "ZWheelConfig_Fn15") {
        printf("  %s arg1=0x%08x arg2=0x%08x arg3=0x%08x\n",
               name.c_str(), arg1, arg2, arg3);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name == "ZWheelRootForm_ConfigureOrEvent") {
        // Observed uses:
        //   EVT_APP_START: arg1=0x800, arg2=0x5000, arg3=out/context.
        //   input events:  arg1=event code, arg2=wParam, arg3=dwParam.
        if (arg3 && arg3 < 0xFF000000) {
            memory_.write_value(arg3, arg1 == 0x800 ? root_form_object_ptr_ : static_cast<uint32_t>(0));
        }
        printf("  ZWheelRootForm_ConfigureOrEvent op=0x%08x arg2=0x%08x arg3=0x%08x\n",
               arg1, arg2, arg3);
        cpu.set_reg(REG_R0, (arg1 & 0x800) || arg1 < 0x800 ? 1u : 0u);
        return;
    }

    if (name.rfind("ZWheelMcp_", 0) == 0) {
        printf("  [%s] not implemented yet arg1=0x%08x arg2=0x%08x arg3=0x%08x\n",
               name.c_str(), arg1, arg2, arg3);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    if (name.rfind("ZWheelTelemetry_", 0) == 0) {
        // CLSID 0x01011810 is created by the Z-Wheel path around
        // CTelemetryData.cpp and logs "Telemetry Disabled" when config is not
        // present. Model it as an offline telemetry sink instead of an
        // anonymous dummy service so slot usage remains visible in traces.
        if (name == "ZWheelTelemetry_Fn28") {
            ++telemetry_send_count_;
            printf("  %s offline send #%u buffer=0x%08x size=%u signal=0x%08x\n",
                   name.c_str(), telemetry_send_count_, arg1, arg2, arg3);
            cpu.set_reg(REG_R0, 1);
            return;
        }
        printf("  [%s] not implemented yet offline arg1=0x%08x arg2=0x%08x arg3=0x%08x\n",
               name.c_str(), arg1, arg2, arg3);
        cpu.set_reg(REG_R0, 0);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, 0);
}
