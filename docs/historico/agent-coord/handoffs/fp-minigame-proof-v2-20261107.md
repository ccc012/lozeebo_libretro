# Handoff — Family Pack minigame proof v2

**Branch:** `front/fp-minigame-proof-v2`  
**Base:** `17a8768`  
**Pacote:** `D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod`

## Resultado honesto

Critério **não atingido**. Menu e minigame não ficaram reconhecíveis no framebuffer. Nenhuma sequência longa foi executada, conforme exigência de só usar 900–1800 frames após encontrar sequência comprovada.

## Evidência

- Baseline 360 frames: exit 0, 69.468 s, `video_ever_varied=1`, `audio_nonzero=137088`, `audio_peak=30396`.
- Frame final 360: branco uniforme (`video_varied=0`), SHA-256 `0E2476EE4A789FB3AFDAF323CA140CA4EAA1728316CBAF8620A6253188565F0C`.
- Matriz isolada em frame 240, hold 5: `UP`, `DOWN`, `LEFT`, `RIGHT`, `A`.
- Todos os cinco frames finais têm o mesmo SHA-256 do baseline. Logo, imagem não permite inferir seleção.
- Callbacks chegaram: logs `hid-button` para `A`; `hid-position` para D-pad; `SMOKE-SEQ` em todos os pulsos.
- Assets de menu carregaram (`menu\gfx\game_selected.png`) e MP3 tocou, mas apresentação permaneceu branca.
- Sustentado `RIGHT:240:60,A:310:20`, 360 frames: exit 0; framebuffer final ainda idêntico.

Artefatos locais ignorados por Git: `artifacts/fp-minigame-proof-v2/` (PPMs e logs completos).

## Reprodução

```powershell
cmake -S . -B build-fp-proof -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-fp-proof --config Release
powershell -File tools\fp-minigame-proof-v2\probe.ps1 -Tag baseline
powershell -File tools\fp-minigame-proof-v2\probe.ps1 -Tag right -Seq 'RIGHT:240:5'
powershell -File tools\fp-minigame-proof-v2\probe.ps1 -Tag sustained -Seq 'RIGHT:240:60,A:310:20'
```

`ZEEMU_SMOKE_MAX_SECONDS` está aplicado pelo script. Nenhum arquivo em `src/` alterado.
