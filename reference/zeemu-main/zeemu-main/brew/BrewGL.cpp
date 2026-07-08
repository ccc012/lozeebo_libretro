#include "brew/Brew3D.h"
#include "brew/Brew3DCommon.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewFileMgr.h"
#include "brew/BrewQXGL.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include "graphics/RenderBackend.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace {

bool trace_gles_textures() {
    return std::getenv("ZEEMU_TRACE_GLES_TEXTURES") != nullptr;
}

bool dump_gles_textures() {
    return std::getenv("ZEEMU_DUMP_GLES_TEXTURES") != nullptr;
}

enum class OesPaletteEncoding {
    RGB8,
    RGBA8,
    RGB565,
    RGBA4,
    RGB5A1,
};

struct OesPaletteFormat {
    int bits_per_index = 0;
    int palette_entries = 0;
    int palette_entry_bytes = 0;
    OesPaletteEncoding encoding = OesPaletteEncoding::RGB8;
};

bool describe_oes_palette_format(uint32_t internal_format, OesPaletteFormat& out) {
    switch (internal_format) {
    case 0x8B90: out = {4, 16, 3, OesPaletteEncoding::RGB8}; return true;    // GL_PALETTE4_RGB8_OES
    case 0x8B91: out = {4, 16, 4, OesPaletteEncoding::RGBA8}; return true;   // GL_PALETTE4_RGBA8_OES
    case 0x8B92: out = {4, 16, 2, OesPaletteEncoding::RGB565}; return true;  // GL_PALETTE4_R5_G6_B5_OES
    case 0x8B93: out = {4, 16, 2, OesPaletteEncoding::RGBA4}; return true;   // GL_PALETTE4_RGBA4_OES
    case 0x8B94: out = {4, 16, 2, OesPaletteEncoding::RGB5A1}; return true;  // GL_PALETTE4_RGB5_A1_OES
    case 0x8B95: out = {8, 256, 3, OesPaletteEncoding::RGB8}; return true;   // GL_PALETTE8_RGB8_OES
    case 0x8B96: out = {8, 256, 4, OesPaletteEncoding::RGBA8}; return true;  // GL_PALETTE8_RGBA8_OES
    case 0x8B97: out = {8, 256, 2, OesPaletteEncoding::RGB565}; return true; // GL_PALETTE8_R5_G6_B5_OES
    case 0x8B98: out = {8, 256, 2, OesPaletteEncoding::RGBA4}; return true;  // GL_PALETTE8_RGBA4_OES
    case 0x8B99: out = {8, 256, 2, OesPaletteEncoding::RGB5A1}; return true; // GL_PALETTE8_RGB5_A1_OES
    default: return false;
    }
}

uint8_t expand4(uint16_t value) {
    value &= 0x0F;
    return static_cast<uint8_t>((value << 4) | value);
}

