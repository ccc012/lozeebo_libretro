# Frente: zuma-stable

**Objetivo:** Zuma 900f com SMOKE-SUMMARY limpo + cena estável.  
**Estado:** parked  
**Owner:** `cpu.c` (empty-count) / soft GL path  
**Deps:** tex2d=1 OK; HLE empty-count (não cap cega)

## Feito

- Soft Gen/Bind + TexImage2D
- HLE RGB565 upload
- `try_hle_empty_count` em loops 91B14/92C40
- 900f `varied=1` xor=0087A4D8

## Falta

1. SUMMARY estável sem EXIT=-1
2. Visual sprites/alpha
3. Não reintroduzir delay-cap cega
