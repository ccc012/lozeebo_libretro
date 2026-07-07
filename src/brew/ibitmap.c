/* ibitmap.c - IBitmap: sprites/imagens
 *
 * Objeto: [vtable][w][h][addr_pixels]  (dados em offsets 4, 8, 12)
 * Os pixels ficam na memoria emulada em formato XRGB8888.
 * Magenta puro (0xFF00FF) e tratado como transparente no Blt.
 */
#include "brew.h"
#include "../cpu/cpu.h"
#include "../memory/memory.h"
#include "../gpu/framebuffer.h"
#include "../debug/log.h"

void zbrew_handle_bitmap(uint32_t id) {
    uint32_t self = g_cpu.r[0];

    switch (id) {
    case ZT_BMP_ADDREF:
    case ZT_BMP_RELEASE:
        g_cpu.r[0] = 1;
        break;

    case ZT_BMP_GETINFO: {
        /* escreve [w][h] na struct apontada por R1 */
        uint32_t out = g_cpu.r[1];
        zmem_write32(out, zmem_read32(self + 4));
        zmem_write32(out + 4, zmem_read32(self + 8));
        g_cpu.r[0] = ZBREW_SUCCESS;
        break;
    }

    case ZT_BMP_BLT: {
        uint32_t w = zmem_read32(self + 4);
        uint32_t h = zmem_read32(self + 8);
        uint32_t pixels = zmem_read32(self + 12);
        if (pixels && w && h) {
            zfb_blit(pixels, (int)w, (int)h,
                     (int)g_cpu.r[1], (int)g_cpu.r[2], 0x00FF00FFu);
            g_cpu.r[0] = ZBREW_SUCCESS;
        } else {
            LOGW("IBitmap_Blt em bitmap vazio (this=0x%08X)", self);
            g_cpu.r[0] = ZBREW_EFAILED;
        }
        break;
    }
    }
}
