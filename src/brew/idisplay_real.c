/* idisplay_real.c - IDisplay implementado para modulos BREW reais
 * Portado de zeemu/BrewDisplay.cpp com simplificacoes para o libretro.
 * GPL-3.0 (zeemu) + MIT (nosso projeto)
 */
#include <string.h>
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

void zbrew_mark_frame(void);

/* Retorna SUCCESS(0) ou EFAILED(1) */
#define SUCCESS 0
#define EFAILED 1

/* IDisplay real: vtable com 10+ metodos
 * [+0] AddRef, [+4] Release, [+8] Update, [+12] ClearScreen, [+16] SetColor, ...
 * Delegamos a nossos handlers no magic address. */

void zbrew_handle_idisplay_real(uint32_t id) {
    uint32_t slot = id - ZT_STUB_BASE; /* assumindo 0x300+ */
    uint32_t r0 = g_cpu.r[0], r1 = g_cpu.r[1], r2 = g_cpu.r[2], r3 = g_cpu.r[3];

    /* Para agora, qualquer chamada em IDisplay stub retorna sucesso */
    switch (slot) {
    case 0: /* AddRef */
    case 1: /* Release */
        g_cpu.r[0] = 1; /* refcount */
        break;
    case 2: /* Update */
        zbrew_mark_frame();
        g_cpu.r[0] = SUCCESS;
        break;
    case 3: /* ClearScreen */
        zfb_clear(0xFF000000u); /* preto */
        g_cpu.r[0] = SUCCESS;
        break;
    case 4: /* SetColor */
        g_cpu.r[0] = SUCCESS;
        break;
    case 5: /* FillRect */
    case 6: /* DrawRect */
    case 7: /* DrawLine */
    case 8: /* DrawPixel */
    case 9: /* Blit */
        /* stubs para agora; retorna sucesso */
        g_cpu.r[0] = SUCCESS;
        break;
    default:
        LOGW("IDisplay real metodo %u nao implementado", slot);
        g_cpu.r[0] = EFAILED;
        break;
    }
}

/* Factory: cria uma interface IDisplay real
 * Para o applet conseguir desenhar. */
uint32_t zbrew_make_idisplay_real(void) {
    uint32_t vtbl = zheap_alloc(10 * 4);
    uint32_t obj = zheap_alloc(16);
    if (!vtbl || !obj) return 0;

    int i;
    for (i = 0; i < 10; i++) {
        /* todos os metodos apontam para stubs que retornam sucesso */
        zmem_write32(vtbl + (uint32_t)i * 4,
                     ZTRAP_ADDR(ZT_STUB_BASE + 200 + i)); /* magic ranges */
    }
    zmem_write32(obj, vtbl);
    LOGI("IDisplay real criado em 0x%08X", obj);
    return obj;
}
