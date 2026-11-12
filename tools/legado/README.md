# Ferramentas históricas — não usar como build atual

`build/` contém Makefile e projeto Visual Studio antigos, incompletos em relação ao CMake atual. Foram arquivados, não adaptados nem certificados para compilar desta localização.

`scripts/` preserva receitas antigas. Dependem de diretório corrente, caminhos absolutos e builds removidos; alguns instalam automaticamente DLLs no RetroArch. Não executar como fluxo recomendado. Chamadas internas e caminhos originais permanecem históricos, não portáveis após movimentação.

Build atual: `CMakeLists.txt` na raiz. Suíte: `tools/regression.py`. Estes arquivos são referências de pesquisa, não alternativas atuais.
