# Handoff — `dd-playable`

**Data:** 2026-11-07  
**De:** subagent dd-playable  
**Para:** mestre (integração só se FP gate verde)  
**Branch:** `front/dd-playable`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\dd-playable`  
**HEAD:** `2d02ff9`  
**Base:** `9e21101`

## Objetivo

Consolidar Double Dragon: smoke estável, gate anti-regressão, sem fake `HandleEvent(evt=2)`.

## Concluído

- [x] CMake Release `build-dd` + smoke
- [x] Baseline 180/300/600f (pré-fix): exit 0, varied=1, áudio OK, preso em CARREGANDO + retry GRAPHICS
- [x] Causa: fake game-loop registra `HandleEvent` como timer e dispara **evt=2** → CancelTimer + HID Release + loop `CreateInstance(GRAPHICS)`
- [x] Fix mínimo: excluir CLSID `0x0102F789` das duas exclusões do fake loop em `boot.c` (mesma família FP/Zuma/Ovos/DE)
- [x] Pós-fix: `data.ggz` abre, CompressedTex×10, glClear/glDrawArrays, SetTimer real 33ms, HID intacto, áudio ADPCM+MIDI path
- [x] `tools/dd-gate/run.ps1` — falha alto: exit≠0, varied≠1, audio_nz=0, GRAPHICS>2, HID Release, sem data.ggz, hard faults
- [x] Gate 180f + 600f PASS

## Arquivos alterados

| Arquivo | Tipo de mudança |
|---|---|
| `src/brew/boot.c` | Excluir DD `0x0102F789` do fake HandleEvent(evt=2) loop + re-registro |
| `tools/dd-gate/run.ps1` | Gate smoke DD |
| `agent-coord/fronts/dd-playable.md` | Frente |
| `agent-coord/handoffs/dd-playable-20261107.md` | Este handoff |

## Testes

| Comando / ROM | Frames | Resultado | Artefato |
|---|---|---|---|
| baseline (pré-fix) `ddragonz.mod` | 180/300/600 | exit 0; varied=1; xor=4A; PPM hash idêntico; GRAPHICS infinito; HID Release | `artifacts/dd/baseline-*.log` |
| pós-fix gate | 180 | exit 0; varied=1; xor=FF8B39DE; audio_nz=264600; data.ggz=10; GRAPHICS=1 | `artifacts/dd-gate/dd-180.*` SHA `A6FE9989…` |
| pós-fix gate | 600 | exit 0; varied=1; xor=FFA40BD5; audio_nz=881994 peak=27339; data.ggz=10 | `artifacts/dd-gate/dd-600.*` SHA `2149E446…` |
| input SEQ 900f | 900 | exit 0; HID button/position; ainda só `CARREGANDO…` | `artifacts/dd/fix-input-900.log` |

MOD: `D:\Temp\zeebo_all\Double_Dragon__Brazil___Es_Pt_\mod\274754\ddragonz.mod`  
(= `tests/roms/real_ddragon_game.mod` SHA256 `5485A189…`)

## Ainda falta

1. Sair de `CARREGANDO…` → menu/title reconhecível (PPM distinto estável pós-load)
2. Sequência input com prova de movimento/soco (ainda sem evidência)
3. Stub `0x01002001` método 33 (WARN único no boot)
4. `IMedia_SetMediaData dwSize=0` WARN (não bloqueia áudio principal)

## Problemas / hipóteses abertas

- Fake evt=2 em DD **comprovado** por log (CancelTimer+HID Release+GRAPHICS retry). Exclusão CLSID é o fix mínimo; não inventar evt=2 novo.
- Pós-load guest ainda só anima dots de CARREGANDO + 1 glDrawArrays; não alegar jogável.
- PPM 180 ≠ 600 (load incompleto vs pós-tex) — gate **não** fixa hash único; registra SHA.

## Como continuar

1. `cd C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\dd-playable`
2. `powershell -File tools\dd-gate\run.ps1 -Frames 600`
3. Investigar por que load para em CARREGANDO após CompressedTex (perfil opt-in / stub 0x01002001 m33)
4. Não integrar em `wip/integration` daqui — mestre só se FP gate verde

## NÃO fazer

- Fake `HandleEvent(evt=2)` em FP/Ovos/DD
- Declarar jogável por varied/áudio/boot
- Tocar worktrees fp-gate / core-options / ovos
- Inventar sequência de soco sem PPM distintos

## Dependências

- Bloqueado por: progresso visual além de loading; possível stub GRAPHICS_REAL m33
- Bloqueia: nada no FP gate

## Artefatos

- Logs: `artifacts/dd-gate/dd-180.log`, `dd-600.log`; baseline/fix em `artifacts/dd/`
- PPM: `artifacts/dd-gate/dd-180.ppm`, `dd-600.ppm`
- Docs: `agent-coord/fronts/dd-playable.md`
