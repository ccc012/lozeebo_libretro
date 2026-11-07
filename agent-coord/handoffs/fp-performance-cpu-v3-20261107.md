# Family Pack — CPU hot-path v3 (2026-11-07)

**Branch:** `front/fp-performance-cpu-v3`  
**Base:** `5614173`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\fp-performance-cpu-v3`

## Objetivo

≥30 FPS sem mudar guest timing/comportamento. Meta intermediária: ≥20% vs baseline integrado 61.3s / 900f (~14.7 FPS) → <49s.

## Concluído

- [x] Fast-path `zmem_read32`/`write32`/`read16` para ROM (0..RAM_SIZE) e heap (0x10000000..0x11FFFFFF)
- [x] Watches de `zmem_write32` só em Debug + `ZEEMU_MEM_WATCH`
- [x] `ztrace_pc` no-op em Release salvo `ZEEMU_TRACE_PC` / `ZEEMU_PROFILE_PC`
- [x] PC watches de `zcpu_step` em Release só sob `ZEEMU_CPU_WATCH` (HLE funcional preservado)
- [x] `zcpu_cond_pass` via lookup NZCV; skip AL em `zarm_execute`
- [x] Smoke 900f seq comprovada: exit 0, PPM varied, audio_nonzero, zero inválidos

## Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `src/memory/memory.c` | fast-path LE load/store; watches NDEBUG+env |
| `src/debug/trace.c` | ring buffer opt-in em Release |
| `src/cpu/cpu.c` | gate watches Release; labels HLE |
| `src/cpu/flags.c` | tabela de condições |
| `src/cpu/execute_arm.c` | skip cond AL |

## Testes

| Comando / ROM | Frames | Resultado | Artefato |
|---|---|---|---|
| `libretro_smoke` + FP `277229` seq `A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60` | 900 | exit 0; **19266 ms** (~46.7 FPS); video_varied=1; audio_nonzero=927840; 0 inválidos/descarril/STACK | `fp-cpu-v3-900.log`, `fp-cpu-v3-900.ppm` |

Baseline: 61.3s → **19.3s** (−68.6%, ~3.2×). Meta <49s e ≥30 FPS atingidas.

## Env opt-in (Release)

- `ZEEMU_MEM_WATCH` — watches de write32
- `ZEEMU_CPU_WATCH` — PC watches em `zcpu_step`
- `ZEEMU_TRACE_PC` / `ZEEMU_PROFILE_PC` — ring/histograma

## Ainda falta

1. Empilhar mais hot-path (fetch direto, I-cache) se quiser ≥60 FPS
2. Promoção para main após revisão do integrator

## NÃO fazer

- Não reativar watches no hot path sem env
- Não alterar guest timing / HLE de uncompress/Zuma RGB

## Build

```
cmake -S . -B build-cpu-v3 -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-cpu-v3 --config Release
```
