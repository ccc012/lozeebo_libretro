/* brew.c - Dispatch central do trap HLE e criacao de interfaces */
#include <string.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../gpu/framebuffer.h"
#include "../gpu/egl_gl.h"
#include "../debug/log.h"


static uint32_t g_shell_ptr = 0;
static bool     g_frame_ready = false;
static uint32_t g_uptime_ms = 0;
static uint32_t g_unimpl_count = 0;

uint32_t zbrew_stack_arg(int n) {
    return zmem_read32(g_cpu.r[REG_SP] + (uint32_t)n * 4);
}

uint32_t zbrew_make_interface(const uint32_t *trap_ids, int n_methods,
                              uint32_t data_words) {
    /* objeto: [vtable_ptr][dados...] ; vtable: n ponteiros magicos */
    uint32_t obj = zheap_alloc(4 + data_words * 4);
    uint32_t vtbl = zheap_alloc((uint32_t)n_methods * 4);
    int i;
    if (!obj || !vtbl) {
        LOGE("zbrew_make_interface: heap esgotado");
        return 0;
    }
    for (i = 0; i < n_methods; i++)
        zmem_write32(vtbl + (uint32_t)i * 4, ZTRAP_ADDR(trap_ids[i]));
    zmem_write32(obj, vtbl);
    return obj;
}

/* Flag de frame (IDisplay_Update) */
bool zbrew_frame_ready(void) { return g_frame_ready; }
void zbrew_clear_frame_flag(void) { g_frame_ready = false; }
void zbrew_mark_frame(void);
void zbrew_mark_frame(void) { g_frame_ready = true; }

void zbrew_tick_ms(uint32_t ms) { g_uptime_ms += ms; }
uint32_t zbrew_uptime_ms(void);
uint32_t zbrew_uptime_ms(void) { return g_uptime_ms; }

uint32_t zbrew_shell_ptr(void) { return g_shell_ptr; }

/* input.c fornece o bitmask de teclas */
extern uint32_t zinput_key_mask(void);

/* Cor atual de desenho (compartilhada com idisplay.c) */
uint32_t g_brew_draw_color = 0xFFFFFFFFu;

static void trap_dispatch(uint32_t addr) {
    uint32_t id = ZTRAP_ID(addr);
    uint32_t ret_pc = g_cpu.r[REG_LR];

    switch (id) {
    case ZT_RETURN_APPLET:
        LOGI("applet retornou (R0=0x%08X) - CPU parada", g_cpu.r[0]);
        g_cpu.halted = true;
        return;

    /* ---- helpers globais ---- */
    case ZT_MALLOC: case ZT_FREE: case ZT_REALLOC:
    case ZT_MEMSET: case ZT_MEMCPY:
        zbrew_handle_memory(id);
        break;

    case ZT_LOG_STR: {
        char buf[256];
        zmem_read_cstr(g_cpu.r[0], buf, sizeof(buf));
        LOGI("DBGPRINTF: %s", buf);
        break;
    }
    case ZT_GET_TIME_MS:
        g_cpu.r[0] = g_uptime_ms;
        break;
    case ZT_GET_KEYS:
        g_cpu.r[0] = zinput_key_mask();
        break;

    /* ---- traps de teste ---- */
    case ZT_TEST_FILL:
        zfb_clear(0xFF000000u | g_cpu.r[0]);
        g_frame_ready = true;
        break;
    case ZT_TEST_RECT:
        zfb_fill_rect((int)g_cpu.r[0], (int)g_cpu.r[1],
                      (int)g_cpu.r[2], (int)g_cpu.r[3], g_brew_draw_color);
        g_frame_ready = true;
        break;
    case ZT_TEST_SETCOLOR:
        g_brew_draw_color = 0xFF000000u | g_cpu.r[0];
        break;

    /* ---- retorno de guest call: a maquina de estados decide o PC ---- */
    case ZT_GUEST_RETURN:
        zboot_on_guest_return();
        return;

    /* ---- interfaces ---- */
    default:
        if (id >= ZT_HELPER_BASE && id <= ZT_HELPER_END)
            zbrew_handle_helper(id);
        else if (id >= ZT_ISHELL_BASE && id <= ZT_ISHELL_END)
            zbrew_handle_ishell_real(id);
        else if (id >= ZT_STUB_BASE && id <= ZT_STUB_END)
            zbrew_handle_stub(id);
        else if (id >= ZT_EGL_BASE && id <= ZT_EGL_END)
            zegl_handle(id - ZT_EGL_BASE);
        else if (id >= ZT_GL_BASE && id <= ZT_GL_END)
            zgl_handle(id - ZT_GL_BASE);
        else if (id >= ZT_SHELL_ADDREF && id <= ZT_SHELL_GETUPTIMEMS)
            zbrew_handle_shell(id);
        else if (id >= ZT_DISP_ADDREF && id <= ZT_DISP_BITBLT)
            zbrew_handle_display(id);
        else if (id >= ZT_BMP_ADDREF && id <= ZT_BMP_BLT)
            zbrew_handle_bitmap(id);
        else if (id >= ZT_FMGR_ADDREF && id <= ZT_FILE_SEEK)
            zbrew_handle_file(id);
        else if (id >= ZT_SND_ADDREF && id <= ZT_SND_SETVOLUME)
            zbrew_handle_sound(id);
        else {
            if (g_unimpl_count < 64) {
                LOGW("API nao implementada: addr=0x%08X id=0x%X "
                     "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                     "LR=0x%08X SP=0x%08X",
                     addr, id, g_cpu.r[0], g_cpu.r[1], g_cpu.r[2],
                     g_cpu.r[3], ret_pc, g_cpu.r[REG_SP]);
                g_unimpl_count++;
            }
            g_cpu.r[0] = ZBREW_EFAILED;
        }
        break;
    }

    /* volta para o jogo (convencao: PC = LR) */
    zcpu_bx(ret_pc);
}

bool zbrew_init(void) {
    static const uint32_t shell_vtbl[] = {
        ZT_SHELL_ADDREF, ZT_SHELL_RELEASE, ZT_SHELL_CREATEINSTANCE,
        ZT_SHELL_GETDEVICEINFO, ZT_SHELL_SETTIMER, ZT_SHELL_GETUPTIMEMS
    };
    g_shell_ptr = zbrew_make_interface(shell_vtbl, 6, 0);
    if (!g_shell_ptr) return false;
    if (!zegl_init()) return false;

    zcpu_set_trap_handler(trap_dispatch);
    g_frame_ready = false;
    g_uptime_ms = 0;
    g_unimpl_count = 0;
    LOGI("brew_init: IShell em 0x%08X", g_shell_ptr);
    return true;
}

void zbrew_reset(void) {
    g_frame_ready = false;
    g_uptime_ms = 0;
    g_unimpl_count = 0;
    g_brew_draw_color = 0xFFFFFFFFu;
    zegl_init();
}

void zbrew_shutdown(void) {
    zegl_shutdown();
    g_shell_ptr = 0;
}
