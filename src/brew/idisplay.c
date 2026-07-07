/* idisplay.c - IDisplay/IGraphics: desenho no framebuffer
 *
 * Convencao de argumentos (vtable com this em R0):
 *   R0 = this, R1..R3 = args, resto na stack (zbrew_stack_arg).
 */
#include "brew.h"
#include "../cpu/cpu.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

extern uint32_t g_brew_draw_color;
void zbrew_mark_frame(void);

void zbrew_handle_display(uint32_t id) {
    switch (id) {
    case ZT_DISP_ADDREF:
    case ZT_DISP_RELEASE:
        g_cpu.r[0] = 1;
        break;

    case ZT_DISP_UPDATE:
        zbrew_mark_frame();
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_CLEARSCREEN:
        zfb_clear(0xFF000000u);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_SETCOLOR:
        g_brew_draw_color = 0xFF000000u | g_cpu.r[1];
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_FILLRECT:
        zfb_fill_rect((int)g_cpu.r[1], (int)g_cpu.r[2],
                      (int)g_cpu.r[3], (int)zbrew_stack_arg(0),
                      g_brew_draw_color);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_DRAWRECT:
        zfb_draw_rect((int)g_cpu.r[1], (int)g_cpu.r[2],
                      (int)g_cpu.r[3], (int)zbrew_stack_arg(0),
                      g_brew_draw_color);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_DRAWLINE:
        zfb_draw_line((int)g_cpu.r[1], (int)g_cpu.r[2],
                      (int)g_cpu.r[3], (int)zbrew_stack_arg(0),
                      g_brew_draw_color);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_DRAWPIXEL:
        zfb_set_pixel((int)g_cpu.r[1], (int)g_cpu.r[2], g_brew_draw_color);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;

    case ZT_DISP_BITBLT:
        /* R1=addr origem R2=w R3=h [SP]=x destino [SP+4]=y destino */
        zfb_blit(g_cpu.r[1], (int)g_cpu.r[2], (int)g_cpu.r[3],
                 (int)zbrew_stack_arg(0), (int)zbrew_stack_arg(1),
                 ZFB_NO_TRANSPARENT);
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }
}
