# Infuse Reverse Engineering — Relatório Técnico

> Objetivo: mapear as funcionalidades do Infuse (emulador fechado de referência do
> Zeebo por Tuxality) e implementá-las no LOZeebo.

Fontes primárias:
- <https://tuxality.net/projects/infuse_zeebo_emulator> (site oficial Infuse)
- <https://github.com/Tuxality/Infuse> (repo vazia — código fechado)
- <https://github.com/mrpostiga/zeemu> (referência aberta com arquitetura similar)
- `reference/openzeebo-master` (docs de hardware MSM7201A)
- `ZeeboDeveloperGuide0.97.pdf` (specs oficiais Qualcomm)

## 1. Arquitetura Geral do Infuse

| Camada | Infuse | LOZeebo (atual) |
|---|---|---|
| CPU core | dynarmic ARM JIT | interpreter ARM custom |
| Graphics | OpenGL ES 1.x via OpenGL host | stub EGL/GLES mínimo |
| Audio | MIDI+PCM+ADPCM+MP3, mixing, resampling | PCM voices básico |
| Input | HID até 2 gamepads, analog+digital, hotplug | digital básico |
| Display | 640x480 VGA, RGB565, aspect 4:3 | 640x480, XRGB8888 |
| Timing | 20-35 FPS lock, vsync | 60Hz libretro default |

## 2. Graphics — Stack OpenGL ES 1.x

### 2.1 Especificação do hardware (Adreno 130 no MSM7201A)
- **Resolução**: 640×480 VGA, aspecto 4:3
- **Cor**: RGB565 (16bpp) nativo — Zeebo devguide §3.2
- **OpenGL ES**: 1.0+ Common Profile (fixed-point only)
- **Fixed-point**: GLfixed s15.16 (32-bit signed)
- **Performance**: 1.6M tri/s, 63M pixels texturizados (2 texturas)
- **Formatos de textura**: RGB565, RGBA5551, RGBA4444, luminance, alpha, paletizados

### 2.2 Padrão Infuse (do site/blog)
- Renderiza via OpenGL host (não Vulkan — "glitches exist because OpenGL
  stack implementation is not finished")
- Backend SDL-frame presentation
- Suporte a superfícies EGL (window, pbuffer, pixmap)
- Escala de superfície com letterbox

### 2.3 Padrão zeemu (referência aberta implementada)
- Framebuffer RGB565 nativo, pitch DWORD-aligned: `((w*bpp)+31)/32*4`
- Presenter zero-copy: ponteiro guest → host sem cópia
- IDisplay vtable 48 slots: DrawText, BitBlt, Update, SetDestination, etc.
- EGL: três camadas (IEGL surface/config, IQEGL window, QXGL dispatch)
- GLES dispatch ~78 pontos de entrada
- Estado: QxGpuState com matrizes, lights, materials, viewport 640x480
- DrawText: rasterizador vetorial 5x7 builtin (sem TTF na camada BREW)
- Device info reportado: RAM=64MB, colorDepth=24, lang="ptbr"

### 2.4 Constantes gráficas críticas (do devguide)
```
AEE_ENC_S_JIS     = 0x00FD
dwPlatformID      = 0x01000000
wMaxPath           = 260
cxScreen/cyScreen  = 640/480
clip rect default  = 640x480
viewport GLES      = {0,0,640,480}
```

## 3. Audio — Mixer Multi-Stream

### 3.1 Especificação hardware (Zeebo devguide §2.1)
- MIDI: 72-voice polyphony, wavetable 512kB, 44.1kHz sampling
- Formatos: MP3, PCM, ADPCM, CMX, QCELP
- DSP QDSP-5 dedicado @ 528MHz

### 3.2 Padrão Infuse (do site A1 release)
- Backend MIDI + PCM/ADPCM + MP3
- **Mixing multi-stream com resampling independente da sample rate do host**
- Backends: waveOut (Win), Core Audio (macOS), Media Kit (Haiku),
  PulseAudio (Linux)
- Suporte a soundfonts .sf2 externos
- Quality improvements: resamplers com estado separado, init rápida,
  sem crunch/distortion/clipping

### 3.3 Padrão zeemu (implementação de referência)
- TinySoundFont + TinyMidiLoader para MIDI
- minimp3 para MP3
- FFmpeg opcional (libavformat/codec/util) para decode extra
- Voice mixing: step_fp = (rate << 32) / host_rate (resampling em fixed-point 32.32)

### 3.4 O que falta no LOZeebo atual
- [ ] MIDI/soundfont (72-voice wavetable)
- [ ] ADPCM decode
- [ ] MP3 decode
- [ ] Mixing multi-stream adequado
- [ ] Resampling independente de host rate

## 4. Input — Interface HID

### 4.1 Padrão Infuse
- Até **2 gamepais** simultâneos
- **Digital + analógico** processado
- **Sem input lag observado**
- Suporte a modo cooperativo (Double Dragon) e competitivo (Family Pack)
- **Hotplug**: detecta conexão/desconexão em runtime
- **Fallback de teclado** (para jogar sem gamepad)

### 4.2 Especificação hardware (openzeebo keypad.c)
- Matrix GPIO 2×7 = 14 teclas
- GPIO rows: 30, 78; cols: 42,41,40,39 (main) + 38,37,36 (side)
- Settle time: 5000µs, polling decimado 1-in-11
- PMIC para power button (reg 0x41, bit 3)

### 4.3 O que falta no LOZeebo atual
- [ ] 2 gamepads simultâneos
- [ ] Analógicos (lx, ly, rx, ry) com range real
- [ ] Hotplug detection
- [ ] Mapeamento configurável
- [ ] Modo keyboard fallback

## 5. Timing & Performance

### 5.1 Padrão Infuse
- Jogos Zeebo travados em **20-35 FPS** (não 60!)
- Full-speed em M1 MacBook Pro (debug build)
- Playable em Intel Haswell e Pentium N4200
- Sem frame-pacing issues reportados

### 5.2 Implicação para LOZeebo
- Libretro roda a 60Hz fixo; jogos Zeebo esperam ~30 FPS
- Precisa frame-skip ou timing adaptativo
- Sync audio com vídeo essencial

## 6. Implementação Priorizada

### Fase 1 — Gráficos (maior impacto visual)
1. Converter framebuffer interno para RGB565 (compatível com saída nativa)
2. Implementar EGL surface/config/context real
3. Dispatch GLES 1.x completo (~78 funções)
4. Estado QxGpuState: matrizes modelview/projection/texture
5. Lighting + materials (fixed-point s15.16)
6. Texturização + mipmap
7. glDrawArrays/glDrawElements com T&L
8. DrawText com rasterizador 5x7

### Fase 2 — Áudio
1. Integrar TinySoundFont (MIDI 72-voice)
2. ADPCM decoder
3. MP3 via minimp3
4. Mixer multi-stream com resampling 32.32 fixo
5. Suporte a soundfont .sf2 externo

### Faze 3 — Input
1. Suporte a 2 gamepads
2. Analógicos com deadzone + curva
3. Hotplug via libretro controller info
4. Mapeamento configurável (core options)
5. Keyboard fallback

### Fase 4 — Timing
1. Frame-pacing para jogos 20-35 FPS
2. Audio-video sync adaptativo
3. Oção de frame-skip
