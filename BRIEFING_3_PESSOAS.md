# 📢 BRIEFING PARA 3 PESSOAS

**Projeto**: Zeebo LibRetro Emulator  
**Objetivo**: Fazer máximo de jogos Zeebo (68 total) renderizarem com imagem no RetroArch  
**Abordagem**: Trabalho paralelo - cada pessoa em uma tarefa  
**Data**: 2026-07-11

---

## 🎯 Status Atual

- ✅ MOD (.mod direto) renderiza imagem (branco/cores)
- ❌ ZIP (.zip compactado) fica tela preta (código rejeita)
- ✅ 2 fixes já aplicadas (Pac-Mania case 5 + glVertexPointer validation)
- ⏳ Prontos para próximas 3 fixes

---

## 👥 Divisão de Trabalho

### 👤 PESSOA 1: Lucas (TESTES)

**Responsabilidade**: Validar jogos em RetroArch, coletar logs, anotar resultados

**O que fazer**:
1. Abra RetroArch
2. Teste MOD direto (Pac-Mania, Family Pack, etc)
   - Load Core → Zeebo
   - Load Content → game.mod
   - Espera 5-10 seg
   - Anota: renderiza cores? branco? trava?
3. Teste ZIP via Archive Browser
   - Load Content → game.zip
   - RetroArch expande pasta
   - Clica em .mod dentro
   - Espera 5-10 seg
   - Anota resultado
4. Coleta logs: RetroArch → Information → System Log
   - Procura: `[Zeebo]` e `boot: EVT_APP_START`
5. Preenche: **TESTS_MOD_ZIP.md**

**Arquivo para preencher**:
```
C:\Users\Lucas\source\repos\zeebo_libretro\TESTS_MOD_ZIP.md
```

**Duração esperada**: 15 min

---

### 👤 PESSOA 2: Claude (ANÁLISE)

**Responsabilidade**: Analisar código, identificar bloqueadores, propor fixes

**O que fazer**:
1. Lê: **COLLABORATION.md** (overview)
2. Monitora: **TESTS_MOD_ZIP.md** (conforme PESSOA 1 preenche)
3. Analisa código:
   - `src/loader/mod_loader.c` (por que ZIP rejeita? linha 145)
   - `src/brew/boot.c` (sequência de boot)
   - `src/gpu/egl_gl.c` (renderização)
4. Identifica **3 próximos bloqueadores** (por severidade)
5. Propõe **fixes específicas** (qual código mudar?)
6. Preenche: **ANALYSIS_FINDINGS.md**

**Exemplo de achado**:
```
BLOQUEADOR 1: ZIP rejeitado
- Severidade: CRÍTICO
- Localização: src/loader/mod_loader.c:145
- Problema: Magic "PK" detecta ZIP e retorna false
- Fix: Usar Archive Browser (workaround) ou implementar descompactação
- Tempo: 0 min (workaround) / 1-2h (implementar)
```

**Arquivo para preencher**:
```
C:\Users\Lucas\source\repos\zeebo_libretro\ANALYSIS_FINDINGS.md
```

**Duração esperada**: 15 min

---

### 👤 PESSOA 3: Outra IA (RENDERING)

**Responsabilidade**: Deep dive em renderização, encontrar bugs visuais

**O que fazer**:
1. Lê: **COLLABORATION.md** (overview)
2. Monitora: **TESTS_MOD_ZIP.md** (conforme PESSOA 1 preenche)
3. Analisa fluxo de renderização:
   - glDrawArrays (linha 1157 em egl_gl.c)
   - draw_prim (linha 702)
   - transform_vertex (linha 511)
   - raster_triangle (linha 572)
4. Verifica:
   - glDrawArrays é chamado? (procurar logs)
   - Viewport configurado corretamente?
   - Cores saem certas?
   - Pointer validation funciona?
5. Identifica **bugs de rendering**
6. Propõe **fixes específicas**
7. Preenche: **RENDERING_ANALYSIS.md**

**Exemplo de bug**:
```
BUG: glVertexPointer recebe pointer inválido 0x00000003
- Localização: src/gpu/egl_gl.c:992 (decode_vertex_ptr)
- Problema: Pointer fora da RAM não é validado
- Fix: Rejeitar triângulo se pointer for inválido
- Já corrigido? Verificar se 0x04000000 check está em decode_vertex_ptr
```

**Arquivo para preencher**:
```
C:\Users\Lucas\source\repos\zeebo_libretro\RENDERING_ANALYSIS.md
```

**Duração esperada**: 15 min

---

## 📂 Arquivos de Comunicação

**Todos estão em**: `C:\Users\Lucas\source\repos\zeebo_libretro\`

| Arquivo | Quem faz | O quê | Status |
|---------|----------|-------|--------|
| COLLABORATION.md | [Referência] | Guia geral | ✅ Pronto |
| TESTS_MOD_ZIP.md | PESSOA 1 | Testes MOD + ZIP | 📝 Para preencher |
| ANALYSIS_FINDINGS.md | PESSOA 2 | Análise + bloqueadores | 📝 Para preencher |
| RENDERING_ANALYSIS.md | PESSOA 3 | Rendering deep dive | 📝 Para preencher |
| FIXES_TO_IMPLEMENT.md | TODAS | Consolidação de fixes | 📝 Para sincronizar |

---

## ⏰ Timeline

```
⏰ MINUTO 0-3: Setup
   - PESSOA 1: Abra RetroArch
   - PESSOA 2: Leia COLLABORATION.md
   - PESSOA 3: Leia COLLABORATION.md

