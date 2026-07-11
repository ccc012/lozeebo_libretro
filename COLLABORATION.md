# COLABORAÇÃO 3 PESSOAS - ZEEBO LIBRETRO

**Data:** 2026-07-11  
**Objetivo:** Identificar + fixar bloqueadores pra renderizar 20+ jogos em 60 min  
**Abordagem:** Análise paralela

---

## STATUS

- ✅ MOD (direto) renderiza imagem  
- ❌ ZIP fica preto (código rejeita em mod_loader.c:145)  
- ✅ 2 fixes já aplicadas (Pac-Mania + glVertexPointer)  
- ⏳ Próximas 3 fixes prontas para ir

---

## COMUNICAÇÃO

Todos preenchem seus arquivos de análise:

| Arquivo | Responsável | O quê |
|---------|-------------|-------|
| TESTS_MOD_ZIP.md | PESSOA 1 | Testes em RetroArch |
| ANALYSIS_FINDINGS.md | PESSOA 2 (Claude) | Análise de código |
| RENDERING_ANALYSIS.md | PESSOA 3 | Rendering deep dive |
| FIXES_TO_IMPLEMENT.md | TODAS | Consolidação |

---

## CONTEXTO TÉCNICO

**Problema principal:**  
- MOD renderiza (funciona!)  
- ZIP fica tela preta (rejeita arquivo?)  

**Pipeline de renderização:**  
1. ✅ Carregamento de MOD  
2. ✅ Execução ARM  
3. ✅ BREW HLE  
4. ✅ GL/GLES (rasterizador software)  
5. ❌ Só renderiza first frame (event loop falta)  

**Bloqueadores conhecidos:**  
1. Event loop não contínuo (SetTimer implementado mas não acionado)  
2. ZIP talvez rejeite em mod_loader.c  
3. Renderização pode estar desabilitada em frames > 1  

---

## PRÓXIMAS AÇÕES

Depois que PESSOA 1 testar e preencher TESTS_MOD_ZIP.md:
- PESSOA 2 (Claude): Analisa mod_loader.c, identifica bloqueadores em código
- PESSOA 3 (IA): Deep dive em egl_gl.c, verifica renderização
- TODAS: Discutem 3 bloqueadores + propose fixes
- Implementam + testam
