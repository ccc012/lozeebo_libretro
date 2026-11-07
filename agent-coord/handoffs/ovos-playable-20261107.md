# Handoff — `ovos-playable`

**Data:** 2026-11-07  
**De:** subagent `front/ovos-playable`  
**Para:** próximo agente (visual QA / áudio Play / input)  
**Branch:** `front/ovos-playable`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\ovos-playable`  
**HEAD base:** `9e21101c99396ed042a0425988b402d09f742db7`

## Objetivo

Diagnosticar estado ATUAL de Um Jogo de Ovos (CLSID `0x010963A5`) @ `9e21101` e recuperar progresso mensurável. Sem fake `HandleEvent(evt=2)`.

## Concluído

- [x] Build CMake Release + smoke ON (`build-ovos`)
- [x] Baseline honesto 180f e 300f SEM input
- [x] Comparação com `ovos-scene` / `de-load` handoffs
- [x] Input mínimo (SEQ A/START) — hid-button chega; PPM muda; cena ainda não reconhecível
- [x] Gate `tools/ovos-gate/run.ps1` com critérios comprovados
- [ ] Diff de core — **não** (sem bug claro com prova antes/depois)

## Arquivos alterados

| Arquivo | Tipo de mudança |
|---|---|
| `tools/ovos-gate/run.ps1` | novo gate |
| `agent-coord/fronts/ovos-playable.md` | frente |
| `agent-coord/fronts/ovos-scene.md` | nota baseline atual |
| `agent-coord/REGISTRY.md` | registra frente |
| `agent-coord/handoffs/ovos-playable-20261107.md` | este handoff |

## Testes

| Comando / ROM | Frames | Resultado | Artefato |
|---|---|---|---|
| smoke SEM input | 180 | exit 0; `video_varied=1` `ever=1`; `audio_nonzero=0`; xor=`00B309A3`; SHA256 `6F3120DB…8413E0` | `artifacts/ovos-baseline/f180.{log,ppm,png}` |
| smoke SEM input | 300 | exit 0; varied=1; audio_nonzero=0; xor=`00FC4706`; SHA256 `A8FFE8E2…7CDC4D` | `artifacts/ovos-baseline/f300.{log,ppm,png}` |
| SEQ `A:90:10,A:150:10,A:210:10,START:240:10` | 300 | exit 0; varied=1; hid-button @150; PPM **diferente** do baseline; audio_nonzero=0 | `artifacts/ovos-baseline/input/seq_a.{log,ppm,png}` |
| `tools/ovos-gate/run.ps1` | 180 | PASS (varied+exit+sem inválidos) | `artifacts/ovos-gate/` |

### ppm_info (sem input)

- f180: white≈0.0004 black≈0.6816 buckets=140 varied=True first=000000
- f300: white≈0.0004 black≈0.6816 buckets=265 varied=True first=000000

### Visual (PNG)

Cena **não sólida** — fundo escuro granulado / nuvens escuras. Não é tela preta pura nem branco FP. Ainda **não** é logo/menu reconhecível de Ovos.

## Comparação handoffs antigos

| Fonte | Claim | @ 9e21101 |
|---|---|---|
| `de-load` / `ovos-scene` | Ovos 180f `video_varied=1` | **Mantido** — sem regressão de varied |
| Relato usuário “cores antigos / regressão” | suspeita tela preta/sólida | **Não reproduzida** neste HEAD: varied=1, buckets>100, PNG granulado |
| Áudio | (não claimado em de-load para Ovos) | `audio_nonzero=0` — SetMediaData×3 (PCM s16) **sem** `IMedia_Play` |

**Conclusão:** não há regressão mensurável de `video_varied` vs parked. Progresso visual “cores antigos” **não** recuperável daqui sem artefato antigo (PPM/PNG) para diff. Baseline atual = verdade.

## Ainda falta

1. Visual QA: cena reconhecível (logo/menu) — atual = grain escuro
2. Por que `IMedia_Play` não dispara após SetMediaData (áudio=0)
3. Input que avance intro → menu com PPM reconhecível
4. Stubs quentes: IQEGL SwapBuffers; HIDD; clsid `0x0106C411` / `0x01041207`

## Problemas / hipóteses abertas

- **H1 áudio:** jogo espera notify/timer antes de Play; SetMediaData ok, Play ausente → `audio_nonzero=0`.
- **H2 visual:** framebuffer GLES apresenta conteúdo parcial (grain) — possível tex/clear errado ou cena real pré-logo; precisa dump temporal + oracle.
- **H3 input:** hid-button chega (log @ frame 150) mas sem mudança de fase óbvia; SEQ mais longa / outros botões.
- Sem prova antes/depois → **sem patch de core nesta frente**.

## Como continuar (passo a passo)

1. `cd C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\ovos-playable`
2. `.\tools\ovos-gate\run.ps1` — deve PASS
3. Dump temporal: `ZEEMU_SMOKE_DUMP_DIR` + frames 0..600; classificar PPM
4. Instrumentar só `IMedia_Play` path / guest call pós-SetMediaData (sem fake evt=2)
5. Sequências input longas; comparar digests PPM

```
cmake -S . -B build-ovos -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-ovos --config Release
.\tools\ovos-gate\run.ps1 -Frames 180
.\tools\ovos-gate\run.ps1 -Frames 300
```

## NÃO fazer

- Fake `HandleEvent(evt=2)` em CLSID `0x010963A5`
- Declarar “jogável” por `video_varied`/boot
- Tocar `fp-gate` / `core-options` worktrees
- Otimizar GLES/IDIB no escuro sem PPM antes/depois

## Dependências

- Bloqueado por: prova visual oracle (PPM antigo “cores bons” se existir)
- Bloqueia: promoção “cena reconhecível”

## Artefatos

- Logs/PPM/PNG: `artifacts/ovos-baseline/` (gitignored)
- Gate: `tools/ovos-gate/run.ps1`
- Docs: este handoff + `agent-coord/fronts/ovos-playable.md`
