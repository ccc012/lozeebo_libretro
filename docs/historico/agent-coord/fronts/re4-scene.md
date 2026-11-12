# Frente: re4-scene

**Objetivo:** Cena/UI RE4 reconhecível pós-pixels ATC.  
**Estado:** ready-merge  
**Owner:** liberado  
**Deps:** softGen OK; ATC/ETC decode; GetError out-ptr; M1–M5 Display

## Feito

- IDisplay 48 + SetFont
- GetError escreve 0 no out guest
- ATC RGB/RGBA + ETC1; 512² `has_pix=1`
- 900f ppm nonzero / varied

- Corrigido ATC: endpoint 0 RGB555+mode e interpolação conforme ATITC; removida heurística DXT1 por ordenação.
- RE4 900f pós-fix: PPM 640x480, 4729 cores, 108094 pixels pretos; SHA256 `5D38C841D8B454B990EA9C7C439362710F25D86BC68CD4A844B317094086D2E0`.
- Artefatos: `C:\Users\Lucas\AppData\Local\Temp\opencode\re4-scene\fixed-900.{log,ppm,bmp}`.
- Zuma path RGB565 intacto: `convert_teximage(type=0x8363)` não alterado. ROM Zuma indisponível para smoke focal.

## Falta

1. QA visual manual do PPM/BMP no integrador
2. Áudio RE4
3. Smoke Zuma quando ROM disponível
