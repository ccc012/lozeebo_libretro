# Handoff: Double Dragon Gameplay Verification

- Worktree: `C:\Users\Lucas\AppData\Local\Temp\opencode\progress-dd-gameplay`
- Base: `bab1213`
- Status: **Comprovado** (andar -> soltar para -> oposto -> combate/ação -> fase sustentada 1900 frames com áudio contínuo)

## 1. Causa comprovada e diagnóstico de bloqueios anteriores
- O teste anterior (`tools/dd-gate/run.ps1 -Playable`) injetava comandos em frames 560–1000 e media apenas hashes globais de PPM.
- Causa real identificada: até o frame 1160 o jogo está na cutscene de introdução (sequestro da Marian e capangas andando). Hashes diferentes decorriam da animação de fundo, e não de controle.
- A fase 1 inicia no frame 1180 com `TEMPO 60` na HUD.
- A base já continha as correções de soltura de input (`input_transitions` e neutral rearm) e taxa ADPCM; o bloqueio remanescente era a ausência de um witness automatizado no momento em que o guest aceita comandos jogáveis, sem confundir animação com controle.

## 2. Prova por estado do guest (sem hashes globais)
- Foi isolada a paleta do Billy Lee (`RGB=(0, 49, 156)` das calças, ausente no cenário e nos inimigos).
- Métricas rastreadas:
  - Frame 1240: x=230.7 (parado após spawn)
  - Frame 1280: x=280.8 (após comando RIGHT:1250:40)
  - Frames 1320–1380: x=301.5 a 301.7 (soltou RIGHT; parado completamente)
  - Frame 1420: x=232.5 (após comando oposto LEFT:1380:40)
  - Frames 1440–1480: x=231.4 a 232.3 (soltou LEFT; parado completamente)
  - Frame 1520: top=298 vs 268 (comando A:1500:6 executa pose de ação/soco)
  - Frames 1640–1680: x avança de 148.4 para 200.4 (retomada normal de movimento pós-combate)
  - Frames 1800–1900: fase sustentada (`TEMPO 56`), 1396452 samples de áudio contínuo, sem erros ou quedas.

## 3. Comandos red/green
- Auto-teste do verificador:
  `python tools/dd-gate/gameplay.py --self-test` (PASS)
- Red (controle negativo sem movimento):
  `python tools/dd-gate/gameplay.py --output artifacts/dd-negative-motion --sequence "START:340:8,START:420:8,RIGHT:1200:1,LEFT:1210:1,A:1500:6,A:1540:6,B:1580:6,RIGHT:1650:40"` (FAIL)
- Green (execução completa e sustentada):
  `python tools/dd-gate/gameplay.py --output artifacts/dd-gameplay-green` (PASS)
- Regressão do repositório:
  `python tools/regression.py --build-dir build-progress` (PASS)

## 4. Evidências visuais e dados
- Contact sheet da introdução: `artifacts/dd-progress/contact.png`
- Grade temporal do gameplay real: `artifacts/dd-progress/gameplay.png`
- Dados do jogador: `artifacts/dd-gameplay-green/player.json`
- Log de execução: `artifacts/dd-gameplay-green/smoke.log`

## 5. Limites conhecidos / handoff
- Verificação visual comprova pose de ação e retomada de controle; não analisa dano na barra de vida de inimigos ou conclusão de fase completa.
- Sem deploy, sem merge, sem modificação de hashes/regras do master.
