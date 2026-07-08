#include "brew/BrewQXGLState.h"

namespace qxgl {

struct Vertex {
    Vec4 pos;
    Vec4 color;
    Vec2 texcoord;
    Vec2 texcoord1;
};

struct ClipVertex {
    Vec4 clip;
    Vec4 color;
    Vec2 texcoord;
    Vec2 texcoord1;
};

void draw_triangle(EndianMemory& memory, BrewShell& shell, const Vertex& v0, const Vertex& v1, const Vertex& v2, int width, int height);
uint32_t read_index(EndianMemory& memory, const QxGpuState& state, addr_t indices, uint32_t type, uint32_t element);

void process_draw_call(BrewShell& shell, EndianMemory& memory, CPU& cpu, const std::string& label);
void process_draw_elements(BrewShell& shell, EndianMemory& memory, CPU& cpu, uint32_t mode, uint32_t count, uint32_t type, addr_t indices, const std::string& label);

void paint_device_bitmap(BrewShell& shell, EndianMemory& memory, uint16_t fill, const char* label) {
    BrewDisplay* display = shell.get_display();
    if (!display) {
        return;
    }
    BrewBitmap* bmp = display->get_device_bitmap();
    if (!bmp) {
        return;
    }

    const int width = bmp->get_width();
    const int height = bmp->get_height();
    const addr_t base = bmp->get_buffer_ptr();
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::string pattern(pixel_count * 2, '\0');
    for (size_t i = 0; i < pixel_count; ++i) {
        pattern[i * 2 + 0] = static_cast<char>(fill & 0xFFu);
        pattern[i * 2 + 1] = static_cast<char>((fill >> 8) & 0xFFu);
    }
    memory.write(base, pattern);

    (void)label;
}

std::vector<float> read_vertex_array(EndianMemory& memory, const QxGpuState& state, const VertexArray& va, int count, int offset = 0) {
    if (!va.enabled || count <= 0) return {};
    std::vector<float> result(count * va.size);
    const int component_size = array_component_size(va.type);
    const int stride = va.stride ? va.stride : va.size * component_size;
    for (int vertex = 0; vertex < count; ++vertex) {
        const addr_t base = va.ptr + static_cast<addr_t>((offset + vertex) * stride);
        for (int component = 0; component < va.size; ++component) {
            const addr_t component_offset = base + static_cast<addr_t>(component * component_size);
            uint32_t raw = 0;
            bool have_raw = false;
            if (va.buffer != 0) {
                const auto it = state.buffers.find(va.buffer);
                if (it != state.buffers.end()) {
                    have_raw = read_buffer_value(it->second, static_cast<size_t>(component_offset), component_size, raw);
                }
            } else {
                const addr_t addr = component_offset;
                if (component_size == 1) {
                    raw = memory.read_value(addr, EndianMemory::Byte);
                } else if (component_size == 2) {
                    raw = memory.read_value(addr, EndianMemory::Halfword);
                } else {
                    raw = memory.read_value(addr);
                }
                have_raw = true;
            }
            float value = 0.0f;
            if (!have_raw) {
                result[vertex * va.size + component] = value;
                continue;
            }
            if (va.type == kGlByte) {
                value = static_cast<float>(static_cast<int8_t>(raw & 0xffu));
            } else if (va.type == kGlUnsignedByte) {
                value = static_cast<float>(raw) / 255.0f;
            } else if (va.type == kGlFloat) {
                value = raw_to_float(raw);
            } else if (va.type == kGlFixed) {
                value = fixed_to_float(raw);
            } else if (va.type == kGlShort) {
                value = static_cast<float>(static_cast<int16_t>(raw));
            } else if (va.type == kGlUnsignedShort) {
                value = static_cast<float>(raw);
            }
            result[vertex * va.size + component] = value;
        }
    }
    return result;
}

std::vector<float> read_array_vertex(EndianMemory& memory, const QxGpuState& state, const VertexArray& va, uint32_t index) {
    return read_vertex_array(memory, state, va, 1, static_cast<int>(index));
}

std::vector<float> read_texcoord_array_vertex(EndianMemory& memory, const QxGpuState& state, const VertexArray& va, uint32_t index, bool legacy_qx_fixed) {
    if (!legacy_qx_fixed || va.type != kGlFixed) {
        return read_array_vertex(memory, state, va, index);
    }
    if (!va.enabled || va.size <= 0) {
        return {};
    }
    std::vector<float> result(static_cast<size_t>(va.size));
    const int component_size = 4;
    const int stride = va.stride ? va.stride : va.size * component_size;
    const addr_t base = va.ptr + static_cast<addr_t>(index * stride);
    for (int component = 0; component < va.size; ++component) {
        const addr_t component_offset = base + static_cast<addr_t>(component * component_size);
        uint32_t raw = 0;
        if (va.buffer != 0) {
            const auto it = state.buffers.find(va.buffer);
            if (it == state.buffers.end() ||
                !read_buffer_value(it->second, static_cast<size_t>(component_offset), component_size, raw)) {
                continue;
            }
        } else {
            raw = memory.read_value(component_offset);
        }
        result[static_cast<size_t>(component)] = fixed_to_float(raw);
    }
    return result;
}

bool uses_legacy_qx_fixed_texcoords(const std::string& label) {
    return label.rfind("IGL_gl", 0) == 0 || label.rfind("GLProc_", 0) == 0;
}

bool uses_legacy_qx_fixed_texcoords_for_draw_arrays(const QxGpuState& state, uint32_t count, const std::string& label) {
    if (uses_legacy_qx_fixed_texcoords(label)) {
        return true;
    }
    return label.rfind("QXGL", 0) == 0 &&
           state.texcoord_array.type == kGlFixed &&
           state.texcoord_array.stride == 8 &&
           count >= 64;
}

bool matrix_is_identity(const std::array<float, 16>& mat) {
    constexpr float kEpsilon = 0.0001f;
    for (int i = 0; i < 16; ++i) {
        const float expected = (i % 5) == 0 ? 1.0f : 0.0f;
        if (std::fabs(mat[static_cast<size_t>(i)] - expected) > kEpsilon) {
            return false;
        }
    }
    return true;
}

bool uses_legacy_qx_centered_pixel_projection(const QxGpuState& state, uint32_t mode) {
    return mode == kGlTriangleStrip &&
           state.vertex_array.enabled &&
           state.vertex_array.size == 2 &&
           state.vertex_array.type == kGlShort &&
           state.vertex_array.stride == 0 &&
           state.viewport[2] > 0 &&
           state.viewport[3] > 0 &&
           matrix_is_identity(state.current[0]) &&
           matrix_is_identity(state.current[1]);
}

bool matrix_is_pure_scale(const std::array<float, 16>& mat, float sx, float sy, float sz, float epsilon) {
    for (int i = 0; i < 16; ++i) {
        float expected = 0.0f;
        if (i == 0) expected = sx;
        else if (i == 5) expected = sy;
        else if (i == 10) expected = sz;
        else if (i == 15) expected = 1.0f;
        if (std::fabs(mat[static_cast<size_t>(i)] - expected) > epsilon) {
            return false;
        }
    }
    return true;
}

bool projection_already_normalizes_short_vertices(const QxGpuState& state) {
    const auto& proj = state.current[1];
    return std::fabs(proj[0]) > 0.0f && std::fabs(proj[0]) <= (1.0f / 1024.0f) &&
           std::fabs(proj[5]) > 0.0f && std::fabs(proj[5]) <= (1.0f / 1024.0f) &&
           std::fabs(proj[3]) < 0.0001f &&
           std::fabs(proj[7]) < 0.0001f &&
           std::fabs(proj[11]) < 0.0001f &&
           std::fabs(proj[15] - 1.0f) < 0.0001f;
}

bool uses_ffp_short_overlay_projection(const QxGpuState& state) {
    constexpr float kShortUnit = 1.0f / 32767.0f;
    return state.vertex_array.enabled &&
           state.vertex_array.size >= 3 &&
           state.vertex_array.type == kGlShort &&
           state.texcoord_array.enabled &&
           state.texcoord_array.type == kGlShort &&
           matrix_is_pure_scale(state.current[0], kShortUnit, kShortUnit, kShortUnit, 0.0000001f) &&
           projection_already_normalizes_short_vertices(state);
}

void draw_triangle(EndianMemory& memory, BrewShell& shell, const Vertex& v0, const Vertex& v1, const Vertex& v2, int width, int height) {
    (void)memory;
    BrewDisplay* display = shell.get_display();
    if (!display) return;
    BrewBitmap* bmp = display->get_device_bitmap();
    if (!bmp) return;

    if (auto* presenter = shell.get_presenter()) {
        const auto& state = qx_state();
        const auto apply_texture_state = [&](const Vertex& in) {
            Vertex out = in;
            if (state.texture_env_mode == kGlReplace) {
                out.color.x = 1.0f;
                out.color.y = 1.0f;
                out.color.z = 1.0f;
                out.color.w = 1.0f;
            }
            return out;
        };
        const Vertex tv0 = apply_texture_state(v0);
        const Vertex tv1 = apply_texture_state(v1);
        const Vertex tv2 = apply_texture_state(v2);
        const float min_x = std::min({tv0.pos.x, tv1.pos.x, tv2.pos.x});
        const float max_x = std::max({tv0.pos.x, tv1.pos.x, tv2.pos.x});
        const float min_y = std::min({tv0.pos.y, tv1.pos.y, tv2.pos.y});
        const float max_y = std::max({tv0.pos.y, tv1.pos.y, tv2.pos.y});
        if (max_x <= 0.0f || min_x >= static_cast<float>(width) ||
            max_y <= 0.0f || min_y >= static_cast<float>(height)) {
            return;
        }
        presenter->guest_gl_draw_triangle(
            {tv0.pos.x, tv0.pos.y, tv0.pos.z, tv0.color.x, tv0.color.y, tv0.color.z, tv0.color.w, tv0.texcoord.u, tv0.texcoord.v, tv0.texcoord1.u, tv0.texcoord1.v, tv0.pos.w != 0.0f ? 1.0f / tv0.pos.w : 1.0f},
            {tv1.pos.x, tv1.pos.y, tv1.pos.z, tv1.color.x, tv1.color.y, tv1.color.z, tv1.color.w, tv1.texcoord.u, tv1.texcoord.v, tv1.texcoord1.u, tv1.texcoord1.v, tv1.pos.w != 0.0f ? 1.0f / tv1.pos.w : 1.0f},
            {tv2.pos.x, tv2.pos.y, tv2.pos.z, tv2.color.x, tv2.color.y, tv2.color.z, tv2.color.w, tv2.texcoord.u, tv2.texcoord.v, tv2.texcoord1.u, tv2.texcoord1.v, tv2.pos.w != 0.0f ? 1.0f / tv2.pos.w : 1.0f});
    }
}

uint32_t read_index(EndianMemory& memory, const QxGpuState& state, addr_t indices, uint32_t type, uint32_t element) {
    const int component_size = type == kGlUnsignedByte ? 1 : (type == kGlUnsignedShort ? 2 : 4);
    const addr_t offset = indices + static_cast<addr_t>(element * component_size);
    if (state.bound_element_buffer != 0) {
        const auto it = state.buffers.find(state.bound_element_buffer);
        uint32_t out = 0;
        if (it != state.buffers.end()) {
            read_buffer_value(it->second, static_cast<size_t>(offset), component_size, out);
        }
        return out;
    }
    if (type == kGlUnsignedByte) {
        return memory.read_value(indices + element, EndianMemory::Byte);
    }
    if (type == kGlUnsignedShort) {
        return memory.read_value(indices + static_cast<addr_t>(element * 2), EndianMemory::Halfword);
    }
    return memory.read_value(indices + static_cast<addr_t>(element * 4));
}

float map_ndc_depth_to_presenter_depth(const QxGpuState& state, float ndc_z) {
    const float normalized = std::clamp((ndc_z * 0.5f) + 0.5f, 0.0f, 1.0f);
    const float window_z = state.depth_range_near + (state.depth_range_far - state.depth_range_near) * normalized;
    return (std::clamp(window_z, 0.0f, 1.0f) * 2.0f) - 1.0f;
}

Vertex make_vertex_from_index(EndianMemory& memory, const QxGpuState& state, uint32_t index,
                              const std::array<float, 16>& mvp, float vp_x, float vp_y,
                              float vp_w, float vp_h, bool legacy_qx_fixed_texcoords) {
    Vertex out{};
    out.color = Vec4(state.current_color[0], state.current_color[1], state.current_color[2], state.current_color[3]);

    const auto position_values = read_array_vertex(memory, state, state.vertex_array, index);
    if (state.vertex_array.size >= 2 && position_values.size() >= static_cast<size_t>(state.vertex_array.size)) {
        Vec4 pos(position_values[0],
                 position_values[1],
                 state.vertex_array.size >= 3 ? position_values[2] : 0.0f,
                 state.vertex_array.size >= 4 ? position_values[3] : 1.0f);
        Vec4 clip_pos = transform_point(pos, mvp);
        if (clip_pos.w != 0.0f) {
            const float inv_w = 1.0f / clip_pos.w;
            out.pos.x = (clip_pos.x * inv_w + 1.0f) * 0.5f * vp_w + vp_x;
            out.pos.y = (1.0f - (clip_pos.y * inv_w + 1.0f) * 0.5f) * vp_h + vp_y;
            out.pos.z = map_ndc_depth_to_presenter_depth(state, clip_pos.z * inv_w);
            out.pos.w = clip_pos.w;
        }
    }

    const auto color_values = read_array_vertex(memory, state, state.color_array, index);
    if (state.color_array.size == 3 && color_values.size() >= 3) {
        out.color = Vec4(color_values[0], color_values[1], color_values[2], 1.0f);
    } else if (state.color_array.size == 4 && color_values.size() >= 4) {
        out.color = Vec4(color_values[0], color_values[1], color_values[2], color_values[3]);
    }

    const auto texcoord_values = read_texcoord_array_vertex(memory, state, state.texcoord_array, index, legacy_qx_fixed_texcoords);
    if (state.texcoord_array.size >= 2 && texcoord_values.size() >= 2) {
        const Vec2 raw(texcoord_values[0], texcoord_values[1]);
        out.texcoord = transform_texcoord(raw, state.current[2]);
    } else {
        out.texcoord = transform_texcoord(Vec2(state.current_texcoord[0][0], state.current_texcoord[0][1]), state.current[2]);
    }
    const auto texcoord1_values = read_texcoord_array_vertex(memory, state, state.texcoord_arrays[1], index, legacy_qx_fixed_texcoords);
    if (state.texcoord_arrays[1].size >= 2 && texcoord1_values.size() >= 2) {
        out.texcoord1 = transform_texcoord(Vec2(texcoord1_values[0], texcoord1_values[1]), state.current[2]);
    } else {
        out.texcoord1 = transform_texcoord(Vec2(state.current_texcoord[1][0], state.current_texcoord[1][1]), state.current[2]);
    }

    return out;
}

ClipVertex make_clip_vertex_from_index(EndianMemory& memory, const QxGpuState& state, uint32_t index,
                                       const std::array<float, 16>& mvp, bool legacy_qx_fixed_texcoords) {
    ClipVertex out{};
    out.color = Vec4(state.current_color[0], state.current_color[1], state.current_color[2], state.current_color[3]);

    const auto position_values = read_array_vertex(memory, state, state.vertex_array, index);
    if (state.vertex_array.size >= 2 && position_values.size() >= static_cast<size_t>(state.vertex_array.size)) {
        Vec4 pos(position_values[0],
                 position_values[1],
                 state.vertex_array.size >= 3 ? position_values[2] : 0.0f,
                 state.vertex_array.size >= 4 ? position_values[3] : 1.0f);
        out.clip = transform_point(pos, mvp);
    }

    const auto color_values = read_array_vertex(memory, state, state.color_array, index);
    if (state.color_array.size == 3 && color_values.size() >= 3) {
        out.color = Vec4(color_values[0], color_values[1], color_values[2], 1.0f);
    } else if (state.color_array.size == 4 && color_values.size() >= 4) {
        out.color = Vec4(color_values[0], color_values[1], color_values[2], color_values[3]);
    }

    const auto texcoord_values = read_texcoord_array_vertex(memory, state, state.texcoord_array, index, legacy_qx_fixed_texcoords);
    if (state.texcoord_array.size >= 2 && texcoord_values.size() >= 2) {
        const Vec2 raw(texcoord_values[0], texcoord_values[1]);
        out.texcoord = transform_texcoord(raw, state.current[2]);
    } else {
        out.texcoord = transform_texcoord(Vec2(state.current_texcoord[0][0], state.current_texcoord[0][1]), state.current[2]);
    }
    const auto texcoord1_values = read_texcoord_array_vertex(memory, state, state.texcoord_arrays[1], index, legacy_qx_fixed_texcoords);
    if (state.texcoord_arrays[1].size >= 2 && texcoord1_values.size() >= 2) {
        out.texcoord1 = transform_texcoord(Vec2(texcoord1_values[0], texcoord1_values[1]), state.current[2]);
    } else {
        out.texcoord1 = transform_texcoord(Vec2(state.current_texcoord[1][0], state.current_texcoord[1][1]), state.current[2]);
    }

    return out;
}

ClipVertex interpolate_clip_vertex(const ClipVertex& a, const ClipVertex& b, float t) {
    const auto lerp = [t](float x, float y) { return x + (y - x) * t; };
    ClipVertex out{};
    out.clip = Vec4(lerp(a.clip.x, b.clip.x), lerp(a.clip.y, b.clip.y),
                    lerp(a.clip.z, b.clip.z), lerp(a.clip.w, b.clip.w));
    out.color = Vec4(lerp(a.color.x, b.color.x), lerp(a.color.y, b.color.y),
                     lerp(a.color.z, b.color.z), lerp(a.color.w, b.color.w));
    out.texcoord = Vec2(lerp(a.texcoord.u, b.texcoord.u), lerp(a.texcoord.v, b.texcoord.v));
    out.texcoord1 = Vec2(lerp(a.texcoord1.u, b.texcoord1.u), lerp(a.texcoord1.v, b.texcoord1.v));
    return out;
}

bool alpha_test_passes(const QxGpuState& state, float alpha) {
    if (!state.alpha_test_enabled) {
        return true;
    }
    switch (state.alpha_func) {
        case kGlNever: return false;
        case kGlLess: return alpha < state.alpha_ref;
        case kGlEqual: return alpha == state.alpha_ref;
        case kGlLequal: return alpha <= state.alpha_ref;
        case kGlGreater: return alpha > state.alpha_ref;
        case kGlNotequal: return alpha != state.alpha_ref;
        case kGlGequal: return alpha >= state.alpha_ref;
        case kGlAlways: return true;
        default: return true;
    }
}

bool triangle_culled(const QxGpuState& state, const ClipVertex& a, const ClipVertex& b, const ClipVertex& c) {
    if (!state.cull_face_enabled) {
        return false;
    }
    const auto ndc_x = [](const ClipVertex& v) { return v.clip.x / v.clip.w; };
    const auto ndc_y = [](const ClipVertex& v) { return v.clip.y / v.clip.w; };
    const float ax = ndc_x(a);
    const float ay = ndc_y(a);
    const float bx = ndc_x(b);
    const float by = ndc_y(b);
    const float cx = ndc_x(c);
    const float cy = ndc_y(c);
    const float area = ((bx - ax) * (cy - ay)) - ((by - ay) * (cx - ax));
    if (area == 0.0f) {
        return true;
    }
    const bool ccw = area > 0.0f;
    const bool front = (state.front_face == kGlCw) ? !ccw : ccw;
    if (state.cull_face == kGlFrontAndBack) {
        return true;
    }
    if (state.cull_face == kGlFront) {
        return front;
    }
    return !front;
}

enum class ClipPlane {
    PositiveW,
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far,
};

float clip_plane_distance(const ClipVertex& v, ClipPlane plane) {
    constexpr float kMinW = 0.0001f;
    switch (plane) {
        case ClipPlane::PositiveW: return v.clip.w - kMinW;
        case ClipPlane::Left: return v.clip.x + v.clip.w;
        case ClipPlane::Right: return v.clip.w - v.clip.x;
        case ClipPlane::Bottom: return v.clip.y + v.clip.w;
        case ClipPlane::Top: return v.clip.w - v.clip.y;
        case ClipPlane::Near: return v.clip.z + v.clip.w;
        case ClipPlane::Far: return v.clip.w - v.clip.z;
    }
    return 0.0f;
}

std::vector<ClipVertex> clip_polygon_against_plane(const std::vector<ClipVertex>& input, ClipPlane plane) {
    if (input.empty()) {
        return {};
    }

    std::vector<ClipVertex> output;
    output.reserve(input.size() + 1u);
    ClipVertex prev = input.back();
    float prev_distance = clip_plane_distance(prev, plane);
    bool prev_inside = prev_distance >= 0.0f;

    for (const ClipVertex& cur : input) {
        const float cur_distance = clip_plane_distance(cur, plane);
        const bool cur_inside = cur_distance >= 0.0f;
        if (cur_inside != prev_inside) {
            const float denom = prev_distance - cur_distance;
            const float t = std::fabs(denom) > 0.0000001f ? prev_distance / denom : 0.0f;
            output.push_back(interpolate_clip_vertex(prev, cur, std::clamp(t, 0.0f, 1.0f)));
        }
        if (cur_inside) {
            output.push_back(cur);
        }
        prev = cur;
        prev_distance = cur_distance;
        prev_inside = cur_inside;
    }

    return output;
}

std::vector<ClipVertex> clip_to_view_volume(const std::array<ClipVertex, 3>& tri) {
    std::vector<ClipVertex> polygon = {tri[0], tri[1], tri[2]};
    const ClipPlane planes[] = {
        ClipPlane::PositiveW,
        ClipPlane::Left,
        ClipPlane::Right,
        ClipPlane::Bottom,
        ClipPlane::Top,
        ClipPlane::Near,
        ClipPlane::Far,
    };
    for (ClipPlane plane : planes) {
        polygon = clip_polygon_against_plane(polygon, plane);
        if (polygon.size() < 3) {
            return {};
        }
    }
    return polygon;
}

Vertex project_clip_vertex(const ClipVertex& in, float vp_x, float vp_y, float vp_w, float vp_h) {
    Vertex out{};
    out.color = in.color;
    out.texcoord = in.texcoord;
    out.texcoord1 = in.texcoord1;
    const float inv_w = 1.0f / in.clip.w;
    out.pos.x = (in.clip.x * inv_w + 1.0f) * 0.5f * vp_w + vp_x;
    out.pos.y = (1.0f - (in.clip.y * inv_w + 1.0f) * 0.5f) * vp_h + vp_y;
    out.pos.z = map_ndc_depth_to_presenter_depth(qx_state(), in.clip.z * inv_w);
    out.pos.w = in.clip.w;
    return out;
}

void process_draw_elements(BrewShell& shell, EndianMemory& memory, CPU& cpu, uint32_t mode, uint32_t count, uint32_t type, addr_t indices, const std::string& label) {
    auto& state = qx_state();
    static int trace_element_draws = 0;
    static int trace_element_summaries = 0;
    static int trace_element_results = 0;
    if (mode != kGlTriangles && mode != kGlTriangleStrip && mode != kGlTriangleFan) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw elements skipped: mode 0x%x unsupported\n", label.c_str(), mode);
        }
        return;
    }
    if (count < 3 || (!indices && state.bound_element_buffer == 0)) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw elements skipped: count=%u indices=0x%08x\n", label.c_str(), count, indices);
        }
        return;
    }
    if (state.bound_element_buffer != 0 && state.buffers.find(state.bound_element_buffer) == state.buffers.end()) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw elements skipped: element buffer %u missing\n", label.c_str(), state.bound_element_buffer);
        }
        return;
    }
    if (!vertex_array_has_source(state, state.vertex_array)) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw elements skipped: missing vertex array enabled=%u ptr=0x%08x buffer=%u\n",
                   label.c_str(),
                   state.vertex_array.enabled ? 1u : 0u,
                   state.vertex_array.ptr,
                   state.vertex_array.buffer);
        }
        return;
    }
    if (type != kGlUnsignedByte && type != kGlUnsignedShort) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw elements skipped: index type 0x%x unsupported\n", label.c_str(), type);
        }
        return;
    }

    BrewDisplay* display = shell.get_display();
    if (!display) return;
    BrewBitmap* bmp = display->get_device_bitmap();
    if (!bmp) return;

    const int width = bmp->get_width();
    const int height = bmp->get_height();
    const bool ffp_short_overlay_projection = uses_ffp_short_overlay_projection(state);
    const std::array<float, 16> mvp = ffp_short_overlay_projection ? state.current[1] : multiply_matrix(state.current[1], state.current[0]);
    const float vp_x = static_cast<float>(state.viewport[0]);
    const float vp_y = static_cast<float>(state.viewport[1]);
    const float vp_w = static_cast<float>(state.viewport[2]);
    const float vp_h = static_cast<float>(state.viewport[3]);
    const bool legacy_qx_fixed_texcoords = uses_legacy_qx_fixed_texcoords_for_draw_arrays(state, count, label);
    uint32_t trace_total_tris = 0;
    uint32_t trace_clip_xy = 0;
    uint32_t trace_clip_w = 0;
    uint32_t trace_culled = 0;
    uint32_t trace_alpha = 0;
    uint32_t trace_emitted = 0;

    if (trace_gles_vertices() && trace_element_summaries < 32) {
        const auto tex_it = state.textures.find(state.bound_texture);
        const TextureInfo tex = tex_it != state.textures.end() ? tex_it->second : TextureInfo{};
        const uint32_t sp = cpu.get_reg(REG_SP);
        const uint32_t stack0 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 0) : 0;
        const uint32_t stack1 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 4) : 0;
        const uint32_t stack2 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 8) : 0;
        const uint32_t stack3 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 12) : 0;
        const uint32_t stack4 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 16) : 0;
        const uint32_t stack5 = (sp && sp < 0xFF000000u) ? memory.read_value(sp + 20) : 0;
        const uint32_t r4 = cpu.get_reg(REG_R4);
        const uint32_t r4_obj = (r4 && r4 < 0xFF000000u) ? memory.read_value(r4) : 0;
        const uint32_t r4_limit = (r4_obj && r4_obj < 0xFF000000u) ? memory.read_value(r4_obj + 0x0c) : 0;
        printf("  %s elem-state sp=0x%08x stack=(0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x) r4=0x%08x r4_obj=0x%08x r4_limit=0x%08x elem_buf=%u tex=%u tex_size=%dx%d tex_format=0x%x tex_type=0x%x tex2d=%u depth=%u cull=%u alpha=%u color=(%.3f,%.3f,%.3f,%.3f) ffp_short_overlay=%u viewport=(%d,%d,%d,%d) vertex=(en=%u ptr=0x%08x buf=%u size=%d type=0x%x stride=%d) texcoord=(en=%u ptr=0x%08x buf=%u size=%d type=0x%x stride=%d)\n",
               label.c_str(),
               sp,
               stack0,
               stack1,
               stack2,
               stack3,
               stack4,
               stack5,
               r4,
               r4_obj,
               r4_limit,
               state.bound_element_buffer,
               state.bound_texture,
               tex.width,
               tex.height,
               tex.format,
               tex.type,
               state.texture_2d_enabled ? 1u : 0u,
               state.depth_test_enabled ? 1u : 0u,
               state.cull_face_enabled ? 1u : 0u,
               state.alpha_test_enabled ? 1u : 0u,
               state.current_color[0], state.current_color[1], state.current_color[2], state.current_color[3],
               ffp_short_overlay_projection ? 1u : 0u,
               state.viewport[0],
               state.viewport[1],
               state.viewport[2],
               state.viewport[3],
               state.vertex_array.enabled ? 1u : 0u,
               state.vertex_array.ptr,
               state.vertex_array.buffer,
               state.vertex_array.size,
               state.vertex_array.type,
               state.vertex_array.stride,
               state.texcoord_array.enabled ? 1u : 0u,
               state.texcoord_array.ptr,
               state.texcoord_array.buffer,
               state.texcoord_array.size,
               state.texcoord_array.type,
               state.texcoord_array.stride);
        printf("  %s elem-matrix mv=[%.9g %.9g %.9g %.9g %.9g %.9g %.9g] prj=[%.9g %.9g %.9g %.9g %.9g %.9g %.9g] tex=[%.9g %.9g %.9g %.9g %.9g %.9g %.9g]\n",
               label.c_str(),
               state.current[0][0], state.current[0][5], state.current[0][10], state.current[0][12], state.current[0][13], state.current[0][14], state.current[0][15],
               state.current[1][0], state.current[1][5], state.current[1][10], state.current[1][12], state.current[1][13], state.current[1][14], state.current[1][15],
               state.current[2][0], state.current[2][5], state.current[2][10], state.current[2][12], state.current[2][13], state.current[2][14], state.current[2][15]);
        ++trace_element_summaries;
    }

    auto emit_triangle = [&](uint32_t element0, uint32_t element1, uint32_t element2) {
        ++trace_total_tris;
        const uint32_t i0 = read_index(memory, state, indices, type, element0);
        const uint32_t i1 = read_index(memory, state, indices, type, element1);
        const uint32_t i2 = read_index(memory, state, indices, type, element2);
        const std::array<ClipVertex, 3> clip_tri = {
            make_clip_vertex_from_index(memory, state, i0, mvp, legacy_qx_fixed_texcoords),
            make_clip_vertex_from_index(memory, state, i1, mvp, legacy_qx_fixed_texcoords),
            make_clip_vertex_from_index(memory, state, i2, mvp, legacy_qx_fixed_texcoords)
        };
        if (trace_gles_vertices() && trace_element_draws < 32) {
            const auto tex_it = state.textures.find(state.bound_texture);
            const TextureInfo tex = tex_it != state.textures.end() ? tex_it->second : TextureInfo{};
            const auto raw_u0 = read_array_raw_component(memory, state, state.texcoord_array, i0, 0);
            const auto raw_v0 = read_array_raw_component(memory, state, state.texcoord_array, i0, 1);
            const auto raw_u1 = read_array_raw_component(memory, state, state.texcoord_array, i1, 0);
            const auto raw_v1 = read_array_raw_component(memory, state, state.texcoord_array, i1, 1);
            const auto raw_u2 = read_array_raw_component(memory, state, state.texcoord_array, i2, 0);
            const auto raw_v2 = read_array_raw_component(memory, state, state.texcoord_array, i2, 1);
            const auto raw_x0 = read_array_raw_component(memory, state, state.vertex_array, i0, 0);
            const auto raw_y0 = read_array_raw_component(memory, state, state.vertex_array, i0, 1);
            const auto raw_z0 = read_array_raw_component(memory, state, state.vertex_array, i0, 2);
            const auto raw_x1 = read_array_raw_component(memory, state, state.vertex_array, i1, 0);
            const auto raw_y1 = read_array_raw_component(memory, state, state.vertex_array, i1, 1);
            const auto raw_z1 = read_array_raw_component(memory, state, state.vertex_array, i1, 2);
            const auto raw_x2 = read_array_raw_component(memory, state, state.vertex_array, i2, 0);
            const auto raw_y2 = read_array_raw_component(memory, state, state.vertex_array, i2, 1);
            const auto raw_z2 = read_array_raw_component(memory, state, state.vertex_array, i2, 2);
            const Vertex screen0 = project_clip_vertex(clip_tri[0], vp_x, vp_y, vp_w, vp_h);
            const Vertex screen1 = project_clip_vertex(clip_tri[1], vp_x, vp_y, vp_w, vp_h);
            const Vertex screen2 = project_clip_vertex(clip_tri[2], vp_x, vp_y, vp_w, vp_h);
            printf("  %s elem tri idx=(%u,%u,%u) tex=%u tex_size=%dx%d pos_raw=(%08x,%08x,%08x)(%08x,%08x,%08x)(%08x,%08x,%08x) uv_raw=(%08x,%08x)(%08x,%08x)(%08x,%08x) uv=(%.6f,%.6f)(%.6f,%.6f)(%.6f,%.6f) color=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f) sdl_uv=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f) clip=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f) screen=(%.2f,%.2f)(%.2f,%.2f)(%.2f,%.2f)\n",
                   label.c_str(), i0, i1, i2,
                   state.bound_texture, tex.width, tex.height,
                   raw_x0, raw_y0, raw_z0, raw_x1, raw_y1, raw_z1, raw_x2, raw_y2, raw_z2,
                   raw_u0, raw_v0, raw_u1, raw_v1, raw_u2, raw_v2,
                   clip_tri[0].texcoord.u, clip_tri[0].texcoord.v,
                   clip_tri[1].texcoord.u, clip_tri[1].texcoord.v,
                   clip_tri[2].texcoord.u, clip_tri[2].texcoord.v,
                   clip_tri[0].color.x, clip_tri[0].color.y, clip_tri[0].color.z, clip_tri[0].color.w,
                   clip_tri[1].color.x, clip_tri[1].color.y, clip_tri[1].color.z, clip_tri[1].color.w,
                   clip_tri[2].color.x, clip_tri[2].color.y, clip_tri[2].color.z, clip_tri[2].color.w,
                   clip_tri[0].texcoord.u * static_cast<float>(tex.width),
                   clip_tri[0].texcoord.v * static_cast<float>(tex.height),
                   clip_tri[1].texcoord.u * static_cast<float>(tex.width),
                   clip_tri[1].texcoord.v * static_cast<float>(tex.height),
                   clip_tri[2].texcoord.u * static_cast<float>(tex.width),
                   clip_tri[2].texcoord.v * static_cast<float>(tex.height),
                   clip_tri[0].clip.x, clip_tri[0].clip.y, clip_tri[0].clip.z, clip_tri[0].clip.w,
                   clip_tri[1].clip.x, clip_tri[1].clip.y, clip_tri[1].clip.z, clip_tri[1].clip.w,
                   clip_tri[2].clip.x, clip_tri[2].clip.y, clip_tri[2].clip.z, clip_tri[2].clip.w,
                   screen0.pos.x, screen0.pos.y,
                   screen1.pos.x, screen1.pos.y,
                   screen2.pos.x, screen2.pos.y);
            ++trace_element_draws;
        }
        const std::vector<ClipVertex> clipped = clip_to_view_volume(clip_tri);
        if (clipped.size() < 3) {
            ++trace_clip_w;
            return;
        }
        const Vertex base = project_clip_vertex(clipped[0], vp_x, vp_y, vp_w, vp_h);
        for (size_t i = 1; i + 1 < clipped.size(); ++i) {
            if (triangle_culled(state, clipped[0], clipped[i], clipped[i + 1])) {
                ++trace_culled;
                continue;
            }
            const Vertex v1 = project_clip_vertex(clipped[i], vp_x, vp_y, vp_w, vp_h);
            const Vertex v2 = project_clip_vertex(clipped[i + 1], vp_x, vp_y, vp_w, vp_h);
            if (!alpha_test_passes(state, base.color.w) ||
                !alpha_test_passes(state, v1.color.w) ||
                !alpha_test_passes(state, v2.color.w)) {
                ++trace_alpha;
                continue;
            }
            draw_triangle(memory, shell, base, v1, v2, width, height);
            ++trace_emitted;
        }
    };

    if (mode == kGlTriangles) {
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            emit_triangle(i + 0, i + 1, i + 2);
        }
    } else if (mode == kGlTriangleStrip) {
        for (uint32_t i = 0; i + 2 < count; ++i) {
            if ((i & 1u) == 0) {
                emit_triangle(i + 0, i + 1, i + 2);
            } else {
                emit_triangle(i + 1, i + 0, i + 2);
            }
        }
    } else {
        for (uint32_t i = 1; i + 1 < count; ++i) {
            emit_triangle(0, i, i + 1);
        }
    }

    if (trace_gles_vertices() && trace_element_results < 64) {
        printf("  %s elem-result tris=%u emitted=%u clip_xy=%u clip_w=%u cull=%u alpha=%u\n",
               label.c_str(),
               trace_total_tris,
               trace_emitted,
               trace_clip_xy,
               trace_clip_w,
               trace_culled,
               trace_alpha);
        ++trace_element_results;
    }
}

