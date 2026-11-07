# Input transitions — fix/input-transitions, base 90c1179

## Causa comprovada

- `zboot_tick` escolhia pressed **ou** released. A release+B press deixava A ligado no consumidor guest.
- O retorno antecipado fora de BOOT_RUNNING perdia edges de polls seguintes enquanto callback/timer/thread executava.
- A supressao de callback no centro impedia FP observar neutral e rearmar a mesma direcao.
- `GetPositionState(po,pInfo)` tinha escrita extra em R2, inexistente na ABI. Reference `BrewHID.cpp:433-494` escreve apenas R1. FP 0x6ab4-0x6ac8 deixa R2 como objeto; DD tambem nao prepara output R2.

Captura agora ocorre em cada poll; callbacks apenas drenam em BOOT_RUNNING, apos capturar botoes **e** posicao. Cada notificacao de posicao preserva sua amostra, evitando held/neutral/held virar tres leituras do ultimo held durante guest ocupado. Sem debounce, sem mudanca de map_dd, CPU, audio ou rendering.

## Red/green runnable

```powershell
cmake -S . -B build-input -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-input --config Release --clean-first --parallel 6
foreach ($case in @('buttons','position','busy','abi','busy-position')) {
  & .\build-input\Release\input_transitions.exe $case
  if ($LASTEXITCODE -ne 0) { throw $case }
}
python tests/fp_position_transitions.py build-input/Release/lozeebo_libretro.dll 'D:\Temp\zeebo_all\Zeebo_Family_Pack__Brazil___Es_Pt_\mod\277229\game.mod'
powershell -File tools/dd-gate/check-directions.ps1 -BuildDir build-input
powershell -File tools/fp-gate/run.ps1 -BuildDir build-input -SkipBuild
```

Cinco casos C falharam no codigo base: `mask == ZKEY_B`, callback neutral ausente, `hid_evt_pop` vazio com guest ocupado, sentinel R2 sobrescrito, fila posicao vazia com guest ocupado. Todos passaram apos correcao. O teste inclui boot.c para acessar a costura real sem API publica de testes; demais fontes sao as mesmas do core. Assert continua ativo em Release.

Callback real FP lido antes da correcao: 0x5690 chama GetMin/GetMax e 0x6a84; esta consulta GetPositionState e grava eixos signed em user+0x154 (user 0x100121c0 na ROM fornecida). O teste Python executa ROM real, serializa memoria e verifica esses valores, nao hashes de video:

| frame | acao | guest x |
|---|---|---:|
| 299 | neutral | 0 |
| 300 | LEFT | -32768 |
| 302 | neutral | 0 |
| 330 | LEFT repetido | -32768 |
| 332 | neutral | 0 |
| 350 | RIGHT | 32766 |
| 352 | neutral | 0 |
| 370 | LEFT anterior | -32768 |
| 372 | neutral | 0 |

FP 400 frames com releases: sem descarrilamento/crash historico. DD gate 760 frames: press/release dos quatro UIDs entregues. FP gate 900 frames: PASS, 15172 ms, audio_nonzero=931855, SHA256 DC43FEA183E9930E6C6C24F1012680CBCC9CBD754CEFDA39BE98A31F258947C0 igual referencia. Baseline nao modificado.

## Limites

Prova FP cobre eixo recebido pelo callback real; nao afirma selecao de cada minigame. DD cobre transporte e mascara guest sintetica, nao deslocamento/parada do personagem medidos em gameplay; precisa confirmacao interativa. Filas existentes permanecem limitadas (32 sinais, 16 eventos HID): guest bloqueado indefinidamente ainda pode causar overflow. Save-state continua com limitacoes preexistentes de filas/estado de input; nenhum formato alterado. Teste Python usa enderecos da ROM FP fornecida, nao e generico entre versoes.

Artefatos locais em build-input/fp-proof.txt, build-input/fp-proof.log, build-input/fp-gate-result.txt e artifacts/dd-directions/input.log. build-input foi configurado novo nesta worktree. Nenhum deploy/push/merge.
