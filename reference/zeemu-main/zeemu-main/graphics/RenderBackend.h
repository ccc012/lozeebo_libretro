#ifndef ZEEMU_GRAPHICS_RENDER_BACKEND_H_
#define ZEEMU_GRAPHICS_RENDER_BACKEND_H_

#include <cstdint>
#include <memory>

namespace zeemu::gfx {

enum class RenderBackendKind {
    SDL,
    Vulkan,
    OpenGL,
};

RenderBackendKind parse_render_backend(const char* name);
const char* render_backend_name(RenderBackendKind kind);

struct GuestGLVertex2D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float inv_w = 1.0f;
};

class FramePresenter {
public:
    virtual ~FramePresenter() = default;

    virtual bool initialize(const char* title, int width, int height) = 0;
    virtual void begin_frame() = 0;
    virtual void present_rgb565(const void* pixels, int pitch) = 0;
    virtual void present_rgb565(const void* pixels, int pitch, int width, int height) {
        (void)width;
        (void)height;
        present_rgb565(pixels, pitch);
    }
    virtual void draw_debug_text(float x, float y, const char* text) = 0;
    virtual void end_frame() = 0;

    virtual bool guest_gl_available() const { return false; }
    virtual void guest_gl_viewport(int, int, int, int) {}
    virtual void guest_gl_surface_scale(bool, int, int, int, int, int, int, int, int) {}
    virtual void guest_gl_clear_color(float, float, float, float) {}
    virtual void guest_gl_clear(uint32_t) {}
    virtual void guest_gl_depth_state(bool, uint32_t, bool) {}
    virtual void guest_gl_blend(bool, uint32_t, uint32_t) {}
    virtual void guest_gl_alpha_test(bool, uint32_t, float) {}
    virtual void guest_gl_active_texture_unit(uint32_t) {}
    virtual void guest_gl_texture_2d_enabled(uint32_t, bool) {}
    virtual void guest_gl_tex_env(uint32_t, uint32_t) {}
    virtual void guest_gl_tex_env_color(float, float, float, float) {}
    virtual void guest_gl_bind_texture(uint32_t, uint32_t) {}
    virtual void guest_gl_delete_texture(uint32_t) {}
    virtual void guest_gl_tex_parameter(uint32_t, uint32_t, uint32_t) {}
    virtual void guest_gl_tex_image_2d(uint32_t, int, int, int, int, int, uint32_t, uint32_t, const void*) {}
    virtual void guest_gl_draw_triangle(const GuestGLVertex2D&, const GuestGLVertex2D&, const GuestGLVertex2D&) {}
    virtual void guest_gl_swap_behavior_preserved(bool) {}
    virtual void guest_gl_swap_buffers() {}
    virtual bool consume_guest_gl_presented() { return false; }
    virtual bool has_guest_gl_frame() const { return false; }
    
    // I3D fixed-function state (light/material args: pname selects which
    // color/vector the floats carry, matching glLightfv/glMaterialfv shapes).
    virtual void guest_gl_light(uint32_t, uint32_t, float, float, float, float, float, float, float) {}
    virtual void guest_gl_material(uint32_t, float, float, float, float, float) {}
    virtual void guest_gl_shade_model(uint32_t) {}
    virtual void guest_gl_cull_face(uint32_t) {}
    virtual void guest_gl_enable_disable(uint32_t, bool) {}
};

std::unique_ptr<FramePresenter> create_frame_presenter(RenderBackendKind kind);

} // namespace zeemu::gfx

#endif
