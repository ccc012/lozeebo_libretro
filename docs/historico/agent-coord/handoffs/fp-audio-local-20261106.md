# Handoff fp-audio-local — 2026-11-06

## Resultado

- Build exclusivo `build-smoke-fp-audio`: OK.
- Family Pack 600f: exit 0; input A frames 120–129; 640x480 ativo.
- Family Pack 1800f: exit 0; input A frames 120–129; sem crash/travamento.
- Pipeline revisado: MP3 detectado antes de PCM; re-Play usa `zmp3_restart`; mono recebe upmix L/R; Stop encerra decoder; PCM stereo usa amostras intercaladas; loop respeitado no decoder/mixer.
- Nenhuma mudança em `src/`: comportamento requerido já está implementado. Alterar sem falha reproduzida seria regressão especulativa.

## Métricas

| Janela | Exit | Input | Vídeo | `audio_nonzero` |
|---:|---:|---|---|---:|
| 600f | 0 | A @120–129 | 640x480 | não exposto pelo harness atual |
| 1800f | 0 | A @120–129 | 640x480 | não exposto pelo harness atual |

Baseline comparável documentado em `docs/AUDIO_REGRESSION_CANDIDATES_20261106.md`: **489842/600f**, MP3 + restart, contra 493903 anterior. A diferença é janela/timing; baseline antigo incluía centenas de re-Play caindo incorretamente em PCM sujo. O caminho atual preserva stereo correto sem bitstream MP3 interpretado como PCM.

## Logs

- `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-audio-local\fp-600.log`
- `C:\Users\Lucas\AppData\Local\Temp\opencode\fp-audio-local\fp-1800.log`

## Limite

O smoke desta branch não contabiliza samples em `audio_batch`; portanto não inventa métrica nova. Para nova contagem, integrar contador no harness fora da ownership desta frente.