void process_draw_call(BrewShell& shell, EndianMemory& memory, CPU& cpu, const std::string& label) {
    auto& state = qx_state();
    static int trace_draws = 0;
    static int trace_draw_summaries = 0;
    static int trace_draw_results = 0;

    BrewDisplay* display = shell.get_display();
    if (!display) return;
    BrewBitmap* bmp = display->get_device_bitmap();
    if (!bmp) return;

    int width = bmp->get_width();
    int height = bmp->get_height();

    uint32_t mode = cpu.get_reg(REG_R0);
    uint32_t first = cpu.get_reg(REG_R1);
    uint32_t count = cpu.get_reg(REG_R2);

    if (mode != kGlTriangles && mode != kGlTriangleStrip && mode != kGlTriangleFan) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw arrays skipped: mode 0x%x unsupported\n", label.c_str(), mode);
        }
        return;
    }
    if (count < 3 || !vertex_array_has_source(state, state.vertex_array)) {
        if (trace_qxgl_calls() || trace_gles_vertices()) {
            printf("  %s draw arrays skipped: count=%u vertex enabled=%u ptr=0x%08x buffer=%u\n",
                   label.c_str(),
                   count,
                   state.vertex_array.enabled ? 1u : 0u,
                   state.vertex_array.ptr,
                   state.vertex_array.buffer);
        }
        return;
    }

    float vp_x = static_cast<float>(state.viewport[0]);
    float vp_y = static_cast<float>(state.viewport[1]);
    float vp_w = static_cast<float>(state.viewport[2]);
    float vp_h = static_cast<float>(state.viewport[3]);
    std::array<float, 16> modelview = state.current[0];
    std::array<float, 16> projection = state.current[1];
    const bool legacy_centered_pixels = uses_legacy_qx_centered_pixel_projection(state, mode);
    const bool ffp_short_overlay_projection = uses_ffp_short_overlay_projection(state);
    if (legacy_centered_pixels) {
        projection = ortho_matrix(-vp_w * 0.5f, vp_w * 0.5f, -vp_h * 0.5f, vp_h * 0.5f, -1.0f, 1.0f);
    }
    std::array<float, 16> mvp = ffp_short_overlay_projection ? projection : multiply_matrix(projection, modelview);
    const bool legacy_qx_fixed_texcoords = uses_legacy_qx_fixed_texcoords_for_draw_arrays(state, count, label);
    uint32_t trace_total_tris = 0;
    uint32_t trace_clip_xy = 0;
    uint32_t trace_clip_w = 0;
    uint32_t trace_culled = 0;
    uint32_t trace_alpha = 0;
    uint32_t trace_emitted = 0;

    if (trace_gles_vertices() && (trace_draw_summaries < 32 || std::getenv("ZEEMU_TRACE_GLES_TEXT")) && state.texcoord_array.enabled && state.texcoord_array.ptr) {
        uint32_t raw_min_u = 0xffffffffu;
        uint32_t raw_min_v = 0xffffffffu;
        uint32_t raw_max_u = 0;
        uint32_t raw_max_v = 0;
        float min_u = 1.0e30f;
        float min_v = 1.0e30f;
        float max_u = -1.0e30f;
        float max_v = -1.0e30f;
        for (uint32_t i = first; i < first + count; ++i) {
            const uint32_t ru = read_array_raw_component(memory, state, state.texcoord_array, i, 0);
            const uint32_t rv = read_array_raw_component(memory, state, state.texcoord_array, i, 1);
            raw_min_u = std::min(raw_min_u, ru);
            raw_min_v = std::min(raw_min_v, rv);
            raw_max_u = std::max(raw_max_u, ru);
            raw_max_v = std::max(raw_max_v, rv);
            const auto values = read_texcoord_array_vertex(memory, state, state.texcoord_array, i, legacy_qx_fixed_texcoords);
            if (values.size() >= 2) {
                const Vec2 raw_uv(values[0], values[1]);
                const Vec2 uv = transform_texcoord(raw_uv, state.current[2]);
                min_u = std::min(min_u, uv.u);
                min_v = std::min(min_v, uv.v);
                max_u = std::max(max_u, uv.u);
                max_v = std::max(max_v, uv.v);
            }
        }
        printf("  %s uv-summary first=%u count=%u tex_ptr=0x%08x tex_type=0x%x stride=%d raw_u=[0x%08x,0x%08x] raw_v=[0x%08x,0x%08x] uv=[%.6f..%.6f, %.6f..%.6f]\n",
               label.c_str(), first, count, state.texcoord_array.ptr, state.texcoord_array.type, state.texcoord_array.stride,
               raw_min_u, raw_max_u, raw_min_v, raw_max_v, min_u, max_u, min_v, max_v);
        printf("  %s draw-state tex=%u tex2d=%u depth=%u cull=%u cull_face=0x%x front=0x%x alpha=%u color=(%.3f,%.3f,%.3f,%.3f) legacy_centered=%u ffp_short_overlay=%u viewport=(%d,%d,%d,%d) mv=[%.3f %.3f %.3f %.3f %.3f %.3f %.3f] prj=[%.3f %.3f %.3f %.3f %.3f %.3f %.3f] texm=[%.3f %.3f %.3f %.3f %.3f %.3f %.3f]\n",
               label.c_str(),
               state.bound_texture,
               state.texture_2d_enabled ? 1u : 0u,
               state.depth_test_enabled ? 1u : 0u,
               state.cull_face_enabled ? 1u : 0u,
               state.cull_face,
               state.front_face,
               state.alpha_test_enabled ? 1u : 0u,
               state.current_color[0], state.current_color[1], state.current_color[2], state.current_color[3],
               legacy_centered_pixels ? 1u : 0u,
               ffp_short_overlay_projection ? 1u : 0u,
               state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3],
               state.current[0][0], state.current[0][5], state.current[0][10], state.current[0][12], state.current[0][13], state.current[0][14], state.current[0][15],
               projection[0], projection[5], projection[10], projection[12], projection[13], projection[14], projection[15],
               state.current[2][0], state.current[2][5], state.current[2][10], state.current[2][12], state.current[2][13], state.current[2][14], state.current[2][15]);
        ++trace_draw_summaries;
    }

    auto emit_triangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        ++trace_total_tris;
        const std::array<ClipVertex, 3> clip_tri = {
            make_clip_vertex_from_index(memory, state, i0, mvp, legacy_qx_fixed_texcoords),
            make_clip_vertex_from_index(memory, state, i1, mvp, legacy_qx_fixed_texcoords),
            make_clip_vertex_from_index(memory, state, i2, mvp, legacy_qx_fixed_texcoords)
        };
        static int trace_text_tris = 0;
        const bool trace_text_tri = std::getenv("ZEEMU_TRACE_GLES_TEXT") &&
                                    state.bound_texture == 2 && trace_text_tris < 48;
        if (trace_gles_vertices() && (trace_draws < 24 || trace_text_tri)) {
            const uint32_t uv0_raw0 = read_array_raw_component(memory, state, state.texcoord_array, i0, 0);
            const uint32_t uv0_raw1 = read_array_raw_component(memory, state, state.texcoord_array, i0, 1);
            const uint32_t uv1_raw0 = read_array_raw_component(memory, state, state.texcoord_array, i1, 0);
            const uint32_t uv1_raw1 = read_array_raw_component(memory, state, state.texcoord_array, i1, 1);
            const uint32_t uv2_raw0 = read_array_raw_component(memory, state, state.texcoord_array, i2, 0);
            const uint32_t uv2_raw1 = read_array_raw_component(memory, state, state.texcoord_array, i2, 1);
            printf("  %s tri tex=%u idx=(%u,%u,%u) tex_ptr=0x%08x tex_type=0x%x tex_stride=%d uvraw0=(0x%08x,0x%08x) uv0=(%.6f,%.6f) uvraw1=(0x%08x,0x%08x) uv1=(%.6f,%.6f) uvraw2=(0x%08x,0x%08x) uv2=(%.6f,%.6f) color=(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f)(%.3f,%.3f,%.3f,%.3f) clip0=(%.3f,%.3f,%.3f,%.3f) clip1=(%.3f,%.3f,%.3f,%.3f) clip2=(%.3f,%.3f,%.3f,%.3f)\n",
                   label.c_str(),
                   state.bound_texture,
                   i0, i1, i2,
                   state.texcoord_array.ptr,
                   state.texcoord_array.type,
                   state.texcoord_array.stride,
                   uv0_raw0, uv0_raw1,
                   clip_tri[0].texcoord.u, clip_tri[0].texcoord.v,
                   uv1_raw0, uv1_raw1,
                   clip_tri[1].texcoord.u, clip_tri[1].texcoord.v,
                   uv2_raw0, uv2_raw1,
                   clip_tri[2].texcoord.u, clip_tri[2].texcoord.v,
                   clip_tri[0].color.x, clip_tri[0].color.y, clip_tri[0].color.z, clip_tri[0].color.w,
                   clip_tri[1].color.x, clip_tri[1].color.y, clip_tri[1].color.z, clip_tri[1].color.w,
                   clip_tri[2].color.x, clip_tri[2].color.y, clip_tri[2].color.z, clip_tri[2].color.w,
                   clip_tri[0].clip.x, clip_tri[0].clip.y, clip_tri[0].clip.z, clip_tri[0].clip.w,
                   clip_tri[1].clip.x, clip_tri[1].clip.y, clip_tri[1].clip.z, clip_tri[1].clip.w,
                   clip_tri[2].clip.x, clip_tri[2].clip.y, clip_tri[2].clip.z, clip_tri[2].clip.w);
            if (trace_text_tri) {
                ++trace_text_tris;
            } else {
                ++trace_draws;
            }
        }
        const std::vector<ClipVertex> clipped = clip_to_view_volume(clip_tri);
        if (clipped.size() < 3) {
            ++trace_clip_w;
            return;
        }
        const Vertex base = project_clip_vertex(clipped[0], vp_x, vp_y, vp_w, vp_h);
        for (size_t i = 1; i + 1 < clipped.size(); ++i) {
            if (triangle_culled(state, clipped[0], clipped[i], clipped[i + 1])) {
                ++trace_culled;
                continue;
            }
            const Vertex v1 = project_clip_vertex(clipped[i], vp_x, vp_y, vp_w, vp_h);
            const Vertex v2 = project_clip_vertex(clipped[i + 1], vp_x, vp_y, vp_w, vp_h);
            if (!alpha_test_passes(state, base.color.w) ||
                !alpha_test_passes(state, v1.color.w) ||
                !alpha_test_passes(state, v2.color.w)) {
                ++trace_alpha;
                continue;
            }
            draw_triangle(memory, shell, base, v1, v2, width, height);
            ++trace_emitted;
        }
    };

    if (mode == kGlTriangles) {
        for (uint32_t i = first; i + 2 < first + count; i += 3) {
            emit_triangle(i + 0, i + 1, i + 2);
        }
    } else if (mode == kGlTriangleStrip) {
        for (uint32_t i = 0; i + 2 < count; ++i) {
            const uint32_t a = first + i;
            const uint32_t b = first + i + 1;
            const uint32_t c = first + i + 2;
            if ((i & 1u) == 0) {
                emit_triangle(a, b, c);
            } else {
                emit_triangle(b, a, c);
            }
        }
    } else {
        for (uint32_t i = 1; i + 1 < count; ++i) {
            emit_triangle(first, first + i, first + i + 1);
        }
    }

    if (trace_gles_vertices() && trace_draw_results < 64) {
        printf("  %s draw-result tris=%u emitted=%u clip_xy=%u clip_w=%u cull=%u alpha=%u\n",
               label.c_str(),
               trace_total_tris,
               trace_emitted,
               trace_clip_xy,
               trace_clip_w,
               trace_culled,
               trace_alpha);
        ++trace_draw_results;
    }
}



} // namespace qxgl
