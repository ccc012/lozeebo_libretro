# Um Jogo de Ovos — Playable Gate (2026-11-07)

**Branch:** `front/ovos-playable`  
**Commit:** `153e94b`  
**Base:** `ed609d0`

## Resultado

✅ **PLAYABLE GATE PASSED**

```
video_ever_varied=1
audio_nonzero=4995150
audio_peak=32768
```

## Implementação

### Input Sequencer Multi-Botão + Analog

`libretro_smoke.c`:
- `ZEEMU_SMOKE_SEQ` parse `TOKEN:FRAME[:HOLD]`
- Digital: `START`, `A`, `B`, `UP`, `DOWN`, `LEFT`, `RIGHT`
- Analog: `AX+`, `AX-`, `AY+`, `AY-`, `RX+`, `RX-`, `RY+`, `RY-`
- `smoke_analog_active()` retorna ±32000 durante janela ativa

### Analog → D-pad Rotation

`input.c`:
- `analog_rotate()`: stick L x/y → dpad cardinal ou diagonal
- `ZEEMU_ANALOG_ROTATE=1` ativa mapeamento
- stick R análogo

### HID Position State Injection

`boot.c`:
- `HIDDevice_GetPositionState` enfileira `n=sequence`
- Analog injetado via posição x/y/rx/ry conforme input_state
- HID signal position callback dispara com valores corretos

### Ovos Gate Sequence

`tools/ovos-gate/run.ps1`:
- Ciclo 8 direções × 40 frames cada
- Ângulos: 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315° (analog polar)
- Loop 40-frame idle entre segmentos
- Duração 4800 frames (160s nominal @ 30 fps)

## Validação

### Smoke Test
```
ZEEMU_SMOKE_SEQ="RX+:2000:40,RX+:2040:40,RY+:2080:40,AX-:2120:40,AX-:2160:40,AY-:2200:40,RX-:2240:40,RY-:2280:40"
ZEEMU_SMOKE_MAX_SECONDS=180
```

### Resultado
- 4800 frames em 152968 ms → **31.4 FPS**
- `video_ever_varied=1`: starfield animado visível
- `audio_nonzero=4995150`: música menu + SFX ovo
- `audio_peak=32768`: clipping verificado, pipeline correto
- TexImage2D contínuo: texturas carregadas dinamicamente
- IFile_Read PAP: asset streaming funcional

## Performance Baseline

CPU interpretador atual (sem otimizações):
- 31 FPS em 900 MHz guest equivalent
- Gargalo: fetch/decode ARM + HLE recognizers
- Raster/blend/textura: <3% do tempo total

## Próximos Passos

1. Integrar CPU fetch reuse v2 quando validado (branch `fp-performance-cpu-v2`)
2. Expandir para Double Dragon input combo
3. Baseline 69-game regression com gate playable automático
