# 🎯 PROMPT ÚNICO - 3 PESSOAS (GENÉRICO)

**Projeto**: Zeebo LibRetro Emulator  
**Objetivo**: 20+ jogos renderizando com imagem em 60 minutos  
**Abordagem**: Trabalho paralelo com papéis específicos

---

## 👥 SEUS PAPÉIS (escolha um)

### Se você é **LUCAS** (Testador/Executor)
➜ Vai para **SEÇÃO 1: TESTER**

### Se você é **CLAUDE ou CODEX** (Analisador/Programador)
➜ Vai para **SEÇÃO 2: ANALYZER**

### Se você é **TERCEIRA PESSOA** (Revisor/Deep Dive)
➜ Vai para **SEÇÃO 3: REVIEWER**

---

## SEÇÃO 1️⃣: TESTER (Lucas)

### Responsabilidade
Validar código em RetroArch, coletar logs, anotar resultados

### Tarefas
1. **Teste MOD direto** (5 min)
   - RetroArch → Load Core → Zeebo
   - Load Content → Pac-Mania.mod
   - Espera 5-10 seg
   - Renderiza cores? Branco? Trava?
   
2. **Teste ZIP** (5 min)
   - Load Content → game.zip
   - RetroArch expande pasta (Archive Browser)
   - Clica em .mod dentro
   - Renderiza? Preto? Cores?

3. **Coleta Logs** (3 min)
   - RetroArch → Information → System Log
   - Procura: `[Zeebo]` `boot: EVT_APP_START` `rodando` `halted`
   - Copia tudo para TESTS_MOD_ZIP.md

4. **Preenche Resultado** (2 min)
   - Arquivo: `TESTS_MOD_ZIP.md`
   - O quê: Qual jogo renderiza? Qual fica preto/branco?

### Output
**Arquivo**: `TESTS_MOD_ZIP.md` (preenchido com resultados)

### Timeline
- Total: 15 min
- Início: AGORA
- Fim: Aguard análise de Analyzer

---

## SEÇÃO 2️⃣: ANALYZER (Claude ou Codex)

### Responsabilidade
Analisar código, identificar bloqueadores, propor fixes

### Tarefas
1. **Lê Contexto** (2 min)
   - COLLABORATION.md (overview)
   - Entender status MOD vs ZIP

2. **Monitora Tester** (3 min)
   - Aguarda TESTS_MOD_ZIP.md
   - Conforme preenche, começa análise paralela

3. **Analisa Código** (7 min)
   - `src/loader/mod_loader.c` (ZIP rejection)
   - `src/brew/boot.c` (event loop, halted)
   - `src/gpu/egl_gl.c` (rendering)
   
4. **Identifica 3 Bloqueadores** (2 min)
   - Qual é crítico?
   - Qual é alto?
   - Qual é médio?

5. **Propõe Fixes** (1 min)
   - Qual código mudar?
   - Qual função chamar?

6. **Preenche Resultado** (1 min)
   - Arquivo: `ANALYSIS_FINDINGS.md`
   - O quê: Bloqueadores + código a mudar

### Output
**Arquivo**: `ANALYSIS_FINDINGS.md` (preenchido com análise)

### Timeline
- Total: 15 min
- Início: IMEDIATO (paralelo com Tester)
- Fim: Antes de implementar

---

## SEÇÃO 3️⃣: REVIEWER (Terceira Pessoa)

### Responsabilidade
Deep dive em renderização, encontrar bugs visuais

### Tarefas
1. **Lê Contexto** (2 min)
   - COLLABORATION.md
   - Entender MOD renderiza branco/cores

2. **Monitora Tester** (3 min)
   - Aguarda TESTS_MOD_ZIP.md
   - Conforme preenche, começa análise

3. **Analisa Rendering Pipeline** (7 min)
   - glClear (zfb_clear?)
   - glDrawArrays (draw_prim?)
   - Viewport (g_gl_viewport)
   - Pointer validation (decode_vertex_ptr?)

4. **Verifica Implementação** (2 min)
   - Código está lá?
   - Funciona?
   - Qual bug?

