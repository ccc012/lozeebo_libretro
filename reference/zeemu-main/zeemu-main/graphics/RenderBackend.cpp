#include "graphics/RenderBackend.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <set>
#include <unordered_map>
#include <vector>

namespace zeemu::gfx {

namespace {

std::string lower_ascii(const char* value) {
    std::string out = value ? value : "";
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

constexpr uint32_t GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr uint32_t GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr uint32_t GL_TEXTURE_2D = 0x0DE1;
constexpr uint32_t GL_TEXTURE = 0x1702;
constexpr uint32_t GL_TEXTURE_ENV_MODE = 0x2200;
constexpr uint32_t GL_ALPHA = 0x1906;
constexpr uint32_t GL_RGB = 0x1907;
constexpr uint32_t GL_RGBA = 0x1908;
constexpr uint32_t GL_LUMINANCE = 0x1909;
constexpr uint32_t GL_LUMINANCE_ALPHA = 0x190A;
constexpr uint32_t GL_UNSIGNED_BYTE = 0x1401;
constexpr uint32_t GL_UNSIGNED_SHORT_5_6_5 = 0x8363;
constexpr uint32_t GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033;
constexpr uint32_t GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034;
constexpr uint32_t GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr uint32_t GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr uint32_t GL_TEXTURE_WRAP_S = 0x2802;
constexpr uint32_t GL_TEXTURE_WRAP_T = 0x2803;
constexpr uint32_t GL_REPEAT = 0x2901;
constexpr uint32_t GL_CLAMP_TO_EDGE = 0x812F;
constexpr uint32_t GL_NEAREST = 0x2600;
constexpr uint32_t GL_LINEAR = 0x2601;
constexpr uint32_t GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr uint32_t GL_LINEAR_MIPMAP_NEAREST = 0x2701;
constexpr uint32_t GL_NEAREST_MIPMAP_LINEAR = 0x2702;
constexpr uint32_t GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr uint32_t GL_NEVER = 0x0200;
constexpr uint32_t GL_LESS = 0x0201;
constexpr uint32_t GL_EQUAL = 0x0202;
constexpr uint32_t GL_LEQUAL = 0x0203;
constexpr uint32_t GL_GREATER = 0x0204;
constexpr uint32_t GL_NOTEQUAL = 0x0205;
constexpr uint32_t GL_GEQUAL = 0x0206;
constexpr uint32_t GL_ALWAYS = 0x0207;
constexpr uint32_t GL_ZERO = 0x0000;
constexpr uint32_t GL_ONE = 0x0001;
constexpr uint32_t GL_SRC_COLOR = 0x0300;
constexpr uint32_t GL_ONE_MINUS_SRC_COLOR = 0x0301;
constexpr uint32_t GL_SRC_ALPHA = 0x0302;
constexpr uint32_t GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr uint32_t GL_DST_ALPHA = 0x0304;
constexpr uint32_t GL_ONE_MINUS_DST_ALPHA = 0x0305;
constexpr uint32_t GL_DST_COLOR = 0x0306;
constexpr uint32_t GL_ONE_MINUS_DST_COLOR = 0x0307;
constexpr uint32_t GL_SRC_ALPHA_SATURATE = 0x0308;
constexpr uint32_t GL_MODULATE = 0x2100;
constexpr uint32_t GL_DECAL = 0x2101;
constexpr uint32_t GL_BLEND_ENV = 0x0BE2;
constexpr uint32_t GL_REPLACE = 0x1E01;
constexpr uint32_t GL_ADD = 0x0104;
constexpr uint32_t GL_SUBTRACT = 0x84E7;
constexpr uint32_t GL_COMBINE = 0x8570;
constexpr uint32_t GL_COMBINE_RGB = 0x8571;
constexpr uint32_t GL_COMBINE_ALPHA = 0x8572;
constexpr uint32_t GL_RGB_SCALE = 0x8573;
constexpr uint32_t GL_ADD_SIGNED = 0x8574;
constexpr uint32_t GL_INTERPOLATE = 0x8575;
constexpr uint32_t GL_CONSTANT = 0x8576;
constexpr uint32_t GL_PRIMARY_COLOR = 0x8577;
constexpr uint32_t GL_PREVIOUS = 0x8578;
constexpr uint32_t GL_SRC0_RGB = 0x8580;
constexpr uint32_t GL_SRC1_RGB = 0x8581;
constexpr uint32_t GL_SRC2_RGB = 0x8582;
constexpr uint32_t GL_SRC0_ALPHA = 0x8588;
constexpr uint32_t GL_SRC1_ALPHA = 0x8589;
constexpr uint32_t GL_SRC2_ALPHA = 0x858A;
constexpr uint32_t GL_OPERAND0_RGB = 0x8590;
constexpr uint32_t GL_OPERAND1_RGB = 0x8591;
constexpr uint32_t GL_OPERAND2_RGB = 0x8592;
constexpr uint32_t GL_OPERAND0_ALPHA = 0x8598;
constexpr uint32_t GL_OPERAND1_ALPHA = 0x8599;
constexpr uint32_t GL_OPERAND2_ALPHA = 0x859A;
constexpr uint32_t GL_ALPHA_SCALE = 0x0D1C;
constexpr uint32_t GL_DOT3_RGB = 0x86AE;
constexpr uint32_t GL_DOT3_RGBA = 0x86AF;

class SDLFramePresenter final : public FramePresenter {
public:
    SDLFramePresenter(RenderBackendKind kind, const char* sdl_driver)
        : kind_(kind), sdl_driver_(sdl_driver) {}

    ~SDLFramePresenter() override {
        for (auto& entry : guest_textures_) {
            SDL_DestroyTexture(entry.second.texture);
        }
        if (guest_gl_cpu_texture_) SDL_DestroyTexture(guest_gl_cpu_texture_);
        if (guest_gl_framebuffer_) SDL_DestroyTexture(guest_gl_framebuffer_);
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
    }

    bool initialize(const char* title, int width, int height) override {
        if (sdl_driver_) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, sdl_driver_);
        }

        const int window_width = std::max(width, 640);
        const int window_height = std::max(height, 480);
        window_ = SDL_CreateWindow(title, window_width, window_height, SDL_WINDOW_RESIZABLE);
        if (!window_) {
            std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
            return false;
        }

        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            std::cerr << "Failed to create " << render_backend_name(kind_)
                      << " SDL renderer: " << SDL_GetError() << std::endl;
            return false;
        }

        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                     width, height);
        if (!texture_) {
            std::cerr << "Failed to create SDL texture: " << SDL_GetError() << std::endl;
            return false;
        }
        texture_width_ = width;
        texture_height_ = height;
        guest_gl_framebuffer_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_TARGET, width, height);
        if (!guest_gl_framebuffer_) {
            std::cerr << "Failed to create guest GL framebuffer: " << SDL_GetError() << std::endl;
            return false;
        }
        guest_gl_cpu_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                                  SDL_TEXTUREACCESS_STATIC, width, height);
        if (!guest_gl_cpu_texture_) {
            std::cerr << "Failed to create guest GL CPU texture: " << SDL_GetError() << std::endl;
            return false;
        }

        if (!SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST)) {
            std::cerr << "Warning: failed to set nearest texture scaling: "
                      << SDL_GetError() << std::endl;
        }
        SDL_SetTextureScaleMode(guest_gl_framebuffer_, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(guest_gl_framebuffer_, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(guest_gl_cpu_texture_, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(guest_gl_cpu_texture_, SDL_BLENDMODE_NONE);

        if (!SDL_SetRenderLogicalPresentation(renderer_, width, height,
                                              SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
            std::cerr << "Warning: failed to enable logical presentation: "
                      << SDL_GetError() << std::endl;
        }

        logical_width_ = width;
        logical_height_ = height;
        framebuffer_rgba_.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        depth_buffer_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 1.0f);
        cpu_framebuffer_valid_ = true;
        guest_gl_viewport_ = SDL_Rect{0, 0, width, height};
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_SetRenderTarget(renderer_, nullptr);
        return true;
    }

    void begin_frame() override {
        suppress_host_bitmap_present_ = guest_gl_presented_since_host_frame_;
        if (suppress_host_bitmap_present_) {
            return;
        }
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
    }

    void present_rgb565(const void* pixels, int pitch) override {
        present_rgb565(pixels, pitch, logical_width_, logical_height_);
    }

    void present_rgb565(const void* pixels, int pitch, int width, int height) override {
        if (!pixels) return;
        if (width <= 0 || height <= 0) {
            return;
        }
        if (suppress_host_bitmap_present_ || guest_gl_presented_since_host_frame_) {
            if (std::getenv("ZEEMU_TRACE_RENDER") != nullptr) {
                std::cout << "SDLFramePresenter: skipped RGB565 present after guest GL swap" << std::endl;
            }
            return;
        }
        if (width != texture_width_ || height != texture_height_) {
            if (texture_) {
                SDL_DestroyTexture(texture_);
                texture_ = nullptr;
            }
            texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                         width, height);
            texture_width_ = width;
            texture_height_ = height;
            if (!texture_) {
                std::cerr << "Failed to create RGB565 presentation texture: "
                          << SDL_GetError() << std::endl;
                texture_width_ = 0;
                texture_height_ = 0;
                return;
            }
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        }
        bool trace_render = std::getenv("ZEEMU_TRACE_RENDER") != nullptr;
        if (trace_render) {
            std::cout << "SDLFramePresenter: SDL_UpdateTexture RGB565 "
                      << width << "x" << height << " pitch=" << pitch << std::endl;
        }
        if (!SDL_UpdateTexture(texture_, nullptr, pixels, pitch)) {
            std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << std::endl;
            return;
        }
        if (trace_render) {
            std::cout << "SDLFramePresenter: SDL_RenderTexture" << std::endl;
        }
        SDL_SetRenderTarget(renderer_, nullptr);
        if (!SDL_RenderTexture(renderer_, texture_, nullptr, nullptr)) {
            std::cerr << "SDL_RenderTexture failed: " << SDL_GetError() << std::endl;
        }
        maybe_dump_guest_gl_frame();
        guest_gl_frame_active_ = false;
    }

    void draw_debug_text(float x, float y, const char* text) override {
        if (!renderer_ || !text || !*text) {
            return;
        }
        if (!SDL_RenderDebugText(renderer_, x, y, text)) {
            std::cerr << "SDL_RenderDebugText failed: " << SDL_GetError() << std::endl;
        }
    }

    void end_frame() override {
        if (suppress_host_bitmap_present_) {
            suppress_host_bitmap_present_ = false;
            return;
        }
        SDL_RenderPresent(renderer_);
    }

    bool guest_gl_available() const override { return renderer_ != nullptr; }

    void guest_gl_viewport(int x, int y, int w, int h) override {
        if (!renderer_) return;
        guest_gl_viewport_ = SDL_Rect{x, y, w, h};
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
    }

    void guest_gl_surface_scale(bool enabled,
                                int src_x,
                                int src_y,
                                int src_w,
                                int src_h,
                                int dst_x,
                                int dst_y,
                                int dst_w,
                                int dst_h) override {
        guest_gl_surface_scale_enabled_ = enabled && src_w > 0 && src_h > 0 && dst_w > 0 && dst_h > 0;
        if (!guest_gl_surface_scale_enabled_) {
            if (std::getenv("ZEEMU_TRACE_RENDER") != nullptr) {
                std::cout << "SDLFramePresenter: guest GL surface scale disabled" << std::endl;
            }
            return;
        }
        guest_gl_surface_scale_src_ = SDL_FRect{
            static_cast<float>(src_x),
            static_cast<float>(src_y),
            static_cast<float>(src_w),
            static_cast<float>(src_h),
        };
        guest_gl_surface_scale_dst_ = SDL_FRect{
            static_cast<float>(dst_x),
            static_cast<float>(dst_y),
            static_cast<float>(dst_w),
            static_cast<float>(dst_h),
        };
        if (std::getenv("ZEEMU_TRACE_RENDER") != nullptr) {
            std::cout << "SDLFramePresenter: guest GL surface scale src="
                      << src_x << "," << src_y << " " << src_w << "x" << src_h
                      << " dst=" << dst_x << "," << dst_y << " " << dst_w << "x" << dst_h
                      << std::endl;
        }
    }

    void guest_gl_clear_color(float r, float g, float b, float a) override {
        clear_r_ = static_cast<Uint8>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        clear_g_ = static_cast<Uint8>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        clear_b_ = static_cast<Uint8>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        clear_a_ = static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    }

    void guest_gl_clear(uint32_t mask) override {
        if (!renderer_) return;
        if (mask & GL_DEPTH_BUFFER_BIT) {
            std::fill(depth_buffer_.begin(), depth_buffer_.end(), 1.0f);
        }
        if (mask & GL_COLOR_BUFFER_BIT) {
            pending_triangles_.clear();
            for (size_t i = 0; i + 3 < framebuffer_rgba_.size(); i += 4) {
                framebuffer_rgba_[i + 0] = clear_r_;
                framebuffer_rgba_[i + 1] = clear_g_;
                framebuffer_rgba_[i + 2] = clear_b_;
                framebuffer_rgba_[i + 3] = clear_a_;
            }
            cpu_framebuffer_valid_ = true;
            cpu_framebuffer_dirty_ = false;
            SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
            SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
            SDL_SetRenderDrawColor(renderer_, clear_r_, clear_g_, clear_b_, clear_a_);
            SDL_RenderClear(renderer_);
        }
    }

    void guest_gl_depth_state(bool enabled, uint32_t func, bool mask) override {
        depth_test_enabled_ = enabled;
        depth_func_ = func;
        depth_mask_ = mask;
    }

    void guest_gl_blend(bool enabled, uint32_t src, uint32_t dst) override {
        blend_enabled_ = enabled;
        blend_src_ = src;
        blend_dst_ = dst;
    }

    void guest_gl_alpha_test(bool enabled, uint32_t func, float ref) override {
        alpha_test_enabled_ = enabled;
        alpha_func_ = func;
        alpha_ref_ = ref;
    }

    void guest_gl_active_texture_unit(uint32_t unit) override {
        active_texture_unit_ = std::min<uint32_t>(unit, 1u);
        bound_texture_2d_ = bound_textures_2d_[active_texture_unit_];
        tex_env_ = tex_env_units_[active_texture_unit_];
    }

    void guest_gl_texture_2d_enabled(uint32_t unit, bool enabled) override {
        texture_2d_enabled_[std::min<uint32_t>(unit, 1u)] = enabled;
    }

    void guest_gl_tex_env(uint32_t pname, uint32_t value) override {
        TexEnvState& env = tex_env_units_[active_texture_unit_];
        switch (pname) {
            case GL_TEXTURE_ENV_MODE:
                env.mode = value;
                break;
            case GL_COMBINE_RGB:
                env.combine_rgb = value;
                break;
            case GL_COMBINE_ALPHA:
                env.combine_alpha = value;
                break;
            case GL_SRC0_RGB:
            case GL_SRC1_RGB:
            case GL_SRC2_RGB:
                env.src_rgb[(pname - GL_SRC0_RGB) & 0x3u] = value;
                break;
            case GL_SRC0_ALPHA:
            case GL_SRC1_ALPHA:
            case GL_SRC2_ALPHA:
                env.src_alpha[(pname - GL_SRC0_ALPHA) & 0x3u] = value;
                break;
            case GL_OPERAND0_RGB:
            case GL_OPERAND1_RGB:
            case GL_OPERAND2_RGB:
                env.operand_rgb[(pname - GL_OPERAND0_RGB) & 0x3u] = value;
                break;
            case GL_OPERAND0_ALPHA:
            case GL_OPERAND1_ALPHA:
            case GL_OPERAND2_ALPHA:
                env.operand_alpha[(pname - GL_OPERAND0_ALPHA) & 0x3u] = value;
                break;
            case GL_RGB_SCALE:
                env.rgb_scale = scale_from_tex_env_value(value);
                break;
            case GL_ALPHA_SCALE:
                env.alpha_scale = scale_from_tex_env_value(value);
                break;
            default:
                break;
        }
        tex_env_ = tex_env_units_[active_texture_unit_];
    }

    void guest_gl_tex_env_color(float r, float g, float b, float a) override {
        tex_env_units_[active_texture_unit_].color = {r, g, b, a};
        tex_env_ = tex_env_units_[active_texture_unit_];
    }

    void guest_gl_bind_texture(uint32_t target, uint32_t texture) override {
        if (target == GL_TEXTURE_2D) {
            bound_textures_2d_[active_texture_unit_] = texture;
            bound_texture_2d_ = texture;
        }
    }

    void guest_gl_delete_texture(uint32_t texture) override {
        auto it = guest_textures_.find(texture);
        if (it != guest_textures_.end()) {
            SDL_DestroyTexture(it->second.texture);
            guest_textures_.erase(it);
        }
        for (auto& bound : bound_textures_2d_) {
            if (bound == texture) {
                bound = 0;
            }
        }
        bound_texture_2d_ = bound_textures_2d_[active_texture_unit_];
    }

    void guest_gl_tex_parameter(uint32_t target, uint32_t pname, uint32_t value) override {
        if (target != GL_TEXTURE_2D || bound_texture_2d_ == 0) return;
        GuestTexture& guest_tex = guest_textures_[bound_texture_2d_];
        switch (pname) {
            case GL_TEXTURE_MIN_FILTER:
                guest_tex.min_filter = value;
                break;
            case GL_TEXTURE_MAG_FILTER:
                guest_tex.mag_filter = value;
                break;
            case GL_TEXTURE_WRAP_S:
                guest_tex.wrap_s = value;
                break;
            case GL_TEXTURE_WRAP_T:
                guest_tex.wrap_t = value;
                break;
            default:
                break;
        }
        if (guest_tex.texture) {
            SDL_SetTextureScaleMode(guest_tex.texture, sdl_scale_mode_for_texture(guest_tex));
        }
    }

    void guest_gl_tex_image_2d(uint32_t target, int level, int, int width, int height,
                               int, uint32_t format, uint32_t type, const void* pixels) override {
        if (!renderer_ || target != GL_TEXTURE_2D || bound_texture_2d_ == 0 || width <= 0 || height <= 0) {
            return;
        }
        if (level != 0) {
            // The SDL presenter currently samples only level 0. QXEngine uploads full
            // mip chains, so keep higher levels from replacing the base texture.
            return;
        }

        SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_UNKNOWN;
        int pitch = 0;
        // Some GLES 1.x texture formats do not map cleanly to a single SDL
        // format. Decode them to RGBA32 so SDL upload and software sampling use
        // the same fixed-function RGBA values.
        bool convert_rgba = false;
        if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
            pixel_format = SDL_PIXELFORMAT_RGB565;
            pitch = width * 2;
        } else if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
            pixel_format = SDL_PIXELFORMAT_RGBA32;
            pitch = width * 4;
        } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
            pixel_format = SDL_PIXELFORMAT_RGB24;
            pitch = width * 3;
        } else if ((format == GL_ALPHA || format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA) &&
                   type == GL_UNSIGNED_BYTE) {
            pixel_format = SDL_PIXELFORMAT_RGBA32;
            pitch = width * (format == GL_LUMINANCE_ALPHA ? 2 : 1);
            convert_rgba = true;
        } else if (format == GL_RGBA &&
                   (type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_4_4_4_4)) {
            // RGBA5551 / RGBA4444 (used e.g. by Crash Nitro Kart 3D track/object
            // textures). Decoded to RGBA32 below.
            pixel_format = SDL_PIXELFORMAT_RGBA32;
            pitch = width * 2; // raw source stride
            convert_rgba = true;
        }
        if (pixel_format == SDL_PIXELFORMAT_UNKNOWN) {
            return;
        }

        auto& guest_tex = guest_textures_[bound_texture_2d_];
        if (guest_tex.texture) {
            SDL_DestroyTexture(guest_tex.texture);
        }
        guest_tex.texture = SDL_CreateTexture(renderer_, pixel_format, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!guest_tex.texture) {
            guest_textures_.erase(bound_texture_2d_);
            return;
        }
        guest_tex.width = width;
        guest_tex.height = height;
        guest_tex.rgba_pixels.clear();
        guest_tex.color_pixels.clear();
        SDL_SetTextureScaleMode(guest_tex.texture, sdl_scale_mode_for_texture(guest_tex));
        SDL_SetTextureBlendMode(guest_tex.texture, SDL_BLENDMODE_BLEND);
        if (pixels) {
            const uint8_t* src = static_cast<const uint8_t*>(pixels);
            guest_tex.rgba_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
            guest_tex.color_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t out = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
                    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
                        const size_t in = out;
                        guest_tex.rgba_pixels[out + 0] = src[in + 0];
                        guest_tex.rgba_pixels[out + 1] = src[in + 1];
                        guest_tex.rgba_pixels[out + 2] = src[in + 2];
                        guest_tex.rgba_pixels[out + 3] = src[in + 3];
                    } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
                        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3u;
                        guest_tex.rgba_pixels[out + 0] = src[in + 0];
                        guest_tex.rgba_pixels[out + 1] = src[in + 1];
                        guest_tex.rgba_pixels[out + 2] = src[in + 2];
                        guest_tex.rgba_pixels[out + 3] = 0xff;
                    } else if (format == GL_ALPHA && type == GL_UNSIGNED_BYTE) {
                        const size_t in = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                        guest_tex.rgba_pixels[out + 0] = 0x00;
                        guest_tex.rgba_pixels[out + 1] = 0x00;
                        guest_tex.rgba_pixels[out + 2] = 0x00;
                        guest_tex.rgba_pixels[out + 3] = src[in];
                    } else if (format == GL_LUMINANCE && type == GL_UNSIGNED_BYTE) {
                        const size_t in = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                        guest_tex.rgba_pixels[out + 0] = src[in];
                        guest_tex.rgba_pixels[out + 1] = src[in];
                        guest_tex.rgba_pixels[out + 2] = src[in];
                        guest_tex.rgba_pixels[out + 3] = 0xff;
                    } else if (format == GL_LUMINANCE_ALPHA && type == GL_UNSIGNED_BYTE) {
                        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 2u;
                        guest_tex.rgba_pixels[out + 0] = src[in + 0];
                        guest_tex.rgba_pixels[out + 1] = src[in + 0];
                        guest_tex.rgba_pixels[out + 2] = src[in + 0];
                        guest_tex.rgba_pixels[out + 3] = src[in + 1];
                    } else if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
                        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 2u;
                        const uint16_t v = static_cast<uint16_t>(src[in + 0]) |
                                           static_cast<uint16_t>(src[in + 1] << 8);
                        guest_tex.rgba_pixels[out + 0] = static_cast<uint8_t>(((v >> 11) & 0x1f) * 255u / 31u);
                        guest_tex.rgba_pixels[out + 1] = static_cast<uint8_t>(((v >> 5) & 0x3f) * 255u / 63u);
                        guest_tex.rgba_pixels[out + 2] = static_cast<uint8_t>((v & 0x1f) * 255u / 31u);
                        guest_tex.rgba_pixels[out + 3] = 0xff;
                    } else if (format == GL_RGBA && type == GL_UNSIGNED_SHORT_5_5_5_1) {
                        // RGBA5551: bits [15:11]=R [10:6]=G [5:1]=B [0]=A.
                        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 2u;
                        const uint16_t v = static_cast<uint16_t>(src[in + 0]) |
                                           static_cast<uint16_t>(src[in + 1] << 8);
                        guest_tex.rgba_pixels[out + 0] = static_cast<uint8_t>(((v >> 11) & 0x1f) * 255u / 31u);
                        guest_tex.rgba_pixels[out + 1] = static_cast<uint8_t>(((v >> 6) & 0x1f) * 255u / 31u);
                        guest_tex.rgba_pixels[out + 2] = static_cast<uint8_t>(((v >> 1) & 0x1f) * 255u / 31u);
                        guest_tex.rgba_pixels[out + 3] = (v & 0x1u) ? 0xff : 0x00;
                    } else if (format == GL_RGBA && type == GL_UNSIGNED_SHORT_4_4_4_4) {
                        // RGBA4444: bits [15:12]=R [11:8]=G [7:4]=B [3:0]=A.
                        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 2u;
                        const uint16_t v = static_cast<uint16_t>(src[in + 0]) |
                                           static_cast<uint16_t>(src[in + 1] << 8);
                        guest_tex.rgba_pixels[out + 0] = static_cast<uint8_t>(((v >> 12) & 0xf) * 255u / 15u);
                        guest_tex.rgba_pixels[out + 1] = static_cast<uint8_t>(((v >> 8) & 0xf) * 255u / 15u);
                        guest_tex.rgba_pixels[out + 2] = static_cast<uint8_t>(((v >> 4) & 0xf) * 255u / 15u);
                        guest_tex.rgba_pixels[out + 3] = static_cast<uint8_t>((v & 0xf) * 255u / 15u);
                    }
                    constexpr float inv255 = 1.0f / 255.0f;
                    guest_tex.color_pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = {
                        static_cast<float>(guest_tex.rgba_pixels[out + 0]) * inv255,
                        static_cast<float>(guest_tex.rgba_pixels[out + 1]) * inv255,
                        static_cast<float>(guest_tex.rgba_pixels[out + 2]) * inv255,
                        static_cast<float>(guest_tex.rgba_pixels[out + 3]) * inv255,
                    };
                }
            }
            if (std::getenv("ZEEMU_TRACE_TEXTURE_UPLOADS")) {
                const uint8_t* p = src;
                printf("SDL texture upload id=%u size=%dx%d gl_format=0x%x gl_type=0x%x sdl_format=0x%x/%s pitch=%d first=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                       bound_texture_2d_,
                       width,
                       height,
                       format,
                       type,
                       static_cast<uint32_t>(pixel_format),
                       SDL_GetPixelFormatName(pixel_format),
                       pitch,
                       p[0], p[1], p[2], p[3],
                       p[4], p[5], p[6], p[7],
                       p[8], p[9], p[10], p[11],
                       p[12], p[13], p[14], p[15]);
            }
            // Converted formats decoded into rgba_pixels feed SDL as RGBA32;
            // native formats (565/RGB24/RGBA8) upload the raw guest pixels.
            if (convert_rgba) {
                SDL_UpdateTexture(guest_tex.texture, nullptr, guest_tex.rgba_pixels.data(), width * 4);
            } else {
                SDL_UpdateTexture(guest_tex.texture, nullptr, pixels, pitch);
            }
            // Debug: dump the decoded guest texture so the atlas/glyph pixels can
            // be inspected directly. RGB goes to a .ppm and alpha to a companion
            // .pgm, which separates "baked white edge texels" from a compositing
            // artifact when diagnosing text/atlas issues. Gated on an env var so
            // it never runs in normal smokes.
            if (std::getenv("ZEEMU_DUMP_GUEST_TEX") && !guest_tex.rgba_pixels.empty()) {
                char base[128];
                std::snprintf(base, sizeof(base), "logs/guesttex_%u_%dx%d",
                              bound_texture_2d_, width, height);
                const std::string rgb_path = std::string(base) + ".ppm";
                const std::string a_path = std::string(base) + "_a.pgm";
                if (FILE* fp = std::fopen(rgb_path.c_str(), "wb")) {
                    std::fprintf(fp, "P6\n%d %d\n255\n", width, height);
                    for (int i = 0; i < width * height; ++i) {
                        std::fputc(guest_tex.rgba_pixels[i * 4 + 0], fp);
                        std::fputc(guest_tex.rgba_pixels[i * 4 + 1], fp);
                        std::fputc(guest_tex.rgba_pixels[i * 4 + 2], fp);
                    }
                    std::fclose(fp);
                }
                if (FILE* fp = std::fopen(a_path.c_str(), "wb")) {
                    std::fprintf(fp, "P5\n%d %d\n255\n", width, height);
                    for (int i = 0; i < width * height; ++i) {
                        std::fputc(guest_tex.rgba_pixels[i * 4 + 3], fp);
                    }
                    std::fclose(fp);
                }
            }
        }
    }

    void guest_gl_draw_triangle(const GuestGLVertex2D& v0, const GuestGLVertex2D& v1,
                                const GuestGLVertex2D& v2) override {
        if (!renderer_ || !guest_gl_framebuffer_) return;
        PendingTriangle tri{};
        tri.v[0] = v0;
        tri.v[1] = v1;
        tri.v[2] = v2;
        tri.texture = bound_textures_2d_[0];
        tri.texture1 = bound_textures_2d_[1];
        tri.texture_enabled = texture_2d_enabled_[0];
        tri.texture1_enabled = texture_2d_enabled_[1];
        tri.blend_mode = sdl_blend_mode();
        tri.blend_enabled = blend_enabled_;
        tri.blend_src = blend_src_;
        tri.blend_dst = blend_dst_;
        tri.alpha_test_enabled = alpha_test_enabled_;
        tri.alpha_func = alpha_func_;
        tri.alpha_ref = alpha_ref_;
        if (tri.alpha_test_enabled && tri.alpha_func == GL_GREATER && tri.alpha_ref <= 0.0f &&
            !tri.blend_enabled) {
            // The SDL geometry path has no real alpha-test stage. For the common
            // GLES 1.x binary-mask case (Crash menu RGBA5551: alpha 0 or 1),
            // alpha blending is equivalent for zero-alpha rejection and keeps
            // the overlay on the SDL fast path instead of losing glyph quads to
            // the incomplete CPU raster path.
            tri.blend_mode = SDL_BLENDMODE_BLEND;
        }
        tri.depth_enabled = depth_test_enabled_;
        tri.depth_func = depth_func_;
        tri.depth_mask = depth_mask_;
        tri.tex_env = tex_env_units_[0];
        tri.tex_env1 = tex_env_units_[1];
        tri.tex_env_path = classify_tex_env(tri.tex_env);
        tri.tex_env1_path = classify_tex_env(tri.tex_env1);
        tri.cpu_raster_required = tri.tex_env.requires_cpu_raster() ||
                                  (tri.texture1_enabled && tri.texture1 != 0) ||
                                  tri.tex_env1.requires_cpu_raster();
        if (tri.tex_env_path == kTexEnvPathGeneric && std::getenv("ZEEMU_TRACE_RENDER_PROFILE") != nullptr) {
            static int generic_texenv_logs = 0;
            if (generic_texenv_logs < 12) {
                printf("  RenderProfile texenv-generic mode=0x%x combine=(0x%x,0x%x) src_rgb=(0x%x,0x%x,0x%x) op_rgb=(0x%x,0x%x,0x%x) src_a=(0x%x,0x%x,0x%x) op_a=(0x%x,0x%x,0x%x) scale=(%.1f,%.1f)\n",
                       tri.tex_env.mode,
                       tri.tex_env.combine_rgb,
                       tri.tex_env.combine_alpha,
                       tri.tex_env.src_rgb[0], tri.tex_env.src_rgb[1], tri.tex_env.src_rgb[2],
                       tri.tex_env.operand_rgb[0], tri.tex_env.operand_rgb[1], tri.tex_env.operand_rgb[2],
                       tri.tex_env.src_alpha[0], tri.tex_env.src_alpha[1], tri.tex_env.src_alpha[2],
                       tri.tex_env.operand_alpha[0], tri.tex_env.operand_alpha[1], tri.tex_env.operand_alpha[2],
                       tri.tex_env.rgb_scale,
                       tri.tex_env.alpha_scale);
                ++generic_texenv_logs;
            }
        }
        pending_triangles_.push_back(tri);
    }

    void guest_gl_swap_behavior_preserved(bool preserved) override {
        guest_gl_preserve_swap_buffer_ = preserved;
    }

    void guest_gl_swap_buffers() override {
        if (!renderer_ || !guest_gl_framebuffer_) return;
        guest_gl_presented_since_host_frame_ = true;
        guest_gl_frame_active_ = true;
        flush_guest_gl_triangles();
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        const SDL_FRect* src = guest_gl_surface_scale_enabled_ ? &guest_gl_surface_scale_src_ : nullptr;
        const SDL_FRect* dst = guest_gl_surface_scale_enabled_ ? &guest_gl_surface_scale_dst_ : nullptr;
        if (!SDL_RenderTexture(renderer_, guest_gl_framebuffer_, src, dst)) {
            std::cerr << "SDL_RenderTexture guest GL framebuffer failed: " << SDL_GetError() << std::endl;
        }
        maybe_dump_guest_gl_frame(last_flush_queued_triangles_ > 0);
        SDL_RenderPresent(renderer_);
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
        if (!guest_gl_preserve_swap_buffer_) {
            pending_triangles_.clear();
            std::fill(depth_buffer_.begin(), depth_buffer_.end(), 1.0f);
            std::fill(framebuffer_rgba_.begin(), framebuffer_rgba_.end(), 0);
            cpu_framebuffer_valid_ = true;
            cpu_framebuffer_dirty_ = false;
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderClear(renderer_);
        }
    }

    bool consume_guest_gl_presented() override {
        const bool presented = guest_gl_presented_since_host_frame_;
        guest_gl_presented_since_host_frame_ = false;
        suppress_host_bitmap_present_ = false;
        return presented;
    }

    bool has_guest_gl_frame() const override {
        return guest_gl_frame_active_;
    }
    
    // I3D state methods
    void guest_gl_light(uint32_t light, uint32_t pname, float r, float g, float b, float a, float x, float y, float z) override {
        // Store light state for future rendering.
        // light = GL_LIGHT0 (0x4000) + n; OpenGL ES 1.x guarantees 8 lights.
        uint32_t index = light - 0x4000;
        if (index >= 8) {
            return;
        }
        if (index >= i3d_lights_.size()) {
            i3d_lights_.resize(index + 1);
        }
        auto& l = i3d_lights_[index];
        if (pname == 0x1203) { // GL_POSITION
            l.pos_x = x; l.pos_y = y; l.pos_z = z; l.pos_w = a;
        } else if (pname == 0x1200) { // GL_AMBIENT
            l.ambient_r = r; l.ambient_g = g; l.ambient_b = b; l.ambient_a = a;
        } else if (pname == 0x1201) { // GL_DIFFUSE
            l.diffuse_r = r; l.diffuse_g = g; l.diffuse_b = b; l.diffuse_a = a;
        } else if (pname == 0x1202) { // GL_SPECULAR
            l.specular_r = r; l.specular_g = g; l.specular_b = b; l.specular_a = a;
        }
    }
    
    void guest_gl_material(uint32_t pname, float r, float g, float b, float a, float shininess) override {
        // Store material state
        if (pname == 0x1200) { // GL_AMBIENT
            i3d_material_.ambient_r = r; i3d_material_.ambient_g = g; i3d_material_.ambient_b = b; i3d_material_.ambient_a = a;
        } else if (pname == 0x1201) { // GL_DIFFUSE
            i3d_material_.diffuse_r = r; i3d_material_.diffuse_g = g; i3d_material_.diffuse_b = b; i3d_material_.diffuse_a = a;
        } else if (pname == 0x1202) { // GL_SPECULAR
            i3d_material_.specular_r = r; i3d_material_.specular_g = g; i3d_material_.specular_b = b; i3d_material_.specular_a = a;
        } else if (pname == 0x1601) { // GL_SHININESS
            i3d_material_.shininess = shininess;
        } else if (pname == 0x1600) { // GL_EMISSION
            i3d_material_.emission_r = r; i3d_material_.emission_g = g; i3d_material_.emission_b = b; i3d_material_.emission_a = a;
        }
    }
    
    void guest_gl_shade_model(uint32_t mode) override {
        i3d_shade_model_ = mode; // GL_FLAT=0x1D00, GL_SMOOTH=0x1D01
    }
    
    void guest_gl_cull_face(uint32_t mode) override {
        i3d_cull_face_ = mode; // GL_FRONT=0x0404, GL_BACK=0x0405, GL_FRONT_AND_BACK=0x0408
        i3d_cull_face_enabled_ = true;
    }
    
    void guest_gl_enable_disable(uint32_t cap, bool enabled) override {
        switch (cap) {
            case 0x0B44: i3d_cull_face_enabled_ = enabled; break; // GL_CULL_FACE
            case 0x0B71: i3d_depth_test_ = enabled; break; // GL_DEPTH_TEST
            case 0x0BE2: i3d_blend_enabled_ = enabled; break; // GL_BLEND
            case 0x0B50: i3d_lighting_enabled_ = enabled; break; // GL_LIGHTING
            case 0x0B57: i3d_color_material_ = enabled; break; // GL_COLOR_MATERIAL
            default: break;
        }
    }

