# Handoff — `ovos-playable` (pós-ovo: rachadura → starfield)

**Data:** 2026-11-07  
**De:** subagent `front/ovos-playable`  
**Para:** master / próximo (menu/fase além do starfield)  
**Branch:** `front/ovos-playable`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\ovos-playable`  
**HEAD base:** `4da83a4` → commit deste handoff

## Objetivo

Um Jogo de Ovos JOGÁVEL pós-ovo: fases distintas, controle sustentado, áudio contínuo. Sem fake `HandleEvent(evt=2)`.

## Concluído

- [x] Alinhado `front/ovos-playable` → `origin/wip/integration-20261106` (`4da83a4`)
- [x] `zbrew_media_tick`: START PCM adiado + DONE quando voz acaba → guest re-Play / MP3 pós-quebra
- [x] Fila HID button-events (16 slots, como zeemu) — slot único perdia press/release
- [x] SEQ smoke expandida (128 itens / buf 2048)
- [x] Prova temporal: black → grain → fade → **ovo** → **rachadura** → **starfield** + MP3
- [x] `ovos-gate -Playable` PASS (`egg=1 crack=1 star=1`, `audio_nonzero=311276`, MP3 Play)
- [x] `ovos-gate` baseline 180f PASS; FP-gate PASS (audio≈931855; PPM SHA atualizado)
- [x] Sem fake evt=2 CLSID `0x010963A5`

## Evidência (fases)

| Fase | Frames (aprox) | PPM | Áudio |
|---|---|---|---|
| Intro grain/fade | 60–900 | grain → fade | 3× PCM boot |
| Title egg amarelo | 940–1220 | `ffcc33`, black≈0 | — |
| Rachadura (input) | 1240–1320 | crack lines, yellow↑ | — |
| Starfield pós-quebra | 1340+ | black≈0.996, white≈650 | MP3 `0x01005502` ~1.1MB |

SEQ que quebra ovo: D-pad shake + B @ f1000–1320 (stride 8).  
`audio_nonzero`: boot-only ≈3902 → pós-crack ≈0.3M–3.2M (MP3).

## Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `src/brew/imedia.c/h` | `pending_start_notify` / `awaiting_done` + `zbrew_media_tick` |
| `src/brew/brew.h` | declara `zbrew_media_tick` |
| `src/core/libretro_core.c` | chama tick após `zaudio_render` |
| `src/brew/boot.c` | fila HID `g_hid_evt_q[16]`; multi-button push/pop |
| `tests/libretro_smoke.c` | SEQ 128 itens / buf 2048 |
| `tools/ovos-gate/run.ps1` | `-Playable` 1600f shake + dump egg∧star + MP3 + audio≥100k |
| `tools/fp-gate/baseline.json` | PPM SHA (menu FP com fila HID) |

## Não completo (próximo)

- Starfield estável sob input; **sem** prova de menu/seleção/fase além disso.
- Suspeita: gameplay real usa tilt contínuo / eixos Z/Rx/Ry (hoje zerados) ou timing longo no starfield.
- `IShell_GetPosition` stub retorna zeros (GPS-style; provavelmente irrelevante).
- Só 1 MP3 Play pós-quebra; SFX PCM adicionais no starfield não observados.

## Gates

```
ovos-gate -Playable → PASS egg=1 crack=1 star=1 audio_nonzero=311276
ovos-gate 180f      → PASS audio_nonzero=3902
fp-gate (build-ovos)→ PASS audio_nonzero=931855 ppm=DC43FEA1…
```

## Próximo passo

1. Instrumentar polls HID/GetPositionState no starfield; mapear Z/Rx/Ry se guest ler.
2. SEQ longa (3k–6k) pós-starfield com shake contínuo + A/START.
3. Só então declarar menu/fase jogável; não regredir critérios egg∧star do gate.
