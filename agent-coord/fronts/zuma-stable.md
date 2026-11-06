# Frente: zuma-stable

**Objetivo:** Zuma 900f com saída limpa + cena estável.  
**Estado:** ready-merge  
**Owner:** liberado (`src/gpu/egl_gl.c`, `src/cpu/cpu.c`)  
**Base:** `31e52a6`; ATC RE4 aplicado por cherry-pick limpo de `62f584f` como `611c3b7`.

## Validação 2026-11-06

- Build exclusivo: `build-smoke-zuma-stable`, Release OK.
- Logs/PPM: `C:\Users\Lucas\AppData\Local\Temp\opencode\zuma-stable\`.
- Zuma 900 frames: exit 0; `TexImage2D OK`; raster `tex2d=1`; PPM 640x480 com 33 cores.
- RE4 300 frames: exit 0; PPM 640x480 com 112 cores; correção ATC preservada.
- ROM externa: `D:\Temp\zeebo_all\...`; nenhuma ROM copiada ou commitada.
- Harness desta base não imprime `SMOKE-SUMMARY`; exit limpo e métricas equivalentes verificadas pelos logs/PPM.

## Limites honestos

- Cena estável no smoke; jogabilidade/manual input não validada.
- Nenhuma alteração adicional em `src/gpu/egl_gl.c` ou `src/cpu/cpu.c` necessária.
