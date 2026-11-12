# Coordenação de agentes — zeebo_libretro

Agente mestre decide integração. Demais agentes trabalham em worktree/branch isolados.

## Estrutura

| Arquivo | Uso |
|---|---|
| `REGISTRY.md` | Estado vivo de todas as frentes |
| `RULES.md` | Regras obrigatórias (shared files, build, deploy) |
| `INTEGRATION.md` | Fluxo merge → smoke → key10 → master |
| `HANDOFF_TEMPLATE.md` | Modelo de passagem entre agentes |
| `AGENT_PROMPT.md` | Prompt pronto para colar em agente novo |
| `fronts/*.md` | Uma ficha por frente ativa |
| `handoffs/` | Passagens preenchidas (histórico) |

## Espelhos

- Repo: `C:\Users\Lucas\source\repos\zeebo_libretro\agent-coord\`
- Obsidian: `G:\Meu Drive\Documentos obi\Projeto\Emulador\14_Coordenacao_Agentes\`
- Backup verificado: `D:\backups\zeebo_libretro_20261106\`
- Worktrees novos: `D:\zeebo-worktrees\`
- Worktrees antigos `rodada*`: **não apagar** até revisão

## Branches de preservação (2026-11-06)

| Branch | Papel |
|---|---|
| `master` | Baseline remoto; não tocar direto |
| `backup/pre-coord-20261106` | Snapshot HEAD pré-coordenação (`a3edac0`) |
| `wip/integration-20261106` | Integração controlada pelo mestre (árvore dirty preservada) |

## Comando canônico

```
cmake --build build-smoke --config Release --target zeebo_libretro
build-smoke\Release\libretro_smoke.exe build-smoke\Release\lozeebo_libretro.dll tests\roms\<game>.mod <dump.ppm> <frames>
```

Deploy RetroArch: só o **agente mestre** (ou dono explícito em `REGISTRY.md`).
DLL: `D:\SteamLibrary\steamapps\common\RetroArch\cores\lozeebo_libretro.dll`