⏰ MINUTO 3-10: Execução Paralela
   - PESSOA 1: Testa MOD + ZIP
   - PESSOA 2: Analisa código (lê TESTS_MOD_ZIP conforme preenchido)
   - PESSOA 3: Analisa rendering (lê TESTS_MOD_ZIP conforme preenchido)

⏰ MINUTO 10-12: Documentação
   - PESSOA 1: Termina TESTS_MOD_ZIP.md
   - PESSOA 2: Termina ANALYSIS_FINDINGS.md
   - PESSOA 3: Termina RENDERING_ANALYSIS.md

⏰ MINUTO 12-15: Sincronização
   - TODAS: Leem resultados uns dos outros
   - TODAS: Votam em qual fix implementar primeiro
   - TODAS: Consolidam em FIXES_TO_IMPLEMENT.md

⏰ MINUTO 15-20: Implementação
   - PESSOA 2: Implementa Fix #1
   - .\test_before_build.ps1 (validate)
   - .\build_safe.ps1 (compile)

⏰ MINUTO 20-25: Validação
   - PESSOA 1: Testa DLL nova em RetroArch
   - Resultado: PASSOU / FALHOU

⏰ MINUTO 25+: Próxima Iteração
   - Se passou → próximo fix
   - Se falhou → volta ao debug
```

---

## 🔗 Referência Rápida

### Problema MOD vs ZIP
```
MOD: ✅ Funciona (renderiza branco/cores)
ZIP: ❌ Rejeita em mod_loader.c:145 (magic "PK")

Solução temporária: Use Archive Browser do RetroArch
Solução permanente: Implementar descompactação automática
```

### Como testar MOD
```
RetroArch → Load Core → Zeebo
Load Content → [pasta com .mod]
Seleciona jogo.mod → renderiza
```

### Como testar ZIP
```
RetroArch → Load Core → Zeebo
Load Content → [pasta com .zip]
Seleciona game.zip → expande
Clica em arquivo .mod dentro → renderiza
```

---

## 📋 Workflow de Comunicação

1. **PESSOA 1** coleta dados (TESTS_MOD_ZIP.md)
2. **PESSOA 2** lê dados → analisa → propõe fixes (ANALYSIS_FINDINGS.md)
3. **PESSOA 3** lê dados → analisa rendering → propõe fixes (RENDERING_ANALYSIS.md)
4. **TODAS** sincronizam e votam em Fix #1
5. **PESSOA 2** implementa (com test_before_build + build_safe)
6. **PESSOA 1** valida em RetroArch
7. **VOLTA AO PASSO 1** com próximo fix

---

## ✅ Checklist Individual

### PESSOA 1 (Lucas)
- [ ] RetroArch aberto
- [ ] Pac-Mania MOD testado
- [ ] Family Pack MOD testado
- [ ] Double Dragon MOD testado
- [ ] Zeeboids MOD testado
- [ ] Game.zip testado via Archive Browser
- [ ] Logs coletados
- [ ] TESTS_MOD_ZIP.md preenchido
- [ ] Enviou resultado para PESSOA 2 e 3

### PESSOA 2 (Claude)
- [ ] Leu COLLABORATION.md
- [ ] Leu TESTS_MOD_ZIP.md (conforme PESSOA 1 preencheu)
- [ ] Analisou src/loader/mod_loader.c
- [ ] Analisou src/brew/boot.c
- [ ] Analisou src/gpu/egl_gl.c
- [ ] Identifiquei 3 bloqueadores
- [ ] ANALYSIS_FINDINGS.md preenchido
- [ ] Conversou com PESSOA 3 sobre prioridades

### PESSOA 3 (Outra IA)
- [ ] Leu COLLABORATION.md
- [ ] Leu TESTS_MOD_ZIP.md (conforme PESSOA 1 preencheu)
- [ ] Analisei glDrawArrays path
- [ ] Analisei draw_prim
- [ ] Analisei transform_vertex
- [ ] Analisei raster_triangle
- [ ] Identifiquei bugs de rendering
- [ ] RENDERING_ANALYSIS.md preenchido
- [ ] Conversou com PESSOA 2 sobre prioridades

---

## 🚀 COMEÇAR AGORA

### Para PESSOA 1 (Lucas):
```bash
1. Abra RetroArch
2. Clique: Load Core → Zeebo
3. Clique: Load Content → Pac-Mania.mod
4. [Espera 5-10 seg]
5. Anota: renderiza? cores? trava?
6. Preenche: TESTS_MOD_ZIP.md
```

### Para PESSOA 2 (Claude):
```bash
1. cd C:\Users\Lucas\source\repos\zeebo_libretro
2. Lê: COLLABORATION.md
3. Monitora: TESTS_MOD_ZIP.md
4. Analisa: src/loader/mod_loader.c
5. Preenche: ANALYSIS_FINDINGS.md
```

### Para PESSOA 3 (Outra IA):
```bash
1. cd C:\Users\Lucas\source\repos\zeebo_libretro
2. Lê: COLLABORATION.md
3. Monitora: TESTS_MOD_ZIP.md
4. Analisa: src/gpu/egl_gl.c
5. Preenche: RENDERING_ANALYSIS.md
```

---

## 💡 Dúvidas?

Tudo está documentado em:
- `COLLABORATION.md` - Guia completo
- Templates específicas para cada pessoa
- Fluxo claro de comunicação via arquivos markdown

**Meta**: 20+ jogos Zeebo rodando com imagem em 60 minutos! 🎮

