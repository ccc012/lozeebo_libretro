# Organização completa do projeto — 2026-11-12

## Escopo e método

Somente `C:\Users\Lucas\source\repos\zeebo_libretro`; Obsidian não alterado nesta etapa. Inventariados 930 arquivos antes da reorganização, excluindo metadados internos de `.git`. Conteúdo/estrutura revisados por área; binários e bancos Ghidra tratados como opacos, não como código auditado. Isto não constitui revisão funcional linha a linha de todo o emulador.

Inventário com tamanhos, SHA-256, formato e contagem de linhas textuais: `preservacao/registros/organizacao/inventario_antes.json`. Mapa completo de 83 movimentações: `movimentacoes.json` nessa pasta. Lista de 33 objetos/caches excluídos: `gerados_removidos.json`.

## Raiz: somente entrada e build canônico

- `README.md`: resumo atual, mapa, comandos e restrições de publicação.
- `LICENSE`: GPL-3.0 do projeto.
- `CMakeLists.txt`, `CMakePresets.json`: configuração de build; CMake é canônico.
- `.gitignore`, `.gitattributes`: regras de versionamento.
- `.github`: templates de colaboração existentes.
- `.git`: histórico mantido, não reorganizado internamente; contém material externo antigo que exige saneamento antes de publicação.

Builds regenerados ficam em `build/`, ignorados pelo Git. O schema dos presets requer CMake compatível; o mínimo 3.10 declarado no projeto não garante suporte aos presets. Comandos `-S/-B` foram usados na verificação desta organização.

## Código: arquivos mantidos no contexto correto

79 arquivos próprios/vendorizados originais revisados em `src/`; um header necessário foi recuperado do Git, totalizando 80. Sem alteração de lógica de emulação:

| Subsistema | Responsabilidade |
|---|---|
| `audio` | PCM, ADPCM, MP3, MIDI e mixer |
| `audio/extern` | dr_mp3, TinySoundFont e TinyMidiLoader, com notices originais |
| `brew` | Boot, helpers e interfaces BREW/AEE |
| `core` | Integração e contrato libretro |
| `cpu` | ARM/Thumb, decode e flags |
| `debug` | Log, trace, disassembly, métricas |
| `gpu` | Framebuffer, desenho e EGL/GLES |
| `input` | Entrada e transições |
| `extern/stb` | `stb_image.h`, dependência real da decodificação de imagens |
| `loader` | MOD/MIF/BAR, ZIP, pacotes e PAKZ |
| `memory` | Mapa de memória e heap |

Os dez arquivos miniz continuam em `src/audio` por dependência histórica dos includes, embora sejam descompressão compartilhada. Só `miniz_tinfl.c` é compilado; os demais não foram excluídos por aparência de inutilidade. Mover o conjunto exigiria ajustar consumidores e build, sem benefício funcional nesta etapa.

`idisplay.c` e `idisplay_real.c` não são cópias descartáveis. Ambos participam do build. Arquivos PAKZ locais não commitados foram mantidos.

## Testes

- Fontes C e harnesses Python em `tests/`.
- `test_gles_transform.c` também é incluído por `gles_composition.c`; não é órfão.
- `pakz_reader.c` possui alvo CMake e execução manual, mas ainda não está no agregador de regressão.
- `test_cpu.c` e `test_memory.c` são placeholders históricos, não testes efetivos; mantidos como registro, sem reivindicação de cobertura.
- `tests/roms`: política e fixture sintética, sem jogos comerciais.
- Não há suíte CTest registrada; usar executáveis e runners documentados.

## Ferramentas

`tools/regression.py` agrega testes; `REGRESSION.md` descreve seus limites. Gates FP/DD/Ovos/capability mantêm posições porque calculam raiz por profundidade e usam baselines locais. `baseline.json` e `evidence180.json` permanecem junto dos consumidores.

`fp-visual-nav/evidence` contém 13 arquivos de evidência histórica: imagens, classificações e resumo. Não são uma duplicata de jogos nem baseline principal. Mantidos no contexto original para preservar proveniência e links; não são saídas novas desta organização.

`smoke-all.ps1` e `summarize-survey.py` são levantamento/relatório auxiliares. Caminhos pessoais antigos podem requerer ajuste explícito; não restaurar ROMs no repositório para satisfazê-los.

### Legado separado

- `tools/legado/build`: Makefile, solução/projeto Visual Studio e configuração IDE antigos. Omitiam módulos atuais mesmo antes da movimentação; não funcionam como substitutos certificados do CMake.
- `tools/legado/scripts`: cinco receitas antigas de build/teste/survey. Caminhos originais e comandos ficaram históricos; algumas copiam DLL automaticamente para RetroArch. Não executar como fluxo atual.

## Documentação

- `docs/INDEX.md`: mapa temático.
- `docs/historico/raiz`: 30 documentos de análise, status, prompts e metodologia anteriormente soltos.
- `docs/historico/agent-coord`: conjunto de coordenação/handoffs, incluindo script histórico.
- Demais documentos técnicos preservam os nomes, datas e escopos de evidência.
- Links Markdown locais foram recalculados nas movimentações identificadas. Caminhos absolutos, comandos e citações históricas não foram convertidos em instruções atuais. Links preexistentes para recursos removidos podem continuar apenas como registro histórico.

Os catálogos antigos não descrevem necessariamente a estrutura vigente; esta página e o README prevalecem. Não houve consolidação semântica de relatórios de épocas distintas.

## Distribuição e preservação

`dist/info/zeebo_libretro.info` é metadado do frontend, não binário compilado.

`preservacao/README.md` descreve patches, backup anterior, bancos Ghidra completos, scripts, seleção de fontes de referência e registros. Pasta privada ignorada; não deve ser publicada automaticamente. Removidos 32 objetos `.o` do backup e um cache Python. Bancos, `.gpr` vazios, `.rep` e índices mantidos como conjuntos.

A seleção `zeemu_reference` não é projeto independente completo e não inclui prova suficiente de licença de redistribuição. Não tratá-la como dependência distribuível ou código automaticamente incorporável.

## Verificação repetível

```powershell
python tools/check-layout.py
python tools/regression.py --self-test
cmake -S . -B build/check -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build/check --config Release
```

Verificações de layout e testes sintéticos não substituem testes de jogos. Sem commit/push, sem exclusão de branches ou saneamento de histórico. Sem alteração do Obsidian.

## Resultado da verificação

Build Release completo passou em Windows/MSVC. O primeiro build detectou include oculto de `stb_image.h` na árvore `reference/` anteriormente removida: recuperado exatamente do Git para `src/extern/stb/`, include de `iimage.c` corrigido, notice registrado em `THIRD_PARTY.md`. Nenhuma árvore externa completa foi restaurada.

Passaram seis verificações: layout, self-test do runner, ADPCM, input, composição GLES e PAKZ. Saídas/códigos em `preservacao/registros/organizacao/testes.json`. Avisos C4996 existentes permanecem. Sem regressão comercial ou validação multiplataforma.