uint8_t expand5(uint16_t value) {
    value &= 0x1F;
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t expand6(uint16_t value) {
    value &= 0x3F;
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}

bool decode_oes_paletted_texture(uint32_t internal_format,
                                 int width,
                                 int height,
                                 const std::vector<uint8_t>& source,
                                 std::vector<uint8_t>& rgba) {
    OesPaletteFormat format;
    if (!describe_oes_palette_format(internal_format, format) || width <= 0 || height <= 0) {
        return false;
    }

    const uint64_t pixel_count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    const uint64_t palette_bytes = static_cast<uint64_t>(format.palette_entries) * format.palette_entry_bytes;
    const uint64_t index_bytes = format.bits_per_index == 4 ? ((pixel_count + 1) / 2) : pixel_count;
    const uint64_t required_bytes = palette_bytes + index_bytes;
    if (required_bytes > source.size() || pixel_count > (64ull * 1024ull * 1024ull)) {
        return false;
    }

    std::vector<std::array<uint8_t, 4>> palette(static_cast<size_t>(format.palette_entries));
    for (int i = 0; i < format.palette_entries; ++i) {
        const uint8_t* entry = source.data() + static_cast<size_t>(i) * format.palette_entry_bytes;
        switch (format.encoding) {
        case OesPaletteEncoding::RGB8:
            palette[static_cast<size_t>(i)] = {entry[0], entry[1], entry[2], 0xFF};
            break;
        case OesPaletteEncoding::RGBA8:
            palette[static_cast<size_t>(i)] = {entry[0], entry[1], entry[2], entry[3]};
            break;
        case OesPaletteEncoding::RGB565: {
            const uint16_t v = static_cast<uint16_t>(entry[0]) | (static_cast<uint16_t>(entry[1]) << 8);
            palette[static_cast<size_t>(i)] = {expand5(v >> 11), expand6(v >> 5), expand5(v), 0xFF};
            break;
        }
        case OesPaletteEncoding::RGBA4: {
            const uint16_t v = static_cast<uint16_t>(entry[0]) | (static_cast<uint16_t>(entry[1]) << 8);
            palette[static_cast<size_t>(i)] = {expand4(v >> 12), expand4(v >> 8), expand4(v >> 4), expand4(v)};
            break;
        }
        case OesPaletteEncoding::RGB5A1: {
            const uint16_t v = static_cast<uint16_t>(entry[0]) | (static_cast<uint16_t>(entry[1]) << 8);
            palette[static_cast<size_t>(i)] = {expand5(v >> 11), expand5(v >> 6), expand5(v >> 1),
                                               static_cast<uint8_t>((v & 1) ? 0xFF : 0x00)};
            break;
        }
        }
    }

    rgba.assign(static_cast<size_t>(pixel_count) * 4, 0);
    const uint8_t* indices = source.data() + static_cast<size_t>(palette_bytes);
    for (uint64_t pixel = 0; pixel < pixel_count; ++pixel) {
        uint8_t index = 0;
        if (format.bits_per_index == 4) {
            const uint8_t packed = indices[pixel / 2];
            index = (pixel & 1) ? static_cast<uint8_t>(packed >> 4) : static_cast<uint8_t>(packed & 0x0F);
        } else {
            index = indices[pixel];
        }

        const auto& color = palette[index];
        const size_t out = static_cast<size_t>(pixel) * 4;
        rgba[out + 0] = color[0];
        rgba[out + 1] = color[1];
        rgba[out + 2] = color[2];
        rgba[out + 3] = color[3];
    }

    return true;
}

void dump_decoded_texture_ppm(uint32_t texture,
                              uint32_t internal_format,
                              int width,
                              int height,
                              const std::vector<uint8_t>& rgba) {
    char path[160];
    std::snprintf(path, sizeof(path), "logs/texture_%02u_%04x_%dx%d.ppm", texture, internal_format, width, height);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int i = 0; i < width * height; ++i) {
        out.put(static_cast<char>(rgba[static_cast<size_t>(i) * 4 + 0]));
        out.put(static_cast<char>(rgba[static_cast<size_t>(i) * 4 + 1]));
        out.put(static_cast<char>(rgba[static_cast<size_t>(i) * 4 + 2]));
    }

    char alpha_path[176];
    std::snprintf(alpha_path, sizeof(alpha_path), "logs/texture_%02u_%04x_%dx%d_alpha.ppm", texture, internal_format, width, height);
    std::ofstream alpha(alpha_path, std::ios::binary);
    if (!alpha) {
        return;
    }
    alpha << "P6\n" << width << " " << height << "\n255\n";
    for (int i = 0; i < width * height; ++i) {
        const uint8_t a = rgba[static_cast<size_t>(i) * 4 + 3];
        alpha.put(static_cast<char>(a));
        alpha.put(static_cast<char>(a));
        alpha.put(static_cast<char>(a));
    }
}

} // namespace

