# Handoff — zuma-stable — 2026-11-06

## Resultado

Pronto para merge. ATC RE4 cherry-picked sem conflito. Zuma completa 900 frames com exit 0, texturas 2D ativas e frame variado/estável. RE4 completa 300 frames com exit 0 após a correção ATC.

## Commits

- `611c3b7` — cherry-pick exato de `62f584f` (`gpu: corrige decodificacao de blocos ATC`).
- Commit seguinte — ficha/handoff da validação.

## Evidências

- `C:\Users\Lucas\AppData\Local\Temp\opencode\zuma-stable\zuma-900.log`
- `C:\Users\Lucas\AppData\Local\Temp\opencode\zuma-stable\zuma-900.ppm` — 640x480, 33 cores.
- `C:\Users\Lucas\AppData\Local\Temp\opencode\zuma-stable\re4-300.log`
- `C:\Users\Lucas\AppData\Local\Temp\opencode\zuma-stable\re4-300.ppm` — 640x480, 112 cores.
- Zuma: log contém `TexImage2D OK` e raster com `tex2d=1`.
- Ambos: processo `EXIT=0`.

## Observação

`tests/libretro_smoke.c` desta base não implementa `SMOKE-SUMMARY`. Não foi alterado por estar fora do ownership. Validação equivalente: 900/300 frames concluídos, exit limpo, logs de render e variedade dos PPMs.

ROMs usadas apenas fora do worktree em `D:\Temp\zeebo_all`; não copiadas/commitadas. `boot.c`, `imedia.c`, DLL RetroArch e árvore principal intocados.
