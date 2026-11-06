# Frente: alice-play

**Objetivo:** Alice chegar em `IMedia_Play` após CreateDevice.  
**Estado:** parked  
**Owner:** `imedia.c` (+ softfloat/TTDMM se necessário); `boot.c` HID já tem synthetic connect — evitar reeditar  
**Deps:** HID CreateDevice=1 OK; FP áudio ~489k não regredir

## Feito

- `IHID_GetNextConnectEvent` 1 evento sintético
- `ZINPUT_HID_POS_WORDS` 25→7 (stack Alice)
- CreateDevice=1; Play=0; host -1 pós-TTDMM

## Falta

1. Diagnóstico TTDMM / softfloat `0xBB***` pós-CD
2. Caminho asset/heap até Play
3. Smoke Alice 600f com Play≥1; FP 600f audio~489k

## NÃO

- `ZEEMU_STUB_THREAD=1` (mata CreateDevice)
- Fake HandleEvent 2
