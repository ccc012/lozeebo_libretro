# Family Pack — CPU fetch reuse v2 (2026-11-07)

**Branch:** `front/fp-performance-cpu-v2`  
**Base:** `c5ff608`

## Alteração

- `zcpu_step()` faz o único fetch ARM e passa `instr` aos recognizers HLE.
- `zcpu_run()` não faz fetch nem reconhecimento adicional.
- Thumb permanece fora dos recognizers; PC só muda depois de non-match; match preserva PC/ciclos dos recognizers.

## Validação

- Compilação de todos os fontes Release passou; link da solution falhou por 29 símbolos já ausentes no `.vcxproj` (`zmidi_render`, `zmp3_render_all`, `tinfl_decompress_mem_to_mem`, etc.).
- Benchmark 900f, PPM, áudio/input e inválidos não executados: binário Release não foi produzido pelo bloqueio de link.
- Macros de trace/watch não aplicadas nem medidas: sem baseline executável, empilhar a segunda alteração violaria medição isolada.

## Promoção

Não promover. Ganho >=20% não demonstrado.
