# Integração controlada

Só o **agente mestre** integra em `wip/integration-20261106`. `master` só após gate completo.

## Gate por PR/frente

1. **Build** no worktree da frente: `cmake --build build-smoke-<frente> --config Release --target zeebo_libretro`
2. **Smoke do componente** (≥300f, prefer 600–900f) — log + PPM salvos
3. **Regressão mínima** (não quebrar): ovos, FP, Pac-Mania, DD — 180–300f
4. **Comparar** com baseline key10 `20261106-100312` / DLL `638D14…`
5. **Conflito**: se toca arquivo quente, mestre rebase/merge manual; agente não force-merge
6. **Registro**: atualizar `REGISTRY.md` + `fronts/<id>.md` com PASS/FAIL
7. **Merge** → `wip/integration-20261106` (ff ou merge commit curto)
8. **Deploy RetroArch** (opcional, só mestre): `.bak` + SHA256 + espelho `dist\`

## Baseline 69

- Script: `tools/honest_regression.ps1`
- Key10 amostra: ovos, FP, pac, dd, re4, zuma, baddudes, action_hero, ridge, alice
- Full 69: serial ≥600f para áudio; paralelo só com build dirs isolados
- Nunca promover GREEN/YELLOW a jogável sem menu+input+gameplay

## Conflito em arquivo quente

```
# agente NÃO edita; mestre:
cd C:\Users\Lucas\source\repos\zeebo_libretro
git checkout wip/integration-20261106
git merge --no-ff front/<nome>
# resolve só o arquivo quente; re-smoke key subset
```

## Rollback seguro

1. DLL: restaurar `lozeebo_libretro.dll.bak`
2. Código: `git checkout backup/pre-coord-20261106 -- <paths>` (seletivo)
3. Full tree: `D:\backups\zeebo_libretro_20261106\` (robocopy src/docs + patch)

**Nunca** `reset --hard` na árvore dirty principal sem confirmação explícita do usuário.
