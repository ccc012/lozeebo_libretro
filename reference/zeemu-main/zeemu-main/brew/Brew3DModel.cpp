#include "brew/Brew3D.h"
#include "brew/Brew3DCommon.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include <cstdio>
#include <string>
Brew3DModel::Brew3DModel(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}

void Brew3DModel::setup_vtable() {
    vtable_ptr_ = shell_.malloc(32 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    // Slot evidence (tutori3d trace, run log 2026-06-10):
    // - Fn5: (stack struct, index) -> I3DModel_SetTextureTbl.
    // - Fn6: (matrix ptr, R2=0xffffffff) -> I3DModel_SetSegmentMVT with
    //   segment -1 = all segments. NOT Draw: R1 matches the matrix pointer
    //   used by the preceding I3DUtil calls.
    // - Fn7: (filename ptr) -> I3DModel_Load ('ladybug.q3d', ...).
    // - Fn8: (R1 = the IBrew3D object pointer) -> I3DModel_Draw(p, pI3D).
    static const char* kNames[32] = {
        "I3DModel_AddRef", "I3DModel_Release", "I3DModel_QueryInterface",
        nullptr, nullptr,
        "I3DModel_SetTextureTbl", "I3DModel_SetSegmentMVT",
        "I3DModel_Load", "I3DModel_Draw",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    for (int i = 0; i < 32; ++i) {
        std::string name = kNames[i] ? std::string(kNames[i])
                                     : "IBrew3DModel_Fn" + std::to_string(i);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(name, this));
    }
}

void Brew3DModel::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "I3DModel_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "I3DModel_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DModel_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        if (cls == 0x010113f6) {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, object_ptr_);
            cpu.set_reg(REG_R0, 0); // SUCCESS
        } else {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, 0u);
            cpu.set_reg(REG_R0, 3); // ECLASSNOTSUPPORT
        }
    } else if (name == "I3DModel_SetTextureTbl") {
        // I3DModel_SetTextureTbl(p, &texture, index). AEE3DTexture field set
        // mirrors the sample's texture usage; exact layout pending header
        // evidence -- raw words logged under ZEEMU_TRACE_HLE for follow-up.
        if (i3d_guest_ptr(r1)) {
            TextureEntry tex;
            tex.type = memory_.read_value(r1);
            tex.sampling_mode = memory_.read_value(r1 + 4);
            tex.wrap_s = memory_.read_value(r1 + 8);
            tex.wrap_t = memory_.read_value(r1 + 12);
            tex.border_color_index = memory_.read_value(r1 + 16);
            tex.image_ptr = memory_.read_value(r1 + 20);
            uint32_t index = r2;
            if (index < 64) {
                if (index >= textures_.size()) {
                    textures_.resize(index + 1);
                }
                textures_[index] = tex;
            }
            if (i3d_trace_enabled()) {
                printf("  I3DModel_SetTextureTbl index=%u raw={0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x}\n",
                       index, tex.type, tex.sampling_mode, tex.wrap_s, tex.wrap_t,
                       tex.border_color_index, tex.image_ptr);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DModel_SetSegmentMVT") {
        // I3DModel_SetSegmentMVT(p, &matrix, segment). Trace: R1 = the same
        // matrix the guest just built with I3DUtil, R2 = 0xffffffff (-1 = all
        // segments).
        segment_mvt_matrix_ = r1;
        segment_mvt_index_ = static_cast<int32_t>(r2);
        if (i3d_trace_enabled()) {
            printf("  I3DModel_SetSegmentMVT matrix=0x%08x segment=%d\n", r1, segment_mvt_index_);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DModel_Load") {
        // I3DModel_Load(p, filename). The .q3d container belongs to the I3D
        // extension, so parsing it is HLE scope; deferred with the render
        // path (tracked in status/targets-ogles-demos.md).
        if (i3d_guest_ptr(r1)) {
            char filename[256] = {0};
            shell_.read_string(r1, filename, sizeof(filename));
            model_path_ = filename;
            loaded_ = true;
            if (i3d_trace_enabled()) printf("  I3DModel_Load path='%s'\n", filename);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3DModel_Draw") {
        // I3DModel_Draw(p, pI3D). Real draw needs the .q3d geometry + the
        // I3D render pipeline; acknowledged as implementation debt until the
        // render path lands (status/targets-ogles-demos.md).
        if (i3d_trace_enabled()) printf("  I3DModel_Draw i3d=0x%08x model='%s'\n", r1, model_path_.c_str());
        cpu.set_reg(REG_R0, 0);
    } else {
        if (i3d_log_unknown(name)) {
            printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                   name.c_str(), r0, r1, r2, r3);
        }
        cpu.set_reg(REG_R0, 0);
    }
}

