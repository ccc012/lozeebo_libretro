# Handoff – Family Pack selection/minigames

## Resultado
- **FP‑final‑playable** (`front/fp-final-playable-proof`) **PASS**
  - Video varied = 1, audio nonzero ≈ 928 k (above baseline)
  - Frame 900 ≈ 78 s, no errors, menu and minigame recognizáveis
- **Prova de estado** (`artifacts/fp-gameplay/state/sheet_post600.png` etc.) mostra menu → seleção → minigame com gráficos corretos.

## Evidência de teste
- Comando usado:
```powershell
cmake -S . -B build-fp-final -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-fp-final --config Release
powershell -File tools\fp-final-playable-proof\run.ps1
```
- Saída resumida (`smoke.log`):
```
[SMOKE-TIMING] frames=900 elapsed_ms=78438
[SMOKE-SUMMARY] video_frames=900 video_first=0x00000000 video_xor=... video_varied=1 video_ever_varied=1 audio_frames=... audio_nonzero=928840 audio_peak=30456
```
- SHA‑256 do frame final: `0fca80b15a5d54df…` (verificado no relatório).

## Próximo bloqueio concreto
Nenhum bloqueio restante dentro da worktree **progress‑fp‑gameplay**. As próximas etapas são:
1. Integrar a mudança de `front/fp-final-playable-proof` ao branch `master`.
2. Atualizar a documentação de **Family Pack** em `docs/FAMILYPACK_REGRESSION.md` com a nova evidência.
3. Verificar performance nas outras mini‑games (Long John, Zeeboids) em futuros worktrees.

*Conforme as regras de `PROGRESS_RULES.md`, nenhuma alteração fora do escopo foi feita. Commits futuros devem seguir o mesmo padrão de mínima alteração e teste red/green.*