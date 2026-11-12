# Unificação física — 2026-11-12

## Duas raízes

- **Notas pessoais, somente `.md`:** `G:\Meu Drive\Documentos obi\Projeto\Emulador`.
- **Projeto concreto e Git principal:** `C:\Users\Lucas\source\repos\zeebo_libretro`.

Esta etapa reúne pastas e copia notas; não consolida textos, não integra branches e não escolhe automaticamente entre versões divergentes.

## Organização do projeto

O código principal continua em `src`, `tests`, `tools`, com seu `.git` original. Documentos necessários ao repositório continuam nele e possuem cópia pessoal no Obsidian.

`arquivo_local/` é ignorado pelo Git principal e contém:

| Local | Conteúdo |
|---|---|
| `worktrees/` | Cinco worktrees antigas Zeebo, movidas com Git |
| `repositorios/lozeebo_libretro/` | Clone independente, com seu próprio histórico e arquivos locais |
| `copilot-worktrees/lozeebo_libretro/` | Worktree Copilot desse clone, ponteiros Git reparados |
| `referencias/zeemu_reference/` | Referência Zeemu |
| `retirado_do_obsidian/` | Todos os arquivos não Markdown retirados das notas: backups, bundle, bancos, scripts, executável, objetos, imagens, logs, manifests e metadados |
| `unificacao_2026-11-12/` | Manifestos, hashes, estados Git anteriores/posteriores e verificação |

Os repositórios e worktrees internos não foram fundidos: versões e alterações locais continuam separadas, agora sob uma só raiz física. Pastas de outros projetos em `copilot-worktrees` não fazem parte desta operação.

## Organização do Obsidian

- Notas pessoais existentes mantêm os caminhos.
- `16_Copias_Projeto/<origem>/`: cópia byte a byte de todos os `.md` das raízes reunidas, inclusive documentação de terceiros fora de metadados `.git`.
- Caminhos relativos preservados; nomes iguais de origens diferentes não se sobrescrevem.
- `.space`, bancos Ghidra e backups binários saíram do Obsidian. Nenhum arquivo não Markdown deve permanecer nesta pasta.
- Imagens e links para anexos/código podem não abrir na cópia Markdown isolada; arquivos continuam preservados no projeto. Revisão de links/textos será uma etapa posterior.
- As cópias não possuem sincronização automática. Documentação do Git continua sendo editada no projeto; notas pessoais são cópias para revisão.

## Limites e cuidados

Scripts, caches de build e documentos antigos podem conter caminhos absolutos anteriores. Git foi reparado nas worktrees movidas; caminhos internos de ferramentas não foram reescritos indiscriminadamente. Regenerar builds quando necessário.

Registros anteriores ao movimento conservam caminhos antigos como evidência histórica. Em particular, o pacote antes localizado em `15_Encerramento` está dividido: `.md` continuam no Obsidian; demais arquivos estão em `arquivo_local/retirado_do_obsidian/15_Encerramento`. Seus manifestos antigos descrevem o estado anterior, não o destino atual.

Não publicar `arquivo_local`: contém backups, ROMs comerciais e referências sujeitas a licenças distintas. Nenhum commit/push, exclusão de branch, reset ou fusão de código foi autorizado nesta etapa.
