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
static uint32_t g_gl_clear_color = 0xFF000000u;

bool zegl_init(void)
{
    g_egl_display = 0;
    g_egl_surface = 0;
    g_egl_context = 0;
    g_egl_config = 0;
    g_gl_clear_color = 0xFF000000u;
    return true;
}

void zegl_shutdown(void)
{
    g_egl_display = 0;
    g_egl_surface = 0;
    g_egl_context = 0;
    g_egl_config = 0;
}

uint32_t zegl_create_interface(void)
{
    static const uint32_t vtbl[] = {
        ZT_EGL_BASE + 0,
        ZT_EGL_BASE + 1,
        ZT_EGL_BASE + 2,
        ZT_EGL_BASE + 3,
        ZT_EGL_BASE + 4,
        ZT_EGL_BASE + 5,
        ZT_EGL_BASE + 6,
        ZT_EGL_BASE + 7,
        ZT_EGL_BASE + 8,
        ZT_EGL_BASE + 9,
        ZT_EGL_BASE + 10,
        ZT_EGL_BASE + 11,
        ZT_EGL_BASE + 12,
    };
    return zbrew_make_interface(vtbl, 13, 0);
}

uint32_t zgl_create_interface(void)
{
    static const uint32_t vtbl[] = {
        ZT_GL_BASE + 0,
        ZT_GL_BASE + 1,
        ZT_GL_BASE + 2,
        ZT_GL_BASE + 3,
        ZT_GL_BASE + 4,
        ZT_GL_BASE + 5,
        ZT_GL_BASE + 6,
        ZT_GL_BASE + 7,
        ZT_GL_BASE + 8,
        ZT_GL_BASE + 9,
        ZT_GL_BASE + 10,
        ZT_GL_BASE + 11,
    };
    return zbrew_make_interface(vtbl, 12, 0);
}

void zegl_handle(uint32_t slot)
{
    uint32_t r1 = g_cpu.r[1];
    uint32_t r2 = g_cpu.r[2];
    uint32_t r3 = g_cpu.r[3];
    static uint32_t warn_count = 0;

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
        if (!g_egl_display) {
            g_egl_display = zheap_alloc(16);
            if (g_egl_display)
                zmem_write32(g_egl_display, 0xEED15900u);
        }
        g_cpu.r[0] = g_egl_display;
        break;
    case 4:
        if (r2)
            zmem_write32(r2, 1);
        if (r3)
            zmem_write32(r3, 4);
        g_cpu.r[0] = 1;
        break;
    case 5:
        g_cpu.r[0] = 1;
        break;
    case 6: {
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
    case 7:
        if (!g_egl_surface) {
            g_egl_surface = zheap_alloc(16);
            if (g_egl_surface) {
                zmem_write32(g_egl_surface, 0x5FACE000u);
                zmem_write32(g_egl_surface + 4, ZFB_WIDTH);
                zmem_write32(g_egl_surface + 8, ZFB_HEIGHT);
            }
        }
        g_cpu.r[0] = g_egl_surface;
        break;
    case 8:
        if (!g_egl_context) {
            g_egl_context = zheap_alloc(16);
            if (g_egl_context)
                zmem_write32(g_egl_context, 0xC04E7000u);
        }
        g_cpu.r[0] = g_egl_context;
        break;
    case 9:
    case 10:
    case 11:
    case 12:
        if (slot == 10)
            zbrew_mark_frame();
        g_cpu.r[0] = 1;
        break;
    default:
        if (warn_count < 16) {
            LOGW("egl_gl: EGL metodo %u nao implementado", slot);
            warn_count++;
        }
        g_cpu.r[0] = 0;
        break;
    }
}

void zgl_handle(uint32_t slot)
{
    uint32_t r1 = g_cpu.r[1];
    uint32_t r2 = g_cpu.r[2];
    uint32_t r3 = g_cpu.r[3];
    static uint32_t warn_count = 0;

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
        zfb_clear(g_gl_clear_color);
        zbrew_mark_frame();
        break;
    case 4:
        g_gl_clear_color = 0xFF000000u |
                           ((r1 & 0xFFu) << 16) |
                           ((r2 & 0xFFu) << 8) |
                           (r3 & 0xFFu);
        break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        break;
    default:
        if (warn_count < 32) {
            LOGW("egl_gl: GL metodo %u nao implementado", slot);
            warn_count++;
        }
        break;
    }
}
