#ifndef ZEEMU_BREW_QXGL_STATE_H_
#define ZEEMU_BREW_QXGL_STATE_H_

#include "brew/BrewBitmap.h"
#include "brew/BrewDisplay.h"
#include "brew/BrewShell.h"
#include "cpu/core/CPU.h"
#include "graphics/RenderBackend.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace qxgl {
constexpr uint32_t kGlModelview = 0x1700;
constexpr uint32_t kGlProjection = 0x1701;
constexpr uint32_t kGlTexture = 0x1702;
constexpr uint32_t kGlViewport = 0x0BA2;
constexpr uint32_t kGlModelviewMatrix = 0x0BA6;
constexpr uint32_t kGlProjectionMatrix = 0x0BA7;
constexpr uint32_t kGlTextureMatrix = 0x0BA8;
constexpr uint32_t kGlModelviewStackDepth = 0x0BA3;
constexpr uint32_t kGlProjectionStackDepth = 0x0BA4;
constexpr uint32_t kGlTextureStackDepth = 0x0BA5;
constexpr uint32_t kGlScissorBox = 0x0C10;
constexpr uint32_t kGlUnpackAlignment = 0x0CF5;
constexpr uint32_t kGlPackAlignment = 0x0D05;
constexpr uint32_t kGlMaxTextureSize = 0x0D33;
constexpr uint32_t kGlMaxViewportDims = 0x0D3A;
constexpr uint32_t kGlRedBits = 0x0D52;
constexpr uint32_t kGlGreenBits = 0x0D53;
constexpr uint32_t kGlBlueBits = 0x0D54;
constexpr uint32_t kGlAlphaBits = 0x0D55;
constexpr uint32_t kGlDepthBits = 0x0D56;
constexpr uint32_t kGlStencilBits = 0x0D57;
constexpr uint32_t kGlArrayBuffer = 0x8892;
constexpr uint32_t kGlElementArrayBuffer = 0x8893;
constexpr uint32_t kGlArrayBufferBinding = 0x8894;
constexpr uint32_t kGlElementArrayBufferBinding = 0x8895;
constexpr uint32_t kGlBufferSize = 0x8764;
constexpr uint32_t kGlBufferUsage = 0x8765;
constexpr uint32_t kGlTexture2D = 0x0DE1;
constexpr uint32_t kGlTextureBinding2D = 0x8069;
constexpr uint32_t kGlBlend = 0x0BE2;
constexpr uint32_t kGlDither = 0x0BD0;
constexpr uint32_t kGlSampleBuffers = 0x80A8;
constexpr uint32_t kGlSamples = 0x80A9;
constexpr uint32_t kGlMaxElementsVertices = 0x80E8;
constexpr uint32_t kGlMaxElementsIndices = 0x80E9;
constexpr uint32_t kGlTextureMagFilter = 0x2800;
constexpr uint32_t kGlTextureMinFilter = 0x2801;
constexpr uint32_t kGlTextureWrapS = 0x2802;
constexpr uint32_t kGlTextureWrapT = 0x2803;
constexpr uint32_t kGlTextureCropRectOes = 0x8B9D;
constexpr uint32_t kGlNearest = 0x2600;
constexpr uint32_t kGlLinear = 0x2601;
constexpr uint32_t kGlRepeat = 0x2901;
constexpr uint32_t kGlClampToEdge = 0x812F;
constexpr uint32_t kGlTextureEnv = 0x2300;
constexpr uint32_t kGlTextureEnvMode = 0x2200;
constexpr uint32_t kGlTextureEnvColor = 0x2201;
constexpr uint32_t kGlModulate = 0x2100;
constexpr uint32_t kGlDecal = 0x2101;
constexpr uint32_t kGlBlendEnv = 0x0BE2;
constexpr uint32_t kGlReplace = 0x1E01;
constexpr uint32_t kGlAdd = 0x0104;
constexpr uint32_t kGlSubtract = 0x84E7;
constexpr uint32_t kGlCombine = 0x8570;
constexpr uint32_t kGlCombineRgb = 0x8571;
constexpr uint32_t kGlCombineAlpha = 0x8572;
constexpr uint32_t kGlRgbScale = 0x8573;
constexpr uint32_t kGlAddSigned = 0x8574;
constexpr uint32_t kGlInterpolate = 0x8575;
constexpr uint32_t kGlConstant = 0x8576;
constexpr uint32_t kGlPrimaryColor = 0x8577;
constexpr uint32_t kGlPrevious = 0x8578;
constexpr uint32_t kGlSrc0Rgb = 0x8580;
constexpr uint32_t kGlSrc1Rgb = 0x8581;
constexpr uint32_t kGlSrc2Rgb = 0x8582;
constexpr uint32_t kGlSrc0Alpha = 0x8588;
constexpr uint32_t kGlSrc1Alpha = 0x8589;
constexpr uint32_t kGlSrc2Alpha = 0x858A;
constexpr uint32_t kGlOperand0Rgb = 0x8590;
constexpr uint32_t kGlOperand1Rgb = 0x8591;
constexpr uint32_t kGlOperand2Rgb = 0x8592;
constexpr uint32_t kGlOperand0Alpha = 0x8598;
constexpr uint32_t kGlOperand1Alpha = 0x8599;
constexpr uint32_t kGlOperand2Alpha = 0x859A;
constexpr uint32_t kGlAlphaScale = 0x0D1C;
constexpr uint32_t kGlDot3Rgb = 0x86AE;
constexpr uint32_t kGlDot3Rgba = 0x86AF;
constexpr uint32_t kGlVertexArray = 0x8074;
constexpr uint32_t kGlNormalArray = 0x8075;
constexpr uint32_t kGlColorArray = 0x8076;
constexpr uint32_t kGlTextureCoordArray = 0x8078;
constexpr uint32_t kGlRgb = 0x1907;
constexpr uint32_t kGlRgba = 0x1908;
constexpr uint32_t kGlDepthTest = 0x0B71;
constexpr uint32_t kGlStencilTest = 0x0B90;
constexpr uint32_t kGlCullFace = 0x0B44;
constexpr uint32_t kGlAlphaTest = 0x0BC0;
constexpr uint32_t kGlFog = 0x0B60;
constexpr uint32_t kGlFogDensity = 0x0B62;
constexpr uint32_t kGlFogStart = 0x0B63;
constexpr uint32_t kGlFogEnd = 0x0B64;
constexpr uint32_t kGlFogMode = 0x0B65;
constexpr uint32_t kGlFogColor = 0x0B66;
constexpr uint32_t kGlLighting = 0x0B50;
constexpr uint32_t kGlLight0 = 0x4000;
constexpr uint32_t kGlLightModelTwoSide = 0x0B52;
constexpr uint32_t kGlLightModelAmbient = 0x0B53;
constexpr uint32_t kGlEmission = 0x1600;
constexpr uint32_t kGlAmbient = 0x1200;
constexpr uint32_t kGlDiffuse = 0x1201;
constexpr uint32_t kGlSpecular = 0x1202;
constexpr uint32_t kGlPosition = 0x1203;
constexpr uint32_t kGlSpotDirection = 0x1204;
constexpr uint32_t kGlSpotExponent = 0x1205;
constexpr uint32_t kGlSpotCutoff = 0x1206;
constexpr uint32_t kGlConstantAttenuation = 0x1207;
constexpr uint32_t kGlLinearAttenuation = 0x1208;
constexpr uint32_t kGlQuadraticAttenuation = 0x1209;
constexpr uint32_t kGlShininess = 0x1601;
constexpr uint32_t kGlAmbientAndDiffuse = 0x1602;
constexpr uint32_t kGlFront = 0x0404;
constexpr uint32_t kGlBack = 0x0405;
constexpr uint32_t kGlFrontAndBack = 0x0408;
constexpr uint32_t kGlCw = 0x0900;
constexpr uint32_t kGlCcw = 0x0901;
constexpr uint32_t kGlNever = 0x0200;
constexpr uint32_t kGlLess = 0x0201;
constexpr uint32_t kGlEqual = 0x0202;
constexpr uint32_t kGlLequal = 0x0203;
constexpr uint32_t kGlGreater = 0x0204;
constexpr uint32_t kGlNotequal = 0x0205;
constexpr uint32_t kGlGequal = 0x0206;
constexpr uint32_t kGlAlways = 0x0207;
constexpr uint32_t kGlZero = 0x0000;
constexpr uint32_t kGlOne = 0x0001;
constexpr uint32_t kGlSrcColor = 0x0300;
constexpr uint32_t kGlOneMinusSrcColor = 0x0301;
constexpr uint32_t kGlSrcAlpha = 0x0302;
constexpr uint32_t kGlOneMinusSrcAlpha = 0x0303;
constexpr uint32_t kGlFlat = 0x1D00;
constexpr uint32_t kGlSmooth = 0x1D01;
constexpr uint32_t kGlPerspectiveCorrectionHint = 0x0C50;
constexpr uint32_t kGlPointSmoothHint = 0x0C51;
constexpr uint32_t kGlLineSmoothHint = 0x0C52;
constexpr uint32_t kGlFogHint = 0x0C54;
constexpr uint32_t kGlGenerateMipmapHint = 0x8192;
constexpr uint32_t kGlDontCare = 0x1100;
constexpr uint32_t kGlFastest = 0x1101;
constexpr uint32_t kGlNicest = 0x1102;
constexpr uint32_t kGlTexture0 = 0x84C0;
constexpr uint32_t kGlTexture1 = 0x84C1;
constexpr uint32_t kGlActiveTexture = 0x84E0;
constexpr uint32_t kGlClientActiveTexture = 0x84E1;
constexpr uint32_t kGlMaxTextureUnits = 0x84E2;
constexpr uint32_t kGlCompressedTextureFormats = 0x86A3;
constexpr uint32_t kGlNumCompressedTextureFormats = 0x86A2;
constexpr uint32_t kGlFramebufferBinding = 0x8CA6;
constexpr uint32_t kGlMaxVaryingVectors = 0x8DFC;
constexpr uint32_t kGlMajorVersion = 0x821B;
constexpr uint32_t kGlMinorVersion = 0x821C;
constexpr uint32_t kGlFloatAsFixed = 65536.0f;
constexpr uint32_t kGlTriangles = 0x0004;
constexpr uint32_t kGlTriangleStrip = 0x0005;
constexpr uint32_t kGlTriangleFan = 0x0006;
constexpr uint32_t kGlByte = 0x1400;
constexpr uint32_t kGlUnsignedByte = 0x1401;
constexpr uint32_t kGlShort = 0x1402;
constexpr uint32_t kGlUnsignedShort = 0x1403;
constexpr uint32_t kGlFloat = 0x1406;
constexpr uint32_t kGlFixed = 0x140C;
constexpr uint32_t kGlUnsignedShort565 = 0x8363;

