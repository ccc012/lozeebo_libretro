# Frente: de-load

**Objetivo:** Acelerar load romset Heavy Barrel / Caveman (IFile 1-byte / zip / CUNG).  
**Estado:** parked  
**Branch/WT:** `front/de-load` @ `D:\zeebo-worktrees\de-load`  
**Owner arquivos:** preferir `mod_loader.c` / IFile paths; `boot.c` só se IThread — claim no REGISTRY  
**Deps:** IThread `flag==0` já OK; BD DrawArrays OK  
**Baseline:** BD DrawArrays=20; HB/CN `Loading romset` dentro budget

## Feito

- Wake Data East `write8(flag,0)` em `IThread_Start`
- BD: Starting emulation + DrawArrays; PPM cores reais em runs longos
- SendEvent ASYNC não usado por DE

## Falta

1. I/O HLE mais largo (não 1-byte) no path zip/CUNG
2. Smoke HB+Caveman 900f com DrawArrays / varied
3. Remover dump BITPACK barulhento se ainda ligado

## Teste

```
...\libretro_smoke.exe ... tests\roms\heavy_barrel.mod ... 900
...\libretro_smoke.exe ... tests\roms\caveman_ninja.mod ... 900
...\libretro_smoke.exe ... tests\roms\bad_dudes_vs._dragonninja.mod ... 900
```

Preserve: ovos, FP, BD.
