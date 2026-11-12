# Revisão do disco D: — 2026-11-12

## Escopo aprovado

Reunir somente arquivos do projeto e temporários de desenvolvimento em `C:\Users\Lucas\source\repos\zeebo_libretro\arquivo_local\origem_D`. Coleção pessoal, instalações RetroArch, saves, configurações, DATs e temas ficam em D: por decisão explícita do autor.

Busca por nomes/caminhos relacionados a Zeebo, LOZeebo, Zeemu, OpenZeebo, Infuse, zbrew e Zeeboids; leitura dos vínculos `.git`. Não é busca textual pelo conteúdo de todos os arquivos nem garantia de identificar arquivos renomeados sem relação aparente.

Examinados: **366.647 arquivos, 47.393 pastas**. Resultados brutos: 521, incluindo itens internos de árvores e falsos positivos (mod Infused de outro jogo).

Áreas inacessíveis: `D:\WindowsApps`, `D:\System Volume Information`, uma subpasta da Lixeira. Não houve elevação de privilégios ou remoção nessas áreas.

## Material selecionado

- `D:\zeebo-worktrees`: 13 diretórios; 12 vínculos Git válidos, vários com alterações locais; `fp-video` com vínculo previamente quebrado, preservado como snapshot.
- `D:\backups\zeebo_libretro_20261106`.
- `D:\Temp\zeebo_all`, `D:\Temp\zeebo_extract`.
- Scripts `parse_sweep.py`, `sweep.ps1`, `sweep2.ps1`.
- Resultados `zeebo_sweep.csv`, `zeebo_sweep2.csv`.
- Logs `extract3734.log`, `ferrari_pack.log`, `ferrari_test.log`.

## Preservado fora do projeto por decisão do autor

- `D:\Games\Zeebo - Zeebo`: coleção pessoal.
- Pastas/arquivos Zeebo/LOZeebo na instalação `D:\SteamLibrary\steamapps\common\RetroArch`: ROMs, cores, saves, states, configurações, cheats, playlists, logs, backups e assets.
- DATs Zeebo em `D:\rv`.
- Temas/assets compartilhados do Steam Workshop.

Portanto, após esta etapa haverá material relacionado ao console fora da pasta concreta; isso é deliberado, não omissão da busca.

## Evidências e verificação

Inventário e resultados da transferência ficam exclusivamente no projeto:

`arquivo_local/inventario_d_2026-11-12/`

- `busca_nomes.json`: busca e erros.
- `worktrees_antes.json`: HEAD, status e vínculo original de cada pasta.
- `movimentos_d.json`: origem/destino, tamanho e SHA-256.
- `copias_md_d.json`: notas copiadas por origem ao Obsidian.
- `git_destinos_verificados.json`: validação Git das worktrees transferidas.
- `concluido.json`: criado somente após retirada das origens selecionadas.

As cópias Markdown ficam em `16_Copias_Projeto/origem_D`, no Obsidian. Não há fusão de código, integração de branches, commit ou push. Scripts e caches podem conservar caminhos absolutos antigos; revisar antes de executar. Material comercial e referências não devem ser publicados indiscriminadamente.
