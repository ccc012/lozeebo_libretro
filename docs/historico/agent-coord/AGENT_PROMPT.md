# Prompt pronto — agente de frente zeebo_libretro

Cole isto no início de cada sessão de agente (substitua `FRONT_ID`):

---

Você trabalha no projeto **zeebo_libretro** sob coordenação do agente mestre.

## Ambiente

- Worktree: `D:\zeebo-worktrees\FRONT_ID`
- Branch: `front/FRONT_ID`
- Repo principal (não editar a menos que seja mestre): `C:\Users\Lucas\source\repos\zeebo_libretro`
- Coordenação: ler `agent-coord/REGISTRY.md`, `agent-coord/RULES.md`, `agent-coord/fronts/FRONT_ID.md`
- Status técnico: `G:\Meu Drive\Documentos obi\Projeto\Emulador\12_Estado_Atual\09_RESUMO_ATUAL_2026-11-06.md`

## Regras

1. Não `reset --hard` / `clean -fd` / force-push.
2. Não inventar RetroArch; não chamar jogo “jogável” por `video_varied`/áudio/boot.
3. Não copiar Infuse; contratos só; preferir Zeemu GPLv3.
4. Arquivos quentes (`boot.c`, `egl_gl.c`, `imedia.c`, `idisplay_real.c`, `cpu.c`, `mod_loader.c`): só se você for o `owner` no REGISTRY.
5. Build em `build-smoke-FRONT_ID/` — não no `build-smoke` compartilhado se outro testa.
6. Sem fake `HandleEvent(2)` em FP `0x010903C6` / Ovos `0x010963A5`.
7. Sem padrões EGL fabricados.
8. Sem perguntas ao usuário — decidir e registrar no front/handoff.
9. Ao atingir limite ou pausar: preencher `agent-coord/handoffs/FRONT_ID-YYYYMMDD.md` com o template.

## Build / smoke

```
cmake -S . -B build-smoke-FRONT_ID -DCMAKE_BUILD_TYPE=Release
cmake --build build-smoke-FRONT_ID --config Release --target zeebo_libretro
build-smoke-FRONT_ID\Release\libretro_smoke.exe build-smoke-FRONT_ID\Release\lozeebo_libretro.dll tests\roms\<rom> C:\Users\Lucas\AppData\Local\Temp\opencode\FRONT_ID\f.ppm <frames>
```

## Entrega

- Diff mínimo
- Smoke com métricas honestas no log
- Atualizar `fronts/FRONT_ID.md`
- Se limite → handoff completo

## Objetivo desta frente

(ver `fronts/FRONT_ID.md`)

---
