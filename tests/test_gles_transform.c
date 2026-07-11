/* test_gles_transform.c - harness isolado do rasterizador GLES 1.x
 *
 * Reproduz a sequencia real de chamadas GL do Family Pack (log das sessoes
 * 2026-07-08/2026-07-10, ver docs/PROGRESS.md e STATUS_B_RENDER_FAMILYPACK.md)
 * sem precisar do jogo/CPU rodando: LoadIdentity -> Orthox -> VertexPointer ->
 * TexCoordPointer -> DrawArrays(GL_TRIANGLE_FAN, count=4).
 *
 * Objetivo (Tarefa D, Rodada 5): confirmar/reproduzir de forma determinista o
 * bug reportado de "vertices computam mas coordenadas de tela ficam fora da
 * area visivel" sem depender do bloqueio de signals (Tarefa A).
 *
 * Compilar (MSVC, a partir da raiz do worktree):
 *   cl /nologo /W3 /Isrc /Isrc\core /DZEEBO_CORE_BUILD=1 ^
 *      src\cpu\cpu.c src\cpu\decode.c src\cpu\execute_arm.c ^
 *      src\cpu\execute_thumb.c src\cpu\flags.c ^
 *      src\memory\memory.c src\memory\heap.c ^
 *      src\gpu\framebuffer.c src\gpu\egl_gl.c ^
 *      src\debug\log.c src\debug\trace.c ^
 *      tests\test_gles_transform.c /Fe:test_gles_transform.exe
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/cpu/cpu.h"
#include "../src/memory/memory.h"
#include "../src/gpu/framebuffer.h"
#include "../src/gpu/egl_gl.h"

/* egl_gl.c chama zbrew_stack_arg() (definida em src/brew/brew.c, que nao
 * linkamos aqui para evitar puxar boot.c/loader/audio inteiros). Mesma
 * implementacao real: le a word na SP + n*4. */
uint32_t zbrew_stack_arg(int n) {
    return zmem_read32(g_cpu.r[REG_SP] + (uint32_t)n * 4);
}

/* zegl_create_interface() (chamada indiretamente por zegl_init(), que nao
 * usamos aqui) monta uma vtable via zbrew_make_interface - stub minimo
 * suficiente pra satisfazer o link, nao exercitado neste harness. */
uint32_t zbrew_make_interface(const uint32_t *trap_ids, int n_methods,
                              uint32_t data_words) {
    uint32_t obj = zheap_alloc(4 + data_words * 4);
    uint32_t vtbl = zheap_alloc((uint32_t)n_methods * 4);
    int i;
    if (!obj || !vtbl) return 0;
    for (i = 0; i < n_methods; i++)
        zmem_write32(vtbl + (uint32_t)i * 4, trap_ids[i]);
    zmem_write32(obj, vtbl);
    return obj;
}

void zbrew_mark_frame(void) { /* no-op: nao ha loop de frame neste harness */ }

/* Ordem alfabetica das funcoes GL na tabela direta (ZGL_DIRECT_BASE + fn),
 * espelhando o enum interno (static) de egl_gl.c. Indices re-derivados a
 * mao a partir da lista alfabetica em zgl_fn_name() - qualquer mudanca la
 * precisa ser espelhada aqui. */
enum {
    T_ActiveTexture = 0, T_AlphaFuncx, T_BindTexture, T_BlendFunc,
    T_Clear, T_ClearColorx, T_ClearDepthx, T_ClearStencil,
    T_ClientActiveTexture, T_Color4x, T_ColorMask, T_ColorPointer,
    T_CompressedTexImage2D, T_CompressedTexSubImage2D,
    T_CopyTexImage2D, T_CopyTexSubImage2D, T_CullFace,
    T_DeleteTextures, T_DepthFunc, T_DepthMask, T_DepthRangex,
    T_Disable, T_DisableClientState, T_DrawArrays, T_DrawElements,
    T_Enable, T_EnableClientState, T_Finish, T_Flush, T_Fogx,
    T_Fogxv, T_FrontFace, T_Frustumx, T_GenTextures,
    T_GetError, T_GetIntegerv, T_GetString, T_Hint,
    T_LightModelx, T_LightModelxv, T_Lightx, T_Lightxv,
    T_LineWidthx, T_LoadIdentity, T_LoadMatrixx, T_LogicOp,
    T_Materialx, T_Materialxv, T_MatrixMode, T_MultMatrixx,
    T_MultiTexCoord4x, T_Normal3x, T_NormalPointer, T_Orthox,
    T_PixelStorei, T_PointSizex, T_PolygonOffsetx, T_PopMatrix,
    T_PushMatrix, T_ReadPixels, T_Rotatex, T_SampleCoveragex,
    T_Scalex, T_Scissor, T_ShadeModel, T_StencilFunc,
    T_StencilMask, T_StencilOp, T_TexCoordPointer, T_TexEnvx,
    T_TexEnvxv, T_TexImage2D, T_TexParameterx, T_TexSubImage2D,
    T_Translatex, T_VertexPointer, T_Viewport
};

#define ZGL_DIRECT_BASE 0x80u /* mesmo valor de egl_gl.c (ZGL_DIRECT_BASE) */

static void gl_call(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    g_cpu.r[0] = a0; g_cpu.r[1] = a1; g_cpu.r[2] = a2; g_cpu.r[3] = a3;
    zgl_handle(ZGL_DIRECT_BASE + fn);
}

/* fixed-point 16.16, igual ao formato GLFixed do BREW/GLES 1.x */
static uint32_t FX(float v) { return (uint32_t)(int32_t)(v * 65536.0f); }

