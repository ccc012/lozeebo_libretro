/* egl_gl.c - Stub basico de EGL/OpenGL ES para jogos 3D
 *
 * Zeebo Family Pack cria AEECLSID_EGL e AEECLSID_GL. Esta implementacao
 * retorna sucesso para operacoes basicas de bootstrap e sinaliza frames
 * simples no framebuffer enquanto o backend 3D real nao existe.
 */
#include "egl_gl.h"
#include "framebuffer.h"
#include "../brew/brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../debug/log.h"

void zbrew_mark_frame(void);

static uint32_t g_egl_display = 0;
static uint32_t g_egl_surface = 0;
static uint32_t g_egl_context = 0;
static uint32_t g_egl_config = 0;
static uint32_t g_gl_object = 0;
static uint32_t g_gl_vendor = 0;
static uint32_t g_gl_renderer = 0;
static uint32_t g_gl_version = 0;
static uint32_t g_gl_extensions = 0;
static uint32_t g_next_texture_id = 1;
static uint32_t g_gl_clear_color = 0xFF000000u;
static int32_t g_gl_viewport_x = 0;
static int32_t g_gl_viewport_y = 0;
static int32_t g_gl_viewport_w = ZFB_WIDTH;
static int32_t g_gl_viewport_h = ZFB_HEIGHT;

static uint32_t ensure_egl_surface(void)
{
    if (!g_egl_surface) {
        g_egl_surface = zheap_alloc(16);
        if (g_egl_surface) {
            zmem_write32(g_egl_surface, 0x5FACE000u);
            zmem_write32(g_egl_surface + 4, ZFB_WIDTH);
            zmem_write32(g_egl_surface + 8, ZFB_HEIGHT);
        }
    }
    return g_egl_surface;
}

static uint32_t alloc_guest_cstr(const char *s)
{
    uint32_t len = 0;
    uint32_t ptr;
    while (s[len])
        len++;
    ptr = zheap_alloc(len + 1);
    if (ptr)
        zmem_write_block(ptr, s, len + 1);
    return ptr;
}

bool zegl_init(void)
{
    g_egl_display = 0;
    g_egl_surface = 0;
    g_egl_context = 0;
    g_egl_config = 0;
    g_gl_object = 0;
    g_gl_vendor = 0;
    g_gl_renderer = 0;
    g_gl_version = 0;
    g_gl_extensions = 0;
    g_next_texture_id = 1;
    g_gl_clear_color = 0xFF000000u;
    g_gl_viewport_x = 0;
    g_gl_viewport_y = 0;
    g_gl_viewport_w = ZFB_WIDTH;
    g_gl_viewport_h = ZFB_HEIGHT;
    return true;
}

void zegl_shutdown(void)
{
    g_egl_display = 0;
    g_egl_surface = 0;
    g_egl_context = 0;
    g_egl_config = 0;
    g_gl_object = 0;
    g_gl_vendor = 0;
    g_gl_renderer = 0;
    g_gl_version = 0;
    g_gl_extensions = 0;
}

uint32_t zegl_create_interface(void)
{
    static const uint32_t vtbl[] = {
        ZT_EGL_BASE + 0,  ZT_EGL_BASE + 1,  ZT_EGL_BASE + 2,  ZT_EGL_BASE + 3,
        ZT_EGL_BASE + 4,  ZT_EGL_BASE + 5,  ZT_EGL_BASE + 6,  ZT_EGL_BASE + 7,
        ZT_EGL_BASE + 8,  ZT_EGL_BASE + 9,  ZT_EGL_BASE + 10, ZT_EGL_BASE + 11,
        ZT_EGL_BASE + 12, ZT_EGL_BASE + 13, ZT_EGL_BASE + 14, ZT_EGL_BASE + 15,
        ZT_EGL_BASE + 16, ZT_EGL_BASE + 17, ZT_EGL_BASE + 18, ZT_EGL_BASE + 19,
        ZT_EGL_BASE + 20, ZT_EGL_BASE + 21, ZT_EGL_BASE + 22, ZT_EGL_BASE + 23,
        ZT_EGL_BASE + 24, ZT_EGL_BASE + 25, ZT_EGL_BASE + 26, ZT_EGL_BASE + 27,
        ZT_EGL_BASE + 28, ZT_EGL_BASE + 29, ZT_EGL_BASE + 30, ZT_EGL_BASE + 31,
    };
    return zbrew_make_interface(vtbl, 32, 0);
}

