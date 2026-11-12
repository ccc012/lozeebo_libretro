# Regras de coordenação

## Proibido

- `git reset --hard`, `git clean -fd`, force-push, sobrescrita sem backup
- Apagar / reverter alteração sem identificar origem
- Inventar resultado RetroArch ou marcar jogo “jogável” por boot/áudio/`video_varied=1`
- Copiar código Infuse (só contratos/comportamento; preferir Zeemu GPLv3)
- Build/teste/deploy simultâneo no mesmo `build-smoke` ou na mesma DLL RetroArch
- Alterar features do emulador só para reorganizar agentes
- Fake `HandleEvent(evt=2)` em CLSID `0x010903C6` (Family Pack) ou `0x010963A5` (Ovos) — isso é `EVT_APP_SUSPEND`
- Padrões EGL fabricados / autopresent paint sólido

## Arquivos quentes — um escritor por vez

| Arquivo | Dono padrão |
|---|---|
| `src/brew/boot.c` | scheduler / HID / IThread / Data East |
| `src/gpu/egl_gl.c` | GLES / ATC / ETC / QEGL |
| `src/brew/imedia.c` | IMedia / Play / MP3 |
| `src/brew/idisplay_real.c` | IDisplay 48 slots |
| `src/cpu/cpu.c` | empty-count / word-fill HLE |
| `src/loader/mod_loader.c` | loader MOD/ZIP |

Antes de editar arquivo quente: marcar `owner` em `REGISTRY.md`. Se já há dono → handoff ou esperar.

## Isolamento

1. Cada frente = 1 branch `front/<nome>` + 1 worktree sob `D:\zeebo-worktrees\<nome>`
2. Build local: `build-smoke-<frente>/` (nunca `build-smoke` compartilhado se outro agente testa)
3. Logs/PPM: `C:\Users\Lucas\AppData\Local\Temp\opencode\<frente>\`
4. Integração só via mestre em `wip/integration-20261106`

## Critérios honestos

| Sinal | Significa | NÃO significa |
|---|---|---|
| boot / CreateInstance | applet sobe | jogável |
| `audio_nonzero>0` | áudio real | cena/input OK |
| `video_varied=1` | pixels mudam | cena correta |
| DrawArrays / tex2d | path GL ativo | UI reconhecível |

Promoção a “jogável”: menu + input + gameplay manuais registrados.

## Continuação sem chat

Contexto vive em `REGISTRY.md` + `fronts/<id>.md` + último `handoffs/<id>-YYYYMMDD.md`.
Agente novo **não** depende do histórico da conversa anterior.
