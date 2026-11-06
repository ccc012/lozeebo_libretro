# Regressão de áudio — 69 jogos — 2026-11-06

## Build testado

- Core: `x64/Release/zeebo_libretro.dll`
- Build: `build_safe.ps1`, sucesso, 397312 bytes, 2026-11-06 06:08:56
- Runner: `tools/honest_regression.ps1`
- Varredura: 600 frames, 60 s/caso, paralelo 8
- Resultado bruto: `tests/regression-audio/20261106-060904/results.csv`
- Repetição focal: 600 frames, 180 s/caso, serial
- Resultado focal: `tests/regression-audio-focused/20261106-061651/results.csv`

## `audio_nonzero` confirmado

| Jogo | Execução | `audio_nonzero` | Pico/caminho |
|---|---:|---:|---|
| `ddragonz.mod` | varredura | 882000 | PCM S16 mono, 22050 Hz, `IMedia` -> `zaudio_play_pcm_ch` |
| `double_dragon.mod` | varredura | 882000 | PCM S16 mono, 22050 Hz |
| `real_ddragon_game.mod` | focal | 882000 | PCM S16 mono, 22050 Hz |
| `action_hero_3d_-_wild_dog_&_imicro3d.mod` | focal | 881358 | MP3, `IMedia` -> `zmp3_play` -> `zmp3_render_all` |
| `pac-mania.mod` | focal | 879060 | PCM S16 mono, 22050 Hz |
| `real_pacmania_game.mod` | focal | 879060 | PCM S16 mono, 22050 Hz |

`ddragonz`, `double_dragon` e `real_ddragon_game` têm o mesmo SHA-256 de ROM; `pac-mania` e `real_pacmania_game` também. Resultados duplicados representam aliases, não jogos independentes.

## Codecs/caminhos observados

- PCM: CLSID `0x01005511`; `src/brew/imedia.c::media_set_data` -> `zaudio_play_pcm_ch` -> `src/audio/audio.c`/mixer -> `retro_run`.
- MP3: CLSID `0x01005502`; detecção por CLSID/conteúdo -> `zmp3_play` -> `zmp3_render_all`. Action Hero abriu quatro streams MP3 antes da janela terminar.
- WAV/IMA ADPCM: parser RIFF e formato `0x0011` presentes; nenhum caso concluído desta rodada comprovou saída não zero por esse caminho.
- MIDI: `zmidi_load_smf` -> `zmidi_play_smf` -> `zmidi_render`; nenhum caso desta rodada comprovou saída não zero.
- U8: cabeçalho observado em `zeebo_f.c._foot_camp`, mas o caso expirou antes de produzir resumo.

## Falsos negativos

A medição curta/paralela de 60 frames anterior marcou todos os jogos com zero. A repetição de 600 frames serial provou cinco falsos negativos/aliases:

- Action Hero: timeout paralelo; serial produz `audio_nonzero=881358`.
- Pac-Mania e alias real: zero em 60 frames; serial produz `879060`.
- Double Dragon real: crash na varredura paralela; serial produz `882000`.

Causa de medição: janela curta e contenção/timeout do runner, não falha transversal do mixer. O teste paralelo de 600 frames também gerou 39 timeouts e 15 crashes; esses 54 casos não podem ser classificados como silêncio.

## Cluster de silêncio corrigível

### Confirmado no pipeline

- Nenhum cluster transversal de codec/mixer foi comprovado.
- PCM e MP3 produzem áudio não zero no build atual.
- Não aplicar correção especulativa em `audio/imedia`.

### Candidatos, exigem desbloqueio/execução completa

- `alice_no_pais_das_maravilhas`: oito buffers PCM válidos carregados, mas timeout sem resumo; provável controle/callback, não mixer.
- `zeebo_sports_peteca` e `zeebo_sports_queimada`: PCM de apenas 118 bytes toca; timeout impede distinguir áudio curtíssimo de silêncio real.
- `ridge_racer`: tamanho PCM inválido `268440808` a partir de endereço baixo; provável `AEEMediaData`/ABI incorreto. Caso crashou antes do resumo.
- `zeebo_f.c._foot_camp`: buffers PCM/U8 carregados; timeout.
- Family Pack: nenhum objeto `IMedia` chega a tocar; bloqueio de boot/CreateInstance já documentado em `docs/FAMILYPACK_REGRESSION.md`, fora do pipeline de áudio.

## Decisão de código

Nenhuma alteração em `src/audio` ou `src/brew/imedia.c`. Evidência atual mostra os caminhos PCM e MP3 funcionais; uma mudança transversal seria insegura e poderia regredir os seis resultados positivos.

## Próximo teste recomendado

Executar serialmente, 600 frames, timeout >= 180 s, em lotes pequenos. Classificar silêncio somente quando existir `SMOKE-SUMMARY` com `audio_nonzero=0`; timeout/crash = inconclusivo. Instrumentar contagem por codec e primeiro frame não zero no runner antes de alterar emulação.
