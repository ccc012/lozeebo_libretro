# Ovos: CPU/HLE probe cost

Base `bab121395337519e4aa556708216692fa4335afb`; correção `02bfac3` (documentação acrescentada depois). Worktree exclusiva `C:\Users\Lucas\AppData\Local\Temp\opencode\progress-ovos-performance`.

## Causa comprovada / diff

`src/cpu/cpu.c`: +7 linhas, filtros de opcode antes de lookahead em `try_hle_empty_count` e `try_hle_word_fill`. Chamados a cada instrução ARM; antes faziam respectivamente 2 e 4 leituras guest mesmo para opcodes impossíveis. Agora retornam imediatamente nesses casos. Nenhuma instrução/frame emulada removida; corpos HLE, mixer, áudio, GPU, budget e timers intactos. Não é frameskip nem correção de gameplay.

`tests/hle_probe_cost.py`: extrai os corpos reais das duas funções para harness C independente. Conta leituras; verifica ambos loops count em três posições de entrada e fill em duas posições, memória, registradores, flags, PC, contador executado. Sem ROM, sem dependência nova; requer Python, CMake/MSVC presentes.

## Red/green

```
python tests/hle_probe_cost.py
```

Executado antes da correção: `FAIL line 170: reads==0` (exit 1). Depois: `PASS: unmatched probes=0 guest reads; count/fill entry outputs preserved` (exit 0). Causalidade determinística: seis leituras especulativas viram zero por instrução ARM incompatível. Não há ainda contador de distribuição real dos opcodes de Ovos; não extrapolar economia total a partir do harness.

Build:
```
cmake -S . -B build-progress -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-progress --config Release --parallel 2
```

## Perfil e A/B/B/A

Primeiro perfil 120f: CPU guest 39097,360 ms; raster 8,523 ms; elapsed 39390 ms. Sob 15 agentes, comparação desse primeiro tempo com qualquer outro é inválida.

A/B/B/A consecutivo, um smoke por vez, mesmo executável, mesmo cenário 120 frames, mesma ROM/assets copiados para sandbox local `artifacts/ovos-performance/game/` (origem compartilhada somente leitura):

| Variante | elapsed ms | CPU guest ms | calls / budget solicitado |
|---|---:|---:|---|
| baseline-1 | 11187 | 11082,412 | 117 / 117000000 |
| fixed-1 | 8828 | 8709,683 | 117 / 117000000 |
| fixed-2 | 9921 | 9790,887 | 117 / 117000000 |
| baseline-2 | 12875 | 12774,217 | 117 / 117000000 |

Raster: 1,776–6,260 ms total. CPU guest inclui traps/render síncrono; não equivale a CPU interpreter puro. Restante fora do bloco CPU: 105–130 ms/120 frames (inclui áudio, callbacks e outros custos). Mixer isolado não instrumentado; não demonstrado gargalo de áudio neste trecho. QPC mede wall time, inclui preempção. A/B temporal sugere redução, porém carga externa não controlada: FPS e percentual de ganho **inconclusivos**. 30 FPS não atingidos/comprovados; áudio frontend sem travamento não comprovado.

Comando usado em cada variante (PowerShell; trocar `$dll`/`$tag`; baseline DLL preservada antes do build corrigido):
```
$env:ZEEMU_PROFILE_CORE='1'
$env:ZEEMU_SMOKE_MAX_SECONDS='55'
$env:ZEEMU_SMOKE_AUDIO_DUMP="artifacts\ovos-performance\$tag.pcm"
& .\build-progress\Release\libretro_smoke.exe $dll artifacts\ovos-performance\game\game.mod "artifacts\ovos-performance\$tag.ppm" 120 2> "artifacts\ovos-performance\$tag.log"
```

Todos quatro: exit 0, video_frames=120, video_first=FF000000, video_xor=006D94E0, varied=1, ever_varied=1, audio_frames=88200, audio_nonzero=3902, peak=32768. PCM integral capturado, não só contagem:
- SHA256 PCM (todos): `9DD7038FCE260CF54A55B2AD6ACE81EED7F75589FE96CA990B711A4BADD77990`.
- SHA256 PPM (todos): `C36446C8E6CCEDB23BF7EC025EA67E0F911CA807B85ED99A5878FB84885EB567`.
- Referências antes/depois: `artifacts/ovos-performance/baseline-1.ppm`, `fixed-1.ppm`, `fixed-2.ppm`, `baseline-2.ppm`; logs/PCM de mesmo prefixo. Não commitados.
- Não há prova de gameplay/tela reconhecível/input: somente equivalência dos primeiros 120 frames. CARREGANDO é outra frente; não duplicada.

## Regressões / pendências

`build-progress/Release/audio_adpcm_rate.exe`: PASS 22050/44100/88200 Hz, EOF, loop, batch continuity.
`build-progress/Release/input_transitions.exe`: `input transitions: OK`.

Suite global, capability gates, FP color gate e áudio real em frontend pendentes ao master. Sem alteração de hashes/thresholds. Validar ganho isolado depois de cessar concorrência; repetir cena posterior ao carregamento fornecida pela frente gameplay. Sem merge, integração ou deploy.
