#include "brew/BrewAppHistory.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>

BrewAppHistory::BrewAppHistory(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}

void BrewAppHistory::setup_vtable() {
    // IAppHistory vtable (from AEEAppHist.h / AEEIAppHistory.h):
    //  [0] AddRef   [1] Release   [2] QueryInterface
    //  [3] Back     [4] Bottom    [5] Current
    //  [6] Forward  [7] GetArgs   [8] GetClass
    //  [9] GetData  [10] GetReason [11] GetResumeData
    // [12] Insert   [13] Move     [14] Remove
    // [15] SetData  [16] SetReason [17] SetResumeData
    // [18] Top
    vtable_ptr_ = shell_.malloc(19 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);

    const char* names[] = {
        "AddRef", "Release", "QueryInterface",
        "Back", "Bottom", "Current",
        "Forward", "GetArgs", "GetClass",
        "GetData", "GetReason", "GetResumeData",
        "Insert", "Move", "Remove",
        "SetData", "SetReason", "SetResumeData",
        "Top"
    };
    for (int i = 0; i < 19; ++i) {
        addr_t h = shell_.add_hook(std::string("IAppHistory_") + names[i], this);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), h);
    }
}

void BrewAppHistory::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    bool is_thunk = (r1 >= 0xFF000000);
    uint32_t arg1 = is_thunk ? cpu.get_reg(REG_R5) : r1;
    uint32_t arg2 = is_thunk ? cpu.get_reg(REG_R6) : r2;

    if (name == "IAppHistory_AddRef") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IAppHistory_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_QueryInterface") {
        uint32_t ppObj = is_thunk ? r3 : r2;
        if (ppObj && ppObj < 0xFF000000)
            memory_.write_value(ppObj, object_ptr_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Back") {
        printf("  IAppHistory_Back\n");
        cpu.set_reg(REG_R0, 0x0204); // ENOMORE
    } else if (name == "IAppHistory_Bottom") {
        printf("  IAppHistory_Bottom\n");
        cpu.set_reg(REG_R0, 0x0204); // ENOMORE — already at bottom
    } else if (name == "IAppHistory_Current") {
        // arg1 = AEEAppInfo* out-param
        printf("  IAppHistory_Current pAppInfo=0x%08x\n", arg1);
        if (arg1 && arg1 < 0xFF000000) {
            // Zero out AEEAppInfo struct (size ~20 bytes)
            for (int i = 0; i < 20; i += 4)
                memory_.write_value(arg1 + static_cast<uint32_t>(i), (uint32_t)0);
            // Fill cls at offset 0
            memory_.write_value(arg1, current_cls_);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Forward") {
        printf("  IAppHistory_Forward\n");
        cpu.set_reg(REG_R0, 0x0204); // ENOMORE
    } else if (name == "IAppHistory_GetArgs") {
        // arg1 = char* pszBuf, arg2 = nBufSize
        printf("  IAppHistory_GetArgs pszBuf=0x%08x nBufSize=0x%08x\n", arg1, arg2);
        if (arg1 && arg1 < 0xFF000000) {
            // nBufSize may be a thunk register value, not a real size.
            // Always write empty string if buffer pointer looks valid.
            memory_.write_value(arg1, (uint8_t)0, EndianMemory::Byte);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_GetClass") {
        // arg1 = AEECLSID* out-param
        printf("  IAppHistory_GetClass pCls=0x%08x -> 0x%08x\n", arg1, current_cls_);
        if (arg1 && arg1 < 0xFF000000)
            memory_.write_value(arg1, current_cls_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_GetData") {
        // arg1 = void** out-param
        if (arg1 && arg1 < 0xFF000000)
            memory_.write_value(arg1, (uint32_t)0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_GetReason") {
        // arg1 = int* out-param
        printf("  IAppHistory_GetReason\n");
        if (arg1 && arg1 < 0xFF000000)
            memory_.write_value(arg1, (uint32_t)0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_GetResumeData") {
        // arg1 = void** out-param
        if (arg1 && arg1 < 0xFF000000)
            memory_.write_value(arg1, (uint32_t)0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Insert") {
        printf("  IAppHistory_Insert\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Move") {
        printf("  IAppHistory_Move\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Remove") {
        printf("  IAppHistory_Remove\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_SetData") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_SetReason") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_SetResumeData") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IAppHistory_Top") {
        printf("  IAppHistory_Top\n");
        cpu.set_reg(REG_R0, 0x0204); // ENOMORE
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), cpu.get_reg(REG_R0), r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