/* Vtable IGL real (AEECLSID_GL 0x01014BC3): [0]AddRef [1]Release
 * [2]QueryInterface e depois as funcoes GLES 1.x fixed-point em ordem
 * ALFABETICA a partir do slot 3 (glActiveTexture=3 ... glViewport=79).
 * Layout confirmado no BrewGL::setup_vtable() do zeemu, que roda jogos
 * comerciais reais. Alem da vtable COM, o wrapper GLES_1x da Qualcomm
 * tambem usa o corpo do objeto como tabela de funcoes "sem this":
 * obj[1+i] = ponteiro direto da funcao GL i (mesma ordem alfabetica).
 * Espelhamos as duas formas: traps ZT_GL_BASE+slot para a vtable COM e
 * ZT_GL_BASE+0x80+i para as chamadas diretas (argumentos sem this). */
#define ZGL_FN_COUNT 77
#define ZGL_DIRECT_BASE 0x80u

uint32_t zgl_create_interface(void)
{
    uint32_t vtbl = zheap_alloc(128 * 4);
    uint32_t obj  = zheap_alloc(128 * 4);
    uint32_t i;
    if (!vtbl || !obj)
        return 0;
    for (i = 0; i < 128; i++)
        zmem_write32(vtbl + i * 4, ZTRAP_ADDR(ZT_GL_BASE + i));
    zmem_write32(obj, vtbl);
    for (i = 0; i < ZGL_FN_COUNT; i++)
        zmem_write32(obj + (1 + i) * 4,
                     ZTRAP_ADDR(ZT_GL_BASE + ZGL_DIRECT_BASE + i));
    g_gl_object = obj;
    return g_gl_object;
}

void zegl_handle(uint32_t slot)
{
    uint32_t r1 = g_cpu.r[1];
    uint32_t r2 = g_cpu.r[2];
    uint32_t r3 = g_cpu.r[3];
    static uint32_t warn_count = 0;
    static uint32_t trace_count = 0;

    if (trace_count < 28 && (slot == 4 || slot == 5 || slot == 10 ||
                             slot == 11 || slot == 12 || slot == 13 ||
                             slot == 14 || slot == 16 || slot == 17 ||
                             slot == 19 || slot == 26)) {
        LOGI("EGL slot %u r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X sp0=0x%08X",
             slot, g_cpu.r[0], r1, r2, r3, zbrew_stack_arg(0));
        trace_count++;
    }

    switch (slot) {
    case 0:
    case 1:
        g_cpu.r[0] = 1;
        break;
    case 2:
        if (r2)
            zmem_write32(r2, 0);
        g_cpu.r[0] = 1;
        break;
    case 3:
        g_cpu.r[0] = 0x3000u; /* EGL_SUCCESS */
        break;
    case 4:
        /* Zeebo/Qualcomm EGL uses an opaque EGLDisplay handle here, not an
         * IDisplay-like BREW object. Returning a COM object makes some games
         * later dereference code bytes as a vtable. */
        g_egl_display = 1;
        g_cpu.r[0] = g_egl_display;
        break;
    case 5:
        if (r1)
            zmem_write32(r1, 1);
        if (r2)
            zmem_write32(r2, 1);
        g_cpu.r[0] = 1;
        break;
    case 6:
        g_cpu.r[0] = 1;
        break;
    case 7:
    case 8:
        g_cpu.r[0] = 0;
        break;
    case 9:
        if (r1 && r2 >= 1)
            zmem_write32(r1, g_egl_config ? g_egl_config : 1);
        if (r3)
            zmem_write32(r3, 1);
        g_cpu.r[0] = 1;
        break;
    case 10: {
        uint32_t configs = r2;
        uint32_t config_size = r3;
        uint32_t num_config = zbrew_stack_arg(0);
        if (!g_egl_config) {
            g_egl_config = zheap_alloc(16);
            if (g_egl_config)
                zmem_write32(g_egl_config, 0xC04F1600u);
        }
        if (configs && config_size >= 1)
            zmem_write32(configs, g_egl_config);
        if (num_config)
            zmem_write32(num_config, 1);
        g_cpu.r[0] = 1;
        break;
    }
    case 11: { /* GetConfigAttrib(dpy, config, attrib, value*) - valores do
                * hardware real (RGB565 + depth 16, tabela do zeemu). Zerar
                * tudo aqui trava jogos: BUFFER_SIZE=0 vira divisor zero no
                * calculo de bytes-por-pixel (visto no Family Pack). */
        uint32_t val = 0;
        switch (r2) {
        case 0x3020u: val = 16; break; /* EGL_BUFFER_SIZE */
        case 0x3024u: val = 5;  break; /* EGL_RED_SIZE */
        case 0x3023u: val = 6;  break; /* EGL_GREEN_SIZE */
        case 0x3022u: val = 5;  break; /* EGL_BLUE_SIZE */
        case 0x3021u: val = 0;  break; /* EGL_ALPHA_SIZE */
        case 0x3025u: val = 16; break; /* EGL_DEPTH_SIZE */
        case 0x3026u: val = 0;  break; /* EGL_STENCIL_SIZE */
        case 0x3033u: val = 1;  break; /* EGL_SURFACE_TYPE: WINDOW_BIT */
        default: break;
        }
        if (r3)
            zmem_write32(r3, val);
        g_cpu.r[0] = 1;
        break;
    }
    case 12:
    case 13:
    case 14:
        g_cpu.r[0] = ensure_egl_surface();
        break;
    case 15:
        g_cpu.r[0] = 1;
        break;
    case 16: {
        uint32_t attrib = r2;
        uint32_t pvalue = r3;
        uint32_t value = 0;
        if (r2 != 0x3057u && r2 != 0x3056u) {
            attrib = r3;
            pvalue = zbrew_stack_arg(0);
        }
        if (attrib == 0x3057u)
            value = ZFB_WIDTH;
        else if (attrib == 0x3056u)
            value = ZFB_HEIGHT;
        else if (attrib == 0x3093u)
            value = 0x3095u;
        if (pvalue)
            zmem_write32(pvalue, value);
        g_cpu.r[0] = 1;
        break;
    }
    case 17:
        if (!g_egl_context) {
            g_egl_context = zheap_alloc(16);
            if (g_egl_context)
                zmem_write32(g_egl_context, 0xC04E7000u);
        }
        g_cpu.r[0] = g_egl_context;
        break;
    case 18:
        g_cpu.r[0] = 1;
        break;
    case 19:
        if (r3)
            zmem_write32(r3, 1);
        g_cpu.r[0] = 1;
        break;
    case 20:
        g_cpu.r[0] = g_egl_context;
        break;
    case 21:
        g_cpu.r[0] = g_egl_surface;
        break;
    case 22:
        g_cpu.r[0] = g_egl_display;
        break;
    case 23:
    case 24:
    case 25:
        g_cpu.r[0] = 1;
        break;
    case 26:
        /* SwapBuffers: apresenta o que esta na VRAM - NAO limpar aqui
         * (limpar apagaria tudo que o frame desenhou; quem limpa e o
         * glClear do jogo, no inicio do proximo frame). */
        zbrew_mark_frame();
        g_cpu.r[0] = 1;
        break;
    case 27:
        g_cpu.r[0] = 1;
        break;
    default:
        if (slot >= 28 && slot <= 31) {
            g_cpu.r[0] = 1;
            break;
        }
        if (warn_count < 16) {
            LOGW("egl_gl: EGL metodo %u nao implementado", slot);
            warn_count++;
        }
        g_cpu.r[0] = 0;
        break;
    }
}

