/* egl_gl.c - Stub basico de EGL/OpenGL ES para jogos 3D
 *
 * Zeebo Family Pack cria AEECLSID_EGL e AEECLSID_GL. Esta implementacao
 * retorna sucesso para operacoes basicas de bootstrap e sinaliza frames
 * simples no framebuffer enquanto o backend 3D real nao existe.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

static void zgl_reset_raster(void);

bool zegl_init(void)
{
    zgl_reset_raster();
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
    zgl_reset_raster();
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

/* ================= Rasterizador de software GLES 1.x ==================
 * Subconjunto minimo para o menu 2D do Family Pack (quads texturizados
 * via GL_TRIANGLE_FAN): texturas, vertex arrays, matrizes fixed-point e
 * rasterizacao afim com blend. Semantica de referencia: BrewQXGLDispatch
 * do zeemu. Sem correcao de perspectiva nem depth buffer por enquanto. */

#define ZGL_MAX_TEXTURES 128

typedef struct {
    uint32_t *pix; /* host, ARGB8888 */
    int w, h;
} zgl_texture_t;

typedef struct {
    uint32_t addr;   /* endereco guest dos dados */
    int      size;   /* componentes por vertice */
    uint32_t type;   /* GL_FIXED/FLOAT/SHORT/BYTE */
    int      stride; /* bytes; 0 = compacto */
    bool     on;
} zgl_varray_t;

static zgl_texture_t g_textures[ZGL_MAX_TEXTURES];
static uint32_t g_bound_tex = 0;
static zgl_varray_t g_va_pos, g_va_tex, g_va_col;

static float g_m_mv[16], g_m_pj[16];       /* column-major (padrao GL) */
static float g_m_tx[16];                   /* GL_TEXTURE (guardada, nao aplicada) */
static float g_stk_mv[16][16], g_stk_pj[4][16];
static int g_mv_sp = 0, g_pj_sp = 0;
static int g_mtx_mode = 0;                 /* 0=modelview 1=projection 2=texture */
static float g_cur_color[4] = {1.f, 1.f, 1.f, 1.f};
static bool g_en_tex2d = false, g_en_blend = false, g_en_alpha = false;

static float *cur_mtx(void)
{
    if (g_mtx_mode == 1) return g_m_pj;
    if (g_mtx_mode == 2) return g_m_tx;
    return g_m_mv;
}

static void mtx_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mtx_mul(float *out, const float *a, const float *b)
{
    /* out = a * b (column-major: out[c*4+r] = sum a[k*4+r]*b[c*4+k]) */
    float t[16];
    int r, c, k;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++) {
            float s = 0.f;
            for (k = 0; k < 4; k++)
                s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    memcpy(out, t, sizeof(t));
}

static float fx_to_f(uint32_t v) { return (float)(int32_t)v / 65536.0f; }

static void mtx_load_guest_fixed(float *m, uint32_t addr)
{
    int i;
    for (i = 0; i < 16; i++)
        m[i] = fx_to_f(zmem_read32(addr + (uint32_t)i * 4));
}

static void zgl_reset_raster(void)
{
    int i;
    for (i = 0; i < ZGL_MAX_TEXTURES; i++) {
        free(g_textures[i].pix);
        g_textures[i].pix = NULL;
        g_textures[i].w = g_textures[i].h = 0;
    }
    g_bound_tex = 0;
    memset(&g_va_pos, 0, sizeof(g_va_pos));
    memset(&g_va_tex, 0, sizeof(g_va_tex));
    memset(&g_va_col, 0, sizeof(g_va_col));
    mtx_identity(g_m_mv);
    mtx_identity(g_m_pj);
    mtx_identity(g_m_tx);
    g_mv_sp = g_pj_sp = 0;
    g_mtx_mode = 0;
    g_cur_color[0] = g_cur_color[1] = g_cur_color[2] = g_cur_color[3] = 1.f;
    g_en_tex2d = g_en_blend = g_en_alpha = false;
}

/* Converte um upload de textura guest para ARGB8888 host.
 * Formatos: GL_RGB 0x1907 / GL_RGBA 0x1908 / GL_LUMINANCE 0x1909 /
 * GL_LUMINANCE_ALPHA 0x190A; tipos: UNSIGNED_BYTE 0x1401,
 * UNSIGNED_SHORT_5_6_5 0x8363, 4_4_4_4 0x8033, 5_5_5_1 0x8034. */
