/* Reckless disables unused texture units between draws; unit zero must survive. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "../src/gpu/egl_gl.c"

int main(void)
{
    zgl_dispatch(GLFN_ActiveTexture, 0x84C0, 0, 0, 0);
    zgl_dispatch(GLFN_Enable, 0x0DE1, 0, 0, 0);
    zgl_dispatch(GLFN_ClientActiveTexture, 0x84C0, 0, 0, 0);
    zgl_dispatch(GLFN_EnableClientState, 0x8078, 0, 0, 0);
    zgl_dispatch(GLFN_ActiveTexture, 0x84C1, 0, 0, 0);
    zgl_dispatch(GLFN_Disable, 0x0DE1, 0, 0, 0);
    zgl_dispatch(GLFN_ClientActiveTexture, 0x84C1, 0, 0, 0);
    zgl_dispatch(GLFN_DisableClientState, 0x8078, 0, 0, 0);
    zgl_dispatch(GLFN_ActiveTexture, 0x84C0, 0, 0, 0);
    zgl_dispatch(GLFN_ClientActiveTexture, 0x84C0, 0, 0, 0);
    assert(g_en_tex2d);
    assert(g_va_tex.on);
    return 0;
}
