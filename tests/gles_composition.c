/* Actual FP trace: TexEnvx(TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE),
 * current color (0,.46,.64,1), textured character. REPLACE ignores tint.
 * Reuse the standalone GLES harness stubs, not its legacy main. */
#include <stdlib.h>
#define main transform_legacy_main
#include "test_gles_transform.c"
#undef main

static void expect(uint32_t rgb, const char *label) {
    uint32_t got = zfb_pixels()[200 * ZFB_WIDTH + 300] & 0xffffffu;
    if (got != rgb) { fprintf(stderr, "%s: got %06x expected %06x\n", label, got, rgb); exit(1); }
}

int main(void) {
    uint32_t vertices, pixels;
    int i;
    const int xy[] = {-1,-1, 1,-1, 1,1, -1,1};
    if (!zmem_init() || !zfb_init() || !zegl_init()) return 1;
    memset(&g_cpu, 0, sizeof(g_cpu));
    g_cpu.r[REG_SP] = ZMEM_STACK_TOP - 64;
    vertices = zheap_alloc(32); pixels = zheap_alloc(4);
    for (i=0; i<8; ++i) zmem_write32(vertices+i*4, FX((float)xy[i]));
    gl_call(T_EnableClientState, 0x8074,0,0,0);
    gl_call(T_VertexPointer,2,0x140c,0,vertices);
    gl_call(T_BindTexture,0x0de1,1,0,0);
    zmem_write32(pixels,0xff204080); /* RGBA bytes: 80 40 20 ff */
    zmem_write32(g_cpu.r[REG_SP],0); /* border */
    zmem_write32(g_cpu.r[REG_SP]+4,0x1908);
    zmem_write32(g_cpu.r[REG_SP]+8,0x1401);
    zmem_write32(g_cpu.r[REG_SP]+12,pixels);
    gl_call(T_TexImage2D,0x0de1,0,0x1908,1); /* height on stack? fifth arg */
    /* TexImage2D(target,level,internal,w,h,border,format,type,pixels). */
    zmem_write32(g_cpu.r[REG_SP],1);
    zmem_write32(g_cpu.r[REG_SP]+4,0);
    zmem_write32(g_cpu.r[REG_SP]+8,0x1908);
    zmem_write32(g_cpu.r[REG_SP]+12,0x1401);
    zmem_write32(g_cpu.r[REG_SP]+16,pixels);
    gl_call(T_TexImage2D,0x0de1,0,0x1908,1);
    gl_call(T_Enable,0x0de1,0,0,0);
    gl_call(T_Color4x,0,FX(.46f),FX(.64f),FX(1));
    gl_call(T_TexEnvx,0x2300,0x2200,0x1e01,0);
    gl_call(T_DrawArrays,6,0,4,0);
    expect(0x804020,"FP REPLACE ignores blue current color");
    gl_call(T_TexEnvx,0x2300,0x2200,0x2100,0);
    gl_call(T_Color4x,FX(1),FX(1),FX(1),FX(.5f));
    gl_call(T_Enable,0x0be2,0,0,0);
    gl_call(T_BlendFunc,0x302,0x303,0,0);
    gl_call(T_ClearColorx,0,0,FX(1),FX(1));
    gl_call(T_Clear,0x4000,0,0,0);
    gl_call(T_DrawArrays,6,0,4,0);
    expect(0x40208f,"MODULATE preserves half-alpha transparency");

    /* REPLACE: alpha final vem da textura; alpha de vertice zero nao pode
     * descartar o triangulo. Antes da correcao o discard global zerava a tela. */
    {
        uint32_t colors = zheap_alloc(4 * 4);
        for (i = 0; i < 4; i++) zmem_write32(colors + (uint32_t)i * 4, 0);
        gl_call(T_ColorPointer,4,0x140c,0,colors);
        gl_call(T_EnableClientState,0x8076,0,0,0); /* GL_COLOR_ARRAY */
        gl_call(T_Enable,0x0be2,0,0,0);            /* GL_BLEND */
        gl_call(T_BlendFunc,0x302,0x303,0,0);      /* SRC_ALPHA/1-SRC_ALPHA */
        gl_call(T_TexEnvx,0x2300,0x2200,0x1e01,0); /* GL_REPLACE */
        gl_call(T_ClearColorx,0,0,0,FX(1));
        gl_call(T_Clear,0x4000,0,0,0);
        gl_call(T_DrawArrays,6,0,4,0);
        expect(0x804020,"REPLACE keeps triangle with zero vertex alpha");
    }
    puts("GLES composition: OK");
    return 0;
}
