# Handoff — `ovos-playable` (jogável proof)

**Data:** 2026-11-07  
**De:** subagent `front/ovos-playable`  
**Para:** master / próximo (gameplay pós-ovo)  
**Branch:** `front/ovos-playable`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\ovos-playable`  
**HEAD base:** `76aad43` → commit deste handoff

## Objetivo

Um Jogo de Ovos JOGÁVEL: menu/cena reconhecível, controles, áudio nonzero, vídeo varied, zero fake `HandleEvent(evt=2)`.

## Concluído

- [x] Dump temporal PPM 0..900 (`ZEEMU_SMOKE_DUMP_DIR`, stride 15)
- [x] Root-cause áudio: `mixer.c` sem `pos_fp += step_fp` + auto-Play heap PCM
- [x] SEQ longa → PPM ovo amarelo (`ffcc33`, black=0.343)
- [x] Gate reforçado (`audio_nonzero>0`, `-Playable` yellow+hid)
- [x] FP-gate PASS (sem regressão)
- [x] Sem fake evt=2 CLSID `0x010963A5`

## Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `src/audio/mixer.c` | `voice->pos_fp += voice->step_fp` no loop de render |
| `src/brew/imedia.c` | RegisterNotify auto-Play para qualquer READY+bytes (incl. heap); START notify PCM adiado |
| `tests/libretro_smoke.c` | DUMP_DIR grava frames com stride (`ZEEMU_SMOKE_DUMP_STRIDE`) |
| `tools/ovos-gate/run.ps1` | exige audio>0; `-Playable` 900f+ovo |
| `agent-coord/fronts/ovos-*.md` | estado proof |
| este handoff | evidência |

## Testes

| Comando | Frames | Resultado | Artefato |
|---|---|---|---|
| smoke SEM input | 180 | exit 0; varied=1; **audio_nonzero=3902** | `artifacts/ovos-gate/f180.*` |
| smoke SEQ longa | 900 | exit 0; audio=3902; PPM ovo amarelo unique=378 black=0.343 yellow=154540 | `artifacts/ovos-gate/playable.*` / `artifacts/ovos-baseline/input/seq_long.png` |
| dump temporal | 900 | hashes mudam; f810+ deixa grain; f840+ cena clara | `artifacts/ovos-dump/` |
| `ovos-gate -Frames 180` | 180 | **PASS** | |
| `ovos-gate -Playable` | 900 | **PASS** | |
| `fp-gate -BuildDir build-ovos` | 900 | **GATE PASS** audio=931855 | `artifacts/fp-gate/` |

### SEQ usada (playable)

```
A:60:15,A:120:15,START:180:15,A:240:15,START:300:15,A:360:20,UP:420:10,DOWN:450:10,A:480:20,START:540:20,A:600:20,RIGHT:660:15,LEFT:700:15,A:750:20,START:810:20
```

## Hipótese correta

1. **Mixer:** `pos_fp` nunca avançava → decode sempre sample[0]=0 nos WAVs Ovos (silêncio inicial longo) → `audio_nonzero=0` mesmo com Play.
2. **IMedia:** RegisterNotify só auto-Playava ROM/MP3; Ovos PCM está no heap → Play nunca vinha do guest.
3. Visual grain @180f é intro; SEQ+tempo → ovo amarelo reconhecível (~f810+ / @900 com input).

## Ainda falta

1. Progressão pós-ovo (menu/jogo completo)
2. DONE notify diferido quando voz PCM acaba
3. Confirmar se há MP3/música de menu além dos 3 SFX

## NÃO fazer

- Fake `HandleEvent(evt=2)` em `0x010963A5`
- Tocar worktree `dd-playable`
- Declarar “completo” só por grain/varied

## Como validar

```
cd C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\ovos-playable
.\tools\ovos-gate\run.ps1 -Frames 180
.\tools\ovos-gate\run.ps1 -Playable
.\tools\fp-gate\run.ps1 -SkipBuild -BuildDir build-ovos
```
