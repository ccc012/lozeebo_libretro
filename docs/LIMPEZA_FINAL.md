# Limpeza final — 2026-11-12

## Objetivo

Checkout compacto de código e pesquisa. Exclusão definitiva de jogos locais, extrações, clones repetidos, builds, bancos/metadados de ferramenta dispensáveis e capturas volumosas. Histórico Git mantido por decisão explícita do autor; sem commit, push forçado ou reescrita.

## Estrutura resultante

- `src/`: implementação do core.
- `tests/`: fontes de testes e fixture sintética; sem os quatro MODs comerciais.
- `tools/`: runners e baselines; provas históricas que dependiam de capturas externas não são integralmente reproduzíveis sem novas capturas.
- `docs/`: documentação técnica e registros de encerramento.
- `agent-coord/`: coordenação histórica, mantida para referências cruzadas.
- `preservacao/alteracoes_locais/`: patches e arquivos únicos de versões paralelas, separados por origem. Não são código integrado nem novas funcionalidades validadas.
- `preservacao/backup_codigo_20261106/`: backup anterior de código/patch, sem jogos.
- `preservacao/pesquisa_infuse/`: scripts, exports e projeto Ghidra completo; executável Infuse não preservado nesta limpeza.
- `preservacao/projetos_ghidra_adicionais/`: projetos de análise encontrados no clone, preservados com seus bancos.
- `preservacao/zeemu_reference/`: pequena referência em fontes, não cópia completa de dependências e assets.
- `preservacao/levantamento/`: scripts/resultados de survey.
- `preservacao/resultados/`: sumários leves das execuções históricas; imagens e logs volumosos removidos.
- `preservacao/registros/`: proveniência das movimentações anteriores. Caminhos desses manifestos são históricos e podem não existir após esta limpeza.
- `.git/`: histórico preservado, ainda contendo versões antigas de jogos/material externo. Não publicar sem saneamento separado.

## Material excluído

- `arquivo_local/`: snapshots físicos reunidos, romsets, extrações, backups redundantes, bundle repetido, referências copiadas, builds antigos e metadados `.space`; somente itens únicos selecionados foram extraídos antes.
- `reference/`: árvores de terceiros e suas dependências/binários/assets repetidos. Avisos de dependências efetivamente incluídas em `src/` permanecem nos respectivos headers e em `THIRD_PARTY.md`.
- `artifacts/`: capturas de tela e logs repetidos, após selecionar resultados leves.
- `build-archive-check/`: build regenerável.
- `..zeebo_libretro-clsid-lote1/`: snapshot aninhado; alteração local exportada.
- `tests/roms/real_*_game.mod`: quatro jogos comerciais removidos do checkout.

Escopo exato e tamanhos: `preservacao/exclusoes_2026-11-12.json`.

## Proteções

Antes de excluir snapshots: exportados HEAD/status/patches e arquivos de código/texto modificados ou não rastreados de 21 repositórios. Checkpoint Copilot exclusivo importado como `preservacao/copilot-checkpoint-20261112`, sem sobrescrever branches existentes. Projetos Ghidra preservados como conjunto `.gpr` e `.rep`.

Patches de preservação focam código/documentação, não fixtures comerciais ou binários de terceiros. A presença de um patch não comprova que sua implementação é necessária ou correta; revisar antes de aplicar.

Não houve integração funcional de versões. Builds e suítes com ROMs externas não foram executados novamente por esta limpeza. Verificações anteriores estão em `ENCERRAMENTO.md`.

## Documentação histórica

Este registro prevalece sobre instruções anteriores de manter quarentena/bundle/snapshots. `ENCERRAMENTO.md`, `UNIFICACAO.md` e `REVISAO_DISCO_D.md` registram etapas anteriores. As notas pessoais e cópias Markdown continuam no Obsidian; não foram excluídas nesta limpeza. Consolidação semântica de todos os textos é trabalho separado.

## Publicação

Remover jogos do checkout **não remove os blobs de commits anteriores**. O histórico não está liberado para redistribuição. A GPL do core não autoriza distribuir material externo. Licenças de assets e dependências exigem avaliação própria. Não houve conclusão jurídica de que possuir cópias locais constitui pirataria.

## Reutilização

Para build limpo, usar CMake conforme `ENCERRAMENTO.md`; saída regenerável ignorada pelo Git. Para regressão, fornecer fixtures autorizadas externamente. Alguns runners ainda referenciam caminhos históricos: ajustar deliberadamente, não restaurar jogos dentro deste repositório.

## Resultado conferido

Exclusoes liberaram **11,282 GiB**. Pasta resultante: **695,74 MiB**, incluindo .git (**486,90 MiB**) e preservacao privada (**201,69 MiB**). Apenas fixture sintetica 	ests/roms/test_draw.mod permanece entre arquivos de jogo/pacotes pesquisados. HEAD preservado; git diff --check passou; python tools/regression.py --self-test passou. Sem novo build completo ou validacao comercial apos limpeza.
