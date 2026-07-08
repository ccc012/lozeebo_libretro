#include "brew/Brew3D.h"
#include "brew/Brew3DCommon.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewQXGL.h"
#include "brew/BrewQXGLState.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include "graphics/RenderBackend.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
BrewEGL::BrewEGL(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtables();
    const char* ext = gl_extensions();
    extensions_ptr_ = shell_.malloc((uint32_t)std::strlen(ext) + 1);
    memory_.write(extensions_ptr_, std::string(ext, std::strlen(ext) + 1));

    vendor_ptr_ = shell_.malloc(16);
    memory_.write(vendor_ptr_, std::string("Qualcomm", 9));
    
    renderer_ptr_ = shell_.malloc(32);
    memory_.write(renderer_ptr_, std::string("Adreno 130", 11));
    
    version_ptr_ = shell_.malloc(16);
    memory_.write(version_ptr_, std::string("1.1", 4));
}

void BrewEGL::setup_vtables() {
    qegl_vtable_ptr_ = shell_.malloc(40 * 4);
    qegl_object_ptr_ = shell_.malloc(4);
    memory_.write_value(qegl_object_ptr_, qegl_vtable_ptr_);

    auto add_qegl = [&](int index, const std::string& name) {
        memory_.write_value(qegl_vtable_ptr_ + (uint32_t)(index * 4),
                            shell_.add_hook("IQEGL_" + name, this));
    };

    add_qegl(0, "AddRef");
    add_qegl(1, "Release");
    add_qegl(2, "QueryInterface");
    add_qegl(3, "GetError");
    add_qegl(4, "GetDisplay");
    add_qegl(5, "Initialize");
    add_qegl(6, "Terminate");
    add_qegl(7, "QueryString");
    add_qegl(8, "GetConfigs");
    add_qegl(9, "ChooseConfig");
    add_qegl(10, "GetConfigAttrib");
    add_qegl(11, "CreateWindowSurface");
    add_qegl(12, "CreatePixmapSurface");
    add_qegl(13, "CreatePbufferSurface");
    add_qegl(14, "DestroySurface");
    add_qegl(15, "QuerySurface");
    add_qegl(16, "CreateContext");
    add_qegl(17, "DestroyContext");
    add_qegl(18, "MakeCurrent");
    add_qegl(19, "GetCurrentContext");
    add_qegl(20, "GetCurrentSurface");
    add_qegl(21, "GetCurrentDisplay");
    add_qegl(22, "QueryContext");
    add_qegl(23, "WaitGL");
    add_qegl(24, "WaitNative");
    add_qegl(25, "SwapBuffers");
    add_qegl(26, "CopyBuffers");
    for (int index = 27; index < 40; ++index) {
        add_qegl(index, "Fn" + std::to_string(index));
    }

    egl_vtable_ptr_ = shell_.malloc(32 * 4);
    egl_object_ptr_ = shell_.malloc(4);
    memory_.write_value(egl_object_ptr_, egl_vtable_ptr_);

    auto add_egl = [&](int index, const std::string& name) {
        memory_.write_value(egl_vtable_ptr_ + (uint32_t)(index * 4),
                            shell_.add_hook("IEGL_" + name, this));
    };

    add_egl(0, "AddRef");
    add_egl(1, "Release");
    add_egl(2, "QueryInterface");
    add_egl(3, "GetError");
    add_egl(4, "GetDisplay");
    add_egl(5, "Initialize");
    add_egl(6, "Terminate");
    add_egl(7, "QueryString");
    add_egl(8, "GetProcAddress");
    add_egl(9, "GetConfigs");
    add_egl(10, "ChooseConfig");
    add_egl(11, "GetConfigAttrib");
    add_egl(12, "CreateWindowSurface");
    add_egl(13, "CreatePixmapSurface");
    add_egl(14, "CreatePbufferSurface");
    add_egl(15, "DestroySurface");
    add_egl(16, "QuerySurface");
    add_egl(17, "CreateContext");
    add_egl(18, "DestroyContext");
    add_egl(19, "MakeCurrent");
    add_egl(20, "GetCurrentContext");
    add_egl(21, "GetCurrentSurface");
    add_egl(22, "GetCurrentDisplay");
    add_egl(23, "QueryContext");
    add_egl(24, "WaitGL");
    add_egl(25, "WaitNative");
    add_egl(26, "SwapBuffers");
    add_egl(27, "CopyBuffers");
    for (int index = 28; index < 32; ++index) {
        add_egl(index, "Fn" + std::to_string(index));
    }

    gles_vtable_ptr_ = shell_.malloc(160 * 4);
    gles_object_ptr_ = shell_.malloc(4);
    memory_.write_value(gles_object_ptr_, gles_vtable_ptr_);

    for (int i = 0; i < 160; ++i) {
        std::string name = "IGLES_Fn" + std::to_string(i);
        if (const char* sdk_name = gles_name_for_index(i)) {
            name = std::string("IGLES_") + sdk_name;
        }
        memory_.write_value(gles_vtable_ptr_ + (uint32_t)(i * 4), shell_.add_hook(name, this));
    }

    gles10_ext_vtable_ptr_ = shell_.malloc(4 * 4);
    gles10_ext_object_ptr_ = shell_.malloc(4);
    memory_.write_value(gles10_ext_object_ptr_, gles10_ext_vtable_ptr_);
    static const char* kGles10ExtMethods[] = {
        "AddRef", "Release", "QueryInterface", "QueryMatrixxOES",
    };
    for (int i = 0; i < 4; ++i) {
        memory_.write_value(gles10_ext_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IGLES10EXT_") + kGles10ExtMethods[i], this));
    }

    gles11_ext_vtable_ptr_ = shell_.malloc(15 * 4);
    gles11_ext_object_ptr_ = shell_.malloc(4);
    memory_.write_value(gles11_ext_object_ptr_, gles11_ext_vtable_ptr_);
    static const char* kGles11ExtMethods[] = {
        "AddRef", "Release", "QueryInterface",
        "CurrentPaletteMatrixOES", "LoadPaletteFromModelViewMatrixOES",
        "MatrixIndexPointerOES", "WeightPointerOES",
        "DrawTexsOES", "DrawTexiOES", "DrawTexxOES",
        "DrawTexsvOES", "DrawTexivOES", "DrawTexxvOES",
        "DrawTexfOES", "DrawTexfvOES",
    };
    for (int i = 0; i < 15; ++i) {
        memory_.write_value(gles11_ext_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IGLES11EXT_") + kGles11ExtMethods[i], this));
    }

    gles11_extpak_vtable_ptr_ = shell_.malloc(30 * 4);
    gles11_extpak_object_ptr_ = shell_.malloc(4);
    memory_.write_value(gles11_extpak_object_ptr_, gles11_extpak_vtable_ptr_);
    static const char* kGles11ExtPakMethods[] = {
        "AddRef", "Release", "QueryInterface",
        "GetTexGenfv", "GetTexGeniv", "GetTexGenxv",
        "TexGenf", "TexGeni", "TexGenx",
        "TexGenfv", "TexGeniv", "TexGenxv",
        "BlendEquation", "BlendFuncSeparate", "BlendEquationSeparate",
        "BindFramebufferOES", "BindRenderbufferOES", "CheckFramebufferStatusOES",
        "DeleteFramebuffersOES", "DeleteRenderbuffersOES",
        "FramebufferRenderbufferOES", "FramebufferTexture2DOES",
        "GenerateMipmapOES", "GenFramebuffersOES", "GenRenderbuffersOES",
        "GetFramebufferAttachmentParameterivOES", "GetRenderbufferParameterivOES",
        "IsFramebufferOES", "IsRenderbufferOES", "RenderbufferStorageOES",
    };
    for (int i = 0; i < 30; ++i) {
        memory_.write_value(gles11_extpak_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IGLES11EXTPAK_") + kGles11ExtPakMethods[i], this));
    }

    gles_imageon_ext_vtable_ptr_ = shell_.malloc(27 * 4);
    gles_imageon_ext_object_ptr_ = shell_.malloc(4);
    memory_.write_value(gles_imageon_ext_object_ptr_, gles_imageon_ext_vtable_ptr_);
    static const char* kGlesImageonExtMethods[] = {
        "AddRef", "Release", "QueryInterface",
        "PointSizePointerOES", "BlendEquationSeparateEXT", "BlendFuncSeparateEXT",
        "BlendEquationEXT", "BindBufferQUALCOMM", "DeleteBuffersQUALCOMM",
        "GenBuffersQUALCOMM", "BufferDataQUALCOMM", "BufferSubDataQUALCOMM",
        "IsBufferQUALCOMM", "BufferDataATI", "MeshListATI",
        "DrawVertexBufferObjectATI", "GetPointerv", "TexEnvi", "TexEnviv",
        "TexParameteri", "TexParameteriv", "TexParameterfv", "TexParameterxv",
        "GetMaterialfv", "GetTexParameteriv", "GetTexParameterfv",
        "GetTexParameterxv",
    };
    for (int i = 0; i < 27; ++i) {
        memory_.write_value(gles_imageon_ext_vtable_ptr_ + static_cast<uint32_t>(i * 4),
                            shell_.add_hook(std::string("IGLESIMAGEONEXT_") + kGlesImageonExtMethods[i], this));
    }

    surface_manip_vtable_ptr_ = shell_.malloc(27 * 4);
    surface_manip_object_ptr_ = shell_.malloc(4);
    memory_.write_value(surface_manip_object_ptr_, surface_manip_vtable_ptr_);

    auto add_surface_manip = [&](int index, const std::string& name) {
        memory_.write_value(surface_manip_vtable_ptr_ + (uint32_t)(index * 4),
                            shell_.add_hook("IEGLSurfaceManip_" + name, this));
    };

    add_surface_manip(0, "AddRef");
    add_surface_manip(1, "Release");
    add_surface_manip(2, "QueryInterface");
    add_surface_manip(3, "SurfaceScaleEnable");
    add_surface_manip(4, "SetSurfaceScale");
    add_surface_manip(5, "GetSurfaceScale");
    add_surface_manip(6, "GetSurfaceScaleCaps");
    add_surface_manip(7, "SurfaceRotateEnable");
    add_surface_manip(8, "SetSurfaceRotate");
    add_surface_manip(9, "GetSurfaceRotate");
    add_surface_manip(10, "GetSurfaceRotateCaps");
    add_surface_manip(11, "SurfaceTransparencyEnable");
    add_surface_manip(12, "SetSurfaceTransparency");
    add_surface_manip(13, "GetSurfaceTransparency");
    add_surface_manip(14, "SetSurfaceTransparencyMap");
    add_surface_manip(15, "GetSurfaceTransparencyMap");
    add_surface_manip(16, "GetSurfaceTransparencyCaps");
    add_surface_manip(17, "SurfaceColorKeyEnable");
    add_surface_manip(18, "SetSurfaceColorKey");
    add_surface_manip(19, "GetSurfaceColorKey");
    add_surface_manip(20, "CreateCompositeSurface");
    add_surface_manip(21, "SurfaceOverlayEnable");
    add_surface_manip(22, "SurfaceOverlayLayerEnable");
    add_surface_manip(23, "SurfaceOverlayBind");
    add_surface_manip(24, "GetSurfaceOverlayBinding");
    add_surface_manip(25, "GetSurfaceOverlay");
    add_surface_manip(26, "GetSurfaceOverlayCaps");
}

addr_t BrewEGL::make_proc_stub(const std::string& name) {
    return shell_.add_hook("GLProc_" + name, this);
}

void BrewEGL::show_visible_marker(const char* label) {
    const std::string key = label ? label : "(null)";
    static std::unordered_map<std::string, int> marker_counts;
    int& count = marker_counts[key];
    if (count < 8 || std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
        std::getenv("ZEEMU_TRACE_RENDER_PROFILE") != nullptr) {
        printf("  [BrewEGL] %s\n", key.c_str());
        ++count;
    }
}

BrewEGL::SurfaceState& BrewEGL::surface_state(uint32_t surface) {
    auto [it, inserted] = surfaces_.try_emplace(surface);
    if (inserted) {
        it->second.scale_dst = {0, 0, 640, 480};
    }
    return it->second;
}

uint32_t BrewEGL::create_window_surface_state() {
    const uint32_t surface = next_surface_id_++;
    SurfaceState state;
    state.scale_src = {0, 0, 640, 480};
    state.scale_dst = {0, 0, 640, 480};
    surfaces_[surface] = state;
    return surface;
}

uint32_t BrewEGL::query_surface_value(uint32_t surface, uint32_t attrib) {
    const auto& state = surface_state(surface);
    switch (attrib) {
        case 0x3057: return state.width;  // EGL_WIDTH
        case 0x3056: return state.height; // EGL_HEIGHT
        case 0x3093: return state.swap_behavior; // EGL_SWAP_BEHAVIOR
        default: return 0;
    }
}

void BrewEGL::set_surface_swap_behavior(uint32_t surface, uint32_t value) {
    if (value == 0x3094 || value == 0x3095) { // EGL_BUFFER_PRESERVED / EGL_BUFFER_DESTROYED
        surface_state(surface).swap_behavior = value;
    }
}

bool BrewEGL::read_surface_scale_rect(uint32_t addr, SurfaceScaleRect& rect) {
    if (addr == 0 || addr >= 0xFF000000) {
        return false;
    }
    rect.x = memory_.read_value(addr + 0);
    rect.y = memory_.read_value(addr + 4);
    rect.width = memory_.read_value(addr + 8);
    rect.height = memory_.read_value(addr + 12);
    return rect.width != 0 && rect.height != 0;
}

void BrewEGL::write_surface_scale_rect(uint32_t addr, const SurfaceScaleRect& rect) {
    if (addr == 0 || addr >= 0xFF000000) {
        return;
    }
    memory_.write_value(addr + 0, rect.x);
    memory_.write_value(addr + 4, rect.y);
    memory_.write_value(addr + 8, rect.width);
    memory_.write_value(addr + 12, rect.height);
}

bool BrewEGL::set_surface_scale_state(uint32_t surface, uint32_t src_addr, uint32_t dst_addr) {
    if (src_addr == 0 && dst_addr == 0) {
        return false;
    }

    auto& state = surface_state(surface);
    SurfaceScaleRect src = state.has_scale_src ? state.scale_src : SurfaceScaleRect{0, 0, state.width, state.height};
    SurfaceScaleRect dst = state.has_scale_dst ? state.scale_dst : SurfaceScaleRect{0, 0, 640, 480};

    if (src_addr != 0 && !read_surface_scale_rect(src_addr, src)) {
        return false;
    }
    if (dst_addr != 0 && !read_surface_scale_rect(dst_addr, dst)) {
        return false;
    }

    state.scale_src = src;
    state.scale_dst = dst;
    state.has_scale_src = true;
    state.has_scale_dst = true;
    // QUALCOMM_surface_scale resizes the EGL window-surface buffers to the
    // source rect dimensions; eglQuerySurface must report that resized size.
    state.width = src.width;
    state.height = src.height;
    printf("  [BrewEGL] SurfaceScale surface=0x%08x src=%u,%u %ux%u dst=%u,%u %ux%u\n",
           surface, src.x, src.y, src.width, src.height, dst.x, dst.y, dst.width, dst.height);
    return true;
}

void BrewEGL::apply_surface_scale_to_presenter(uint32_t surface) {
    auto* presenter = shell_.get_presenter();
    if (!presenter) {
        return;
    }

    auto& state = surface_state(surface);
    if (!state.scale_enabled || !state.has_scale_src) {
        presenter->guest_gl_surface_scale(false, 0, 0, 0, 0, 0, 0, 0, 0);
        return;
    }

    const SurfaceScaleRect& src = state.scale_src;
    const SurfaceScaleRect dst = state.has_scale_dst ? state.scale_dst : SurfaceScaleRect{0, 0, 640, 480};
    presenter->guest_gl_surface_scale(true,
                                      static_cast<int>(src.x),
                                      static_cast<int>(src.y),
                                      static_cast<int>(src.width),
                                      static_cast<int>(src.height),
                                      static_cast<int>(dst.x),
                                      static_cast<int>(dst.y),
                                      static_cast<int>(dst.width),
                                      static_cast<int>(dst.height));
}

void BrewEGL::write_surface_scale_caps(uint32_t addr) {
    if (addr == 0 || addr >= 0xFF000000) {
        return;
    }
    static const uint32_t values[] = {
        0x00004000, 0x00040000, // Min/Max X scale factor
        0x00004000, 0x00040000, // Min/Max Y scale factor
        1, 640,                 // Min/Max source width
        1, 480,                 // Min/Max source height
        1, 640,                 // Min/Max destination width
        1, 480,                 // Min/Max destination height
    };
    for (uint32_t i = 0; i < static_cast<uint32_t>(sizeof(values) / sizeof(values[0])); ++i) {
        memory_.write_value(addr + i * 4, values[i]);
    }
}

void BrewEGL::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);

    if (handle_qx_gl_call(name, shell_, memory_, cpu, "QXGL")) {
        return;
    }

    auto gles_extension_object_for_iid = [&](uint32_t cls) -> uint32_t {
        switch (cls) {
            case 0x0103d8de: return gles10_ext_object_ptr_;       // AEEIID_GLES10EXT
            case 0x0103d8eb: return gles11_ext_object_ptr_;       // AEEIID_GLES11EXT
            case 0x0103def1: return gles11_extpak_object_ptr_;    // AEEIID_GLES11EXTPAK
            case 0x01058546: return gles_imageon_ext_object_ptr_; // AEEIID_GLESIMAGEONEXT
            case 0x010459b1: return gles_imageon_ext_object_ptr_; // AEEIID_GLESIMAGEONEXT_V1
            default: return 0;
        }
    };
    auto handle_draw_tex_oes = [&]() -> bool {
        const bool object_call = name.rfind("IGLES11EXT_DrawTex", 0) == 0;
        const bool proc_call = name.rfind("GLProc_glDrawTex", 0) == 0;
        if (!object_call && !proc_call) {
            return false;
        }

        const std::string op = object_call
            ? name.substr(std::strlen("IGLES11EXT_"))
            : name.substr(std::strlen("GLProc_gl"));
        const size_t prefix_len = std::strlen("DrawTex");
        if (op.size() <= prefix_len) {
            cpu.set_reg(REG_R0, 0);
            return true;
        }
        const char type = op[prefix_len];
        const bool vector_args = op.find('v', prefix_len) != std::string::npos;
        auto decode_scalar = [&](uint32_t raw) -> float {
            switch (type) {
                case 's': return static_cast<float>(static_cast<int16_t>(raw & 0xffffu));
                case 'i': return static_cast<float>(static_cast<int32_t>(raw));
                case 'x': return qxgl::fixed_to_float(raw);
                case 'f': return qxgl::raw_to_float(raw);
                default: return 0.0f;
            }
        };
        auto read_vector_component = [&](addr_t ptr, int index) -> float {
            if (ptr == 0 || ptr >= 0xFF000000u) {
                return 0.0f;
            }
            if (type == 's') {
                const uint32_t raw = memory_.read_value(ptr + static_cast<addr_t>(index * 2), EndianMemory::Halfword);
                return decode_scalar(raw);
            }
            const uint32_t raw = memory_.read_value(ptr + static_cast<addr_t>(index * 4));
            return decode_scalar(raw);
        };

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        if (vector_args) {
            const addr_t coords = object_call ? r1 : r0;
            x = read_vector_component(coords, 0);
            y = read_vector_component(coords, 1);
            z = read_vector_component(coords, 2);
            w = read_vector_component(coords, 3);
            h = read_vector_component(coords, 4);
        } else if (object_call) {
            x = decode_scalar(r1);
            y = decode_scalar(r2);
            z = decode_scalar(r3);
            w = decode_scalar(stack_arg(cpu, memory_, 0));
            h = decode_scalar(stack_arg(cpu, memory_, 1));
        } else {
            x = decode_scalar(r0);
            y = decode_scalar(r1);
            z = decode_scalar(r2);
            w = decode_scalar(r3);
            h = decode_scalar(stack_arg(cpu, memory_, 0));
        }

        auto& state = qxgl::qx_state();
        const uint32_t unit = std::min<uint32_t>(state.active_texture_unit, 1u);
        const uint32_t texture_id = state.bound_textures[unit];
        const auto tex_it = state.textures.find(texture_id);
        static int draw_tex_logs = 0;
        const bool log_draw_tex = draw_tex_logs < 16 || qxgl::trace_gles_vertices() || qxgl::trace_gles_textures();
        if (tex_it == state.textures.end() || tex_it->second.width <= 0 || tex_it->second.height <= 0 ||
            w == 0.0f || h == 0.0f) {
            if (log_draw_tex) {
                printf("  %s skipped tex=%u size=%dx%d xywh=(%.3f,%.3f %.3fx%.3f)\n",
                       name.c_str(), texture_id,
                       tex_it != state.textures.end() ? tex_it->second.width : 0,
                       tex_it != state.textures.end() ? tex_it->second.height : 0,
                       x, y, w, h);
                ++draw_tex_logs;
            }
            cpu.set_reg(REG_R0, 0);
            return true;
        }

        const qxgl::TextureInfo& tex = tex_it->second;
        const int32_t crop_x = tex.has_crop_rect ? tex.crop_rect[0] : 0;
        const int32_t crop_y = tex.has_crop_rect ? tex.crop_rect[1] : 0;
        const int32_t crop_w = tex.has_crop_rect ? tex.crop_rect[2] : tex.width;
        const int32_t crop_h = tex.has_crop_rect ? tex.crop_rect[3] : tex.height;
        const float u0 = static_cast<float>(crop_x) / static_cast<float>(tex.width);
        const float v0 = static_cast<float>(crop_y) / static_cast<float>(tex.height);
        const float u1 = static_cast<float>(crop_x + crop_w) / static_cast<float>(tex.width);
        const float v1 = static_cast<float>(crop_y + crop_h) / static_cast<float>(tex.height);

        const float vp_x = static_cast<float>(state.viewport[0]);
        const float vp_y = static_cast<float>(state.viewport[1]);
        const float vp_h = static_cast<float>(state.viewport[3]);
        const float left = vp_x + x;
        const float right = vp_x + x + w;
        const float screen_y0 = vp_y + vp_h - y;
        const float screen_y1 = vp_y + vp_h - (y + h);
        const float bottom = std::max(screen_y0, screen_y1);
        const float top = std::min(screen_y0, screen_y1);

        auto make_vertex = [&](float px, float py, float u, float v) {
            zeemu::gfx::GuestGLVertex2D out{};
            out.x = px;
            out.y = py;
            out.z = z;
            out.r = 1.0f;
            out.g = 1.0f;
            out.b = 1.0f;
            out.a = 1.0f;
            out.u = u;
            out.v = v;
            out.inv_w = 1.0f;
            return out;
        };

        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_active_texture_unit(unit);
            presenter->guest_gl_bind_texture(qxgl::kGlTexture2D, texture_id);
            presenter->guest_gl_draw_triangle(
                make_vertex(left, bottom, u0, v0),
                make_vertex(right, bottom, u1, v0),
                make_vertex(right, top, u1, v1));
            presenter->guest_gl_draw_triangle(
                make_vertex(left, bottom, u0, v0),
                make_vertex(right, top, u1, v1),
                make_vertex(left, top, u0, v1));
        }
        if (log_draw_tex) {
            printf("  %s tex=%u tex_size=%dx%d crop=(%d,%d %dx%d) rect=(%.3f,%.3f %.3fx%.3f) screen=(%.3f,%.3f)-(%.3f,%.3f) uv=(%.6f,%.6f)-(%.6f,%.6f)\n",
                   name.c_str(), texture_id, tex.width, tex.height,
                   crop_x, crop_y, crop_w, crop_h, x, y, w, h,
                   left, top, right, bottom, u0, v0, u1, v1);
            ++draw_tex_logs;
        }
        cpu.set_reg(REG_R0, 0);
        return true;
    };

    if (name == "IQEGL_AddRef" || name == "IEGL_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IQEGL_Release" || name == "IEGL_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_QueryInterface" || name == "IEGL_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        uint32_t obj = 0;
        if (cls == 0x01014bc4) obj = egl_object_ptr_;
        else if (cls == 0x0103d8ec || cls == 0x0103d8ed || cls == 0x0103d8ee) obj = qegl_object_ptr_;
        else if (cls == 0x0103d8dd || cls == 0x0103d8ea) obj = gles_object_ptr_;
        else if ((obj = gles_extension_object_for_iid(cls)) != 0) {}
        else if (cls == 0x010434cc || cls == 0x01051834) obj = surface_manip_object_ptr_;
        else if (cls == 0x0103d8ef || cls == 0x0103d8f0 || cls == 0x010426e3 ||
                 cls == 0x0109b10e) obj = qegl_object_ptr_;
        if (pp != 0 && obj != 0) memory_.write_value(pp, obj);
        printf("  %s cls=0x%08x pp=0x%08x -> 0x%08x\n", name.c_str(), cls, pp, obj);
        show_visible_marker("QEGL");
        cpu.set_reg(REG_R0, obj ? 0u : 4u);
    } else if (name == "IGLES10EXT_AddRef" || name == "IGLES11EXT_AddRef" ||
               name == "IGLES11EXTPAK_AddRef" || name == "IGLESIMAGEONEXT_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IGLES10EXT_Release" || name == "IGLES11EXT_Release" ||
               name == "IGLES11EXTPAK_Release" || name == "IGLESIMAGEONEXT_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES10EXT_QueryInterface" || name == "IGLES11EXT_QueryInterface" ||
               name == "IGLES11EXTPAK_QueryInterface" || name == "IGLESIMAGEONEXT_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        uint32_t obj = gles_extension_object_for_iid(cls);
        if (obj == 0 && (cls == 0x0103d8dd || cls == 0x0103d8ea)) {
            obj = gles_object_ptr_;
        }
        if (pp && pp < 0xFF000000) {
            memory_.write_value(pp, obj);
        }
        printf("  %s cls=0x%08x pp=0x%08x -> 0x%08x\n", name.c_str(), cls, pp, obj);
        cpu.set_reg(REG_R0, obj ? 0u : 4u);
    } else if (name == "IGLES10EXT_QueryMatrixxOES") {
        const uint32_t mantissa = r1;
        const uint32_t exponent = r2;
        const uint32_t ret = r3;
        for (int i = 0; i < 16; ++i) {
            if (mantissa && mantissa < 0xFF000000) {
                memory_.write_value(mantissa + static_cast<uint32_t>(i * 4), (i % 5) == 0 ? 0x00010000u : 0u);
            }
            if (exponent && exponent < 0xFF000000) {
                memory_.write_value(exponent + static_cast<uint32_t>(i * 4), 0u);
            }
        }
        write_bool_ret(memory_, ret, 0);
        cpu.set_reg(REG_R0, 0);
    } else if (handle_draw_tex_oes()) {
        return;
    } else if (name == "IGLESIMAGEONEXT_PointSizePointerOES") {
        if (handle_qx_gl_call("IGLES_PointSizePointerOES", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_BindBufferQUALCOMM") {
        if (handle_qx_gl_call("IGLES_BindBuffer", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_DeleteBuffersQUALCOMM") {
        if (handle_qx_gl_call("IGLES_DeleteBuffers", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_GenBuffersQUALCOMM") {
        if (handle_qx_gl_call("IGLES_GenBuffers", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_BufferDataQUALCOMM") {
        if (handle_qx_gl_call("IGLES_BufferData", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_BufferSubDataQUALCOMM") {
        if (handle_qx_gl_call("IGLES_BufferSubData", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_IsBufferQUALCOMM") {
        const uint32_t ret = r2;
        write_bool_ret(memory_, ret, 0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_GetPointerv") {
        if (handle_qx_gl_call("IGLES_GetPointerv", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexEnvi") {
        if (handle_qx_gl_call("IGLES_TexEnvi", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexEnviv") {
        if (handle_qx_gl_call("IGLES_TexEnviv", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexParameteri") {
        if (handle_qx_gl_call("IGLES_TexParameteri", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexParameteriv") {
        if (handle_qx_gl_call("IGLES_TexParameteriv", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexParameterfv") {
        if (handle_qx_gl_call("IGLES_TexParameterfv", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLESIMAGEONEXT_TexParameterxv") {
        if (handle_qx_gl_call("IGLES_TexParameterxv", shell_, memory_, cpu, "QXGL")) return;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES11EXTPAK_CheckFramebufferStatusOES") {
        // GL_FRAMEBUFFER_COMPLETE_OES
        write_bool_ret(memory_, r2, 0x8CD5);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES11EXTPAK_GenFramebuffersOES" || name == "IGLES11EXTPAK_GenRenderbuffersOES") {
        const uint32_t n = r1;
        const uint32_t out = r2;
        for (uint32_t i = 0; i < n; ++i) {
            if (out && out < 0xFF000000) {
                memory_.write_value(out + i * 4, next_texture_id_++);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES11EXTPAK_IsFramebufferOES" || name == "IGLES11EXTPAK_IsRenderbufferOES") {
        write_bool_ret(memory_, r2, r1 != 0 ? 1u : 0u);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES11EXTPAK_GetTexGenfv" || name == "IGLES11EXTPAK_GetTexGeniv" ||
               name == "IGLES11EXTPAK_GetTexGenxv" || name == "IGLES11EXTPAK_GetFramebufferAttachmentParameterivOES" ||
               name == "IGLES11EXTPAK_GetRenderbufferParameterivOES" ||
               name == "IGLESIMAGEONEXT_GetMaterialfv" || name == "IGLESIMAGEONEXT_GetTexParameteriv" ||
               name == "IGLESIMAGEONEXT_GetTexParameterfv" || name == "IGLESIMAGEONEXT_GetTexParameterxv") {
        uint32_t out = r3;
        if (name == "IGLES11EXTPAK_GetFramebufferAttachmentParameterivOES") {
            out = stack_arg(cpu, memory_, 0);
        }
        if (name == "IGLESIMAGEONEXT_GetMaterialfv" || name == "IGLESIMAGEONEXT_GetTexParameteriv" ||
            name == "IGLESIMAGEONEXT_GetTexParameterfv" || name == "IGLESIMAGEONEXT_GetTexParameterxv") {
            out = r3;
        }
        if (out && out < 0xFF000000) {
            memory_.write_value(out, 0u);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES11EXT_", 0) == 0 || name.rfind("IGLES11EXTPAK_", 0) == 0 ||
               name.rfind("IGLESIMAGEONEXT_", 0) == 0) {
        printf("  [%s] not implemented yet R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r1, r2, r3, stack_arg(cpu, memory_, 0));
        cpu.set_reg(REG_R0, 4);
    } else if (name == "IEGLSurfaceManip_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IEGLSurfaceManip_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_QueryInterface") {
        uint32_t cls = r1;
        uint32_t pp = r2;
        uint32_t obj = (cls == 0x010434cc || cls == 0x01051834) ? surface_manip_object_ptr_ : 0;
        if (pp && obj) memory_.write_value(pp, obj);
        cpu.set_reg(REG_R0, obj ? 0u : 4u);
    } else if (name == "IEGLSurfaceManip_SurfaceScaleEnable") {
        auto& state = surface_state(r2);
        state.scale_enabled = r3 != 0;
        printf("  IEGLSurfaceManip_SurfaceScaleEnable surface=0x%08x enable=%u ret=0x%08x\n",
               r2, r3, stack_arg(cpu, memory_, 0));
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_SurfaceRotateEnable" ||
               name == "IEGLSurfaceManip_SurfaceTransparencyEnable" ||
               name == "IEGLSurfaceManip_SurfaceColorKeyEnable") {
        printf("  %s stack0=0x%08x stack1=0x%08x\n",
               name.c_str(), stack_arg(cpu, memory_, 0), stack_arg(cpu, memory_, 1));
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_SetSurfaceScale") {
        const uint32_t dst = stack_arg(cpu, memory_, 0);
        const bool ok = set_surface_scale_state(r2, r3, dst);
        printf("  IEGLSurfaceManip_SetSurfaceScale surface=0x%08x src=0x%08x dst=0x%08x ret=0x%08x -> %u\n",
               r2, r3, dst, stack_arg(cpu, memory_, 1), ok ? 1u : 0u);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 1), ok ? 1u : 0u);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_GetSurfaceScale") {
        auto& state = surface_state(r2);
        uint32_t enabled = r3;
        uint32_t src = stack_arg(cpu, memory_, 0);
        uint32_t dst = stack_arg(cpu, memory_, 1);
        if (enabled && enabled < 0xFF000000) memory_.write_value(enabled, state.scale_enabled ? 1u : 0u);
        write_surface_scale_rect(src, state.scale_src);
        write_surface_scale_rect(dst, state.scale_dst);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 2), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_GetSurfaceScaleCaps") {
        write_surface_scale_caps(r3);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_GetSurfaceRotateCaps" ||
               name == "IEGLSurfaceManip_GetSurfaceTransparencyCaps" ||
               name == "IEGLSurfaceManip_GetSurfaceOverlayCaps") {
        if (r3 && r3 < 0xFF000000) {
            for (int i = 0; i < 8; ++i) {
                memory_.write_value(r3 + static_cast<uint32_t>(i * 4), 0u);
            }
        }
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_GetSurfaceRotate") {
        if (r3 && r3 < 0xFF000000) memory_.write_value(r3, 1u);
        uint32_t rot90 = stack_arg(cpu, memory_, 0);
        uint32_t hmirror = stack_arg(cpu, memory_, 1);
        if (rot90 && rot90 < 0xFF000000) memory_.write_value(rot90, 0u);
        if (hmirror && hmirror < 0xFF000000) memory_.write_value(hmirror, 0u);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 2), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_GetSurfaceTransparency" ||
               name == "IEGLSurfaceManip_GetSurfaceTransparencyMap" ||
               name == "IEGLSurfaceManip_GetSurfaceOverlayBinding" ||
               name == "IEGLSurfaceManip_GetSurfaceOverlay") {
        if (r3 && r3 < 0xFF000000) memory_.write_value(r3, 0u);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 1), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGLSurfaceManip_CreateCompositeSurface") {
        write_bool_ret(memory_, stack_arg(cpu, memory_, 1), 2);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IEGLSurfaceManip_", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, stack_arg(cpu, memory_, 0));
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IEGL_GetError") {
        cpu.set_reg(REG_R0, 0x3000); // EGL_SUCCESS
    } else if (name == "IEGL_GetDisplay") {
        // SDK_Zeebo/inc/AEEGL.h: IEGL_eglGetDisplay returns an opaque
        // EGLDisplay by value. Do not return the BREW IDisplay object here:
        // titles can later feed the display handle through GL/EGL state paths,
        // and an IDisplay* makes slot 3 resolve to IDisplay_MeasureTextEx.
        constexpr addr_t kEglDisplayHandle = 1;
        printf("  IEGL_GetDisplay native=0x%08x -> dpy=0x%08x\n", r0, kEglDisplayHandle);
        cpu.set_reg(REG_R0, kEglDisplayHandle);
    } else if (name == "IEGL_Initialize") {
        if (r1) memory_.write_value(r1, 1);
        if (r2) memory_.write_value(r2, 1); // Minor version 1.1?
        show_visible_marker("IEGL");
        cpu.set_reg(REG_R0, 1); // EGL_TRUE
    } else if (name == "IEGL_Terminate") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_QueryString") {
        uint32_t name_id = r1;
        addr_t ret = 0;
        switch (name_id) {
            case 0x3053: ret = vendor_ptr_; break;
            case 0x3054: ret = version_ptr_; break;
            case 0x3055: ret = extensions_ptr_; break;
            default: ret = 0; break;
        }
        cpu.set_reg(REG_R0, ret);
    } else if (name == "IEGL_GetConfigs") {
        uint32_t configs = r1;
        uint32_t config_size = r2;
        uint32_t num_config = r3;
        if (configs && config_size > 0) memory_.write_value(configs, 1);
        if (num_config) memory_.write_value(num_config, 1);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_ChooseConfig") {
        uint32_t configs = r2;
        uint32_t config_size = r3;
        uint32_t num_config = stack_arg(cpu, memory_, 0);
        if (configs && config_size > 0) memory_.write_value(configs, 1);
        if (num_config) memory_.write_value(num_config, 1);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_GetConfigAttrib") {
        uint32_t attrib = r2;
        uint32_t pValue = r3;
        uint32_t val = 0;
        switch (attrib) {
            case 0x3020: val = 16; break; // BUFFER_SIZE
            case 0x3024: val = 5; break;  // RED_SIZE
            case 0x3023: val = 6; break;  // GREEN_SIZE
            case 0x3022: val = 5; break;  // BLUE_SIZE
            case 0x3021: val = 0; break;  // ALPHA_SIZE
            case 0x3025: val = 16; break; // DEPTH_SIZE
            case 0x3026: val = 0; break;  // STENCIL_SIZE
            case 0x3033: val = 0x1; break; // SURFACE_TYPE: EGL_WINDOW_BIT
            default: val = 0; break;
        }
        if (pValue) memory_.write_value(pValue, val);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_CreateWindowSurface") {
        show_visible_marker("EGL Surface");
        cpu.set_reg(REG_R0, create_window_surface_state());
    } else if (name == "IEGL_DestroySurface") {
        surfaces_.erase(r2);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_QuerySurface") {
        uint32_t surface = r2;
        uint32_t attrib = r3;
        uint32_t pValue = stack_arg(cpu, memory_, 0);
        if (r2 == 0x3057 || r2 == 0x3056) {
            // Older local stubs used the wrong IEGL ABI. Keep this tolerant
            // path so existing traces still decode while SDK-shaped callers
            // use surface in R2, attribute in R3, value on the stack.
            surface = 0;
            attrib = r2;
            pValue = r3;
        }
        uint32_t val = query_surface_value(surface, attrib);
        if (pValue) memory_.write_value(pValue, val);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_CreateContext") {
        cpu.set_reg(REG_R0, 3); // context handle
    } else if (name == "IEGL_DestroyContext") {
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_MakeCurrent") {
        current_display_ = r0;
        current_draw_surface_ = r1;
        current_read_surface_ = r2;
        current_context_ = r3;
        show_visible_marker("EGL MakeCurrent");
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_GetCurrentContext") {
        cpu.set_reg(REG_R0, current_context_);
    } else if (name == "IEGL_GetCurrentSurface") {
        constexpr uint32_t EGL_DRAW = 0x3059u;
        constexpr uint32_t EGL_READ = 0x305Au;
        cpu.set_reg(REG_R0, r0 == EGL_READ ? current_read_surface_ :
                            (r0 == EGL_DRAW ? current_draw_surface_ : 0u));
    } else if (name == "IEGL_GetCurrentDisplay") {
        cpu.set_reg(REG_R0, current_display_);
    } else if (name == "IEGL_SwapBuffers") {
        static int iegl_swap_logs = 0;
        if (iegl_swap_logs < 8 || std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
            std::getenv("ZEEMU_TRACE_RENDER_PROFILE") != nullptr) {
            printf("  IEGL_SwapBuffers called! Rendering loop reached.\n");
            ++iegl_swap_logs;
        }
        if (auto* presenter = shell_.get_presenter()) {
            const uint32_t surface = r1 != 0 ? r1 : current_draw_surface_;
            apply_surface_scale_to_presenter(surface);
            presenter->guest_gl_swap_behavior_preserved(query_surface_value(surface, 0x3093) == 0x3094);
            presenter->guest_gl_swap_buffers();
        }
        show_visible_marker("EGL SwapBuffers");
        cpu.set_reg(REG_R0, 1);
    } else if (name == "IEGL_GetProcAddress") {
        char proc[96] = {};
        shell_.read_string(r0, proc, sizeof(proc));
        addr_t stub = make_proc_stub(proc);
        printf("  IEGL_GetProcAddress '%s' -> 0x%08x\n", proc, stub);
        cpu.set_reg(REG_R0, stub);
    } else if (name == "IQEGL_GetError") {
        write_bool_ret(memory_, r1, 0x3000); // EGL_SUCCESS
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetDisplay") {
        write_bool_ret(memory_, r2, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_Initialize") {
        uint32_t pRet = stack_arg(cpu, memory_, 0);
        if (r2) memory_.write_value(r2, 1);
        if (r3) memory_.write_value(r3, 1);
        show_visible_marker("IQEGL");
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_QueryString") {
        uint32_t name_id = r2;
        addr_t ret = 0;
        switch (name_id) {
            case 0x3053: ret = vendor_ptr_; break;
            case 0x3054: ret = version_ptr_; break;
            case 0x3055: ret = extensions_ptr_; break;
            default: ret = 0; break;
        }
        if (r3 && r3 < 0xFF000000) {
            memory_.write_value(r3, ret);
        } else {
            write_bool_ret(memory_, stack_arg(cpu, memory_, 0), ret);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_ChooseConfig") {
        uint32_t pConfigs = r3;
        uint32_t pNumConfig = stack_arg(cpu, memory_, 1);
        uint32_t pRet = stack_arg(cpu, memory_, 2);
        if (pConfigs) memory_.write_value(pConfigs, 1);
        if (pNumConfig) memory_.write_value(pNumConfig, 1);
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetConfigs") {
        uint32_t pConfigs = r2;
        uint32_t pNumConfig = stack_arg(cpu, memory_, 0);
        uint32_t pRet = stack_arg(cpu, memory_, 1);
        if (pConfigs) memory_.write_value(pConfigs, 1);
        if (pNumConfig) memory_.write_value(pNumConfig, 1);
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetConfigAttrib") {
        uint32_t attrib = r3;
        uint32_t pValue = stack_arg(cpu, memory_, 0);
        uint32_t pRet = stack_arg(cpu, memory_, 1);
        uint32_t val = 0;
        switch (attrib) {
            case 0x3020: val = 16; break;
            case 0x3024: val = 5; break;
            case 0x3023: val = 6; break;
            case 0x3022: val = 5; break;
            case 0x3021: val = 0; break;
            case 0x3025: val = 16; break;
            default: val = 0; break;
        }
        if (pValue) memory_.write_value(pValue, val);
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_CreateWindowSurface") {
        uint32_t pSurface = stack_arg(cpu, memory_, 1);
        show_visible_marker("QEGL Surface");
        write_bool_ret(memory_, pSurface, create_window_surface_state());
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_DestroySurface") {
        surfaces_.erase(r2);
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_QuerySurface") {
        uint32_t surface = r2;
        uint32_t attrib = r3;
        uint32_t pValue = stack_arg(cpu, memory_, 0);
        uint32_t pRet = stack_arg(cpu, memory_, 1);
        uint32_t val = query_surface_value(surface, attrib);
        if (pValue) memory_.write_value(pValue, val);
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_SurfaceAttrib") {
        const uint32_t pRet = stack_arg(cpu, memory_, 1);
        if (r3 == 0x3093) { // EGL_SWAP_BEHAVIOR
            set_surface_swap_behavior(r2, stack_arg(cpu, memory_, 0));
        }
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_CreateContext") {
        uint32_t pContext = stack_arg(cpu, memory_, 1);
        show_visible_marker("QEGL Context");
        write_bool_ret(memory_, pContext, 3);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_MakeCurrent") {
        current_display_ = r1;
        current_draw_surface_ = r2;
        current_read_surface_ = r3;
        current_context_ = stack_arg(cpu, memory_, 0);
        show_visible_marker("QEGL MakeCurrent");
        write_bool_ret(memory_, stack_arg(cpu, memory_, 1), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetCurrentContext") {
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1, current_context_);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetCurrentDisplay") {
        if (r1 && r1 < 0xFF000000) {
            memory_.write_value(r1, current_display_);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_GetCurrentSurface") {
        constexpr uint32_t EGL_DRAW = 0x3059u;
        constexpr uint32_t EGL_READ = 0x305Au;
        uint32_t pSurface = r2;
        if (pSurface && pSurface < 0xFF000000) {
            memory_.write_value(pSurface, r1 == EGL_READ ? current_read_surface_ :
                                          (r1 == EGL_DRAW ? current_draw_surface_ : 0u));
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_SwapBuffers") {
        static int iqegl_swap_logs = 0;
        if (iqegl_swap_logs < 8 || std::getenv("ZEEMU_TRACE_HLE") != nullptr ||
            std::getenv("ZEEMU_TRACE_RENDER_PROFILE") != nullptr) {
            printf("  IQEGL_SwapBuffers called! Rendering loop reached.\n");
            ++iqegl_swap_logs;
        }
        if (auto* presenter = shell_.get_presenter()) {
            const uint32_t surface = r2 != 0 ? r2 : current_draw_surface_;
            apply_surface_scale_to_presenter(surface);
            presenter->guest_gl_swap_behavior_preserved(query_surface_value(surface, 0x3093) == 0x3094);
            presenter->guest_gl_swap_buffers();
        }
        show_visible_marker("QEGL SwapBuffers");
        // BREW 4.0.2 AEEEGL10.h: SwapBuffers(pMe, dpy, draw, ret).
        // The return pointer is the fourth argument (R3), not a stack slot.
        uint32_t pRet = r3;
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "GLProc_eglSetSurfaceScaleQUALCOMM") {
        const bool ok = set_surface_scale_state(r1, r2, r3);
        printf("  GLProc_eglSetSurfaceScaleQUALCOMM dpy=0x%08x surface=0x%08x src=0x%08x dst=0x%08x -> %u\n",
               r0, r1, r2, r3, ok ? 1u : 0u);
        cpu.set_reg(REG_R0, ok ? 1u : 0u);
    } else if (name == "GLProc_eglSurfaceScaleEnableQUALCOMM") {
        auto& state = surface_state(r1);
        state.scale_enabled = r2 != 0;
        printf("  GLProc_eglSurfaceScaleEnableQUALCOMM dpy=0x%08x surface=0x%08x enable=%u\n", r0, r1, r2);
        cpu.set_reg(REG_R0, 1); // EGL_TRUE
    } else if (name == "GLProc_eglGetSurfaceScaleQUALCOMM") {
        auto& state = surface_state(r1);
        if (r2 && r2 < 0xFF000000) memory_.write_value(r2, state.scale_enabled ? 1u : 0u);
        write_surface_scale_rect(r3, state.scale_src);
        write_surface_scale_rect(stack_arg(cpu, memory_, 0), state.scale_dst);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "GLProc_eglGetSurfaceScaleCapsQUALCOMM") {
        write_surface_scale_caps(r2);
        cpu.set_reg(REG_R0, 1);
    } else if (name == "GLProc_eglSwapIntervalOES") {
        printf("  GLProc_eglSwapIntervalOES dpy=0x%08x interval=%u\n", r0, r1);
        cpu.set_reg(REG_R0, 1); // EGL_TRUE
    } else if (name.rfind("GLProc_", 0) == 0) {
        // Direct GLES methods from GetProcAddress
        // Need to identify them or return success
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IGLES_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES_QueryInterface") {
        uint32_t cls = r1, pp = r2;
        uint32_t obj = (cls == 0x0103d8dd || cls == 0x0103d8ea) ? gles_object_ptr_ : 0;
        if (pp) memory_.write_value(pp, obj);
        cpu.set_reg(REG_R0, obj ? 0u : 4u);
    } else if (name == "IGLES_GetString") {
        uint32_t token = r1;
        addr_t ret = 0;
        switch (token) {
            case 0x1F00: ret = vendor_ptr_; break;
            case 0x1F01: ret = renderer_ptr_; break;
            case 0x1F02: ret = version_ptr_; break;
            case 0x1F03: ret = extensions_ptr_; break;
            default: ret = 0; break;
        }
        printf("  IGLES_GetString token=0x%08x -> 0x%08x\n", token, ret);
        if (r2 && r2 < 0xFF000000) {
            // Qualcomm/BREW GLES wrapper ABI: object in R0, GLenum in R1,
            // Khronos return value written through R2, AEE result in R0.
            memory_.write_value(r2, ret);
            cpu.set_reg(REG_R0, 0);
        } else {
            cpu.set_reg(REG_R0, ret);
        }
    } else if (name == "IGLES_GetError") {
        if (r1 && r1 < 0xFF000000) {
            // Quake reaches this through the Qualcomm/BREW GLES wrapper ABI:
            // R0 is the IGLES object and R1 is the guest out-param for the
            // Khronos return value. Returning SUCCESS without clearing *R1
            // leaves stale error state and traps the app in a GetError loop.
            memory_.write_value(r1, 0u);
        }
        if (r2 && r2 < 0xFF000000) {
            // Some GLES veneers keep the object in R0 and shift the Khronos
            // return out-param to R2. Clearing both candidate return slots is
            // harmless for invalid pointers and avoids stale guest stack data.
            memory_.write_value(r2, 0u);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES_GenTextures") {
        uint32_t n = r1, textures = r2;
        for (uint32_t i = 0; i < n; ++i) memory_.write_value(textures + i * 4, next_texture_id_++);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGLES_GenBuffers") {
        uint32_t n = r1, buffers = r2;
        for (uint32_t i = 0; i < n; ++i) memory_.write_value(buffers + i * 4, next_texture_id_++);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_Viewport", 0) == 0) {
        const uint32_t x = r1;
        const uint32_t y = r2;
        const uint32_t w = r3;
        const uint32_t h = stack_arg(cpu, memory_, 0);
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_viewport(static_cast<int>(x), static_cast<int>(y),
                                         static_cast<int>(w), static_cast<int>(h));
        }
        printf("  IGLES_Viewport x=%u y=%u w=%u h=%u\n", x, y, w, h);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_ClearColorx", 0) == 0 || name.rfind("IGLES_ClearColor", 0) == 0) {
        const uint32_t red = r1;
        const uint32_t green = r2;
        const uint32_t blue = r3;
        const uint32_t alpha = stack_arg(cpu, memory_, 0);
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_clear_color(static_cast<float>(static_cast<int32_t>(red)) / 65536.0f,
                                            static_cast<float>(static_cast<int32_t>(green)) / 65536.0f,
                                            static_cast<float>(static_cast<int32_t>(blue)) / 65536.0f,
                                            static_cast<float>(static_cast<int32_t>(alpha)) / 65536.0f);
        }
        printf("  %s r=0x%x g=0x%x b=0x%x a=0x%x\n", name.c_str(), red, green, blue, alpha);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_Clear", 0) == 0) {
        const uint32_t mask = r1;
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_clear(mask);
        }
        printf("  IGLES_Clear mask=0x%x\n", mask);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_MatrixMode", 0) == 0) {
        printf("  IGLES_MatrixMode 0x%x\n", r1);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_LoadIdentity", 0) == 0) {
        printf("  IGLES_LoadIdentity\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_BindTexture", 0) == 0) {
        const uint32_t target = r1;
        const uint32_t texture = r2;
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_bind_texture(target, texture);
        }
        printf("  IGLES_BindTexture target=0x%x texture=%u\n", target, texture);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IQEGL_SurfaceRotateEnable") {
        write_bool_ret(memory_, stack_arg(cpu, memory_, 0), 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_Fn", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, stack_arg(cpu, memory_, 0));
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IGLES_", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, stack_arg(cpu, memory_, 0));
        cpu.set_reg(REG_R0, 4);
    } else if (name.rfind("IQEGL_Fn", 0) == 0 || name.rfind("IEGL_Fn", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, stack_arg(cpu, memory_, 0));
        uint32_t pRet = stack_arg(cpu, memory_, 0);
        write_bool_ret(memory_, pRet, 1);
        cpu.set_reg(REG_R0, 0);
    } else if (name.rfind("IQEGL_", 0) == 0) {
        uint32_t pRet = stack_arg(cpu, memory_, 0);
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x stack0=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, pRet);
        write_bool_ret(memory_, pRet, 0);
        cpu.set_reg(REG_R0, 4);
    } else if (name.rfind("IEGL_", 0) == 0) {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x\n",
               name.c_str(), r0, r1, r2, r3);
        cpu.set_reg(REG_R0, 1);
    }
}

