# Encerramento e preservação — 2026-11-12

## Decisão

Desenvolvimento encerrado por decisão do autor. Motivo informado: surgimento de outras opções de emulação que atendem ao seu objetivo. A cobertura dessas opções não foi verificada nesta organização. Este projeto fica como pesquisa e código histórico; não como produto completo ou suporte ativo.

Este documento substitui as indicações de desenvolvimento ativo dos documentos antigos. Roadmaps, tarefas, prompts e registros de agentes são históricos, não compromissos futuros.

## O que foi preservado

- Interpretador ARM/Thumb, memória, loader, BREW HLE, gráficos, áudio e integração libretro.
- Testes, runners, baselines, logs, imagens, diagnósticos, engenharia reversa e licenças.
- Todas as branches/tags, commits, remotos e registros de worktrees. Nenhum reset, commit, push, prune ou reescrita de histórico.
- Alterações locais anteriores, inclusive implementação PAKZ não commitada e alteração no repositório aninhado `..zeebo_libretro-clsid-lote1`.
- Projetos Visual Studio manuais em `zeebo_libretro/`; não são equivalentes ao CMake atual.

## Estado técnico: não confundir épocas

O README e `docs/TESTING.md` descrevem principalmente julho de 2026. Relatórios de novembro registram avanços posteriores. `docs/PROGRESS_dd_gameplay.md` registra evidência de movimento/ação em Double Dragon; relatórios FP registram provas específicas de navegação/minigame. Não há declaração de catálogo completo jogável, nem nova validação manual de jogos neste encerramento.

Para navegar: [catálogo completo](CATALOGO_ENCERRAMENTO.md), [índice documental](INDEX.md), [regressão](../tools/REGRESSION.md), [terceiros](THIRD_PARTY.md).

## Verificações executadas neste encerramento

Ambiente: Windows, Visual Studio 2022/MSVC 19.44, CMake, Python 3.15.

```powershell
cmake -S . -B build-archive-check -DZEEBO_BUILD_SMOKE_TEST=ON
cmake --build build-archive-check --config Release
python tools/regression.py --self-test
.\build-archive-check\Release\audio_adpcm_rate.exe
.\build-archive-check\Release\input_transitions.exe
.\build-archive-check\Release\gles_composition.exe
.\build-archive-check\Release\pakz_reader.exe
```

Todos passaram. Build com avisos C4996 existentes, sem correções funcionais. DLL: `build-archive-check/Release/lozeebo_libretro.dll`. O build novo foi mantido localmente, ignorado pelo Git.

**Não executado:** regressão completa com ROMs, gates de gameplay, RetroArch manual, Android, web, outros sistemas. Testes de pacotes que exigem argumentos/fixtures não foram executados isoladamente. Os resultados acima não provam compatibilidade geral. CTest não está configurado como suíte neste CMake.

Para reproduzir a suíte existente, em ambiente com fixtures autorizadas:

```powershell
python tools/regression.py --build-dir build-archive-check
```

Leia antes `tools/REGRESSION.md`. Requer ROMs locais, 7-Zip e caminhos específicos. Não rode suítes simultâneas. `--contracts-only` é parcial; `--skip-build` pode usar binário antigo. Evite scripts de build legados: alguns copiam automaticamente para RetroArch.

## Organização realizada

Documentos e scripts históricos mantêm caminhos para preservar links e receitas. A organização documental é por catálogo, não por movimentação indiscriminada.

Builds antigos `build-fp-cpu-v2-validate`, `build-fp-gate`, `build-release-deploy`, `build-smoke`, `build-test-head` foram movidos integralmente para quarentena externa; isso inclui o probe manual dentro de um build. Os arquivos gerados anteriormente rastreados aparecerão como exclusões **não staged**. Não foram apagados; revisar essas exclusões antes de eventual commit.

O `.gitignore` continha uma cauda com NULs e uma regra ampla aparente `*.txt`. A cauda foi substituída por regras UTF-8 para builds locais e sumário de smoke; fontes e documentos `.txt` não são ignorados genericamente. Regras antigas de referências não retiram arquivos já rastreados.

## Preservação externa e recuperação

Documentação de planejamento e pesquisa:

`G:\Meu Drive\Documentos obi\Projeto\Emulador`

Pacote de preservação:

`G:\Meu Drive\Documentos obi\Projeto\Emulador\15_Encerramento`

Conteúdo:

- `inventario_antes.json`: caminhos e tamanhos de ambos os locais antes da limpeza.
- `movimentacoes.json`: origem, destino, tamanho e SHA-256 de cada arquivo movido.
- `quarentena/`: arquivos originais; não excluir antes de revisão.
- `backup_local_2026-11-12/`: cópia dos arquivos previamente modificados/não rastreados; `.gitignore` original incluído.
- `diff_antes.patch`, `index_antes.patch`, estados/branches/worktrees/remotos e lista do índice anterior.
- `historico_git_2026-11-12.bundle`: objetos/refs alcançáveis do repositório principal; não inclui working tree, arquivos ignorados, reflogs nem histórico completo dos repositórios aninhados.
- `testes_2026-11-12.json`: saídas e códigos dos cinco testes executados.
- `verificar_preservacao.py`: verificação somente leitura dos hashes e do código preservado.

Para restaurar um arquivo movido: localizar sua entrada no manifesto, confirmar o destino e copiar de volta para a origem **somente se ela estiver ausente**. Não sobrepor trabalho posterior. Builds CMake antigos podem conter caminhos absolutos inválidos; preferir regenerá-los. O pacote externo não é backup independente contra falha/perda de conta do Drive.

Um bundle pode ser inspecionado com `git bundle verify <arquivo>` e clonado para uma pasta nova com `git clone <arquivo> <pasta-nova>`. As alterações locais continuam dependendo do backup separado. Não aplicar patches cegamente sobre este checkout.

## Git: resultado deliberadamente revisável

Branch preservada: `wip/fp-cpu-gate`; HEAD inicial `360882de53ccbd2aa6b405d147bb1111dec820e1`. Remoto preservado: `https://github.com/ccc012/lozeebo_libretro.git`.

Branches paralelas não foram integradas nem removidas. Worktrees marcadas como prunable não foram limpas; suas branches podem conter trabalho exclusivo. Repositórios aninhados/gitlinks continuam no lugar. Sem commit ou push, conforme solicitado; arquivamento no GitHub também não foi solicitado/executado.

## Bloqueio antes de publicação

**Não publique o bundle/quarentena nem faça push indiscriminado.** O índice atual já contém quatro MODs comerciais em `tests/roms`, árvores externas, assets/fontes sonoras, bibliotecas e binários. `docs/THIRD_PARTY.md` contém afirmações históricas de que algumas referências não eram versionadas, contraditas pelo inventário atual. `.gitignore` não resolve isso.

A licença GPL do código do projeto não concede direitos sobre jogos e material externo. Necessários antes de redistribuir: revisão de licenças por componente/asset, decisão sobre remoção do snapshot e eventual saneamento do histórico. Nenhum direito de redistribuição foi presumido. Nenhum histórico foi alterado para esconder material.

## Limites

Inventário não equivale a auditoria funcional linha a linha nem parecer jurídico. Bancos Ghidra, `.space`, licenças, ROMs, evidências e referências foram preservados. Nada foi definitivamente excluído. Não há continuação automática do desenvolvimento.
