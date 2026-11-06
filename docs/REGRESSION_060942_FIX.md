# Fix regressões 20261106-060942

## Causa

`src/brew/idisplay_real.c` tratava `nl=-1` (`0xFFFFFFFF` / AECHAR illimité) como endereço de trap:

```c
int thunk = (r2 >= ZTRAP_BASE) || (r3 >= ZTRAP_BASE);  // BUG
```

`r3=0xFFFFFFFF` ⇒ thunk falso ⇒ `psz=R5` (objeto display) e `y=R6` (flags) ⇒
`DrawText '????????' em (8194,-8030)` em vez de `CARREGANDO (10,460)`.

Sem clear via `DrawRect(NULL, NONE, NONE)` (fallback background ausente no real).

CRASH (−1) sob parallel=4: flaky (mesmos ROMs passam solo) — OOM/resource race, não lógica guest.

## Correção transversal

Arquivo: `src/brew/idisplay_real.c`

1. Thunk só se `R2` ∈ `[ZTRAP_BASE, ZTRAP_BASE+0x100000)` — `nl=-1` NÃO é thunk.
2. `DrawRect`: se `pRect==0 && frame==NONE && fill==NONE` → fill=background, flags=FILL (paridade `boot.c`).
3. Log `IDisplay_DrawRect` (até 16).

Não toca Family Pack / Pac-Mania / Action Hero / RE4 / Zuma paths além do IDisplay real compartilhado.

## Antes → Depois (smoke 60f, ZEEMU_STUB_THREAD off)

| ROM | Baseline 172840 | Regressão 060942 | Após fix |
|---|---|---|---|
| ddragonz | VARIED | SOLID_FAIL | **VARIED** (`CARREGANDO` 10,460) |
| double_dragon | VARIED | SOLID_FAIL | **VARIED** |
| real_ddragon_game | VARIED | SOLID_FAIL | **VARIED** |
| real_family_pack_game | VARIED | CRASH | **VARIED** |
| test_draw | VARIED | CRASH | **VARIED** |
| um_jogo_de_ovos | VARIED | SOLID_FAIL | SOLID_FAIL (IDisplay real descarrilha PC=0x04000000; baseline usava stub) |
| raging_thunder_2 | SOLID | CRASH | SOLID (sem crash) |
| real_zeeboids_game | SOLID | CRASH | SOLID (sem crash) |
| reckless_racing | SOLID | CRASH | **VARIED** |
| turma_da_monica… | SOLID | CRASH | SOLID (sem crash) |
| ultimate_chess_3d | SOLID | CRASH | SOLID (sem crash) |

### Controles (não desfazer)

| ROM | Após fix |
|---|---|
| zeebo_family_pack | VARIED |
| pac-mania / real_pacmania_game | VARIED |
| action_hero_3d… | VARIED (era TIMEOUT) |
| re4 | VARIED (era SOLID) |
| zuma's_revenge | SOLID (inalterado) |

## Loop vermelho → verde

```
tests\libretro_smoke-honest.exe dist\lozeebo_libretro.dll tests\roms\ddragonz.mod dd.ppm 60
```

Antes: `DrawText '????????' (8194,-8030) thunk=1` + `video_varied=0`  
Depois: `DrawText 'CARREGANDO' (10,460) thunk=0` + `video_varied=1`

## Pendente

- `um_jogo_de_ovos`: IDisplay real vs stub (baseline stub). Separado do thunk.
- Re-rodar suite 69 completa (`tools\honest_regression.ps1 -TimeoutSeconds 90`).
- CRASH flaky sob parallel: investigar se ainda ocorre na suite completa.