private:
    struct ColorF {
        float r;
        float g;
        float b;
        float a;
    };

    struct GuestTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        uint32_t min_filter = GL_LINEAR;
        uint32_t mag_filter = GL_LINEAR;
        uint32_t wrap_s = GL_REPEAT;
        uint32_t wrap_t = GL_REPEAT;
        std::vector<uint8_t> rgba_pixels;
        std::vector<ColorF> color_pixels;
    };

    struct TexEnvState {
        uint32_t mode = GL_MODULATE;
        std::array<float, 4> color{0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t combine_rgb = GL_MODULATE;
        uint32_t combine_alpha = GL_MODULATE;
        uint32_t src_rgb[3] = {GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT};
        uint32_t src_alpha[3] = {GL_TEXTURE, GL_PREVIOUS, GL_CONSTANT};
        uint32_t operand_rgb[3] = {GL_SRC_COLOR, GL_SRC_COLOR, GL_SRC_ALPHA};
        uint32_t operand_alpha[3] = {GL_SRC_ALPHA, GL_SRC_ALPHA, GL_SRC_ALPHA};
        float rgb_scale = 1.0f;
        float alpha_scale = 1.0f;

        bool requires_cpu_raster() const {
            return mode == GL_COMBINE || mode == GL_ADD || mode == GL_ADD_SIGNED ||
                   mode == GL_SUBTRACT || mode == GL_INTERPOLATE ||
                   mode == GL_DOT3_RGB || mode == GL_DOT3_RGBA ||
                   mode == GL_BLEND_ENV || mode == GL_DECAL;
        }
    };

    static constexpr uint8_t kTexEnvPathModulate = 0;
    static constexpr uint8_t kTexEnvPathReplace = 1;
    static constexpr uint8_t kTexEnvPathAddSigned = 2;
    static constexpr uint8_t kTexEnvPathDot3 = 3;
    static constexpr uint8_t kTexEnvPathAddSignedPrimary = 4;
    static constexpr uint8_t kTexEnvPathGeneric = 5;

    static uint8_t classify_tex_env(const TexEnvState& env) {
        if (env.mode == GL_MODULATE) {
            return kTexEnvPathModulate;
        }
        if (env.mode == GL_REPLACE) {
            return kTexEnvPathReplace;
        }
        const bool texture_previous_rgb =
            env.src_rgb[0] == GL_TEXTURE &&
            (env.src_rgb[1] == GL_PREVIOUS || env.src_rgb[1] == GL_PRIMARY_COLOR) &&
            env.operand_rgb[0] == GL_SRC_COLOR &&
            env.operand_rgb[1] == GL_SRC_COLOR;
        const bool texture_previous_alpha =
            env.src_alpha[0] == GL_TEXTURE &&
            (env.src_alpha[1] == GL_PREVIOUS || env.src_alpha[1] == GL_PRIMARY_COLOR) &&
            (env.operand_alpha[0] == GL_SRC_ALPHA || env.operand_alpha[0] == GL_SRC_COLOR) &&
            (env.operand_alpha[1] == GL_SRC_ALPHA || env.operand_alpha[1] == GL_SRC_COLOR);
        if (env.mode == GL_COMBINE && env.combine_rgb == GL_MODULATE &&
            env.combine_alpha == GL_MODULATE && texture_previous_rgb && texture_previous_alpha) {
            return kTexEnvPathModulate;
        }
        if (env.mode == GL_COMBINE && env.combine_rgb == GL_ADD_SIGNED &&
            (env.combine_alpha == GL_MODULATE || env.combine_alpha == GL_REPLACE) &&
            texture_previous_rgb) {
            return kTexEnvPathAddSigned;
        }
        const bool previous_previous_rgb =
            (env.src_rgb[0] == GL_PREVIOUS || env.src_rgb[0] == GL_PRIMARY_COLOR) &&
            (env.src_rgb[1] == GL_PREVIOUS || env.src_rgb[1] == GL_PRIMARY_COLOR) &&
            env.operand_rgb[0] == GL_SRC_COLOR &&
            env.operand_rgb[1] == GL_SRC_COLOR;
        if (env.mode == GL_COMBINE && env.combine_rgb == GL_ADD_SIGNED &&
            env.combine_alpha == GL_REPLACE &&
            previous_previous_rgb &&
            env.src_alpha[0] == GL_TEXTURE &&
            (env.operand_alpha[0] == GL_SRC_ALPHA || env.operand_alpha[0] == GL_SRC_COLOR)) {
            return kTexEnvPathAddSignedPrimary;
        }
        if (env.mode == GL_COMBINE && (env.combine_rgb == GL_DOT3_RGB || env.combine_rgb == GL_DOT3_RGBA) &&
            texture_previous_rgb) {
            return kTexEnvPathDot3;
        }
        return kTexEnvPathGeneric;
    }

    struct PendingTriangle {
        GuestGLVertex2D v[3];
        uint32_t texture = 0;
        uint32_t texture1 = 0;
        bool texture_enabled = false;
        bool texture1_enabled = false;
        SDL_BlendMode blend_mode = SDL_BLENDMODE_NONE;
        bool blend_enabled = false;
        uint32_t blend_src = GL_ONE;
        uint32_t blend_dst = GL_ZERO;
        bool alpha_test_enabled = false;
        uint32_t alpha_func = GL_ALWAYS;
        float alpha_ref = 0.0f;
        bool depth_enabled = false;
        uint32_t depth_func = GL_LESS;
        bool depth_mask = true;
        TexEnvState tex_env;
        TexEnvState tex_env1;
        uint8_t tex_env_path = kTexEnvPathModulate;
        uint8_t tex_env1_path = kTexEnvPathModulate;
        bool cpu_raster_required = false;
    };

    static float scale_from_tex_env_value(uint32_t value) {
        if (value == 2 || value == 4) {
            return static_cast<float>(value);
        }
        float f = 0.0f;
        std::memcpy(&f, &value, sizeof(f));
        if (std::isfinite(f) && (std::fabs(f - 1.0f) < 0.001f ||
                                std::fabs(f - 2.0f) < 0.001f ||
                                std::fabs(f - 4.0f) < 0.001f)) {
            return f;
        }
        const float fixed = static_cast<float>(static_cast<int32_t>(value)) / 65536.0f;
        if (std::fabs(fixed - 1.0f) < 0.001f ||
            std::fabs(fixed - 2.0f) < 0.001f ||
            std::fabs(fixed - 4.0f) < 0.001f) {
            return fixed;
        }
        return 1.0f;
    }

    static float apply_texture_wrap(float value, uint32_t wrap) {
        if (wrap == GL_CLAMP_TO_EDGE) {
            return std::clamp(value, 0.0f, 1.0f);
        }
        // SDL_RenderGeometry auto-selects WRAP when normalized UVs leave 0..1.
        return value;
    }

    static float wrap_sample_coord(float value, uint32_t wrap) {
        if (wrap == GL_CLAMP_TO_EDGE) {
            return std::clamp(value, 0.0f, 1.0f);
        }
        value = value - std::floor(value);
        return value < 0.0f ? value + 1.0f : value;
    }

    static bool texture_filter_uses_linear(uint32_t filter) {
        switch (filter) {
            case GL_LINEAR:
            case GL_LINEAR_MIPMAP_NEAREST:
            case GL_LINEAR_MIPMAP_LINEAR:
                return true;
            case GL_NEAREST:
            case GL_NEAREST_MIPMAP_NEAREST:
            case GL_NEAREST_MIPMAP_LINEAR:
                return false;
            default:
                return true;
        }
    }

    static SDL_ScaleMode sdl_scale_mode_for_texture(const GuestTexture& texture) {
        return (texture_filter_uses_linear(texture.mag_filter) || texture_filter_uses_linear(texture.min_filter))
            ? SDL_SCALEMODE_LINEAR
            : SDL_SCALEMODE_NEAREST;
    }

    static uint8_t unit_to_u8(float value) {
        return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }

    static std::array<float, 4> texel_rgba(const GuestTexture& texture, int x, int y) {
        x = std::clamp(x, 0, texture.width - 1);
        y = std::clamp(y, 0, texture.height - 1);
        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(texture.width) + static_cast<size_t>(x)) * 4u;
        return {
            static_cast<float>(texture.rgba_pixels[in + 0]) / 255.0f,
            static_cast<float>(texture.rgba_pixels[in + 1]) / 255.0f,
            static_cast<float>(texture.rgba_pixels[in + 2]) / 255.0f,
            static_cast<float>(texture.rgba_pixels[in + 3]) / 255.0f,
        };
    }

    static ColorF texel_color(const GuestTexture& texture, int x, int y) {
        x = std::clamp(x, 0, texture.width - 1);
        y = std::clamp(y, 0, texture.height - 1);
        if (!texture.color_pixels.empty()) {
            return texture.color_pixels[static_cast<size_t>(y) * static_cast<size_t>(texture.width) + static_cast<size_t>(x)];
        }
        const size_t in = (static_cast<size_t>(y) * static_cast<size_t>(texture.width) + static_cast<size_t>(x)) * 4u;
        constexpr float inv255 = 1.0f / 255.0f;
        return {
            static_cast<float>(texture.rgba_pixels[in + 0]) * inv255,
            static_cast<float>(texture.rgba_pixels[in + 1]) * inv255,
            static_cast<float>(texture.rgba_pixels[in + 2]) * inv255,
            static_cast<float>(texture.rgba_pixels[in + 3]) * inv255,
        };
    }

    static ColorF lerp_color(const ColorF& a, const ColorF& b, float t) {
        return {
            a.r * (1.0f - t) + b.r * t,
            a.g * (1.0f - t) + b.g * t,
            a.b * (1.0f - t) + b.b * t,
            a.a * (1.0f - t) + b.a * t,
        };
    }

    static bool alpha_test_passes(const PendingTriangle& tri, float alpha) {
        if (!tri.alpha_test_enabled) {
            return true;
        }
        switch (tri.alpha_func) {
            case GL_NEVER: return false;
            case GL_LESS: return alpha < tri.alpha_ref;
            case GL_EQUAL: return alpha == tri.alpha_ref;
            case GL_LEQUAL: return alpha <= tri.alpha_ref;
            case GL_GREATER: return alpha > tri.alpha_ref;
            case GL_NOTEQUAL: return alpha != tri.alpha_ref;
            case GL_GEQUAL: return alpha >= tri.alpha_ref;
            case GL_ALWAYS: return true;
            default: return true;
        }
    }

    static bool depth_test_passes(uint32_t func, float incoming, float stored) {
        switch (func) {
            case GL_NEVER: return false;
            case GL_LESS: return incoming < stored;
            case GL_EQUAL: return incoming == stored;
            case GL_LEQUAL: return incoming <= stored;
            case GL_GREATER: return incoming > stored;
            case GL_NOTEQUAL: return incoming != stored;
            case GL_GEQUAL: return incoming >= stored;
            case GL_ALWAYS: return true;
            default: return incoming < stored;
        }
    }

    ColorF sample_texture_color(const GuestTexture* texture, float u, float v) const {
        if (!texture || texture->width <= 0 || texture->height <= 0 ||
            (texture->rgba_pixels.empty() && texture->color_pixels.empty())) {
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
        u = wrap_sample_coord(u, texture->wrap_s);
        v = wrap_sample_coord(v, texture->wrap_t);
        if (texture_filter_uses_linear(texture->mag_filter)) {
            const float sx = u * static_cast<float>(texture->width) - 0.5f;
            const float sy = v * static_cast<float>(texture->height) - 0.5f;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const float fx = sx - static_cast<float>(x0);
            const float fy = sy - static_cast<float>(y0);
            const ColorF top = lerp_color(texel_color(*texture, x0, y0), texel_color(*texture, x0 + 1, y0), fx);
            const ColorF bottom = lerp_color(texel_color(*texture, x0, y0 + 1), texel_color(*texture, x0 + 1, y0 + 1), fx);
            return lerp_color(top, bottom, fy);
        }
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(texture->width)), 0, texture->width - 1);
        const int y = std::clamp(static_cast<int>(v * static_cast<float>(texture->height)), 0, texture->height - 1);
        return texel_color(*texture, x, y);
    }

    std::array<float, 4> sample_texture(const GuestTexture* texture, float u, float v) const {
        if (!texture || texture->width <= 0 || texture->height <= 0 || texture->rgba_pixels.empty()) {
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
        u = wrap_sample_coord(u, texture->wrap_s);
        v = wrap_sample_coord(v, texture->wrap_t);
        if (texture_filter_uses_linear(texture->mag_filter)) {
            const float sx = u * static_cast<float>(texture->width) - 0.5f;
            const float sy = v * static_cast<float>(texture->height) - 0.5f;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const float fx = sx - static_cast<float>(x0);
            const float fy = sy - static_cast<float>(y0);
            const auto c00 = texel_rgba(*texture, x0, y0);
            const auto c10 = texel_rgba(*texture, x0 + 1, y0);
            const auto c01 = texel_rgba(*texture, x0, y0 + 1);
            const auto c11 = texel_rgba(*texture, x0 + 1, y0 + 1);
            std::array<float, 4> out{};
            for (size_t i = 0; i < out.size(); ++i) {
                const float top = c00[i] * (1.0f - fx) + c10[i] * fx;
                const float bottom = c01[i] * (1.0f - fx) + c11[i] * fx;
                out[i] = top * (1.0f - fy) + bottom * fy;
            }
            return out;
        }
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(texture->width)), 0, texture->width - 1);
        const int y = std::clamp(static_cast<int>(v * static_cast<float>(texture->height)), 0, texture->height - 1);
        return texel_rgba(*texture, x, y);
    }

    static std::array<float, 4> tex_env_source(uint32_t source,
                                               const std::array<float, 4>& texture,
                                               const std::array<float, 4>& primary,
                                               const TexEnvState& env) {
        switch (source) {
            case GL_TEXTURE:
                return texture;
            case GL_CONSTANT:
                return env.color;
            case GL_PRIMARY_COLOR:
            case GL_PREVIOUS:
            default:
                return primary;
        }
    }

    static std::array<float, 3> apply_rgb_operand(const std::array<float, 4>& src, uint32_t operand) {
        switch (operand) {
            case GL_ONE_MINUS_SRC_COLOR:
                return {1.0f - src[0], 1.0f - src[1], 1.0f - src[2]};
            case GL_SRC_ALPHA:
                return {src[3], src[3], src[3]};
            case GL_ONE_MINUS_SRC_ALPHA:
                return {1.0f - src[3], 1.0f - src[3], 1.0f - src[3]};
            case GL_SRC_COLOR:
            default:
                return {src[0], src[1], src[2]};
        }
    }

    static float apply_alpha_operand(const std::array<float, 4>& src, uint32_t operand) {
        switch (operand) {
            case GL_ONE_MINUS_SRC_ALPHA:
                return 1.0f - src[3];
            case GL_SRC_ALPHA:
            default:
                return src[3];
        }
    }

    static std::array<float, 4> apply_tex_env_state(const TexEnvState& env,
                                                    const std::array<float, 4>& texture,
                                                    const std::array<float, 4>& primary) {
        std::array<float, 4> out = primary;
        switch (env.mode) {
            case GL_REPLACE:
                out = texture;
                break;
            case GL_DECAL:
                out[0] = primary[0] * (1.0f - texture[3]) + texture[0] * texture[3];
                out[1] = primary[1] * (1.0f - texture[3]) + texture[1] * texture[3];
                out[2] = primary[2] * (1.0f - texture[3]) + texture[2] * texture[3];
                out[3] = primary[3];
                break;
            case GL_BLEND_ENV:
                out[0] = primary[0] * (1.0f - texture[0]) + env.color[0] * texture[0];
                out[1] = primary[1] * (1.0f - texture[1]) + env.color[1] * texture[1];
                out[2] = primary[2] * (1.0f - texture[2]) + env.color[2] * texture[2];
                out[3] = primary[3] * texture[3];
                break;
            case GL_ADD:
                out[0] = primary[0] + texture[0];
                out[1] = primary[1] + texture[1];
                out[2] = primary[2] + texture[2];
                out[3] = primary[3] * texture[3];
                break;
            case GL_COMBINE: {
                const auto s0_rgb = apply_rgb_operand(tex_env_source(env.src_rgb[0], texture, primary, env), env.operand_rgb[0]);
                const auto s1_rgb = apply_rgb_operand(tex_env_source(env.src_rgb[1], texture, primary, env), env.operand_rgb[1]);
                const auto s2_rgb = apply_rgb_operand(tex_env_source(env.src_rgb[2], texture, primary, env), env.operand_rgb[2]);
                switch (env.combine_rgb) {
                    case GL_REPLACE:
                        out[0] = s0_rgb[0]; out[1] = s0_rgb[1]; out[2] = s0_rgb[2];
                        break;
                    case GL_ADD:
                        out[0] = s0_rgb[0] + s1_rgb[0]; out[1] = s0_rgb[1] + s1_rgb[1]; out[2] = s0_rgb[2] + s1_rgb[2];
                        break;
                    case GL_ADD_SIGNED:
                        out[0] = s0_rgb[0] + s1_rgb[0] - 0.5f; out[1] = s0_rgb[1] + s1_rgb[1] - 0.5f; out[2] = s0_rgb[2] + s1_rgb[2] - 0.5f;
                        break;
                    case GL_SUBTRACT:
                        out[0] = s0_rgb[0] - s1_rgb[0]; out[1] = s0_rgb[1] - s1_rgb[1]; out[2] = s0_rgb[2] - s1_rgb[2];
                        break;
                    case GL_INTERPOLATE:
                        out[0] = s0_rgb[0] * s2_rgb[0] + s1_rgb[0] * (1.0f - s2_rgb[0]);
                        out[1] = s0_rgb[1] * s2_rgb[1] + s1_rgb[1] * (1.0f - s2_rgb[1]);
                        out[2] = s0_rgb[2] * s2_rgb[2] + s1_rgb[2] * (1.0f - s2_rgb[2]);
                        break;
                    case GL_DOT3_RGB:
                    case GL_DOT3_RGBA: {
                        const float dot = 4.0f * ((s0_rgb[0] - 0.5f) * (s1_rgb[0] - 0.5f) +
                                                  (s0_rgb[1] - 0.5f) * (s1_rgb[1] - 0.5f) +
                                                  (s0_rgb[2] - 0.5f) * (s1_rgb[2] - 0.5f));
                        out[0] = out[1] = out[2] = dot;
                        break;
                    }
                    case GL_MODULATE:
                    default:
                        out[0] = s0_rgb[0] * s1_rgb[0]; out[1] = s0_rgb[1] * s1_rgb[1]; out[2] = s0_rgb[2] * s1_rgb[2];
                        break;
                }

                const float s0_a = apply_alpha_operand(tex_env_source(env.src_alpha[0], texture, primary, env), env.operand_alpha[0]);
                const float s1_a = apply_alpha_operand(tex_env_source(env.src_alpha[1], texture, primary, env), env.operand_alpha[1]);
                const float s2_a = apply_alpha_operand(tex_env_source(env.src_alpha[2], texture, primary, env), env.operand_alpha[2]);
                switch (env.combine_alpha) {
                    case GL_REPLACE:
                        out[3] = s0_a;
                        break;
                    case GL_ADD:
                        out[3] = s0_a + s1_a;
                        break;
                    case GL_ADD_SIGNED:
                        out[3] = s0_a + s1_a - 0.5f;
                        break;
                    case GL_SUBTRACT:
                        out[3] = s0_a - s1_a;
                        break;
                    case GL_INTERPOLATE:
                        out[3] = s0_a * s2_a + s1_a * (1.0f - s2_a);
                        break;
                    case GL_MODULATE:
                    default:
                        out[3] = s0_a * s1_a;
                        break;
                }
                out[0] *= env.rgb_scale;
                out[1] *= env.rgb_scale;
                out[2] *= env.rgb_scale;
                out[3] *= env.alpha_scale;
                break;
            }
            case GL_MODULATE:
            default:
                out[0] = primary[0] * texture[0];
                out[1] = primary[1] * texture[1];
                out[2] = primary[2] * texture[2];
                out[3] = primary[3] * texture[3];
                break;
        }
        for (float& component : out) {
            component = std::clamp(component, 0.0f, 1.0f);
        }
        return out;
    }

    static std::array<float, 4> apply_tex_env(const PendingTriangle& tri,
                                              const std::array<float, 4>& texture,
                                              const std::array<float, 4>& primary) {
        return apply_tex_env_state(tri.tex_env, texture, primary);
    }

    static float blend_factor(uint32_t factor,
                              const std::array<float, 4>& src,
                              const std::array<float, 4>& dst,
                              int channel) {
        switch (factor) {
            case GL_ZERO:
                return 0.0f;
            case GL_ONE:
                return 1.0f;
            case GL_SRC_COLOR:
                return src[static_cast<size_t>(channel)];
            case GL_ONE_MINUS_SRC_COLOR:
                return 1.0f - src[static_cast<size_t>(channel)];
            case GL_DST_COLOR:
                return dst[static_cast<size_t>(channel)];
            case GL_ONE_MINUS_DST_COLOR:
                return 1.0f - dst[static_cast<size_t>(channel)];
            case GL_SRC_ALPHA:
                return src[3];
            case GL_ONE_MINUS_SRC_ALPHA:
                return 1.0f - src[3];
            case GL_DST_ALPHA:
                return dst[3];
            case GL_ONE_MINUS_DST_ALPHA:
                return 1.0f - dst[3];
            case GL_SRC_ALPHA_SATURATE:
                return channel == 3 ? 1.0f : std::min(src[3], 1.0f - dst[3]);
            default:
                return 1.0f;
        }
    }

    void blend_pixel(size_t pixel, const ColorF& src, const PendingTriangle& tri) {
        const size_t out = pixel * 4u;
        const std::array<float, 4> src_array = {src.r, src.g, src.b, src.a};
        std::array<float, 4> dst = {
            static_cast<float>(framebuffer_rgba_[out + 0]) / 255.0f,
            static_cast<float>(framebuffer_rgba_[out + 1]) / 255.0f,
            static_cast<float>(framebuffer_rgba_[out + 2]) / 255.0f,
            static_cast<float>(framebuffer_rgba_[out + 3]) / 255.0f,
        };
        for (int i = 0; i < 4; ++i) {
            const float sf = blend_factor(tri.blend_src, src_array, dst, i);
            const float df = blend_factor(tri.blend_dst, src_array, dst, i);
            const float value = src_array[static_cast<size_t>(i)] * sf + dst[static_cast<size_t>(i)] * df;
            framebuffer_rgba_[out + static_cast<size_t>(i)] = unit_to_u8(value);
        }
    }

    void upload_cpu_framebuffer_to_target() {
        if (!cpu_framebuffer_dirty_ || !guest_gl_cpu_texture_ || framebuffer_rgba_.empty()) {
            return;
        }
        SDL_UpdateTexture(guest_gl_cpu_texture_, nullptr, framebuffer_rgba_.data(), logical_width_ * 4);
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_RenderTexture(renderer_, guest_gl_cpu_texture_, nullptr, nullptr);
        SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
        cpu_framebuffer_dirty_ = false;
        cpu_framebuffer_valid_ = true;
    }

    void sync_cpu_framebuffer_from_target() {
        if (cpu_framebuffer_valid_) {
            return;
        }
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_Surface* surface = SDL_RenderReadPixels(renderer_, nullptr);
        if (!surface) {
            return;
        }
        if (surface->w == logical_width_ && surface->h == logical_height_) {
            framebuffer_rgba_.assign(static_cast<size_t>(logical_width_) * static_cast<size_t>(logical_height_) * 4u, 0);
            for (int y = 0; y < surface->h; ++y) {
                for (int x = 0; x < surface->w; ++x) {
                    Uint8 r = 0;
                    Uint8 g = 0;
                    Uint8 b = 0;
                    Uint8 a = 0;
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
                    const size_t out = (static_cast<size_t>(y) * static_cast<size_t>(logical_width_) + static_cast<size_t>(x)) * 4u;
                    framebuffer_rgba_[out + 0] = r;
                    framebuffer_rgba_[out + 1] = g;
                    framebuffer_rgba_[out + 2] = b;
                    framebuffer_rgba_[out + 3] = a;
                }
            }
            cpu_framebuffer_valid_ = true;
        }
        SDL_DestroySurface(surface);
    }

    size_t rasterize_depth_triangle(const PendingTriangle& tri) {
        if (logical_width_ <= 0 || logical_height_ <= 0 || framebuffer_rgba_.empty() || depth_buffer_.empty()) {
            return 0;
        }
        sync_cpu_framebuffer_from_target();
        const auto edge = [](const GuestGLVertex2D& a, const GuestGLVertex2D& b, float x, float y) {
            return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
        };
        const float area = edge(tri.v[0], tri.v[1], tri.v[2].x, tri.v[2].y);
        if (std::fabs(area) < 0.000001f) {
            return 0;
        }

        const int min_x = std::clamp(static_cast<int>(std::floor(std::min({tri.v[0].x, tri.v[1].x, tri.v[2].x}))), 0, logical_width_ - 1);
        const int max_x = std::clamp(static_cast<int>(std::ceil(std::max({tri.v[0].x, tri.v[1].x, tri.v[2].x}))), 0, logical_width_ - 1);
        const int min_y = std::clamp(static_cast<int>(std::floor(std::min({tri.v[0].y, tri.v[1].y, tri.v[2].y}))), 0, logical_height_ - 1);
        const int max_y = std::clamp(static_cast<int>(std::ceil(std::max({tri.v[0].y, tri.v[1].y, tri.v[2].y}))), 0, logical_height_ - 1);
        if (min_x > max_x || min_y > max_y) {
            return 0;
        }

        const GuestTexture* texture = nullptr;
        auto it = guest_textures_.find(tri.texture);
        if (tri.texture_enabled && it != guest_textures_.end()) {
            texture = &it->second;
        }
        const GuestTexture* texture1 = nullptr;
        auto it1 = guest_textures_.find(tri.texture1);
        if (tri.texture1_enabled && it1 != guest_textures_.end()) {
            texture1 = &it1->second;
        }
        const TexEnvState& env = tri.tex_env;

        const float e0_dx = tri.v[2].y - tri.v[1].y;
        const float e0_dy = -(tri.v[2].x - tri.v[1].x);
        const float e1_dx = tri.v[0].y - tri.v[2].y;
        const float e1_dy = -(tri.v[0].x - tri.v[2].x);
        const float e2_dx = tri.v[1].y - tri.v[0].y;
        const float e2_dy = -(tri.v[1].x - tri.v[0].x);
        const float start_x = static_cast<float>(min_x) + 0.5f;
        const float start_y = static_cast<float>(min_y) + 0.5f;
        const float e0_row_start = edge(tri.v[1], tri.v[2], start_x, start_y);
        const float e1_row_start = edge(tri.v[2], tri.v[0], start_x, start_y);
        const float e2_row_start = edge(tri.v[0], tri.v[1], start_x, start_y);
        const float inv_w0 = tri.v[0].inv_w;
        const float inv_w1 = tri.v[1].inv_w;
        const float inv_w2 = tri.v[2].inv_w;
        const float u0 = tri.v[0].u * inv_w0;
        const float u1 = tri.v[1].u * inv_w1;
        const float u2 = tri.v[2].u * inv_w2;
        const float v0 = tri.v[0].v * inv_w0;
        const float v1 = tri.v[1].v * inv_w1;
        const float v2 = tri.v[2].v * inv_w2;
        const float u10 = tri.v[0].u1 * inv_w0;
        const float u11 = tri.v[1].u1 * inv_w1;
        const float u12 = tri.v[2].u1 * inv_w2;
        const float v10 = tri.v[0].v1 * inv_w0;
        const float v11 = tri.v[1].v1 * inv_w1;
        const float v12 = tri.v[2].v1 * inv_w2;
        const float r0 = tri.v[0].r * inv_w0;
        const float r1 = tri.v[1].r * inv_w1;
        const float r2 = tri.v[2].r * inv_w2;
        const float g0 = tri.v[0].g * inv_w0;
        const float g1 = tri.v[1].g * inv_w1;
        const float g2 = tri.v[2].g * inv_w2;
        const float b0 = tri.v[0].b * inv_w0;
        const float b1 = tri.v[1].b * inv_w1;
        const float b2 = tri.v[2].b * inv_w2;
        const float a0 = tri.v[0].a * inv_w0;
        const float a1 = tri.v[1].a * inv_w1;
        const float a2 = tri.v[2].a * inv_w2;
        const float inv_area = 1.0f / area;
        size_t written_pixels = 0;
        for (int y = min_y; y <= max_y; ++y) {
            float e0 = e0_row_start + static_cast<float>(y - min_y) * e0_dy;
            float e1 = e1_row_start + static_cast<float>(y - min_y) * e1_dy;
            float e2 = e2_row_start + static_cast<float>(y - min_y) * e2_dy;
            for (int x = min_x; x <= max_x; ++x) {
                const float w0 = e0 * inv_area;
                const float w1 = e1 * inv_area;
                const float w2 = e2 * inv_area;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    continue;
                }
                const float ndc_z = w0 * tri.v[0].z + w1 * tri.v[1].z + w2 * tri.v[2].z;
                const float z = std::clamp(ndc_z * 0.5f + 0.5f, 0.0f, 1.0f);
                const size_t pixel = static_cast<size_t>(y) * static_cast<size_t>(logical_width_) + static_cast<size_t>(x);
                if (!depth_test_passes(tri.depth_func, z, depth_buffer_[pixel])) {
                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    continue;
                }

                const float persp_denom = w0 * tri.v[0].inv_w + w1 * tri.v[1].inv_w + w2 * tri.v[2].inv_w;
                const bool perspective_ok = std::fabs(persp_denom) > 0.0000001f;
                const float inv_persp = perspective_ok ? (1.0f / persp_denom) : 1.0f;
                const float u = perspective_ok
                    ? (w0 * u0 + w1 * u1 + w2 * u2) * inv_persp
                    : (w0 * tri.v[0].u + w1 * tri.v[1].u + w2 * tri.v[2].u);
                const float v = perspective_ok
                    ? (w0 * v0 + w1 * v1 + w2 * v2) * inv_persp
                    : (w0 * tri.v[0].v + w1 * tri.v[1].v + w2 * tri.v[2].v);
                const float tu1 = perspective_ok
                    ? (w0 * u10 + w1 * u11 + w2 * u12) * inv_persp
                    : (w0 * tri.v[0].u1 + w1 * tri.v[1].u1 + w2 * tri.v[2].u1);
                const float tv1 = perspective_ok
                    ? (w0 * v10 + w1 * v11 + w2 * v12) * inv_persp
                    : (w0 * tri.v[0].v1 + w1 * tri.v[1].v1 + w2 * tri.v[2].v1);
                const ColorF tex = sample_texture_color(texture, u, v);
                const ColorF primary = {
                    perspective_ok ? (w0 * r0 + w1 * r1 + w2 * r2) * inv_persp : (w0 * tri.v[0].r + w1 * tri.v[1].r + w2 * tri.v[2].r),
                    perspective_ok ? (w0 * g0 + w1 * g1 + w2 * g2) * inv_persp : (w0 * tri.v[0].g + w1 * tri.v[1].g + w2 * tri.v[2].g),
                    perspective_ok ? (w0 * b0 + w1 * b1 + w2 * b2) * inv_persp : (w0 * tri.v[0].b + w1 * tri.v[1].b + w2 * tri.v[2].b),
                    perspective_ok ? (w0 * a0 + w1 * a1 + w2 * a2) * inv_persp : (w0 * tri.v[0].a + w1 * tri.v[1].a + w2 * tri.v[2].a),
                };
                ColorF src{};
                switch (tri.tex_env_path) {
                    case kTexEnvPathModulate:
                        src = {tex.r * primary.r, tex.g * primary.g, tex.b * primary.b, tex.a * primary.a};
                        break;
                    case kTexEnvPathReplace:
                        src = tex;
                        break;
                    case kTexEnvPathAddSigned:
                        src = {
                            std::clamp((tex.r + primary.r - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp((tex.g + primary.g - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp((tex.b + primary.b - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp(env.combine_alpha == GL_REPLACE ? tex.a * env.alpha_scale : tex.a * primary.a * env.alpha_scale, 0.0f, 1.0f),
                        };
                        break;
                    case kTexEnvPathDot3: {
                        const float dot = 4.0f * ((tex.r - 0.5f) * (primary.r - 0.5f) +
                                                  (tex.g - 0.5f) * (primary.g - 0.5f) +
                                                  (tex.b - 0.5f) * (primary.b - 0.5f));
                        const float rgb = std::clamp(dot * env.rgb_scale, 0.0f, 1.0f);
                        src = {rgb, rgb, rgb, std::clamp(tex.a * primary.a * env.alpha_scale, 0.0f, 1.0f)};
                        break;
                    }
                    case kTexEnvPathAddSignedPrimary:
                        src = {
                            std::clamp((primary.r + primary.r - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp((primary.g + primary.g - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp((primary.b + primary.b - 0.5f) * env.rgb_scale, 0.0f, 1.0f),
                            std::clamp(tex.a * env.alpha_scale, 0.0f, 1.0f),
                        };
                        break;
                    default: {
                        const std::array<float, 4> tex_array = {tex.r, tex.g, tex.b, tex.a};
                        const std::array<float, 4> primary_array = {primary.r, primary.g, primary.b, primary.a};
                        const auto out = apply_tex_env(tri, tex_array, primary_array);
                        src = {out[0], out[1], out[2], out[3]};
                        break;
                    }
                }
                if (tri.texture1_enabled && texture1) {
                    const ColorF tex_unit1 = sample_texture_color(texture1, tu1, tv1);
                    const std::array<float, 4> texture1_array = {tex_unit1.r, tex_unit1.g, tex_unit1.b, tex_unit1.a};
                    const std::array<float, 4> previous = {src.r, src.g, src.b, src.a};
                    const auto out = apply_tex_env_state(tri.tex_env1, texture1_array, previous);
                    src = {out[0], out[1], out[2], out[3]};
                }
                if (!alpha_test_passes(tri, src.a)) {
                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    continue;
                }
                if (tri.depth_mask) {
                    depth_buffer_[pixel] = z;
                }
                if (!tri.blend_enabled) {
                    const size_t out_offset = pixel * 4u;
                    framebuffer_rgba_[out_offset + 0] = unit_to_u8(src.r);
                    framebuffer_rgba_[out_offset + 1] = unit_to_u8(src.g);
                    framebuffer_rgba_[out_offset + 2] = unit_to_u8(src.b);
                    framebuffer_rgba_[out_offset + 3] = unit_to_u8(src.a);
                } else {
                    blend_pixel(pixel, src, tri);
                }
                ++written_pixels;
                e0 += e0_dx;
                e1 += e1_dx;
                e2 += e2_dx;
            }
        }
        if (written_pixels != 0) {
            cpu_framebuffer_dirty_ = true;
        }
        return written_pixels;
    }

    void render_guest_gl_triangle(const PendingTriangle& tri) {
        upload_cpu_framebuffer_to_target();
        SDL_SetRenderTarget(renderer_, guest_gl_framebuffer_);
        SDL_SetRenderViewport(renderer_, &guest_gl_viewport_);
        SDL_Texture* texture = nullptr;
        auto it = guest_textures_.find(tri.texture);
        if (tri.texture_enabled && it != guest_textures_.end()) {
            texture = it->second.texture;
        }
        SDL_SetRenderDrawBlendMode(renderer_, tri.blend_mode);
        if (texture) {
            SDL_SetTextureBlendMode(texture, tri.blend_mode);
        }

        const GuestTexture* active_tex = (tri.texture_enabled && it != guest_textures_.end()) ? &it->second : nullptr;
        const auto make_vertex = [active_tex](const GuestGLVertex2D& v) {
            SDL_Vertex out{};
            out.position.x = v.x;
            out.position.y = v.y;
            out.color.r = std::clamp(v.r, 0.0f, 1.0f);
            out.color.g = std::clamp(v.g, 0.0f, 1.0f);
            out.color.b = std::clamp(v.b, 0.0f, 1.0f);
            out.color.a = std::clamp(v.a, 0.0f, 1.0f);
            out.tex_coord.x = active_tex ? apply_texture_wrap(v.u, active_tex->wrap_s) : v.u;
            out.tex_coord.y = active_tex ? apply_texture_wrap(v.v, active_tex->wrap_t) : v.v;
            return out;
        };
        SDL_Vertex verts[3] = {make_vertex(tri.v[0]), make_vertex(tri.v[1]), make_vertex(tri.v[2])};
        if (!SDL_RenderGeometry(renderer_, texture, verts, 3, nullptr, 0)) {
            std::cerr << "SDL_RenderGeometry triangle failed: " << SDL_GetError() << std::endl;
        }
        cpu_framebuffer_valid_ = false;
    }

    struct FlushStats {
        size_t queued_triangles = 0;
        size_t depth_triangles = 0;
        size_t sdl_triangles = 0;
        size_t depth_pixels = 0;
        size_t generic_texenv_triangles = 0;
        std::set<uint32_t> textures;
    };

    template <typename Iter>
    void flush_depth_run(Iter first, Iter last, FlushStats& stats) {
        for (Iter it = first; it != last; ++it) {
            ++stats.depth_triangles;
            stats.textures.insert(it->texture);
            if (it->tex_env_path == kTexEnvPathGeneric) {
                ++stats.generic_texenv_triangles;
            }
            stats.depth_pixels += rasterize_depth_triangle(*it);
        }
    }

    void flush_guest_gl_triangles() {
        const bool trace_profile = std::getenv("ZEEMU_TRACE_RENDER_PROFILE") != nullptr;
        const auto start = std::chrono::steady_clock::now();
        FlushStats stats{};
        stats.queued_triangles = pending_triangles_.size();
        last_flush_queued_triangles_ = stats.queued_triangles;
        auto depth_run_first = pending_triangles_.end();
        for (auto it = pending_triangles_.begin(); it != pending_triangles_.end(); ++it) {
            if (it->depth_enabled || it->cpu_raster_required) {
                if (depth_run_first == pending_triangles_.end()) {
                    depth_run_first = it;
                }
                continue;
            }
            if (depth_run_first != pending_triangles_.end()) {
                flush_depth_run(depth_run_first, it, stats);
                depth_run_first = pending_triangles_.end();
            }
            ++stats.sdl_triangles;
            stats.textures.insert(it->texture);
            render_guest_gl_triangle(*it);
        }
        if (depth_run_first != pending_triangles_.end()) {
            flush_depth_run(depth_run_first, pending_triangles_.end(), stats);
        }
        upload_cpu_framebuffer_to_target();
        pending_triangles_.clear();
        if (trace_profile) {
            const auto end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - start).count();
            printf("  RenderProfile swap queued=%zu depth_tris=%zu sdl_tris=%zu depth_pixels=%zu texenv_generic=%zu tex={",
                   stats.queued_triangles,
                   stats.depth_triangles,
                   stats.sdl_triangles,
                   stats.depth_pixels,
                   stats.generic_texenv_triangles);
            bool first_tex = true;
            for (uint32_t texture : stats.textures) {
                printf("%s%u", first_tex ? "" : ",", texture);
                first_tex = false;
            }
            printf("} flush_ms=%.3f\n",
                   ms);
        }
    }

    void maybe_dump_guest_gl_frame(bool swap_had_content = true) {
        const char* requested_path = std::getenv("ZEEMU_DUMP_GUEST_GL_FRAME");
        const char* mode = std::getenv("ZEEMU_DUMP_GUEST_GL_FRAME_MODE");
        const bool dump_last_swap = mode != nullptr && std::strcmp(mode, "last") == 0;
        if ((dumped_guest_gl_frame_ && !dump_last_swap) || requested_path == nullptr) {
            return;
        }
        // In last-swap mode prefer the last swap that actually drew geometry.
        // Guest engines often emit clear-only swaps (queued=0) between scenes or
        // during loading; capturing one of those overwrites the checkpoint with a
        // black frame even though a valid frame was presented earlier. The first
        // dump in non-last mode is unaffected because it captures swap one.
        if (dump_last_swap && !swap_had_content) {
            return;
        }
        SDL_Surface* surface = SDL_RenderReadPixels(renderer_, nullptr);
        if (!surface) {
            std::cerr << "SDL_RenderReadPixels guest GL framebuffer failed: " << SDL_GetError() << std::endl;
            dumped_guest_gl_frame_ = true;
            return;
        }

        const char* path = (requested_path[0] != '\0' && std::strcmp(requested_path, "1") != 0)
            ? requested_path
            : "logs/guest_gl_frame.ppm";
        const std::string temp_path = std::string(path) + ".tmp";
        FILE* fp = std::fopen(temp_path.c_str(), "wb");
        if (!fp) {
            SDL_DestroySurface(surface);
            dumped_guest_gl_frame_ = true;
            return;
        }
        std::fprintf(fp, "P6\n%d %d\n255\n", surface->w, surface->h);
        for (int y = 0; y < surface->h; ++y) {
            for (int x = 0; x < surface->w; ++x) {
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                Uint8 a = 0;
                SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
                std::fputc(r, fp);
                std::fputc(g, fp);
                std::fputc(b, fp);
            }
        }
        std::fclose(fp);
        std::remove(path);
        std::rename(temp_path.c_str(), path);
        SDL_DestroySurface(surface);
        if (!dumped_guest_gl_frame_) {
            printf("  Dumped guest GL frame: %s%s\n", path, dump_last_swap ? " (last-swap mode)" : "");
        }
        dumped_guest_gl_frame_ = true;
    }

    RenderBackendKind kind_;
    const char* sdl_driver_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int texture_width_ = 0;
    int texture_height_ = 0;
    SDL_Texture* guest_gl_framebuffer_ = nullptr;
    SDL_Texture* guest_gl_cpu_texture_ = nullptr;
    int logical_width_ = 0;
    int logical_height_ = 0;
    SDL_Rect guest_gl_viewport_{0, 0, 640, 480};
    bool guest_gl_surface_scale_enabled_ = false;
    SDL_FRect guest_gl_surface_scale_src_{0.0f, 0.0f, 640.0f, 480.0f};
    SDL_FRect guest_gl_surface_scale_dst_{0.0f, 0.0f, 640.0f, 480.0f};
    bool dumped_guest_gl_frame_ = false;
    size_t last_flush_queued_triangles_ = 0;
    std::vector<uint8_t> framebuffer_rgba_;
    std::vector<float> depth_buffer_;
    bool cpu_framebuffer_valid_ = true;
    bool cpu_framebuffer_dirty_ = false;
    bool suppress_host_bitmap_present_ = false;
    bool guest_gl_frame_active_ = false;
    std::unordered_map<uint32_t, GuestTexture> guest_textures_;
    uint32_t bound_texture_2d_ = 0;
    std::array<uint32_t, 2> bound_textures_2d_ = {0, 0};
    uint32_t active_texture_unit_ = 0;
    std::array<bool, 2> texture_2d_enabled_ = {false, false};
    Uint8 clear_r_ = 0;
    Uint8 clear_g_ = 0;
    Uint8 clear_b_ = 0;
    Uint8 clear_a_ = 255;
    bool blend_enabled_ = false;
    uint32_t blend_src_ = 0x0302; // GL_SRC_ALPHA
    uint32_t blend_dst_ = 0x0303; // GL_ONE_MINUS_SRC_ALPHA
    bool alpha_test_enabled_ = false;
    uint32_t alpha_func_ = 0x0207; // GL_ALWAYS
    float alpha_ref_ = 0.0f;
    bool depth_test_enabled_ = false;
    uint32_t depth_func_ = GL_LESS;
    bool depth_mask_ = true;
    TexEnvState tex_env_;
    std::array<TexEnvState, 2> tex_env_units_;
    std::vector<PendingTriangle> pending_triangles_;
    bool guest_gl_presented_since_host_frame_ = false;
    bool guest_gl_preserve_swap_buffer_ = false;
    
    // I3D state
    struct I3DLight {
        float pos_x = 0, pos_y = 0, pos_z = 0, pos_w = 0;
        float ambient_r = 0, ambient_g = 0, ambient_b = 0, ambient_a = 0;
        float diffuse_r = 0, diffuse_g = 0, diffuse_b = 0, diffuse_a = 0;
        float specular_r = 0, specular_g = 0, specular_b = 0, specular_a = 0;
    };
    struct I3DMaterial {
        float ambient_r = 0, ambient_g = 0, ambient_b = 0, ambient_a = 0;
        float diffuse_r = 0, diffuse_g = 0, diffuse_b = 0, diffuse_a = 0;
        float specular_r = 0, specular_g = 0, specular_b = 0, specular_a = 0;
        float emission_r = 0, emission_g = 0, emission_b = 0, emission_a = 0;
        float shininess = 0;
    };
    std::vector<I3DLight> i3d_lights_;
    I3DMaterial i3d_material_;
    uint32_t i3d_shade_model_ = 0x1D01; // GL_SMOOTH
    uint32_t i3d_cull_face_ = 0x0405; // GL_BACK
    bool i3d_cull_face_enabled_ = false;
    bool i3d_depth_test_ = false;
    bool i3d_blend_enabled_ = false;
    bool i3d_lighting_enabled_ = false;
    bool i3d_color_material_ = false;

    SDL_BlendMode sdl_blend_mode() const {
        if (!blend_enabled_) {
            if (alpha_test_enabled_ && alpha_func_ == 0x0205 && alpha_ref_ == 0.0f) { // GL_NOTEQUAL, 0
                return SDL_BLENDMODE_BLEND;
            }
            return SDL_BLENDMODE_NONE;
        }
        if (blend_src_ == 0x0302 && blend_dst_ == 0x0303) {
            return SDL_BLENDMODE_BLEND;
        }
        if (blend_src_ == 0x0302 && blend_dst_ == 0x0001) {
            return SDL_BLENDMODE_ADD;
        }
        if (blend_src_ == 0x0001 && blend_dst_ == 0x0001) {
            return SDL_BLENDMODE_ADD;
        }
        return SDL_BLENDMODE_BLEND;
    }
};

} // namespace

RenderBackendKind parse_render_backend(const char* name) {
    std::string value = lower_ascii(name);
    if (value == "vulkan" || value == "vk") return RenderBackendKind::Vulkan;
    if (value == "opengl" || value == "gl") return RenderBackendKind::OpenGL;
    return RenderBackendKind::SDL;
}

const char* render_backend_name(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::SDL: return "SDL";
        case RenderBackendKind::Vulkan: return "Vulkan";
        case RenderBackendKind::OpenGL: return "OpenGL";
    }
    return "unknown";
}

std::unique_ptr<FramePresenter> create_frame_presenter(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::SDL:
            return std::make_unique<SDLFramePresenter>(kind, nullptr);
        case RenderBackendKind::Vulkan:
            return std::make_unique<SDLFramePresenter>(kind, "vulkan");
        case RenderBackendKind::OpenGL:
            return std::make_unique<SDLFramePresenter>(kind, "opengl");
    }
    return {};
}

} // namespace zeemu::gfx
