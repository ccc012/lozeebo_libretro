# Frente: de-load

**Objetivo:** Acelerar load romset Heavy Barrel / Caveman (IFile 1-byte / zip / CUNG).  
**Estado:** merged no snapshot WIP `31e52a6`  
**Branch/WT:** `front/de-load` @ `D:\zeebo-worktrees\de-load`  
**Owner arquivos:** livre  
**Deps:** IThread `flag==0` já OK  
**Evidência:** `validation-zip-accel/`

## Feito

- Wake Data East `write8(flag,0)` em `IThread_Start`
- Cache RAM RO até 32 MB em `IFile`; remove `fread(1)`/`ftell` repetidos
- `IFile_Map` + `IFile_SetCacheSize` + dispatch `brew.c/h`
- Slice 50k preservado
- HB, Caveman, BD 900f: EndLoading=sim; DrawArrays=20
- Ovos e Family Pack 180f: `video_varied=1`

## Falta

1. Remover dump BITPACK barulhento se ainda ligado
2. Incluir HB/Caveman no próximo baseline 69

## Integração

Código já estava no commit de preservação `31e52a6`; não cherry-pick duplicado.

## Teste

```
...\libretro_smoke.exe ... tests\roms\heavy_barrel.mod ... 900
...\libretro_smoke.exe ... tests\roms\caveman_ninja.mod ... 900
...\libretro_smoke.exe ... tests\roms\bad_dudes_vs._dragonninja.mod ... 900
```

Preserve: ovos, FP, BD.