int main(void) {
    uint32_t vbuf, tbuf;
    float verts[4 * 3]; /* quad: 4 vertices XYZ, TRIANGLE_FAN */
    float texc[4 * 2];
    int i;
    uint32_t *fb;
    int printed = 0;

    if (!zmem_init()) { fprintf(stderr, "zmem_init falhou\n"); return 1; }
    if (!zfb_init())  { fprintf(stderr, "zfb_init falhou\n");  return 1; }
    memset(&g_cpu, 0, sizeof(g_cpu));
    g_cpu.r[REG_SP] = ZMEM_STACK_TOP;

    /* dados de vertice/texcoord na regiao de heap (mesmo padrao real:
     * ponteiros de heap 0x1001xxxx observados no log do Family Pack) */
    vbuf = zheap_alloc(sizeof(verts));
    tbuf = zheap_alloc(sizeof(texc));
    if (!vbuf || !tbuf) { fprintf(stderr, "zheap_alloc falhou\n"); return 1; }

    /* Quad em coordenadas de PIXEL (100,100)-(540,380) - convencao usual de
     * UI 2D (Orthox(0,640,480,0,...) tambem em unidades de pixel, nao NDC
     * -1..1). Se o pipeline estiver correto, isso deve mapear para uma area
     * bem dentro da tela 640x480. */
    verts[0]=100.f; verts[1]=100.f; verts[2]=0.f;
    verts[3]=540.f; verts[4]=100.f; verts[5]=0.f;
    verts[6]=540.f; verts[7]=380.f; verts[8]=0.f;
    verts[9]=100.f; verts[10]=380.f; verts[11]=0.f;
    texc[0]=0.f; texc[1]=0.f;
    texc[2]=1.f; texc[3]=0.f;
    texc[4]=1.f; texc[5]=1.f;
    texc[6]=0.f; texc[7]=1.f;

    for (i = 0; i < 4*3; i++) zmem_write32(vbuf + (uint32_t)i*4, FX(verts[i]));
    for (i = 0; i < 4*2; i++) zmem_write32(tbuf + (uint32_t)i*4, FX(texc[i]));

    printf("=== Sequencia real do Family Pack (log 2026-07-08/10) ===\n");
    printf("vbuf=0x%08X tbuf=0x%08X\n", vbuf, tbuf);

    /* MatrixMode(MODELVIEW) + LoadIdentity */
    gl_call(T_MatrixMode, 0x1700u, 0, 0, 0);
    gl_call(T_LoadIdentity, 0, 0, 0, 0);

    /* MatrixMode(PROJECTION) + LoadIdentity + Orthox(0,640,480,0,-1,1)
     * (convencao tela: origem no canto superior esquerdo, Y crescendo p/
     * baixo - padrao comum em jogos 2D/menus BREW) */
    gl_call(T_MatrixMode, 0x1701u, 0, 0, 0);
    gl_call(T_LoadIdentity, 0, 0, 0, 0);
    {
        /* Orthox(l,r,b,t,near,far): near/far vem do stack (zbrew_stack_arg) */
        uint32_t near_far[2];
        near_far[0] = FX(-1.f);
        near_far[1] = FX(1.f);
        g_cpu.r[REG_SP] = ZMEM_STACK_TOP - 64;
        zmem_write32(g_cpu.r[REG_SP] + 0, near_far[0]);
        zmem_write32(g_cpu.r[REG_SP] + 4, near_far[1]);
        gl_call(T_Orthox, FX(0.f), FX(640.f), FX(480.f), FX(0.f));
        g_cpu.r[REG_SP] = ZMEM_STACK_TOP;
    }
    gl_call(T_MatrixMode, 0x1700u, 0, 0, 0);

    /* EnableClientState(VERTEX_ARRAY=0x8074) + VertexPointer(3,FIXED,0,vbuf) */
    gl_call(T_EnableClientState, 0x8074u, 0, 0, 0);
    gl_call(T_VertexPointer, 3, 0x140Cu, 0, vbuf);

    /* EnableClientState(TEXTURE_COORD_ARRAY=0x8078) + TexCoordPointer */
    gl_call(T_EnableClientState, 0x8078u, 0, 0, 0);
    gl_call(T_TexCoordPointer, 2, 0x140Cu, 0, tbuf);

    /* DrawArrays(GL_TRIANGLE_FAN=0x6, first=0, count=4) */
    gl_call(T_DrawArrays, 0x6u, 0, 4, 0);

    fb = zfb_pixels();
    printf("\n=== Resultado ===\n");
    printf("fb[0]=0x%08X (canto superior esquerdo)\n", fb[0]);
    printf("fb[centro]=0x%08X (320,240)\n", fb[240 * ZFB_WIDTH + 320]);

    /* varre a VRAM inteira procurando qualquer pixel != cor de clear
     * (0x00000000, framebuffer novo/zerado - zfb_init nao chama clear) */
    for (i = 0; i < ZFB_WIDTH * ZFB_HEIGHT && printed < 12; i++) {
        if (fb[i] != 0) {
            int x = i % ZFB_WIDTH, y = i / ZFB_WIDTH;
            printf("pixel nao-zero em (%d,%d) = 0x%08X\n", x, y, fb[i]);
            printed++;
        }
    }
    if (printed == 0)
        printf("NENHUM pixel nao-zero escrito - triangulos nao rasterizaram nada visivel.\n");
    else
        printf("(%d pixels nao-zero encontrados, mostrando ate 12)\n", printed);

    return 0;
}
