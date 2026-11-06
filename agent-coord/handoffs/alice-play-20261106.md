# Handoff alice-play — 2026-11-06

## Resultado

- Alice ainda não chega a `IMedia_Play`.
- `IHID_CreateDevice=1`; `IMedia_SetMediaData done=28`; `IMedia_Play=0`.
- TTDMM `FALSE / 269 @ TTDMemoryManager.cpp` repete durante carga de assets, mas não interrompe execução: depois de cada assert aparecem `GenTextures`, `BindTexture`, `TexImage2D` e novos `IFile_Read`.
- Portanto o assert TTDMM não é, isoladamente, o bloqueio de `Play`; o guest continua carregando assets. O smoke ficou preso/lento e foi encerrado pelo limite externo após 120 s, sem `SUMMARY`.
- Nenhuma correção segura em `imedia.c`: todos os 28 objetos já recebem mídia válida antes do trecho lento. Forçar `Play` seria falso comportamento.

## Testes reais

Build exclusivo:

```text
cmake -S . -B build-smoke-alice-play -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-smoke-alice-play --config Release --parallel 4
Resultado: OK
```

| Teste | Resultado |
|---|---|
| Alice 600f solicitado | limite externo 120 s; processo encerrado; CreateDevice=1; SetMediaData=28; Play=0; TTDMM@269 repetido |
| Family Pack 600f | exit=0; CreateDevice=1; Play MP3 + restart |
| Action Hero 120f | exit=0; 20 linhas `IMedia_Play:` |

Logs:

- `C:\Users\Lucas\AppData\Local\Temp\opencode\alice-play\alice-baseline-600.log`
- `C:\Users\Lucas\AppData\Local\Temp\opencode\alice-play\fp-600.log`
- `C:\Users\Lucas\AppData\Local\Temp\opencode\alice-play\ah-120.log`

## Limites

- Harness atual não imprime `audio_nonzero`; FP preserva o caminho MP3/restart, mas o número `489842` não foi mensurado nesta execução.
- Próximo diagnóstico deve perfilar o loop guest `0xBB***/0x5F***` ou reduzir logging para verificar progresso de frames. Não alterar `imedia.c` sem chamada guest real.
