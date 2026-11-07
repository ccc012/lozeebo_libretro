# Family Pack — prova final jogável (2026-11-07)

**Branch:** `front/fp-final-playable-proof`  
**Base:** `d0bcde2`

## Resultado

Critério atingido em 900 frames. Sequência curta inicial `A:240:10,A:300:10` seleciona/confirma; movimentos sustentados e confirmação posterior exercitam o minigame:

```text
A:240:10,A:300:10,RIGHT:420:120,LEFT:570:120,A:720:60
```

- exit `0`; 900f em `78.438 s`.
- Callbacks reais: `hid-button` e `hid-position`.
- Vídeo final distinto: 640x480, `2799` buckets, branco `22.56%`, SHA curto `0fca80b15a5d54df`.
- Resumo: `video_varied=1`, `video_ever_varied=1`.
- Áudio real: `audio_nonzero=927840`, `audio_peak=30396`; `IMedia_Play: MP3 stream=0`.
- Assets observados na transição: `game_selected.png`, `logo_cannonball.png`, `logo_zap.png`, `screen_cannonball.png`, `screen_zap.png`.
- Nenhum acesso inválido; nenhuma alteração em `src/`.

## Evidência local

`artifacts/fp-final/` (ignorado pelo Git):

- `menu.ppm` / `menu.png`: captura do menu após seleção.
- `minigame.ppm` / `minigame.png`: tela final distinta, colorida, reconhecível.
- `minigame.log`: callbacks, mídia, timing e `SMOKE-SUMMARY`.

## Reprodução

```powershell
cmake -S . -B build-fp-final -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-fp-final --config Release
powershell -File tools\fp-final-playable-proof\run.ps1
```
