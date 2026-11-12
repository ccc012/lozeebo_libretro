# Frente: alice-play

**Objetivo:** Alice chegar em `IMedia_Play` após CreateDevice.  
**Estado:** blocked  
**Owner:** `imedia.c` (+ caminhos Alice pós-HID); `boot.c` intocado  
**Deps:** HID CreateDevice=1 OK; FP MP3/restart e AH Play preservados

## Feito

- `IHID_GetNextConnectEvent` 1 evento sintético (pré-existente).
- `ZINPUT_HID_POS_WORDS` 25→7 (pré-existente).
- Build exclusivo `build-smoke-alice-play`: OK.
- Alice real: CreateDevice=1, SetMediaData=28, Play=0.
- TTDMM `FALSE / 269` não para execução: asset reads e chamadas GL continuam.
- FP 600f exit=0 com MP3 + restart; AH 120f exit=0 com Play.

## Bloqueio

Alice 600f não conclui em 120 s por loop/lentidão guest pós-assets; sem correção segura em `imedia.c`. Forçar Play seria resultado inventado.

## Próximo

1. Perfilar PCs `0xBB***/0x5F***` com logging reduzido.
2. Determinar se há progresso de frames ou loop determinístico.
3. Só alterar caminho de mídia após observar chamada/contrato guest incorreto.

## Evidência

`agent-coord/handoffs/alice-play-20261106.md`

## NÃO

- `ZEEMU_STUB_THREAD=1`.
- Fake `HandleEvent(2)`.
- Auto-Play em `imedia.c`.
