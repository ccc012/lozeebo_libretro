# Family Pack — prova de minigame (2026-11-07)

## Resultado

**Critério não atingido.** Não há prova honesta de menu + minigame nesta frente.

- Branch/HEAD inicial: `front/fp-minigame-proof` / `25e7a6d1678b1fa13e9b2e1a58567143c0e5c02c`.
- MOD exclusivo: `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod`.
- Build limpo: `build-fp-visual-nav`, Release, core + smoke OK.
- Execução `600f`, sem input: excedeu 120 s; encerrada externamente.
- Execução `360f`, `ZEEMU_SMOKE_SEQ=A:180:10`: excedeu 300 s; encerrada externamente.
- Nenhuma execução chegou a `SMOKE-SUMMARY`; portanto `exit 0`, áudio, `video_varied=1` e minigame não podem ser alegados.

## Evidência visual disponível

`tools/fp-visual-nav/evidence/` contém evidência anterior reconhecível somente do splash JadeStone:

- `frame_0060.ppm`: início do splash.
- `frame_0090.ppm` / `frame_0120.ppm`: logo JadeStone + ampulheta.
- `frame_0210.ppm`: branco sólido.
- `classify_dense300.json`: conteúdo não sólido entre 60–200f; branco sólido a partir de 205f.
- `classify_nav1800.json`: branco sólido a partir de 210f.
- `summaries.txt`: áudio não zero, mas `video_varied=0` em 300f e 1800f.

Esses PPMs **não** provam menu nem minigame.

## Bloqueio reproduzido

O smoke atual fica lento demais antes de concluir uma janela curta. Como stderr é redirecionado e permanece sem conteúdo útil durante a execução, não foi possível localizar por log o frame exato nem testar uma matriz de navegação. O ensaio com `A:180:10` não concluiu 360f em cinco minutos.

## Próximo passo proposto — sem editar `src/`

1. Medir custo/frame no soft GL após o primeiro `eglSwapBuffers`; capturar stacks durante o ensaio lento.
2. Corrigir a falha de desempenho na frente proprietária de `src/gpu/egl_gl.c`, caso confirmada.
3. Repetir varredura temporal em processo concluinte: baseline até menu; depois pulsos isolados `UP/DOWN/LEFT/RIGHT/A`, 5–10f, espaçados 30f.
4. Só promover sequência quando houver dois PPMs reconhecíveis (menu e minigame), `video_varied=1`, `audio_frames>0`, `exit 0` até 1800f.

Nenhum arquivo de `src/` alterado. Nenhuma sequência inventada.
