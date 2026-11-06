# Handoff — fp-flow-local — 2026-11-06

## Resultado

- Build Release OK.
- Nenhuma mudança funcional segura; experimento revertido; `boot.c` limpo.
- Family Pack 600f preto, sem menu/minigame/gameplay; 1800f omitido por não adicionar evidência.

## Causa isolada

`Detach` invalida o signal HID antes do disparo. Ignorar `Detach` faz `DeviceConnectCB` executar, porém o callback usa objeto guest já destruído; guest entra em loop `0xEA000017` / `0xEA000013` até watchdog.

## Correção necessária

Unificar ownership/refcount/lifetime do signal HID e objeto callback. Não ignorar `Detach`; não disparar callback com objeto destruído; não fake `HandleEvent(2)`.

## Próxima frente

`fp-signal`: ownership conjunto da seção HID + signals/refcount em `boot.c`, com teste do ciclo attach/detach/fire/release.

## Commit da frente

`c11aefc46d4b2783a55645e85978e77c2ecbb409` (commit vazio; diagnóstico entregue externamente e consolidado aqui).
