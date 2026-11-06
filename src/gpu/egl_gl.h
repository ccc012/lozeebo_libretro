/* egl_gl.h - Stub basico de EGL/OpenGL ES para jogos 3D
 *
 * Zeebo Family Pack cria AEECLSID_EGL e AEECLSID_GL. Por enquanto
 * implementamos apenas o minimo para o jogo inicializar sem travar.
 * Renderizacao 3D real requer portar a pilha do zeemu (GPL-3.0).
 */
#ifndef ZEEBO_EGL_GL_H
#define ZEEBO_EGL_GL_H

#include <stdbool.h>
#include <stdint.h>

uint32_t zegl_create_interface(void);
uint32_t zegl_create_qegl_surface(void);
uint32_t zgl_create_interface(void);
uint32_t zgl_create_igles_interface(void);

/* Zuma soft d=2 @0957C: RGB565 IDIB → textura bound (pot). Evita loop ARM. */
bool zgl_hle_zuma_soft_rgb565(uint32_t bmp, uint32_t pot_w, uint32_t pot_h);

void zegl_handle(uint32_t slot);
void zgl_handle(uint32_t slot);
void zgl_igles_handle(uint32_t slot);

bool zegl_init(void);
void zegl_shutdown(void);
bool zegl_has_context(void);

/* Pinta um degrade de teste no framebuffer que varia a cada frame.
 * Usado pelo auto-apresentacao para jogos que iniciam EGL mas nunca
 * desenham por conta própria, confirmando que o pipeline EGL/GL esta ativo. */
void zegl_paint_test_pattern(void);

/* Auto-apresentação por frame: glClear + eglSwapBuffers.
 * Chamada pelo núcleo para jogos que usam EGL mas nunca invocam
 * glClear nem eglSwapBuffers por conta própria (ex: Double Dragon). */
void zegl_autopresent(void);

#endif /* ZEEBO_EGL_GL_H */
