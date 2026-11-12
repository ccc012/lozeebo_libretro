# Handoff — fp-visual-nav-local

**Data:** 2026-11-06  
**Branch:** `front/fp-visual-nav-local`  
**Worktree:** `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\fp-visual-nav-local`  
**Pacote:** `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod` (+ `data.vfs`)

## Objetivo

Capturar sequência temporal de frames, classificar mudanças reais de tela, achar sequência até menu reconhecível e depois minigame. Sem alterar `src/`.

## Concluído

- [x] Harness: `ZEEMU_SMOKE_DUMP_EVERY` + cache de último frame (só `tests/libretro_smoke.c`)
- [x] Scripts: `tools/fp-visual-nav/{run_nav.ps1,classify_ppm.py,ppm_info.py,ppm_to_png.py,export_keys.py}`
- [x] Duas telas distintas reconhecíveis além de branco:
  1. **Boot preto** (frame 0–45)
  2. **Loading JadeStone** (frame ~60–200): hourglass + barra de progresso animada (canto superior esquerdo); buckets 4→80
- [x] Transição loading → branco sólido (~frame 205); menu GLES abre assets/`music.mp3` mas framebuffer apresentado fica branco
- [x] 1800f com `ZEEMU_SMOKE_SEQ` (formato `BOTAO:FRAME:HOLD`): exit 0; `hid-button`/`hid-position` reais; MP3 menu; sem crash
- [ ] Menu visual reconhecível pós-loading (bloqueio render/present, fora desta frente)
- [ ] Entrada visual em minigame

## Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `tests/libretro_smoke.c` | `dump_ppm` helper; cache último FB; `ZEEMU_SMOKE_DUMP_EVERY` |
| `tools/fp-visual-nav/*` | scripts + evidence PPMs/PNGs chave |
| `agent-coord/handoffs/fp-visual-nav-local-20261106.md` | este handoff |

Nenhum `src/` tocado.

## Sequência temporal classificada (dense300, dump every 5)

| Frame | Tela | white | buckets | digest |
|---:|---|---:|---:|---|
| 0 | boot preto | 0.00 | 0 | `b07b3a96…` |
| 60 | loading (hourglass) | 0.99 | 4 | `de7583a3…` |
| 90 | loading (barra ~meio) | 0.99 | 75 | `0e833f37…` |
| 120 | loading (barra cheia) | 0.99 | 79 | `b1c21575…` |
| 205+ | branco sólido | 1.00 | 0 | `e2b22ca7…` |

Critério atingido: ≥2 telas distintas reconhecíveis + sequência com conteúdo ≠ branco.

## SEQ promissora (formato harness local)

```text
ZEEMU_SMOKE_SEQ=DOWN:180:5,A:240:5,A:480:5,RIGHT:720:60,A:840:60,LEFT:1080:60,B:1200:5,A:1320:5,UP:1440:60
ZEEMU_SMOKE_HOLD_FRAMES=0
```

Nota: handoff `fp-gameplay` usava `frame:button:hold` via `ZEEMU_SMOKE_INPUT_SCRIPT` (não presente neste tree). Aqui o harness expõe `ZEEMU_SMOKE_SEQ=BOTAO:FRAME[:HOLD]`.

## Testes

| Run | Frames | Dump | Resultado | Artefato |
|---|---:|---|---|---|
| dense300 | 300 | every 5 | loading animado 60–200; branco 205+; MP3 | `artifacts/.../dense300/` |
| nav1800b | 1800 | every 15 | exit 0; SEQ entregue; hid callbacks; vídeo final branco | `artifacts/.../nav1800b/` |
| earlyA | 450 | every 10 | A durante loading não muda transição | `artifacts/.../earlyA/` |

Evidence commitada: `tools/fp-visual-nav/evidence/frame_{0000,0060,0090,0120,0210}.{ppm,png}` + classify JSON.

## Ainda falta

1. Present/blit do menu GLES pós-loading (framebuffer fica branco enquanto DrawArrays/MP3 seguem) — ownership `fp-video` / GLES
2. Após menu visível: mapear RIGHT/A até minigame e confirmar visualmente

## Como reproduzir

```powershell
cd C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\fp-visual-nav-local
cmake -S . -B build-fp-visual-nav -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-fp-visual-nav --config Release
powershell -File tools\fp-visual-nav\run_nav.ps1 -Frames 300 -DumpEvery 5 -Tag dense300
# com nav:
powershell -File tools\fp-visual-nav\run_nav.ps1 -Frames 1800 -DumpEvery 15 -Tag nav `
  -Seq 'DOWN:180:5,A:240:5,A:480:5,RIGHT:720:60,A:840:60,LEFT:1080:60,B:1200:5,A:1320:5,UP:1440:60'
```

## NÃO fazer

- Fake `HandleEvent(2)` em CLSID `0x010903C6`
- Alterar `src/` nesta frente
- Declarar “jogável” por `video_varied`/áudio/boot

## Dependências

- Bloqueado por: present/render do menu GLES (vídeo)
- Bloqueia: navegação visual honesta até minigame
