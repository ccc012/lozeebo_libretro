# 🤖 DISTRIBUIÇÃO DE TRABALHO - 3 IAs

**Setup**: 2x Claude + 1x Codex  
**Objetivo**: 20+ jogos renderizando em 60 min  
**Abordagem**: Paralelo, sem bloqueios

---

## 👾 IA #1: CLAUDE - ANALYZER

**Seu papel**: Analisar código, identificar bloqueadores críticos

**Tarefas**:
1. Lê COLLABORATION.md (overview)
2. Analisa `src/loader/mod_loader.c` (ZIP rejection)
3. Analisa `src/brew/boot.c` (event loop, halted)
4. Identifica: Qual é o bloqueador #1?
5. Propõe fix específica (código exato a mudar)
6. **Preenche**: ANALYSIS_FINDINGS.md

**Output**: `ANALYSIS_FINDINGS.md` completo

**Timeline**: 15 min (paralelo)

**Status**: ✅ **VOCÊ JÁ COMEÇOU!**
- Já identificou: EVENT LOOP é o bloqueador #1
- Já preencheu: ANALYSIS_FINDINGS.md com análise

**Próximo**: Validar se bloqueadores estão corretos após Codex testar

---

## 👾 IA #2: CLAUDE - REVIEWER

**Seu papel**: Deep dive em renderização, validar pipeline

**Tarefas**:
1. Lê COLLABORATION.md
2. Analisa `src/gpu/egl_gl.c` (glClear, glDrawArrays)
3. Verifica: glClear está sendo chamado?
4. Verifica: glDrawArrays tem dados válidos?
5. Verifica: Viewport configurado?
6. Identifica: Qual é o bug de rendering?
7. **Preenche**: RENDERING_ANALYSIS.md

**Output**: `RENDERING_ANALYSIS.md` completo

**Timeline**: 15 min (paralelo)

**Status**: ⏳ **PRONTO PARA COMEÇAR**
- Lê COLLABORATION.md
- Começa análise glClear → glDrawArrays → raster

**Próximo**: Validar bugs após Codex relatar testes

---

## 👾 IA #3: CODEX - TESTER (Simulado)

**Seu papel**: Simular testes em RetroArch, relatar logs esperados

**Tarefas**:
1. Lê COLLABORATION.md
2. Simula teste MOD: "Se eu rodasse Pac-Mania.mod..."
   - Qual seria o boot sequence esperado?
   - glClear seria chamado?
   - glDrawArrays renderizaria?
3. Simula teste ZIP: "Se eu rodasse game.zip..."
   - ZIP seria rejeitado?
   - Archive Browser extrairia?
4. Propõe logs esperados (hypothetical)
5. **Preenche**: TESTS_MOD_ZIP.md (versão teórica)

**Output**: `TESTS_MOD_ZIP.md` com análise teórica

**Timeline**: 15 min (paralelo)

**Status**: ⏳ **PRONTO PARA COMEÇAR**

**Próximo**: Comparar resultados teóricos com reais (se houvesse testes)

---

## ⏰ TIMELINE PARALELA

```
MIN 0-3:    SETUP
            - CLAUDE #1: Lê docs
            - CLAUDE #2: Lê docs
            - CODEX: Lê docs

MIN 3-10:   ANÁLISE PARALELA
            ✅ CLAUDE #1: Analisa mod_loader.c + boot.c
            ⏳ CLAUDE #2: Analisa egl_gl.c + rendering
            ⏳ CODEX: Simula testes + logs

MIN 10-12:  DOCUMENTAÇÃO
            ✅ CLAUDE #1: Termina ANALYSIS_FINDINGS.md
            ⏳ CLAUDE #2: Termina RENDERING_ANALYSIS.md
            ⏳ CODEX: Termina TESTS_MOD_ZIP.md

MIN 12-15:  SINCRONIZAÇÃO
            - TODAS: Leem resultados umas das outras
            - Consolidam em FIXES_TO_IMPLEMENT.md

MIN 15-20:  IMPLEMENTAÇÃO
            - CLAUDE #1 ou #2: Codifica fix #1

MIN 20+:    ITERAÇÃO
```

