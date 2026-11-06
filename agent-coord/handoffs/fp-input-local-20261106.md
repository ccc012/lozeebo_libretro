# Handoff: fp-input-local — 2026-11-06

## Resultado

- Corrigido mapeamento RetroPad B/voltar: botão HID 1, UID `0x0106C40B`.
- Confirmar permanece RetroPad A: botão HID 0, UID `0x0106C40A`.
- Direções permanecem no `IHIDDevice` position; `GetAxesInfo`: X/Y `0x0106C4D0/0x0106C4D1`.
- Nenhum `HandleEvent(2)` adicionado.

## Evidência

Build exclusivo:

```powershell
cmake -S . -B build-smoke-fp-input -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-smoke-fp-input --config Release
```

Scripted smoke executado:

```powershell
$env:ZEEMU_TRACE_HID='1'
$env:ZEEMU_SMOKE_BUTTON='DOWN'
$env:ZEEMU_SMOKE_PRESS_FRAME='180'
$env:ZEEMU_SMOKE_HOLD_FRAMES='10'
build-smoke-fp-input\Release\libretro_smoke.exe build-smoke-fp-input\Release\lozeebo_libretro.dll tests\roms\real_family_pack_game.mod C:\Users\Lucas\AppData\Local\Temp\opencode\fp-input-local\down.ppm 360
```

Log: `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-input-local\down.log`.

Observado: registro real dos signals button/position; `EVT_APP_START` retorna; input DOWN frames 180–189. ROM fixture não inclui `data.vfs`; jogo destrói HID antes do primeiro frame (`Release: button/position signal`), portanto navegação/menu/minigame não pode ser honestamente comprovada com esse fixture. Necessário pacote completo 277229 para validação manual.
