/* framebuffer.c - Framebuffer na VRAM emulada (0x30000000) */
#include <string.h>
#include "framebuffer.h"
#include "../memory/memory.h"
#include "../debug/log.h"

static uint32_t *g_fb = NULL;

bool zfb_init(void) {
    g_fb = (uint32_t *)zmem_host_ptr(ZMEM_VRAM_BASE,
                                     ZFB_WIDTH * ZFB_HEIGHT * 4);
    if (!g_fb) {
        LOGE("zfb_init: VRAM nao mapeada");
        return false;
    }
    zfb_clear(0xFF000000u);
    return true;
}

uint32_t *zfb_pixels(void) {
    return g_fb;
}

void zfb_clear(uint32_t color) {
    int i;
    if (!g_fb) return;
    for (i = 0; i < ZFB_WIDTH * ZFB_HEIGHT; i++)
        g_fb[i] = color;
}

void zfb_set_pixel(int x, int y, uint32_t color) {
    if (!g_fb || x < 0 || y < 0 || x >= ZFB_WIDTH || y >= ZFB_HEIGHT) return;
    g_fb[y * ZFB_WIDTH + x] = color;
}

uint32_t zfb_get_pixel(int x, int y) {
    if (!g_fb || x < 0 || y < 0 || x >= ZFB_WIDTH || y >= ZFB_HEIGHT) return 0;
    return g_fb[y * ZFB_WIDTH + x];
}

void zfb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int px, py;
    if (!g_fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ZFB_WIDTH)  w = ZFB_WIDTH - x;
    if (y + h > ZFB_HEIGHT) h = ZFB_HEIGHT - y;
    for (py = y; py < y + h; py++)
        for (px = x; px < x + w; px++)
            g_fb[py * ZFB_WIDTH + px] = color;
}

void zfb_blit(uint32_t src_addr, int src_w, int src_h,
              int dst_x, int dst_y, uint32_t transparent) {
    int sx, sy;
    if (!g_fb || src_w <= 0 || src_h <= 0) return;

    const uint32_t *src =
        (const uint32_t *)zmem_host_ptr(src_addr, (uint32_t)src_w * src_h * 4);

    for (sy = 0; sy < src_h; sy++) {
        int dy = dst_y + sy;
        if (dy < 0 || dy >= ZFB_HEIGHT) continue;
        for (sx = 0; sx < src_w; sx++) {
            int dx = dst_x + sx;
            if (dx < 0 || dx >= ZFB_WIDTH) continue;
            uint32_t c = src ? src[sy * src_w + sx]
                             : zmem_read32(src_addr + (uint32_t)(sy * src_w + sx) * 4);
            if (transparent != ZFB_NO_TRANSPARENT &&
                (c & 0x00FFFFFFu) == (transparent & 0x00FFFFFFu))
                continue;
            g_fb[dy * ZFB_WIDTH + dx] = c;
        }
    }
}
