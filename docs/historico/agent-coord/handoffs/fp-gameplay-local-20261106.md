# Handoff: fp-gameplay-local — 2026-11-06

## Resultado

- Harness aceita sequência genérica `ZEEMU_SMOKE_INPUT_SCRIPT=frame:button:hold,...`.
- Botões: `A`, `B`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT`; até 64 eventos, inclusive sobrepostos.
- Compatibilidade preservada com `ZEEMU_SMOKE_BUTTON/PRESS_FRAME/HOLD_FRAMES`.
- Validação negativa: script `bad` retorna 2 e registra evento inválido.
- Nenhum arquivo `src/` alterado.

## Family Pack completo

Pacote externo: `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod` + `data.vfs` adjacente.

Sequência mais promissora executada:

```text
180:DOWN:5,240:A:5,480:A:5,720:RIGHT:60,840:A:60,1080:LEFT:60,1200:B:5,1320:A:5,1440:UP:60
```

Execução 1800f: exit 0. Inputs geraram callbacks reais `hid-position`/`hid-button`; MP3 real iniciou (`44100Hz`, mono, `984396` bytes); vídeo 640x480 continuou atualizado. Sem fake guest/patterns.

## Evidências locais

- `fp-gameplay-1800.log`, `fp-gameplay-1800.ppm`: execução completa 1800f.
- `fp-down-a.log/.ppm`, `fp-right-a.log/.ppm`, `fp-a-repeat.log/.ppm`: exploração de sequências.
- `fp-script-invalid.log`: validação negativa.

Arquivos ficam no worktree, ignorados pelo Git. PPM finais mostram framebuffer predominantemente branco; logs mostram draw contínuo e áudio, porém evidência visual não permite afirmar minigame reconhecível. Sequência/timing entrega navegação e confirmações ao guest, mas entrada visual em minigame permanece não comprovada.

## Build/teste

```powershell
cmake -S . -B build-fp-gameplay -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-fp-gameplay --config Release
$env:ZEEMU_SMOKE_INPUT_SCRIPT='180:DOWN:5,240:A:5,480:A:5,720:RIGHT:60,840:A:60,1080:LEFT:60,1200:B:5,1320:A:5,1440:UP:60'
build-fp-gameplay\Release\libretro_smoke.exe build-fp-gameplay\Release\lozeebo_libretro.dll 'D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod' fp-gameplay-1800.ppm 1800 2> fp-gameplay-1800.log
```
