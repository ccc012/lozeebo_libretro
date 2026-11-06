# REGISTRY — frentes ativas

Atualizado: 2026-11-06  
Mestre: sessão OpenCode atual  
Integração: branch `wip/integration-20261106` @ worktree principal  
HEAD base preservação: `a3edac002e82e3854609df7580a617298f9d2e4e`  
DLL deploy: SHA256 `638D14381D37C9C12C2C7D21D3813D9D3E5F13CF454F36B1A2F19F1F8D933A40`

## Legenda estado

`idle` | `claimed` | `active` | `blocked` | `handoff` | `ready-merge` | `merged` | `parked`

## Shared-file lock

| Arquivo | Owner agora | Frente |
|---|---|---|
| `src/brew/boot.c` | — livre; signal fix integrado | FP exclusivo |
| `src/gpu/egl_gl.c` | — livre (não editar nesta rodada) | — |
| `src/brew/imedia.c` | — livre | — |
| `src/brew/idisplay_real.c` | — livre | — |
| `src/cpu/cpu.c` | — livre (não editar nesta rodada) | — |
| `src/loader/mod_loader.c` | — livre | — |

## Frentes

| ID | Objetivo | Branch / WT | Owner | Estado | Deps | Próxima ação |
|---|---|---|---|---|---|---|
| `de-load` | HB/Caveman romset load speed (IFile/zip/CUNG) | `front/de-load` @ `D:\zeebo-worktrees\de-load` | — | `merged` | código em `31e52a6` | Próximo baseline 69; limpar BITPACK |
| `alice-play` | Alice IMedia_Play pós-CreateDevice | `front/alice-play` @ `D:\zeebo-worktrees\alice-play` | — | `blocked` | HID OK; FP/AH preservados | Perfilar loop guest pós-assets; não forçar Play |
| `re4-scene` | RE4 cena/UI pós-pixels ATC | `front/re4-scene` @ `D:\zeebo-worktrees\re4-scene` | — | `merged` | ATC `62f584f` | Próximo key10; preservar 4729 cores |
| `zuma-stable` | Zuma pós-tex2d estabilidade 900f SUMMARY | `front/zuma-stable` @ `D:\zeebo-worktrees\zuma-stable` | — | `merged` | 900f exit 0; RE4 300f exit 0 | Próximo key10; harness sem SUMMARY |
| `fp-input` | Family Pack input pós-menu | `front/fp-input` @ `D:\zeebo-worktrees\fp-input` | — | `parked` | áudio ~489k OK | HID/Register path |
| `ovos-scene` | Ovos cena reconhecível | `front/ovos-scene` @ `D:\zeebo-worktrees\ovos-scene` | — | `parked` | bitmap unificado | Visual + input |
| `rea-runtime` | REA runtime Infuse (oracle) | `front/rea-runtime` @ `D:\zeebo-worktrees\rea-runtime` | — | `merged` | contrato `c4fa455` | Configurar GHIDRA_INSTALL_DIR antes de nova análise |
| `repo-hygiene` | Organizar repo/docs/GitHub | `front/repo-hygiene` @ `D:\zeebo-worktrees\repo-hygiene` | — | `merged-partial` | audit/index/templates integrados | README/.gitignore ficam branch por conflito WIP |
| `fp-input-local` | Controle/menu/minigame FP | `front/fp-input-local` @ Temp opencode | — | `merged-partial` | B/voltar corrigido | Localizar pacote 277229/data.vfs; validar navegação |
| `fp-audio-local` | Áudio completo FP | `front/fp-audio-local` @ Temp opencode | — | `merged` | 600/1800f exit 0 | Validar áudio no minigame após input/flow |
| `fp-video-local` | UI/vídeo completo FP | `front/fp-video-local` @ Temp opencode | — | `blocked` | fixture sem draw pós-start | Clip candidate `d0d5f34` preservado, não integrado |
| `fp-flow-local` | Scheduler/callbacks/navegação FP | `front/fp-flow-local` @ Temp opencode | — | `blocked` | Detach invalida signal | Transferido para `fp-signal-local` |
| `fp-signal-local` | Lifetime HID signal/callback FP | `front/fp-signal-local` @ Temp opencode | — | `merged` | self-test + 600f completo | Validar menu/minigame manual/script avançado |
| `fp-visual-nav-local` | Sequência temporal frames → menu/minigame | `front/fp-visual-nav-local` @ Temp opencode | — | `done-partial` | boot+loading JadeStone reconhecíveis; 1800f SEQ | Menu GLES pós-f205 branco; present/render |
| `honest69` | Regressão 69 baseline reproduzível | mestre only | mestre | `paused` | foco FP | Retomar após FP jogável |
| `coord` | Sistema de coordenação | `wip/integration-20261106` | mestre | `active` | — | Integrar somente FP |

## Worktrees legados (não apagar)

| Path | Branch | Nota |
|---|---|---|
| `...\zeebo_libretro-double-dragon` | `rodada3/double-dragon` | stale vs WIP |
| `...\zeebo_libretro-family-pack` | `rodada3/family-pack` | stale |
| `...\zeebo_libretro-pacmania` | `rodada3/pacmania` | stale |
| `...\zeebo_libretro-gles-fp` | `rodada5/gles-fp` | stale |
| `...\zeebo_libretro-qegl-surface2` | (rodada) | stale |
| `..\zeebo_libretro-clsid-lote1` | `rodada7/clsid-lote1` | stale |
| Temp claude wt-head | detached | **prunable** |

## Dirty tree (preservar)

- ~2833 modified + ~1010 untracked no principal
- Patch tracked src: `D:\backups\zeebo_libretro_20261106\dirty-tracked-src.patch`
- Cópia src/docs: `D:\backups\zeebo_libretro_20261106\`
- Untracked src relevante: `imedia.c`, `mp3.c/h`, `midi.*`, `adpcm.*`, `iimage.*`, `zip_extract.c`, …
- **Não** atribuir tudo a um agente; tratar como WIP coletivo até triagem

## Paralelo seguro (quando retomar)

Pode paralelo se owners distintos e sem overlap de arquivo quente:

1. `rea-runtime` (docs/oracle only) ∥ `honest69` (mestre, read-only DLL)
2. `de-load` (loader/IFile — claim `mod_loader.c`) ∥ `alice-play` (imedia — claim `imedia.c`) — **não** ambos em `boot.c`
3. `re4-scene` e `zuma-stable` **serial** se ambos precisam `egl_gl.c`

## Últimos ganhos (contexto)

- DE: `flag==0` wake; BD DrawArrays
- RE4: pixels + ATC/ETC decode
- Zuma: soft tex2d + empty-count HLE
- FP: re-Play + mono upmix ~489k
- Alice: CreateDevice; Play ainda 0
- Display M1–M5 em `idisplay_real.c`