struct VertexArray {
    bool enabled = false;
    addr_t ptr = 0;
    uint32_t buffer = 0;
    int size = 0;
    uint32_t type = 0;
    int stride = 0;
};

struct TextureInfo {
    int width = 0;
    int height = 0;
    uint32_t format = 0;
    uint32_t type = 0;
    uint32_t min_filter = kGlNearest;
    uint32_t mag_filter = kGlNearest;
    uint32_t wrap_s = kGlRepeat;
    uint32_t wrap_t = kGlRepeat;
    bool has_crop_rect = false;
    std::array<int32_t, 4> crop_rect = {0, 0, 0, 0};
};

struct TextureEnvInfo {
    uint32_t mode = kGlModulate;
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t combine_rgb = kGlModulate;
    uint32_t combine_alpha = kGlModulate;
    uint32_t src_rgb[3] = {kGlTexture, kGlPrevious, kGlConstant};
    uint32_t src_alpha[3] = {kGlTexture, kGlPrevious, kGlConstant};
    uint32_t operand_rgb[3] = {kGlSrcColor, kGlSrcColor, kGlSrcAlpha};
    uint32_t operand_alpha[3] = {kGlSrcAlpha, kGlSrcAlpha, kGlSrcAlpha};
    float rgb_scale = 1.0f;
    float alpha_scale = 1.0f;
};