/* Indices 0-based das funcoes GL na ordem alfabetica da vtable real
 * (slot COM = 3 + indice; tabela direta obj[1+indice]). */
enum zgl_fn {
    GLFN_ActiveTexture = 0, GLFN_AlphaFuncx, GLFN_BindTexture, GLFN_BlendFunc,
    GLFN_Clear, GLFN_ClearColorx, GLFN_ClearDepthx, GLFN_ClearStencil,
    GLFN_ClientActiveTexture, GLFN_Color4x, GLFN_ColorMask, GLFN_ColorPointer,
    GLFN_CompressedTexImage2D, GLFN_CompressedTexSubImage2D,
    GLFN_CopyTexImage2D, GLFN_CopyTexSubImage2D, GLFN_CullFace,
    GLFN_DeleteTextures, GLFN_DepthFunc, GLFN_DepthMask, GLFN_DepthRangex,
    GLFN_Disable, GLFN_DisableClientState, GLFN_DrawArrays, GLFN_DrawElements,
    GLFN_Enable, GLFN_EnableClientState, GLFN_Finish, GLFN_Flush, GLFN_Fogx,
    GLFN_Fogxv, GLFN_FrontFace, GLFN_Frustumx, GLFN_GenTextures,
    GLFN_GetError, GLFN_GetIntegerv, GLFN_GetString, GLFN_Hint,
    GLFN_LightModelx, GLFN_LightModelxv, GLFN_Lightx, GLFN_Lightxv,
    GLFN_LineWidthx, GLFN_LoadIdentity, GLFN_LoadMatrixx, GLFN_LogicOp,
    GLFN_Materialx, GLFN_Materialxv, GLFN_MatrixMode, GLFN_MultMatrixx,
    GLFN_MultiTexCoord4x, GLFN_Normal3x, GLFN_NormalPointer, GLFN_Orthox,
    GLFN_PixelStorei, GLFN_PointSizex, GLFN_PolygonOffsetx, GLFN_PopMatrix,
    GLFN_PushMatrix, GLFN_ReadPixels, GLFN_Rotatex, GLFN_SampleCoveragex,
    GLFN_Scalex, GLFN_Scissor, GLFN_ShadeModel, GLFN_StencilFunc,
    GLFN_StencilMask, GLFN_StencilOp, GLFN_TexCoordPointer, GLFN_TexEnvx,
    GLFN_TexEnvxv, GLFN_TexImage2D, GLFN_TexParameterx, GLFN_TexSubImage2D,
    GLFN_Translatex, GLFN_VertexPointer, GLFN_Viewport
};

