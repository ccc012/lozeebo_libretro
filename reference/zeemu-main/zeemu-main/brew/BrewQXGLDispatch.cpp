#include "brew/BrewQXGLState.h"

namespace qxgl {

void paint_device_bitmap(BrewShell& shell, EndianMemory& memory, uint16_t fill, const char* label);
void process_draw_call(BrewShell& shell, EndianMemory& memory, CPU& cpu, const std::string& label);
void process_draw_elements(BrewShell& shell, EndianMemory& memory, CPU& cpu, uint32_t mode, uint32_t count, uint32_t type, addr_t indices, const std::string& label);

bool handle_qx_gl_call(const std::string& name, BrewShell& shell, EndianMemory& memory, CPU& cpu, const char* label) {
    auto& state = qx_state();
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    const uint32_t raw_r0 = r0;
    const uint32_t raw_r1 = r1;
    const uint32_t raw_r2 = r2;
    const uint32_t raw_r3 = r3;
    const uint32_t r4 = cpu.get_reg(REG_R4);
    const uint32_t r5 = cpu.get_reg(REG_R5);
    const uint32_t r6 = cpu.get_reg(REG_R6);
    const uint32_t r7 = cpu.get_reg(REG_R7);
    const uint32_t sp = cpu.get_reg(REG_SP);
    auto stack_arg = [&](uint32_t index) -> uint32_t {
        return (sp && sp < 0xFF000000) ? memory.read_value(sp + index * 4) : 0;
    };

    const bool is_igles_method = name.rfind("IGLES_", 0) == 0;
    if (is_igles_method) {
        // IQEGL/IGLES vtable calls carry the interface object in R0. The GL
        // arguments start at R1, unlike IGL_gl*/GLProc_* veneers used by QX.
        r0 = r1;
        r1 = r2;
        r2 = r3;
        r3 = (sp && sp < 0xFF000000) ? memory.read_value(sp) : 0;
    }
    auto gl_arg = [&](uint32_t index) -> uint32_t {
        if (is_igles_method) {
            switch (index) {
                case 0: return raw_r1;
                case 1: return raw_r2;
                case 2: return raw_r3;
                default: return stack_arg(index - 3);
            }
        }
        switch (index) {
            case 0: return raw_r0;
            case 1: return raw_r1;
            case 2: return raw_r2;
            case 3: return raw_r3;
            default: return stack_arg(index - 4);
        }
    };

    auto set_success = [&]() { cpu.set_reg(REG_R0, 0); };
    auto texture_unit_index = [](uint32_t texture) -> int {
        if (texture >= kGlTexture0 && texture <= kGlTexture1) {
            return static_cast<int>(texture - kGlTexture0);
        }
        return -1;
    };
    auto light_index = [](uint32_t light) -> int {
        return (light >= kGlLight0 && light < kGlLight0 + 8u) ? static_cast<int>(light - kGlLight0) : -1;
    };
    auto active_bound_texture = [&]() -> uint32_t {
        return state.bound_textures[std::min<uint32_t>(state.active_texture_unit, 1u)];
    };
    auto current_texcoord_array = [&]() -> VertexArray& {
        return state.texcoord_arrays[std::min<uint32_t>(state.client_active_texture_unit, 1u)];
    };
    auto current_texture_env = [&]() -> TextureEnvInfo& {
        return state.texture_env_units[std::min<uint32_t>(state.active_texture_unit, 1u)];
    };
    auto sync_texture_env_aliases = [&]() {
        const TextureEnvInfo& env = state.texture_env_units[0];
        state.texture_env_mode = env.mode;
        for (int i = 0; i < 4; ++i) {
            state.texture_env_color[i] = env.color[i];
        }
        state.texture_env_combine_rgb = env.combine_rgb;
        state.texture_env_combine_alpha = env.combine_alpha;
        for (int i = 0; i < 3; ++i) {
            state.texture_env_src_rgb[i] = env.src_rgb[i];
            state.texture_env_src_alpha[i] = env.src_alpha[i];
            state.texture_env_operand_rgb[i] = env.operand_rgb[i];
            state.texture_env_operand_alpha[i] = env.operand_alpha[i];
        }
        state.texture_env_rgb_scale = env.rgb_scale;
        state.texture_env_alpha_scale = env.alpha_scale;
    };
    auto sync_unit0_aliases = [&]() {
        state.bound_texture = state.bound_textures[0];
        state.texcoord_array = state.texcoord_arrays[0];
        state.texture_2d_enabled = state.texture_2d_enabled_units[0];
        sync_texture_env_aliases();
    };
    auto decode_matrix_component = [&](uint32_t raw, bool float_args) -> float {
        if (float_args) {
            return raw_to_float(raw);
        }
        return fixed_to_float(raw);
    };

    if (name == "IGL_glActiveTexture" || name == "IGLES_ActiveTexture" || name == "GLProc_glActiveTexture") {
        const int unit = texture_unit_index(r0);
        if (unit >= 0) {
            state.active_texture_unit = static_cast<uint32_t>(unit);
            sync_unit0_aliases();
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(state.active_texture_unit);
                presenter->guest_gl_bind_texture(kGlTexture2D, state.bound_textures[state.active_texture_unit]);
            }
        }
        if (trace_gles_vertices() || trace_gles_textures()) {
            printf("  %s active texture=0x%x unit=%d\n", label, r0, unit);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glClientActiveTexture" || name == "IGLES_ClientActiveTexture" || name == "GLProc_glClientActiveTexture") {
        const int unit = texture_unit_index(r0);
        if (unit >= 0) {
            state.client_active_texture_unit = static_cast<uint32_t>(unit);
            sync_unit0_aliases();
        }
        if (trace_gles_vertices() || trace_gles_textures()) {
            printf("  %s client active texture=0x%x unit=%d\n", label, r0, unit);
        }
        set_success();
        return true;
    }

    if (name == "IGL_glMatrixMode" || name == "IGLES_MatrixMode" || name == "GLProc_glMatrixMode") {
        state.matrix_mode = r0;
        if (trace_gles_vertices()) {
            printf("  %s matrix_mode=0x%x\n", label, r0);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLoadIdentity" || name == "IGLES_LoadIdentity" || name == "GLProc_glLoadIdentity") {
        const int slot = matrix_slot(state.matrix_mode);
        state.current[slot] = identity_matrix();
        if (trace_gles_vertices()) {
            printf("  %s load identity mode=0x%x\n", label, state.matrix_mode);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLoadMatrixx" || name == "IGL_glLoadMatrixf" ||
        name == "IGLES_LoadMatrixx" || name == "IGLES_LoadMatrixf" ||
        name == "GLProc_glLoadMatrixx" || name == "GLProc_glLoadMatrixf") {
        const bool float_args = name == "IGL_glLoadMatrixf" ||
                                name == "IGLES_LoadMatrixf" ||
                                name == "GLProc_glLoadMatrixf";
        const int slot = matrix_slot(state.matrix_mode);
        auto& dst = state.current[slot];
        std::array<uint32_t, 16> raw_matrix{};
        for (int i = 0; i < 16; ++i) {
            uint32_t raw = memory.read_value(r0 + static_cast<addr_t>(i * 4));
            raw_matrix[static_cast<size_t>(i)] = raw;
            dst[i] = decode_matrix_component(raw, float_args);
        }
        if (trace_gles_vertices()) {
            printf("  %s %s loaded matrix mode=0x%x ptr=0x%08x first=(%.9g,%.9g,%.9g,%.9g)\n",
                   label, name.c_str(), state.matrix_mode, r0, dst[0], dst[1], dst[2], dst[3]);
            if (slot == 2 && std::getenv("ZEEMU_TRACE_GLES_TEXT")) {
                printf("  %s texture matrix raw=[%08x %08x %08x %08x] [%08x %08x %08x %08x] [%08x %08x %08x %08x] [%08x %08x %08x %08x]\n",
                       label,
                       raw_matrix[0], raw_matrix[4], raw_matrix[8], raw_matrix[12],
                       raw_matrix[1], raw_matrix[5], raw_matrix[9], raw_matrix[13],
                       raw_matrix[2], raw_matrix[6], raw_matrix[10], raw_matrix[14],
                       raw_matrix[3], raw_matrix[7], raw_matrix[11], raw_matrix[15]);
                printf("  %s texture matrix rows=[%.9g %.9g %.9g %.9g] [%.9g %.9g %.9g %.9g] [%.9g %.9g %.9g %.9g] [%.9g %.9g %.9g %.9g]\n",
                       label,
                       dst[0], dst[4], dst[8], dst[12],
                       dst[1], dst[5], dst[9], dst[13],
                       dst[2], dst[6], dst[10], dst[14],
                       dst[3], dst[7], dst[11], dst[15]);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glMultMatrixx" || name == "IGL_glMultMatrixf" ||
        name == "IGLES_MultMatrixx" || name == "IGLES_MultMatrixf" ||
        name == "GLProc_glMultMatrixx" || name == "GLProc_glMultMatrixf") {
        const bool float_args = name == "IGL_glMultMatrixf" ||
                                name == "IGLES_MultMatrixf" ||
                                name == "GLProc_glMultMatrixf";
        std::array<float, 16> rhs{};
        const int slot = matrix_slot(state.matrix_mode);
        for (int i = 0; i < 16; ++i) {
            uint32_t raw = memory.read_value(r0 + static_cast<addr_t>(i * 4));
            rhs[i] = decode_matrix_component(raw, float_args);
        }
        state.current[slot] = multiply_matrix(state.current[slot], rhs);
        if (trace_gles_vertices()) {
            printf("  %s %s mult matrix mode=0x%x ptr=0x%08x first=(%.9g,%.9g,%.9g,%.9g)\n",
                   label, name.c_str(), state.matrix_mode, r0, rhs[0], rhs[1], rhs[2], rhs[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glTranslatex" || name == "IGL_glTranslatef" ||
        name == "IGLES_Translatex" || name == "IGLES_Translatef" ||
        name == "GLProc_glTranslatex" || name == "GLProc_glTranslatef") {
        const bool float_args = name == "IGL_glTranslatef" || name == "IGLES_Translatef" || name == "GLProc_glTranslatef";
        const float x = float_args ? raw_to_float(r0) : fixed_to_float(r0);
        const float y = float_args ? raw_to_float(r1) : fixed_to_float(r1);
        const float z = float_args ? raw_to_float(r2) : fixed_to_float(r2);
        state.current[matrix_slot(state.matrix_mode)] = multiply_matrix(state.current[matrix_slot(state.matrix_mode)], translate_matrix(x, y, z));
        if (trace_gles_vertices()) {
            printf("  %s translate=(%g,%g,%g)\n", label, x, y, z);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glScalex" || name == "IGL_glScalef" ||
        name == "IGLES_Scalex" || name == "IGLES_Scalef" ||
        name == "GLProc_glScalex" || name == "GLProc_glScalef") {
        const bool float_args = name == "IGL_glScalef" || name == "IGLES_Scalef" || name == "GLProc_glScalef";
        const float x = float_args ? raw_to_float(r0) : fixed_to_float(r0);
        const float y = float_args ? raw_to_float(r1) : fixed_to_float(r1);
        const float z = float_args ? raw_to_float(r2) : fixed_to_float(r2);
        state.current[matrix_slot(state.matrix_mode)] = multiply_matrix(state.current[matrix_slot(state.matrix_mode)], scale_matrix(x, y, z));
        if (trace_gles_vertices()) {
            printf("  %s scale=(%g,%g,%g)\n", label, x, y, z);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glRotatex" || name == "IGL_glRotatef" ||
        name == "IGLES_Rotatex" || name == "IGLES_Rotatef" ||
        name == "GLProc_glRotatex" || name == "GLProc_glRotatef") {
        const bool float_args = name == "IGL_glRotatef" || name == "IGLES_Rotatef" || name == "GLProc_glRotatef";
        const float angle = float_args ? raw_to_float(r0) : fixed_to_float(r0);
        const float x = float_args ? raw_to_float(r1) : fixed_to_float(r1);
        const float y = float_args ? raw_to_float(r2) : fixed_to_float(r2);
        const float z = float_args ? raw_to_float(r3) : fixed_to_float(r3);
        state.current[matrix_slot(state.matrix_mode)] = multiply_matrix(state.current[matrix_slot(state.matrix_mode)], rotate_matrix(angle, x, y, z));
        if (trace_gles_vertices()) {
            printf("  %s rotate=%g axis=(%g,%g,%g)\n", label, angle, x, y, z);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glMultiTexCoord4x" || name == "IGL_glMultiTexCoord4f" ||
        name == "IGLES_MultiTexCoord4x" || name == "IGLES_MultiTexCoord4f" ||
        name == "GLProc_glMultiTexCoord4x" || name == "GLProc_glMultiTexCoord4f") {
        const bool float_args = name == "IGL_glMultiTexCoord4f" ||
                                name == "IGLES_MultiTexCoord4f" ||
                                name == "GLProc_glMultiTexCoord4f";
        const int unit = texture_unit_index(r0);
        if (unit >= 0) {
            auto& coord = state.current_texcoord[static_cast<size_t>(unit)];
            coord[0] = float_args ? raw_to_float(r1) : fixed_to_float(r1);
            coord[1] = float_args ? raw_to_float(r2) : fixed_to_float(r2);
            coord[2] = float_args ? raw_to_float(r3) : fixed_to_float(r3);
            const uint32_t q_raw = stack_arg(is_igles_method ? 1 : 0);
            coord[3] = float_args ? raw_to_float(q_raw) : fixed_to_float(q_raw);
            if (trace_gles_vertices()) {
                printf("  %s multitexcoord unit=%d coord=(%g,%g,%g,%g)\n",
                       label, unit, coord[0], coord[1], coord[2], coord[3]);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glColor4x" || name == "IGLES_Color4x" || name == "GLProc_glColor4x") {
        state.current_color[0] = fixed_to_float(r0);
        state.current_color[1] = fixed_to_float(r1);
        state.current_color[2] = fixed_to_float(r2);
        state.current_color[3] = fixed_to_float(r3);
        if (trace_gles_vertices()) {
            printf("  %s color=(%.3f,%.3f,%.3f,%.3f)\n", label,
                   state.current_color[0], state.current_color[1],
                   state.current_color[2], state.current_color[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glColor4f" || name == "IGLES_Color4f" || name == "GLProc_glColor4f") {
        state.current_color[0] = raw_to_float(r0);
        state.current_color[1] = raw_to_float(r1);
        state.current_color[2] = raw_to_float(r2);
        state.current_color[3] = raw_to_float(r3);
        if (trace_gles_vertices()) {
            printf("  %s color=(%.3f,%.3f,%.3f,%.3f)\n", label,
                   state.current_color[0], state.current_color[1],
                   state.current_color[2], state.current_color[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGLES_Color4ub" || name == "GLProc_glColor4ub") {
        constexpr float inv255 = 1.0f / 255.0f;
        state.current_color[0] = static_cast<float>(r0 & 0xffu) * inv255;
        state.current_color[1] = static_cast<float>(r1 & 0xffu) * inv255;
        state.current_color[2] = static_cast<float>(r2 & 0xffu) * inv255;
        state.current_color[3] = static_cast<float>(r3 & 0xffu) * inv255;
        if (trace_gles_vertices()) {
            printf("  %s color4ub=(%u,%u,%u,%u) color=(%.3f,%.3f,%.3f,%.3f)\n", label,
                   r0 & 0xffu, r1 & 0xffu, r2 & 0xffu, r3 & 0xffu,
                   state.current_color[0], state.current_color[1],
                   state.current_color[2], state.current_color[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glOrthox" || name == "IGL_glOrthof" ||
        name == "IGLES_Orthox" || name == "IGLES_Orthof" ||
        name == "GLProc_glOrthox" || name == "GLProc_glOrthof") {
        const bool float_args = name == "IGL_glOrthof" || name == "IGLES_Orthof" || name == "GLProc_glOrthof";
        float left = float_args ? raw_to_float(r0) : fixed_to_float(r0);
        float right = float_args ? raw_to_float(r1) : fixed_to_float(r1);
        float bottom = float_args ? raw_to_float(r2) : fixed_to_float(r2);
        float top = float_args ? raw_to_float(r3) : fixed_to_float(r3);
        const uint32_t znear_raw = stack_arg(is_igles_method ? 1 : 0);
        const uint32_t zfar_raw = stack_arg(is_igles_method ? 2 : 1);
        const float znear = float_args ? raw_to_float(znear_raw) : fixed_to_float(znear_raw);
        const float zfar = float_args ? raw_to_float(zfar_raw) : fixed_to_float(zfar_raw);
        if (std::fabs(right - left) > 4096.0f || std::fabs(top - bottom) > 4096.0f ||
            std::fabs(right - left) < 1.0f || std::fabs(top - bottom) < 1.0f) {
            if (BrewDisplay* display = shell.get_display()) {
                if (BrewBitmap* bmp = display->get_device_bitmap()) {
                    left = 0.0f;
                    right = static_cast<float>(bmp->get_width());
                    bottom = static_cast<float>(bmp->get_height());
                    top = 0.0f;
                }
            }
        }
        state.current[matrix_slot(state.matrix_mode)] = multiply_matrix(state.current[matrix_slot(state.matrix_mode)], ortho_matrix(left, right, bottom, top, znear, zfar));
        if (trace_qxgl_calls()) {
            printf("  %s ortho left=%g right=%g bottom=%g top=%g\n", label, left, right, bottom, top);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glFrustumx" || name == "IGL_glFrustumf" ||
        name == "IGLES_Frustumx" || name == "IGLES_Frustumf" ||
        name == "GLProc_glFrustumx" || name == "GLProc_glFrustumf") {
        const bool float_args = name == "IGL_glFrustumf" || name == "IGLES_Frustumf" || name == "GLProc_glFrustumf";
        const float left = float_args ? raw_to_float(r0) : fixed_to_float(r0);
        const float right = float_args ? raw_to_float(r1) : fixed_to_float(r1);
        const float bottom = float_args ? raw_to_float(r2) : fixed_to_float(r2);
        const float top = float_args ? raw_to_float(r3) : fixed_to_float(r3);
        const uint32_t znear_raw = stack_arg(is_igles_method ? 1 : 0);
        const uint32_t zfar_raw = stack_arg(is_igles_method ? 2 : 1);
        const float znear = float_args ? raw_to_float(znear_raw) : fixed_to_float(znear_raw);
        const float zfar = float_args ? raw_to_float(zfar_raw) : fixed_to_float(zfar_raw);
        state.current[matrix_slot(state.matrix_mode)] = multiply_matrix(state.current[matrix_slot(state.matrix_mode)], frustum_matrix(left, right, bottom, top, znear, zfar));
        if (trace_qxgl_calls()) {
            printf("  %s frustum left=%g right=%g bottom=%g top=%g near=%g far=%g\n",
                   label, left, right, bottom, top, znear, zfar);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glPushMatrix" || name == "IGLES_PushMatrix" || name == "GLProc_glPushMatrix") {
        const int slot = matrix_slot(state.matrix_mode);
        auto& stack = state.stacks[slot];
        stack.push_back(state.current[slot]);
        set_success();
        return true;
    }
    if (name == "IGL_glPopMatrix" || name == "IGLES_PopMatrix" || name == "GLProc_glPopMatrix") {
        const int slot = matrix_slot(state.matrix_mode);
        auto& stack = state.stacks[slot];
        if (!stack.empty()) {
            state.current[slot] = stack.back();
            stack.pop_back();
        }
        set_success();
        return true;
    }
    if (name == "IGL_glViewport" || name == "IGLES_Viewport" || name == "GLProc_glViewport") {
        if (trace_gles_vertices()) {
            printf("  %s viewport raw=(0x%08x,0x%08x,0x%08x,0x%08x) norm=(0x%08x,0x%08x,0x%08x,0x%08x) sp=0x%08x stack=(0x%08x,0x%08x,0x%08x,0x%08x)\n",
                   label,
                   raw_r0, raw_r1, raw_r2, raw_r3,
                   r0, r1, r2, r3,
                   sp,
                   stack_arg(0), stack_arg(1), stack_arg(2), stack_arg(3));
        }
        auto x = static_cast<int32_t>(r0);
        auto y = static_cast<int32_t>(r1);
        auto w = static_cast<int32_t>(r2);
        auto h = static_cast<int32_t>(r3);
        if (w > 4096 && (r2 & 0xffffu) != 0 && (r2 >> 16) != 0) {
            // Some RVCT/BREW GL veneers observed in OGL ES demo 01 pack
            // height:width into R2 for glViewport(0, 0, width, height).
            w = static_cast<int32_t>(r2 & 0xffffu);
            h = static_cast<int32_t>(r2 >> 16);
        } else if (h > 4096 && (r3 & 0xffffu) != 0 && (r3 >> 16) != 0) {
            h = static_cast<int32_t>(r3 & 0xffffu);
        }
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
            if (BrewDisplay* display = shell.get_display()) {
                if (BrewBitmap* bmp = display->get_device_bitmap()) {
                    x = 0;
                    y = 0;
                    w = bmp->get_width();
                    h = bmp->get_height();
                }
            }
        }
        state.viewport[0] = x;
        state.viewport[1] = y;
        state.viewport[2] = w;
        state.viewport[3] = h;
        if (trace_qxgl_calls()) {
            printf("  %s x=%d y=%d w=%d h=%d\n", label, state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        }
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_viewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glGetIntegerv" || name == "IGLES_GetIntegerv" || name == "GLProc_glGetIntegerv") {
        const uint32_t pname = r0;
        const addr_t params = r1;
        if (params) {
            switch (pname) {
                case kGlViewport:
                    memory.write_value(params + 0, static_cast<uint32_t>(state.viewport[0]));
                    memory.write_value(params + 4, static_cast<uint32_t>(state.viewport[1]));
                    memory.write_value(params + 8, static_cast<uint32_t>(state.viewport[2]));
                    memory.write_value(params + 12, static_cast<uint32_t>(state.viewport[3]));
                    break;
                case kGlScissorBox:
                    memory.write_value(params + 0, 0u);
                    memory.write_value(params + 4, 0u);
                    memory.write_value(params + 8, static_cast<uint32_t>(state.viewport[2]));
                    memory.write_value(params + 12, static_cast<uint32_t>(state.viewport[3]));
                    break;
                case kGlMaxViewportDims:
                    memory.write_value(params + 0, 1024u);
                    memory.write_value(params + 4, 1024u);
                    break;
                case kGlArrayBufferBinding:
                    memory.write_value(params, state.bound_array_buffer);
                    break;
                case kGlElementArrayBufferBinding:
                    memory.write_value(params, state.bound_element_buffer);
                    break;
                case kGlTextureBinding2D:
                    memory.write_value(params, active_bound_texture());
                    break;
                case kGlUnpackAlignment:
                    memory.write_value(params, state.unpack_alignment);
                    break;
                case kGlPackAlignment:
                    memory.write_value(params, 4u);
                    break;
                case kGlActiveTexture:
                    memory.write_value(params, kGlTexture0 + std::min<uint32_t>(state.active_texture_unit, 1u));
                    break;
                case kGlClientActiveTexture:
                    memory.write_value(params, kGlTexture0 + std::min<uint32_t>(state.client_active_texture_unit, 1u));
                    break;
                case kGlModelviewStackDepth:
                case kGlProjectionStackDepth:
                case kGlTextureStackDepth:
                    memory.write_value(params, 1u + static_cast<uint32_t>(state.stacks[matrix_slot(pname == kGlModelviewStackDepth ? kGlModelview :
                                                                                           pname == kGlProjectionStackDepth ? kGlProjection : kGlTexture)].size()));
                    break;
                case kGlMaxTextureSize:
                    memory.write_value(params, 1024u);
                    break;
                case kGlMaxTextureUnits:
                    memory.write_value(params, 2u);
                    break;
                case kGlMaxElementsVertices:
                case kGlMaxElementsIndices:
                    memory.write_value(params, 4096u);
                    break;
                case kGlNumCompressedTextureFormats:
                    memory.write_value(params, 0u);
                    break;
                case kGlCompressedTextureFormats:
                    break;
                case kGlFramebufferBinding:
                    memory.write_value(params, 0u);
                    break;
                case kGlMaxVaryingVectors:
                    memory.write_value(params, 0u);
                    break;
                case kGlMajorVersion:
                    memory.write_value(params, 1u);
                    break;
                case kGlMinorVersion:
                    memory.write_value(params, 1u);
                    break;
                case kGlRedBits:
                case kGlGreenBits:
                case kGlBlueBits:
                    memory.write_value(params, 5u);
                    break;
                case kGlAlphaBits:
                    memory.write_value(params, 0u);
                    break;
                case kGlDepthBits:
                    memory.write_value(params, 16u);
                    break;
                case kGlStencilBits:
                case kGlSampleBuffers:
                case kGlSamples:
                    memory.write_value(params, 0u);
                    break;
                default:
                    memory.write_value(params, 1u);
                    break;
            }
        }
        set_success();
        return true;
    }
    if (name == "IGLES_GetFloatv" || name == "GLProc_glGetFloatv") {
        const uint32_t pname = r0;
        const addr_t params = r1;
        if (params) {
            switch (pname) {
                case kGlViewport:
                    write_float(memory, params + 0, static_cast<float>(state.viewport[0]));
                    write_float(memory, params + 4, static_cast<float>(state.viewport[1]));
                    write_float(memory, params + 8, static_cast<float>(state.viewport[2]));
                    write_float(memory, params + 12, static_cast<float>(state.viewport[3]));
                    break;
                case kGlModelviewMatrix:
                    write_matrix_float(memory, params, state.current[0]);
                    break;
                case kGlProjectionMatrix:
                    write_matrix_float(memory, params, state.current[1]);
                    break;
                case kGlTextureMatrix:
                    write_matrix_float(memory, params, state.current[2]);
                    break;
                default:
                    write_float(memory, params, 0.0f);
                    break;
            }
        }
        set_success();
        return true;
    }
    if (name == "IGLES_GetFixedv" || name == "GLProc_glGetFixedv") {
        const uint32_t pname = r0;
        const addr_t params = r1;
        if (params) {
            switch (pname) {
                case kGlModelviewMatrix:
                    write_matrix_fixed(memory, params, state.current[0]);
                    break;
                case kGlProjectionMatrix:
                    write_matrix_fixed(memory, params, state.current[1]);
                    break;
                case kGlTextureMatrix:
                    write_matrix_fixed(memory, params, state.current[2]);
                    break;
                default:
                    memory.write_value(params, 0u);
                    break;
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glBindBuffer" || name == "IGLES_BindBuffer" || name == "GLProc_glBindBuffer" || name == "GLProc_glBindBufferARB" || name == "GLProc_glBindBufferOES" || name == "GLProc_glBindBufferQUALCOMM") {
        const uint32_t target = r0;
        const uint32_t buffer = r1;
        if (target == kGlArrayBuffer) {
            state.bound_array_buffer = buffer;
        } else if (target == kGlElementArrayBuffer) {
            state.bound_element_buffer = buffer;
        }
        if (trace_qxgl_calls()) {
            printf("  %s target=0x%x buffer=%u\n", label, target, buffer);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glGenBuffers" || name == "IGLES_GenBuffers" || name == "GLProc_glGenBuffers" || name == "GLProc_glGenBuffersARB" || name == "GLProc_glGenBuffersOES" || name == "GLProc_glGenBuffersQUALCOMM") {
        const uint32_t n = r0;
        const addr_t buffers = r1;
        for (uint32_t i = 0; i < n; ++i) {
            memory.write_value(buffers + static_cast<addr_t>(i * 4), state.next_buffer_id++);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glBufferData" || name == "IGLES_BufferData" || name == "GLProc_glBufferData" || name == "GLProc_glBufferDataARB" || name == "GLProc_glBufferDataOES" || name == "GLProc_glBufferDataQUALCOMM") {
        const uint32_t target = r0;
        const uint32_t size = r1;
        const addr_t data = r2;
        std::vector<uint8_t> bytes(size);
        for (uint32_t i = 0; i < size; ++i) {
            bytes[i] = static_cast<uint8_t>(data ? memory.read_value(data + i, EndianMemory::Byte) : 0);
        }
        if (target == kGlArrayBuffer) {
            state.buffers[state.bound_array_buffer] = std::move(bytes);
        } else if (target == kGlElementArrayBuffer) {
            state.buffers[state.bound_element_buffer] = std::move(bytes);
        }
        if (trace_qxgl_calls()) {
            printf("  %s target=0x%x size=%u usage=0x%x\n", label, target, size, r3);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDeleteBuffers" || name == "IGLES_DeleteBuffers" || name == "GLProc_glDeleteBuffers" || name == "GLProc_glDeleteBuffersQUALCOMM") {
        const uint32_t n = r0;
        const addr_t buffers = r1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = memory.read_value(buffers + i * 4);
            state.buffers.erase(id);
            if (state.bound_array_buffer == id) state.bound_array_buffer = 0;
            if (state.bound_element_buffer == id) state.bound_element_buffer = 0;
        }
        set_success();
        return true;
    }
    if (name == "IGLES_IsBuffer" || name == "GLProc_glIsBuffer") {
        const uint32_t buffer = r0;
        cpu.set_reg(REG_R0, state.buffers.count(buffer) ? 1u : 0u);
        return true;
    }
    if (name == "IGLES_GetBufferParameteriv" || name == "GLProc_glGetBufferParameteriv") {
        const uint32_t target = r0;
        const uint32_t pname = r1;
        const addr_t params = r2;
        uint32_t value = 0;
        uint32_t buffer_id = (target == kGlArrayBuffer) ? state.bound_array_buffer : state.bound_element_buffer;
        const auto it = state.buffers.find(buffer_id);
        if (it != state.buffers.end()) {
            if (pname == kGlBufferSize) {
                value = static_cast<uint32_t>(it->second.size());
            } else if (pname == kGlBufferUsage) {
                value = 0x88E4; // STATIC_DRAW
            }
        }
        if (params) memory.write_value(params, value);
        set_success();
        return true;
    }
    if (name == "IGLES_PointSizePointerOES" || name == "GLProc_glPointSizePointerOES") {
        state.point_size_pointer = r2;
        if (trace_qxgl_calls()) {
            printf("  %s ptr=0x%08x stride=%u type=0x%x\n", label, state.point_size_pointer, r1, r0);
        }
        set_success();
        return true;
    }
    if (name == "IGLES_GetPointerv" || name == "GLProc_glGetPointerv") {
        const uint32_t pname = r0;
        const addr_t params = r1;
        uint32_t value = 0;
        if (pname == 0x8645) { // GL_POINT_SIZE_ARRAY_POINTER_OES
            value = state.point_size_pointer;
        }
        if (params) memory.write_value(params, value);
        set_success();
        return true;
    }
    if (name == "IGLES_GetBooleanv" || name == "GLProc_glGetBooleanv" || name == "IGLES_IsEnabled" || name == "GLProc_glIsEnabled" || name == "IGLES_IsTexture" || name == "GLProc_glIsTexture") {
        uint32_t value = 0;
        if (r0 == kGlTexture2D) value = state.texture_2d_enabled_units[std::min<uint32_t>(state.active_texture_unit, 1u)] ? 1u : 0u;
        if (r0 == kGlDither) value = state.dither_enabled ? 1u : 0u;
        if (r0 == kGlDepthTest) value = state.depth_test_enabled ? 1u : 0u;
        if (r0 == kGlStencilTest) value = state.stencil_test_enabled ? 1u : 0u;
        if (r0 == kGlCullFace) value = state.cull_face_enabled ? 1u : 0u;
        if (r0 == kGlAlphaTest) value = state.alpha_test_enabled ? 1u : 0u;
        if (r0 == kGlBlend) value = state.blend_enabled ? 1u : 0u;
        if (r0 == kGlFog) value = state.fog_enabled ? 1u : 0u;
        if (r0 == kGlLighting) value = state.lighting_enabled ? 1u : 0u;
        const int light = light_index(r0);
        if (light >= 0) value = state.light_enabled[static_cast<size_t>(light)] ? 1u : 0u;
        if (name == "IGLES_IsTexture" || name == "GLProc_glIsTexture") {
            value = state.textures.count(r0) ? 1u : 0u;
        }
        if (!(name == "IGLES_IsEnabled" || name == "GLProc_glIsEnabled" ||
              name == "IGLES_IsTexture" || name == "GLProc_glIsTexture") && r1) {
            memory.write_value(r1, value);
        }
        if (name.find("Is") != std::string::npos) {
            cpu.set_reg(REG_R0, value);
        } else {
            set_success();
        }
        return true;
    }
    if (name == "IGL_glGenTextures" || name == "IGLES_GenTextures" || name == "GLProc_glGenTextures") {
        const uint32_t n = r0;
        const addr_t textures = r1;
        for (uint32_t i = 0; i < n; ++i) {
            memory.write_value(textures + static_cast<addr_t>(i * 4), state.next_texture_id++);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glBindTexture" || name == "IGLES_BindTexture" || name == "GLProc_glBindTexture") {
        if (r0 == kGlTexture2D) {
            const uint32_t unit = std::min<uint32_t>(state.active_texture_unit, 1u);
            state.bound_textures[unit] = r1;
            sync_unit0_aliases();
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(unit);
                presenter->guest_gl_bind_texture(r0, r1);
            }
        }
        if (trace_gles_textures()) {
            printf("  %s target=0x%x texture=%u unit=%u\n", label, r0, r1, state.active_texture_unit);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDeleteTextures" || name == "IGLES_DeleteTextures" || name == "GLProc_glDeleteTextures") {
        const uint32_t n = r0;
        const addr_t textures = r1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t texture = memory.read_value(textures + static_cast<addr_t>(i * 4));
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_delete_texture(texture);
            }
            state.textures.erase(texture);
            for (auto& bound : state.bound_textures) {
                if (bound == texture) {
                    bound = 0;
                }
            }
            sync_unit0_aliases();
        }
        set_success();
        return true;
    }
    if (name == "IGL_glPixelStorei" || name == "IGLES_PixelStorei" || name == "GLProc_glPixelStorei") {
        if (r0 == 0x0CF5) { // GL_UNPACK_ALIGNMENT
            state.unpack_alignment = r1 ? r1 : 1;
        }
        set_success();
        return true;
    }
    const auto bytes_per_pixel_for = [](uint32_t format, uint32_t type) -> int {
        if (format == kGlRgba && type == kGlUnsignedByte) {
            return 4;
        }
        if (format == kGlRgb && type == kGlUnsignedByte) {
            return 3;
        }
        if ((format == 0x1906 || format == 0x1909) && type == kGlUnsignedByte) {
            return 1; // GL_ALPHA / GL_LUMINANCE
        }
        if (format == 0x190A && type == kGlUnsignedByte) {
            return 2; // GL_LUMINANCE_ALPHA
        }
        if (format == kGlRgb && type == kGlUnsignedShort565) {
            return 2;
        }
        if (format == kGlRgba && (type == 0x8033 || type == 0x8034)) {
            return 2; // GL_UNSIGNED_SHORT_4_4_4_4 / GL_UNSIGNED_SHORT_5_5_5_1
        }
        return 0;
    };
    auto texture_payload = [&](int width, int height, uint32_t format, uint32_t type, addr_t pixels) {
        const int bytes_per_pixel = bytes_per_pixel_for(format, type);
        if (width <= 0 || height <= 0 || bytes_per_pixel == 0 || !pixels) {
            return std::vector<uint8_t>{};
        }
        const uint64_t size64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(bytes_per_pixel);
        if (size64 > 16ull * 1024ull * 1024ull) {
            return std::vector<uint8_t>{};
        }
        const size_t size = static_cast<size_t>(size64);
        std::string raw = memory.read(pixels, size);
        std::vector<uint8_t> bytes(raw.begin(), raw.end());
        return bytes;
    };
    const auto plausible_dim = [](uint32_t value) {
        return value > 0 && value <= 2048 && value != kGlTexture2D && value != 0x812F && value != kGlRgba && value != kGlRgb && value != kGlUnsignedByte;
    };
    const auto plausible_format = [](uint32_t value) {
        return value == kGlRgb || value == kGlRgba || value == 0x1906 || value == 0x1909 || value == 0x190A;
    };
    const auto plausible_type = [](uint32_t value) {
        return value == kGlUnsignedByte || value == kGlUnsignedShort565 || value == 0x8033 || value == 0x8034;
    };
    const auto plausible_pointer = [](uint32_t value) {
        return value >= 0x10000 && value < 0xFF000000;
    };
    const auto choose_first = [](std::initializer_list<uint32_t> values, auto pred, uint32_t fallback) -> uint32_t {
        for (uint32_t value : values) {
            if (pred(value)) {
                return value;
            }
        }
        return fallback;
    };
    if (name == "IGL_glTexImage2D" || name == "IGLES_TexImage2D" || name == "GLProc_glTexImage2D") {
        std::vector<uint32_t> stack_words;
        stack_words.reserve(16);
        for (uint32_t i = 0; i < 16; ++i) {
            stack_words.push_back(stack_arg(i));
        }
        if (trace_gles_vertices()) {
            printf("  %s teximage raw r=(%08x,%08x,%08x,%08x) r4=%08x r5=%08x r6=%08x r7=%08x sp=%08x stack=(%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x)\n",
                   label,
                   raw_r0, raw_r1, raw_r2, raw_r3,
                   r4, r5, r6, r7, sp,
                   stack_words[0], stack_words[1], stack_words[2], stack_words[3], stack_words[4],
                   stack_words[5], stack_words[6], stack_words[7], stack_words[8], stack_words[9]);
        }
        auto choose_stack_dim = [&]() -> uint32_t {
            for (uint32_t value : stack_words) {
                if (plausible_dim(value)) return value;
            }
            return 0;
        };
        const uint32_t target = gl_arg(0);
        const uint32_t level = gl_arg(1);
        const uint32_t internal_format = gl_arg(2);
        const uint32_t explicit_width = gl_arg(3);
        const uint32_t explicit_height = gl_arg(4);
        const addr_t explicit_pixels = gl_arg(8);
        uint32_t width_u = plausible_dim(explicit_width) ? explicit_width : 0;
        uint32_t height_u = plausible_dim(explicit_height) ? explicit_height : 0;
        if ((!width_u || !height_u) && plausible_pointer(explicit_pixels)) {
            auto it = texture_payload_hints().find(explicit_pixels);
            if (it != texture_payload_hints().end()) {
                width_u = it->second.width;
                height_u = it->second.height;
            }
        }
        if ((!width_u || !height_u) && !plausible_pointer(explicit_pixels)) {
            auto& pending_hints = pending_texture_payload_hints();
            if (!pending_hints.empty()) {
                const TexturePayloadHint hint = pending_hints.front();
                pending_hints.erase(pending_hints.begin());
                width_u = next_power_of_two(hint.width);
                height_u = next_power_of_two(hint.height);
            }
        }
        if ((!width_u || !height_u) && !is_igles_method) {
            width_u = choose_first({r4, r3, stack_arg(0), stack_arg(1), r7, choose_stack_dim()}, plausible_dim, width_u ? width_u : r3);
            height_u = choose_first({r7, stack_arg(0), stack_arg(1), r4, choose_stack_dim()}, plausible_dim, height_u ? height_u : width_u);
        }
        const uint32_t border_u = gl_arg(5) <= 1 ? gl_arg(5) : 0;
        const uint32_t format = plausible_format(gl_arg(6)) ? gl_arg(6) : choose_first({stack_arg(2), stack_arg(3), r2, r5, r6}, plausible_format, internal_format);
        const uint32_t type = plausible_type(gl_arg(7)) ? gl_arg(7) : choose_first({stack_arg(3), stack_arg(4), r7, r4}, plausible_type, kGlUnsignedByte);
        const addr_t pixels = plausible_pointer(explicit_pixels) ? explicit_pixels :
            (is_igles_method ? 0 : choose_first({stack_arg(4), stack_arg(5), stack_arg(6), r7, r4}, plausible_pointer, 0));
        const int width = static_cast<int>(width_u);
        const int height = static_cast<int>(height_u);
        const int border = static_cast<int>(border_u);
        std::vector<uint8_t> bytes = texture_payload(width, height, format, type, pixels);
        const uint32_t bound_texture = active_bound_texture();
        const bool upload_dims_ok = width > 0 && height > 0 &&
            (static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= 4096ull * 4096ull);
        if (upload_dims_ok) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
                presenter->guest_gl_tex_image_2d(target, static_cast<int>(level), static_cast<int>(internal_format), width, height, border, format, type, bytes.empty() ? nullptr : bytes.data());
            }
            if (target == kGlTexture2D && bound_texture != 0 && level == 0) {
                TextureInfo& info = state.textures[bound_texture];
                info.width = width;
                info.height = height;
                info.format = format;
                info.type = type;
            }
        }
        if (trace_gles_textures()) {
            printf("  %s target=0x%x size=%dx%d format=0x%x type=0x%x pixels=0x%08x\n",
                   label, target, width, height, format, type, pixels);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glTexSubImage2D" || name == "IGLES_TexSubImage2D" || name == "GLProc_glTexSubImage2D") {
        const uint32_t target = gl_arg(0);
        const uint32_t level = gl_arg(1);
        const int xoffset = static_cast<int>(gl_arg(2));
        const int yoffset = static_cast<int>(choose_first({gl_arg(3), r4, stack_arg(0)}, [](uint32_t value) { return value <= 4096; }, 0));
        std::vector<uint32_t> stack_words;
        stack_words.reserve(16);
        for (uint32_t i = 0; i < 16; ++i) {
            stack_words.push_back(stack_arg(i));
        }
        if (trace_gles_vertices()) {
            printf("  %s texsub raw r=(%08x,%08x,%08x,%08x) r4=%08x r5=%08x r6=%08x r7=%08x sp=%08x stack=(%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x)\n",
                   label,
                   raw_r0, raw_r1, raw_r2, raw_r3,
                   r4, r5, r6, r7, sp,
                   stack_words[0], stack_words[1], stack_words[2], stack_words[3], stack_words[4],
                   stack_words[5], stack_words[6], stack_words[7], stack_words[8], stack_words[9]);
        }
        auto choose_stack_dim = [&]() -> uint32_t {
            for (uint32_t value : stack_words) {
                if (plausible_dim(value)) return value;
            }
            return 0;
        };
        const uint32_t width_u = plausible_dim(gl_arg(4)) ? gl_arg(4) : choose_first({r7, stack_arg(0), stack_arg(1), r4, choose_stack_dim()}, plausible_dim, 0);
        const uint32_t height_u = plausible_dim(gl_arg(5)) ? gl_arg(5) : choose_first({stack_arg(1), r7, r4, choose_stack_dim()}, plausible_dim, width_u);
        const uint32_t format = plausible_format(gl_arg(6)) ? gl_arg(6) : choose_first({stack_arg(2), stack_arg(3), r5, r6}, plausible_format, kGlRgba);
        const uint32_t type = plausible_type(gl_arg(7)) ? gl_arg(7) : choose_first({stack_arg(3), stack_arg(4), r7, r4}, plausible_type, kGlUnsignedByte);
        const addr_t pixels = plausible_pointer(gl_arg(8)) ? gl_arg(8) : choose_first({stack_arg(4), stack_arg(5), stack_arg(6), r7, r4}, plausible_pointer, 0);
        uint32_t resolved_width = width_u;
        uint32_t resolved_height = height_u;
        if ((!plausible_dim(resolved_width) || !plausible_dim(resolved_height)) && pixels) {
            auto it = texture_payload_hints().find(pixels);
            if (it != texture_payload_hints().end()) {
                resolved_width = it->second.width;
                resolved_height = it->second.height;
            }
        }
        const int width = static_cast<int>(resolved_width);
        const int height = static_cast<int>(resolved_height);
        std::vector<uint8_t> bytes = texture_payload(width, height, format, type, pixels);
        int upload_width = width;
        int upload_height = height;
        std::vector<uint8_t> upload_bytes;
        TextureInfo* existing_info = nullptr;
        const uint32_t bound_texture = active_bound_texture();
        if (target == kGlTexture2D && bound_texture != 0) {
            existing_info = &state.textures[bound_texture];
        }
        if (xoffset == 0 && yoffset == 0 && !bytes.empty()) {
            uint32_t backing_width = existing_info && existing_info->width > 0 ? static_cast<uint32_t>(existing_info->width) : 0;
            uint32_t backing_height = existing_info && existing_info->height > 0 ? static_cast<uint32_t>(existing_info->height) : 0;
            if (backing_width < resolved_width || backing_width > 4096) {
                backing_width = next_power_of_two(resolved_width);
            }
            if (backing_height < resolved_height || backing_height > 4096) {
                backing_height = next_power_of_two(resolved_height);
            }
            const int bytes_per_pixel = bytes_per_pixel_for(format, type);
            if (backing_width != resolved_width || backing_height != resolved_height) {
                const uint64_t backing_size64 = static_cast<uint64_t>(backing_width) * static_cast<uint64_t>(backing_height) * static_cast<uint64_t>(bytes_per_pixel);
                if (bytes_per_pixel > 0 && backing_size64 <= 16ull * 1024ull * 1024ull) {
                    upload_bytes.assign(static_cast<size_t>(backing_size64), 0);
                    const size_t src_pitch = static_cast<size_t>(resolved_width) * static_cast<size_t>(bytes_per_pixel);
                    const size_t dst_pitch = static_cast<size_t>(backing_width) * static_cast<size_t>(bytes_per_pixel);
                    for (uint32_t y = 0; y < resolved_height; ++y) {
                        std::copy(bytes.data() + static_cast<size_t>(y) * src_pitch,
                                  bytes.data() + static_cast<size_t>(y + 1) * src_pitch,
                                  upload_bytes.data() + static_cast<size_t>(y) * dst_pitch);
                    }
                    upload_width = static_cast<int>(backing_width);
                    upload_height = static_cast<int>(backing_height);
                }
            }
        }
        const bool upload_dims_ok = width > 0 && height > 0 &&
            (static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= 4096ull * 4096ull);
        if (upload_dims_ok && xoffset == 0 && yoffset == 0) {
            if (auto* presenter = shell.get_presenter()) {
                const std::vector<uint8_t>& data = upload_bytes.empty() ? bytes : upload_bytes;
                presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
                presenter->guest_gl_tex_image_2d(target, static_cast<int>(level), 0, upload_width, upload_height, 0, format, type, data.empty() ? nullptr : data.data());
            }
            if (target == kGlTexture2D && bound_texture != 0 && level == 0) {
                TextureInfo& info = state.textures[bound_texture];
                info.width = upload_width;
                info.height = upload_height;
                info.format = format;
                info.type = type;
            }
        }
        if (trace_gles_textures()) {
            printf("  %s target=0x%x offset=%d,%d size=%dx%d format=0x%x type=0x%x pixels=0x%08x\n",
                   label, target, xoffset, yoffset, width, height, format, type, pixels);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glCompressedTexImage2D" || name == "IGLES_CompressedTexImage2D" || name == "GLProc_glCompressedTexImage2D") {
        const uint32_t target = gl_arg(0);
        const uint32_t level = gl_arg(1);
        const uint32_t internal_format = gl_arg(2);
        const uint32_t width_u = gl_arg(3);
        const uint32_t height_u = gl_arg(4);
        const uint32_t border = gl_arg(5);
        const uint32_t image_size = gl_arg(6);
        const addr_t data_ptr = gl_arg(7);

        auto expand5 = [](uint16_t value) -> uint8_t {
            value &= 0x1f;
            return static_cast<uint8_t>((value << 3) | (value >> 2));
        };
        auto expand6 = [](uint16_t value) -> uint8_t {
            value &= 0x3f;
            return static_cast<uint8_t>((value << 2) | (value >> 4));
        };
        auto expand4 = [](uint16_t value) -> uint8_t {
            value &= 0x0f;
            return static_cast<uint8_t>((value << 4) | value);
        };
        auto read_le16_at = [&](addr_t ptr) -> uint16_t {
            return static_cast<uint16_t>(memory.read_value(ptr, EndianMemory::Byte)) |
                   static_cast<uint16_t>(memory.read_value(ptr + 1, EndianMemory::Byte) << 8);
        };
        auto decode_oes_palette = [&](int width, int height, std::vector<uint8_t>& rgba) -> bool {
            if (width <= 0 || height <= 0 || image_size == 0 || image_size > 16u * 1024u * 1024u || !plausible_pointer(data_ptr)) {
                return false;
            }
            enum PaletteEncoding {
                PaletteRgb8,
                PaletteRgba8,
                PaletteRgb565,
                PaletteRgba4,
                PaletteRgb5A1,
            };
            int index_bits = 0;
            uint32_t palette_entries = 0;
            uint32_t palette_entry_bytes = 0;
            PaletteEncoding encoding = PaletteRgb8;
            switch (internal_format) {
                case 0x8B90: index_bits = 4; palette_entries = 16;  palette_entry_bytes = 3; encoding = PaletteRgb8;   break; // GL_PALETTE4_RGB8_OES
                case 0x8B91: index_bits = 4; palette_entries = 16;  palette_entry_bytes = 4; encoding = PaletteRgba8;  break; // GL_PALETTE4_RGBA8_OES
                case 0x8B92: index_bits = 4; palette_entries = 16;  palette_entry_bytes = 2; encoding = PaletteRgb565; break; // GL_PALETTE4_R5_G6_B5_OES
                case 0x8B93: index_bits = 4; palette_entries = 16;  palette_entry_bytes = 2; encoding = PaletteRgba4;  break; // GL_PALETTE4_RGBA4_OES
                case 0x8B94: index_bits = 4; palette_entries = 16;  palette_entry_bytes = 2; encoding = PaletteRgb5A1; break; // GL_PALETTE4_RGB5_A1_OES
                case 0x8B95: index_bits = 8; palette_entries = 256; palette_entry_bytes = 3; encoding = PaletteRgb8;   break; // GL_PALETTE8_RGB8_OES
                case 0x8B96: index_bits = 8; palette_entries = 256; palette_entry_bytes = 4; encoding = PaletteRgba8;  break; // GL_PALETTE8_RGBA8_OES
                case 0x8B97: index_bits = 8; palette_entries = 256; palette_entry_bytes = 2; encoding = PaletteRgb565; break; // GL_PALETTE8_R5_G6_B5_OES
                case 0x8B98: index_bits = 8; palette_entries = 256; palette_entry_bytes = 2; encoding = PaletteRgba4;  break; // GL_PALETTE8_RGBA4_OES
                case 0x8B99: index_bits = 8; palette_entries = 256; palette_entry_bytes = 2; encoding = PaletteRgb5A1; break; // GL_PALETTE8_RGB5_A1_OES
                default: return false;
            }
            const uint64_t pixel_count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
            const uint64_t palette_bytes = static_cast<uint64_t>(palette_entries) * static_cast<uint64_t>(palette_entry_bytes);
            const uint64_t index_bytes = index_bits == 4 ? ((pixel_count + 1u) / 2u) : pixel_count;
            const uint64_t required = palette_bytes + index_bytes;
            if (pixel_count > 64ull * 1024ull * 1024ull || required > image_size) {
                return false;
            }

            std::vector<std::array<uint8_t, 4>> palette(palette_entries);
            for (uint32_t i = 0; i < palette_entries; ++i) {
                const addr_t entry = data_ptr + static_cast<addr_t>(i * palette_entry_bytes);
                switch (encoding) {
                    case PaletteRgb8:
                        palette[i] = {
                            static_cast<uint8_t>(memory.read_value(entry, EndianMemory::Byte)),
                            static_cast<uint8_t>(memory.read_value(entry + 1, EndianMemory::Byte)),
                            static_cast<uint8_t>(memory.read_value(entry + 2, EndianMemory::Byte)),
                            0xff,
                        };
                        break;
                    case PaletteRgba8:
                        palette[i] = {
                            static_cast<uint8_t>(memory.read_value(entry, EndianMemory::Byte)),
                            static_cast<uint8_t>(memory.read_value(entry + 1, EndianMemory::Byte)),
                            static_cast<uint8_t>(memory.read_value(entry + 2, EndianMemory::Byte)),
                            static_cast<uint8_t>(memory.read_value(entry + 3, EndianMemory::Byte)),
                        };
                        break;
                    case PaletteRgb565: {
                        const uint16_t v = read_le16_at(entry);
                        palette[i] = {expand5(v >> 11), expand6(v >> 5), expand5(v), 0xff};
                        break;
                    }
                    case PaletteRgba4: {
                        const uint16_t v = read_le16_at(entry);
                        palette[i] = {expand4(v >> 12), expand4(v >> 8), expand4(v >> 4), expand4(v)};
                        break;
                    }
                    case PaletteRgb5A1: {
                        const uint16_t v = read_le16_at(entry);
                        palette[i] = {
                            expand5(v >> 11),
                            expand5(v >> 6),
                            expand5(v >> 1),
                            static_cast<uint8_t>((v & 1u) ? 0xff : 0x00),
                        };
                        break;
                    }
                }
            }

            rgba.assign(static_cast<size_t>(pixel_count) * 4u, 0);
            const addr_t indices = data_ptr + static_cast<addr_t>(palette_bytes);
            for (uint64_t pixel = 0; pixel < pixel_count; ++pixel) {
                uint8_t index = 0;
                if (index_bits == 4) {
                    // GL_OES_compressed_paletted_texture packs two 4-bit indices
                    // per byte with the FIRST (even) texel in the high-order
                    // nibble and the second (odd) texel in the low-order nibble.
                    // The previous order was swapped, which transposed adjacent
                    // texel pairs and scrambled every PALETTE4 texture (e.g. the
                    // Double Dragon title font atlases) into fragmented glyphs,
                    // while PALETTE8 art was unaffected.
                    const uint8_t packed = static_cast<uint8_t>(memory.read_value(indices + static_cast<addr_t>(pixel / 2u), EndianMemory::Byte));
                    index = (pixel & 1u) ? static_cast<uint8_t>(packed & 0x0f) : static_cast<uint8_t>(packed >> 4);
                } else {
                    index = static_cast<uint8_t>(memory.read_value(indices + static_cast<addr_t>(pixel), EndianMemory::Byte));
                }
                const auto& color = palette[index];
                const size_t out = static_cast<size_t>(pixel) * 4u;
                rgba[out + 0] = color[0];
                rgba[out + 1] = color[1];
                rgba[out + 2] = color[2];
                rgba[out + 3] = color[3];
            }
            return true;
        };
        auto decode_rgb565 = [&](uint16_t v) -> std::array<uint8_t, 4> {
            return {expand5(v >> 11), expand6(v >> 5), expand5(v), 0xff};
        };
        auto decode_rgb555 = [&](uint16_t v) -> std::array<uint8_t, 4> {
            return {expand5(v >> 10), expand5(v >> 5), expand5(v), 0xff};
        };
        auto mix_colors = [](const std::array<uint8_t, 4>& a, const std::array<uint8_t, 4>& b,
                             uint32_t aw, uint32_t bw, uint32_t denom) {
            std::array<uint8_t, 4> out{};
            for (int component = 0; component < 4; ++component) {
                out[component] = static_cast<uint8_t>((aw * a[component] + bw * b[component]) / denom);
            }
            return out;
        };
        auto decode_atc = [&](int width, int height, bool explicit_alpha, std::vector<uint8_t>& rgba) -> bool {
            if (width <= 0 || height <= 0 || image_size == 0 || image_size > 16u * 1024u * 1024u || !plausible_pointer(data_ptr)) {
                return false;
            }
            const uint32_t blocks_w = (static_cast<uint32_t>(width) + 3u) / 4u;
            const uint32_t blocks_h = (static_cast<uint32_t>(height) + 3u) / 4u;
            const uint64_t block_count = static_cast<uint64_t>(blocks_w) * static_cast<uint64_t>(blocks_h);
            const uint32_t bytes_per_block = explicit_alpha ? 16u : 8u;
            if (block_count == 0 || block_count * bytes_per_block > image_size) {
                return false;
            }

            constexpr uint8_t kAtcSelectorRemap[4] = {0, 2, 3, 1};
            rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
            for (uint32_t by = 0; by < blocks_h; ++by) {
                for (uint32_t bx = 0; bx < blocks_w; ++bx) {
                    const uint64_t block_index = static_cast<uint64_t>(by) * blocks_w + bx;
                    const addr_t block = data_ptr + static_cast<addr_t>(block_index * bytes_per_block);
                    const addr_t color_block = explicit_alpha ? block + 8 : block;
                    const uint16_t endpoint0 = read_le16_at(color_block);
                    const uint16_t endpoint1 = read_le16_at(color_block + 2);
                    const auto c0 = decode_rgb555(static_cast<uint16_t>(endpoint0 & 0x7fffu));
                    const auto c1 = decode_rgb565(endpoint1);

                    std::array<std::array<uint8_t, 4>, 4> colors{};
                    colors[0] = c0;
                    colors[1] = c1;
                    if (endpoint0 & 0x8000u) {
                        colors[2] = mix_colors(c0, c1, 1, 1, 2);
                        colors[3] = {0, 0, 0, 0xff};
                    } else {
                        colors[2] = mix_colors(c0, c1, 5, 3, 8);
                        colors[3] = mix_colors(c0, c1, 3, 5, 8);
                    }

                    uint32_t indices = 0;
                    for (uint32_t byte = 0; byte < 4; ++byte) {
                        indices |= memory.read_value(color_block + 4 + byte, EndianMemory::Byte) << (byte * 8);
                    }

                    for (uint32_t py = 0; py < 4; ++py) {
                        for (uint32_t px = 0; px < 4; ++px) {
                            const uint32_t x = bx * 4u + px;
                            const uint32_t y = by * 4u + py;
                            if (x >= static_cast<uint32_t>(width) || y >= static_cast<uint32_t>(height)) {
                                continue;
                            }
                            const uint32_t pixel = py * 4u + px;
                            const uint32_t selector = (indices >> (pixel * 2u)) & 0x3u;
                            const auto& color = colors[kAtcSelectorRemap[selector]];
                            const size_t out = (static_cast<size_t>(y) * static_cast<size_t>(width) + x) * 4u;
                            rgba[out + 0] = color[0];
                            rgba[out + 1] = color[1];
                            rgba[out + 2] = color[2];
                            rgba[out + 3] = color[3];
                            if (explicit_alpha) {
                                const uint8_t packed = static_cast<uint8_t>(memory.read_value(block + pixel / 2u, EndianMemory::Byte));
                                const uint8_t alpha4 = (pixel & 1u) ? static_cast<uint8_t>(packed >> 4) : static_cast<uint8_t>(packed & 0x0f);
                                rgba[out + 3] = static_cast<uint8_t>((alpha4 << 4) | alpha4);
                            }
                        }
                    }
                }
            }
            return true;
        };

        std::vector<uint8_t> rgba;
        bool decoded = false;
        if (internal_format >= 0x8B90 && internal_format <= 0x8B99) {
            decoded = decode_oes_palette(static_cast<int>(width_u), static_cast<int>(height_u), rgba);
        } else if (internal_format == 0x8C92) { // GL_ATC_RGB_AMD / Qualcomm ATITC RGB
            decoded = decode_atc(static_cast<int>(width_u), static_cast<int>(height_u), false, rgba);
        } else if (internal_format == 0x8C93) { // GL_ATC_RGBA_EXPLICIT_ALPHA_AMD
            decoded = decode_atc(static_cast<int>(width_u), static_cast<int>(height_u), true, rgba);
        }

        const uint32_t bound_texture = active_bound_texture();
        if (decoded && target == kGlTexture2D && bound_texture != 0) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
                presenter->guest_gl_tex_image_2d(target, static_cast<int>(level), static_cast<int>(internal_format),
                                                 static_cast<int>(width_u), static_cast<int>(height_u), static_cast<int>(border),
                                                 kGlRgba, kGlUnsignedByte, rgba.data());
            }
            if (level == 0) {
                TextureInfo& info = state.textures[bound_texture];
                info.width = static_cast<int>(width_u);
                info.height = static_cast<int>(height_u);
                info.format = kGlRgba;
                info.type = kGlUnsignedByte;
            }
        }

        if (trace_gles_vertices() || trace_gles_textures()) {
            printf("  %s compressed target=0x%x tex=%u level=%u size=%ux%u format=0x%x bytes=%u data=0x%08x decoded=%d\n",
                   label, target, bound_texture, level, width_u, height_u, internal_format, image_size, data_ptr, decoded ? 1 : 0);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glTexParameterx" || name == "IGL_glTexParameteri" || name == "IGL_glTexParameterf" ||
        name == "GLProc_glTexParameterx" || name == "GLProc_glTexParameteri" || name == "GLProc_glTexParameterf" ||
        name == "IGLES_TexParameterx" || name == "IGLES_TexParameteri" || name == "IGLES_TexParameterf" ||
        name == "IGLES_TexParameteriv" || name == "IGLES_TexParameterfv" || name == "IGLES_TexParameterxv") {
        const uint32_t target = gl_arg(0);
        const uint32_t pname = gl_arg(1);
        const bool vector_value = name.find("iv") != std::string::npos ||
                                  name.find("fv") != std::string::npos ||
                                  name.find("xv") != std::string::npos;
        const bool float_value = name.find("TexParameterf") != std::string::npos ||
                                 name.find("TexParameterfv") != std::string::npos;
        const addr_t value_ptr = gl_arg(2);
        uint32_t value = gl_arg(2);
        if (vector_value && value_ptr != 0 && value_ptr < 0xFF000000u) {
            value = memory.read_value(value_ptr);
        }
        if (float_value) {
            value = static_cast<uint32_t>(static_cast<int32_t>(std::lround(raw_to_float(value))));
        }
        if ((pname == kGlTextureWrapS || pname == kGlTextureWrapT) && value == 0) {
            // QXEngine titles can pass 0 for GL_TEXTURE_WRAP_{S,T}. GLES would
            // flag this as invalid, but on-device NFS splash textures clamp;
            // treating it as repeat tiles small loading-logo chunks across the
            // whole framebuffer.
            value = kGlClampToEdge;
        }
        const uint32_t bound_texture = active_bound_texture();
        if (target == kGlTexture2D && bound_texture != 0) {
            TextureInfo& tex = state.textures[bound_texture];
            switch (pname) {
                case kGlTextureMinFilter:
                    tex.min_filter = value;
                    break;
                case kGlTextureMagFilter:
                    tex.mag_filter = value;
                    break;
                case kGlTextureWrapS:
                    if (value == kGlRepeat || value == kGlClampToEdge) {
                        tex.wrap_s = value;
                    }
                    break;
                case kGlTextureWrapT:
                    if (value == kGlRepeat || value == kGlClampToEdge) {
                        tex.wrap_t = value;
                    }
                    break;
                case kGlTextureCropRectOes:
                    if (vector_value && value_ptr != 0 && value_ptr < 0xFF000000u) {
                        for (int i = 0; i < 4; ++i) {
                            const uint32_t raw = memory.read_value(value_ptr + static_cast<addr_t>(i * 4));
                            int32_t decoded = static_cast<int32_t>(raw);
                            if (name.find("fv") != std::string::npos) {
                                decoded = static_cast<int32_t>(std::lround(raw_to_float(raw)));
                            } else if (name.find("xv") != std::string::npos) {
                                decoded = static_cast<int32_t>(std::lround(fixed_to_float(raw)));
                            }
                            tex.crop_rect[static_cast<size_t>(i)] = decoded;
                        }
                        tex.has_crop_rect = true;
                    }
                    break;
                default:
                    if (trace_qxgl_calls()) {
                        printf("  %s tex parameter target=0x%x pname=0x%x value=0x%x stored as unknown\n", label, target, pname, value);
                    }
                    break;
            }
            if (trace_gles_textures()) {
                printf("  %s %s tex parameter tex=%u target=0x%x pname=0x%x value=0x%x ptr=0x%08x\n",
                       label, name.c_str(), bound_texture, target, pname, value, vector_value ? value_ptr : 0u);
            }
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
                if (pname == kGlTextureCropRectOes) {
                    // DrawTex*OES consumes this from QXGL state; it is not a
                    // normal sampler parameter in the SDL presenter.
                } else if (pname != kGlTextureWrapS && pname != kGlTextureWrapT) {
                    presenter->guest_gl_tex_parameter(target, pname, value);
                } else if (value == kGlRepeat || value == kGlClampToEdge) {
                    presenter->guest_gl_tex_parameter(target, pname, value);
                }
            }
        }
        set_success();
        return true;
    }
    auto tex_env_float_arg = [&](uint32_t raw) -> float {
        if (name.find("Envf") != std::string::npos) {
            return raw_to_float(raw);
        }
        if (name.find("Envx") != std::string::npos) {
            return fixed_to_float(raw);
        }
        return static_cast<float>(static_cast<int32_t>(raw));
    };
    auto tex_env_scale_arg = [&](uint32_t raw) -> float {
        if (raw == 1 || raw == 2 || raw == 4) {
            return static_cast<float>(raw);
        }
        return tex_env_float_arg(raw);
    };
    auto set_tex_env_param = [&](uint32_t pname, uint32_t value) -> bool {
        TextureEnvInfo& env = current_texture_env();
        switch (pname) {
            case kGlTextureEnvMode:
                env.mode = value;
                break;
            case kGlCombineRgb:
                env.combine_rgb = value;
                break;
            case kGlCombineAlpha:
                env.combine_alpha = value;
                break;
            case kGlSrc0Rgb:
            case kGlSrc1Rgb:
            case kGlSrc2Rgb:
                env.src_rgb[(pname - kGlSrc0Rgb) & 0x3u] = value;
                break;
            case kGlSrc0Alpha:
            case kGlSrc1Alpha:
            case kGlSrc2Alpha:
                env.src_alpha[(pname - kGlSrc0Alpha) & 0x3u] = value;
                break;
            case kGlOperand0Rgb:
            case kGlOperand1Rgb:
            case kGlOperand2Rgb:
                env.operand_rgb[(pname - kGlOperand0Rgb) & 0x3u] = value;
                break;
            case kGlOperand0Alpha:
            case kGlOperand1Alpha:
            case kGlOperand2Alpha:
                env.operand_alpha[(pname - kGlOperand0Alpha) & 0x3u] = value;
                break;
            case kGlRgbScale:
                env.rgb_scale = tex_env_scale_arg(value);
                break;
            case kGlAlphaScale:
                env.alpha_scale = tex_env_scale_arg(value);
                break;
            default:
                return false;
        }
        sync_texture_env_aliases();
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
            presenter->guest_gl_tex_env(pname, value);
        }
        return true;
    };
    auto update_tex_env_color = [&](addr_t values) {
        TextureEnvInfo& env = current_texture_env();
        for (int i = 0; i < 4; ++i) {
            const uint32_t raw = (values && values < 0xFF000000) ? memory.read_value(values + static_cast<addr_t>(i * 4)) : 0;
            env.color[i] = tex_env_float_arg(raw);
        }
        sync_texture_env_aliases();
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
            presenter->guest_gl_tex_env_color(env.color[0], env.color[1], env.color[2], env.color[3]);
        }
    };
    if (name == "IGL_glTexEnvx" || name == "IGL_glTexEnvi" || name == "IGL_glTexEnvf" ||
        name == "IGLES_TexEnvx" || name == "IGLES_TexEnvi" || name == "IGLES_TexEnvf" ||
        name == "GLProc_glTexEnvx" || name == "GLProc_glTexEnvi" || name == "GLProc_glTexEnvf") {
        if (r0 == kGlTextureEnv && r1 == kGlTextureEnvColor) {
            TextureEnvInfo& env = current_texture_env();
            env.color[0] = tex_env_float_arg(r2);
            sync_texture_env_aliases();
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_active_texture_unit(std::min<uint32_t>(state.active_texture_unit, 1u));
                presenter->guest_gl_tex_env_color(env.color[0], env.color[1], env.color[2], env.color[3]);
            }
        } else if (r0 != kGlTextureEnv || !set_tex_env_param(r1, r2)) {
            if (trace_qxgl_calls()) {
                printf("  %s tex env target=0x%x pname=0x%x value=0x%x stored as unknown\n", label, r0, r1, r2);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glTexEnviv" || name == "IGL_glTexEnvfv" || name == "IGL_glTexEnvxv" ||
        name == "IGLES_TexEnviv" || name == "IGLES_TexEnvfv" || name == "IGLES_TexEnvxv" ||
        name == "GLProc_glTexEnviv" || name == "GLProc_glTexEnvfv" || name == "GLProc_glTexEnvxv") {
        const uint32_t value = (r2 && r2 < 0xFF000000) ? memory.read_value(r2) : r2;
        if (r0 == kGlTextureEnv && r1 == kGlTextureEnvColor) {
            update_tex_env_color(r2);
        } else if (r0 != kGlTextureEnv || !set_tex_env_param(r1, value)) {
            if (trace_qxgl_calls()) {
                printf("  %s tex env target=0x%x pname=0x%x value=0x%x stored as unknown\n", label, r0, r1, r2);
            }
        }
        set_success();
        return true;
    }
    auto decode_numeric_arg = [&](uint32_t raw, bool fixed_arg) {
        return fixed_arg ? fixed_to_float(raw) : raw_to_float(raw);
    };
    auto read_numeric_vector = [&](addr_t ptr, bool fixed_arg, int count, float* out) {
        for (int i = 0; i < count; ++i) {
            const uint32_t raw = (ptr && ptr < 0xFF000000u) ? memory.read_value(ptr + static_cast<addr_t>(i * 4)) : 0;
            out[i] = decode_numeric_arg(raw, fixed_arg);
        }
    };
    auto store_material_vec4 = [](MaterialInfo& material, uint32_t pname, const float* values) {
        float* target = nullptr;
        switch (pname) {
            case kGlAmbient: target = material.ambient; break;
            case kGlDiffuse: target = material.diffuse; break;
            case kGlSpecular: target = material.specular; break;
            case kGlEmission: target = material.emission; break;
            case kGlAmbientAndDiffuse:
                for (int i = 0; i < 4; ++i) {
                    material.ambient[i] = values[i];
                    material.diffuse[i] = values[i];
                }
                return;
            default: break;
        }
        if (target) {
            for (int i = 0; i < 4; ++i) {
                target[i] = values[i];
            }
        }
    };
    auto apply_material = [&](uint32_t face, uint32_t pname, const float* values, bool scalar) {
        auto apply_one = [&](MaterialInfo& material) {
            if (pname == kGlShininess) {
                material.shininess = values[0];
            } else if (!scalar) {
                store_material_vec4(material, pname, values);
            }
        };
        if (face == kGlFront || face == kGlFrontAndBack) {
            apply_one(state.front_material);
        }
        if (face == kGlBack || face == kGlFrontAndBack) {
            apply_one(state.back_material);
        }
    };
    auto store_light_vec4 = [](LightInfo& light, uint32_t pname, const float* values) {
        float* target = nullptr;
        switch (pname) {
            case kGlAmbient: target = light.ambient; break;
            case kGlDiffuse: target = light.diffuse; break;
            case kGlSpecular: target = light.specular; break;
            case kGlPosition: target = light.position; break;
            default: break;
        }
        if (target) {
            for (int i = 0; i < 4; ++i) {
                target[i] = values[i];
            }
        } else if (pname == kGlSpotDirection) {
            for (int i = 0; i < 3; ++i) {
                light.spot_direction[i] = values[i];
            }
        }
    };
    auto store_light_scalar = [](LightInfo& light, uint32_t pname, float value) {
        switch (pname) {
            case kGlSpotExponent: light.spot_exponent = value; break;
            case kGlSpotCutoff: light.spot_cutoff = value; break;
            case kGlConstantAttenuation: light.constant_attenuation = value; break;
            case kGlLinearAttenuation: light.linear_attenuation = value; break;
            case kGlQuadraticAttenuation: light.quadratic_attenuation = value; break;
            default: break;
        }
    };
    const bool fixed_numeric_call = name.find('x') != std::string::npos;
    if (name == "IGL_glFogx" || name == "IGL_glFogf" ||
        name == "IGLES_Fogx" || name == "IGLES_Fogf" ||
        name == "GLProc_glFogx" || name == "GLProc_glFogf") {
        const uint32_t pname = gl_arg(0);
        const uint32_t raw = gl_arg(1);
        const float value = decode_numeric_arg(raw, fixed_numeric_call);
        if (pname == kGlFogMode) {
            state.fog_mode = raw;
        } else if (pname == kGlFogDensity) {
            state.fog_density = value;
        } else if (pname == kGlFogStart) {
            state.fog_start = value;
        } else if (pname == kGlFogEnd) {
            state.fog_end = value;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glFogxv" || name == "IGL_glFogfv" ||
        name == "IGLES_Fogxv" || name == "IGLES_Fogfv" ||
        name == "GLProc_glFogxv" || name == "GLProc_glFogfv") {
        const uint32_t pname = gl_arg(0);
        const addr_t values = gl_arg(1);
        if (pname == kGlFogColor) {
            read_numeric_vector(values, fixed_numeric_call, 4, state.fog_color);
        } else if (pname == kGlFogMode && values && values < 0xFF000000u) {
            state.fog_mode = memory.read_value(values);
        } else {
            float scalar[1] = {0.0f};
            read_numeric_vector(values, fixed_numeric_call, 1, scalar);
            if (pname == kGlFogDensity) state.fog_density = scalar[0];
            if (pname == kGlFogStart) state.fog_start = scalar[0];
            if (pname == kGlFogEnd) state.fog_end = scalar[0];
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLightModelx" || name == "IGL_glLightModelf" ||
        name == "IGLES_LightModelx" || name == "IGLES_LightModelf" ||
        name == "GLProc_glLightModelx" || name == "GLProc_glLightModelf") {
        const uint32_t pname = gl_arg(0);
        const uint32_t raw = gl_arg(1);
        if (pname == kGlLightModelTwoSide) {
            state.light_model_two_side = raw != 0;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLightModelxv" || name == "IGL_glLightModelfv" ||
        name == "IGLES_LightModelxv" || name == "IGLES_LightModelfv" ||
        name == "GLProc_glLightModelxv" || name == "GLProc_glLightModelfv") {
        const uint32_t pname = gl_arg(0);
        const addr_t values = gl_arg(1);
        if (pname == kGlLightModelAmbient) {
            read_numeric_vector(values, fixed_numeric_call, 4, state.light_model_ambient);
        } else if (pname == kGlLightModelTwoSide && values && values < 0xFF000000u) {
            state.light_model_two_side = memory.read_value(values) != 0;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLightx" || name == "IGL_glLightf" ||
        name == "IGLES_Lightx" || name == "IGLES_Lightf" ||
        name == "GLProc_glLightx" || name == "GLProc_glLightf") {
        const int index = light_index(gl_arg(0));
        if (index >= 0) {
            const uint32_t pname = gl_arg(1);
            store_light_scalar(state.lights[static_cast<size_t>(index)], pname,
                               decode_numeric_arg(gl_arg(2), fixed_numeric_call));
        }
        set_success();
        return true;
    }
    if (name == "IGL_glLightxv" || name == "IGL_glLightfv" ||
        name == "IGLES_Lightxv" || name == "IGLES_Lightfv" ||
        name == "GLProc_glLightxv" || name == "GLProc_glLightfv") {
        const int index = light_index(gl_arg(0));
        if (index >= 0) {
            const uint32_t pname = gl_arg(1);
            const addr_t values_ptr = gl_arg(2);
            float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            read_numeric_vector(values_ptr, fixed_numeric_call, 4, values);
            if (pname == kGlSpotExponent || pname == kGlSpotCutoff ||
                pname == kGlConstantAttenuation || pname == kGlLinearAttenuation ||
                pname == kGlQuadraticAttenuation) {
                store_light_scalar(state.lights[static_cast<size_t>(index)], pname, values[0]);
            } else {
                store_light_vec4(state.lights[static_cast<size_t>(index)], pname, values);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glMaterialx" || name == "IGL_glMaterialf" ||
        name == "IGLES_Materialx" || name == "IGLES_Materialf" ||
        name == "GLProc_glMaterialx" || name == "GLProc_glMaterialf") {
        const uint32_t face = gl_arg(0);
        const uint32_t pname = gl_arg(1);
        const float value[4] = {decode_numeric_arg(gl_arg(2), fixed_numeric_call), 0.0f, 0.0f, 0.0f};
        apply_material(face, pname, value, true);
        set_success();
        return true;
    }
    if (name == "IGL_glMaterialxv" || name == "IGL_glMaterialfv" ||
        name == "IGLES_Materialxv" || name == "IGLES_Materialfv" ||
        name == "GLProc_glMaterialxv" || name == "GLProc_glMaterialfv") {
        const uint32_t face = gl_arg(0);
        const uint32_t pname = gl_arg(1);
        const addr_t values_ptr = gl_arg(2);
        float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        read_numeric_vector(values_ptr, fixed_numeric_call, 4, values);
        apply_material(face, pname, values, false);
        set_success();
        return true;
    }
    if (name == "IGLES_ClipPlanef" || name == "IGLES_ClipPlanex" || name == "IGLES_GetClipPlanef" || name == "IGLES_GetClipPlanex") {
        set_success();
        return true;
    }
    if (name == "IGL_glEnable" || name == "IGLES_Enable" || name == "GLProc_glEnable") {
        if (r0 == kGlTexture2D) {
            const uint32_t unit = std::min<uint32_t>(state.active_texture_unit, 1u);
            state.texture_2d_enabled_units[unit] = true;
            sync_unit0_aliases();
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_texture_2d_enabled(unit, true);
            }
        }
        if (r0 == kGlDither) state.dither_enabled = true;
        if (r0 == kGlDepthTest) state.depth_test_enabled = true;
        if (r0 == kGlCullFace) state.cull_face_enabled = true;
        if (r0 == kGlAlphaTest) state.alpha_test_enabled = true;
        if (r0 == kGlBlend) state.blend_enabled = true;
        if (r0 == kGlStencilTest) state.stencil_test_enabled = true;
        if (r0 == kGlFog) {
            state.fog_enabled = true;
            printf("  [%s] not implemented yet: GL_FOG raster effect enabled\n", label);
        }
        if (r0 == kGlLighting) {
            state.lighting_enabled = true;
            printf("  [%s] not implemented yet: GL_LIGHTING raster effect enabled\n", label);
        }
        const int enabled_light = light_index(r0);
        if (enabled_light >= 0) {
            state.light_enabled[static_cast<size_t>(enabled_light)] = true;
            printf("  [%s] not implemented yet: GL_LIGHT%u raster effect enabled\n", label, static_cast<unsigned>(enabled_light));
        }
        if (r0 == kGlAlphaTest) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_alpha_test(state.alpha_test_enabled, state.alpha_func, state.alpha_ref);
            }
        }
        if (r0 == kGlDepthTest) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_depth_state(state.depth_test_enabled, state.depth_func, state.depth_mask);
            }
        }
        if (r0 == kGlBlend) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_blend(state.blend_enabled, state.blend_src, state.blend_dst);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDisable" || name == "IGLES_Disable" || name == "GLProc_glDisable") {
        if (r0 == kGlTexture2D) {
            const uint32_t unit = std::min<uint32_t>(state.active_texture_unit, 1u);
            state.texture_2d_enabled_units[unit] = false;
            sync_unit0_aliases();
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_texture_2d_enabled(unit, false);
            }
        }
        if (r0 == kGlDither) state.dither_enabled = false;
        if (r0 == kGlDepthTest) state.depth_test_enabled = false;
        if (r0 == kGlCullFace) state.cull_face_enabled = false;
        if (r0 == kGlAlphaTest) state.alpha_test_enabled = false;
        if (r0 == kGlBlend) state.blend_enabled = false;
        if (r0 == kGlStencilTest) state.stencil_test_enabled = false;
        if (r0 == kGlFog) state.fog_enabled = false;
        if (r0 == kGlLighting) state.lighting_enabled = false;
        const int disabled_light = light_index(r0);
        if (disabled_light >= 0) {
            state.light_enabled[static_cast<size_t>(disabled_light)] = false;
        }
        if (r0 == kGlAlphaTest) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_alpha_test(state.alpha_test_enabled, state.alpha_func, state.alpha_ref);
            }
        }
        if (r0 == kGlDepthTest) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_depth_state(state.depth_test_enabled, state.depth_func, state.depth_mask);
            }
        }
        if (r0 == kGlBlend) {
            if (auto* presenter = shell.get_presenter()) {
                presenter->guest_gl_blend(state.blend_enabled, state.blend_src, state.blend_dst);
            }
        }
        set_success();
        return true;
    }
    if (name == "IGL_glBlendFunc" || name == "IGLES_BlendFunc" || name == "GLProc_glBlendFunc") {
        state.blend_src = r0;
        state.blend_dst = r1;
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_blend(state.blend_enabled, state.blend_src, state.blend_dst);
        }
        if (trace_gles_vertices()) {
            printf("  %s blend_func src=0x%x dst=0x%x enabled=%u\n", label, r0, r1, state.blend_enabled ? 1u : 0u);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDepthFunc" || name == "IGLES_DepthFunc" || name == "GLProc_glDepthFunc") {
        state.depth_func = r0;
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_depth_state(state.depth_test_enabled, state.depth_func, state.depth_mask);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDepthRangef" || name == "IGLES_DepthRangef" || name == "GLProc_glDepthRangef" ||
        name == "IGL_glDepthRangex" || name == "IGLES_DepthRangex" || name == "GLProc_glDepthRangex") {
        const bool fixed_args = name.find("Rangex") != std::string::npos;
        state.depth_range_near = std::clamp(fixed_args ? fixed_to_float(r0) : raw_to_float(r0), 0.0f, 1.0f);
        state.depth_range_far = std::clamp(fixed_args ? fixed_to_float(r1) : raw_to_float(r1), 0.0f, 1.0f);
        if (trace_gles_vertices()) {
            printf("  %s depth_range near=%f far=%f\n", label, state.depth_range_near, state.depth_range_far);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDepthMask" || name == "IGLES_DepthMask" || name == "GLProc_glDepthMask") {
        state.depth_mask = r0 != 0;
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_depth_state(state.depth_test_enabled, state.depth_func, state.depth_mask);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glHint" || name == "IGLES_Hint" || name == "GLProc_glHint") {
        if (r0 == kGlPerspectiveCorrectionHint || r0 == kGlPointSmoothHint ||
            r0 == kGlLineSmoothHint || r0 == kGlFogHint || r0 == kGlGenerateMipmapHint) {
            state.hints[r0] = r1;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glShadeModel" || name == "IGLES_ShadeModel" || name == "GLProc_glShadeModel") {
        if (r0 == kGlFlat || r0 == kGlSmooth) {
            state.shade_model = r0;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glCullFace" || name == "IGLES_CullFace" || name == "GLProc_glCullFace") {
        state.cull_face = r0;
        set_success();
        return true;
    }
    if (name == "IGL_glFrontFace" || name == "IGLES_FrontFace" || name == "GLProc_glFrontFace") {
        state.front_face = r0;
        set_success();
        return true;
    }
    if (name == "IGL_glAlphaFunc" || name == "IGLES_AlphaFunc" || name == "GLProc_glAlphaFunc" ||
        name == "IGL_glAlphaFuncx" || name == "IGLES_AlphaFuncx" || name == "GLProc_glAlphaFuncx") {
        const bool fixed_args = name.find("Funcx") != std::string::npos;
        state.alpha_func = r0;
        state.alpha_ref = std::clamp(fixed_args ? fixed_to_float(r1) : raw_to_float(r1), 0.0f, 1.0f);
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_alpha_test(state.alpha_test_enabled, state.alpha_func, state.alpha_ref);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glEnableClientState" || name == "IGLES_EnableClientState" || name == "GLProc_glEnableClientState") {
        if (r0 == kGlVertexArray) state.vertex_array.enabled = true;
        if (r0 == kGlColorArray) state.color_array.enabled = true;
        if (r0 == kGlTextureCoordArray) {
            current_texcoord_array().enabled = true;
            sync_unit0_aliases();
        }
        if (r0 == kGlNormalArray) state.normal_array.enabled = true;
        if (trace_gles_vertices()) {
            printf("  %s enable state 0x%x client_unit=%u active_unit=%u\n", label, r0, state.client_active_texture_unit, state.active_texture_unit);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glDisableClientState" || name == "IGLES_DisableClientState" || name == "GLProc_glDisableClientState") {
        if (r0 == kGlVertexArray) state.vertex_array.enabled = false;
        if (r0 == kGlColorArray) state.color_array.enabled = false;
        if (r0 == kGlTextureCoordArray) {
            current_texcoord_array().enabled = false;
            sync_unit0_aliases();
        }
        if (r0 == kGlNormalArray) state.normal_array.enabled = false;
        if (trace_gles_vertices()) {
            printf("  %s disable state 0x%x client_unit=%u active_unit=%u\n", label, r0, state.client_active_texture_unit, state.active_texture_unit);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glVertexPointer" || name == "IGLES_VertexPointer" || name == "GLProc_glVertexPointer") {
        state.vertex_array.size = static_cast<int>(r0);
        state.vertex_array.type = normalize_array_type(r1);
        state.vertex_array.stride = static_cast<int>(r2);
        state.vertex_array.ptr = r3;
        state.vertex_array.buffer = state.bound_array_buffer;
        if (trace_gles_vertices()) {
            static int vertex_pointer_logs = 0;
            if (vertex_pointer_logs < 8) {
                printf("  %s vertex ptr=0x%x buffer=%u size=%d type=0x%x stride=%d\n", label, r3, state.vertex_array.buffer, r0, state.vertex_array.type, r2);
            } else if (vertex_pointer_logs == 8) {
                printf("  %s suppressing repeated vertex ptr logs\n", label);
            }
            ++vertex_pointer_logs;
        }
        set_success();
        return true;
    }
    if (name == "IGL_glColorPointer" || name == "IGLES_ColorPointer" || name == "GLProc_glColorPointer") {
        state.color_array.size = static_cast<int>(r0);
        state.color_array.type = normalize_array_type(r1);
        state.color_array.stride = static_cast<int>(r2);
        state.color_array.ptr = r3;
        state.color_array.buffer = state.bound_array_buffer;
        if (trace_gles_vertices()) {
            printf("  %s color ptr=0x%x buffer=%u size=%d type=0x%x stride=%d\n", label, r3, state.color_array.buffer, r0, state.color_array.type, r2);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glTexCoordPointer" || name == "IGLES_TexCoordPointer" || name == "GLProc_glTexCoordPointer") {
        VertexArray& texcoord_array = current_texcoord_array();
        texcoord_array.size = static_cast<int>(r0);
        texcoord_array.type = normalize_array_type(r1);
        texcoord_array.stride = static_cast<int>(r2);
        texcoord_array.ptr = r3;
        texcoord_array.buffer = state.bound_array_buffer;
        sync_unit0_aliases();
        if (trace_gles_vertices()) {
            printf("  %s texcoord ptr=0x%x buffer=%u unit=%u size=%d type=0x%x stride=%d\n",
                   label, r3, texcoord_array.buffer, state.client_active_texture_unit, r0, texcoord_array.type, r2);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glNormalPointer" || name == "IGLES_NormalPointer" || name == "GLProc_glNormalPointer") {
        state.normal_array.type = normalize_array_type(r0);
        state.normal_array.stride = static_cast<int>(r1);
        state.normal_array.ptr = r2;
        state.normal_array.buffer = state.bound_array_buffer;
        if (trace_gles_vertices()) {
            printf("  %s normal ptr=0x%x buffer=%u type=0x%x stride=%d\n", label, r2, state.normal_array.buffer, state.normal_array.type, r1);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glClearColorx" || name == "IGL_glClearColor" || name == "IGLES_ClearColorx" || name == "IGLES_ClearColor" || name == "GLProc_glClearColorx" || name == "GLProc_glClearColor") {
        if (name.find("Colorx") != std::string::npos) {
            state.clear_color[0] = fixed_to_float(r0);
            state.clear_color[1] = fixed_to_float(r1);
            state.clear_color[2] = fixed_to_float(r2);
            state.clear_color[3] = fixed_to_float(r3);
        } else {
            state.clear_color[0] = raw_to_float(r0);
            state.clear_color[1] = raw_to_float(r1);
            state.clear_color[2] = raw_to_float(r2);
            state.clear_color[3] = raw_to_float(r3);
        }
        if (trace_qxgl_calls()) {
            printf("  %s rgba=(0x%x,0x%x,0x%x,0x%x)\n", label, r0, r1, r2, r3);
        }
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_clear_color(state.clear_color[0],
                                            state.clear_color[1],
                                            state.clear_color[2],
                                            state.clear_color[3]);
        }
        set_success();
        return true;
    }
    if (name == "IGL_glClearDepthx" || name == "IGLES_ClearDepthx" || name == "GLProc_glClearDepthx" ||
        name == "IGL_glClearDepthf" || name == "IGLES_ClearDepthf" || name == "GLProc_glClearDepthf") {
        state.clear_depth = std::clamp(name.find("Depthx") != std::string::npos ? fixed_to_float(r0) : raw_to_float(r0), 0.0f, 1.0f);
        set_success();
        return true;
    }
    if (name == "IGL_glClearStencil" || name == "IGLES_ClearStencil" || name == "GLProc_glClearStencil") {
        state.clear_stencil = r0;
        set_success();
        return true;
    }
    if (name == "IGL_glDrawArrays" || name == "IGLES_DrawArrays" || name == "GLProc_glDrawArrays") {
        if (trace_gles_vertices()) {
            printf("  %s draw mode=0x%x first=%u count=%u\n", label, r0, r1, r2);
        }
        const uint32_t saved_r0 = cpu.get_reg(REG_R0);
        const uint32_t saved_r1 = cpu.get_reg(REG_R1);
        const uint32_t saved_r2 = cpu.get_reg(REG_R2);
        cpu.set_reg(REG_R0, r0);
        cpu.set_reg(REG_R1, r1);
        cpu.set_reg(REG_R2, r2);
        process_draw_call(shell, memory, cpu, label);
        cpu.set_reg(REG_R0, saved_r0);
        cpu.set_reg(REG_R1, saved_r1);
        cpu.set_reg(REG_R2, saved_r2);
        set_success();
        return true;
    }
    if (name == "IGL_glDrawElements" || name == "IGLES_DrawElements" || name == "GLProc_glDrawElements") {
        if (trace_gles_vertices()) {
            printf("  %s draw elements mode=0x%x count=%u type=0x%x indices=0x%x\n", label, r0, r1, r2, r3);
        }
        process_draw_elements(shell, memory, cpu, r0, r1, r2, r3, label);
        set_success();
        return true;
    }
    if (name == "IGL_glFinish" || name == "IGL_glFlush" || name == "IGLES_Finish" || name == "IGLES_Flush" || name == "GLProc_glFinish" || name == "GLProc_glFlush") {
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_swap_buffers();
        }
        set_success();
        return true;
    }
    if (name == "IGL_glClear" || name == "IGLES_Clear" || name == "GLProc_glClear") {
        const auto clamp_255 = [](float v) -> uint8_t {
            int iv = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            return static_cast<uint8_t>(std::clamp(iv, 0, 255));
        };
        const uint8_t r = clamp_255(state.clear_color[0]);
        const uint8_t g = clamp_255(state.clear_color[1]);
        const uint8_t b = clamp_255(state.clear_color[2]);
        const uint16_t fill = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        if (trace_qxgl_calls()) {
            printf("  %s mask=0x%x fill=0x%04x\n", label, r0, fill);
        }
        if (auto* presenter = shell.get_presenter()) {
            presenter->guest_gl_clear(r0);
        }
        set_success();
        return true;
    }

    return false;
}

} // namespace qxgl