BrewGL::BrewGL(BrewShell& shell, EndianMemory& memory)
    : shell_(shell), memory_(memory) {
    setup_vtable();
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

void BrewGL::setup_vtable() {
    vtable_ptr_ = shell_.malloc(128 * 4);
    object_ptr_ = shell_.malloc(128 * 4);
    memory_.write_value(object_ptr_, vtable_ptr_);
    static const char* kGlMethods[] = {
        "glActiveTexture", "glAlphaFuncx", "glBindTexture", "glBlendFunc",
        "glClear", "glClearColorx", "glClearDepthx", "glClearStencil",
        "glClientActiveTexture", "glColor4x", "glColorMask", "glColorPointer",
        "glCompressedTexImage2D", "glCompressedTexSubImage2D", "glCopyTexImage2D",
        "glCopyTexSubImage2D", "glCullFace", "glDeleteTextures", "glDepthFunc",
        "glDepthMask", "glDepthRangex", "glDisable", "glDisableClientState",
        "glDrawArrays", "glDrawElements", "glEnable", "glEnableClientState",
        "glFinish", "glFlush", "glFogx", "glFogxv", "glFrontFace", "glFrustumx",
        "glGenTextures", "glGetError", "glGetIntegerv", "glGetString", "glHint",
        "glLightModelx", "glLightModelxv", "glLightx", "glLightxv", "glLineWidthx",
        "glLoadIdentity", "glLoadMatrixx", "glLogicOp", "glMaterialx",
        "glMaterialxv", "glMatrixMode", "glMultMatrixx", "glMultiTexCoord4x",
        "glNormal3x", "glNormalPointer", "glOrthox", "glPixelStorei",
        "glPointSizex", "glPolygonOffsetx", "glPopMatrix", "glPushMatrix",
        "glReadPixels", "glRotatex", "glSampleCoveragex", "glScalex", "glScissor",
        "glShadeModel", "glStencilFunc", "glStencilMask", "glStencilOp",
        "glTexCoordPointer", "glTexEnvx", "glTexEnvxv", "glTexImage2D",
        "glTexParameterx", "glTexSubImage2D", "glTranslatex", "glVertexPointer",
        "glViewport",
    };
    static const char* kComNames[] = {
        "AddRef", "Release", "QueryInterface",
        "glActiveTexture", "glAlphaFuncx", "glBindTexture", "glBlendFunc",
        "glClear", "glClearColorx", "glClearDepthx", "glClearStencil",
        "glClientActiveTexture", "glColor4x", "glColorMask", "glColorPointer",
        "glCompressedTexImage2D", "glCompressedTexSubImage2D", "glCopyTexImage2D",
        "glCopyTexSubImage2D", "glCullFace", "glDeleteTextures", "glDepthFunc",
        "glDepthMask", "glDepthRangex", "glDisable", "glDisableClientState",
        "glDrawArrays", "glDrawElements", "glEnable", "glEnableClientState",
        "glFinish", "glFlush", "glFogx", "glFogxv", "glFrontFace", "glFrustumx",
        "glGenTextures", "glGetError", "glGetIntegerv", "glGetString", "glHint",
        "glLightModelx", "glLightModelxv", "glLightx", "glLightxv", "glLineWidthx",
        "glLoadIdentity", "glLoadMatrixx", "glLogicOp", "glMaterialx",
        "glMaterialxv", "glMatrixMode", "glMultMatrixx", "glMultiTexCoord4x",
        "glNormal3x", "glNormalPointer", "glOrthox", "glPixelStorei",
        "glPointSizex", "glPolygonOffsetx", "glPopMatrix", "glPushMatrix",
        "glReadPixels", "glRotatex", "glSampleCoveragex", "glScalex", "glScissor",
        "glShadeModel", "glStencilFunc", "glStencilMask", "glStencilOp",
        "glTexCoordPointer", "glTexEnvx", "glTexEnvxv", "glTexImage2D",
        "glTexParameterx", "glTexSubImage2D", "glTranslatex", "glVertexPointer",
        "glViewport",
    };
    for (int i = 0; i < 128; ++i) {
        std::string name = "IGL_Fn" + std::to_string(i);
        if (i < (int)(sizeof(kComNames) / sizeof(kComNames[0]))) {
            name = std::string("IGL_") + kComNames[i];
        }
        const addr_t hook_addr = shell_.add_hook(name, this);
        memory_.write_value(vtable_ptr_ + (uint32_t)(i * 4), hook_addr);
    }

    // The Qualcomm GLES_1x wrapper uses two shapes:
    // - IGL_* macros use the normal BREW COM object at object[0] -> vtable.
    // - Some old plain-GL veneers use the IGL pointer itself as a no-this GL
    //   function table. Mirror that direct table after object[0] while keeping
    //   the COM vtable pointer intact.
    for (int i = 1; i < 128; ++i) {
        std::string name = "IGL_Fn" + std::to_string(i);
        const int direct_index = i - 1;
        if (direct_index < (int)(sizeof(kGlMethods) / sizeof(kGlMethods[0]))) {
            name = std::string("IGL_") + kGlMethods[direct_index];
        }
        memory_.write_value(object_ptr_ + (uint32_t)(i * 4), shell_.add_hook(name, this));
    }
}

uint16_t BrewGL::clear_color_565_from_fixed(uint32_t r, uint32_t g, uint32_t b) const {
    auto fixed_to_8 = [](uint32_t v) -> uint32_t {
        uint32_t scaled = v >> 8;
        return std::min<uint32_t>(scaled, 255u);
    };
    uint32_t rr = fixed_to_8(r);
    uint32_t gg = fixed_to_8(g);
    uint32_t bb = fixed_to_8(b);
    return (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
}

void BrewGL::paint_visible_frame(const char* label) {
    BrewDisplay* display = shell_.get_display();
    if (!display) {
        return;
    }
    BrewBitmap* bmp = display->get_device_bitmap();
    if (!bmp) {
        return;
    }

    const size_t pixel_count = (size_t)bmp->get_width() * (size_t)bmp->get_height();
    const uint16_t fill = pending_clear_color_valid_ ? pending_clear_color_ : 0xF81F;
    std::string pattern(pixel_count * 2, '\0');
    for (size_t i = 0; i < pixel_count; ++i) {
        pattern[i * 2 + 0] = (char)(fill & 0xFFu);
        pattern[i * 2 + 1] = (char)((fill >> 8) & 0xFFu);
    }
    memory_.write(bmp->get_buffer_ptr(), pattern);

    if (fill == 0x0000) {
        const uint16_t accent = 0xF81F;
        for (int y = 0; y < 32 && y < bmp->get_height(); ++y) {
            for (int x = 0; x < 180 && x < bmp->get_width(); ++x) {
                memory_.write_value(bmp->get_buffer_ptr() + (addr_t)((y * bmp->get_width() + x) * 2),
                                    accent, EndianMemory::Halfword);
            }
        }
    }

    if (shell_.get_presenter() && label && *label) {
        shell_.get_presenter()->draw_debug_text(8.0f, 8.0f, label);
    }
}

void BrewGL::handle_hook(const std::string& name, CPU& cpu) {
    uint32_t r0 = cpu.get_reg(REG_R0);
    uint32_t r1 = cpu.get_reg(REG_R1);
    uint32_t r2 = cpu.get_reg(REG_R2);
    uint32_t r3 = cpu.get_reg(REG_R3);
    uint32_t r4 = cpu.get_reg(REG_R4);
    uint32_t r5 = cpu.get_reg(REG_R5);
    uint32_t r6 = cpu.get_reg(REG_R6);
    uint32_t r7 = cpu.get_reg(REG_R7);

    if (handle_qx_gl_call(name, shell_, memory_, cpu, "QXGL")) {
        return;
    }

    if (name == "IGL_AddRef") {
        cpu.set_reg(REG_R0, r0);
    } else if (name == "IGL_Release") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_QueryInterface") {
        uint32_t pp = r2;
        if (pp) memory_.write_value(pp, object_ptr_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glGetString") {
        uint32_t token = r0;
        addr_t ret = 0;
        switch (token) {
            case 0x1F00: ret = vendor_ptr_; break;
            case 0x1F01: ret = renderer_ptr_; break;
            case 0x1F02: ret = version_ptr_; break;
            case 0x1F03: ret = extensions_ptr_; break;
            default: ret = 0; break;
        }
        printf("  IGL_glGetString 0x%08x -> 0x%08x\n", token, ret);
        cpu.set_reg(REG_R0, ret);
    } else if (name == "IGL_glGetError") {
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glGenTextures") {
        uint32_t n = r0, textures = r1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = next_texture_id_++;
            texture_names_.insert(id);
            memory_.write_value(textures + i * 4, id);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glDeleteTextures") {
        uint32_t n = r0, textures = r1;
        auto* presenter = shell_.get_presenter();
        bool logged = false;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t id = memory_.read_value(textures + i * 4);
            if (texture_delete_log_count_ < 8) {
                printf("  IGL_glDeleteTextures id=%u\n", id);
                logged = true;
            }
            texture_names_.erase(id);
            textures_.erase(id);
            if (presenter) {
                presenter->guest_gl_delete_texture(id);
            }
            if (bound_texture_2d_ == id) {
                bound_texture_2d_ = 0;
            }
        }
        pending_preserved_texture_2d_ = 0;
        if (!logged && texture_delete_log_count_ == 8) {
            printf("  IGL_glDeleteTextures suppressing repeated cleanup logs\n");
        }
        ++texture_delete_log_count_;
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glIsTexture") {
        cpu.set_reg(REG_R0, texture_names_.count(r0) ? 1u : 0u);
    } else if (name == "IGL_glGenBuffers") {
        uint32_t n = r0, buffers = r1;
        for (uint32_t i = 0; i < n; ++i) memory_.write_value(buffers + i * 4, next_texture_id_++);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glGetIntegerv") {
        uint32_t params = r1;
        if (params) memory_.write_value(params, 0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glClearColorx" || name == "IGL_glClearColor" ||
               name == "IGLES_ClearColorx" || name == "IGLES_ClearColor") {
        pending_clear_color_ = clear_color_565_from_fixed(r0, r1, r2);
        pending_clear_color_valid_ = true;
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_clear_color(gl_fixed_to_float(r0), gl_fixed_to_float(r1), gl_fixed_to_float(r2), gl_fixed_to_float(r3));
        }
        printf("  %s r=0x%x g=0x%x b=0x%x a=0x%x -> 0x%04x\n",
               name.c_str(), r0, r1, r2, r3, pending_clear_color_);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glClear" || name == "IGLES_Clear") {
        printf("  IGL_glClear mask=0x%x\n", r0);
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_clear(r0);
        }
        // The visible old-GLES frame is produced by the SDL clear above plus the
        // SDL_RenderGeometry bridge. paint_visible_frame() only painted a debug
        // magenta fill / accent block into the guest device bitmap, which then
        // composited over the real output as large opaque magenta rectangles on
        // the Double Dragon title. Drop it.
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glViewport") {
        printf("  IGL_glViewport x=%u y=%u w=%u h=%u\n", r0, r1, r2, r3);
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_viewport(static_cast<int>(r0), static_cast<int>(r1), static_cast<int>(r2), static_cast<int>(r3));
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glMatrixMode") {
        printf("  IGL_glMatrixMode 0x%x\n", r0);
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glLoadIdentity") {
        printf("  IGL_glLoadIdentity\n");
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glBindTexture") {
        uint32_t texture = r1;
        if (r0 == 0x0DE1 && texture == 0 && r6 > 0 && r6 < 0x10000) {
            // Observed in Double Dragon's old Qualcomm GL thunk path during
            // texture setup: R1 is zero, while the intended texture id is kept
            // in R6. Real glBindTexture(GL_TEXTURE_2D, 0) unbind calls also
            // preserve loop counters in R6 during cleanup, so do not bind R6
            // immediately. Keep it as a pending preserved texture id and only
            // consume it if the next relevant operation is an upload.
            pending_preserved_texture_2d_ = r6;
        } else if (r0 == 0x0DE1) {
            pending_preserved_texture_2d_ = 0;
        }
        if (r0 == 0x0DE1) {
            bound_texture_2d_ = texture;
            if (texture != 0 && texture_names_.count(texture) != 0) {
                textures_[texture].target = r0;
                last_bound_texture_2d_ = texture;
            }
        }
        if (texture == 0) {
            if (texture_unbind_log_count_ < 8) {
                printf("  IGL_glBindTexture target=0x%x texture=0\n", r0);
            } else if (texture_unbind_log_count_ == 8) {
                printf("  IGL_glBindTexture suppressing repeated unbind logs\n");
            }
            ++texture_unbind_log_count_;
        } else {
            printf("  IGL_glBindTexture target=0x%x texture=%u\n", r0, texture);
        }
        if (auto* presenter = shell_.get_presenter()) {
            presenter->guest_gl_bind_texture(r0, texture);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glTexImage2D") {
        if (bound_texture_2d_ == 0 && pending_preserved_texture_2d_ != 0) {
            bound_texture_2d_ = pending_preserved_texture_2d_;
            texture_names_.insert(bound_texture_2d_);
            if (auto* presenter = shell_.get_presenter()) {
                presenter->guest_gl_bind_texture(0x0DE1, bound_texture_2d_);
            }
            printf("  IGL_glTexImage2D using preserved texture=%u\n", bound_texture_2d_);
        }
        pending_preserved_texture_2d_ = 0;
        if (bound_texture_2d_ != 0) {
            const auto plausible_dim = [](uint32_t value) {
                return value > 0 && value <= 4096;
            };
            const auto plausible_format = [](uint32_t value) {
                switch (value) {
                    case 0x1906: // GL_ALPHA
                    case 0x1907: // GL_RGB
                    case 0x1908: // GL_RGBA
                    case 0x1909: // GL_LUMINANCE
                    case 0x190A: // GL_LUMINANCE_ALPHA
                        return true;
                    default:
                        return false;
                }
            };
            const auto plausible_type = [](uint32_t value) {
                switch (value) {
                    case 0x1401: // GL_UNSIGNED_BYTE
                    case 0x140C: // GL_FIXED
                    case 0x8363: // GL_UNSIGNED_SHORT_5_6_5
                    case 0x8033: // GL_UNSIGNED_SHORT_4_4_4_4
                    case 0x8034: // GL_UNSIGNED_SHORT_5_5_5_1
                        return true;
                    default:
                        return false;
                }
            };
            const auto choose_first = [](std::initializer_list<uint32_t> values, auto pred, uint32_t fallback) -> uint32_t {
                for (uint32_t value : values) {
                    if (pred(value)) {
                        return value;
                    }
                }
                return fallback;
            };

            // Old Qualcomm BREW GL veneers are not a clean AAPCS call at this
            // boundary. The first four GL args are in R0-R3, while observed
            // sample binaries also preserve later arguments in R4-R7. Prefer
            // preserved-register values here so texture upload cannot block
            // startup while deeper stack decoding is still under investigation.
            const uint32_t stack_height = stack_arg(cpu, memory_, 0);
            const uint32_t stack_border = stack_arg(cpu, memory_, 1);
            const uint32_t stack_format = stack_arg(cpu, memory_, 2);
            const uint32_t stack_type = stack_arg(cpu, memory_, 3);
            const uint32_t stack_pixels = stack_arg(cpu, memory_, 4);

            const uint32_t height = plausible_dim(stack_height) ? stack_height : choose_first({r6, r4, r7}, plausible_dim, r3);
            const uint32_t border = stack_border <= 1 ? stack_border : 0;
            const uint32_t format = plausible_format(stack_format) ? stack_format : choose_first({r5, r6, r4, r7}, plausible_format, r2);
            const uint32_t type = plausible_type(stack_type) ? stack_type : choose_first({r7, r4}, plausible_type, 0x1401);
            const auto plausible_pointer = [](uint32_t value) {
                return value >= 0x10000 && value < 0xFF000000;
            };
            const uint32_t sp = cpu.get_reg(REG_SP);
            const uint32_t stack_pixels_alt1 = stack_arg(cpu, memory_, 5);
            const uint32_t stack_pixels_alt2 = stack_arg(cpu, memory_, 6);
            const uint32_t stack_pixels_alt3 = stack_arg(cpu, memory_, 7);

            auto bytes_per_pixel = [](uint32_t fmt, uint32_t ty) -> uint32_t {
                if (ty == 0x8363 || ty == 0x8033 || ty == 0x8034) return 2; // packed 16-bit
                if (ty != 0x1401) return 0; // GL_UNSIGNED_BYTE only here
                switch (fmt) {
                    case 0x1906: return 1; // GL_ALPHA
                    case 0x1907: return 3; // GL_RGB
                    case 0x1908: return 4; // GL_RGBA
                    case 0x1909: return 1; // GL_LUMINANCE
                    case 0x190A: return 2; // GL_LUMINANCE_ALPHA
                    default: return 0;
                }
            };

            std::vector<uint8_t> pixel_copy;
            const void* pixel_data = nullptr;
            const uint32_t bpp = bytes_per_pixel(format, type);
            const uint64_t byte_count64 = static_cast<uint64_t>(r3) * static_cast<uint64_t>(height) * bpp;
            uint32_t pixels = 0;
            size_t uploaded_bytes = 0;
            bool used_shadow_pixels = false;
            if (bpp != 0 && byte_count64 <= 16u * 1024u * 1024u) {
                const auto byte_count = static_cast<uint32_t>(byte_count64);
                if (stack_pixels != 0 && stack_pixels < 0x10000) {
                    if (const auto* shadow = brew_take_low_pointer_shadow(stack_pixels, byte_count)) {
                        pixels = stack_pixels;
                        pixel_data = shadow->data();
                        uploaded_bytes = byte_count;
                        used_shadow_pixels = true;
                    }
                }
                if (!pixel_data) {
                    pixels = choose_first({stack_pixels, stack_pixels_alt2, stack_pixels_alt3, r4, r7, stack_pixels_alt1}, plausible_pointer, 0);
                    if (pixels) {
                        pixel_copy.resize(byte_count);
                        for (uint32_t i = 0; i < byte_count; ++i) {
                            pixel_copy[i] = static_cast<uint8_t>(memory_.read_value(pixels + i, EndianMemory::Byte));
                        }
                        pixel_data = pixel_copy.data();
                        uploaded_bytes = pixel_copy.size();
                    }
                }
            }

            TextureInfo& info = textures_[bound_texture_2d_];
            info.target = r0;
            info.width = r3;
            info.height = height;
            info.format = format;
            info.type = type;
            info.compressed = false;
            if (auto* presenter = shell_.get_presenter()) {
                presenter->guest_gl_tex_image_2d(
                    r0,
                    static_cast<int>(r1),
                    static_cast<int>(r2),
                    static_cast<int>(info.width),
                    static_cast<int>(info.height),
                    static_cast<int>(border),
                    info.format,
                    info.type,
                    pixel_data);
            }
            if (trace_gles_textures()) {
                printf("  IGL_glTexImage2D tex=%u target=0x%x size=%ux%u ifmt=0x%x border=%u format=0x%x type=0x%x pixels=0x%08x copied=%zu shadow=%d sp=0x%08x stack=[%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x] r4=0x%08x r7=0x%08x\n",
                       bound_texture_2d_, info.target, info.width, info.height, r2, border,
                       info.format, info.type, pixels, uploaded_bytes, used_shadow_pixels ? 1 : 0, sp,
                       stack_height, stack_border, stack_format, stack_type, stack_pixels,
                       stack_pixels_alt1, stack_pixels_alt2, stack_pixels_alt3, r4, r7);
            }
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glTexSubImage2D") {
        if (trace_gles_textures()) {
            printf("  IGL_glTexSubImage2D tex=%u target=0x%x size=%ux%u pixels=0x%08x\n",
                   bound_texture_2d_, r0, stack_arg(cpu, memory_, 2), stack_arg(cpu, memory_, 3), stack_arg(cpu, memory_, 5));
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glCompressedTexImage2D") {
        if (bound_texture_2d_ == 0 && pending_preserved_texture_2d_ != 0) {
            bound_texture_2d_ = pending_preserved_texture_2d_;
            texture_names_.insert(bound_texture_2d_);
            if (auto* presenter = shell_.get_presenter()) {
                presenter->guest_gl_bind_texture(0x0DE1, bound_texture_2d_);
            }
            printf("  IGL_glCompressedTexImage2D using preserved texture=%u\n", bound_texture_2d_);
        } else if (bound_texture_2d_ == 0 && last_bound_texture_2d_ != 0 && texture_names_.count(last_bound_texture_2d_) != 0) {
            bound_texture_2d_ = last_bound_texture_2d_;
            if (auto* presenter = shell_.get_presenter()) {
                presenter->guest_gl_bind_texture(0x0DE1, bound_texture_2d_);
            }
            printf("  IGL_glCompressedTexImage2D using last bound texture=%u\n", bound_texture_2d_);
        }
        pending_preserved_texture_2d_ = 0;
        if (bound_texture_2d_ != 0) {
            uint32_t width = r3;
            uint32_t height = stack_arg(cpu, memory_, 0);
            uint32_t image_size = stack_arg(cpu, memory_, 2);
            uint32_t data_ptr = stack_arg(cpu, memory_, 3);
            const uint32_t border = stack_arg(cpu, memory_, 1);
            TextureInfo& info = textures_[bound_texture_2d_];
            info.target = r0;
            info.width = width;
            info.height = height;
            info.format = r2;
            info.type = 0;
            info.compressed = true;
            bool decoded_oes = false;
            if (width > 0 && height > 0 && image_size > 0 && image_size <= (16u * 1024u * 1024u)
                && data_ptr != 0 && data_ptr < 0xFF000000) {
                OesPaletteFormat oes_format;
                if (describe_oes_palette_format(r2, oes_format)) {
                    std::vector<uint8_t> compressed_copy(image_size);
                    for (uint32_t i = 0; i < image_size; ++i) {
                        compressed_copy[i] = static_cast<uint8_t>(memory_.read_value(data_ptr + i, EndianMemory::Byte));
                    }

                    std::vector<uint8_t> rgba;
                    decoded_oes = decode_oes_paletted_texture(r2, static_cast<int>(width), static_cast<int>(height),
                                                              compressed_copy, rgba);
                    if (decoded_oes) {
                        info.format = 0x1908; // GL_RGBA
                        info.type = 0x1401;   // GL_UNSIGNED_BYTE
                        info.compressed = false;
                        if (dump_gles_textures()) {
                            dump_decoded_texture_ppm(bound_texture_2d_, r2, static_cast<int>(width), static_cast<int>(height), rgba);
                        }
                        if (auto* presenter = shell_.get_presenter()) {
                            presenter->guest_gl_tex_image_2d(
                                r0,
                                static_cast<int>(r1),
                                static_cast<int>(r2),
                                static_cast<int>(width),
                                static_cast<int>(height),
                                0,
                                info.format,
                                info.type,
                                rgba.data());
                        }
                    }
                }
            }
            // Keep unknown guest compressed payloads guest-owned until the real
            // SDK or title trace identifies the compression interface. Guessing
            // custom containers here turns emulator work into per-game asset
            // modding.
            if (trace_gles_textures()) {
                printf("  IGL_glCompressedTexImage2D tex=%u target=0x%x level=%u size=%ux%u border=%u format=0x%x bytes=%u data=0x%08x decoded_oes=%d stack=[%08x,%08x,%08x,%08x] r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x\n",
                       bound_texture_2d_, info.target, r1, info.width, info.height, border, r2, image_size, data_ptr, decoded_oes ? 1 : 0,
                       height, border, image_size, data_ptr, r4, r5, r6, r7);
            }
        } else if (trace_gles_textures()) {
            printf("  IGL_glCompressedTexImage2D skipped: no bound texture pending=%u target=0x%x level=%u format=0x%x width=%u stack=[%08x,%08x,%08x,%08x] r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x\n",
                   pending_preserved_texture_2d_, r0, r1, r2, r3,
                   stack_arg(cpu, memory_, 0), stack_arg(cpu, memory_, 1),
                   stack_arg(cpu, memory_, 2), stack_arg(cpu, memory_, 3),
                   r4, r5, r6, r7);
        }
        cpu.set_reg(REG_R0, 0);
    } else if (name == "IGL_glCompressedTexSubImage2D") {
        if (trace_gles_textures()) {
            printf("  IGL_glCompressedTexSubImage2D tex=%u target=0x%x size=%ux%u bytes=%u data=0x%08x\n",
                   bound_texture_2d_, r0, stack_arg(cpu, memory_, 2), stack_arg(cpu, memory_, 3), stack_arg(cpu, memory_, 5), stack_arg(cpu, memory_, 6));
        }
        cpu.set_reg(REG_R0, 0);
    } else {
        printf("  [%s] not implemented yet R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x R6=0x%08x R7=0x%08x\n",
               name.c_str(), r0, r1, r2, r3, r4, r5, r6, r7);
        cpu.set_reg(REG_R0, 0);
    }
}
