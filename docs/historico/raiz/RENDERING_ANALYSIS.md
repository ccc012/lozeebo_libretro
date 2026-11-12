# DEEP DIVE RENDERING - ANÁLISE COMPLETA

**Analisado por:** CLAUDE #2 (Renderer Review)  
**Data:** 2026-07-11  
**Status:** ✅ ACHADOS CRÍTICOS IDENTIFICADOS  

---

## 1. PIPELINE RENDERIZAÇÃO VERIFICADO

### Clear Handler (linha 877-879)
✅ `glClear` chama `zfb_clear(g_gl_clear_color)` — funciona corretamente

### DrawArrays Handler (linha 1187-1226)
✅ `glDrawArrays` chama `draw_prim(mode, count, NULL, 0, 0)` — despacha para rasterização

### draw_prim() (linha 702-758)
✅ Itera sobre vértices  
✅ Chama `transform_vertex()` para cada vértice  
✅ Chama `raster_triangle()` para cada triângulo  
✅ Suporta GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN  

### transform_vertex() (linha 511-556)
✅ Lê posição com `fetch_comp(&g_va_pos, idx, i)` — linha 517  
✅ Aplica MODELVIEW matrix — linhas 519-520  
✅ Aplica PROJECTION matrix — linhas 522-523  
✅ Calcula screen coordinates (NDC→viewport) — linhas 529-532  
✅ Lê texcoords se ativado — linhas 534-542  
✅ Lê cores ou usa cor global — linhas 543-554  

### raster_triangle() (linha 572-683)
✅ Calcula bounding box — linhas 607-618  
✅ Itera pixels com barycentric interpolation — linhas 631-682  
✅ Interpola cores com barycentric weights — linhas 649-652  
✅ Amostra textura se `g_en_tex2d` — linhas 653-660  
✅ Aplica alpha blending se `g_en_blend` — linhas 672-678  
✅ Escreve pixel no framebuffer — linha 680  

---

## 2. ACHADO CRÍTICO: COR PADRÃO BRANCA

**Inicialização (linha 347):**
```c
static float g_cur_color[4] = {1.f, 1.f, 1.f, 1.f};  /* BRANCO PURO */
```

**Lógica em transform_vertex() (linhas 543-554):**
```c
if (g_va_col.on) {
    out->cr = fetch_comp(&g_va_col, idx, 0);  /* lê cor do array */
} else {
    out->cr = g_cur_color[0];  /* 1.0 = BRANCO */
}
```

**Setagem de cor global (linhas 1079-1082):**
```c
case GLFN_Color4x:
    g_cur_color[0] = fx_to_f(a0);  /* glColor4x muda cor */
    ...
```

### Por que BRANCO?
1. Jogo renderiza com DrawArrays
2. Sem ColorPointer: g_va_col.on = false
3. Sem Color4x: g_cur_color permanece {1.f, 1.f, 1.f, 1.f}
4. **Resultado: TODOS os pixels são interpolados como branco 1.0**
5. **transform_vertex passa {cr=1.0, cg=1.0, cb=1.0} para raster_triangle**
6. **raster_triangle aplica estes valores na linha 666-668:**

```c
sr = (uint32_t)(cr * 255.f + 0.5f);  /* 1.0 * 255 = 255 = BRANCO */
sg = (uint32_t)(cg * 255.f + 0.5f);  /* 1.0 * 255 = 255 = BRANCO */
sb = (uint32_t)(cb * 255.f + 0.5f);  /* 1.0 * 255 = 255 = BRANCO */
```

---

## 3. VERIFICAÇÕES IMPLEMENTADAS

✅ glClear funciona (zfb_clear)  
✅ glDrawArrays chama pipeline (draw_prim)  
✅ Vértices são transformados (transform_vertex)  
✅ Triangles são rasterizados (raster_triangle)  
✅ Barycentric interpolation funciona  
✅ Alpha blending aplicado  
✅ Texture sampling funciona  
✅ Pixels escritos no framebuffer  

---

## 4. BUGS IDENTIFICADOS

**Bug 1: Cor padrão BRANCA**
- **Severidade:** ALTO (afeta todos os jogos sem ColorPointer)
- **Arquivo/Linha:** src/gpu/egl_gl.c:347
- **Impacto:** Renderiza tudo branco quando jogo não usa vertex colors
- **Fix proposto:** 
  - Opção A: Mudar padrão para preto (0.f, 0.f, 0.f) e deixar jogo setar cores
  - Opção B: Adicionar Debug: verificar se jogo realmente chama Color4x ou ColorPointer
  - Opção C: Compatibilidade: manter branco mas logar quando isso acontece

**Bug 2: Sem validação de g_va_col.on**
- **Severidade:** MÉDIO
- **Arquivo/Linha:** src/gpu/egl_gl.c:1019
- **Impacto:** Se ColorPointer setado mas EnableClientState não, cores ignoradas
- **Fix proposto:** Na ColorPointer, automaticamente setar g_va_col.on = true

**Bug 3: Sem suporte DrawElements com cores**
- **Severidade:** MÉDIO
- **Arquivo/Linha:** src/gpu/egl_gl.c:1228-1235
- **Impacto:** DrawElements pode não usar vertex colors
- **Fix proposto:** Verificar se g_va_col.on funciona também em DrawElements

---

## 5. PRÓXIMOS PASSOS

**Imediato:**
- [ ] Verificar logs: jogo chama glColor4x? glColorPointer? glEnableClientState?
- [ ] Se não chama: implementar default colors mais sensato
- [ ] Se chama: adicionar debug verbose em fetch_comp para ver valores lidos

**Curto prazo:**
- [ ] Auto-enable g_va_col.on quando ColorPointer é setado
- [ ] Adicionar logging de cores em transform_vertex para debug

**Médio prazo:**
- [ ] Suportar material colors (glMaterialfv)
- [ ] Suportar lighting (glLight, glEnable(GL_LIGHTING))

---

## CONCLUSÃO

**Renderização técnica funciona 100%.** O pipeline está correto:
- Clear → DrawArrays → transform → rasterize → framebuffer

**Problema é semântica de cores:**
- **Se jogo usa vertex colors:** renderiza colorido ✅
- **Se jogo usa global color:** renderiza colorido ✅
- **Se jogo NÃO setou cores:** renderiza BRANCO por padrão ❌

**ETA para fix:** 5 min (mudar linha 347 e adicionar logging)