static const char *zgl_fn_name(uint32_t fn)
{
    static const char *names[ZGL_FN_COUNT] = {
        "ActiveTexture", "AlphaFuncx", "BindTexture", "BlendFunc", "Clear",
        "ClearColorx", "ClearDepthx", "ClearStencil", "ClientActiveTexture",
        "Color4x", "ColorMask", "ColorPointer", "CompressedTexImage2D",
        "CompressedTexSubImage2D", "CopyTexImage2D", "CopyTexSubImage2D",
        "CullFace", "DeleteTextures", "DepthFunc", "DepthMask", "DepthRangex",
        "Disable", "DisableClientState", "DrawArrays", "DrawElements",
        "Enable", "EnableClientState", "Finish", "Flush", "Fogx", "Fogxv",
        "FrontFace", "Frustumx", "GenTextures", "GetError", "GetIntegerv",
        "GetString", "Hint", "LightModelx", "LightModelxv", "Lightx",
        "Lightxv", "LineWidthx", "LoadIdentity", "LoadMatrixx", "LogicOp",
        "Materialx", "Materialxv", "MatrixMode", "MultMatrixx",
        "MultiTexCoord4x", "Normal3x", "NormalPointer", "Orthox",
        "PixelStorei", "PointSizex", "PolygonOffsetx", "PopMatrix",
        "PushMatrix", "ReadPixels", "Rotatex", "SampleCoveragex", "Scalex",
        "Scissor", "ShadeModel", "StencilFunc", "StencilMask", "StencilOp",
        "TexCoordPointer", "TexEnvx", "TexEnvxv", "TexImage2D",
        "TexParameterx", "TexSubImage2D", "Translatex", "VertexPointer",
        "Viewport"
    };
    return fn < ZGL_FN_COUNT ? names[fn] : "?";
}

/* Executa a funcao GL `fn` (indice alfabetico) com argumentos ja
 * normalizados: a0..a3 sao os 4 primeiros args GL, independentemente de
 * a chamada ter vindo pela vtable COM (this em R0, args em R1+) ou pela
 * tabela direta sem this (args em R0+). */