5. **Propõe Render Fixes** (1 min)
   - Qual função bugada?
   - Qual fix aplicar?

6. **Preenche Resultado** (1 min)
   - Arquivo: `RENDERING_ANALYSIS.md`
   - O quê: Bugs + código a mudar

### Output
**Arquivo**: `RENDERING_ANALYSIS.md` (preenchido com análise)

### Timeline
- Total: 15 min
- Início: IMEDIATO (paralelo com Tester)
- Fim: Antes de implementar

---

## ⏰ TIMELINE GERAL

```
MIN 0-3:    SETUP (abrir docs, RetroArch, IDE)
MIN 3-10:   PARALELO
            - TESTER: Coleta MOD + ZIP
            - ANALYZER: Analisa código
            - REVIEWER: Deep dive rendering

MIN 10-12:  DOCUMENTAÇÃO (preencher templates)
MIN 12-15:  SINCRONIZAÇÃO (ler resultados)
MIN 15-20:  IMPLEMENTAÇÃO (Analyzer codifica)
MIN 20-25:  VALIDAÇÃO (Tester testa DLL nova)
MIN 25+:    PRÓXIMA ITERAÇÃO
```

---

## 📂 ARQUIVOS COMPARTILHADOS

Todos em: `C:\Users\Lucas\source\repos\zeebo_libretro\`

| Arquivo | Quem preenche | O quê |
|---------|---|---|
| COLLABORATION.md | [Referência] | Overview |
| TESTS_MOD_ZIP.md | **TESTER** | Resultados de testes |
| ANALYSIS_FINDINGS.md | **ANALYZER** | Bloqueadores + fixes |
| RENDERING_ANALYSIS.md | **REVIEWER** | Bugs + fixes rendering |
| FIXES_TO_IMPLEMENT.md | **TODOS** | Consolidação final |

---

## 🚀 COMECE AGORA

### Se você é TESTER (Lucas):
```
1. Abra RetroArch
2. Load Core → Zeebo
3. Load Content → Pac-Mania.mod
4. [Espera 5-10 seg]
5. Anota em TESTS_MOD_ZIP.md
```

### Se você é ANALYZER (Claude ou Codex):
```
1. cd C:\Users\Lucas\source\repos\zeebo_libretro
2. Lê: COLLABORATION.md
3. Monitora: TESTS_MOD_ZIP.md (conforme Tester preenche)
4. Analisa: src/loader/mod_loader.c, src/brew/boot.c
5. Preenche: ANALYSIS_FINDINGS.md
```

### Se você é REVIEWER (Terceira Pessoa):
```
1. cd C:\Users\Lucas\source\repos\zeebo_libretro
2. Lê: COLLABORATION.md
3. Monitora: TESTS_MOD_ZIP.md (conforme Tester preenche)
4. Analisa: src/gpu/egl_gl.c, rendering pipeline
5. Preenche: RENDERING_ANALYSIS.md
```

---

## ✅ CHECKLIST por Papel

### TESTER
- [ ] RetroArch pronto
- [ ] MOD + ZIP testados
- [ ] Logs coletados
- [ ] TESTS_MOD_ZIP.md preenchido

### ANALYZER
- [ ] COLLABORATION.md lido
- [ ] Código analisado
- [ ] 3 bloqueadores identificados
- [ ] ANALYSIS_FINDINGS.md preenchido

### REVIEWER
- [ ] COLLABORATION.md lido
- [ ] Rendering pipeline verificado
- [ ] Bugs identificados
- [ ] RENDERING_ANALYSIS.md preenchido

---

## 💡 CONTEXTO

**Problema**:
- MOD renderiza imagem ✅
- ZIP fica preto ❌
- Renderização pode parar após frame 1 ❌

**Solução**:
- 3 pessoas analisam em paralelo
- Identificam raiz do problema
- Implementam 1 fix crítico
- Validam com 20+ jogos

**Meta**: 20+ jogos rodando com imagem em 60 min

---

## 🎯 FIM

**Próximo**: Escolha seu papel acima e comece!

Dúvidas? Revise COLLABORATION.md

