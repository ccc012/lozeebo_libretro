# Missão exclusiva — Family Pack jogável

**Prioridade:** única. Outros jogos pausados, salvo smoke mínimo anti-regressão.

## Definição de pronto

1. Boot/menu visível real
2. Navegação por controle: direções + confirmar + voltar
3. Entrar em pelo menos um minigame
4. Gameplay com input sustentado
5. Áudio audível correto no menu e minigame
6. Vídeo estável, sem patterns fabricados/cores sólidas diagnósticas
7. 1800f sem crash/hang; save/exit se o jogo usar
8. RetroArch manual registrado (somente após integração)

## Frentes isoladas

| Frente | Worktree local | Ownership |
|---|---|---|
| `fp-input-local` | `C:\Users\Lucas\AppData\Local\Temp\opencode\zeebo-worktrees\fp-input-local` | `src/input/*`, HID slots em `boot.c` somente seção HID |
| `fp-audio-local` | `...\fp-audio-local` | `src/audio/*`, `src/brew/imedia.c` |
| `fp-video-local` | `...\fp-video-local` | `src/brew/idisplay_real.c`, framebuffer/bitmap; não `egl_gl.c` sem claim |
| `fp-flow-local` | `...\fp-flow-local` | scheduler/callbacks em `boot.c`; não seção HID |

## Integração

Cada agente: build próprio, logs próprios, commit/handoff. Mestre integra serialmente e roda FP 600/1800f. Deploy RetroArch só mestre.

## Baseline

- Vídeo real `video_varied=1`
- MP3 ~489842 em 600f
- CreateDevice=1 / Register path
- CLSID `0x010903C6`: nunca fake `HandleEvent(evt=2)`
