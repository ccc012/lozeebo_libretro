# Handoff: fp-signal-local — 2026-11-06

## Resultado

- `ISignal`/`ISignalCtl`: refcount real compartilhado; `CreateSignal` entrega duas referências.
- Registros HID retêm referência própria; Release das interfaces HID libera registros.
- Fila de callback retém o signal; `Detach` após `Set` cancela callback já enfileirado.
- `Release` do guest não invalida signal ainda registrado.
- Callback valida objeto, estado detached, função e user antes da chamada guest.
- Nenhum `HandleEvent(2)`; nenhum asset commitado.

## Teste runnable de lifetime

```powershell
$env:ZEEMU_SIGNAL_SELFTEST='1'
build-smoke-fp-signal\Release\libretro_smoke.exe build-smoke-fp-signal\Release\lozeebo_libretro.dll D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod C:\Users\Lucas\AppData\Local\Temp\opencode\fp-signal-local\selftest.ppm 1
```

Evidência: `signal lifetime selftest: attach/detach/fire/release OK` em `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-signal-local\selftest.log`.

## Build / pacote completo

```powershell
cmake -S . -B build-smoke-fp-signal -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-smoke-fp-signal --config Release
```

Pacote usado externamente: `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\` (`game.mod`, `data.vfs`).

## Smoke honesto

600 frames, pacote completo, exit 0:

- A: callback `hid-button`, UID `0x0106C40A` down/up.
- B: callback `hid-button`, UID `0x0106C40B` down/up.
- DOWN: callback `hid-position`.
- Connect callback real; DrawArrays/UI ativos.
- Nenhum `EA000017`/`EA000013` nos logs.

Logs/PPM: `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-signal-local\{a,b,down,selftest}.{log,ppm}`.

Não comprovado: reconhecimento visual manual do menu, entrada em minigame, gameplay sustentado. Smoke scripted comprova draw e entrega de input, não jogabilidade manual.
