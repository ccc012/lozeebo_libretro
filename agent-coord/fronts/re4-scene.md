# Frente: re4-scene

**Objetivo:** Cena/UI RE4 reconhecível pós-pixels ATC.  
**Estado:** parked  
**Owner:** `egl_gl.c` / `idisplay_real.c` (claim)  
**Deps:** softGen OK; ATC/ETC decode; GetError out-ptr; M1–M5 Display

## Feito

- IDisplay 48 + SetFont
- GetError escreve 0 no out guest
- ATC RGB/RGBA + ETC1; 512² `has_pix=1`
- 900f ppm nonzero / varied

## Falta

1. QA visual uploads / UI
2. Áudio RE4
3. Não regredir Zuma soft-rgb565
