/* draw.c - Primitivas de desenho (linha, contorno de retangulo) */
#include <stdlib.h>
#include "framebuffer.h"

void zfb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    zfb_fill_rect(x, y, w, 1, color);
    zfb_fill_rect(x, y + h - 1, w, 1, color);
    zfb_fill_rect(x, y, 1, h, color);
    zfb_fill_rect(x + w - 1, y, 1, h, color);
}

void zfb_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    /* Bresenham */
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        zfb_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
