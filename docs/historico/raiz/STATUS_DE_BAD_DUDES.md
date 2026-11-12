# Status — Data East / Bad Dudes (honest smoke)

Data: 2026-11-06. Sem `ZEEMU_STUB_THREAD`. Core: `lozeebo_v600_snap.dll` (= `build-smoke\Release\lozeebo_libretro.dll`).

## Resultado Bad Dudes 900f

| Marco | Status |
|---|---|
| `EndLoading romset baddudes` / `RomLoaded` | OK |
| `Initializing machine` | OK |
| Bitpack init `0x186F80` / `0x187194` | OK (4 calls, width sane) |
| `Initializing machine video/sound system` | OK |
| `Starting emulation` | OK |
| `DrawArrays` real | OK (20+ logged; tex binds 1024×512 / 256×256) |
| PPM | **581 cores únicos** (não preto sólido; top: preto, cinza `SSS`, off-white) |
| `[SMOKE-SUMMARY]` | **AUSENTE** — processo saiu `ExitCode=-1` (~7 min) antes do `fprintf` do harness |
| `[VIDEO]` frames | 11 amostras (a cada 60) → ~600+ frames rodados |

Log: `validation-honest-de/bad900.log` (~2 MB). PPM: `validation-honest-de/bad900.ppm`.

## O que NÃO era o hang

Hist `0x0187000*` após `Initializing machine` **não** é loop infinito nem UXTB quebrado.

- Offline disasm: `0x186F80` / `0x187194` = bit-pack de tiles/collision.
- `uxtbne` `0x16EF2073` casa máscara SXTAB (`0x0FA003F0 == 0x06A00070`) — decode OK.
- Width em entry: `0x800` / `0x400` / `0x1000` (sano).
- Caller `0x1875E0` faz **4** `bl` sequenciais (não loop externo infinito).
- ~25M+ instr/call no interpretador → wall-clock alto; com timeout 600s passa.

## Fixes já no DLL sob teste

- SXTB/H + SXTAB/H/UXTAB (`src/cpu/execute_arm.c`)
- `IFile_Read` quiet (zip scan)
- Remoção watches `tabptr`/`errfield`
- Dump temporário `BITPACK #N` em `src/cpu/cpu.c` (PC `0x186F80`/`0x187194`)

## Bloqueio atual

1. **Crash host antes do SUMMARY** (`Exit=-1`, stdout vazio). Sem SEH/LOGE no log. Últimos PCs: memcpy-like em `0x1A7C0` (`subs`/`ldm`/`stm`/`bne` — finito, 0xF0×0x10). Provável AV em outro caminho ou no `retro_unload`/`deinit`.
2. **Heavy Barrel / Caveman 900f** — não re-rodados com este DLL.
3. **FP/ovos** — não re-verificados após edits CPU/ifile/memory.

## Próximo (mínimo)

1. Rodar 900f com debugger / `WER` / wrap `__try` no harness pra pegar AV addr.
2. Ou: 300f smoke (já passa `Starting emulation`+DrawArrays) + forçar SUMMARY mid-run.
3. Mesmo DLL: HB + Caveman 900f (timeout ≥600s).
4. Spot-check FP.
5. Remover dump `BITPACK` de `cpu.c` quando diagnóstico fechar.

## Comando

```powershell
Remove-Item Env:ZEEMU_STUB_THREAD -ErrorAction SilentlyContinue
$p = Start-Process .\libretro_smoke_snap.exe -ArgumentList `
  '.\lozeebo_v600_snap.dll','tests\roms\bad_dudes_vs._dragonninja.mod',`
  'validation-honest-de\bad900.ppm','900' `
  -RedirectStandardOutput validation-honest-de\bad900.out `
  -RedirectStandardError validation-honest-de\bad900.log -PassThru -NoNewWindow
$p | Wait-Process -Timeout 600
```

Não usar `2>` no PowerShell (marca `$LASTEXITCODE=-1` em qualquer stderr).
