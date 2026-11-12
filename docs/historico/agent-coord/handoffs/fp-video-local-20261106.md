# Handoff — fp-video-local — 2026-11-06

## Escopo

Family Pack, `src/brew/idisplay_real.c`; auditoria de clipping/formato/alpha/pitch/Update.

## Resultado

- Implementado clipping real de pixels/fills conforme `SetClipRect`.
- RGB565/XRGB, pitch e `Update` revisados; sem evidência para outra mudança.
- Nenhum pattern/autopresent fabricado.

## Validação

Build: `build-smoke-fp-video`, Release, sucesso.

| Frames | Exit | PPM | Métrica |
|---:|---:|---|---|
| 600 | 0 | `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-video-local\fp-600-clipped.ppm` | 640x480, pitch 2560, 1 cor, 0 pixels não pretos |
| 1800 | 0 | `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-video-local\fp-1800-clipped.ppm` | 640x480, pitch 2560, 1 cor, 0 pixels não pretos |

Logs no mesmo diretório. Boot chega a `EVT_APP_START`/RODANDO, porém nenhuma chamada DrawArrays/SwapBuffers/IDisplay draw. Portanto menu/minigame não apareceram nesta branch. Bloqueio está fora do ownership vídeo local; não é clipping/formato/alpha/pitch/Update.
