# Frente: ovos-playable

**Objetivo:** Diagnosticar Ovos @ `9e21101` e recuperar progresso mensurável (gate + baseline).  
**Estado:** handoff  
**Branch/WT:** `front/ovos-playable` @ Temp opencode  
**Deps:** sem fake evt=2 CLSID `0x010963A5`  
**Evidência:** `artifacts/ovos-baseline/`; `tools/ovos-gate/run.ps1`; handoff `ovos-playable-20261107.md`

## Feito

- Build Release+smoke; baseline 180/300f SEM input: exit 0, `video_varied=1`, audio_nonzero=0
- Sem regressão vs `de-load`/`ovos-scene` claim de varied=1
- Input SEQ muda PPM; hid-button ok; cena ainda grain escuro
- Gate PASS em varied+exit+sem inválidos (áudio **não** gated)

## Falta

1. Visual reconhecível (logo/menu)
2. `IMedia_Play` após SetMediaData
3. SEQ input → fase seguinte com PPM prova

## Teste

```
.\tools\ovos-gate\run.ps1 -Frames 180
.\tools\ovos-gate\run.ps1 -Frames 300
```