static void zgl_dispatch(uint32_t fn, uint32_t a0, uint32_t a1,
                         uint32_t a2, uint32_t a3)
{
    static uint32_t seen[(ZGL_FN_COUNT + 31) / 32];

    /* Loga a primeira chamada de cada funcao: vira o inventario do que o
     * jogo realmente usa (guia do rasterizador na proxima fase). */
    if (fn < ZGL_FN_COUNT && !(seen[fn >> 5] & (1u << (fn & 31u)))) {
        seen[fn >> 5] |= 1u << (fn & 31u);
        LOGD("gl%s: primeira chamada (a0=0x%08X a1=0x%08X)",
             zgl_fn_name(fn), a0, a1);
    }

    g_cpu.r[0] = 0;

    switch (fn) {
    case GLFN_Clear:
        zfb_clear(g_gl_clear_color);
        break;

    case GLFN_ClearColorx: {
        /* GLclampx fixed-point 16.16: 0..0x10000 = 0.0..1.0 */
        uint32_t cr = (a0 > 0xFFFFu) ? 255u : (a0 >> 8);
        uint32_t cg = (a1 > 0xFFFFu) ? 255u : (a1 >> 8);
        uint32_t cb = (a2 > 0xFFFFu) ? 255u : (a2 >> 8);
        (void)a3; /* alpha ignorado no framebuffer XRGB */
        g_gl_clear_color = 0xFF000000u | (cr << 16) | (cg << 8) | cb;
        break;
    }

    case GLFN_GenTextures: {
        uint32_t n = a0, ids = a1, i;
        if (n > 256u) {
            LOGW("glGenTextures: n=%u invalido, ignorado", n);
            break;
        }
        for (i = 0; i < n; i++)
            zmem_write32(ids + i * 4, g_next_texture_id++);
        break;
    }

    case GLFN_GetError:
        g_cpu.r[0] = 0; /* GL_NO_ERROR */
        break;

    case GLFN_GetIntegerv:
        if (a1) {
            uint32_t value;
            switch (a0) {
            case 0x0D33u: value = 1024; break; /* GL_MAX_TEXTURE_SIZE */
            case 0x0D36u: value = 16;   break; /* GL_MAX_MODELVIEW_STACK_DEPTH */
            case 0x0D38u: value = 2;    break; /* GL_MAX_PROJECTION_STACK_DEPTH */
            case 0x0D39u: value = 2;    break; /* GL_MAX_TEXTURE_STACK_DEPTH */
            case 0x84E2u: value = 2;    break; /* GL_MAX_TEXTURE_UNITS */
            case 0x0BA0u: value = 0;    break; /* GL_MATRIX_MODE (offset) */
            default:
                /* 1 e mais seguro que 0 como default: valores de device
                 * viram divisores no codigo dos jogos (divisao por
                 * subtracao - 0 = loop infinito, ja visto na pratica) */
                value = 1;
                LOGD("glGetIntegerv: pname=0x%04X desconhecido -> 1", a0);
                break;
            }
            zmem_write32(a1, value);
        }
        break;

    case GLFN_GetString: {
        uint32_t ret = 0;
        if (!g_gl_vendor)
            g_gl_vendor = alloc_guest_cstr("Qualcomm");
        if (!g_gl_renderer)
            g_gl_renderer = alloc_guest_cstr("Adreno 130");
        if (!g_gl_version)
            g_gl_version = alloc_guest_cstr("OpenGL ES-CM 1.1");
        if (!g_gl_extensions)
            g_gl_extensions = alloc_guest_cstr(
                "GL_OES_fixed_point GL_OES_read_format "
                "GL_QUALCOMM_vertex_buffer_object");
        switch (a0) {
        case 0x1F00: ret = g_gl_vendor; break;    /* GL_VENDOR */
        case 0x1F01: ret = g_gl_renderer; break;  /* GL_RENDERER */
        case 0x1F02: ret = g_gl_version; break;   /* GL_VERSION */
        case 0x1F03: ret = g_gl_extensions; break;/* GL_EXTENSIONS */
        default: break;
        }
        g_cpu.r[0] = ret;
        break;
    }

    case GLFN_Viewport:
    case GLFN_Scissor:
        g_gl_viewport_x = (int32_t)a0;
        g_gl_viewport_y = (int32_t)a1;
        g_gl_viewport_w = (int32_t)a2;
        g_gl_viewport_h = (int32_t)a3;
        break;

    default:
        /* Estado/matrizes/draw: no-op ate o rasterizador existir.
         * Nao marcar frame aqui - so eglSwapBuffers apresenta. */
        break;
    }
}

void zgl_handle(uint32_t slot)
{
    static uint32_t warn_count = 0;

    if (slot >= ZGL_DIRECT_BASE) {
        /* Tabela direta sem this: args comecam em R0 */
        uint32_t fn = slot - ZGL_DIRECT_BASE;
        if (fn >= ZGL_FN_COUNT) {
            if (warn_count < 16) {
                LOGW("egl_gl: chamada direta GL %u fora da tabela", fn);
                warn_count++;
            }
            g_cpu.r[0] = 0;
            return;
        }
        zgl_dispatch(fn, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3]);
        return;
    }

    switch (slot) {
    case 0: /* AddRef */
    case 1: /* Release */
        g_cpu.r[0] = 1;
        break;
    case 2: /* QueryInterface(po, iid, ppobj) */
        if (g_cpu.r[2])
            zmem_write32(g_cpu.r[2], g_gl_object);
        g_cpu.r[0] = 0;
        break;
    default:
        if (slot >= 3 && slot < 3 + ZGL_FN_COUNT) {
            /* Vtable COM: this em R0, args GL em R1..R3 + stack */
            zgl_dispatch(slot - 3, g_cpu.r[1], g_cpu.r[2], g_cpu.r[3],
                         zbrew_stack_arg(0));
            break;
        }
        if (warn_count < 16) {
            LOGW("egl_gl: GL slot %u (reservado) chamado", slot);
            warn_count++;
        }
        g_cpu.r[0] = 0;
        break;
    }
}
