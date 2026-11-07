# Missão — Double Dragon playable gate

**Prioridade:** consolidar progresso real; não inventar gameplay.

## Definição de pronto (honesta)

1. Boot → `EVT_APP_START` → timers reais do jogo (`SetTimer` 33 ms)
2. Sem fake `HandleEvent(evt=2)` no CLSID `0x0102F789`
3. Load: `sound.ggz` + `data.ggz`; `CompressedTex` / `glDrawArrays`
4. `video_varied=1`, `audio_nonzero>0`, exit 0 em 180/300/600f
5. Gate: `tools/dd-gate/run.ps1`
6. **Não** declarar jogável sem menu+input+movimento comprovados

## Bloqueio atual

Após load (textures ATC + ADPCM/MIDI), guest permanece em `DrawText 'CARREGANDO…'`. Input HID chega; ainda sem prova de menu/movimento/soco.

## Ownership

- `src/brew/boot.c` — exclusão CLSID DD do fake game-loop (somente)
- Não tocar worktrees fp-gate / core-options / ovos