static uint32_t *convert_teximage(uint32_t src, int w, int h,
                                  uint32_t format, uint32_t type)
{
    uint32_t *out;
    int n = w * h, i;
    if (n <= 0 || n > 4096 * 4096)
        return NULL;
    out = (uint32_t *)malloc((size_t)n * 4);
    if (!out)
        return NULL;

    if (type == 0x1401u) { /* UNSIGNED_BYTE */
        int bpp = (format == 0x1908u) ? 4 :
                  (format == 0x1907u) ? 3 :
                  (format == 0x190Au) ? 2 : 1;
        for (i = 0; i < n; i++) {
            uint32_t p = src + (uint32_t)(i * bpp);
            uint8_t r, g, b, a = 255;
            if (bpp >= 3) {
                r = zmem_read8(p);
                g = zmem_read8(p + 1);
                b = zmem_read8(p + 2);
                if (bpp == 4) a = zmem_read8(p + 3);
            } else {
                r = g = b = zmem_read8(p);
                if (bpp == 2) a = zmem_read8(p + 1);
            }
            out[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | b;
        }
    } else if (type == 0x8363u) { /* 5_6_5 */
        for (i = 0; i < n; i++) {
            uint16_t v = zmem_read16(src + (uint32_t)(i * 2));
            uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
            out[i] = 0xFF000000u | ((r << 3 | r >> 2) << 16) |
                     ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
    } else if (type == 0x8033u) { /* 4_4_4_4 */
        for (i = 0; i < n; i++) {
            uint16_t v = zmem_read16(src + (uint32_t)(i * 2));
            uint32_t r = (v >> 12) & 15, g = (v >> 8) & 15,
                     b = (v >> 4) & 15, a = v & 15;
            out[i] = ((a * 17u) << 24) | ((r * 17u) << 16) |
                     ((g * 17u) << 8) | (b * 17u);
        }
    } else if (type == 0x8034u) { /* 5_5_5_1 */
        for (i = 0; i < n; i++) {
            uint16_t v = zmem_read16(src + (uint32_t)(i * 2));
            uint32_t r = (v >> 11) & 31, g = (v >> 6) & 31,
                     b = (v >> 1) & 31, a = (v & 1) ? 255u : 0u;
            out[i] = (a << 24) | ((r << 3 | r >> 2) << 16) |
                     ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
    } else {
        static uint32_t warn = 0;
        if (warn < 8) {
            LOGW("glTexImage2D: tipo 0x%04X nao suportado", type);
            warn++;
        }
        memset(out, 0xFF, (size_t)n * 4); /* branco visivel p/ debug */
    }
    return out;
}

static float fetch_comp(const zgl_varray_t *a, int vtx, int comp)
{
    int esz = (a->type == 0x1400u || a->type == 0x1401u) ? 1 :
              (a->type == 0x1402u || a->type == 0x1403u) ? 2 : 4;
    int stride = a->stride ? a->stride : a->size * esz;
    uint32_t p = a->addr + (uint32_t)(vtx * stride + comp * esz);
    switch (a->type) {
    case 0x140Cu: return fx_to_f(zmem_read32(p));            /* FIXED */
    case 0x1406u: {                                          /* FLOAT */
        uint32_t bits = zmem_read32(p);
        float f;
        memcpy(&f, &bits, 4);
        return f;
    }
    case 0x1402u: return (float)(int16_t)zmem_read16(p);     /* SHORT */
    case 0x1403u: return (float)zmem_read16(p);              /* USHORT */
    case 0x1400u: return (float)(int8_t)zmem_read8(p);       /* BYTE */
    case 0x1401u: return (float)zmem_read8(p) / 255.0f;      /* UBYTE (cor) */
    }
    return 0.f;
}

typedef struct {
    float sx, sy;      /* coordenadas de tela */
    float u, v;        /* texcoord */
    float cr, cg, cb, ca;
} zgl_vtx_t;

static bool transform_vertex(int idx, zgl_vtx_t *out)
{
    float in[4] = {0.f, 0.f, 0.f, 1.f};
    float eye[4], clip[4];
    int i;
    for (i = 0; i < g_va_pos.size && i < 4; i++)
        in[i] = fetch_comp(&g_va_pos, idx, i);
    for (i = 0; i < 4; i++)
        eye[i] = g_m_mv[0 * 4 + i] * in[0] + g_m_mv[1 * 4 + i] * in[1] +
                 g_m_mv[2 * 4 + i] * in[2] + g_m_mv[3 * 4 + i] * in[3];
    for (i = 0; i < 4; i++)
        clip[i] = g_m_pj[0 * 4 + i] * eye[0] + g_m_pj[1 * 4 + i] * eye[1] +
                  g_m_pj[2 * 4 + i] * eye[2] + g_m_pj[3 * 4 + i] * eye[3];
    if (clip[3] <= 0.0001f && clip[3] >= -0.0001f)
        return false;
    {
        float inv_w = 1.0f / clip[3];
        float nx = clip[0] * inv_w, ny = clip[1] * inv_w;
        out->sx = ((nx * 0.5f) + 0.5f) * (float)g_gl_viewport_w +
                  (float)g_gl_viewport_x;
        out->sy = ((-ny * 0.5f) + 0.5f) * (float)g_gl_viewport_h +
                  (float)g_gl_viewport_y;
    }
    if (g_va_tex.on) {
        float u = fetch_comp(&g_va_tex, idx, 0);
        float v = fetch_comp(&g_va_tex, idx, 1);
        /* aplica a matriz GL_TEXTURE (jogos usam para atlas de sprites) */
        out->u = g_m_tx[0] * u + g_m_tx[4] * v + g_m_tx[12];
        out->v = g_m_tx[1] * u + g_m_tx[5] * v + g_m_tx[13];
    } else {
        out->u = out->v = 0.f;
    }
    if (g_va_col.on) {
        out->cr = fetch_comp(&g_va_col, idx, 0);
        out->cg = fetch_comp(&g_va_col, idx, 1);
        out->cb = fetch_comp(&g_va_col, idx, 2);
        out->ca = g_va_col.size >= 4 ? fetch_comp(&g_va_col, idx, 3) : 1.f;
        /* cores FIXED/FLOAT ja vem 0..1; SHORT viria bruto (raro) */
    } else {
        out->cr = g_cur_color[0];
        out->cg = g_cur_color[1];
        out->cb = g_cur_color[2];
        out->ca = g_cur_color[3];
    }
    return true;
}

static uint32_t sample_texture(const zgl_texture_t *t, float u, float v)
{
    int x, y;
    u -= floorf(u);
    v -= floorf(v);
    x = (int)(u * (float)t->w);
    y = (int)(v * (float)t->h);
    if (x >= t->w) x = t->w - 1;
    if (y >= t->h) y = t->h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return t->pix[y * t->w + x];
}

static void raster_triangle(const zgl_vtx_t *v0, const zgl_vtx_t *v1,
                            const zgl_vtx_t *v2)
{
    uint32_t *fb = zfb_pixels();
    const zgl_texture_t *tex = NULL;
    float area, inv_area;
    int minx, miny, maxx, maxy, x, y;

    {
        static uint32_t tri_logs = 0;
        if (tri_logs < 6) {
            LOGD("raster tri: (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) "
                 "tex2d=%d bound=%u tem_pix=%d blend=%d cor=(%.2f %.2f %.2f %.2f)",
                 v0->sx, v0->sy, v1->sx, v1->sy, v2->sx, v2->sy,
                 g_en_tex2d ? 1 : 0, g_bound_tex,
                 (g_bound_tex < ZGL_MAX_TEXTURES &&
                  g_textures[g_bound_tex].pix) ? 1 : 0,
                 g_en_blend ? 1 : 0,
                 v0->cr, v0->cg, v0->cb, v0->ca);
            tri_logs++;
        }
    }

    if (!fb)
        return;
    if (g_en_tex2d && g_bound_tex < ZGL_MAX_TEXTURES &&
        g_textures[g_bound_tex].pix)
        tex = &g_textures[g_bound_tex];

    area = (v1->sx - v0->sx) * (v2->sy - v0->sy) -
           (v1->sy - v0->sy) * (v2->sx - v0->sx);
    if (area > -0.0001f && area < 0.0001f)
        return;
    inv_area = 1.0f / area;

    minx = (int)floorf(v0->sx < v1->sx ? (v0->sx < v2->sx ? v0->sx : v2->sx)
                                       : (v1->sx < v2->sx ? v1->sx : v2->sx));
    maxx = (int)ceilf(v0->sx > v1->sx ? (v0->sx > v2->sx ? v0->sx : v2->sx)
                                      : (v1->sx > v2->sx ? v1->sx : v2->sx));
    miny = (int)floorf(v0->sy < v1->sy ? (v0->sy < v2->sy ? v0->sy : v2->sy)
                                       : (v1->sy < v2->sy ? v1->sy : v2->sy));
    maxy = (int)ceilf(v0->sy > v1->sy ? (v0->sy > v2->sy ? v0->sy : v2->sy)
                                      : (v1->sy > v2->sy ? v1->sy : v2->sy));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > ZFB_WIDTH) maxx = ZFB_WIDTH;
    if (maxy > ZFB_HEIGHT) maxy = ZFB_HEIGHT;

    {
        static uint32_t bbox_logs = 0;
        if (bbox_logs < 4) {
            LOGD("raster bbox: (%.1f,%.1f)-(%.1f,%.1f) -> pix [%d,%d)-[%d,%d) "
                 "pixels=%u", v0->sx, v0->sy, v2->sx, v2->sy, minx, miny, maxx, maxy,
                 (uint32_t)((maxx - minx) * (maxy - miny)));
            bbox_logs++;
        }
    }

    if (!fb) { LOGD("raster: fb == NULL!"); return; }
    for (y = miny; y < maxy; y++) {
        for (x = minx; x < maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((v1->sx - px) * (v2->sy - py) -
                        (v1->sy - py) * (v2->sx - px)) * inv_area;
            float w1 = ((v2->sx - px) * (v0->sy - py) -
                        (v2->sy - py) * (v0->sx - px)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            float cr, cg, cb, ca;
            uint32_t sr, sg, sb, sa, dst;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f)
                continue;
            cr = w0 * v0->cr + w1 * v1->cr + w2 * v2->cr;
            cg = w0 * v0->cg + w1 * v1->cg + w2 * v2->cg;
            cb = w0 * v0->cb + w1 * v1->cb + w2 * v2->cb;
            ca = w0 * v0->ca + w1 * v1->ca + w2 * v2->ca;
            if (tex) {
                float u = w0 * v0->u + w1 * v1->u + w2 * v2->u;
                float v = w0 * v0->v + w1 * v1->v + w2 * v2->v;
                uint32_t t = sample_texture(tex, u, v);
                cr *= (float)((t >> 16) & 0xFF) / 255.f;
                cg *= (float)((t >> 8) & 0xFF) / 255.f;
                cb *= (float)(t & 0xFF) / 255.f;
                ca *= (float)(t >> 24) / 255.f;
            }
            sa = (uint32_t)(ca * 255.f + 0.5f);
            if (sa > 255) sa = 255;
            if (g_en_alpha && sa == 0)
                continue;
            sr = (uint32_t)(cr * 255.f + 0.5f);
            sg = (uint32_t)(cg * 255.f + 0.5f);
            sb = (uint32_t)(cb * 255.f + 0.5f);
            if (sr > 255) sr = 255;
            if (sg > 255) sg = 255;
            if (sb > 255) sb = 255;
            if (g_en_blend && sa < 255) {
                if (sa == 0)
                    continue;
                dst = fb[y * ZFB_WIDTH + x];
                sr = (sr * sa + ((dst >> 16) & 0xFF) * (255 - sa)) / 255;
                sg = (sg * sa + ((dst >> 8) & 0xFF) * (255 - sa)) / 255;
                sb = (sb * sa + (dst & 0xFF) * (255 - sa)) / 255;
            }
            fb[y * ZFB_WIDTH + x] = 0xFF000000u | (sr << 16) | (sg << 8) | sb;
                static uint32_t pix_writes = 0; if (pix_writes < 10) { LOGD("pixel write: (%d,%d) = 0x%08X", x, y, 0xFF000000u | (sr << 16) | (sg << 8) | sb); pix_writes++; }
        }
    }
}

/* Teste: quad colorido no canto (ignora dados do jogo, prove rasterizador) */
static void draw_test_quad(void)
{
    static uint32_t called = 0;
    if (called == 0) {
        LOGI("draw_test_quad CHAMADO - rasterizando");
        called = 1;
    }
    zgl_vtx_t v[4];
    v[0].sx = 20.f;  v[0].sy = 20.f;  v[0].cr = 1.f; v[0].cg = 0.f; v[0].cb = 0.f; v[0].ca = 1.f;
    v[1].sx = 120.f; v[1].sy = 20.f;  v[1].cr = 0.f; v[1].cg = 1.f; v[1].cb = 0.f; v[1].ca = 1.f;
    v[2].sx = 120.f; v[2].sy = 120.f; v[2].cr = 0.f; v[2].cg = 0.f; v[2].cb = 1.f; v[2].ca = 1.f;
    v[3].sx = 20.f;  v[3].sy = 120.f; v[3].cr = 1.f; v[3].cg = 1.f; v[3].cb = 0.f; v[3].ca = 1.f;
    raster_triangle(&v[0], &v[1], &v[2]);
    raster_triangle(&v[0], &v[2], &v[3]);
}

static void draw_prim(uint32_t mode, int count,
                      int (*index_of)(int, uint32_t, uint32_t),
                      uint32_t idx_addr, uint32_t idx_type)
{
    zgl_vtx_t first, prev, cur;
    bool have_first = false, have_prev = false;
    int i;
    if (!g_va_pos.on || count < 3)
        return;
    {
        static uint32_t draw_logs = 0;
        if (draw_logs < 40) {
            zgl_vtx_t probe;
            int vi0 = index_of ? index_of(0, idx_addr, idx_type) : 0;
            if (transform_vertex(vi0, &probe))
                LOGD("draw: modo=0x%X n=%d v0=(%.1f,%.1f) uv=(%.2f,%.2f) "
                     "tex=%u mv[12..14]=(%.1f %.1f %.1f) pj[0]=%.4f pj[5]=%.4f",
                     mode, count, probe.sx, probe.sy, probe.u, probe.v,
                     g_bound_tex, g_m_mv[12], g_m_mv[13], g_m_mv[14],
                     g_m_pj[0], g_m_pj[5]);
            else
                LOGD("draw: modo=0x%X n=%d v0 DESCARTADO (w~0)", mode, count);
            draw_logs++;
        }
    }
    for (i = 0; i < count; i++) {
        int vi = index_of ? index_of(i, idx_addr, idx_type) : i;
        if (!transform_vertex(vi, &cur))
            return;
        if (mode == 0x0004u) { /* GL_TRIANGLES */
            if (i % 3 == 0) { first = cur; have_first = true; }
            else if (i % 3 == 1) { prev = cur; have_prev = true; }
            else if (have_first && have_prev)
                raster_triangle(&first, &prev, &cur);
        } else if (mode == 0x0005u) { /* GL_TRIANGLE_STRIP */
            if (i == 0) { first = cur; }
            else if (i == 1) { prev = cur; }
            else {
                raster_triangle(&first, &prev, &cur);
                first = prev;
                prev = cur;
            }
        } else { /* GL_TRIANGLE_FAN (0x0006) e fallback */
            if (i == 0) { first = cur; }
            else if (i == 1) { prev = cur; }
            else {
                raster_triangle(&first, &prev, &cur);
                prev = cur;
            }
        }
    }
}

static int elem_index(int i, uint32_t addr, uint32_t type)
{
    if (type == 0x1403u) /* UNSIGNED_SHORT */
        return (int)zmem_read16(addr + (uint32_t)i * 2);
    return (int)zmem_read8(addr + (uint32_t)i); /* UNSIGNED_BYTE */
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

    case GLFN_MatrixMode:
        /* 0x1700 MODELVIEW, 0x1701 PROJECTION, 0x1702 TEXTURE */
        g_mtx_mode = (a0 == 0x1701u) ? 1 : (a0 == 0x1702u) ? 2 : 0;
        break;

    case GLFN_LoadIdentity:
        mtx_identity(cur_mtx());
        break;

    case GLFN_LoadMatrixx:
        if (a0) mtx_load_guest_fixed(cur_mtx(), a0);
        break;

    case GLFN_MultMatrixx:
        if (a0) {
            float m[16];
            mtx_load_guest_fixed(m, a0);
            mtx_mul(cur_mtx(), cur_mtx(), m);
        }
        break;

    case GLFN_Orthox: {
        /* Orthox(l,r,b,t,near,far) - near/far no stack */
        float l = fx_to_f(a0), r = fx_to_f(a1);
        float b = fx_to_f(a2), t = fx_to_f(a3);
        float n = fx_to_f(zbrew_stack_arg(0));
        float f = fx_to_f(zbrew_stack_arg(1));
        float m[16];
        if (r == l || t == b || f == n)
            break;
        mtx_identity(m);
        m[0] = 2.f / (r - l);
        m[5] = 2.f / (t - b);
        m[10] = -2.f / (f - n);
        m[12] = -(r + l) / (r - l);
        m[13] = -(t + b) / (t - b);
        m[14] = -(f + n) / (f - n);
        mtx_mul(cur_mtx(), cur_mtx(), m);
        break;
    }

    case GLFN_Frustumx: {
        float l = fx_to_f(a0), r = fx_to_f(a1);
        float b = fx_to_f(a2), t = fx_to_f(a3);
        float n = fx_to_f(zbrew_stack_arg(0));
        float f = fx_to_f(zbrew_stack_arg(1));
        float m[16];
        if (r == l || t == b || f == n || n <= 0.f)
            break;
        memset(m, 0, sizeof(m));
        m[0] = 2.f * n / (r - l);
        m[5] = 2.f * n / (t - b);
        m[8] = (r + l) / (r - l);
        m[9] = (t + b) / (t - b);
        m[10] = -(f + n) / (f - n);
        m[11] = -1.f;
        m[14] = -2.f * f * n / (f - n);
        mtx_mul(cur_mtx(), cur_mtx(), m);
        break;
    }

    case GLFN_Translatex: {
        float m[16];
        mtx_identity(m);
        m[12] = fx_to_f(a0);
        m[13] = fx_to_f(a1);
        m[14] = fx_to_f(a2);
        mtx_mul(cur_mtx(), cur_mtx(), m);
        break;
    }

    case GLFN_Scalex: {
        float m[16];
        mtx_identity(m);
        m[0] = fx_to_f(a0);
        m[5] = fx_to_f(a1);
        m[10] = fx_to_f(a2);
        mtx_mul(cur_mtx(), cur_mtx(), m);
        break;
    }

    case GLFN_Rotatex: {
        /* Rotatex(angulo_graus, x, y, z) - eixo geral normalizado */
        float ang = fx_to_f(a0) * 3.14159265f / 180.f;
        float x = fx_to_f(a1), y = fx_to_f(a2), z = fx_to_f(a3);
        float len = sqrtf(x * x + y * y + z * z);
        float c, s, ic, m[16];
        if (len < 0.0001f)
            break;
        x /= len; y /= len; z /= len;
        c = cosf(ang); s = sinf(ang); ic = 1.f - c;
        mtx_identity(m);
        m[0] = x * x * ic + c;     m[4] = x * y * ic - z * s; m[8]  = x * z * ic + y * s;
        m[1] = y * x * ic + z * s; m[5] = y * y * ic + c;     m[9]  = y * z * ic - x * s;
        m[2] = x * z * ic - y * s; m[6] = y * z * ic + x * s; m[10] = z * z * ic + c;
        mtx_mul(cur_mtx(), cur_mtx(), m);
        break;
    }

    case GLFN_PushMatrix:
        if (g_mtx_mode == 0 && g_mv_sp < 16)
            memcpy(g_stk_mv[g_mv_sp++], g_m_mv, sizeof(g_m_mv));
        else if (g_mtx_mode == 1 && g_pj_sp < 4)
            memcpy(g_stk_pj[g_pj_sp++], g_m_pj, sizeof(g_m_pj));
        break;

    case GLFN_PopMatrix:
        if (g_mtx_mode == 0 && g_mv_sp > 0)
            memcpy(g_m_mv, g_stk_mv[--g_mv_sp], sizeof(g_m_mv));
        else if (g_mtx_mode == 1 && g_pj_sp > 0)
            memcpy(g_m_pj, g_stk_pj[--g_pj_sp], sizeof(g_m_pj));
        break;

    case GLFN_Enable:
    case GLFN_Disable: {
        bool on = (fn == GLFN_Enable);
        if (a0 == 0x0DE1u) g_en_tex2d = on;       /* GL_TEXTURE_2D */
        else if (a0 == 0x0BE2u) g_en_blend = on;  /* GL_BLEND */
        else if (a0 == 0x0BC0u) g_en_alpha = on;  /* GL_ALPHA_TEST */
        break;
    }

    case GLFN_EnableClientState:
    case GLFN_DisableClientState: {
        bool on = (fn == GLFN_EnableClientState);
        if (a0 == 0x8074u) g_va_pos.on = on;      /* GL_VERTEX_ARRAY */
        else if (a0 == 0x8078u) g_va_tex.on = on; /* GL_TEXTURE_COORD_ARRAY */
        else if (a0 == 0x8076u) g_va_col.on = on; /* GL_COLOR_ARRAY */
        break;
    }

    case GLFN_VertexPointer: {
        g_va_pos.size = (int)a0;
        g_va_pos.type = a1;
        g_va_pos.stride = (int)a2;
        /* Convencao Qualcomm: pointer pode estar em R3 ou stack[0]. Se R3==SP, real ptr=sp[0] */
        uint32_t ptr = a3;
        if (a3 >= 0x2FC00000u && a3 < 0x30000000u) { ptr = zbrew_stack_arg(0); }
        g_va_pos.addr = ptr;
        {
            bool valid = (a3 < 0x04000000u);
            static uint32_t vp_logs = 0;
            if (!valid) {
                LOGD("VP INVALID ptr=0x%08X r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X sp0=0x%08X",
                     a3, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2], g_cpu.r[3], zbrew_stack_arg(0));
            }
            if (vp_logs < 6) {
                LOGD("glVertexPointer(size=%u type=0x%X stride=%u ptr=0x%08X) "
                     "dados={%08X %08X %08X %08X %08X %08X %08X %08X}",
                     a0, a1, a2, a3,
                     zmem_read32(a3), zmem_read32(a3 + 4), zmem_read32(a3 + 8),
                     zmem_read32(a3 + 12), zmem_read32(a3 + 16),
                     zmem_read32(a3 + 20), zmem_read32(a3 + 24),
                     zmem_read32(a3 + 28));
                vp_logs++;
            }
        }
        break;
    }

    case GLFN_TexCoordPointer: {
        g_va_tex.size = (int)a0;
        g_va_tex.type = a1;
        g_va_tex.stride = (int)a2;
        uint32_t ptr = a3;
        if (a3 >= 0x2FC00000u && a3 < 0x30000000u) { ptr = zbrew_stack_arg(0); }
        g_va_tex.addr = ptr;
        {
            static uint32_t tp_logs = 0;
            if (tp_logs < 6) {
                LOGD("glTexCoordPointer(size=%u type=0x%X stride=%u ptr=0x%08X) "
                     "dados={%08X %08X %08X %08X}",
                     a0, a1, a2, a3, zmem_read32(a3), zmem_read32(a3 + 4),
                     zmem_read32(a3 + 8), zmem_read32(a3 + 12));
                tp_logs++;
            }
        }
        break;
    }

    case GLFN_ColorPointer:
        g_va_col.size = (int)a0;
        g_va_col.type = a1;
        g_va_col.stride = (int)a2;
        uint32_t ptr = a3;
        if (a3 >= 0x2FC00000u && a3 < 0x30000000u) { ptr = zbrew_stack_arg(0); }
        g_va_col.addr = ptr;
        break;

    case GLFN_Color4x:
        g_cur_color[0] = fx_to_f(a0);
        g_cur_color[1] = fx_to_f(a1);
        g_cur_color[2] = fx_to_f(a2);
        g_cur_color[3] = fx_to_f(a3);
        break;

    case GLFN_BindTexture:
        g_bound_tex = (a1 < ZGL_MAX_TEXTURES) ? a1 : 0;
        break;

    case GLFN_DeleteTextures: {
        uint32_t n = a0, i;
        if (n > 256u)
            break;
        for (i = 0; i < n; i++) {
            uint32_t id = zmem_read32(a1 + i * 4);
            if (id < ZGL_MAX_TEXTURES && g_textures[id].pix) {
                free(g_textures[id].pix);
                g_textures[id].pix = NULL;
            }
        }
        break;
    }

    case GLFN_TexImage2D: {
        /* (target, level, ifmt, w | stack: h, border, fmt, type, pixels) */
        int w = (int)a3;
        int h = (int)zbrew_stack_arg(0);
        uint32_t fmt = zbrew_stack_arg(2);
        uint32_t type = zbrew_stack_arg(3);
        uint32_t pixels = zbrew_stack_arg(4);
        uint32_t level = a1;
        if (level != 0 || g_bound_tex == 0 || g_bound_tex >= ZGL_MAX_TEXTURES)
            break;
        if (!pixels || w <= 0 || h <= 0)
            break;
        free(g_textures[g_bound_tex].pix);
        g_textures[g_bound_tex].pix = convert_teximage(pixels, w, h, fmt, type);
        g_textures[g_bound_tex].w = w;
        g_textures[g_bound_tex].h = h;
        break;
    }

    case GLFN_TexSubImage2D: {
        /* (target, level, xoff, yoff | stack: w, h, fmt, type, pixels) */
        int xo = (int)a2, yo = (int)a3;
        int w = (int)zbrew_stack_arg(0);
        int h = (int)zbrew_stack_arg(1);
        uint32_t fmt = zbrew_stack_arg(2);
        uint32_t type = zbrew_stack_arg(3);
        uint32_t pixels = zbrew_stack_arg(4);
        zgl_texture_t *t;
        uint32_t *tmp;
        int row;
        if (a1 != 0 || g_bound_tex == 0 || g_bound_tex >= ZGL_MAX_TEXTURES)
            break;
        t = &g_textures[g_bound_tex];
        if (!t->pix || !pixels || w <= 0 || h <= 0 ||
            xo < 0 || yo < 0 || xo + w > t->w || yo + h > t->h)
            break;
        tmp = convert_teximage(pixels, w, h, fmt, type);
        if (!tmp)
            break;
        for (row = 0; row < h; row++)
            memcpy(t->pix + (yo + row) * t->w + xo, tmp + row * w,
                   (size_t)w * 4);
        free(tmp);
        break;
    }

    case GLFN_DrawArrays: {
        /* (mode, first, count): 'first' desloca o array via addr temporario */
        uint32_t mode = a0;
        int firstv = (int)a1, count = (int)a2;
        zgl_varray_t save = g_va_pos;
        zgl_varray_t save_t = g_va_tex, save_c = g_va_col;
        if (count <= 0 || count > 65536)
            break;
        if (firstv > 0) {
            int esz, stride;
            esz = (g_va_pos.type == 0x1402u) ? 2 :
                  (g_va_pos.type == 0x1400u || g_va_pos.type == 0x1401u) ? 1 : 4;
            stride = g_va_pos.stride ? g_va_pos.stride : g_va_pos.size * esz;
            g_va_pos.addr += (uint32_t)(firstv * stride);
            if (g_va_tex.on) {
                esz = (g_va_tex.type == 0x1402u) ? 2 :
                      (g_va_tex.type == 0x1400u || g_va_tex.type == 0x1401u) ? 1 : 4;
                stride = g_va_tex.stride ? g_va_tex.stride : g_va_tex.size * esz;
                g_va_tex.addr += (uint32_t)(firstv * stride);
            }
            if (g_va_col.on) {
                esz = (g_va_col.type == 0x1402u) ? 2 :
                      (g_va_col.type == 0x1400u || g_va_col.type == 0x1401u) ? 1 : 4;
                stride = g_va_col.stride ? g_va_col.stride : g_va_col.size * esz;
                g_va_col.addr += (uint32_t)(firstv * stride);
            }
        }
        draw_prim(mode, count, NULL, 0, 0);
        g_va_pos = save;
        g_va_tex = save_t;
        g_va_col = save_c;
        break;
    }

    case GLFN_DrawElements: {
        /* (mode, count, type, indices) */
        int count = (int)a1;
        if (count <= 0 || count > 65536)
            break;
        draw_prim(a0, count, elem_index, a3, a2);
        break;
    }

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
            /* CONVENCAO QUALCOMM (confirmada no BrewQXGLDispatch do zeemu):
             * as funcoes gl* da vtable IGL NAO recebem this - os argumentos
             * GL comecam direto em R0, igual a tabela direta. So AddRef/
             * Release/QueryInterface (slots 0-2) seguem COM classico. Ler
             * this em R0 deslocava tudo (GetIntegerv com pname=ponteiro,
             * GenTextures com n=269 milhoes - visto na pratica). */
            zgl_dispatch(slot - 3, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                         g_cpu.r[3]);
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
