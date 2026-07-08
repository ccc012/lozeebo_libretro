#include "brew/BrewQXGL.h"
#include "brew/BrewQXGLState.h"

bool handle_qx_gl_call(const std::string& name, BrewShell& shell, EndianMemory& memory, CPU& cpu, const char* label) {
    return qxgl::handle_qx_gl_call(name, shell, memory, cpu, label);
}

void brew_qxgl_register_tga_payload_hint(addr_t pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t bits_per_pixel,
                                         bool origin_top) {
    if (!pixels || pixels >= 0xFF000000 || width == 0 || height == 0) {
        return;
    }
    qxgl::TexturePayloadHint hint{width, height, bits_per_pixel, origin_top};
    qxgl::texture_payload_hints()[pixels] = hint;
    qxgl::pending_texture_payload_hints().push_back(hint);
}
