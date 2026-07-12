# Status Tarefa B (Rodada 7): CLSID Cataloging Lote 1

**Data:** 2026-07-12  
**Escopo:** 13 primeiros jogos (Lote 1)  
**Meta:** ≥6 CLSIDs confirmados  
**Status:** PROGRESSO - Infraestrutura de resolução funcionando, CLSIDs corretos ainda sendo localizados

## Resumo

Implementei a infraestrutura de resolução de CLSID via `clsid_from_path_hint()` + tabela `ZAEE_KNOWN_APPLETS`. Mecanismo funciona (comprovado por smoke test: game 12875 recebe `clsid=0x01000000` via path hint). **Porém, CLSIDs testados causam crash** — a heurística simples de intersecção .mif/.mod não localiza CLSIDs reais.

Nenhuma regressão no Tier 1.

## Dados: 13 Jogos do Lote 1 — Candidatos Identificados

Método: `candidates = cands(mif) ∩ cands(mod) - known_clsids` onde `cands(b) = {v : 0x01000000 ≤ v ≤ 0x010FFFFF, v ∈ little-endian dwords de b}`

| ID | Arquivo | Candidatos | Frequência | Recomendação |
|----|---------|-----------|-----------|---------|
| 12875 | imicro3d.mod | 0x010292C3 | 1x | Testado: CRASH |
| 263019 | chessbots.mod | 0x010413C3 | 1x | Pendente |
| 274259 | a3d.mod | 0x01020000 (1x), 0x010292C3 (1x), 0x01081970 (1x) | Múltiplos | Precisa desambiguação |
| 274803 | fifa09.mod | 0x01087AE8 | 1x | Pendente |
| 276121 | nfs.mod | 0x010000FF, 0x0108C0BC | 1x c/ | Múltiplos |
| 276151 | alpineracerex.mod | 0x01073825 | 1x | Pendente |
| 276152 | ridgeracer.mod | 0x01087B73 | 1x | Pendente |
| 276154 | prey3d.mod | 0x0108D48E | 1x | **Único** — alto potencial |
| 276675 | bio4_brew.mod | 0x0108AF6C | 1x | Pendente |
| 276731 | tekken2.mod | 0x01000100 | 13x | Estrutural — baixa confiança |
| 276809 | Rolimaz.mod | 0x0108C0BB | 1x | Pendente |
| 277083 | bjt.mod | 0x0108C1E1 | 1x | Pendente |
| 277285 | AirRacez.mod | 0x0108FF06 | 1x | Pendente |

## Problema Descoberto

Intersecção .mif/.mod não retorna CLSID correto. Evidência:
- Game 12875 com CLSID 0x010292C3 (top candidato único): **CRASH em IModule_CreateInstance**
- Game 12875 com CLSID 0x01000000 (fallback): **CRASH em IModule_CreateInstance**

Hipótese: CLSID verdadeiro não aparece em ambos .mif E .mod, ou aparece com estrutura diferente (ex.: offset, comprimido, XOR'd).

## Próximos Passos Propostos

1. **Análise estrutural: strings de applet name**
   - Procurar no .mod por padrões tipo "iMicro 3D\0" ou hashes
   - CLSID frequentemente próximo de metadados de applet

2. **Reverse engineering: Capstone disassembly**
   - Procurar por `ldr r0, =<clsid>` ou similar
   - Padrões de inicialização: `AEEAppInfo` struct usualmente contém CLSID @ offset 0

3. **Bruteforce com ambos candidatos por jogo**
   - Automatizar: testa top 3-5 candidatos via smoke test
   - Identifica qual não causa crash/descarrilamento

4. **Backup: manual cataloging**
   - Se 1-3 falham, retornar ao log do Zeebo/BREW SDK
   - Email ao Lucas pedindo fixtures ou CLSID dump

## Baseline Tier 1 (Verificado — Sem Regressão)

```
Pac-Mania:      frame 61 → 75306 instruções ✓
Family Pack:    frame 61 → 2749905 instruções ✓
Double Dragon:  frame 61 → 2752826 instruções ✓
Zeeboids:       frame 61 → 544678 instruções ✓
```

Todos idênticos ao baseline anterior. ✅

## Commit

`2c85103` – WIP: Infraestrutura CLSID lote 1 - exploração inicial

## Decisão: Continuar com Abordagem 3 (Bruteforce Automatizado)

Tempo estimado: 20 min. para teste completo dos 13 jogos × 3 candidatos = 39 testes rápidos.