struct MaterialInfo {
    float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float shininess = 0.0f;
};

struct LightInfo {
    float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float diffuse[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float position[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float spot_direction[3] = {0.0f, 0.0f, -1.0f};
    float spot_exponent = 0.0f;
    float spot_cutoff = 180.0f;
    float constant_attenuation = 1.0f;
    float linear_attenuation = 0.0f;
    float quadratic_attenuation = 0.0f;
};

struct QxGpuState {
    std::array<std::array<float, 16>, 3> current{};
    std::array<std::vector<std::array<float, 16>>, 3> stacks;
    uint32_t matrix_mode = kGlModelview;
    int32_t viewport[4] = {0, 0, 640, 480};
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clear_depth = 1.0f;
    uint32_t clear_stencil = 0;
    uint32_t next_buffer_id = 1;
    uint32_t next_texture_id = 1;
    uint32_t bound_array_buffer = 0;
    uint32_t bound_element_buffer = 0;
    uint32_t point_size_pointer = 0;
    std::unordered_map<uint32_t, std::vector<uint8_t>> buffers;

    VertexArray vertex_array;
    VertexArray color_array;
    VertexArray texcoord_array;
    std::array<VertexArray, 2> texcoord_arrays;
    VertexArray normal_array;

    uint32_t bound_texture = 0;
    std::array<uint32_t, 2> bound_textures = {0, 0};
    std::unordered_map<uint32_t, TextureInfo> textures;
    uint32_t active_texture_unit = 0;
    uint32_t client_active_texture_unit = 0;
    uint32_t unpack_alignment = 4;
    std::array<TextureEnvInfo, 2> texture_env_units;
    uint32_t texture_env_mode = kGlModulate;
    float texture_env_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t texture_env_combine_rgb = kGlModulate;
    uint32_t texture_env_combine_alpha = kGlModulate;
    uint32_t texture_env_src_rgb[3] = {kGlTexture, kGlPrevious, kGlConstant};
    uint32_t texture_env_src_alpha[3] = {kGlTexture, kGlPrevious, kGlConstant};
    uint32_t texture_env_operand_rgb[3] = {kGlSrcColor, kGlSrcColor, kGlSrcAlpha};
    uint32_t texture_env_operand_alpha[3] = {kGlSrcAlpha, kGlSrcAlpha, kGlSrcAlpha};
    float texture_env_rgb_scale = 1.0f;
    float texture_env_alpha_scale = 1.0f;
    std::array<std::array<float, 4>, 2> current_texcoord = {{
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    }};
    bool texture_2d_enabled = false;
    std::array<bool, 2> texture_2d_enabled_units = {false, false};
    bool dither_enabled = true;
    bool depth_test_enabled = false;
    bool depth_mask = true;
    uint32_t depth_func = kGlLess;
    float depth_range_near = 0.0f;
    float depth_range_far = 1.0f;
    bool stencil_test_enabled = false;
    bool cull_face_enabled = false;
    uint32_t cull_face = kGlBack;
    uint32_t front_face = kGlCcw;
    bool alpha_test_enabled = false;
    uint32_t alpha_func = kGlAlways;
    float alpha_ref = 0.0f;
    bool blend_enabled = false;
    uint32_t blend_src = kGlOne;
    uint32_t blend_dst = kGlZero;
    uint32_t shade_model = kGlSmooth;
    std::unordered_map<uint32_t, uint32_t> hints;
    bool fog_enabled = false;
    uint32_t fog_mode = kGlLinear;
    float fog_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float fog_density = 1.0f;
    float fog_start = 0.0f;
    float fog_end = 1.0f;
    bool lighting_enabled = false;
    std::array<bool, 8> light_enabled = {false, false, false, false, false, false, false, false};
    bool light_model_two_side = false;
    float light_model_ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    std::array<LightInfo, 8> lights;
    MaterialInfo front_material;
    MaterialInfo back_material;
    float current_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    QxGpuState() {
        for (auto& mat : current) {
            mat.fill(0.0f);
            mat[0] = 1.0f;
            mat[5] = 1.0f;
            mat[10] = 1.0f;
            mat[15] = 1.0f;
        }
        for (int i = 0; i < 4; ++i) {
            lights[0].diffuse[i] = 1.0f;
            lights[0].specular[i] = 1.0f;
        }
    }
};

inline QxGpuState& qx_state() {
    static QxGpuState state;
    return state;
}

struct TexturePayloadHint {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bits_per_pixel = 0;
    bool origin_top = true;
};

inline std::unordered_map<addr_t, TexturePayloadHint>& texture_payload_hints() {
    static std::unordered_map<addr_t, TexturePayloadHint> hints;
    return hints;
}

inline std::vector<TexturePayloadHint>& pending_texture_payload_hints() {
    static std::vector<TexturePayloadHint> hints;
    return hints;
}

inline uint32_t next_power_of_two(uint32_t value) {
    if (value <= 1) {
        return 1;
    }
    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

inline bool trace_gles_vertices() {
    static const bool enabled = std::getenv("ZEEMU_TRACE_GLES_VERTS") != nullptr;
    return enabled;
}

inline bool trace_gles_textures() {
    static const bool enabled = std::getenv("ZEEMU_TRACE_GLES_TEXTURES") != nullptr;
    return enabled;
}

inline bool trace_qxgl_calls() {
    static const bool enabled = std::getenv("ZEEMU_TRACE_QXGL") != nullptr;
    return enabled;
}

inline int array_component_size(uint32_t type) {
    return (type == kGlByte || type == kGlUnsignedByte) ? 1 :
           ((type == kGlShort || type == kGlUnsignedShort) ? 2 : 4);
}

inline bool read_buffer_value(const std::vector<uint8_t>& bytes, size_t offset, int size, uint32_t& out) {
    if (size <= 0 || offset + static_cast<size_t>(size) > bytes.size()) {
        out = 0;
        return false;
    }
    out = 0;
    for (int i = 0; i < size; ++i) {
        out |= static_cast<uint32_t>(bytes[offset + static_cast<size_t>(i)]) << (8 * i);
    }
    return true;
}

inline bool vertex_array_has_source(const QxGpuState& state, const VertexArray& va) {
    if (!va.enabled) {
        return false;
    }
    if (va.buffer != 0) {
        return state.buffers.find(va.buffer) != state.buffers.end();
    }
    return va.ptr != 0;
}

inline uint32_t read_array_raw_component(EndianMemory& memory, const QxGpuState& state, const VertexArray& va, uint32_t index, int component) {
    if (!va.enabled || component < 0 || component >= va.size) {
        return 0;
    }
    const int component_size = array_component_size(va.type);
    const int stride = va.stride ? va.stride : va.size * component_size;
    const addr_t offset = va.ptr + static_cast<addr_t>(index * stride + component * component_size);
    if (va.buffer != 0) {
        const auto it = state.buffers.find(va.buffer);
        uint32_t out = 0;
        if (it != state.buffers.end()) {
            read_buffer_value(it->second, static_cast<size_t>(offset), component_size, out);
        }
        return out;
    }
    if (va.ptr == 0) {
        return 0;
    }
    const addr_t addr = offset;
    if (component_size == 1) {
        return memory.read_value(addr, EndianMemory::Byte);
    }
    if (component_size == 2) {
        return memory.read_value(addr, EndianMemory::Halfword);
    }
    return memory.read_value(addr);
}

inline int matrix_slot(uint32_t mode) {
    switch (mode) {
        case kGlModelview: return 0;
        case kGlProjection: return 1;
        case kGlTexture: return 2;
        default: return 0;
    }
}

inline std::array<float, 16> identity_matrix() {
    std::array<float, 16> mat{};
    mat.fill(0.0f);
    mat[0] = mat[5] = mat[10] = mat[15] = 1.0f;
    return mat;
}

inline std::array<float, 16> multiply_matrix(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    out.fill(0.0f);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
    return out;
}

inline std::array<float, 16> translate_matrix(float x, float y, float z) {
    auto mat = identity_matrix();
    mat[12] = x;
    mat[13] = y;
    mat[14] = z;
    return mat;
}

inline std::array<float, 16> scale_matrix(float x, float y, float z) {
    auto mat = identity_matrix();
    mat[0] = x;
    mat[5] = y;
    mat[10] = z;
    return mat;
}

inline std::array<float, 16> rotate_matrix(float angle_deg, float x, float y, float z) {
    auto mat = identity_matrix();
    const float len = std::sqrt((x * x) + (y * y) + (z * z));
    if (len <= 0.0f) {
        return mat;
    }
    x /= len;
    y /= len;
    z /= len;
    const float rad = angle_deg * 0.017453292519943295f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    const float t = 1.0f - c;
    mat[0] = t * x * x + c;
    mat[1] = t * x * y + s * z;
    mat[2] = t * x * z - s * y;
    mat[4] = t * x * y - s * z;
    mat[5] = t * y * y + c;
    mat[6] = t * y * z + s * x;
    mat[8] = t * x * z + s * y;
    mat[9] = t * y * z - s * x;
    mat[10] = t * z * z + c;
    return mat;
}

inline std::array<float, 16> ortho_matrix(float left, float right, float bottom, float top, float znear, float zfar) {
    auto mat = identity_matrix();
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = zfar - znear;
    if (rl != 0.0f) mat[0] = 2.0f / rl;
    if (tb != 0.0f) mat[5] = 2.0f / tb;
    if (fn != 0.0f) mat[10] = -2.0f / fn;
    mat[12] = -(right + left) / rl;
    mat[13] = -(top + bottom) / tb;
    mat[14] = -(zfar + znear) / fn;
    return mat;
}

inline std::array<float, 16> frustum_matrix(float left, float right, float bottom, float top, float znear, float zfar) {
    auto mat = std::array<float, 16>{};
    mat.fill(0.0f);
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = zfar - znear;
    if (rl != 0.0f) mat[0] = (2.0f * znear) / rl;
    if (tb != 0.0f) mat[5] = (2.0f * znear) / tb;
    if (rl != 0.0f) mat[8] = (right + left) / rl;
    if (tb != 0.0f) mat[9] = (top + bottom) / tb;
    if (fn != 0.0f) mat[10] = -(zfar + znear) / fn;
    if (fn != 0.0f) mat[11] = -1.0f;
    if (fn != 0.0f) mat[14] = -(2.0f * zfar * znear) / fn;
    return mat;
}

inline float fixed_to_float(uint32_t value) {
    return static_cast<float>(static_cast<int32_t>(value)) / kGlFloatAsFixed;
}

inline float raw_to_float(uint32_t value) {
    float out = 0.0f;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline uint32_t float_to_fixed(float value) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::lround(value * kGlFloatAsFixed)));
}

inline uint32_t normalize_array_type(uint32_t type) {
    switch (type) {
        case 0x00: return kGlByte;          // low byte of GL_BYTE
        case 0x01: return kGlUnsignedByte;  // low byte of GL_UNSIGNED_BYTE
        case 0x02: return kGlShort;         // low byte of GL_SHORT
        case 0x03: return kGlUnsignedShort; // low byte of GL_UNSIGNED_SHORT
        case 0x06: return kGlFloat;         // low byte of GL_FLOAT
        case 0x0c: return kGlFixed;         // low byte of GL_FIXED
        default: return type;
    }
}

inline void write_float(EndianMemory& memory, addr_t addr, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float size");
    std::memcpy(&bits, &value, sizeof(bits));
    memory.write_value(addr, bits);
}

inline void write_matrix_fixed(EndianMemory& memory, addr_t addr, const std::array<float, 16>& mat) {
    for (int i = 0; i < 16; ++i) {
        memory.write_value(addr + static_cast<addr_t>(i * 4), float_to_fixed(mat[i]));
    }
}

inline void write_matrix_float(EndianMemory& memory, addr_t addr, const std::array<float, 16>& mat) {
    for (int i = 0; i < 16; ++i) {
        write_float(memory, addr + static_cast<addr_t>(i * 4), mat[i]);
    }
}

struct Vec3 {
    float x, y, z;
    Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Vec4 {
    float x, y, z, w;
    Vec4(float x = 0, float y = 0, float z = 0, float w = 1) : x(x), y(y), z(z), w(w) {}
};

struct Vec2 {
    float u, v;
    Vec2(float u = 0, float v = 0) : u(u), v(v) {}
};

inline Vec4 transform_point(const Vec4& v, const std::array<float, 16>& mat) {
    return {
        v.x * mat[0] + v.y * mat[4] + v.z * mat[8] + v.w * mat[12],
        v.x * mat[1] + v.y * mat[5] + v.z * mat[9] + v.w * mat[13],
        v.x * mat[2] + v.y * mat[6] + v.z * mat[10] + v.w * mat[14],
        v.x * mat[3] + v.y * mat[7] + v.z * mat[11] + v.w * mat[15]
    };
}

inline Vec2 transform_texcoord(const Vec2& uv, const std::array<float, 16>& mat) {
    const Vec4 transformed = transform_point(Vec4(uv.u, uv.v, 0.0f, 1.0f), mat);
    if (transformed.w != 0.0f) {
        return Vec2(transformed.x / transformed.w, transformed.y / transformed.w);
    }
    return Vec2(transformed.x, transformed.y);
}


bool handle_qx_gl_call(const std::string& name, BrewShell& shell, EndianMemory& memory, CPU& cpu, const char* label);

} // namespace qxgl

#endif
