#include "brew/BrewAppletCtl.h"
#include "cpu/core/CPU.h"
#include <cstdio>

namespace {
static constexpr uint32_t kOk = 0;
static constexpr uint32_t kTrue = 1;

static bool is_known_applet_clsid(uint32_t clsId) {
    switch (clsId) {
        case 0x01070798:
        case 0x01077cf4:
        case 0x01072195:
        case 0x0102f789:
        case 0x01087a3c:
        case 0x01087c1c:
        case 0x01009ff2:
        case 0x0108ff07:
        case 0x00c9b04b:
        case 0xbf2e2021:
            return true;
        default:
            return false;
    }
}
}

BrewAppletCtl::BrewAppletCtl(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewAppletCtl::setup_vtable() {
    vtable_ptr_ = shell_.malloc(12 * 4);
    object_ptr_ = shell_.malloc(0x40);
    memory_.write_value(object_ptr_ + 0x00, vtable_ptr_);
    memory_.write_value(object_ptr_ + 0x04, (uint32_t)0);
    memory_.write_value(object_ptr_ + 0x08, refs_);

    const char* names[] = {
        "AddRef", "Release", "QueryInterface", "BrowseFile",
        "CanStart", "Control", "GetRunningInfo", "GetRunningList",
        "RunningApplet", "Start", "Stop", "BrowseURL"
    };
    for (int i = 0; i < 12; ++i) {
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(std::string("IAppletCtl_") + names[i], this));
    }
}

void BrewAppletCtl::write_running_list(uint32_t p_list, uint32_t p_count) {
    const uint32_t cls_id = shell_.get_current_applet_cls();
    if (p_count && p_count < 0xFF000000) {
        memory_.write_value(p_count, cls_id ? 1u : 0u);
    }
    if (p_list && p_list < 0xFF000000) {
        memory_.write_value(p_list + 0x00, cls_id);
        memory_.write_value(p_list + 0x04, cls_id ? 1u : 0u);
    }
}

void BrewAppletCtl::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "IAppletCtl_AddRef") {
        ++refs_;
        memory_.write_value(object_ptr_ + 0x08, refs_);
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IAppletCtl_Release") {
        if (refs_ > 0) {
            --refs_;
        }
        memory_.write_value(object_ptr_ + 0x08, refs_);
        cpu.set_reg(REG_R0, refs_);
    } else if (name == "IAppletCtl_QueryInterface") {
        uint32_t ppv = r2;
        if (ppv && ppv < 0xFF000000) {
            memory_.write_value(ppv, object_ptr_);
        }
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_BrowseFile" || name == "IAppletCtl_BrowseURL") {
        char buf[256] = {};
        if (r1) {
            shell_.read_string(r1, buf, sizeof(buf));
        }
        printf("  %s: %s\n", name.c_str(), r1 ? buf : "<null>");
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_CanStart") {
        // Launched app is always startable, independent of the catalog list.
        bool startable = is_known_applet_clsid(r1) ||
                         (r1 != 0 && r1 == shell_.get_current_applet_cls());
        cpu.set_reg(REG_R0, startable ? kTrue : 0u);
    } else if (name == "IAppletCtl_Control") {
        printf("  IAppletCtl_Control: r1=0x%x r2=0x%x r3=0x%x\n", r1, r2, r3);
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_GetRunningInfo") {
        uint32_t cls_id = r1;
        uint32_t p_info = r2;
        if (p_info && p_info < 0xFF000000) {
            memory_.write_value(p_info + 0x00, cls_id ? cls_id : shell_.get_current_applet_cls());
            memory_.write_value(p_info + 0x04, shell_.get_current_applet_cls() ? 1u : 0u);
        }
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_GetRunningList") {
        write_running_list(r1, r2);
        printf("  GetRunningList Count = %u\n", shell_.get_current_applet_cls() ? 1u : 0u);
        if (shell_.get_current_applet_cls()) {
            printf("  GetRunningList ClassId =%x ,State =%d\n", shell_.get_current_applet_cls(), 1);
            printf("  GetRunningList clsid = %x\n", shell_.get_current_applet_cls());
            printf("  GetRunningList clsid is Tectoy Exist\n");
        }
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_RunningApplet") {
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1, shell_.get_applet_object_ptr());
        }
        cpu.set_reg(REG_R0, shell_.get_current_applet_cls() ? kTrue : 0u);
    } else if (name == "IAppletCtl_Start") {
        shell_.set_current_applet_cls(r1);
        cpu.set_reg(REG_R0, kOk);
    } else if (name == "IAppletCtl_Stop") {
        if (!r1 || r1 == shell_.get_current_applet_cls()) {
            shell_.set_current_applet_cls(0);
        }
        cpu.set_reg(REG_R0, kOk);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, r0);
    }
}
