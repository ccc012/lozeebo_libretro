# Frente: ovos-playable

**Objetivo:** Um Jogo de Ovos JOGÁVEL (menu reconhecível + áudio + input).  
**Estado:** proof  
**Branch/WT:** `front/ovos-playable` @ Temp opencode  
**Deps:** sem fake evt=2 CLSID `0x010963A5`  
**Evidência:** `artifacts/ovos-gate/`; handoff `ovos-playable-20261107b.md`

## Feito

- Fix `mixer.c`: `pos_fp += step_fp` (voz PCM travava no sample 0 → silêncio)
- Fix `imedia.c`: auto-Play heap PCM no RegisterNotify (Ovos nunca chamava Play)
- Dump temporal PPM + SEQ longa → ovo amarelo (`ffcc33`) reconhecível
- Gate: exige `audio_nonzero>0`; `-Playable` exige ovo amarelo + hid-button
- FP-gate PASS (audio_nonzero=931855) — sem regressão

## Falta

1. Gameplay além do ovo/intro (fases seguintes)
2. DONE notify diferido pós-PCM (hoje START sync adiado)
3. Áudio só SFX boot (~3902) — música de menu se existir

## Teste

```
.\tools\ovos-gate\run.ps1 -Frames 180
.\tools\ovos-gate\run.ps1 -Playable
.\tools\fp-gate\run.ps1 -SkipBuild -BuildDir build-ovos
```
