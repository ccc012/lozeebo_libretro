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

void zegl_handle(uint32_t slot);
void zgl_handle(uint32_t slot);

bool zegl_init(void);
void zegl_shutdown(void);

#endif /* ZEEBO_EGL_GL_H */
