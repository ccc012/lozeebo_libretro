#include "brew/Brew3D.h"
#include "brew/BrewShell.h"
#include "graphics/RenderBackend.h"
#include "brew/Brew3DCommon.h"
#include "cpu/core/CPU.h"
#include <algorithm>
#include <cstdlib>

Brew3D::Brew3D(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory)
{
    setup_vtable();
}
void Brew3D::setup_vtable() {
    vtable_ptr_ = shell_.malloc(32 * 4);
    object_ptr_ = shell_.malloc(4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    // Slot names proven by the tutori3d trace + sample source correlation
    // (status/targets-ogles-demos.md). Slots without evidence keep FnN names.
    // - Fn3: one-arg call right before SetSegmentMVT/Draw/StartFrame in the
    //   draw flow -> I3D_ResetZBuf (tutori3d.c:1390/1460/...).
    // - Fn4/Fn5: state-id dispatcher pair. Trace shows Set states 0x2..0xe and
    //   Get(state=6, out=stack) matching I3D_GetRenderMode redraw reads.
    // - Fn6: looped per vertex-list with (norm, vlist, tri-count, R5=index) ->
    //   I3D_CalcVertexArrayNormal (model.c Obj_GetTriNorm loop).
    // - Fn14: (code ptr, pMe) -> I3D_RegisterEventNotify (tutori3d.c:376).
    // - Fn15: no-arg call after model draw -> I3D_StartFrame.
    // - Fn16: (small int, 0/1) pairs at init -> I3D_Enable(cap, on).
    static const char* kNames[32] = {
        "I3D_AddRef", "I3D_Release", "I3D_QueryInterface",
        "I3D_ResetZBuf", "I3D_Set", "I3D_Get",
        "I3D_CalcVertexArrayNormal",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        "I3D_RegisterEventNotify", "I3D_StartFrame", "I3D_Enable",
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    for (int i = 0; i < 32; ++i) {
        std::string name = kNames[i] ? std::string(kNames[i])
                                     : "IBrew3D_Fn" + std::to_string(i);
        memory_.write_value(vtable_ptr_ + static_cast<uint32_t>(i * 4), shell_.add_hook(name, this));
    }
    // AEE3DEventNotify blocks handed to the registered event callback. The
    // layout comes from the tutori3d.mod disasm of TutorI3D_EventNotify
    // (0x10007610): nEventType is a signed halfword at +4 (ldrsh [r6,+4]) and
    // nErrorCode a signed halfword at +6 (ldrsh [r6,+6]). +0 holds the I3D
    // object pointer (unread by the sample, kept SDK-shaped).
    for (auto& block : event_blocks_) {
        block = shell_.malloc(8);
        memory_.write_value(block, object_ptr_);
        memory_.write_value(block + 4, 0u, EndianMemory::Halfword);
        memory_.write_value(block + 6, 0u, EndianMemory::Halfword);
    }
}

// I3D_Set(state, value, param) dispatcher behind the per-state macros
// (I3D_SetRenderMode, I3D_SetLight, ...). State ids observed in the tutori3d
// trace; the per-state argument shapes follow the sample macro signatures.
void Brew3D::handle_set_state(CPU& cpu) {
    uint32_t state = cpu.get_reg(REG_R1);
    uint32_t value = cpu.get_reg(REG_R2);
    uint32_t param = cpu.get_reg(REG_R3);
    auto* presenter = shell_.get_presenter();
    switch (state) {
        case 0x02:
            // I3D_SetDestination(p, IBitmap*): the only init-flow Set call
            // between SetTextureTbl and CalcVertexArrayNormal, matching the
            // tutori3d.c:337 call order. Bitmap pointer arrives in R3.
            dest_bitmap_ = param;
            if (i3d_trace_enabled()) printf("  I3D_SetDestination bitmap=0x%08x\n", param);
            break;
        case 0x03:
            lighting_mode_ = value;
            if (i3d_trace_enabled()) printf("  I3D_SetLightingMode mode=%u\n", value);
            break;
        case 0x06:
            // Render mode ids (flat/smooth/texture variants) are not pinned to
            // values yet, so only track the state; no GL mapping until proven.
            render_mode_ = value;
            if (i3d_trace_enabled()) printf("  I3D_SetRenderMode mode=%u param=0x%x\n", value, param);
            break;
        case 0x07:
            focal_length_ = value;
            if (i3d_trace_enabled()) printf("  I3D_SetFocalLength value=%u param=0x%x\n", value, param);
            break;
        case 0x09:
            view_depth_near_ = value;
            view_depth_far_ = param;
            if (i3d_trace_enabled()) printf("  I3D_SetViewDepth near=%u far=%u\n", value, param);
            break;
        case 0x0b: {
            // I3D_SetLight(p, &light). R2 carries the light type (1=diffused,
            // 2=specular per AEE3D_LIGHT_* usage in lighting.c) and R3 the
            // AEE3DLight struct: direction (Q14 x,y,z) then color (r,g,b,a).
            // The struct also leads with the type so GetLight/SetLight can
            // round-trip it; both views agree in the trace (R2==[R3]).
            if (i3d_guest_ptr(param)) {
                uint32_t struct_type = memory_.read_value(param);
                uint32_t light_type = (value == 1 || value == 2) ? value : struct_type;
                LightState* light = (light_type == 2) ? &specular_light_ : &diffused_light_;
                light->direction_x = static_cast<int32_t>(memory_.read_value(param + 4));
                light->direction_y = static_cast<int32_t>(memory_.read_value(param + 8));
                light->direction_z = static_cast<int32_t>(memory_.read_value(param + 12));
                light->color_r = static_cast<int32_t>(memory_.read_value(param + 16));
                light->color_g = static_cast<int32_t>(memory_.read_value(param + 20));
                light->color_b = static_cast<int32_t>(memory_.read_value(param + 24));
                light->color_a = static_cast<int32_t>(memory_.read_value(param + 28));
                if (presenter) {
                    // Q14 direction -> float; colors step in 0..255 units.
                    float dx = static_cast<float>(light->direction_x) / 16384.0f;
                    float dy = static_cast<float>(light->direction_y) / 16384.0f;
                    float dz = static_cast<float>(light->direction_z) / 16384.0f;
                    float cr = static_cast<float>(light->color_r) / 255.0f;
                    float cg = static_cast<float>(light->color_g) / 255.0f;
                    float cb = static_cast<float>(light->color_b) / 255.0f;
                    float ca = static_cast<float>(light->color_a) / 255.0f;
                    uint32_t gl_light = (light_type == 2) ? 0x4001 : 0x4000; // GL_LIGHT1 / GL_LIGHT0
                    presenter->guest_gl_light(gl_light, 0x1203, 0, 0, 0, 0, dx, dy, dz); // GL_POSITION (directional)
                    uint32_t color_pname = (light_type == 2) ? 0x1202 : 0x1201; // GL_SPECULAR / GL_DIFFUSE
                    presenter->guest_gl_light(gl_light, color_pname, cr, cg, cb, ca, 0, 0, 0);
                    presenter->guest_gl_enable_disable(0x0B50, true); // GL_LIGHTING
                }
                if (i3d_trace_enabled()) {
                    printf("  I3D_SetLight type=%u dir=(%d,%d,%d) color=(%d,%d,%d,%d)\n",
                           light_type, light->direction_x, light->direction_y, light->direction_z,
                           light->color_r, light->color_g, light->color_b, light->color_a);
                }
            }
            break;
        }
        case 0x0d: {
            // I3D_SetMaterial(p, &material). AEE3DMaterial per lighting.c:
            // color.r/g/b/a then shininess then emissive (alpha exists -- the
            // sample's MATERIAL_ALPHA option edits material.color.a).
            if (i3d_guest_ptr(param)) {
                material_.color_r = static_cast<int32_t>(memory_.read_value(param + 0));
                material_.color_g = static_cast<int32_t>(memory_.read_value(param + 4));
                material_.color_b = static_cast<int32_t>(memory_.read_value(param + 8));
                material_.color_a = static_cast<int32_t>(memory_.read_value(param + 12));
                material_.shininess = static_cast<int32_t>(memory_.read_value(param + 16));
                material_.emissive = static_cast<int32_t>(memory_.read_value(param + 20));
                if (presenter) {
                    float cr = static_cast<float>(material_.color_r) / 255.0f;
                    float cg = static_cast<float>(material_.color_g) / 255.0f;
                    float cb = static_cast<float>(material_.color_b) / 255.0f;
                    float ca = static_cast<float>(material_.color_a) / 255.0f;
                    float emissive = static_cast<float>(material_.emissive) / 255.0f;
                    presenter->guest_gl_material(0x1201, cr, cg, cb, ca, 0); // GL_DIFFUSE
                    presenter->guest_gl_material(0x1601, 0, 0, 0, 0, static_cast<float>(material_.shininess)); // GL_SHININESS
                    presenter->guest_gl_material(0x1600, emissive, emissive, emissive, 1.0f, 0); // GL_EMISSION
                }
                if (i3d_trace_enabled()) {
                    printf("  I3D_SetMaterial color=(%d,%d,%d,%d) shininess=%d emissive=%d\n",
                           material_.color_r, material_.color_g, material_.color_b,
                           material_.color_a, material_.shininess, material_.emissive);
                }
            }
            break;
        }
        case 0x0e:
            current_texture_ = value;
            if (i3d_trace_enabled()) printf("  I3D_SetTexture value=%u param=0x%08x\n", value, param);
            break;
        default:
            if (i3d_log_unknown("I3D_Set" + std::to_string(state))) {
                printf("  I3D_Set unknown state=0x%02x value=0x%08x param=0x%08x\n", state, value, param);
            }
            break;
    }
    cpu.set_reg(REG_R0, 0); // SUCCESS
}

// I3D_Get(state, value, out) mirror of the Set dispatcher. The tutori3d trace
// shows Get(state=6, out=stack) for the I3D_GetRenderMode redraw reads.
void Brew3D::handle_get_state(CPU& cpu) {
    uint32_t state = cpu.get_reg(REG_R1);
    uint32_t value = cpu.get_reg(REG_R2);
    uint32_t out = cpu.get_reg(REG_R3);
    if (!i3d_guest_ptr(out)) {
        cpu.set_reg(REG_R0, 0);
        return;
    }
    switch (state) {
        case 0x03:
            memory_.write_value(out, lighting_mode_);
            break;
        case 0x06:
            memory_.write_value(out, render_mode_);
            break;
        case 0x07:
            memory_.write_value(out, focal_length_);
            break;
        case 0x09:
            // GetViewDepth(p, &near, &far): far pointer would be a 5th slot;
            // only near observed so far. Write near, log for follow-up.
            memory_.write_value(out, view_depth_near_);
            break;
        case 0x0b: {
            // GetLight(p, type, &light): R2 selects the light to read.
            const LightState& light = (value == 2) ? specular_light_ : diffused_light_;
            memory_.write_value(out + 0, value);
            memory_.write_value(out + 4, static_cast<uint32_t>(light.direction_x));
            memory_.write_value(out + 8, static_cast<uint32_t>(light.direction_y));
            memory_.write_value(out + 12, static_cast<uint32_t>(light.direction_z));
            memory_.write_value(out + 16, static_cast<uint32_t>(light.color_r));
            memory_.write_value(out + 20, static_cast<uint32_t>(light.color_g));
            memory_.write_value(out + 24, static_cast<uint32_t>(light.color_b));
            memory_.write_value(out + 28, static_cast<uint32_t>(light.color_a));
            break;
        }
        case 0x0d:
            memory_.write_value(out + 0, static_cast<uint32_t>(material_.color_r));
            memory_.write_value(out + 4, static_cast<uint32_t>(material_.color_g));
            memory_.write_value(out + 8, static_cast<uint32_t>(material_.color_b));
            memory_.write_value(out + 12, static_cast<uint32_t>(material_.color_a));
            memory_.write_value(out + 16, static_cast<uint32_t>(material_.shininess));
            memory_.write_value(out + 20, static_cast<uint32_t>(material_.emissive));
            break;
        case 0x0e:
            memory_.write_value(out, current_texture_);
            break;
        default:
            if (i3d_log_unknown("I3D_Get" + std::to_string(state))) {
                printf("  I3D_Get unknown state=0x%02x value=0x%08x out=0x%08x\n", state, value, out);
            }
            break;
    }
    if (i3d_trace_enabled()) {
        printf("  I3D_Get state=0x%02x value=0x%08x out=0x%08x\n", state, value, out);
    }
    cpu.set_reg(REG_R0, 0); // SUCCESS
}

// I3D_StartFrame completion events. The I3D engine renders asynchronously and
// reports progress through the registered callback; tutori3d only advances its
// draw loop (TutorI3D_ManipulateBuf + next frame) on FRAME_UPDATE_DISPLAY.
// Event values from the tutori3d.mod disasm of TutorI3D_EventNotify:
// 1=FRAME_STARTED, 2=FRAME_COMPLETED, 3=FRAME_UPDATE_DISPLAY, 4=FRAME_ERROR.
void Brew3D::queue_frame_events() {
    if (!i3d_guest_ptr(event_callback_)) {
        return;
    }
    static const uint16_t kFrameEvents[3] = {1, 2, 3};
    static const char* kLabels[3] = {
        "i3d-frame-started", "i3d-frame-completed", "i3d-frame-update-display"};
    for (int i = 0; i < 3; ++i) {
        memory_.write_value(event_blocks_[i] + 4, kFrameEvents[i], EndianMemory::Halfword);
        memory_.write_value(event_blocks_[i] + 6, 0u, EndianMemory::Halfword);
        shell_.queue_signal_callback(event_callback_, event_user_data_, event_blocks_[i], kLabels[i]);
    }
}

void Brew3D::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (name == "I3D_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "I3D_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3D_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        if (cls == 0x01013a83) {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, object_ptr_);
            cpu.set_reg(REG_R0, 0); // SUCCESS
        } else {
            if (i3d_guest_ptr(pp)) memory_.write_value(pp, 0u);
            cpu.set_reg(REG_R0, 3); // ECLASSNOTSUPPORT (AEEError.h)
        }
    } else if (name == "I3D_ResetZBuf") {
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_clear(0x0100); // GL_DEPTH_BUFFER_BIT
        }
        if (i3d_trace_enabled()) printf("  I3D_ResetZBuf\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3D_Set") {
        handle_set_state(cpu);
    } else if (name == "I3D_Get") {
        handle_get_state(cpu);
    } else if (name == "I3D_CalcVertexArrayNormal") {
        // I3D_CalcVertexArrayNormal(p, pNorm, pVlist, numTris, pVertices,
        // rmode) -- model.c:143 Obj_GetTriNorm. The normal output only feeds
        // I3D_CalcVertexArrayColor (also HLE), so the actual math is deferred
        // until the render path lands. Tracked as implementation debt in
        // status/targets-ogles-demos.md.
        if (i3d_trace_enabled()) {
            printf("  I3D_CalcVertexArrayNormal norm=0x%08x vlist=0x%08x tris=%u\n", r1, r2, r3);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3D_RegisterEventNotify") {
        // I3D_RegisterEventNotify(p, PFN3DEVENTNOTIFY, pUser) -- tutori3d.c:376.
        event_callback_ = r1;
        event_user_data_ = r2;
        if (i3d_trace_enabled()) {
            printf("  I3D_RegisterEventNotify callback=0x%08x user=0x%08x\n", r1, r2);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3D_StartFrame") {
        // I3D_StartFrame(p): kick the (future) frame render, then report
        // progress through the registered event callback so the guest draw
        // loop advances (the sample presents on FRAME_UPDATE_DISPLAY).
        if (i3d_trace_enabled()) printf("  I3D_StartFrame\n");
        queue_frame_events();
        cpu.set_reg(REG_R0, 0);
    } else if (name == "I3D_Enable") {
        // I3D_Enable(p, capability, on) -- textures_blending.c:630/740. The
        // capability ids (blending / perspective correction) are tracked by
        // raw value until a trace pins them down.
        enable_state_[r1] = r2 != 0;
        if (i3d_trace_enabled()) printf("  I3D_Enable cap=%u on=%u\n", r1, r2);
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
                    name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    }
}
