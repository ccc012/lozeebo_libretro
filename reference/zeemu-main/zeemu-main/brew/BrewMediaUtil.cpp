#include "brew/BrewMediaUtil.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>
#include <string>

BrewMediaUtil::BrewMediaUtil(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
}

void BrewMediaUtil::setup_vtable() {
    vtable_ptr_ = shell_.malloc(8 * 4);
    object_ptr_ = shell_.malloc(0x10);
    memory_.write_value(object_ptr_, vtable_ptr_);

    const char* names[] = {
        "AddRef",
        "Release",
        "QueryInterface",
        "CreateMedia",
        "EncodeMedia",
        "CreateMediaEx",
    };
    for (int i = 0; i < 6; ++i) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IMediaUtil_") + names[i], this));
    }
    for (int i = 6; i < 8; ++i) {
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook("IMediaUtil_Fn" + std::to_string(i), this));
    }
}

void BrewMediaUtil::handle_hook(const std::string& name, CPU& cpu) {
    const uint32_t r1 = cpu.get_reg(REG_R1);
    const uint32_t r2 = cpu.get_reg(REG_R2);
    const uint32_t r5 = cpu.get_reg(REG_R5);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const bool is_thunk = r1 >= 0xFF000000;
    constexpr uint32_t EBADPARM = 14;
    constexpr uint32_t EUNSUPPORTED = 20;

    auto set_success = [&]() { cpu.set_reg(REG_R0, 0); };
    auto set_ref = [&]() { cpu.set_reg(REG_R0, 1); };

    if (name == "IMediaUtil_AddRef" || name == "IMediaUtil_Release") {
        printf("  [%s] -> 1\n", name.c_str());
        set_ref();
    } else if (name == "IMediaUtil_QueryInterface") {
        const uint32_t ppObj = is_thunk ? r6 : r2;
        if (ppObj && ppObj < 0xFF000000) {
            memory_.write_value(ppObj, object_ptr_);
        }
        printf("  [IMediaUtil_QueryInterface] ppObj=0x%08x\n", ppObj);
        set_success();
    } else if (name == "IMediaUtil_CreateMedia") {
        const uint32_t info = is_thunk ? r5 : r1;
        const uint32_t ppm = is_thunk ? r6 : r2;
        if (info == 0 || info >= 0xFF000000 || ppm == 0 || ppm >= 0xFF000000) {
            printf("  [IMediaUtil_CreateMedia] bad params info=0x%08x ppm=0x%08x\n", info, ppm);
            cpu.set_reg(REG_R0, EBADPARM);
            return;
        }
        const addr_t media_object = shell_.create_media_from_data(info);
        if (media_object == 0) {
            memory_.write_value(ppm, 0u);
            printf("  [IMediaUtil_CreateMedia] info=0x%08x ppm=0x%08x -> unsupported\n", info, ppm);
            cpu.set_reg(REG_R0, EUNSUPPORTED);
            return;
        }
        memory_.write_value(ppm, media_object);
        printf("  [IMediaUtil_CreateMedia] info=0x%08x ppm=0x%08x -> IMedia 0x%08x\n",
               info, ppm, media_object);
        set_success();
    } else if (name == "IMediaUtil_CreateMediaEx") {
        const uint32_t info = is_thunk ? r5 : r1;
        const uint32_t ppm = is_thunk ? r6 : r2;
        if (info == 0 || info >= 0xFF000000 || ppm == 0 || ppm >= 0xFF000000) {
            printf("  [IMediaUtil_CreateMediaEx] bad params info=0x%08x ppm=0x%08x\n", info, ppm);
            cpu.set_reg(REG_R0, EBADPARM);
            return;
        }
        const uint32_t count = memory_.read_value(info + 8);
        const addr_t media_data_list = memory_.read_value(info + 12);
        if (count == 0 || media_data_list == 0 || media_data_list >= 0xFF000000) {
            printf("  [IMediaUtil_CreateMediaEx] empty list info=0x%08x count=%u list=0x%08x\n",
                   info, count, media_data_list);
            cpu.set_reg(REG_R0, EBADPARM);
            return;
        }
        const addr_t media_object = shell_.create_media_from_data(media_data_list);
        if (media_object == 0) {
            memory_.write_value(ppm, 0u);
            printf("  [IMediaUtil_CreateMediaEx] info=0x%08x first=0x%08x -> unsupported\n",
                   info, media_data_list);
            cpu.set_reg(REG_R0, EUNSUPPORTED);
            return;
        }
        memory_.write_value(ppm, media_object);
        printf("  [IMediaUtil_CreateMediaEx] info=0x%08x count=%u first=0x%08x -> IMedia 0x%08x\n",
               info, count, media_data_list, media_object);
        set_success();
    } else if (name == "IMediaUtil_EncodeMedia") {
        printf("  [IMediaUtil_EncodeMedia] unsupported encoder path\n");
        cpu.set_reg(REG_R0, 1);
    } else {
        printf("  [%s] not implemented yet R1=0x%08x R2=0x%08x R5=0x%08x R6=0x%08x\n",
               name.c_str(), r1, r2, r5, r6);
        set_success();
    }
}
