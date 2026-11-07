# Frente: ovos-scene

**Objetivo:** Cena reconhecível Um Jogo de Ovos.  
**Estado:** parked  
**Deps:** IDIB/VRAM unificado; varied=1; sem fake evt=2 CLSID `0x010963A5`

## Feito

- Bitmap unificado; video_varied=1

## Baseline @ 9e21101 (2026-11-07, frente `ovos-playable`)

- 180/300f SEM input: exit 0, `video_varied=1`, `audio_nonzero=0` (SetMediaData sem Play)
- PPM: grain escuro (não sólido); gate `tools/ovos-gate/run.ps1`
- Sem regressão de varied vs claim parked; cena reconhecível ainda falta

## Falta

1. Visual QA cena
2. Input
3. Preservar exclusão HandleEvent(2)
4. IMedia_Play / áudio nonzero
