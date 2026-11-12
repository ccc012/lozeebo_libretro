# Diagnóstico — coordenação 2026-11-06

## Estado do repositório

| Item | Valor |
|---|---|
| Path | `C:\Users\Lucas\source\repos\zeebo_libretro` |
| HEAD | `a3edac002e82e3854609df7580a617298f9d2e4e` |
| Branch integração | `wip/integration-20261106` |
| Branch backup | `backup/pre-coord-20261106` |
| Dirty | ~2833 modified + ~1010 untracked |
| Emulador docs | Google Drive — **não** é git |
| DLL RetroArch | `lozeebo_libretro.dll` SHA256 `638D14…` |

## Riscos

1. Vários agentes no mesmo tree → conflitos em `boot.c` / `egl_gl.c` / `imedia.c`
2. Dirty não atribuível a um agente → perda se `clean`/`reset`
3. Worktrees `rodada*` stale vs WIP atual
4. Build/deploy concorrente na mesma DLL
5. Untracked inclui ROMs/PPM/logs — não commitar

## Preservado (verificado)

| Artefato | Path |
|---|---|
| Cópia src+docs | `D:\backups\zeebo_libretro_20261106\` |
| Patch tracked src | `...\dirty-tracked-src.patch` (~965 KB) |
| Status porcelain | `...\git-status-porcelain.txt` (3839 linhas) |
| DLL hash | `...\retroarch-dll.sha256` |
| Branches | `backup/pre-coord-20261106`, `wip/integration-20261106` |

## Estrutura adotada

- Mestre: integração + deploy + REGISTRY
- 1 frente = 1 branch `front/<id>` + worktree `D:\zeebo-worktrees\<id>`
- Locks em arquivos quentes
- Handoff obrigatório ao trocar agente
- Critérios honestos (varied ≠ jogável)

## Não executado (risco)

- `reset --hard` / `clean -fd`
- Remoção worktrees `rodada*`
- Commit em massa de ROMs/PPM/reference
- Migração forçada de agentes ativos mid-edit
