#ifndef ZEEMU_BREW_3D_COMMON_H_
#define ZEEMU_BREW_3D_COMMON_H_

#include "cpu/core/CPU.h"
#include "cpu/memory/EndianMemory.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

// Whether per-call I3D info logging is enabled. Unknown slots always log in
// normal smoke runs because they are implementation debt, not verbose trace.
inline bool i3d_trace_enabled() {
    return std::getenv("ZEEMU_TRACE_HLE") != nullptr;
}

inline bool i3d_log_unknown(const std::string& key) {
    (void)key;
    return true;
}

inline bool i3d_guest_ptr(uint32_t ptr) {
    return ptr != 0 && ptr < 0xFF000000u;
}

inline uint32_t stack_arg(CPU& cpu, EndianMemory& memory, uint32_t index) {
    const uint32_t sp = cpu.get_reg(REG_SP);
    const uint64_t addr = static_cast<uint64_t>(sp) + static_cast<uint64_t>(index) * 4u;
    if (sp == 0 || sp >= 0xFF000000u || addr >= 0xFF000000ull) {
        return 0;
    }
    return memory.read_value(static_cast<uint32_t>(addr));
}

inline void write_bool_ret(EndianMemory& memory, uint32_t addr, uint32_t value) {
    if (addr != 0 && addr < 0xFF000000) {
        memory.write_value(addr, value);
    }
}

inline float gl_fixed_to_float(uint32_t value) {
    return std::clamp(static_cast<float>(value) / 65536.0f, 0.0f, 1.0f);
}

inline const char* gl_extensions() {
    return "EGL_QUALCOMM_surface_scale EGL_QUALCOMM_surface_rotate EGL_QUALCOMM_get_color_buffer "
           "EGL_QUALCOMM_get_power_level EGL_EXT_swap_control "
           "GL_ARB_texture_env_combine GL_ARB_texture_env_crossbar "
           "GL_ARB_texture_env_dot3 GL_ARB_texture_mirrored_repeat "
           "GL_ARB_vertex_buffer_object GL_ATI_extended_texture_coordinate_data_formats "
           "GL_ATI_imageon_misc GL_ATI_texture_compression_atitc "
           "GL_EXT_blend_equation_separate GL_EXT_blend_func_separate "
           "GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_stencil_wrap "
           "GL_OES_blend_equation_separate GL_OES_blend_func_separate "
           "GL_OES_blend_subtract GL_OES_byte_coordinates "
           "GL_OES_compressed_paletted_texture GL_OES_draw_texture "
           "GL_OES_fixed_point GL_OES_framebuffer_object GL_OES_matrix_palette "
           "GL_OES_point_size_array GL_OES_point_sprite GL_OES_query_matrix "
           "GL_OES_read_format GL_OES_single_precision GL_OES_texture_cube_map "
           "GL_OES_vertex_buffer_object "
           "GL_QUALCOMM_vertex_buffer_object ";
}

inline const char* gles_name_for_index(int index) {
    static const char* names[] = {
        "AddRef", "Release", "QueryInterface",
        "AlphaFunc", "ClearColor", "ClearDepthf", "Color4f", "DepthRangef",
        "Fogf", "Fogfv", "Frustumf", "LightModelf", "LightModelfv", "Lightf",
        "Lightfv", "LineWidth", "LoadMatrixf", "Materialf", "Materialfv",
        "MultMatrixf", "MultiTexCoord4f", "Normal3f", "Orthof", "PointSize",
        "PolygonOffset", "Rotatef", "Scalef", "TexEnvf", "TexEnvfv",
        "TexParameterf", "Translatef", "ActiveTexture", "AlphaFuncx",
        "BindTexture", "BlendFunc", "Clear", "ClearColorx", "ClearDepthx",
        "ClearStencil", "ClientActiveTexture", "Color4x", "ColorMask",
        "ColorPointer", "CompressedTexImage2D", "CompressedTexSubImage2D",
        "CopyTexImage2D", "CopyTexSubImage2D", "CullFace", "DeleteTextures",
        "DepthFunc", "DepthMask", "DepthRangex", "Disable",
        "DisableClientState", "DrawArrays", "DrawElements", "Enable",
        "EnableClientState", "Finish", "Flush", "Fogx", "Fogxv", "FrontFace",
        "Frustumx", "GenTextures", "GetError", "GetIntegerv", "GetString",
        "Hint", "LightModelx", "LightModelxv", "Lightx", "Lightxv",
        "LineWidthx", "LoadIdentity", "LoadMatrixx", "LogicOp", "Materialx",
        "Materialxv", "MatrixMode", "MultMatrixx", "MultiTexCoord4x",
        "Normal3x", "NormalPointer", "Orthox", "PixelStorei", "PointSizex",
        "PolygonOffsetx", "PopMatrix", "PushMatrix", "ReadPixels", "Rotatex",
        "SampleCoverage", "SampleCoveragex", "Scalex", "Scissor", "ShadeModel",
        "StencilFunc", "StencilMask", "StencilOp", "TexCoordPointer",
        "TexEnvx", "TexEnvxv", "TexImage2D", "TexParameterx",
        "TexSubImage2D", "Translatex", "VertexPointer", "Viewport",
        "ClipPlanef", "GetClipPlanef", "GetFloatv", "GetLightfv",
        "GetMaterialfv", "GetTexEnvfv", "GetTexParameterfv", "PointParameterf",
        "PointParameterfv", "TexParameterfv", "BindBuffer", "BufferData",
        "BufferSubData", "ClipPlanex", "Color4ub", "DeleteBuffers",
        "GetBooleanv", "GetBufferParameteriv", "GetClipPlanex", "GenBuffers",
        "GetFixedv", "GetLightxv", "GetMaterialxv", "GetPointerv",
        "GetTexEnviv", "GetTexEnvxv", "GetTexParameteriv",
        "GetTexParameterxv", "IsBuffer", "IsEnabled", "IsTexture",
        "PointParameterx", "PointParameterxv", "TexEnvi", "TexEnviv",
        "TexParameteri", "TexParameteriv", "TexParameterxv",
        "PointSizePointerOES",
    };

    if (index >= 0 && index < static_cast<int>(std::size(names))) {
        return names[index];
    }
    return nullptr;
}

#endif