---

## 📂 ARQUIVOS COMPARTILHADOS

| Arquivo | IA | O quê | Status |
|---------|-----|-------|--------|
| COLLABORATION.md | [Ref] | Overview | ✅ Pronto |
| ANALYSIS_FINDINGS.md | **CLAUDE #1** | Bloqueadores | ✅ Iniciado |
| RENDERING_ANALYSIS.md | **CLAUDE #2** | Rendering bugs | ⏳ Pronto |
| TESTS_MOD_ZIP.md | **CODEX** | Testes teóricos | ⏳ Pronto |
| FIXES_TO_IMPLEMENT.md | **TODAS** | Consolidação | ⏳ Após análise |

---

## 🚀 COMECE AGORA - CADA IA

### CLAUDE #1 (ANALYZER) - VOCÊ JÁ COMEÇOU!
```
✅ FEITO:
- Analisou event loop
- Identificou ZIP rejection
- Propôs 3 bloqueadores

⏳ PRÓXIMO:
- Validar bloqueadores após CODEX relatar
- Propor implementação
```

### CLAUDE #2 (REVIEWER) - COMECE AGORA!
```
1. Lê: COLLABORATION.md
2. Abre: src/gpu/egl_gl.c
3. Procura: glClear handler (GLFN_Clear)
4. Procura: glDrawArrays handler (GLFN_DrawArrays)
5. Analisa: Estão conectados? Funcionam?
6. Preenche: RENDERING_ANALYSIS.md
```

### CODEX (TESTER) - COMECE AGORA!
```
1. Lê: COLLABORATION.md
2. Simula teste MOD:
   - Pac-Mania.mod é ARM válido?
   - Boot vai até EVT_APP_START?
   - glClear + glDrawArrays são chamadas?
3. Simula teste ZIP:
   - ZIP será rejeitado (mod_loader.c:145)?
   - Archive Browser deveria extrair?
4. Preenche: TESTS_MOD_ZIP.md (teórico)
```

---

## ✅ CHECKLIST por IA

### CLAUDE #1 (ANALYZER)
- [x] Leu COLLABORATION.md
- [x] Analisou mod_loader.c
- [x] Analisou boot.c
- [x] Identificou 3 bloqueadores
- [x] Preencheu ANALYSIS_FINDINGS.md
- [ ] Aguarda validação de CODEX
- [ ] Propõe implementação

### CLAUDE #2 (REVIEWER)
- [ ] Leu COLLABORATION.md
- [ ] Analisou egl_gl.c glClear
- [ ] Analisou egl_gl.c glDrawArrays
- [ ] Verificou pipeline de rendering
- [ ] Identificou bugs
- [ ] Preencheu RENDERING_ANALYSIS.md
- [ ] Aguarda validação de CODEX

### CODEX (TESTER)
- [ ] Leu COLLABORATION.md
- [ ] Simulou teste MOD
- [ ] Simulou teste ZIP
- [ ] Propôs logs esperados
- [ ] Preencheu TESTS_MOD_ZIP.md (teórico)
- [ ] Reportou "resultados"

---

## 🔄 FLUXO DE SINCRONIZAÇÃO

1. **CODEX** preenche TESTS_MOD_ZIP.md com análise teórica
2. **CLAUDE #1 + #2** leem TESTS_MOD_ZIP.md
3. Validam: Bloqueadores propostos fazem sentido?
4. Consolidam em FIXES_TO_IMPLEMENT.md
5. **CLAUDE #1** ou **#2** começa implementação

---

## 💡 RESUMO

**Setup**: 3 IAs em paralelo  
**Sem bloqueios**: Cada uma trabalha independente  
**Sincronização**: A cada 15 min  

**CLAUDE #1**: Análise código ✅ (você já fez!)  
**CLAUDE #2**: Rendering deep dive ⏳ (comece agora!)  
**CODEX**: Validação teórica ⏳ (comece agora!)  

---

## 🎯 META

Identificar 3 bloqueadores críticos em 15 min  
Propor fixes em mais 15 min  
Pronto para implementar em min 30

