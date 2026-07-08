#include "brew/BrewNet.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>

namespace {
constexpr int SUCCESS = 0;
constexpr int EFAILED = 1;
constexpr int AEE_NET_WOULDBLOCK = 545;
}

BrewNet::BrewNet(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_netmgr();
    setup_socket();
}

void BrewNet::setup_netmgr() {
    // INetMgr from AEENet.h:
    // [0] AddRef [1] Release [2] QueryInterface [3] SetMask
    // [4] GetHostByName [5] GetLastError [6] OpenSocket [7] NetStatus
    // [8] GetMyIPAddr [9] SetLinger [10] OnEvent [11] SetOpt [12] GetOpt
    static const char* names[] = {
        "AddRef", "Release", "QueryInterface", "SetMask",
        "GetHostByName", "GetLastError", "OpenSocket", "NetStatus",
        "GetMyIPAddr", "SetLinger", "OnEvent", "SetOpt", "GetOpt"
    };
    net_vtable_ = shell_.malloc(sizeof(names) / sizeof(names[0]) * 4);
    net_obj_ = shell_.malloc(4);
    memory_.write_value(net_obj_, net_vtable_);
    for (int i = 0; i < 13; ++i) {
        memory_.write_value(net_vtable_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("INetMgr_") + names[i], this));
    }
}

void BrewNet::setup_socket() {
    // ISocket from AEENet.h inherits IAStream:
    // [0] AddRef [1] Release [2] QueryInterface [3] Readable [4] Read [5] Cancel
    // [6] GetPeerName [7] GetLastError [8] Connect [9] Bind [10] Write
    // [11] WriteV [12] ReadV [13] SendTo [14] RecvFrom [15] Writeable [16] IOCtl
    static const char* names[] = {
        "AddRef", "Release", "QueryInterface", "Readable", "Read", "Cancel",
        "GetPeerName", "GetLastError", "Connect", "Bind", "Write",
        "WriteV", "ReadV", "SendTo", "RecvFrom", "Writeable", "IOCtl"
    };
    socket_vtable_ = shell_.malloc(sizeof(names) / sizeof(names[0]) * 4);
    socket_obj_ = shell_.malloc(4);
    memory_.write_value(socket_obj_, socket_vtable_);
    for (int i = 0; i < 17; ++i) {
        memory_.write_value(socket_vtable_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("ISocket_") + names[i], this));
    }
}

std::string BrewNet::read_guest_string(addr_t addr, size_t max_len) const {
    std::string out;
    if (!addr || addr >= 0xFF000000) {
        return out;
    }
    for (size_t i = 0; i < max_len; ++i) {
        char ch = static_cast<char>(memory_.read_value(addr + static_cast<uint32_t>(i), EndianMemory::Byte));
        if (!ch) {
            break;
        }
        out.push_back(ch);
    }
    return out;
}

void BrewNet::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r0 = cpu.get_reg(REG_R0);
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "INetMgr_AddRef" || name == "ISocket_AddRef") {
        cpu.set_reg(REG_R0, 1);
        return;
    }
    if (name == "INetMgr_Release" || name == "ISocket_Release") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "INetMgr_QueryInterface" || name == "ISocket_QueryInterface") {
        if (r2 && r2 < 0xFF000000) {
            memory_.write_value(r2, name.rfind("INetMgr_", 0) == 0 ? net_obj_ : socket_obj_);
        }
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }
    if (name == "INetMgr_SetMask") {
        printf("  INetMgr_SetMask masks=0x%08x\n", r1);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }
    if (name == "INetMgr_GetHostByName") {
        const std::string host = read_guest_string(r2);
        printf("  INetMgr_GetHostByName result=0x%08x host='%s' callback=0x%08x -> offline failure\n",
               r1, host.c_str(), r3);
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1 + 0, static_cast<uint32_t>(EFAILED));
            for (int i = 0; i < 4; ++i) {
                memory_.write_value(r1 + 4 + static_cast<uint32_t>(i * 4), 0);
            }
        }
        last_error_ = EFAILED;
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "INetMgr_GetLastError" || name == "ISocket_GetLastError") {
        cpu.set_reg(REG_R0, static_cast<uint32_t>(last_error_));
        return;
    }
    if (name == "INetMgr_OpenSocket") {
        printf("  INetMgr_OpenSocket type=0x%08x -> 0x%08x\n", r1, socket_obj_);
        cpu.set_reg(REG_R0, socket_obj_);
        return;
    }
    if (name == "INetMgr_NetStatus") {
        printf("  INetMgr_NetStatus stats=0x%08x -> offline/closed\n", r1);
        if (r1 && r1 < 0xFF000000) {
            for (int i = 0; i < 32; i += 4) {
                memory_.write_value(r1 + static_cast<uint32_t>(i), 0);
            }
        }
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "INetMgr_GetMyIPAddr") {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    if (name == "INetMgr_SetLinger" || name == "INetMgr_OnEvent" || name == "INetMgr_SetOpt") {
        printf("  %s arg1=0x%08x arg2=0x%08x arg3=0x%08x\n", name.c_str(), r1, r2, r3);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }
    if (name == "INetMgr_GetOpt") {
        printf("  INetMgr_GetOpt opt=%u val=0x%08x size=0x%08x\n", r1, r2, r3);
        if (r3 && r3 < 0xFF000000) {
            memory_.write_value(r3, 0);
        }
        cpu.set_reg(REG_R0, EFAILED);
        return;
    }

    if (name == "ISocket_Readable" || name == "ISocket_Cancel" || name == "ISocket_Writeable") {
        printf("  %s callback=0x%08x user=0x%08x\n", name.c_str(), r1, r2);
        cpu.set_reg(REG_R0, SUCCESS);
        return;
    }
    if (name == "ISocket_Read" || name == "ISocket_ReadV" || name == "ISocket_RecvFrom") {
        last_error_ = AEE_NET_WOULDBLOCK;
        cpu.set_reg(REG_R0, static_cast<uint32_t>(AEE_NET_WOULDBLOCK));
        return;
    }
    if (name == "ISocket_Connect") {
        printf("  ISocket_Connect addr=0x%08x port=%u cb=0x%08x user=0x%08x -> offline failure\n",
               r1, static_cast<unsigned>(r2 & 0xffff), r3, cpu.get_reg(REG_SP));
        last_error_ = EFAILED;
        cpu.set_reg(REG_R0, EFAILED);
        return;
    }
    if (name == "ISocket_Bind" || name == "ISocket_Write" || name == "ISocket_WriteV" ||
        name == "ISocket_SendTo" || name == "ISocket_IOCtl") {
        printf("  %s arg1=0x%08x arg2=0x%08x arg3=0x%08x -> offline failure\n",
               name.c_str(), r1, r2, r3);
        last_error_ = EFAILED;
        cpu.set_reg(REG_R0, EFAILED);
        return;
    }
    if (name == "ISocket_GetPeerName") {
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1, 0);
        }
        if (r2 && r2 < 0xFF000000) {
            memory_.write_value(r2, 0);
        }
        cpu.set_reg(REG_R0, EFAILED);
        return;
    }

    printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
           name.c_str(), r0, r1, r2, r3);
    cpu.set_reg(REG_R0, EFAILED);
}
