/* framebuffer.h - Framebuffer 640x480 XRGB8888 mapeado na VRAM emulada */
#ifndef ZEEBO_FRAMEBUFFER_H
#define ZEEBO_FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define ZFB_WIDTH  640
#define ZFB_HEIGHT 480

bool zfb_init(void);           /* mapeia o framebuffer na VRAM emulada */
uint32_t *zfb_pixels(void);    /* ponteiro host para os pixels (video_cb) */

void zfb_clear(uint32_t color);
void zfb_set_pixel(int x, int y, uint32_t color);
uint32_t zfb_get_pixel(int x, int y);
void zfb_fill_rect(int x, int y, int w, int h, uint32_t color);
void zfb_draw_rect(int x, int y, int w, int h, uint32_t color);
void zfb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

/* Blit de pixels XRGB8888 vindos da memoria emulada.
 * src_addr: endereco emulado; transparent: cor tratada como transparente
 * (use ZFB_NO_TRANSPARENT para blit opaco). */
#define ZFB_NO_TRANSPARENT 0x01000000u /* valor impossivel em XRGB */
void zfb_blit(uint32_t src_addr, int src_w, int src_h,
              int dst_x, int dst_y, uint32_t transparent);

#endif /* ZEEBO_FRAMEBUFFER_H */
