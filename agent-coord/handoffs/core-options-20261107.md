# Handoff â€” `core-options`

**Data:** 2026-11-07  
**De:** subagent core-options  
**Para:** integrator / prÃ³ximo agente  
**Branch:** `front/core-options`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\core-options`  
**Base:** `9e21101`  
**HEAD:** `06cfc5a` (feat tree; tip may add docs-only)

## Objetivo

Expor core options RetroArch espelhando env `ZEEMU_*` de diagnÃ³stico. Defaults seguros (off / sem mudar FP). Frameskip real adiado.

## ConcluÃ­do

- [x] Categorias `debug` + `performance`
- [x] Options: `zeebo_profile_core`, `zeebo_profile_pc`, `zeebo_trace_timer`, `zeebo_log_level`, `zeebo_frameskip` (stub)
- [x] `sync_runtime_options`: `GET_VARIABLE` â†’ setters; **env ganha** se setada
- [x] Smoke FP defaults + profile_core via env
- [x] Frameskip: sÃ³ `disabled` (prÃ³ximo passo documentado)

## Options (menu Quick Menu â†’ Options)

| Key | Categoria | Valores | Default | Espelha |
|---|---|---|---|---|
| `zeebo_crop_overscan` | video | disabled\|enabled | disabled | (jÃ¡ existia) |
| `zeebo_username` | system | Player\|Lucas | Player | (jÃ¡ existia) |
| `zeebo_profile_core` | debug | disabled\|enabled | **disabled** | `ZEEMU_PROFILE_CORE` |
| `zeebo_profile_pc` | debug | disabled\|enabled | **disabled** | `ZEEMU_PROFILE_PC` |
| `zeebo_trace_timer` | debug | disabled\|enabled | **disabled** | `ZEEMU_TRACE_TIMER` |
| `zeebo_log_level` | debug | error\|warn\|info\|debug | **info** | `ZEEMU_LOG_LEVEL` (numÃ©rico 0â€“3) |
| `zeebo_frameskip` | performance | disabled | **disabled** | stub â€” sem efeito |

**PrecedÃªncia:** se a env correspondente estiver setada, a option **nÃ£o** sobrescreve.  
**Smoke / sem GET_VARIABLE:** options nÃ£o aplicadas â†’ idÃªntico a `9e21101` (log min = DEBUG se env ausente).

## Arquivos alterados

| Arquivo | MudanÃ§a |
|---|---|
| `src/core/libretro_core.c` | defs/categories/legacy + sync |
| `src/debug/log.h` / `log.c` | `zlog_set_min_level`, `zlog_set_trace_timer` / `zlog_trace_timer` |
| `src/debug/perf.h` / `perf.c` | `zperf_set_option` |
| `src/debug/trace.c` | `ztrace_set_profile_pc` |
| `src/brew/boot.c` | `getenv(ZEEMU_TRACE_TIMER)` â†’ `zlog_trace_timer()` |

## Testes

Build: `cmake -S . -B build-smoke-core-options -DZEEBO_BUILD_SMOKE_TEST=ON` + Release OK.

| Comando / ROM | Frames | Resultado | Artefato |
|---|---|---|---|
| smoke + FP `277229` seq `A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60` (sem ZEEMU profile) | 900 | exit 0; ~21 s; **video_varied=1**; **audio_nonzero=927840** | `C:\Users\Lucas\AppData\Local\Temp\opencode\core-options\fp-default-900.log` |
| smoke + FP + `ZEEMU_PROFILE_CORE=1` seq curta | 180 | exit 0; **ZEEMU PERF** no atexit; sem crash | `...\fp-profile-180.log` |

Nota: `tests\roms\real_family_pack_game.mod` sozinho nÃ£o tem `data.vfs` â†’ video/audio zero. Usar path com assets:

`D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod`

## Como testar no RetroArch

1. Copiar `build-smoke-core-options\Release\lozeebo_libretro.dll` para o core dir do RA.
2. Carregar FP (MOD + pasta com `data.vfs`).
3. Quick Menu â†’ Options â†’ **Debug**:
   - Profile Core = Enabled â†’ ao sair, stderr/`retro_log` com `ZEEMU PERF (QPC)`.
   - Trace Timer = Enabled â†’ logs `TIMERPROBE` / `zboot_timer_set`.
   - Log Level = error â†’ silencia INFO/WARN.
4. Env ainda funciona e **ganha** sobre a option (`ZEEMU_PROFILE_CORE=1` etc.).

## Ainda falta

1. **Frameskip real** (`auto|1|2|3`): pular fatias de `zcpu_run` mantendo present/audio, OU pacing via `GET_FRAME_TIME` / `SET_FRAME_TIME_CALLBACK`. Validar gate FP 900f antes de promover.
2. Harness smoke: `GET_VARIABLE` stub para exercitar options sem RetroArch (opcional).

## NÃƒO fazer

- Fake HandleEvent em FP/Ovos
- Implementar frameskip sem gate FP
- Tocar HLE/ovos/dd especÃ­ficos

## DependÃªncias

- Bloqueado por: â€”
- Bloqueia: â€”
